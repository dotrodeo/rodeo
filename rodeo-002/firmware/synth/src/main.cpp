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

#include "cost_model.h"
#include "panel_pot.h"
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
static constexpr float kSpaceMixValue = 1.f;
VoiceManager    voices;
TextureChain    chain;
EngineId        current_engine = EngineId::kVirtualAnalog;

volatile bool     g_voices_locked = false; // engine swap in progress
volatile uint32_t g_blocks        = 0;
volatile uint32_t g_busy_ticks    = 0;
volatile uint32_t g_busy_blocks   = 0;
static uint32_t   g_block_ticks   = 1;
static float      g_last_load     = 0.f; // for the screen's footer

static float g_mix[kBlockSize];

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
    chain.ProcessBlock(out[0], out[1], size);

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
    kCcCutoff, kCcResonance, kCcSpread, kCcMovementAmt,
    kCcAttack, kCcRelease, kCcVolume, kCcTextureAmt,
    kCcSpaceTime, kCcSpaceFeedback, kCcSpaceGrain, kCcSpaceMix,
    // shift layer, same order
    kCcEnvToTimbre = 90, kCcEnvToMorph, kCcLfoRate, kCcCharacterBias,
    kCcFilterType, kCcFilterEnv, kCcLfoDest, kCcMovementRate,
    kCcDecay, kCcSustain, kCcLfoDepth, kCcTextureTone,
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
        case kCcRelease: patch.release = f; return;
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
        case kCcDecay: patch.decay = f; return;
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

/** 128x64: a header line, the four slots, the parameter readout, a footer. */
static void DrawScreen(float load)
{
    const DisplayState& d = ui.display();
    display.Fill(false);

    // Header: engine, voice mode, voices in use.
    FixedCapStr<32> line(d.engine);
    line.Append(" ");
    line.Append(d.voice_mode);
    display.SetCursor(0, 0);
    display.WriteString(line, Font_6x8, true);
    FixedCapStr<8> voices_str("");
    voices_str.AppendInt(d.voices_active);
    voices_str.Append("/");
    voices_str.AppendInt(d.voices_max);
    display.SetCursor(128 - 6 * 3, 0);
    display.WriteString(voices_str, Font_6x8, true);
    display.DrawLine(0, 9, 127, 9, true);

    // The chain: four slot names, bypassed ones struck through.
    for(int s = 0; s < fx::TextureChain::kNumSlots; s++)
    {
        const uint8_t x = static_cast<uint8_t>(s * 32);
        FixedCapStr<8> name("");
        for(int i = 0; i < 5 && d.slot_type[s][i]; i++)
            name.Append(d.slot_type[s] + i, 1);
        display.SetCursor(x + 1, 12);
        display.WriteString(name, Font_4x6, true);
        if(d.slot_bypass[s])
            display.DrawLine(x + 1, 15, x + 29, 15, true);
    }

    // The parameter last touched, with its value as a bar.
    if(d.show_value)
    {
        display.SetCursor(0, 24);
        display.WriteString(d.param, Font_7x10, true);
        const uint8_t w = static_cast<uint8_t>(d.value * 124.f);
        display.DrawRect(0, 38, 127, 48, true);
        if(w > 1)
            display.DrawRect(2, 40, static_cast<uint8_t>(2 + w), 46, true, true);
        // Pickup: which way to turn to catch the stored value.
        if(d.pickup_hint != 0)
        {
            display.SetCursor(100, 24);
            display.WriteString(d.pickup_hint > 0 ? "UP" : "DN", Font_7x10, true);
        }
    }

    // Footer: shift, freeze, a message, and the measured load.
    FixedCapStr<32> foot("");
    if(d.shift)
        foot.Append("SHIFT ");
    if(d.freeze)
        foot.Append("FREEZE ");
    foot.Append(d.message);
    display.SetCursor(0, 54);
    display.WriteString(foot, Font_6x8, true);
    FixedCapStr<8> load_str("");
    load_str.AppendInt(static_cast<int>(load));
    load_str.Append("%");
    display.SetCursor(128 - 6 * 4, 54);
    display.WriteString(load_str, Font_6x8, true);

    g_in_oled = true;
    display.Update(); // pages out, PumpIo() between each
    g_in_oled = false;
}

/** What the screen currently depends on, folded into one value: redrawing only
 *  when this changes keeps the bus idle while nothing is moving, which is what
 *  leaves room for the hub. */
static uint32_t ScreenSignature(const DisplayState& d, float load)
{
    uint32_t h = 2166136261u;
    auto     mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    auto     mix_ptr
        = [&mix](const char* p) { mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))); };
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
    mix(static_cast<uint32_t>(load)); // whole percent
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
    hw.PrintLine("=== rodeo-002 synth === built %s %s", __DATE__, __TIME__);
    hw.PrintLine("panel: pots %s | encoder hub %s | OLED %s",
                 g_have_adc ? "yes" : "NO",
                 g_have_hub ? "yes" : "NO",
                 g_have_oled ? "yes" : "NO");
    if(!g_have_hub)
        hw.PrintLine("  (no hub: Rev-A hubs carry the I2C pull-ups, so no OLED either)");
    if(kPinSpaceMix)
        hw.PrintLine("NOTE: Space Mix pinned to %d%% -- its pot (R4C4 / J25) is dead",
                     static_cast<int>(kSpaceMixValue * 100.f));
    hw.PrintLine("MIDI IN on the TRS jack. Every panel parameter is also on CC:");
    hw.PrintLine("  70-85  main layer, panel order (70 harmonics .. 85 space mix)");
    hw.PrintLine("  90-105 shift layer, same order (90 env>timbre .. 105 space damp)");
    hw.PrintLine("  110 engine  111-114 slot types  115 voice mode  116 freeze");
    hw.PrintLine("  117-120 slot bypass   1 mod wheel   64 sustain");

    hw.StartAudio(AudioCallback);

    uint32_t last_apply = 0, last_print = 0, last_draw = 0, last_sig = 0, loops = 0,
             last_hb = 0;
    while(1)
    {
        PumpIo();

        // --- the panel ---------------------------------------------------
        UiInput in;
        in.now_ms = System::GetNow();
        if(g_have_adc)
            for(int i = 0; i < Ui::kNumPots; i++)
                // The TRUE voltage, conditioned here: PanelPots learns each
                // channel's travel and detects a straight-built cable (which
                // reads backwards and never reaches zero).
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
        if(kPinSpaceMix)
            patch.amount[TextureChain::kSpace] = kSpaceMixValue;

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

        if(g_have_oled && g_blocks - last_draw >= 40)
        {
            ui.Refresh(voices);
            const uint32_t sig = ScreenSignature(ui.display(), g_last_load);
            if(sig != last_sig || g_blocks - last_draw >= 1000)
            {
                last_sig  = sig;
                last_draw = g_blocks;
                DrawScreen(g_last_load);
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
