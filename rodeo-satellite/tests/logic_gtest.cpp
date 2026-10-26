// The satellite's input logic: board kind, encoders, the switch ladder, pots.
#include <cmath>
#include <vector>

#include "gtest/gtest.h"
#include "boards.h"
#include "encoder.h"
#include "kind.h"
#include "ladder.h"
#include "pot.h"

// --- Board kind ------------------------------------------------------------------

namespace
{
// A divider: R_top from the feed (a GPIO, or 3V3), R_ID to ground.
struct Divider
{
    double rid;
    bool   fed_from_gpio;
    bool   feed_high = false;
    int    drives    = 0;
};

uint16_t ReadId(void* ctx)
{
    auto* d = static_cast<Divider*>(ctx);
    if(d->fed_from_gpio && !d->feed_high)
        return 2; // nothing feeds it: R_ID pulls the pin to ground
    return static_cast<uint16_t>(std::lround(1023.0 * d->rid / (10000.0 + d->rid)));
}

void DriveFeed(void* ctx, bool high)
{
    auto* d      = static_cast<Divider*>(ctx);
    d->feed_high = high;
    if(high)
        d->drives++;
}

int Detect(Divider& d)
{
    sat_kind_io io = {ReadId, DriveFeed, &d, 1023};
    uint8_t     detail;
    return sat_detect_kind(&io, &detail);
}
} // namespace

TEST(Kind, PotBankIsFoundByDrivingItsFeed)
{
    Divider pot_bank = {4990, true};
    EXPECT_EQ(Detect(pot_bank), SAT_KIND_POT_BANK_4);
    EXPECT_EQ(pot_bank.drives, 1);
    EXPECT_FALSE(pot_bank.feed_high); // released after the read
}

TEST(Kind, SinglePotIsFoundByDrivingItsFeed)
{
    // Kind 1 reads lowest of any kind (R_ID 715 ohm), and like the pot bank its
    // divider hangs from PC2, so the first read is near ground either way.
    Divider pot = {715, true};
    EXPECT_EQ(Detect(pot), SAT_KIND_POT);
    EXPECT_EQ(pot.drives, 1);
    EXPECT_FALSE(pot.feed_high);
}

TEST(Kind, EncoderBankIsFoundWithoutEverDrivingPc2)
{
    // On the encoder bank PC2 is encoder 2's A line: driving it could short a
    // closed contact. Its divider is fed from 3V3, so the first read suffices.
    Divider enc_bank = {6650, false};
    EXPECT_EQ(Detect(enc_bank), SAT_KIND_ENC_BANK_4);
    EXPECT_EQ(enc_bank.drives, 0);
}

TEST(Kind, NoIdResistorReadsFullScale)
{
    Divider none = {1e12, false};
    EXPECT_EQ(Detect(none), SAT_KIND_NO_ID);
}

TEST(Kind, BoardsWireTheIdAndAnalogPinsToAdcChannels)
{
    sat_board pot = SAT_BOARD_POT_BANK_4;
    for(auto p : pot.pots)
        EXPECT_GE(sat_adc_channel(p), 0) << p.port << int(p.pin);
    // The single pot is the bank's slot 1, and fills only that slot.
    sat_board one = SAT_BOARD_POT_1;
    EXPECT_EQ(one.pots[0].port, pot.pots[0].port);
    EXPECT_EQ(one.pots[0].pin, pot.pots[0].pin);
    EXPECT_EQ(one.pots[1].port, 0);
    EXPECT_EQ(one.present.port, pot.present.port);
    EXPECT_EQ(one.led.port, pot.led.port);
    EXPECT_EQ(one.id_feed.port, 'C');
    EXPECT_EQ(sat_kind_slots(SAT_KIND_POT), 1);
    sat_board enc = SAT_BOARD_ENC_BANK_4;
    EXPECT_EQ(sat_adc_channel(enc.switches), 7); // PD4
    EXPECT_EQ(enc.id_feed.port, 0);              // tied to 3V3: no feed pin
    EXPECT_EQ(sat_adc_channel(sat_pin{'D', 3}), 4); // the ID pin, every board
}

// --- Encoders --------------------------------------------------------------------

namespace
{
void Turn(sat_quad& q, int detents)
{
    // One detent: 11 -> 01 -> 00 -> 10 -> 11 one way, the reverse the other.
    const uint8_t fwd[4][2] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
    for(int d = 0; d < std::abs(detents); d++)
        for(int i = 0; i < 4; i++)
        {
            int k = detents > 0 ? i : (2 - i + 4) % 4;
            quad_update(&q, fwd[k][0], fwd[k][1]);
        }
}
} // namespace

TEST(Encoder, CountsDetentsBothWaysAndWraps)
{
    sat_quad q;
    quad_init(&q, 1, 1);
    Turn(q, 3);
    EXPECT_EQ(q.detents, 3);
    Turn(q, -5);
    EXPECT_EQ(q.detents, 254); // -2, wrapped
}

TEST(Encoder, BounceThatReturnsCountsNothing)
{
    sat_quad q;
    quad_init(&q, 1, 1);
    quad_update(&q, 0, 1); // a quarter step out
    quad_update(&q, 1, 1); // and straight back
    quad_update(&q, 0, 1);
    quad_update(&q, 0, 0);
    quad_update(&q, 0, 1); // half-way, then back
    quad_update(&q, 1, 1);
    EXPECT_EQ(q.detents, 0);
}

// --- Switch ladder ---------------------------------------------------------------

TEST(Ladder, EveryCombinationDecodesAtOnePercentCorners)
{
    const uint32_t nominal[4] = {10000, 20000, 40200, 80600};
    sat_ladder     l;
    ladder_init(&l, nominal, LADDER_PULL_UP, 1023);
    for(uint8_t m = 0; m < 16; m++)
        for(double pull : {0.99, 1.01})
            for(double sw : {0.99, 1.01})
            {
                double g = 0;
                for(int i = 0; i < 4; i++)
                    if(m & (1 << i))
                        g += 1.0 / (nominal[i] * sw);
                double v = g == 0 ? 1.0 : 1.0 / (1.0 + LADDER_PULL_UP * pull * g);
                EXPECT_EQ(ladder_decode(&l, static_cast<uint16_t>(std::lround(v * 1023))), m)
                    << "mask " << int(m);
            }
}

TEST(Ladder, DebouncesAndCountsPresses)
{
    const uint32_t r[4] = {10000, 20000, 40200, 80600};
    sat_ladder     l;
    ladder_init(&l, r, LADDER_PULL_UP, 1023);
    uint16_t sw3 = l.level[1 << 2];
    ladder_update(&l, sw3);
    ladder_update(&l, sw3);
    EXPECT_EQ(l.pressed, 0); // not yet: twice isn't three times
    ladder_update(&l, sw3);
    EXPECT_EQ(l.pressed, 1 << 2);
    EXPECT_EQ(l.presses[2], 1);
    for(int i = 0; i < 3; i++)
        ladder_update(&l, l.level[0]);
    EXPECT_EQ(l.pressed, 0);
    EXPECT_EQ(l.presses[2], 1); // releasing isn't a press
}

// --- Pots ------------------------------------------------------------------------

TEST(Pot, ScalesTenBitsToTheFullSixteen)
{
    EXPECT_EQ(pot_scale10(0), 0);
    EXPECT_EQ(pot_scale10(1023), 65535);
    EXPECT_EQ(pot_scale10(512), 32800);
}

TEST(Pot, FilterStartsAtTheFirstReadingAndSettles)
{
    sat_pot_filter f = {0};
    EXPECT_EQ(pot_filter(&f, 40000, 2), 40000);
    uint16_t y = 0;
    for(int i = 0; i < 60; i++)
        y = pot_filter(&f, 20000, 2);
    EXPECT_NEAR(y, 20000, 1);
}
