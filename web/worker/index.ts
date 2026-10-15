/// <reference types="@cloudflare/workers-types" />

// The Rodeo site is static except for one thing: the waitlist.
//
// Routing is asset-first. Cloudflare serves a matching static asset without
// ever invoking this script, which is why `_headers` still applies to every
// page — custom headers are not applied to responses a Worker generates. Only
// requests that match no asset reach `fetch()` below, and the single path that
// matters is SUBSCRIBE_PATH.
//
// The one exception is /api/*, which wrangler.jsonc routes Worker-first. That
// is not a stylistic choice: without it the asset layer sees a form POST's
// `Sec-Fetch-Mode: navigate`, tries to serve the 404 page, and returns 405
// before this script runs. curl does not send that header, so the endpoint
// tests clean from a terminal and fails in a browser.

export interface Env {
  /** Static-asset binding; serves everything this script does not handle. */
  ASSETS: Fetcher;
  /** Buttondown API key. `wrangler secret put BUTTONDOWN_API_KEY`. */
  BUTTONDOWN_API_KEY?: string;
  /** Turnstile secret. When unset, verification is skipped — see verifyTurnstile. */
  TURNSTILE_SECRET?: string;
  /** '1' on preview versions: validate everything, call no provider. */
  DRY_RUN?: string;
}

const SUBSCRIBE_PATH = '/api/subscribe';
const THANKS = '/subscribe/thanks/';
const FORM = '/subscribe/';

const BUTTONDOWN_URL = 'https://api.buttondown.com/v1/subscribers';
const TURNSTILE_URL = 'https://challenges.cloudflare.com/turnstile/v0/siteverify';

/** Error codes the /subscribe/ page knows how to render. */
type Problem = 'email' | 'captcha' | 'server';

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === SUBSCRIBE_PATH) {
      return subscribe(request, env, url.origin);
    }

    return env.ASSETS.fetch(request);
  },
} satisfies ExportedHandler<Env>;

async function subscribe(request: Request, env: Env, origin: string): Promise<Response> {
  if (request.method !== 'POST') {
    return new Response('Method not allowed', {
      status: 405,
      headers: { Allow: 'POST', 'Cache-Control': 'no-store' },
    });
  }

  let form: FormData;
  try {
    form = await request.formData();
  } catch {
    return back(origin, 'email');
  }

  // The honeypot is off-screen, labelled, aria-hidden and out of the tab
  // order, so a human filling it in is close to impossible. A bot that does
  // gets a success page and no subscription: telling it why would just let it
  // adapt.
  if (str(form.get('b_name'))) {
    return seeOther(origin + THANKS);
  }

  const email = str(form.get('email')).trim();
  if (!isEmail(email)) return back(origin, 'email');

  // Recorded in logs only — it never leaves this account. Buttondown's tags
  // and metadata are plan-gated, and a 403 on a tag would fail a signup to
  // record where it came from.
  const source = str(form.get('source')).slice(0, 40) || 'unknown';

  const ip = request.headers.get('CF-Connecting-IP') ?? undefined;

  if (env.TURNSTILE_SECRET) {
    const ok = await verifyTurnstile(env.TURNSTILE_SECRET, str(form.get('cf-turnstile-response')), ip);
    if (!ok) return back(origin, 'captcha');
  } else {
    // Loud on purpose. Without the secret the only spam defence left is the
    // honeypot plus Buttondown's own IP-based check.
    console.warn('subscribe: TURNSTILE_SECRET unset — captcha not verified');
  }

  // Preview versions validate the whole path and stop here, so a PR preview
  // can never write to the real list.
  if (env.DRY_RUN === '1') {
    console.log(`subscribe: dry run (source=${source})`);
    return seeOther(origin + THANKS);
  }

  if (!env.BUTTONDOWN_API_KEY) {
    console.error('subscribe: BUTTONDOWN_API_KEY unset');
    return back(origin, 'server');
  }

  let res: Response;
  try {
    res = await fetch(BUTTONDOWN_URL, {
      method: 'POST',
      headers: {
        Authorization: `Token ${env.BUTTONDOWN_API_KEY}`,
        'Content-Type': 'application/json',
        // Without this an address already on the list is a 400. With it, a
        // repeat signup succeeds and a previously-unsubscribed address is sent
        // a fresh confirmation rather than being silently re-added.
        'X-Buttondown-Collision-Behavior': 'add',
      },
      body: JSON.stringify({
        email_address: email,
        // Buttondown sends the confirmation mail and holds the subscriber
        // unactivated until it is clicked. The double opt-in and the dated
        // record of consent live there, not here — that is the whole point.
        type: 'unactivated',
        ip_address: ip,
      }),
    });
  } catch (err) {
    console.error(`subscribe: buttondown unreachable — ${err}`);
    return back(origin, 'server');
  }

  if (res.ok) {
    console.log(`subscribe: ok (source=${source})`);
    return seeOther(origin + THANKS);
  }

  // 400 is the collision and suppression family. The address is valid — we
  // checked — so there is nothing the subscriber can do about it and nothing
  // worth telling them, least of all whether they are already on the list.
  const detail = (await res.text().catch(() => '')).slice(0, 300);
  if (res.status === 400) {
    console.log(`subscribe: buttondown 400, treated as success (source=${source}) ${detail}`);
    return seeOther(origin + THANKS);
  }

  // 401 bad key, 403 plan, 429 rate limit, 5xx provider. All real, all ours.
  console.error(`subscribe: buttondown ${res.status} (source=${source}) ${detail}`);
  return back(origin, 'server');
}

async function verifyTurnstile(secret: string, token: string, ip?: string): Promise<boolean> {
  if (!token) return false;
  try {
    const res = await fetch(TURNSTILE_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ secret, response: token, remoteip: ip }),
    });
    const body = (await res.json()) as { success?: boolean; 'error-codes'?: string[] };
    if (!body.success) {
      console.warn(`subscribe: turnstile rejected — ${(body['error-codes'] ?? []).join(',')}`);
    }
    return body.success === true;
  } catch (err) {
    // A verification service that cannot be reached must not become an open
    // door, so this fails closed.
    console.error(`subscribe: turnstile unreachable — ${err}`);
    return false;
  }
}

/**
 * Deliberately permissive. The authoritative check is whether Buttondown's
 * confirmation mail arrives; a regex that tries to be RFC-exact only rejects
 * real addresses.
 */
function isEmail(value: string): boolean {
  return value.length >= 6 && value.length <= 254 && /^[^\s@]+@[^\s@.]+\.[^\s@]+$/.test(value);
}

function str(value: File | string | null): string {
  return typeof value === 'string' ? value : '';
}

/** 303 so the browser follows with GET and a reload cannot resubmit. */
function seeOther(location: string): Response {
  return new Response(null, {
    status: 303,
    headers: { Location: location, 'Cache-Control': 'no-store' },
  });
}

function back(origin: string, problem: Problem): Response {
  // The address never goes in the query string. The subscriber retypes it;
  // that is cheaper than putting an email address in a URL, in a referer
  // header and in every intermediate log.
  return seeOther(`${origin}${FORM}?error=${problem}#waitlist`);
}
