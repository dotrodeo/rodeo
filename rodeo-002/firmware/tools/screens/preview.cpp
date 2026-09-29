// rodeo-002 — render the instrument's screens on a laptop.
//
// The same draw calls and the SAME FONTS the hardware uses: libDaisy's
// OledDisplay is a template over its driver, and everything under it is plain
// C++ -- only the driver knows about STM32. So a stub driver holding a
// framebuffer gives pixel-exact screens without flashing anything, which is
// what makes it worth having: a preview that is not the real drawing code is a
// preview that can lie to you.
//
//   make            # renders every screen to build/*.pbm and prints them
//   make ascii      # prints only
//
// PBMs open in Preview; they are 128x64, so zoom hard.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hid/disp/oled_display.h"

#include "screen.h"

using namespace rodeo002;

namespace
{
/** Everything OledDisplay asks of a driver, and nothing else. */
struct HostDriver
{
    struct Config
    {
    };
    static constexpr int kW = 128, kH = 64;
    bool                 fb[kW * kH] = {};

    void     Init(Config) {}
    uint16_t Height() const { return kH; }
    uint16_t Width() const { return kW; }
    void     Fill(bool on)
    {
        for(auto& p : fb)
            p = on;
    }
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x < kW && y < kH)
            fb[y * kW + x] = on;
    }
    void Update() {}
    bool UpdateFinished() { return true; }
    void Reset() {}
    void SendCommand(uint8_t) {}
    void SendData(uint8_t*, size_t) {}
};

using Display = daisy::OledDisplay<HostDriver>;

Display& TheDisplay()
{
    static Display  d;
    static bool     init = false;
    if(!init)
    {
        Display::Config c;
        d.Init(c);
        init = true;
    }
    return d;
}

/** Reaches the framebuffer the only way the template allows: ask the display
 *  what it drew, pixel by pixel. Slow, and 8192 pixels is nothing. */
struct Shot
{
    std::string       name;
    std::vector<bool> px;
};

Shot Capture(const std::string& name, const DisplayState& d, Scope& scope)
{
    Display& disp = TheDisplay();
    screen::Draw(disp, d, scope);
    Shot s;
    s.name = name;
    s.px.resize(128 * 64);
    // OledDisplay has no read-back, so the driver is asked directly.
    HostDriver* drv = reinterpret_cast<HostDriver*>(&disp);
    for(int i = 0; i < 128 * 64; i++)
        s.px[i] = drv->fb[i];
    return s;
}

void PrintAscii(const Shot& s)
{
    printf("\n%s\n+", s.name.c_str());
    for(int x = 0; x < 128; x++)
        printf("-");
    printf("+\n");
    for(int y = 0; y < 64; y++)
    {
        printf("|");
        for(int x = 0; x < 128; x++)
            printf("%c", s.px[y * 128 + x] ? '#' : ' ');
        printf("|\n");
    }
    printf("+");
    for(int x = 0; x < 128; x++)
        printf("-");
    printf("+\n");
}

void WritePbm(const Shot& s)
{
    std::string path = "build/" + s.name + ".pbm";
    FILE*       f    = fopen(path.c_str(), "wb");
    if(!f)
        return;
    fprintf(f, "P1\n128 64\n");
    for(int y = 0; y < 64; y++)
    {
        for(int x = 0; x < 128; x++)
            fprintf(f, "%d ", s.px[y * 128 + x] ? 1 : 0);
        fprintf(f, "\n");
    }
    fclose(f);
}

/** A scope with something in it, so the idle screen is not a flat line. */
Scope& TheScope()
{
    static Scope s;
    static bool  filled = false;
    if(!filled)
    {
        s.Init();
        for(int i = 0; i < 48000; i++)
            s.Push(0.6f * sinf(2.f * 3.14159265f * 220.f * i / 48000.f));
        uint8_t y[Scope::kWidth];
        for(int i = 0; i < 20; i++)
            s.Trace(20, 46, y); // let the gain settle
        filled = true;
    }
    return s;
}

DisplayState Base()
{
    DisplayState d;
    d.engine         = "variable-saw";
    d.voice_mode     = "Poly";
    d.voice_short    = "POL";
    d.voices_active  = 3;
    d.voices_max     = 8;
    d.slot_type[0]   = "Drive";
    d.slot_type[1]   = "Chorus";
    d.slot_type[2]   = "Crush";
    d.slot_type[3]   = "Echo";
    d.slot_bypass[2] = true;
    d.preset_name    = "Chroma Pad";
    d.preset_dirty   = true;
    return d;
}
} // namespace

int main(int argc, char** argv)
{
    const bool ascii_only = argc > 1 && std::strcmp(argv[1], "ascii") == 0;
    std::vector<Shot> shots;
    Scope&            scope = TheScope();

    {
        DisplayState d = Base();
        shots.push_back(Capture("01-idle", d, scope));
    }
    {
        DisplayState d = Base();
        d.readout      = Readout::kBar;
        d.param        = "Cutoff";
        d.value        = 0.62f;
        shots.push_back(Capture("02-value", d, scope));
    }
    {
        DisplayState d   = Base();
        d.readout        = Readout::kBar;
        d.param          = "Cutoff";
        d.value          = 0.62f;
        d.pickup_hint    = -1;
        shots.push_back(Capture("03-value-pickup", d, scope));
    }
    {
        DisplayState d = Base();
        d.readout      = Readout::kChoice;
        d.param        = "LFO dest";
        d.choice       = "Space mix";
        shots.push_back(Capture("04-choice", d, scope));
    }
    {
        DisplayState d  = Base();
        d.readout       = Readout::kEnvelope;
        d.param         = "Decay";
        d.env[0]        = 0.15f;
        d.env[1]        = 0.55f;
        d.env[2]        = 0.70f;
        d.env[3]        = 0.55f;
        d.env_moving    = 1;
        shots.push_back(Capture("05-envelope", d, scope));
    }
    {
        DisplayState d = Base();
        d.shift        = true;
        shots.push_back(Capture("06-shift", d, scope));
    }
    {
        DisplayState d = Base();
        d.gesture      = "REC";
        shots.push_back(Capture("07-recording", d, scope));
    }
    {
        DisplayState d = Base();
        d.mode         = UiMode::kMenu;
        d.menu_item    = "Save preset";
        d.menu_index   = 1;
        shots.push_back(Capture("08-menu", d, scope));
    }
    {
        DisplayState d  = Base();
        d.mode          = UiMode::kLoad;
        d.browser_slot  = 4;
        d.browser_name  = "Shimmer Bell";
        d.browser_used  = true;
        shots.push_back(Capture("09-load", d, scope));
    }
    {
        DisplayState d  = Base();
        d.mode          = UiMode::kSave;
        d.browser_slot  = 17;
        d.browser_name  = "-- empty --";
        d.browser_used  = false;
        shots.push_back(Capture("10-save", d, scope));
    }
    {
        DisplayState d = Base();
        d.mode         = UiMode::kName;
        d.browser_slot = 17;
        d.name_text    = "USER 18     ";
        d.name_cursor  = 5;
        shots.push_back(Capture("11-name", d, scope));
    }
    {
        DisplayState d = Base();
        d.mode         = UiMode::kPatchPage;
        d.page_item    = "Bend range";
        d.page_value   = "2 semitones";
        d.page_index   = 2;
        d.page_count   = 5;
        shots.push_back(Capture("12-patch-page", d, scope));
    }
    {
        DisplayState d = Base();
        d.mode         = UiMode::kGlobalPage;
        d.page_item    = "MIDI out ch";
        d.page_value   = "1";
        d.page_index   = 1;
        d.page_count   = 2;
        shots.push_back(Capture("13-global-page", d, scope));
    }
    {
        DisplayState d = Base();
        d.mode         = UiMode::kResetPage;
        d.reset_armed  = true;
        shots.push_back(Capture("14-factory-reset", d, scope));
    }
    {
        DisplayState d = Base();
        d.message      = "harmonic-16";
        d.notice       = "MONO: THIS ENGINE";
        shots.push_back(Capture("15-engine-change", d, scope));
    }

    for(const Shot& s : shots)
    {
        if(!ascii_only)
            WritePbm(s);
        PrintAscii(s);
    }
    printf("\n%zu screens%s\n", shots.size(), ascii_only ? "" : " also written to build/*.pbm");
    return 0;
}
