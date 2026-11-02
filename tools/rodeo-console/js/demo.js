// A card that does not exist, for `?demo`.
//
// Two jobs. It is how the UI gets exercised without a Rodeo on the desk — the
// directory picker needs a real gesture and a real volume, so nothing else can
// drive the rendering path. And it is a worked example of every way a card can
// be wrong, which is more useful than a tidy one: each entry below is a real
// failure mode from samples.cpp or presets.cpp.
//
// Nothing here can be written to. `?demo` leaves the card read-only.

const wav = (name, over = {}) => ({
  name,
  frames: 48000,
  channels: 1,
  rate: 48000,
  bytes: 96_044,
  format: 1,
  bits: 16,
  accepted: true,
  ...over,
});

export function demoCard() {
  return {
    samples: [
      wav('bass.wav', { frames: 96_000, channels: 2, bytes: 384_044 }),
      wav('pad.wav', { frames: 240_000, channels: 2, rate: 44_100, bytes: 960_044 }),
      // The trap: plenty of tools write 24-bit as WAVE_FORMAT_EXTENSIBLE, and
      // the firmware's fmt check rejects it outright (samples.cpp:211).
      wav('strings24.wav', {
        accepted: false,
        format: 0xfffe,
        bits: 24,
        frames: 0,
        why: 'WAVE_FORMAT_EXTENSIBLE, which the Rodeo rejects — re-save it through the console',
      }),
      wav('a-very-long-name-that-the-rodeo-will-cut-off-somewhere.wav', { frames: 24_000 }),
    ],
    kits: new Map([
      [
        '909',
        [
          wav('01_kick.wav', { frames: 12_000 }),
          wav('02_snare.wav', { frames: 14_000 }),
          wav('03_hat.wav', { frames: 6_000 }),
          wav('04_clap.wav', { frames: 9_000 }),
        ],
      ],
      [
        'room',
        [
          // Differ only in case: strcasecmp calls them equal, so which key each
          // lands on is whatever f_readdir gave.
          wav('Tom.wav', { frames: 18_000 }),
          wav('tom.wav', { frames: 18_000 }),
          wav('ride.wav', { frames: 60_000, channels: 2, bytes: 240_044 }),
        ],
      ],
      ['empty', []],
    ]),
    presets: [
      { name: 'init', text: 'layer = synth\nshape = saw\ncutoff = 70\n' },
      {
        name: 'lead',
        text: [
          '# a preset with most of the ways to get it wrong',
          'layer = both',
          'shape = Saw + Sub      ; spacing and case do not matter',
          'sound = bas            # the card has "bass", not "bas"',
          'cutoff = 900           # clamped to 100, and the Rodeo says nothing',
          'resonance',
          'wobble = 3',
          'attack = banana',
        ].join('\n'),
      },
      { name: 'kit909', text: 'layer = sampler\nsound = 909\nrelease = 300\n' },
    ],
    unreadable: [],
  };
}
