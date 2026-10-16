// Proposed screens for the 6-encoder UI. Nothing here is wired into the
// firmware: it draws candidate layouts with the real fonts so they can be
// judged before any of it is built.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "hid/disp/oled_display.h"
#include "util/FixedCapStr.h"

using daisy::FixedCapStr;

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
static Display disp;

static void Show(const char* name)
{
    printf("\n%s\n+", name);
    for(int x = 0; x < 128; x++) printf("-");
    printf("+\n");
    for(int y = 0; y < 64; y++)
    {
        printf("|");
        for(int x = 0; x < 128; x++) printf("%c", g_fb[y * 128 + x] ? '#' : ' ');
        printf("|\n");
    }
    printf("+"); for(int x = 0; x < 128; x++) printf("-"); printf("+\n");
}

/** A title bar, inverted, with an optional right-hand counter. */
static void Title(const char* text, const char* right = nullptr)
{
    disp.DrawRect(0, 0, 127, 9, true, true);
    disp.SetCursor(2, 1);
    disp.WriteString(text, Font_6x8, false);
    if(right)
    {
        disp.SetCursor(static_cast<uint8_t>(126 - 6 * strlen(right)), 1);
        disp.WriteString(right, Font_6x8, false);
    }
}

/** One list row. Selected rows invert, so the eye finds them without a marker
 *  eating a character of width. */
static void Row(int i, const char* left, const char* right, bool sel)
{
    const uint8_t y = static_cast<uint8_t>(12 + i * 10);
    if(sel)
        disp.DrawRect(0, static_cast<uint8_t>(y - 1), 127, static_cast<uint8_t>(y + 8), true, true);
    disp.SetCursor(2, y);
    disp.WriteString(left, Font_6x8, !sel);
    if(right)
    {
        disp.SetCursor(static_cast<uint8_t>(126 - 6 * strlen(right)), y);
        disp.WriteString(right, Font_6x8, !sel);
    }
}

/** How far down a list you are, drawn as a bar on the right edge. */
static void ScrollBar(int first, int visible, int total)
{
    if(total <= visible) return;
    const int track = 52;
    const int h     = track * visible / total;
    const int top   = 12 + track * first / total;
    disp.DrawRect(126, 12, 127, 63, false, true);
    disp.DrawRect(126, static_cast<uint8_t>(top), 127,
                  static_cast<uint8_t>(top + (h < 4 ? 4 : h)), true, true);
}

int main()
{
    // ---- the menu, as a list that fills the screen ----------------------
    {
        const char* items[] = {"Load preset", "Save preset", "Gesture",
                               "Patch settings", "Global settings", "Factory reset"};
        disp.Fill(false);
        Title("MENU", "2/6");
        for(int i = 0; i < 5; i++)
            Row(i, items[i], nullptr, i == 1);
        ScrollBar(0, 5, 6);
        Show("A-menu-list");
    }
    // ---- the preset browser, ditto, with slot numbers --------------------
    {
        const char* names[] = {"Chroma Pad", "Glass Keys", "Tape Choir",
                               "Shimmer Bell", "Broken Radio"};
        disp.Fill(false);
        Title("LOAD PRESET", "5/24");
        for(int i = 0; i < 5; i++)
        {
            FixedCapStr<20> n("");
            n.AppendInt(i + 2);
            n.Append(" ");
            n.Append(names[i]);
            Row(i, n.Cstr(), nullptr, i == 3);
        }
        ScrollBar(1, 5, 24);
        Show("B-load-list");
    }
    // ---- a settings page: name on the left, value on the right ----------
    {
        const char* items[]  = {"Key track", "Vel>filter", "Bend range",
                                "LFO shape", "Mod wheel"};
        const char* values[] = {"+40%", "30%", "2 st", "Triangle", "Cutoff"};
        disp.Fill(false);
        Title("PATCH", "3/5");
        for(int i = 0; i < 5; i++)
            Row(i, items[i], values[i], i == 2);
        Show("C-patch-list");
    }
    // ---- and what the same list looks like while a value is being edited -
    {
        const char* items[]  = {"Key track", "Vel>filter", "Bend range",
                                "LFO shape", "Mod wheel"};
        const char* values[] = {"+40%", "30%", "< 7 st >", "Triangle", "Cutoff"};
        disp.Fill(false);
        Title("PATCH", "3/5");
        for(int i = 0; i < 5; i++)
            Row(i, items[i], values[i], i == 2);
        Show("D-patch-editing");
    }
    return 0;
}
