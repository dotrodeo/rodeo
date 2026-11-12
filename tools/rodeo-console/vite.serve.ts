// How the console and the rodeo-demo editor are served, shared by both
// vite.config.ts files.
//
// `npm run dev` stays on localhost. `npm run dev:lan` also listens on the
// network, so another computer's Chrome or Edge can open the page; the Rodeo
// plugs into that computer, since Web MIDI and Web Serial reach only the
// browser's own USB devices. See "From another computer" in the console README.
//
// Over the network the page needs HTTPS: Web MIDI, Web Serial and the File
// System Access API work only in a secure context, which http://localhost is
// and http://<host>.lan is not. Point RODEO_TLS_CERT and RODEO_TLS_KEY at a
// certificate the other computers trust and both servers use it.
import { readFileSync } from 'node:fs';
import { hostname } from 'node:os';
import type { UserConfig } from 'vite';

export function serve(port: number): Pick<UserConfig, 'server' | 'preview'> {
  const cert = process.env.RODEO_TLS_CERT;
  const key = process.env.RODEO_TLS_KEY;
  const https = cert && key ? { cert: readFileSync(cert), key: readFileSync(key) } : undefined;
  // Vite answers only Host headers it knows (DNS-rebinding protection), so a
  // name like compute.lan gets a 403 until it's listed. Local names only;
  // RODEO_ALLOWED_HOSTS adds more, comma-separated.
  const extra = (process.env.RODEO_ALLOWED_HOSTS ?? '').split(',').map((h) => h.trim()).filter(Boolean);
  const allowedHosts = ['.lan', '.local', '.home.arpa', hostname(), ...extra];
  const common = { port, allowedHosts, https };
  return { server: common, preview: common };
}
