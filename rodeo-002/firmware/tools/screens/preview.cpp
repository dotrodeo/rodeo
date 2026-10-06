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

#include "host_display.h"

#include "factory_presets.h"
#include "screen.h"

using namespace rodeo002;

namespace
{
/** The driver, the framebuffer, and the ASCII/PBM writers are the platform's --
 *  see rodeo-sdk/tools/screens/host_display.h, which also
 *  explains why the framebuffer is not a member. What is left here is this
 *  instrument's: a rig that drives its Ui, and the list of screens worth
 *  looking at. */
using rodeo::host::Capture;
using rodeo::host::PrintAscii;
using rodeo::host::Shot;
using rodeo::host::TheDisplay;
using rodeo::host::WritePbm;

/** Draw one screen with the instrument's own Draw(), then take a copy of it. */
Shot DrawShot(const std::string& name, const DisplayState& d, Scope& scope)
{
    screen::Draw(TheDisplay(), d, scope);
    return Capture(name);
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
        ui.NotePresetLoaded(bank.slot[1], 1); // "Chroma Pad"
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
        ui.Refresh();
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
        shots.push_back(DrawShot(name, r.ui.display(), scope));
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
    { Rig r; r.Menu(MenuItem::kSave); for(int i = 0; i < 17; i++) r.Turn(Ui::kNavEncoder, 1);
      shot("10-save", r); }
    { Rig r; r.Menu(MenuItem::kSave); for(int i = 0; i < 17; i++) r.Turn(Ui::kNavEncoder, 1);
      r.NavTap(); r.Turn(Ui::kNavEncoder, 1); r.Turn(Ui::kNavEncoder, 1);
      shot("11-name", r); }
    { Rig r; r.Menu(MenuItem::kPatch); r.Turn(Ui::kNavEncoder, 1); r.Turn(Ui::kNavEncoder, 1);
      shot("12-patch-page", r); }
    // The widest value on any settings page: "+12.0 dB" beside "Patch gain".
    { Rig r; r.Menu(MenuItem::kPatch);
      for(int i = 0; i < 20 && r.ui.display().page_index != int(PatchSetting::kGain); i++)
          r.Turn(Ui::kNavEncoder, 1);
      for(int i = 0; i < 80; i++) r.Turn(Ui::kMainEncoder, 1);
      shot("17-patch-gain", r); }
    { Rig r; r.Menu(MenuItem::kGlobal); r.Turn(Ui::kNavEncoder, 1); shot("13-global-page", r); }
    { Rig r; r.Menu(MenuItem::kFactoryReset); r.Turn(Ui::kMainEncoder, 1);
      shot("14-factory-reset", r); }
    { Rig r; r.Turn(Ui::kMainEncoder, 1); shot("15-engine-change", r); }
    // The header at its tightest: the longest name a slot can hold, in a
    // two-digit slot, with the unsaved `*`. If anything collides it is here.
    { Rig r;
      StoredPreset& s = r.bank.slot[PresetBank::kNumSlots - 1];
      PatchToPreset(r.ui.patch(), "Wavetable XY", s);
      r.ui.NotePresetLoaded(s, PresetBank::kNumSlots - 1);
      r.Pot(4, 0.2f); r.Pot(4, 0.62f);          // dirty it
      r.Tick(300);                              // let the value readout lapse
      shot("16-header-full", r); }

    for(const Shot& s : shots)
    {
        if(!ascii_only)
            WritePbm(s);
        PrintAscii(s);
    }
    printf("\n%zu screens%s\n", shots.size(), ascii_only ? "" : " also written to build/*.pbm");
    return 0;
}
