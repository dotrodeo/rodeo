// rodeo-002 — the instrument. MIDI in, voices, texture chain, audio out.
//
//   MIDI IN (TRS, USART1) -> VoiceManager -> TextureChain -> LINE OUT
//   16 pots (ADS114S08 x2, SPI) + 5 encoders (hub @0x30, I2C) -> Ui -> Patch
//   OLED (SSD1309 @0x3C, the same I2C bus) <- DisplayState
//
// Every panel parameter is ALSO on MIDI CC (map printed at startup), which is
// how the instrument was played before the panel existed and is still useful as
// a remote control.
//
// HARDWARE IS OPTIONAL: each of the ADC, the hub and the OLED is probed at
// startup and simply left out if it does not answer, so this runs on a bare
// board. Note the Rev-A bus rule -- the HUB carries the I2C pull-ups, so with
// no hub plugged in the OLED cannot work either.
//
// Polyphony is not fixed: CostModel recomputes it from the measured cost table
// whenever the engine or a slot's type changes, so choosing Reverb can cost
// you voices. The serial log prints the count, the predicted load and the
// measured load side by side -- if those two disagree, the cost table is stale
// and the benchmarks need re-running.
//
// Flash: tap RESET, tap BOOT while the LED pulses, then `make program-dfu`.

#include <cmath>
#include <new>

#include "daisy_seed.h"
#include "daisysp.h"
#include "util/PersistentStorage.h"

#include "cost_model.h"
#include "factory_presets.h"
#include "gesture_store.h"
#include "lfo_dest.h"
#include "version.h"
#include "panel_pot.h"
#include "scope.h"
#include "patch.h"
#include "texture_chain.h"
#include "ui.h"
#include "voice.h"

// Platform drivers (rodeo-motherboard/firmware/common).
#include "encoder_hub.h"
#include "oled_ssd130x_bulk.h"
#include "rodeo_adc.h"
#include "rodeo_encoders.h"

using namespace daisy;
using namespace rodeo002;
using fx::Space;
using fx::TextureChain;

static constexpr size_t kBlockSize  = 48;
static constexpr float  kSampleRate = 48000.f;

// ONE BUS, ONE SPEED. RodeoEncoders and the OLED transport each own an
// I2CHandle, but libDaisy keeps one impl per peripheral, so both drive I2C1 and
// THE LAST Init() WINS on speed. They must agree, or whichever was configured
// first ends up running at the other's speed -- which is what stopped the hub
// answering here (the OLED's default is 400 kHz; the hub contract boots at
// 100 kHz). Promote both together once the populated bus is validated.
// 400 kHz: the hub contract's "promote once the populated bus is validated"
// step, taken 2026-09-23 with all five encoders and the OLED live. It matters
// because the display's bus time is what starves the panel -- a full frame is
// ~25 ms here against ~100 ms at 100 kHz.
static constexpr I2CHandle::Config::Speed kI2cSpeed
    = I2CHandle::Config::Speed::I2C_400KHZ;

// Space's buffers and ReverbSc (~396 KB) in SDRAM; the reverb is constructed
// after hw.Init() brings the SDRAM up.
float DSY_SDRAM_BSS g_grain_l[Space::kGrainLen];
float DSY_SDRAM_BSS g_grain_r[Space::kGrainLen];
float DSY_SDRAM_BSS g_pre_l[Space::kPreLen];
float DSY_SDRAM_BSS g_pre_r[Space::kPreLen];
alignas(daisysp::ReverbSc) static uint8_t DSY_SDRAM_BSS
    g_reverb_mem[sizeof(daisysp::ReverbSc)];

// libDaisy's MIDI handler is ~37 KB -- a 256-entry event queue, each entry
// carrying a sysex buffer -- which does not fit in DTCM beside 8 voices and the
// chain. Its DMA buffer is a pointer to a shared buffer elsewhere, so the
// object itself can live in SDRAM; MIDI arrives far too slowly for the slower
// memory to matter.
alignas(MidiUartHandler) static uint8_t DSY_SDRAM_BSS g_midi_mem[sizeof(MidiUartHandler)];

using OledDisplayType
    = OledDisplay<SSD130xDriver<128, 64, rodeo::Ssd130xI2cBulkTransport>>;

DaisySeed          hw;
MidiUartHandler*   midi = nullptr;
rodeo::RodeoAdc    adc;
rodeo::RodeoEncoders encoders;
OledDisplayType    display;
Ui                 ui;
PanelPots          pots; // conditioning, incl. cables built the wrong way

// --- panel calibration, kept in QSPI ---------------------------------------
//
// Learning each pot's true travel takes a sweep of all sixteen, and doing that
// at every power-up is not a reasonable thing to ask (2026-09-24). The chip is
// 8 MB: the bootloader leaves the first four 64 KB sectors alone and stores the
// program from 0x90040000, and an SRAM build cannot exceed 480 KB, so a sector
// 7.5 MB in is clear of both by a wide margin, and a reflash does not touch it.
// Erases are 64 KB, so anything stored later -- presets -- needs its own 64 KB
// boundary rather than a place beside this.
static constexpr uint32_t kCalStoreOffset = 0x780000u;
/** Quiet time after the last thing learned before the calibration is written.
 *  A block erase stalls the main loop for a good fraction of a second, so it
 *  waits until the knob has stopped moving. Audio is unaffected: it runs from
 *  the audio interrupt, which the erase does not block. */
static constexpr uint32_t kCalSettleMs = 4000;
PersistentStorage<PanelPots::Calibration> cal_store(hw.qspi);
static uint32_t g_cal_revision = 0, g_cal_changed_ms = 0;
static bool     g_cal_dirty    = false;
static bool     g_cal_restored = false, g_cal_cleared = false;

// --- the preset bank, its own 64 KB sector -------------------------------
// 2.6 KB of it is used; the sector is its own because an erase takes the whole
// 64 KB with it, and saving a preset must not disturb the wiring map.
static constexpr uint32_t kPresetStoreOffset = 0x790000u;
PersistentStorage<PresetBank> preset_store(hw.qspi);
// The factory copy is read once, at startup, to seed the store -- so it lives
// in SDRAM rather than spending a third copy of the bank in DTCM. (The store's
// own two copies, defaults and live, are unavoidable and stay in DTCM, where
// the browser reads them.)
static PresetBank DSY_SDRAM_BSS g_factory_bank;

// The gesture recorder's take: 32 lanes x 16 s at its frame rate, ~100 KB, which
// DTCM does not have and SDRAM will not miss. Nothing reads it in the audio
// callback, so the slower memory costs nothing.
static uint16_t DSY_SDRAM_BSS
    g_gesture_buf[GestureRecorder::kLanes * GestureRecorder::kMaxFrames];
static GestureRecorder g_gesture;

/** Write the slot's take to flash, or an empty header if there is none.
 *
 *  Always one or the other: a slot saved without a gesture has to CLEAR the one
 *  that was there, or an old loop would attach itself to a new sound. Only the
 *  frames actually recorded are erased and written -- a four-second take is
 *  25 KB rather than 100 -- because erasing stalls this loop for a good part of
 *  a second. Audio keeps running from the interrupt throughout. */
static void SaveGestureForSlot(int slot)
{
    GestureHeader h;
    h.magic     = GestureHeader::kMagic;
    h.version   = GestureHeader::kVersion;
    h.lanes     = static_cast<uint16_t>(GestureRecorder::kLanes);
    h.frames    = static_cast<uint32_t>(g_gesture.length_frames());
    h.lane_mask = g_gesture.LaneMask();

    const uint32_t base  = GestureOffsetForSlot(slot);
    const size_t   bytes = GestureBytes(h.frames);
    hw.qspi.Erase(base, base + bytes);
    hw.qspi.Write(base, sizeof(h), reinterpret_cast<uint8_t*>(&h));
    if(h.frames)
        hw.qspi.Write(base + sizeof(h),
                      h.frames * GestureRecorder::kLanes * sizeof(uint16_t),
                      reinterpret_cast<uint8_t*>(g_gesture.MutableData()));
}

/** Pick up the slot's take, if it has one, and start it looping. QSPI is
 *  memory-mapped for reads, so this is a copy rather than a transfer. */
static bool LoadGestureForSlot(int slot)
{
    const uint32_t base = GestureOffsetForSlot(slot);
    const uint8_t* mem  = static_cast<const uint8_t*>(hw.qspi.GetData(base));
    // INVALIDATE BEFORE READING. QSPI is memory-mapped, so a take written
    // earlier in this session can still be sitting in cache as whatever was
    // there before the erase. libDaisy's own PersistentStorage does the same
    // thing for the same reason, and only when the program is not running from
    // internal flash -- which, under the bootloader, it never is.
    if(System::GetProgramMemoryRegion() != System::MemoryRegion::INTERNAL_FLASH)
        dsy_dma_invalidate_cache_for_buffer(const_cast<uint8_t*>(mem), GestureSlotSize());
    const GestureHeader* h = reinterpret_cast<const GestureHeader*>(mem);
    if(!h->HasGesture())
    {
        g_gesture.Clear();
        return false;
    }
    const uint16_t* src   = reinterpret_cast<const uint16_t*>(mem + sizeof(GestureHeader));
    uint16_t*       dst   = g_gesture.MutableData();
    const size_t    count = h->frames * GestureRecorder::kLanes;
    for(size_t i = 0; i < count; i++)
        dst[i] = src[i];
    g_gesture.Adopt(h->frames, h->lane_mask);
    return true;
}

// --- what the instrument was doing last -----------------------------------
// Only the preset slot, for now: the synth boots back into the sound it was
// left on. Its own sector, because it is written on a different schedule from
// the bank -- every preset change, rather than every save.
struct InstrumentState
{
    uint32_t magic;
    uint16_t version;
    uint16_t last_preset;
    int32_t  midi_channel;     // 0 = omni; global, so it lives here, not in a preset
    int32_t  midi_out_channel; // what the panel's CC goes out on; never omni
    uint32_t reserved;

    bool operator==(const InstrumentState& o) const
    {
        return magic == o.magic && version == o.version && last_preset == o.last_preset
               && midi_channel == o.midi_channel
               && midi_out_channel == o.midi_out_channel && reserved == o.reserved;
    }
    bool operator!=(const InstrumentState& o) const { return !(*this == o); }
};
static constexpr uint32_t kStateMagic       = 0x52325354u; // "R2ST"
static constexpr uint16_t kStateVersion     = 3; // 2 added the MIDI channel, 3 the out channel
static constexpr uint32_t kStateStoreOffset = 0x7A0000u;
PersistentStorage<InstrumentState> state_store(hw.qspi);
static uint32_t g_state_changed_ms = 0;
static bool     g_state_dirty      = false;

bool g_have_adc = false, g_have_hub = false, g_have_oled = false;

// A second view of hub 0 for bring-up: RodeoEncoders keeps its hubs private, and
// what matters here is what the HUB itself reports rather than what the scanner
// cached. SW_STATE (0x19) is the useful register -- a live level, NOT
// read-to-clear -- so watching it while pressing an encoder says whether the hub
// sees the switch at all. Shares I2C1 like everything else.
I2CHandle         dbg_i2c;
rodeo::EncoderHub dbg_hub;
bool              g_have_dbg_hub = false;

// The Ui owns the patch; these are just names for it, so the MIDI CC layer and
// the panel write the same fields.
Patch&   patch   = ui.mutable_patch();
Globals& globals = ui.mutable_globals();

// Panel pot -> ADC channel (control map: rows 1-2 on U3 ch0-7, rows 3-4 on U4
// ch12-19, which is the 8+8 split that doubled the scan rate).
static const uint8_t kPotChannel[Ui::kNumPots]
    = {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 18, 19};
// Panel encoder -> hub slot. The API is 0-based; panel encoders 1-5 are the
// hub's slots 1-5 (connectors J3-J7).
static const uint8_t kEncoderSlot[5] = {0, 1, 2, 3, 4};

// BRING-UP WORKAROUND, 2026-09-23: the pot on J25 -- Space Mix -- is dead. It
// reads 0.00 V at every position. (The faulty pot came off R3C4; it was moved
// here while proving the fault was the pot and not the channel, so the dead
// CHANNEL is J25 whatever the panel labels say.) Because the patch default is
// also 0, pickup catches it immediately and the pot then holds the mix at zero,
// which makes the whole Space slot untestable. Hold it at full instead.
// REMOVE once the pot is replaced.
static constexpr bool  kPinSpaceMix   = true;
static constexpr float kSpaceMixValue = 0.5f;
/** Panel index of that pot (R4C4), and the travel that says a live pot is on
 *  the end of it. A dead channel reads one voltage for ever; anything with a
 *  tenth of a volt of movement in it is a working pot, so the pin lets go by
 *  itself once one is fitted -- no reflash to undo a workaround. */
static constexpr int   kSpaceMixPot     = 15;
static constexpr float kPotAliveVolts   = 0.1f;
static bool            g_space_mix_pinned = kPinSpaceMix;
VoiceManager    voices;
TextureChain    chain;
EngineId        current_engine = EngineId::kVirtualAnalog;
static int      g_boot_preset       = -1;
static int      g_slots_filled      = 0;
static bool     g_boot_engine_known = true;
static bool     g_boot_gesture      = false;

volatile bool     g_voices_locked = false; // engine swap in progress
volatile uint32_t g_blocks        = 0;
volatile uint32_t g_busy_ticks    = 0;
volatile uint32_t g_busy_blocks   = 0;
static uint32_t   g_block_ticks   = 1;
static float      g_last_load     = 0.f; // for the screen's footer

static float g_mix[kBlockSize];

// The output as a picture; the triggering, normalising and clamping all live
// in scope.h, where they can be tested.
static Scope g_scope;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const uint32_t t0 = System::GetTick();

    if(g_voices_locked)
        for(size_t i = 0; i < size; i++)
            g_mix[i] = 0.f;
    else
        voices.Render(g_mix, size);

    for(size_t i = 0; i < size; i++)
        out[0][i] = out[1][i] = g_mix[i];

    // LFO destinations that live in the CHAIN rather than in a voice, applied
    // here once per block. ApplyPatch runs at ~200 Hz, which a 40 Hz LFO would
    // step straight through; the slot's own smoothers turn this into a ramp.
    // Written after ApplyPatch and immediately before the chain runs, so this
    // is always the value the block is processed with.
    ApplyLfoToChain(chain, patch, voices.LfoValue());
    ApplyModWheelToChain(chain, patch, voices.ModWheel());

    chain.ProcessBlock(out[0], out[1], size);

    for(size_t i = 0; i < size; i++)
        g_scope.Push(out[0][i]);

    g_busy_ticks += System::GetTick() - t0;
    g_busy_blocks++;
    g_blocks++;
}

// --------------------------------------------------------------- CC map
//
// Mirrors the panel (control map, *Analog*): 70-85 are the main layer in panel
// order, 90-105 the shift layer in the same order, 110+ the discrete choices.

enum : uint8_t
{
    // main layer, panel order
    kCcHarmonics = 70, kCcTimbre, kCcMorph, kCcCharacterAmt,
    kCcCutoff, kCcResonance, kCcFilterEnv, kCcMovementAmt,
    kCcAttack, kCcDecay, kCcSustain, kCcTextureAmt,
    kCcSpaceTime, kCcSpaceFeedback, kCcSpaceGrain, kCcSpaceMix,
    // shift layer, same order
    kCcEnvToTimbre = 90, kCcEnvToMorph, kCcLfoRate, kCcCharacterBias,
    kCcFilterType, kCcLfoDepth, kCcLfoDest, kCcMovementRate,
    kCcSpread, kCcGlide, kCcVolume, kCcTextureTone,
    kCcGrainPitch, kCcGrainSpread, kCcTilt, kCcSpaceDamp,
    // discrete
    kCcEngine = 110, kCcCharacterType, kCcMovementType, kCcTextureType, kCcSpaceType,
    kCcVoiceMode, kCcFreeze, kCcBypassBase // 117-120: one per slot
};

static bool  g_discrete_changed = true;
static float Norm(uint8_t v) { return static_cast<float>(v) / 127.f; }
static int   Step(uint8_t v, int n) { return (static_cast<int>(v) * n) / 128; }

static void HandleCc(uint8_t cc, uint8_t value)
{
    // A CC moved a parameter out from under the pots: they must catch up again.
    ui.DropPickup();
    const float f = Norm(value);
    switch(cc)
    {
        case 1: voices.SetModWheel(f); return;
        case 64: voices.SetSustain(value >= 64); return;

        case kCcHarmonics: patch.harmonics = f; return;
        case kCcTimbre: patch.timbre = f; return;
        case kCcMorph: patch.morph = f; return;
        case kCcCharacterAmt: patch.amount[TextureChain::kCharacter] = f; return;
        case kCcCutoff: patch.cutoff = f; return;
        case kCcResonance: patch.resonance = f; return;
        case kCcSpread: patch.spread = f; return;
        case kCcMovementAmt: patch.amount[TextureChain::kMovement] = f; return;
        case kCcAttack: patch.attack = f; return;
        case kCcVolume: globals.volume = f; return;
        case kCcTextureAmt: patch.amount[TextureChain::kTexture] = f; return;
        case kCcSpaceTime: patch.space_time = f; return;
        case kCcSpaceFeedback: patch.space_feedback = f; return;
        case kCcSpaceGrain: patch.space_grain = f; return;
        case kCcSpaceMix: patch.amount[TextureChain::kSpace] = f; return;

        case kCcEnvToTimbre: patch.env_to_timbre = f; return;
        case kCcEnvToMorph: patch.env_to_morph = f; return;
        case kCcLfoRate: patch.lfo_rate = f; return;
        case kCcCharacterBias: patch.secondary[TextureChain::kCharacter] = f; return;
        case kCcFilterType:
            patch.filter = static_cast<FilterType>(
                Step(value, static_cast<int>(FilterType::kCount)));
            return;
        case kCcFilterEnv: patch.filter_env = f; return;
        case kCcLfoDest:
            patch.lfo_dest
                = static_cast<LfoDest>(Step(value, static_cast<int>(LfoDest::kCount)));
            return;
        case kCcMovementRate: patch.secondary[TextureChain::kMovement] = f; return;
        // ADS: one fall time, held or not -- kept in step so the stored patch
        // says the same thing the panel does.
        case kCcDecay:
            patch.decay   = f;
            patch.release = f;
            return;
        case kCcGlide: patch.glide = f; return;
        case kCcSustain: patch.sustain = f; return;
        case kCcLfoDepth: patch.lfo_depth = f; return;
        case kCcTextureTone: patch.secondary[TextureChain::kTexture] = f; return;
        case kCcGrainPitch: patch.grain_pitch = f; return;
        case kCcGrainSpread: patch.grain_spread = f; return;
        case kCcTilt: patch.tilt = f; return;
        case kCcSpaceDamp: patch.secondary[TextureChain::kSpace] = f; return;

        case kCcEngine:
            patch.engine = static_cast<EngineId>(Step(value, static_cast<int>(EngineId::kCount)));
            g_discrete_changed = true;
            return;
        case kCcVoiceMode:
            patch.voice_mode
                = static_cast<VoiceMode>(Step(value, static_cast<int>(VoiceMode::kCount)));
            return;
        case kCcFreeze: patch.freeze = value >= 64; return;
        default: break;
    }
    if(cc >= kCcCharacterType && cc <= kCcSpaceType)
    {
        patch.type[cc - kCcCharacterType] = Step(value, 5);
        g_discrete_changed                = true;
    }
    else if(cc >= kCcBypassBase && cc < kCcBypassBase + TextureChain::kNumSlots)
    {
        patch.bypass[cc - kCcBypassBase] = value >= 64;
        g_discrete_changed               = true;
    }
}

static void HandleMidi(MidiEvent m) // libDaisy's accessors are non-const
{
    if(globals.midi_channel > 0 && m.channel != globals.midi_channel - 1)
        return;
    switch(m.type)
    {
        case NoteOn:
        {
            const NoteOnEvent n = m.AsNoteOn();
            voices.NoteOn(n.note, static_cast<float>(n.velocity) / 127.f);
            break;
        }
        case NoteOff: voices.NoteOff(m.AsNoteOff().note); break;
        case ControlChange:
        {
            const ControlChangeEvent c = m.AsControlChange();
            if(c.control_number == 123 || c.control_number == 120)
                voices.AllNotesOff();
            else
                HandleCc(c.control_number, c.value);
            break;
        }
        case PitchBend:
            voices.SetBend(static_cast<float>(m.AsPitchBend().value) / 8192.f
                           * static_cast<float>(patch.bend_range));
            break;
        case ChannelPressure:
            voices.SetAftertouch(static_cast<float>(m.AsChannelPressure().pressure) / 127.f);
            break;
        default: break;
    }
}

// --- MIDI CC out ---------------------------------------------------------
//
// Every panel move goes out of J3 as the same CC that would have caused it, so
// a take can be recorded and played back into the synth. Deliberately ONE
// message per pass at 100 Hz: MIDI is 31250 baud, about 330 three-byte messages
// a second, and a panel with 32 parameters could saturate it in a heartbeat.
// One knob moving sends ~100 values a second, which is finer than the pots are
// even scanned; several knobs share that budget round-robin.
static uint8_t  g_cc_sent[128];
static bool     g_cc_primed  = false;
static uint32_t g_cc_last_ms = 0;
static int      g_cc_scan    = 0;

/** The CC each parameter answers to, and its current 7-bit value. */
static uint8_t CcForIndex(int i)
{
    if(i < Ui::kNumPots * 2)
        return static_cast<uint8_t>(i < Ui::kNumPots ? kCcHarmonics + i
                                                     : kCcEnvToTimbre + i - Ui::kNumPots);
    switch(i - Ui::kNumPots * 2)
    {
        case 0: return kCcEngine;
        case 1: return kCcCharacterType;
        case 2: return kCcMovementType;
        case 3: return kCcTextureType;
        case 4: return kCcSpaceType;
        case 5: return kCcVoiceMode;
        case 6: return kCcFreeze;
        default: return static_cast<uint8_t>(kCcBypassBase + (i - Ui::kNumPots * 2 - 7));
    }
}
static constexpr int kCcWatched = Ui::kNumPots * 2 + 11;

static uint8_t CcValueForIndex(int i)
{
    auto step = [](int v, int n) {
        return static_cast<uint8_t>((v * 127 + (n - 1) / 2) / (n > 1 ? n - 1 : 1));
    };
    if(i < Ui::kNumPots * 2)
        return static_cast<uint8_t>(ui.Value(static_cast<ParamId>(i)) * 127.f + 0.5f);
    switch(i - Ui::kNumPots * 2)
    {
        case 0:
            return step(static_cast<int>(patch.engine), static_cast<int>(EngineId::kCount));
        case 1: return step(patch.type[TextureChain::kCharacter], fx::Character::kNumTypes);
        case 2: return step(patch.type[TextureChain::kMovement], fx::Movement::kNumTypes);
        case 3: return step(patch.type[TextureChain::kTexture], fx::Texture::kNumTypes);
        case 4: return step(patch.type[TextureChain::kSpace], fx::Space::kNumTypes);
        case 5:
            return step(static_cast<int>(patch.voice_mode), static_cast<int>(VoiceMode::kCount));
        case 6: return patch.freeze ? 127 : 0;
        default:
            return patch.bypass[i - Ui::kNumPots * 2 - 7] ? 127 : 0;
    }
}

/** At most one changed value per call, round-robin so no parameter starves. */
static void SendCcChanges(uint32_t now)
{
    if(!midi || now - g_cc_last_ms < 10)
        return;
    g_cc_last_ms = now;
    for(int n = 0; n < kCcWatched; n++)
    {
        const int     i  = (g_cc_scan + n) % kCcWatched;
        const uint8_t cc = CcForIndex(i);
        const uint8_t v  = CcValueForIndex(i);
        if(g_cc_primed && v == g_cc_sent[cc])
            continue;
        g_cc_sent[cc] = v;
        g_cc_scan     = i + 1;
        if(!g_cc_primed)
            continue; // the first pass only learns where everything is
        const uint8_t ch = static_cast<uint8_t>(globals.midi_out_channel > 0
                                                    ? globals.midi_out_channel - 1
                                                    : 0);
        uint8_t       msg[3] = {static_cast<uint8_t>(0xB0 | ch), cc, v};
        midi->SendMessage(msg, 3);
        return;
    }
    g_cc_primed = true;
}

/** Note the slot to boot into next time. Deferred like the wiring map: the
 *  write is a 64 KB erase, and there is no reason to stall the panel with one
 *  the instant a preset is chosen. */
static void RememberPreset(int slot, uint32_t now)
{
    InstrumentState& st = state_store.GetSettings();
    if(st.last_preset == static_cast<uint16_t>(slot))
        return;
    st.last_preset     = static_cast<uint16_t>(slot);
    g_state_dirty      = true;
    g_state_changed_ms = now;
}

/** Push the patch into the voices and the chain. Control rate. */
static void ApplyPatch()
{
    voices.ApplyPatch(patch);
    for(int s = 0; s < TextureChain::kNumSlots; s++)
    {
        chain.SetAmount(s, patch.amount[s]);
        chain.SetSecondary(s, patch.secondary[s]);
        chain.SetOption(s, patch.option[s]);
        chain.SetType(s, patch.type[s]); // crossfades if it changed
        chain.SetBypass(s, patch.bypass[s]);
    }
    Space& sp = chain.space();
    sp.SetTime(patch.space_time);
    sp.SetFeedback(patch.space_feedback);
    sp.SetGrain(patch.space_grain);
    sp.SetPitch(patch.grain_pitch);
    sp.SetSpread(patch.grain_spread);
    sp.SetFreeze(patch.freeze);
    chain.SetTilt(patch.tilt);
    chain.SetVolume(globals.volume);
}

/** Services the panel and MIDI. Also handed to the OLED driver as its yield, so
 *  a screen refresh does not stall the pots or MIDI.
 *
 *  THE HUB IS NOT POLLED DURING A FRAME. Those are I2C transactions on the bus
 *  the display is using, and nesting them risks leaving a device holding the
 *  bus -- which wedges the panel for good while audio, running in the
 *  interrupt, carries on none the wiser. Nothing is lost by waiting: the hub
 *  ACCUMULATES deltas and LATCHES presses, so a ~25 ms gap costs no input.
 *  The ADC is on SPI and MIDI on a UART, so both are safe here.
 *  Deliberately no logging: PrintLine can block. */
static bool g_in_oled = false;

static void PumpIo()
{
    if(g_have_adc)
        adc.Update();
    if(g_have_hub && !g_in_oled)
        encoders.Update();
    if(midi)
    {
        midi->Listen();
        while(midi->HasEvents())
            HandleMidi(midi->PopEvent());
    }
}

// --- where everything sits, 128x64 ---------------------------------------
//
// Named and checked rather than scattered as literals: the screen is 64 rows
// and every one of them is spoken for, so an overlap is a drawing bug that only
// shows up on hardware. The static_asserts below are what make the compiler
// find it instead.
namespace layout
{
constexpr int kRows = 64;

constexpr int kHeaderTop = 0, kHeaderBot = 8;  // preset name, mode, voices
constexpr int kRuleY     = 9;                  // the line under it
constexpr int kSlotTop = 11, kSlotBot = 18;    // the four slot names
constexpr int kBodyTop = 20, kBodyBot = 62;    // scope, readouts, meter

// With a readout on screen the body splits three ways.
constexpr int kStripBot  = 46;                 // scope strip ends
constexpr int kValueY    = 48;                 // name and percentage (6x8)
constexpr int kMeterTop = 57, kMeterBot = kBodyBot;
constexpr int kChoiceY   = 51;                 // "Filter: Band pass" (6x8)
// The envelope keeps the whole body; the message box floats over the strip.
constexpr int kEnvTop = 30, kEnvBot = kBodyBot;
constexpr int kMsgTop = 32, kMsgBot = 45;

static_assert(kHeaderBot < kRuleY, "header runs into its rule");
static_assert(kRuleY < kSlotTop, "rule runs into the slot row");
static_assert(kSlotBot < kBodyTop, "slot row runs into the body");
static_assert(kStripBot < kValueY, "scope strip runs into the value line");
static_assert(kValueY + 8 <= kMeterTop, "value line runs into the meter");
static_assert(kChoiceY + 8 <= kRows, "the choice line falls off the screen");
static_assert(kMeterBot < kRows, "the meter falls off the screen");
static_assert(kEnvBot + 1 < kRows, "the envelope baseline falls off the screen");
static_assert(kMsgTop >= kBodyTop && kMsgBot <= kStripBot, "the message box is not over the strip");
} // namespace layout

/** The output as a triggered scope, in whatever space the readout leaves. */
static void DrawScope(int y0, int y1)
{
    uint8_t y[Scope::kWidth];
    g_scope.Trace(y0, y1, y);
    for(int x = 1; x < Scope::kWidth; x++)
        display.DrawLine(static_cast<uint8_t>(x - 1), y[x - 1], static_cast<uint8_t>(x), y[x], true);
}

/** The envelope the moving segment belongs to, with that segment picked out.
 *  Times are drawn to a fixed budget rather than to scale: the shape is what
 *  says what the sound will do, and 124 pixels cannot hold seconds anyway. */
static void DrawEnvelope(const DisplayState& d)
{
    const int y_top = layout::kEnvTop, y_base = layout::kEnvBot;
    const int h     = y_base - y_top;
    // NO MINIMUM WIDTH. A segment's width is its time and nothing else, so a
    // time of zero draws a vertical line -- which is what zero means. Giving
    // each segment a few pixels of floor drew a slope at zero and implied an
    // attack that was not there (Derek, 2026-09-24).
    const int wa   = static_cast<int>(d.env[0] * 32.f);
    const int wd   = static_cast<int>(d.env[1] * 32.f);
    const int hold = 16;
    const int wr   = static_cast<int>(d.env[3] * 32.f);
    const int y_sus = y_base - static_cast<int>(d.env[2] * static_cast<float>(h));

    int x[5], y[5];
    x[0] = 2;            y[0] = y_base;
    x[1] = x[0] + wa;    y[1] = y_top;
    x[2] = x[1] + wd;    y[2] = y_sus;
    x[3] = x[2] + hold;  y[3] = y_sus;
    x[4] = x[3] + wr;    y[4] = y_base;

    for(int i = 0; i < 4; i++)
    {
        // The segment being moved is drawn twice, a pixel apart: on a 1-bit
        // display that is the only "highlight" available. Decay owns the
        // release segment as well -- this is an ADS envelope -- so both light
        // up, which is what that knob does to the sound.
        const bool moving = i == d.env_moving || (d.env_moving == 1 && i == 3);
        display.DrawLine(static_cast<uint8_t>(x[i]), static_cast<uint8_t>(y[i]),
                         static_cast<uint8_t>(x[i + 1]), static_cast<uint8_t>(y[i + 1]), true);
        if(moving)
            display.DrawLine(static_cast<uint8_t>(x[i]), static_cast<uint8_t>(y[i] - 1),
                             static_cast<uint8_t>(x[i + 1]), static_cast<uint8_t>(y[i + 1] - 1),
                             true);
    }
    display.DrawLine(0, static_cast<uint8_t>(y_base + 1), 127,
                     static_cast<uint8_t>(y_base + 1), true);
}

/** 128x64: a header line, the four slots, the parameter readout, a footer. */
static void DrawScreen()
{
    const DisplayState& d = ui.display();
    display.Fill(false);

    // Header: the preset you are in, whether you have changed it, the voice
    // mode in three letters, and the voices in use. The ENGINE is not here any
    // more -- it announces itself when you turn to it, and the preset's name is
    // the more useful thing to have in front of you.
    // SHIFT inverts the header rather than spending a line on the word. It is
    // a held state, so it wants to be unmissable and to cost nothing: the
    // bottom of the screen is worth more as waveform.
    const bool inv = d.shift;
    if(inv)
        display.DrawRect(0, layout::kHeaderTop, 127, layout::kHeaderBot, true, true);
    // While SHIFT is held the preset's name is the least useful thing on this
    // screen and the gesture nobody can guess is the most useful: holding MAIN
    // and turning it chooses the voice mode, which is shown to the right, so
    // the hint and its result are on the same line.
    FixedCapStr<22> line("");
    if(inv)
        line.Append("TURN: MODE");
    else if(d.notice[0])
        // What the instrument did on its own, where it cannot cover what you
        // did: the engine you just turned onto is in the middle of the screen.
        line.Append(d.notice);
    else
    {
        line.Append(d.preset_name);
        if(d.preset_dirty)
            line.Append("*");
    }
    display.SetCursor(0, 0);
    display.WriteString(line, Font_6x8, !inv);
    // What the recorder is doing displaces the voice mode while it is doing it:
    // knowing a take is running matters more than knowing you are in Poly, and
    // it has to be visible without opening anything.
    display.SetCursor(128 - 6 * 7, 0);
    display.WriteString(d.gesture[0] ? d.gesture : d.voice_short, Font_6x8, !inv);
    FixedCapStr<8> voices_str("");
    voices_str.AppendInt(d.voices_active);
    voices_str.Append("/");
    voices_str.AppendInt(d.voices_max);
    display.SetCursor(128 - 6 * 3, 0);
    display.WriteString(voices_str, Font_6x8, !inv);
    display.DrawLine(0, layout::kRuleY, 127, layout::kRuleY, true);

    // The chain: four slot names, bypassed ones struck through.
    for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
    {
        const uint8_t x = static_cast<uint8_t>(s * 32);
        FixedCapStr<8> name("");
        for(int i = 0; i < 5 && d.slot_type[s][i]; i++)
            name.Append(d.slot_type[s] + i, 1);
        // FREEZE belongs to Space, so it is shown ON Space rather than as a
        // word somewhere else.
        const bool frozen = d.freeze && s == fx::TextureChain::kSpace;
        if(frozen)
            display.DrawRect(x, layout::kSlotTop, static_cast<uint8_t>(x + 31),
                             layout::kSlotBot, true, true);
        display.SetCursor(x + 1, 12);
        display.WriteString(name, Font_4x6, !frozen);
        if(d.slot_bypass[s])
            display.DrawLine(x + 1, 15, x + 29, 15, !frozen);
    }

    // The middle of the screen belongs to the sound, and to whatever you are
    // changing about it -- both at once, so you can watch the shape move as you
    // turn. The scope is a strip above the readout when there is a readout, and
    // takes the whole space when there is not.
    // THE BODY RUNS TO THE BOTTOM. There was a text line reserved down here for
    // shift, freeze and messages, empty in almost every frame; those have moved
    // onto the things they describe, and the waveform got the space. The strip
    // above a readout is now 27 rows where it was 18, and with nothing on
    // screen the scope has all 43.
    // ALWAYS THE SAME HEIGHT. The rows under the strip stay reserved whether or
    // not there is a readout in them, so the waveform never changes size under
    // you (Derek, 2026-09-24) -- a scope that resizes as a popup comes and goes
    // is harder to read than a smaller one that holds still.
    const bool envelope = d.readout == Readout::kEnvelope;
    if(d.mode == UiMode::kNormal && !envelope)
        DrawScope(layout::kBodyTop, layout::kStripBot);

    switch(d.readout)
    {
        case Readout::kNone: break; // the scope has it

        case Readout::kChoice:
        {
            // A stepped pot is a choice: name it. A meter here would say
            // nothing about WHICH destination you landed on.
            FixedCapStr<32> choice(d.param);
            choice.Append(": ");
            choice.Append(d.choice);
            display.SetCursor(0, layout::kChoiceY);
            display.WriteString(choice, Font_6x8, true);
            break;
        }

        case Readout::kEnvelope:
            display.SetCursor(0, layout::kBodyTop);
            display.WriteString(d.param, Font_6x8, true);
            DrawEnvelope(d);
            break;

        default:
        {
            display.SetCursor(0, layout::kValueY);
            display.WriteString(d.param, Font_6x8, true);
            // The number, because a bar says roughly and a knob deserves
            // exactly. Pickup puts which way to turn in front of it: the value
            // shown is the one being chased, not the one the pot is at.
            FixedCapStr<12> pct("");
            if(d.pickup_hint != 0)
                pct.Append(d.pickup_hint > 0 ? "UP " : "DN ");
            pct.AppendInt(static_cast<int>(d.value * 100.f + 0.5f));
            pct.Append("%");
            const int w_px = 6 * static_cast<int>(pct.Size());
            display.SetCursor(static_cast<uint8_t>(128 - w_px), layout::kValueY);
            display.WriteString(pct, Font_6x8, true);

            const uint8_t w = static_cast<uint8_t>(d.value * 124.f);
            display.DrawRect(0, layout::kMeterTop, 127, layout::kMeterBot, true);
            if(w > 1)
                display.DrawRect(2, layout::kMeterTop + 1, static_cast<uint8_t>(2 + w),
                                 layout::kMeterBot - 1, true, true);
            break;
        }
    }

    // The modal layer takes the screen for as long as it applies, so that what
    // a tap will do is never a guess.
    if(d.mode != UiMode::kNormal)
    {
        display.DrawRect(0, 20, 127, 52, false, true); // clear what is under it
        display.DrawRect(0, 20, 127, 52, true);
        FixedCapStr<24> head("");
        FixedCapStr<24> body("");
        const char*     hint = "";
        if(d.mode == UiMode::kMenu)
        {
            head.Append("MENU");
            body.Append(d.menu_item);
            hint = "turn to choose   tap to enter";
        }
        else if(d.mode == UiMode::kName)
        {
            head.Append("NAME ");
            head.AppendInt(d.browser_slot + 1);
            body.Append(d.name_text);
            hint = "turn spells  hold+turn moves  tap saves";
        }
        else if(d.mode == UiMode::kPatchPage || d.mode == UiMode::kGlobalPage)
        {
            head.Append(d.mode == UiMode::kPatchPage ? "PATCH " : "GLOBAL ");
            head.AppendInt(d.page_index + 1);
            head.Append("/");
            head.AppendInt(d.page_count);
            head.Append("  ");
            head.Append(d.page_item);
            body.Append(d.page_value);
            hint = "hold MAIN and turn to change";
        }
        else
        {
            head.Append(d.mode == UiMode::kLoad ? "LOAD " : "SAVE ");
            head.AppendInt(d.browser_slot + 1);
            head.Append("/");
            head.AppendInt(PresetBank::kNumSlots);
            body.Append(d.browser_used ? d.browser_name : "-- empty --");
            hint = d.mode == UiMode::kLoad ? "tap to load" : "tap to overwrite";
        }
        display.SetCursor(4, 23);
        display.WriteString(head, Font_6x8, true);
        display.SetCursor(4, 33);
        display.WriteString(body, Font_7x10, true);
        display.SetCursor(4, 44);
        display.WriteString(hint, Font_4x6, true);
        // The character being spelled, underlined where it sits in the name.
        if(d.mode == UiMode::kName)
        {
            const uint8_t x = static_cast<uint8_t>(4 + d.name_cursor * 7);
            display.DrawLine(x, 43, static_cast<uint8_t>(x + 6), 43, true);
        }
    }

    // Messages and a pending hold are moments, not states, so they appear over
    // what is there and then go away again -- rather than a line held empty for
    // them all the time.
    if(d.mode == UiMode::kNormal && (d.pending[0] || d.message[0]))
    {
        FixedCapStr<32> msg("");
        if(d.pending[0])
        {
            msg.Append("HOLD: ");
            msg.Append(d.pending);
        }
        else
            msg.Append(d.message);
        const int w  = 6 * static_cast<int>(msg.Size()) + 7;
        const int x0 = w >= 128 ? 0 : (128 - w) / 2;
        const int x1 = x0 + w > 127 ? 127 : x0 + w;
        display.DrawRect(static_cast<uint8_t>(x0), layout::kMsgTop, static_cast<uint8_t>(x1),
                         layout::kMsgBot, false, true);
        display.DrawRect(static_cast<uint8_t>(x0), layout::kMsgTop, static_cast<uint8_t>(x1),
                         layout::kMsgBot, true);
        display.SetCursor(static_cast<uint8_t>(x0 + 4), layout::kMsgTop + 3);
        display.WriteString(msg, Font_6x8, true);
    }

    g_in_oled = true;
    display.Update(); // pages out, PumpIo() between each
    g_in_oled = false;
}

/** What the screen currently depends on, folded into one value: redrawing only
 *  when this changes keeps the bus idle while nothing is moving, which is what
 *  leaves room for the hub. */
static uint32_t ScreenSignature(const DisplayState& d, uint32_t extra)
{
    uint32_t h = 2166136261u;
    auto     mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    auto     mix_ptr
        = [&mix](const char* p) { mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))); };
    mix(static_cast<uint32_t>(d.mode) * 0x1000u + static_cast<uint32_t>(d.browser_slot));
    mix(static_cast<uint32_t>(d.menu_index));
    mix_ptr(d.gesture);
    // BY CONTENT, not by pointer. These two live in fixed buffers that are
    // rewritten in place, so hashing the pointer says they never change -- which
    // left a settings page or a name waiting for the one-second heartbeat before
    // it redrew, and felt like the encoder was not working (2026-09-25).
    mix(static_cast<uint32_t>(d.page_index));
    for(const char* c = d.page_value; *c; c++)
        mix(static_cast<uint32_t>(*c));
    for(const char* c = d.name_text; *c; c++)
        mix(static_cast<uint32_t>(*c));
    mix(static_cast<uint32_t>(d.name_cursor));
    mix_ptr(d.pending);
    mix_ptr(d.engine);
    mix_ptr(d.voice_mode);
    mix(static_cast<uint32_t>(d.voices_active));
    mix(static_cast<uint32_t>(d.voices_max));
    for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
    {
        mix_ptr(d.slot_type[s]);
        mix(d.slot_bypass[s] ? 1u : 0u);
    }
    mix(d.freeze ? 1u : 0u);
    mix(d.shift ? 1u : 0u);
    mix_ptr(d.param);
    mix(static_cast<uint32_t>(d.value * 255.f)); // a pixel's worth of the bar
    mix(static_cast<uint32_t>(d.pickup_hint + 2));
    mix(d.show_value ? 1u : 0u);
    mix_ptr(d.message);
    mix_ptr(d.notice);
    mix_ptr(d.preset_name);
    mix(d.preset_dirty ? 1u : 0u);
    mix(static_cast<uint32_t>(d.readout));
    mix_ptr(d.choice);
    for(int i = 0; i < 4; i++)
        mix(static_cast<uint32_t>(d.env[i] * 255.f));
    mix(static_cast<uint32_t>(d.env_moving + 2));
    mix(extra);
    return h;
}

/** Bring-up dump: what the panel hardware actually reads.
 *
 *  GetVolts is the TRUE measured voltage -- never span-corrected -- so it says
 *  whether a floor or ceiling is in the wiring or in the software. GetNorm is
 *  the control value after span calibration. If volts reaches ~0.00 but norm
 *  does not reach 0.00, the span is at fault; if neither does, it is the
 *  hardware. Pot order is panel order (R1C1..R4C4). */
static void DumpPanel()
{
    if(g_have_adc)
    {
        for(int row = 0; row < 4; row++)
        {
            FixedCapStr<128> line("POT R");
            line.AppendInt(row + 1);
            line.Append(":");
            for(int col = 0; col < 4; col++)
            {
                const uint8_t ch = kPotChannel[row * 4 + col];
                line.Append(" J");
                // ch -> connector is uniform across both chips: J(6+ch).
                // ch0=J6 .. ch11=J17, ch12=J18 .. ch23=J29.
                line.AppendInt(6 + ch);
                line.Append("=");
                line.AppendFloat(adc.GetVolts(ch), 2);
                line.Append("V/");
                line.AppendFloat(pots.Normalize(row * 4 + col,
                                                adc.GetVolts(ch)),
                                 2);
                if(pots.IsSupplySwapped(row * 4 + col))
                    line.Append("S"); // supply/ground swapped: rescaled here
            }
            hw.PrintLine("%s", line.Cstr());
        }
    }
    if(g_have_hub)
    {
        // Straight from the hub. ReadReg does not depend on the probe having
        // succeeded, so this runs either way and prints whether each read was
        // ACKed -- a probe that fails while the scanner's identical probe
        // succeeded is itself the finding, and silently skipping the block (as
        // this did) hid it.
        uint8_t who = 0, fw = 0, caps = 0, status = 0, count = 0, sw_lo = 0, sw_hi = 0;
        const bool ok_who = dbg_hub.ReadReg(RODEO_HUB_REG_WHO_AM_I, &who);
        dbg_hub.ReadReg(RODEO_HUB_REG_FW_VERSION, &fw);
        dbg_hub.ReadReg(RODEO_HUB_REG_CAPABILITIES, &caps);
        dbg_hub.ReadReg(RODEO_HUB_REG_STATUS, &status);
        dbg_hub.ReadReg(RODEO_HUB_REG_ENC_COUNT, &count);
        const bool ok_sw = dbg_hub.ReadReg(RODEO_HUB_REG_SW_STATE_LO, &sw_lo)
                           && dbg_hub.ReadReg(RODEO_HUB_REG_SW_STATE_HI, &sw_hi);
        hw.PrintLine("HUB probe=%s read=%s/%s who=%d fw=%d caps=%d status=%d n=%d sw=%d/%d",
                     g_have_dbg_hub ? "ok" : "FAIL",
                     ok_who ? "ok" : "ERR",
                     ok_sw ? "ok" : "ERR",
                     static_cast<int>(who),
                     static_cast<int>(fw),
                     static_cast<int>(caps),
                     static_cast<int>(status),
                     static_cast<int>(count),
                     static_cast<int>(sw_lo),
                     static_cast<int>(sw_hi));
    }
    if(g_have_hub)
    {
        // ALL NINE hub slots, not just the five the panel uses: if an encoder
        // is on the wrong connector its counts show up here instead of where
        // they are expected. Slot N = hub connector J(N+3); * = pressed.
        FixedCapStr<128> line("ENC J3..J11:");
        for(int slot = 0; slot < 9; slot++)
        {
            line.Append(" ");
            line.AppendInt(static_cast<int>(encoders.GetPosition(0, static_cast<uint8_t>(slot))));
            if(encoders.IsPressed(0, static_cast<uint8_t>(slot)))
                line.Append("*");
        }
        hw.PrintLine("%s", line.Cstr());
    }
}

/** Heartbeat on the hub, so a wedged bus reports itself instead of looking like
 *  a frozen panel. Two bytes every half second; if three in a row fail, the I2C
 *  peripheral is re-initialised and the hub re-probed. Whether that clears a
 *  real wedge depends on what is holding the bus -- but the count in the status
 *  line says it happened, which is the part that was missing. */
static uint32_t g_bus_recoveries = 0;
static int      g_hb_fails       = 0;

static void BusHeartbeat()
{
    if(!g_have_hub)
        return;
    uint8_t who = 0;
    if(dbg_hub.ReadReg(RODEO_HUB_REG_WHO_AM_I, &who) && who == 0x9E)
    {
        g_hb_fails = 0;
        return;
    }
    if(++g_hb_fails < 3)
        return;
    g_hb_fails = 0;
    g_bus_recoveries++;
    dbg_i2c.Init(rodeo::EncoderHub::DefaultI2cConfig(kI2cSpeed));
    dbg_hub.Init(&dbg_i2c, 0x30);
}

static void WaitBlocks(uint32_t n)
{
    const uint32_t start = g_blocks;
    while((g_blocks - start) < n)
        ;
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(kBlockSize);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    daisysp::ReverbSc* reverb = new(g_reverb_mem) daisysp::ReverbSc();
    chain.Init(kSampleRate, {g_grain_l, g_grain_r, g_pre_l, g_pre_r, reverb});
    voices.Init(kSampleRate, kBlockSize);
    ui.Init(patch, globals);
    pots.Init();
    ApplyPatch();

    // --- panel hardware, each optional ---------------------------------
    // Pots: only the 16 the panel uses are scanned, which is what keeps the
    // per-pot rate at ~108 Hz (control map, *Analog*).
    rodeo::RodeoAdc::Config adc_cfg;
    adc_cfg.populated_mask = 0;
    for(int i = 0; i < Ui::kNumPots; i++)
        adc_cfg.populated_mask |= (1u << kPotChannel[i]);
    g_have_adc = adc.Init(adc_cfg);

    // Encoders: hub 0 at 0x30. On Rev-A the hub also anchors the I2C bus, so
    // the OLED depends on it being plugged in.
    rodeo::RodeoEncoders::Config enc_cfg;
    enc_cfg.hub_mask = 0x01;
    enc_cfg.speed    = kI2cSpeed;
    g_have_hub       = encoders.Init(enc_cfg);

    // Panel wiring map: restored from flash, or thrown away if MAIN is held
    // down at power-up -- the gesture for a satellite or cable that has been
    // changed, whose stored verdict belongs to the run that left.
    {
        PanelPots::Calibration defaults;
        pots.Export(defaults); // nothing learned yet: the shape of a blank store
        cal_store.Init(defaults, kCalStoreOffset);
        // EVERY SAMPLE, not one. A single stray reading from a hub that has
        // only just powered up must not wipe the wiring map -- that would look
        // exactly like a map that never persisted, and it would happen at a
        // different boot each time.
        int held = 0;
        if(g_have_hub)
            for(int i = 0; i < 8; i++)
            {
                encoders.Update();
                System::Delay(5);
                if(encoders.IsPressed(0, kEncoderSlot[4])) // MAIN
                    held++;
            }
        const bool clear = held == 8;
        if(clear)
        {
            pots.Forget();
            encoders.PopPress(0, kEncoderSlot[4]); // not a gesture as well
            g_cal_dirty      = true;
            g_cal_changed_ms = System::GetNow();
        }
        else
        {
            g_cal_restored = pots.Import(cal_store.GetSettings());
        }
        g_cal_cleared  = clear;
        g_cal_revision = pots.Revision();
    }

    // The preset bank. The factory sounds are the store's defaults, so an
    // instrument that has never been saved to still boots with something to
    // play, and an erased store comes back with them rather than empty slots.
    {
        BuildFactoryBank(g_factory_bank);
        preset_store.Init(g_factory_bank, kPresetStoreOffset);
        if(!preset_store.GetSettings().Valid())
            preset_store.GetSettings() = g_factory_bank; // older layout, or corrupt
        // New factory sounds reach an instrument that has already been saved to
        // by filling its empty slots. Nothing already there is touched.
        g_slots_filled = FillEmptySlots(preset_store.GetSettings(), g_factory_bank);
        if(g_slots_filled)
            preset_store.Save();
        ui.SetBank(&preset_store.GetSettings());
        g_gesture.Init(g_gesture_buf, GestureRecorder::kMaxFrames);
        ui.SetGesture(&g_gesture);

        // Boot into whatever was last loaded -- as STORED, so power-up is
        // always a known-good sound and the screen's * is what tells you the
        // patch has drifted since (Derek, 2026-09-24). Slot 1 if nothing has
        // been chosen yet, or if the stored choice is out of range.
        InstrumentState defaults{kStateMagic, kStateVersion, 0, 0, 1, 0};
        state_store.Init(defaults, kStateStoreOffset);
        InstrumentState& st = state_store.GetSettings();
        if(st.magic != kStateMagic || st.version != kStateVersion
           || st.last_preset >= PresetBank::kNumSlots || st.midi_channel < 0
           || st.midi_channel > 16 || st.midi_out_channel < 1 || st.midi_out_channel > 16)
            st = defaults;
        globals.midi_channel     = st.midi_channel;
        globals.midi_out_channel = st.midi_out_channel;
        const StoredPreset& boot = preset_store.GetSettings().slot[st.last_preset];
        if(boot.used)
        {
            bool known = true;
            PresetToPatch(boot, patch, &known);
            ui.NotePresetLoaded(boot);
            ui.DropPickup();
            current_engine = patch.engine;
            voices.SetEngine(patch.engine);
            ApplyPatch();
            g_boot_preset       = static_cast<int>(st.last_preset);
            g_boot_engine_known = known;
            g_boot_gesture      = LoadGestureForSlot(g_boot_preset);
        }
    }

    if(g_have_hub)
    {
        // Diagnostic view of the same hub, on the same bus and speed. Both
        // outcomes are reported rather than gating the dump, because "the
        // scanner's probe worked and a second identical one did not" is itself
        // worth knowing.
        I2CHandle::Config      dbg_cfg = rodeo::EncoderHub::DefaultI2cConfig(kI2cSpeed);
        const I2CHandle::Result dbg_r  = dbg_i2c.Init(dbg_cfg);
        g_have_dbg_hub                 = dbg_hub.Init(&dbg_i2c, 0x30);
        hw.PrintLine("dbg hub: i2c init %s, probe %s",
                     dbg_r == I2CHandle::Result::OK ? "ok" : "ERR",
                     g_have_dbg_hub ? "ok" : "FAIL");

        OledDisplayType::Config ocfg;
        auto& tcfg                     = ocfg.driver_config.transport_config;
        tcfg.i2c_address               = 0x3C;
        tcfg.i2c_config.periph         = I2CHandle::Config::Peripheral::I2C_1;
        tcfg.i2c_config.pin_config.scl = seed::D11;
        tcfg.i2c_config.pin_config.sda = seed::D12;
        tcfg.i2c_config.speed          = kI2cSpeed; // same bus, same speed
        display.Init(ocfg);
        display.Fill(false);
        display.Update();
        g_have_oled = rodeo::OledLinkOk();
        // The OLED yields mid-transfer so the panel stays responsive.
        rodeo::SetOledYield(PumpIo);
    }

    g_block_ticks = static_cast<uint32_t>(
        static_cast<uint64_t>(System::GetTickFreq()) * kBlockSize / 48000u);

    midi = new(g_midi_mem) MidiUartHandler();
    MidiUartHandler::Config midi_cfg; // USART1, libDaisy defaults = the TRS jacks
    midi->Init(midi_cfg);
    midi->StartReceive();

    hw.StartLog(false);
    System::Delay(1000);
    // Build stamp: without it a flash that silently did not take looks exactly
    // like a firmware bug, which cost a round of hardware debugging on
    // 2026-09-23. Compare this against when you built.
    hw.PrintLine("=== rodeo-002 synth v%s === built %s %s",
                 kVersionString,
                 __DATE__,
                 __TIME__);
    hw.PrintLine("panel: pots %s | encoder hub %s | OLED %s",
                 g_have_adc ? "yes" : "NO",
                 g_have_hub ? "yes" : "NO",
                 g_have_oled ? "yes" : "NO");
    if(!g_have_hub)
        hw.PrintLine("  (no hub: Rev-A hubs carry the I2C pull-ups, so no OLED either)");
    if(g_space_mix_pinned)
        hw.PrintLine("NOTE: Space Mix pinned to %d%% until its pot (R4C4 / J25) shows travel",
                     static_cast<int>(kSpaceMixValue * 100.f));
    if(g_slots_filled)
        hw.PrintLine("presets: %d empty slot(s) filled from the factory bank", g_slots_filled);
    if(g_boot_preset >= 0)
        hw.PrintLine("preset %d loaded at boot: %s%s",
                     g_boot_preset + 1,
                     preset_store.GetSettings().slot[g_boot_preset].name,
                     g_boot_engine_known ? "" : "  (its engine is gone; using virtual-analog)");
    if(g_boot_gesture)
        hw.PrintLine("  ...with a gesture, now looping");
    else
        hw.PrintLine("no preset loaded at boot (slot 1 is empty)");
    {
        int known = 0, swapped = 0;
        for(int i = 0; i < Ui::kNumPots; i++)
        {
            if(pots.WiringKnown(i))
                known++;
            if(pots.IsSupplySwapped(i))
                swapped++;
        }
        // Said plainly, because two very different things look the same from
        // the front panel: a wiring map that did not persist, and pickup asking
        // for every knob after a preset loads.
        if(g_cal_cleared)
            hw.PrintLine("panel wiring: CLEARED (MAIN held through the whole boot check)");
        else if(g_cal_restored)
            hw.PrintLine("panel wiring: RESTORED from flash -- %d/%d runs known, %d swapped",
                         known, Ui::kNumPots, swapped);
        else
            hw.PrintLine("panel wiring: NOTHING STORED yet (blank, or an older layout)");
        hw.PrintLine("  ranges are fixed, never calibrated. A run proves its wiring the first");
        hw.PrintLine("  time that pot reaches either stop, and that verdict is what persists.");
        hw.PrintLine("  Knobs inert after boot are PICKUP, not calibration: a pot takes hold");
        hw.PrintLine("  when it crosses the value the preset loaded, and the screen says UP/DN.");
    }
    hw.PrintLine("MIDI IN on the TRS jack. Every panel parameter is also on CC:");
    hw.PrintLine("  70-85  main layer, panel order (70 harmonics .. 85 space mix)");
    hw.PrintLine("  90-105 shift layer, same order (90 env>timbre .. 105 space damp)");
    hw.PrintLine("  110 engine  111-114 slot types  115 voice mode  116 freeze");
    hw.PrintLine("  117-120 slot bypass   1 mod wheel   64 sustain");

    hw.StartAudio(AudioCallback);

    uint32_t last_apply = 0, last_print = 0, last_draw = 0, last_sig = 0, loops = 0,
             last_hb = 0, last_scope = 0;
    while(1)
    {
        PumpIo();

        // --- the panel ---------------------------------------------------
        UiInput in;
        in.now_ms = System::GetNow();
        if(g_have_adc)
            for(int i = 0; i < Ui::kNumPots; i++)
                // The TRUE voltage, conditioned here: PanelPots maps each
                // wiring's fixed travel onto 0..1 and detects a straight-built
                // analog run, whose range is shifted.
                in.pot[i] = pots.Normalize(i, adc.GetVolts(kPotChannel[i]));
        if(g_have_hub)
            for(int e = 0; e < 5; e++)
            {
                in.enc_delta[e] = encoders.GetDelta(0, kEncoderSlot[e]);
                in.enc_held[e]  = encoders.IsPressed(0, kEncoderSlot[e]);
            }
        if(g_have_adc || g_have_hub)
            g_discrete_changed |= ui.Update(in);

        // Applied after the Ui, so the dead pot cannot put it back to zero.
        // Released for good the moment that channel shows real travel, which a
        // dead one never does; pickup then holds the value until the knob
        // reaches it, so nothing jumps.
        if(g_space_mix_pinned)
        {
            if(pots.SpanSeen(kSpaceMixPot) > kPotAliveVolts)
            {
                g_space_mix_pinned = false;
                hw.PrintLine("Space Mix pot is alive -- pin released, the knob has it now");
            }
            else
                patch.amount[TextureChain::kSpace] = kSpaceMixValue;
        }

        // The wiring map is worked out by turning the knobs, so it is written a
        // few seconds after the last verdict changed -- never from the audio
        // callback. The erase stalls this loop, not the audio.
        if(pots.Revision() != g_cal_revision)
        {
            g_cal_revision   = pots.Revision();
            g_cal_changed_ms = in.now_ms;
            g_cal_dirty      = true;
        }
        else if(g_cal_dirty && in.now_ms - g_cal_changed_ms > kCalSettleMs)
        {
            g_cal_dirty = false;
            pots.Export(cal_store.GetSettings());
            cal_store.Save(); // no-op if nothing actually differs
            int swapped = 0;
            for(int i = 0; i < Ui::kNumPots; i++)
                if(pots.IsSupplySwapped(i))
                    swapped++;
            hw.PrintLine("panel wiring: saved, %d/%d runs supply-swapped",
                         swapped, Ui::kNumPots);
        }
        if(state_store.GetSettings().midi_channel != globals.midi_channel
           || state_store.GetSettings().midi_out_channel != globals.midi_out_channel)
        {
            state_store.GetSettings().midi_channel     = globals.midi_channel;
            state_store.GetSettings().midi_out_channel = globals.midi_out_channel;
            g_state_dirty                              = true;
            g_state_changed_ms                         = in.now_ms;
        }
        if(g_state_dirty && in.now_ms - g_state_changed_ms > kCalSettleMs)
        {
            g_state_dirty = false;
            state_store.Save();
        }

        SendCcChanges(in.now_ms);

        // --- preset load / save -----------------------------------------
        // Both are done here, never in the callback: a save erases a 64 KB
        // sector, which stalls this loop for a fraction of a second.
        {
            const PresetRequest req = ui.TakePresetRequest();
            PresetBank&         bank = preset_store.GetSettings();
            if(req.action == PresetAction::kLoad)
            {
                const StoredPreset& sp = bank.slot[req.slot];
                if(!sp.used)
                    hw.PrintLine("preset %d: empty slot, nothing loaded", req.slot + 1);
                else
                {
                    bool engine_known = true;
                    PresetToPatch(sp, patch, &engine_known);
                    // The pots are wherever the last patch left them, so every
                    // one of them has to be caught again before it takes hold.
                    ui.DropPickup();
                    ui.NotePresetLoaded(sp);
                    RememberPreset(req.slot, in.now_ms);
                    g_discrete_changed = true;
                    // A preset saved with a gesture starts moving when it loads.
                    const bool with_gesture = LoadGestureForSlot(req.slot);
                    hw.PrintLine("preset %d loaded: %s%s%s",
                                 req.slot + 1,
                                 sp.name,
                                 with_gesture ? "  (+ gesture, looping)" : "",
                                 engine_known
                                     ? ""
                                     : "  (WARNING: its engine is gone from this build;"
                                       " using virtual-analog)");
                }
            }
            else if(req.action == PresetAction::kSave)
            {
                FixedCapStr<16> fallback("User ");
                fallback.AppendInt(req.slot + 1);
                PatchToPreset(patch,
                              req.name && req.name[0] ? req.name : fallback.Cstr(),
                              bank.slot[req.slot]);
                preset_store.Save();
                SaveGestureForSlot(req.slot);
                // Saving makes the patch the preset: no longer modified, and
                // this is the slot to come back to next time.
                ui.NotePresetLoaded(bank.slot[req.slot]);
                RememberPreset(req.slot, in.now_ms);
                hw.PrintLine("preset %d saved: %s%s",
                             req.slot + 1,
                             bank.slot[req.slot].name,
                             g_gesture.length_frames() ? "  (+ gesture)" : "");
            }
        }

        // An engine swap rebuilds every voice, so the callback is kept out of
        // the voices while it happens.
        if(g_discrete_changed)
        {
            g_discrete_changed = false;
            if(patch.engine != current_engine)
            {
                g_voices_locked = true;
                WaitBlocks(2);
                voices.SetEngine(patch.engine);
                current_engine  = patch.engine;
                g_voices_locked = false;
            }
            ApplyPatch();
            // Recompute the cap: an expensive Space type costs voices.
            voices.SetMaxVoices(CostModel::MaxVoices(patch.engine, CostModel::ChainCost(chain)));
        }

        if(g_blocks - last_apply >= 5) // ~200 Hz
        {
            last_apply = g_blocks;
            ApplyPatch();
        }

        // A full frame at 100 kHz is ~100 ms of bus time, so redraw at ~8 fps
        // and leave the bus free for the hub in between.
        // Redraw when the screen would actually differ, at up to ~25 fps, plus
        // a heartbeat every second so the load figure stays honest. Idle costs
        // the bus nothing, which is what keeps the encoders responsive.
        if(g_blocks - last_hb >= 500)
        {
            last_hb = g_blocks;
            BusHeartbeat();
        }

        // TWO PACES, because the two things on screen change for different
        // reasons. Anything the panel did redraws as soon as it happened, at up
        // to ~25 fps -- that is a knob feeling responsive. The scope changes
        // constantly on its own, so BY ITSELF it redraws at ~10 fps. Turning a
        // knob therefore costs no more bus than it already did: the frame was
        // going out anyway, and a frame is the same 1024 bytes whatever is
        // drawn in it. Only a scope moving under a still panel is rate-limited.
        ui.Refresh(voices);
        const DisplayState& ds = ui.display();
        const bool          scope_on
            = ds.mode == UiMode::kNormal && ds.readout != Readout::kEnvelope;
        // A frame is ~23 ms of bus, so the floor between redraws is what the
        // panel's latency is made of: at 40 ms a turn could take 65 ms to show.
        // 24 nearly halves that and still leaves the hub most of the bus, which
        // it does not need anyway -- it accumulates deltas and latches presses.
        if(g_have_oled && g_blocks - last_draw >= 24)
        {
            const uint32_t sig        = ScreenSignature(ds, 0u); // all but the scope
            const bool     ui_changed = sig != last_sig;
            const bool     scope_due
                = scope_on && g_blocks - last_draw >= 100 && g_scope.Hash() != last_scope;
            if(ui_changed || scope_due || g_blocks - last_draw >= 1000)
            {
                last_sig   = sig;
                last_scope = scope_on ? g_scope.Hash() : 0u;
                last_draw  = g_blocks;
                DrawScreen();
            }
        }

        if(g_blocks - last_print >= 2000) // every 2 s
        {
            last_print            = g_blocks;
            const uint32_t blocks = g_busy_blocks;
            const float    load   = blocks ? static_cast<float>(g_busy_ticks)
                                            / (static_cast<float>(blocks * g_block_ticks)) * 100.f
                                         : 0.f;
            g_busy_ticks  = 0;
            g_busy_blocks = 0;
            g_last_load            = load;
            DumpPanel();
            const float chain_cost = CostModel::ChainCost(chain);
            const float predicted
                = CostModel::PredictLoad(patch.engine, voices.ActiveVoices(), chain_cost);
            hw.PrintLine("%s %s | voices %d/%d | chain %s+%s+%s+%s | load %d%% (predicted %d%%) | loop %dHz | bus fixes %d",
                         EngineName(patch.engine),
                         VoiceModeName(patch.voice_mode),
                         voices.ActiveVoices(),
                         voices.GetMaxVoices(),
                         TextureChain::TypeName(0, chain.GetType(0)),
                         TextureChain::TypeName(1, chain.GetType(1)),
                         TextureChain::TypeName(2, chain.GetType(2)),
                         TextureChain::TypeName(3, chain.GetType(3)),
                         static_cast<int>(load),
                         static_cast<int>(predicted),
                         static_cast<int>(loops / 2), // over the 2 s window
                         static_cast<int>(g_bus_recoveries));
            loops = 0;
        }
        loops++;
        hw.SetLed(voices.ActiveVoices() > 0);

    }
}
