// The button bank's logic: its debounced switches and its four RGB LEDs.
#include <cmath>

#include "gtest/gtest.h"
#include "buttons.h"
#include "lights.h"

// --- Buttons --------------------------------------------------------------------

namespace
{
void Hold(sat_buttons& b, uint8_t raw, int ms)
{
    for(int i = 0; i < ms; i++)
        buttons_update(&b, raw);
}
} // namespace

TEST(Buttons, APressCountsOnceItHasHeldForFiveMilliseconds)
{
    sat_buttons b;
    buttons_init(&b);
    Hold(b, 0b0100, BUTTONS_STABLE_MS - 1);
    EXPECT_EQ(b.pressed, 0); // not yet
    Hold(b, 0b0100, 1);
    EXPECT_EQ(b.pressed, 0b0100);
    EXPECT_EQ(b.presses[2], 1);
    Hold(b, 0b0100, 50); // held: still one press
    EXPECT_EQ(b.presses[2], 1);
    Hold(b, 0, BUTTONS_STABLE_MS);
    EXPECT_EQ(b.pressed, 0);
    EXPECT_EQ(b.presses[2], 1); // releases don't count
}

TEST(Buttons, BounceShorterThanTheWindowIsIgnored)
{
    sat_buttons b;
    buttons_init(&b);
    // Contact bounce: on and off a millisecond at a time, then settled pressed.
    for(int i = 0; i < 10; i++)
        Hold(b, i % 2 ? 0b0001 : 0, 1);
    EXPECT_EQ(b.presses[0], 0);
    Hold(b, 0b0001, BUTTONS_STABLE_MS);
    EXPECT_EQ(b.presses[0], 1);
    // And bouncing on release doesn't make a second press.
    for(int i = 0; i < 10; i++)
        Hold(b, i % 2 ? 0 : 0b0001, 1);
    Hold(b, 0, BUTTONS_STABLE_MS);
    EXPECT_EQ(b.pressed, 0);
    EXPECT_EQ(b.presses[0], 1);
}

TEST(Buttons, EachSwitchIsIndependentAndCountersWrap)
{
    sat_buttons b;
    buttons_init(&b);
    for(int n = 0; n < 257; n++)
    {
        Hold(b, 0b1001, BUTTONS_STABLE_MS);
        Hold(b, 0b0001, BUTTONS_STABLE_MS); // switch 1 held throughout
    }
    EXPECT_EQ(b.presses[0], 1);
    EXPECT_EQ(b.presses[3], 1); // 257 presses, wrapped at 256
    EXPECT_EQ(b.presses[1] | b.presses[2], 0);
}

// --- Lights ---------------------------------------------------------------------

namespace
{
uint8_t G(uint32_t w) { return (uint8_t)(w >> 16); }
uint8_t R(uint32_t w) { return (uint8_t)(w >> 8); }
uint8_t B(uint32_t w) { return (uint8_t)w; }

double Milliamps(const uint32_t grb[LIGHTS_N])
{
    double sum = 0;
    for(unsigned i = 0; i < LIGHTS_N; i++)
        sum += R(grb[i]) + G(grb[i]) + B(grb[i]);
    return sum * LIGHTS_MA_PER_CHANNEL / 255.0;
}
} // namespace

TEST(Lights, SlotIDrivesLedIInTheWs2812sOrder)
{
    sat_lights l;
    lights_init(&l);
    sat_slot taken[4] = {sat_rgb(sat_rgb565(255, 0, 0)), sat_none(), sat_rgb(sat_rgb565(0, 0, 255)),
                         sat_led(128, SAT_LED_STEADY)};
    lights_take(&l, taken, 4);
    uint32_t grb[LIGHTS_N];
    lights_render(&l, 0, false, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_EQ(grb[0], 0x00FF00u); // red sits in the middle byte: G R B
    EXPECT_EQ(grb[1], 0u);        // NONE: still off
    EXPECT_EQ(grb[2], 0x0000FFu);
    EXPECT_EQ(grb[3], 0x808080u); // an LED slot is white at its brightness
}

TEST(Lights, NoneAndBadSlotsKeepWhatWasThere)
{
    sat_lights l;
    lights_init(&l);
    sat_slot first[1] = {sat_rgb(sat_rgb565(0, 255, 0))};
    lights_take(&l, first, 1);
    sat_slot bad = sat_rgb(sat_rgb565(255, 0, 0));
    bad.crc ^= 1;
    sat_slot second[2] = {bad, sat_none()};
    lights_take(&l, second, 2);
    uint32_t grb[LIGHTS_N];
    lights_render(&l, 0, false, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_EQ(grb[0], 0xFF0000u); // still green
}

TEST(Lights, ChainPowerCapsTheTotalCurrent)
{
    sat_lights l;
    lights_init(&l);
    sat_slot white[4];
    for(auto& s : white)
        s = sat_rgb(0xFFFF);
    lights_take(&l, white, 4);
    uint32_t grb[LIGHTS_N];
    lights_render(&l, 0, false, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_NEAR(Milliamps(grb), 144.0, 0.1); // aux: everything at full
    lights_render(&l, 0, false, LIGHTS_CAP_CHAIN_MA, grb);
    EXPECT_LE(Milliamps(grb), LIGHTS_CAP_CHAIN_MA);
    EXPECT_GT(Milliamps(grb), LIGHTS_CAP_CHAIN_MA - 1.0);
    EXPECT_EQ(grb[0], grb[3]); // scaled together: the colour stays white

    // Under the cap nothing is touched.
    sat_slot dim[1] = {sat_rgb(sat_rgb565(80, 0, 0))};
    lights_init(&l);
    lights_take(&l, dim, 1);
    lights_render(&l, 0, false, LIGHTS_CAP_CHAIN_MA, grb);
    EXPECT_EQ(R(grb[0]), 82); // 80 through RGB565 and back
}

TEST(Lights, BlinkModesAndIdentify)
{
    sat_lights l;
    lights_init(&l);
    sat_slot slow[1] = {sat_led(255, SAT_LED_SLOW_BLINK)};
    lights_take(&l, slow, 1);
    uint32_t grb[LIGHTS_N];
    lights_render(&l, 100, false, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_NE(grb[0], 0u);
    lights_render(&l, 600, false, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_EQ(grb[0], 0u);
    // IDENTIFY: all four white at 4 Hz, whatever they were showing.
    lights_render(&l, 600, true, LIGHTS_CAP_AUX_MA, grb);
    for(unsigned i = 0; i < LIGHTS_N; i++)
        EXPECT_EQ(grb[i], 0xFFFFFFu);
    lights_render(&l, 130, true, LIGHTS_CAP_AUX_MA, grb);
    EXPECT_EQ(grb[2], 0u);
}

TEST(Lights, AuxSenseReadsHalfTheAuxVoltage)
{
    // R6/R7 halve AUX_5V: ~2.5 V against VDD from 3.3 V down to ~2.9 V at the
    // end of a long chain; nothing without the aux cable.
    EXPECT_TRUE(lights_aux_present((uint16_t)std::lround(2.5 / 3.3 * 1023), 1023));
    EXPECT_TRUE(lights_aux_present(1023, 1023)); // 2.5 V above a sagging VDD
    EXPECT_FALSE(lights_aux_present(0, 1023));
    EXPECT_FALSE(lights_aux_present(20, 1023));
}
