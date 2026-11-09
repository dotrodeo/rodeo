// rodeo-demo's own part of the conversation, on top of the host protocol: its
// preset and sound lists, loading a preset, and the fields it adds to the state
// (planning/RODEO_DEMO_MIDI_PROTOCOL.md). Another firmware is left alone.

import type { RodeoClient, Snapshot } from './client.ts';
import { REPLY, type State, u14 } from './protocol.ts';

export const DemoCmd = {
  presets: 0x20,
  load: 0x21, // payload: the preset's index, 14 bits
  sounds: 0x22,
} as const;

export type Sound = { name: string; kit: number; ready: number };

export type DemoState = State & {
  preset: number; // -1 for none
  name: string;
  voices: number;
  card: number; // 0 none, 1 mounted, 2 disk mode
  sounds: number;
  loaded: number;
  samples: number;
  panel: number; // 1 when pot bank 1 and encoder bank 1 are on the chain
};

export function isRodeoDemo(s: Snapshot): boolean {
  return s.hello?.fw === 'rodeo-demo';
}

/** Asks a rodeo-demo for its lists each time one says hello. Returns a
 *  function that stops it. */
export function followRodeoDemo(client: RodeoClient): () => void {
  return client.whenReady((hello) => {
    if (hello.fw !== 'rodeo-demo') return;
    client.request(DemoCmd.presets);
    client.request(DemoCmd.sounds);
  });
}

export function presets(s: Snapshot): string[] {
  const r = s.replies[DemoCmd.presets + REPLY] as { presets?: string[] } | undefined;
  return isRodeoDemo(s) ? (r?.presets ?? []) : [];
}

export function sounds(s: Snapshot): Sound[] {
  const r = s.replies[DemoCmd.sounds + REPLY] as { sounds?: Sound[] } | undefined;
  return isRodeoDemo(s) ? (r?.sounds ?? []) : [];
}

/** The state with rodeo-demo's fields, or null from another firmware. */
export function demoState(s: Snapshot): DemoState | null {
  return isRodeoDemo(s) && s.state ? (s.state as DemoState) : null;
}

export function loadPreset(client: RodeoClient, i: number) {
  client.request(DemoCmd.load, u14(i));
}
