// What a firmware can add to the console. The console knows the host protocol
// and nothing else: any Rodeo gets its keyboard, piano roll, parameter panel and
// chain view from the hello alone. A page made for one firmware (the rodeo-demo
// editor, instruments/rodeo-demo/editor) embeds the console and hands it one
// of these to say the rest: how its panel is grouped, what its encoders turn,
// what its engine does with the notes.
//
// Every field is optional; leaving one out is the generic console's behaviour.

import type { Param, Snapshot } from '@dotrodeo/rodeo-host';
import type { Taper } from './knob.ts';

/** A range of keys to mark on the keyboard, drawn as a bar under them. */
export interface KeySpan {
  label: string;
  from: number;
  /** The last key, inclusive. */
  to: number;
  title?: string;
}

export interface VoiceSlot {
  note: number;
  state: 'held' | 'ringing' | 'free';
}

/**
 * A model of the firmware's voices, fed the notes the console sends: what the
 * voice panel draws. A prediction, so a firmware has to supply it.
 */
export interface VoiceModel {
  noteOn(note: number, velocity: number, now: number): void;
  noteOff(note: number, now: number): void;
  allOff(now: number): void;
  setSustain(down: boolean, now: number): void;
  slots(now: number): VoiceSlot[];
  /** How many are sounding: the panel keeps redrawing while any are. */
  sounding(now: number): number;
  /** The line beside the voices: how many, and what the next note takes. */
  summary(now: number, s: Snapshot): string;
}

/** What an encoder turns, for the chain view's encoder rows. */
export interface EncoderLabel {
  name: string;
  value: string;
  /** What its tap or hold last set. */
  extra?: string;
  /** What a tap does, for the press cell's tooltip. */
  tap?: string;
}

/**
 * One control on a bank of the firmware's panel, as the firmware uses it. The
 * parameter panel draws it as a knob, with its tap and hold under its name.
 */
export interface PanelControl {
  kind: 'pot' | 'encoder';
  /** Every parameter it can turn; the first unless `which` says otherwise. */
  turns: string[];
  /** Which of `turns` it turns now (rodeo-demo's E1: the shape, or the sound). */
  which?(s: Snapshot): string;
  /** Its name on the panel. Without this, the key it turns. */
  name?(s: Snapshot): string;
  /** A press let go without turning: the parameters it sets. */
  tap?: { label: string; act(s: Snapshot): [string, number][] };
  /** Held, then turned: a parameter it steps, or what a step does. */
  hold?: { label: string; key?: string; step?(dir: number, s: Snapshot): void };
  /** Parameters its tap or hold set, drawn under it with their values. */
  shows?: string[];
}

/** A bank of the panel: one column of the grid, as on the instrument. */
export interface PanelBank {
  title: string;
  controls: PanelControl[];
}

/** A serial-port command, as a button beside the chain. */
export interface SerialCommand {
  label: string;
  send: string;
  title?: string;
  /** Said as the command goes out. */
  say?: string;
}

export interface Profile {
  /** The page's title in the bar. */
  title?: string;
  /** The parameter panel as the instrument has it: a column per bank. Any
   *  parameter no control turns or shows goes in a row under them, titled
   *  `panelRest`. Without this: a column per four pots, by pot number, and
   *  the rest. */
  panel?: PanelBank[];
  panelRest?: string;
  /** How a knob for this parameter moves, where the console's guess is wrong:
   *  a time or frequency from 0 ("off") needs its first value above 0. */
  taper?(p: Param): Partial<Taper> | null;
  /** A value as the firmware's panel says it, or null for the console's way. */
  describe?(p: Param, v: number, s: Snapshot): string | null;
  /** A parameter's values as a menu, from p.min up, or null for the usual
   *  control: for values the firmware names elsewhere (rodeo-demo's sounds). */
  menu?(p: Param, s: Snapshot): string[] | null;
  /** More chips for the readout above the chain. */
  readout?(s: Snapshot): [string, string][];
  /** Encoder n (from 0, counted across the chain) as the firmware uses it. */
  encoder?(n: number, s: Snapshot): EncoderLabel | null;
  /** A span of keys worth marking, such as a kit's. */
  keySpan?(): KeySpan | null;
  /** True for a key the firmware will not sound (no sample under it). */
  silent?(note: number): boolean;
  /** Labels on single keys, such as [60, 'root']. */
  marks?: [number, string][];
  voices?: VoiceModel;
  commands?: SerialCommand[];
  /** Firmware controls above the parameter panel (rodeo-demo's preset picker).
   *  Returns a function that takes them down. */
  mountSound?(box: HTMLElement): () => void;
}
