// The page's one conversation with a Rodeo over MIDI, through the SDK's host
// client (rodeo-sdk/host-js; planning/RODEO_DEMO_MIDI_PROTOCOL.md). live.ts
// opens the ports and hands them here; the sound panel and the preset editor
// read and set through `rodeo`.
//
// SysEx needs its own permission in Chrome ("control and reprogram MIDI
// devices"). Without it the keyboard still plays, and this stays quiet.

import { RodeoClient } from '@dotrodeo/rodeo-host';
import type { Param, State } from '@dotrodeo/rodeo-host';
import { followRodeoDemo, sounds, type Sound } from '@dotrodeo/rodeo-host/rodeo-demo';

export type { Snapshot as RodeoSnapshot } from '@dotrodeo/rodeo-host';

export const rodeo = new RodeoClient();
followRodeoDemo(rodeo);

/** What the Rodeo last said it was playing, kept after the port goes away:
 *  disk mode takes MIDI with it, and that is when a preset gets written. */
export interface Heard {
  state: State;
  params: Record<string, Param>;
  sounds: Sound[];
  at: number;
}
let heard: Heard | null = null;
rodeo.subscribe((s) => {
  if (s.status === 'ready' && s.state)
    heard = { state: s.state, params: s.params, sounds: sounds(s), at: Date.now() };
});

export function lastHeard(): Heard | null {
  return heard;
}

let input: MIDIInput | null = null;
let sysex = false;

/** Whether the page may send SysEx at all, for the MIDI dot's sentence. */
export function sysexAllowed(): boolean {
  return sysex;
}

/**
 * Talk to the Rodeo behind `out`: its input is the one with the same name. A
 * null `out` (unplugged, disk mode, another port picked) ends the conversation.
 */
export function attach(access: MIDIAccess | null, out: MIDIOutput | null): void {
  if (input) input.onmidimessage = null;
  input = null;
  sysex = !!access?.sysexEnabled;
  if (!access || !out || !sysex) return rodeo.attach(null);
  input = [...access.inputs.values()].find((i) => i.name === out.name) ?? null;
  if (!input) return rodeo.attach(null);
  input.onmidimessage = (e) => {
    if (e.data) rodeo.receive(e.data);
  };
  rodeo.attach({
    send(bytes, at) {
      try {
        out.send(bytes, at || undefined);
        return true;
      } catch {
        return false; // the port went between the check and the send
      }
    },
    now: () => performance.now(),
  });
}
