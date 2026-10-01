// Rodeo host display — render the real screens on a laptop, pixel for pixel.
//
// libDaisy's OledDisplay is a template over its driver, and everything below it
// (display.h, graphics_common.h, daisy_core.h, oled_fonts.c) is plain C++ --
// only the SSD130x transport knows about an STM32. So a stub driver that holds a
// framebuffer gives the SAME draw calls and the SAME FONTS the hardware uses,
// with nothing flashed. That is what makes it worth having: a preview that is
// not the real drawing code is a preview that can lie to you.
//
// An instrument supplies its own driver loop and shot list; this is the part
// that is the same for every one of them. Written for rodeo-002 and moved here
// on 2026-10-01 (it caught a 12-pixel offset and settled a header-width
// question before either reached hardware).
//
// Build with C++17, not the firmware's gnu++14: libDaisy's FixedCapStr wants
// <string_view>. Nothing else differs.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "hid/disp/oled_display.h"

namespace rodeo
{
namespace host
{
constexpr int kW = 128, kH = 64; // the platform's SSD130x; see oled_ssd130x_bulk.h

/** THE FRAMEBUFFER IS FILE-SCOPE, and that is not a style choice.
 *
 *  OledDisplay has a vtable, so a reinterpret_cast from the display to its
 *  driver lands 12 bytes before the driver's own storage and every screen comes
 *  out shifted -- exactly the kind of quiet lie a preview exists to avoid.
 *  Reading it through this needs no cast at all. */
inline bool* Framebuffer()
{
    static bool fb[kW * kH];
    return fb;
}

/** Everything OledDisplay asks of a driver, and nothing else. */
struct HostDriver
{
    struct Config
    {
    };

    void     Init(Config) {}
    uint16_t Height() const { return kH; }
    uint16_t Width() const { return kW; }
    void     Fill(bool on)
    {
        bool* fb = Framebuffer();
        for(int i = 0; i < kW * kH; i++)
            fb[i] = on;
    }
    void DrawPixel(uint_fast8_t x, uint_fast8_t y, bool on)
    {
        if(x < kW && y < kH)
            Framebuffer()[y * kW + x] = on;
    }
    void Update() {}
    bool UpdateFinished() { return true; }
    void Reset() {}
    void SendCommand(uint8_t) {}
    void SendData(uint8_t*, size_t) {}
};

using Display = daisy::OledDisplay<HostDriver>;

/** One initialised display, shared by every shot. */
inline Display& TheDisplay()
{
    static Display d;
    static bool    init = false;
    if(!init)
    {
        Display::Config c;
        d.Init(c);
        init = true;
    }
    return d;
}

/** A captured screen: 128x64 booleans and a name to file it under. */
struct Shot
{
    std::string       name;
    std::vector<bool> px;
};

/** Copies the framebuffer out as it stands. Call after drawing. */
inline Shot Capture(const std::string& name)
{
    Shot s;
    s.name = name;
    s.px.resize(kW * kH);
    const bool* fb = Framebuffer();
    for(int i = 0; i < kW * kH; i++)
        s.px[i] = fb[i];
    return s;
}

inline void PrintAscii(const Shot& s)
{
    printf("\n%s\n+", s.name.c_str());
    for(int x = 0; x < kW; x++)
        printf("-");
    printf("+\n");
    for(int y = 0; y < kH; y++)
    {
        printf("|");
        for(int x = 0; x < kW; x++)
            printf("%c", s.px[y * kW + x] ? '#' : ' ');
        printf("|\n");
    }
    printf("+");
    for(int x = 0; x < kW; x++)
        printf("-");
    printf("+\n");
}

/** A PBM beside the binary. They are 128x64, so zoom hard. */
inline void WritePbm(const Shot& s, const std::string& dir = "build")
{
    const std::string path = dir + "/" + s.name + ".pbm";
    FILE*             f    = fopen(path.c_str(), "wb");
    if(!f)
        return;
    fprintf(f, "P1\n%d %d\n", kW, kH);
    for(int y = 0; y < kH; y++)
    {
        for(int x = 0; x < kW; x++)
            fprintf(f, "%d ", s.px[y * kW + x] ? 1 : 0);
        fprintf(f, "\n");
    }
    fclose(f);
}

} // namespace host
} // namespace rodeo
