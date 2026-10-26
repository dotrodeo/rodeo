#pragma once
//
// What each satellite board wires to which CH32V003F4P6 pin, read from the
// boards' schematics (hardware/rodeo-pot-1, hardware/rodeo-encoder-1,
// hardware/rodeo-pot-bank-4, hardware/rodeo-encoder-bank-4).
// One firmware image serves every board: it reads the ID resistor (kind.h) and
// then uses that kind's map. Pure data, no register access.
//
// Common to every board (contract §1.5):
//   USART1 mapping 0, TX PD5 -> OUT DATA_FWD (33 ohm), RX PD6 <- IN DATA_FWD
//   USART1 mapping 3, TX PC0 -> the DATA_RET tap (33 ohm), RX PC1 (also IN DATA_FWD)
//   SWIO on PD1, the programming pad
//   ID divider read on PD3 (ADC channel 4): R_ID to GND, 10 k on top
//
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char    port; // 'A', 'C' or 'D'
    uint8_t pin;
} sat_pin;

#define SAT_NO_PIN ((sat_pin){0, 0})

/** ADC channel for a pin, or -1. CH32V003: A0 PA2, A1 PA1, A2 PC4, A3 PD2,
 *  A4 PD3, A5 PD5, A6 PD6, A7 PD4. */
static inline int8_t sat_adc_channel(sat_pin p)
{
    if(p.port == 'A' && p.pin == 2) return 0;
    if(p.port == 'A' && p.pin == 1) return 1;
    if(p.port == 'C' && p.pin == 4) return 2;
    if(p.port == 'D' && p.pin == 2) return 3;
    if(p.port == 'D' && p.pin == 3) return 4;
    if(p.port == 'D' && p.pin == 5) return 5;
    if(p.port == 'D' && p.pin == 6) return 6;
    if(p.port == 'D' && p.pin == 4) return 7;
    return -1;
}

typedef struct
{
    sat_pin present; // OUT PRESENT: high = nothing downstream, this is the last
    sat_pin led;     // indicator LED, active high
    sat_pin id_feed; // drives the ID divider's 10 k, or port 0 if it's tied to 3V3
    sat_pin pots[4]; // pot wipers, pots 1-4 in slot order; unused ones SAT_NO_PIN
    sat_pin enc_a[4]; // encoders 1-4 in slot order; unused ones SAT_NO_PIN
    sat_pin enc_b[4];
    sat_pin switches; // the push switches' resistor ladder (encoder bank)
} sat_board;

/** Kind 1, rodeo-pot-1: the pot bank's map cut to slot 1. One pot on PA2, and
 *  the ID divider fed from PC2 like the bank's. */
#define SAT_BOARD_POT_1                                                        \
    {                                                                          \
        .present = {'C', 3}, .led = {'C', 6}, .id_feed = {'C', 2},             \
        .pots = {{'A', 2}},                                                    \
    }

/** Kind 5, rodeo-pot-bank-4. The ID divider's top is fed from PC2, which the
 *  firmware drives high only while it reads the ID. */
#define SAT_BOARD_POT_BANK_4                                                   \
    {                                                                          \
        .present = {'C', 3}, .led = {'C', 6}, .id_feed = {'C', 2},             \
        .pots = {{'A', 2}, {'D', 2}, {'C', 4}, {'A', 1}},                      \
    }

/** Kind 3, rodeo-encoder-1: the encoder bank's map cut to slot 1. Its push switch
 *  is a one-rung ladder on PD4 (10 k up, 10 k through the switch), and the ID
 *  divider is tied to 3V3 like the bank's. */
#define SAT_BOARD_ENC_1                                                        \
    {                                                                          \
        .present = {'D', 2}, .led = {'D', 0}, .id_feed = {0, 0},               \
        .enc_a = {{'A', 1}}, .enc_b = {{'A', 2}}, .switches = {'D', 4},        \
    }

/** Kind 6, rodeo-encoder-bank-4. The ID divider's top is tied to 3V3, and PC2
 *  is encoder 2's A line: it must never be driven. The four push switches share
 *  one ADC pin through a resistor ladder (ladder.h). */
#define SAT_BOARD_ENC_BANK_4                                                   \
    {                                                                          \
        .present = {'D', 2}, .led = {'D', 0}, .id_feed = {0, 0},               \
        .enc_a = {{'A', 1}, {'C', 2}, {'C', 4}, {'C', 6}},                     \
        .enc_b = {{'A', 2}, {'C', 3}, {'C', 5}, {'C', 7}},                     \
        .switches = {'D', 4},                                                  \
    }

#ifdef __cplusplus
}
#endif
