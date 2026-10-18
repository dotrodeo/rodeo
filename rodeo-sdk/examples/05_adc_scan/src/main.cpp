// Rodeo bring-up 05 — full control-surface scan (both ADS114S08 chips).
//
// Brings up the Layer 2 rodeo::RodeoAdc scanner: inits U3+U4, pipelines a
// non-blocking round-robin across all populated channels, and streams a
// human-readable view over USB serial.
//
//   Live view (~5 Hz): two rows of volts, one per chip —
//       U3 | 0.251 0.260 ...   (ch 0..11,  connectors J6..J17)
//       U4 | 0.234 0.240 ...   (ch 12..23, connectors J18..J29)
//   Drift report (~0.5 Hz): any channel whose span since boot exceeds 0.10 V,
//       shown as min..max, plus that channel's LEARNED span and the 0..1 control
//       value GetNorm() now returns — so a swept pot auto-surfaces and you can
//       confirm both its raw volts and that it reaches a true 0.000..1.000.
//       Idle floating inputs stay quiet.
//
//   Note a satellite pot tops out near 3.00 V, not 3.3 V: rodeo-potentiometer
//   feeds the pot's top leg from +3V3 through R1 = 1 kΩ, so the top of travel is
//   3.3 * Rpot/(Rpot+1k) — 90.9 % of full scale for the nominal 10 kΩ pot. That
//   is expected, not a fault. RodeoAdc learns each channel's real travel as it
//   is swept (rodeo::PotSpan) so GetNorm() still spans a true 0..1; the "norm"
//   and "learned" columns below are how you verify it.
//
//   PASS: sweep a pot on any channel -> its column tracks 0..~3.0 V and it shows
//         up in the drift report; after one full stop-to-stop sweep its norm
//         reads 0.000 at one end and 1.000 at the other; grounded inputs read
//         ~0; a missing chip's 12 columns stay 0 and its presence line prints
//         FAILED at boot.
//
// Bench harness for the multi-channel + revisit-rate validation (needs the JST
// harness). See planning/RODEO_ADC_LIB_DESIGN.md.

#include "daisy_seed.h"
#include "board.h"
#include "rodeo_adc.h"
#include "status_led.h"

using namespace daisy;

DaisySeed       hw;
rodeo::RodeoAdc adc;
rodeo::StatusLed status;

static constexpr uint8_t kNch = rodeo::RodeoAdc::kNumChannels;

// Per-channel span tracking (codes), seeded so the first sample sets both.
uint16_t min_code[kNch];
uint16_t max_code[kNch];

// Print one "W.fff" volts field for a 0..3.3 V reading (Daisy logger has no %f).
static void PrintVolts(float v)
{
    int mv = static_cast<int>(v * 1000.0f);
    if(mv < 0)
        mv = 0;
    hw.Print(" %d.%03d", mv / 1000, mv % 1000);
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    status.Init();

    rodeo::RodeoAdc::Config cfg; // all 24 channels, 1000 SPS low-latency
    adc.Init(cfg);

    hw.PrintLine("=== Rodeo 05_adc_scan (24 ch, U3+U4) ===");
    hw.PrintLine("U3 (chip A, ch 0-11, J6-J17):  %s",
                 adc.ChipPresent(0) ? "present" : "FAILED");
    hw.PrintLine("U4 (chip B, ch 12-23, J18-J29): %s",
                 adc.ChipPresent(1) ? "present" : "FAILED");

    // Span tracking starts after a warm-up so the boot-time zero cache (before
    // the scan has populated every channel) can't pin every min at 0 and make
    // idle channels falsely report as "moved".
    const uint32_t t_boot   = System::GetNow();
    bool           tracking = false;

    uint32_t last_view  = System::GetNow();
    uint32_t last_drift = System::GetNow();
    for(;;)
    {
        adc.Update(); // pump the scan as fast as the main loop turns

        uint32_t now = System::GetNow();

        if(!tracking && now - t_boot >= 300) // seed once all channels are live
        {
            for(uint8_t ch = 0; ch < kNch; ++ch)
                min_code[ch] = max_code[ch] = adc.GetCode(ch);
            tracking = true;
        }
        if(tracking) // per-channel span, updated continuously
        {
            for(uint8_t ch = 0; ch < kNch; ++ch)
            {
                uint16_t c = adc.GetCode(ch);
                if(c < min_code[ch])
                    min_code[ch] = c;
                if(c > max_code[ch])
                    max_code[ch] = c;
            }
        }

        if(now - last_view >= 200) // ~5 Hz labeled live view
        {
            last_view = now;
            status.Heartbeat(200); // blink in step with the refresh

            hw.Print("U3 |");
            for(uint8_t ch = 0; ch < 12; ++ch)
                PrintVolts(adc.GetVolts(ch));
            hw.PrintLine("");

            hw.Print("U4 |");
            for(uint8_t ch = 12; ch < kNch; ++ch)
                PrintVolts(adc.GetVolts(ch));
            hw.PrintLine("");
        }

        if(now - last_drift >= 2000) // ~0.5 Hz drift report
        {
            last_drift = now;
            bool any = false;
            for(uint8_t ch = 0; ch < kNch; ++ch)
            {
                float vmin = (static_cast<float>(min_code[ch]) / 32767.0f)
                             * rodeo::Ads114s08::kVref;
                float vmax = (static_cast<float>(max_code[ch]) / 32767.0f)
                             * rodeo::Ads114s08::kVref;
                float span = vmax - vmin;
                if(span <= 0.10f) // ignore idle float jitter
                    continue;
                any        = true;
                int mn     = static_cast<int>(vmin * 1000.0f);
                int mx     = static_cast<int>(vmax * 1000.0f);
                int sp     = static_cast<int>(span * 1000.0f);
                // The learned travel RodeoAdc is normalizing against, and the
                // control value that falls out of it. A fully swept pot should
                // show norm 0.000 at one stop and 1.000 at the other even though
                // the raw volts only reach ~3.0 (see the R1 note at the top).
                const rodeo::PotSpan &ls = adc.GetSpan(ch);
                int n = static_cast<int>(adc.GetNorm(ch) * 1000.0f);
                hw.PrintLine("  moved ch%02d: %d.%03d..%d.%03d V (span %d.%03d)"
                             "  learned %u..%u  norm %d.%03d",
                             ch,
                             mn / 1000, mn % 1000,
                             mx / 1000, mx % 1000,
                             sp / 1000, sp % 1000,
                             static_cast<unsigned>(ls.lo),
                             static_cast<unsigned>(ls.hi),
                             n / 1000, n % 1000);
            }
            if(!any)
                hw.PrintLine("  (no channel moved >0.10 V yet — sweep a pot)");
        }
    }
}
