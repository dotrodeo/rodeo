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

#include "factory_presets.h"
#include "screen.h"

using namespace rodeo002;

namespace
{
/** Everything OledDisplay asks of a driver, and nothing else. */
/** The framebuffer is a FILE-SCOPE array rather than a member, and that is not
 *  a style choice. OledDisplay has a vtable, so a reinterpret_cast from the
 *  display to its driver lands 12 bytes before the driver's own storage and
 *  every screen comes out shifted -- which is exactly the kind of quiet lie a
 *  preview exists to avoid. Reading it here needs no cast at all. */
static bool g_fb[128 * 64];

struct HostDriver
{
    struct Config
    {
    };
    static constexpr int kW = 128, kH = 64;

    void     Init(Config) {}
    uint16_t Height() const { return kH; }
    uint16_t Width() const { return kW; }
    void     Fill(bool on)
    {
        for(auto& p : g_fb)
            p = on;
    }
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x < kW && y < kH)
            g_fb[y * kW + x] = on;
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
    for(int i = 0; i < 128 * 64; i++)
        s.px[i] = g_fb[i];
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

/** A real Ui, driven by real encoder input. The screens are then whatever the
 *  instrument would actually show, including anything the interaction gets
 *  wrong -- which is the point of looking at them. */
struct Rig
{
    Ui                    ui;
    UiInput               in;
    PresetBank            bank;
    GestureRecorder       gesture;
    std::vector<uint16_t> take;

    Rig() : take(GestureRecorder::kLanes * GestureRecorder::kMaxFrames)
    {
        Patch   p;
        Globals g;
        p.type[fx::TextureChain::kCharacter] = fx::Character::kDrive;
        p.type[fx::TextureChain::kMovement]  = fx::Movement::kChorus;
        p.type[fx::TextureChain::kTexture]   = fx::Texture::kCrush;
        p.type[fx::TextureChain::kSpace]     = fx::Space::kEcho;
        p.bypass[fx::TextureChain::kTexture] = true;
        ui.Init(p, g);
        BuildFactoryBank(bank);
        ui.SetBank(&bank);
        gesture.Init(take.data(), GestureRecorder::kMaxFrames);
        ui.SetGesture(&gesture);
        ui.NotePresetLoaded(bank.slot[1]); // "Chroma Pad"
        for(int i = 0; i < Ui::kNumPots; i++)
            in.pot[i] = 0.5f;
        Tick();
    }
    void Tick(uint32_t ms = 10)
    {
        in.now_ms += ms;
        ui.Update(in);
        for(int e = 0; e < Ui::kNumEncoders; e++)
            in.enc_delta[e] = 0;
        ui.Refresh(3, 8);
    }
    void Turn(int e, int d)
    {
        in.enc_delta[e] = static_cast<int16_t>(d);
        Tick();
    }
    void NavTap()
    {
        in.enc_held[Ui::kNavEncoder] = true;
        Tick();
        in.enc_held[Ui::kNavEncoder] = false;
        Tick();
    }
    void Pot(int i, float to)
    {
        const float from = in.pot[i];
        for(int s = 1; s <= 8; s++)
        {
            in.pot[i] = from + (to - from) * (static_cast<float>(s) / 8.f);
            Tick();
        }
    }
    void Menu(MenuItem item)
    {
        if(ui.Mode() != UiMode::kMenu)
            NavTap();
        for(int g = 0; ui.display().menu_index != static_cast<int>(item) && g < 10; g++)
            Turn(Ui::kNavEncoder, 1);
        NavTap();
    }
};

} // namespace

int main(int argc, char** argv)
{
    const bool        ascii_only = argc > 1 && std::strcmp(argv[1], "ascii") == 0;
    std::vector<Shot> shots;
    Scope&            scope = TheScope();

    auto shot = [&](const char* name, Rig& r) {
        shots.push_back(Capture(name, r.ui.display(), scope));
    };

    { Rig r; shot("01-idle", r); }
    { Rig r; r.Pot(4, 0.2f); r.Pot(4, 0.62f); shot("02-value", r); }
    { Rig r; r.Pot(4, 0.9f); shot("03-value-pickup", r); }
    { Rig r; r.in.enc_held[Ui::kMainEncoder] = true; r.Tick(); r.Pot(6, 0.1f);
      shot("04-choice", r); }
    { Rig r; r.Pot(8, 0.2f); r.Pot(8, 0.45f); shot("05-envelope", r); }
    { Rig r; r.in.enc_held[Ui::kMainEncoder] = true; r.Tick(); r.Tick();
      shot("06-shift", r); }
    { Rig r;
      r.in.enc_held[Ui::kMainEncoder] = true; r.Tick();
      r.in.enc_held[Ui::kMainEncoder] = false; r.Tick();   // arm
      r.Pot(0, 0.2f); r.Pot(0, 0.8f);                      // recording
      shot("07-recording", r); }
    { Rig r; r.NavTap(); r.Turn(Ui::kNavEncoder, 1); shot("08-menu", r); }
    { Rig r; r.Menu(MenuItem::kLoad); for(int i = 0; i < 4; i++) r.Turn(Ui::kNavEncoder, 1);
      shot("09-load", r); }
    { Rig r; r.Menu(MenuItem::kSave); for(int i = 0; i < 17; i++) r.Turn(Ui::kNavEncoder, 1);
      shot("10-save", r); }
    { Rig r; r.Menu(MenuItem::kSave); for(int i = 0; i < 17; i++) r.Turn(Ui::kNavEncoder, 1);
      r.NavTap(); r.Turn(Ui::kNavEncoder, 1); r.Turn(Ui::kNavEncoder, 1);
      shot("11-name", r); }
    { Rig r; r.Menu(MenuItem::kPatch); r.Turn(Ui::kNavEncoder, 1); r.Turn(Ui::kNavEncoder, 1);
      shot("12-patch-page", r); }
    { Rig r; r.Menu(MenuItem::kGlobal); r.Turn(Ui::kNavEncoder, 1); shot("13-global-page", r); }
    { Rig r; r.Menu(MenuItem::kFactoryReset); r.Turn(Ui::kMainEncoder, 1);
      shot("14-factory-reset", r); }
    { Rig r; r.Turn(Ui::kMainEncoder, 1); shot("15-engine-change", r); }

    for(const Shot& s : shots)
    {
        if(!ascii_only)
            WritePbm(s);
        PrintAscii(s);
    }
    printf("\n%zu screens%s\n", shots.size(), ascii_only ? "" : " also written to build/*.pbm");
    return 0;
}
