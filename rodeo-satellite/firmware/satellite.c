// rodeo-satellite: one image for every satellite on the chain.
//
// At power-up it reads its ID resistor to learn which board it is on, sets up
// that board's pins, and then does two things for good:
//
//   * passes every frame along the ring, a byte at a time (stream.c), with its
//     own inputs appended and its own outputs taken off;
//   * keeps those inputs current: pots and the switch ladder through the ADC,
//     encoders by polling, the LED from what the Seed last sent.
//
// The protocol is RODEO_SATELLITE_CHAIN_CONTRACT.md; its bytes are
// rodeo-sdk/common/sat_frame.h. Everything here is hardware glue: the logic it
// calls is host-tested in ../tests. NOT YET RUN ON A CH32V003.
//
// Timing rule: once a frame is passing through, the satellite must send a byte
// for every byte it receives, so nothing in the main loop may block for more
// than a few microseconds. The ADC runs one conversion at a time and is polled.

#include "ch32fun.h"

#include "boards.h"
#include "encoder.h"
#include "kind.h"
#include "ladder.h"
#include "pot.h"
#include "stream.h"

#define FW_MAJOR 1
#define FW_MINOR 0

#define TICKS_US DELAY_US_TIME // SysTick ticks per microsecond
#define TICKS_MS DELAY_MS_TIME
#define IDLE_GAP_TICKS (SAT_IDLE_GAP_BYTES * 10u * TICKS_US) // 2 byte times

// The pin a GPIO-fed ID divider hangs from (the pot bank's PC2); see kind.h.
static const sat_pin kIdFeed = {'C', 2};
static const sat_pin kIdPin  = {'D', 3};
static const sat_pin kTxFwd  = {'D', 5}; // USART1 map 0 TX
static const sat_pin kRxFwd  = {'D', 6}; // map 0 RX
static const sat_pin kTxRet  = {'C', 0}; // map 3 TX: the DATA_RET tap
static const sat_pin kRxRet  = {'C', 1}; // map 3 RX

// --- Pins -------------------------------------------------------------------------

static int  code(sat_pin p) { return (p.port - 'A') * 16 + p.pin; }
static bool has(sat_pin p) { return p.port != 0; }

static void mode(sat_pin p, uint32_t m)
{
    int c = code(p);
    funPinMode(c, m);
}

static int  get(sat_pin p) { return funDigitalRead(code(p)); }
static void put(sat_pin p, int v)
{
    int c = code(p);
    funDigitalWrite(c, v ? FUN_HIGH : FUN_LOW);
}

// --- ADC, one conversion at a time -------------------------------------------------

static void adc_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
    RCC->CFGR0 &= ~(0x1F << 11); // ADC clock HCLK / 2
    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;
    ADC1->RSQR1 = 0; // one conversion per start
    ADC1->RSQR2 = 0;
    ADC1->SAMPTR2 = 0x00FFFFFF; // channels 0-7: the longest sample time (241 cycles)
    ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;
    ADC1->CTLR2 |= ADC_RSTCAL;
    while(ADC1->CTLR2 & ADC_RSTCAL) {}
    ADC1->CTLR2 |= ADC_CAL;
    while(ADC1->CTLR2 & ADC_CAL) {}
}

static void adc_start(int8_t channel)
{
    ADC1->RSQR3 = (uint32_t)channel;
    ADC1->CTLR2 |= ADC_SWSTART;
}

static bool adc_ready(void) { return ADC1->STATR & ADC_EOC; }
static uint16_t adc_result(void) { return (uint16_t)ADC1->RDATAR; }

// Blocking read: only at boot, before the chain is served.
static uint16_t adc_read(int8_t channel)
{
    adc_start(channel);
    while(!adc_ready()) {}
    return adc_result();
}

// --- Board kind (boot) ---------------------------------------------------------------

static uint16_t id_read(void* ctx)
{
    (void)ctx;
    uint32_t sum = 0;
    for(int i = 0; i < 4; i++)
        sum += adc_read(sat_adc_channel(kIdPin));
    return (uint16_t)(sum / 4);
}

static void id_feed(void* ctx, bool high)
{
    (void)ctx;
    if(high)
    {
        mode(kIdFeed, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
        put(kIdFeed, 1);
        Delay_Us(50); // let the divider settle
    }
    else
        mode(kIdFeed, GPIO_CNF_IN_FLOATING);
}

// --- The chain UART ------------------------------------------------------------------

#define RX_LEN 256
static uint8_t           rx_ring[RX_LEN];
static uint32_t          rx_tail;
// A power of two, so the free-running counters wrap cleanly at 2^32.
#define TX_LEN 512
static uint8_t           tx_fifo[TX_LEN];
static volatile uint32_t tx_head, tx_tail;
static bool              routed_last; // USART1 is on map 3: TX on the return tap
static bool              tap_driven;

static void uart_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO;
    RCC->AHBPCENR |= RCC_DMA1EN;
    USART1->BRR   = FUNCONF_SYSTEM_CORE_CLOCK / SAT_BAUD; // 48: mantissa 3, fraction 0
    USART1->CTLR1 = USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_UE; // 8N1
    USART1->CTLR3 = USART_CTLR3_DMAR;
    // Every received byte lands in rx_ring by DMA; the main loop follows it.
    DMA1_Channel5->PADDR = (uint32_t)&USART1->DATAR;
    DMA1_Channel5->MADDR = (uint32_t)rx_ring;
    DMA1_Channel5->CNTR  = RX_LEN;
    DMA1_Channel5->CFGR  = DMA_CFGR1_CIRC | DMA_CFGR1_MINC | DMA_CFGR1_EN;
}

// Map 0 (TX to the next satellite) or map 3 (TX back along DATA_RET). Only
// called between frames. On map 3 the tap is an input until there is something
// to send, so a satellite past a forced break never fights the return wire.
static void route(bool last)
{
    if(last)
    {
        AFIO->PCFR1 |= AFIO_PCFR1_USART1_REMAP | AFIO_PCFR1_USART1_REMAP_1;
        mode(kTxFwd, GPIO_CNF_IN_FLOATING);
        mode(kTxRet, GPIO_CNF_IN_FLOATING);
        mode(kRxRet, GPIO_CNF_IN_FLOATING);
    }
    else
    {
        AFIO->PCFR1 &= ~(AFIO_PCFR1_USART1_REMAP | AFIO_PCFR1_USART1_REMAP_1);
        mode(kTxRet, GPIO_CNF_IN_FLOATING);
        mode(kTxFwd, GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF);
        mode(kRxFwd, GPIO_CNF_IN_FLOATING);
    }
    routed_last = last;
    tap_driven  = false;
}

static void tx_push(const uint8_t* b, size_t n)
{
    if(n && routed_last && !tap_driven)
    {
        mode(kTxRet, GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF); // drive the tap
        tap_driven = true;
    }
    for(size_t i = 0; i < n; i++)
        tx_fifo[tx_head++ & (TX_LEN - 1)] = b[i];
}

static bool tx_idle(void)
{
    return tx_head == tx_tail && (USART1->STATR & USART_STATR_TC);
}

static void tx_pump(void)
{
    if(tx_tail != tx_head && (USART1->STATR & USART_STATR_TXE))
        USART1->DATAR = tx_fifo[tx_tail++ & (TX_LEN - 1)];
    if(tap_driven && tx_idle())
    {
        mode(kTxRet, GPIO_CNF_IN_FLOATING); // release the return wire
        tap_driven = false;
    }
}

// --- State -----------------------------------------------------------------------------

static sat_board  board;
static sat_stream st;
static sat_slot   inputs[4];
static uint8_t    n_inputs;
static bool       fault; // the kind couldn't be read, or isn't supported here

static sat_pot_filter pot_filt[4];
static sat_quad       enc[4];
static sat_ladder     ladder;

static uint8_t  led_level, led_mode; // from the last LED slot
static uint32_t identify_until_ms;
static bool     forced_last;         // CLOSE_RING_HERE until RING_AUTO
static bool     reset_pending;

static uint32_t now_ms;

// The ADC channels this board reads each millisecond, in order.
static int8_t  adc_chan[5];
static uint8_t adc_n, adc_i;
static bool    adc_busy;

static void setup_board(int kind, uint8_t detail)
{
    sat_descriptor me = {0};
    me.fw_major       = FW_MAJOR;
    me.fw_minor       = FW_MINOR;
    me.slots          = 1;
    n_inputs          = 1;

    if(kind == SAT_KIND_POT || kind == SAT_KIND_POT_BANK_4)
    {
        board = kind == SAT_KIND_POT ? (sat_board)SAT_BOARD_POT_1 : (sat_board)SAT_BOARD_POT_BANK_4;
        for(int i = 0; i < 4 && has(board.pots[i]); i++)
        {
            mode(board.pots[i], GPIO_CNF_IN_ANALOG);
            adc_chan[adc_n++] = sat_adc_channel(board.pots[i]);
        }
    }
    else if(kind == SAT_KIND_ENC_BANK_4)
    {
        board = (sat_board)SAT_BOARD_ENC_BANK_4;
        for(int i = 0; i < 4; i++)
        {
            mode(board.enc_a[i], GPIO_CNF_IN_FLOATING); // pulled up on the board
            mode(board.enc_b[i], GPIO_CNF_IN_FLOATING);
            quad_init(&enc[i], (uint8_t)get(board.enc_a[i]), (uint8_t)get(board.enc_b[i]));
        }
        mode(board.switches, GPIO_CNF_IN_ANALOG);
        adc_chan[adc_n++] = sat_adc_channel(board.switches);
        static const uint32_t r_switch[4] = {10000, 20000, 40200, 80600}; // R7, R11, R14, R17
        ladder_init(&ladder, r_switch, LADDER_PULL_UP, 1023);
    }

    if(kind == SAT_KIND_POT || kind == SAT_KIND_POT_BANK_4 || kind == SAT_KIND_ENC_BANK_4)
    {
        me.kind  = (uint8_t)kind;
        me.slots = sat_kind_slots((uint8_t)kind);
        n_inputs = me.slots;
        mode(board.present, GPIO_CNF_IN_PUPD);
        put(board.present, 1); // pull-up
        mode(board.led, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
        put(board.led, 0);
        for(int i = 0; i < n_inputs; i++)
            inputs[i] = sat_none();
    }
    else if(kind < 0)
    {
        fault     = true;
        me.kind   = SAT_KIND_NO_ID;
        inputs[0] = sat_fault(SAT_FAULT_ID_BETWEEN_BANDS, detail);
    }
    else if(kind == SAT_KIND_NO_ID)
    {
        me.kind   = SAT_KIND_NO_ID;
        inputs[0] = sat_slot_make(SAT_IN_EMPTY, 0, 0);
    }
    else
    {
        fault     = true; // a kind this firmware has no board for yet
        me.kind   = (uint8_t)kind;
        inputs[0] = sat_fault(SAT_FAULT_KIND_UNSUPPORTED, (uint8_t)kind);
    }
    stream_init(&st, me);
}

// Is this satellite the last in the ring? PRESENT high means nothing downstream.
static bool is_last(void)
{
    if(forced_last)
        return true;
    return has(board.present) ? get(board.present) : true;
}

// --- Per-millisecond and per-loop work ------------------------------------------------

static void inputs_poll(void)
{
    if(!has(board.enc_a[0]))
        return;
    for(int i = 0; i < 4; i++)
        quad_update(&enc[i], (uint8_t)get(board.enc_a[i]), (uint8_t)get(board.enc_b[i]));
}

static void adc_service(void)
{
    if(!adc_busy || !adc_ready())
        return;
    uint16_t raw = adc_result();
    adc_busy     = false;
    if(st.me.kind == SAT_KIND_POT || st.me.kind == SAT_KIND_POT_BANK_4)
        inputs[adc_i] = sat_pot(pot_filter(&pot_filt[adc_i], pot_scale10(raw), 2));
    else if(st.me.kind == SAT_KIND_ENC_BANK_4)
        ladder_update(&ladder, raw);
    if(++adc_i < adc_n)
    {
        adc_start(adc_chan[adc_i]);
        adc_busy = true;
    }
}

static void every_ms(void)
{
    // Start this millisecond's conversions; adc_service() walks them.
    if(adc_n && !adc_busy)
    {
        adc_i = 0;
        adc_start(adc_chan[0]);
        adc_busy = true;
    }
    if(st.me.kind == SAT_KIND_ENC_BANK_4)
        for(int i = 0; i < 4; i++)
            inputs[i] = sat_encoder(enc[i].detents, (ladder.pressed >> i) & 1, ladder.presses[i]);

    // Re-check PRESENT and re-route, but only between frames.
    if(!st.active && tx_idle())
    {
        bool last = is_last();
        if(last != routed_last)
            route(last);
        st.me.last = last;
    }
}

static void led_update(uint32_t ticks)
{
    if(!has(board.led))
        return;
    uint8_t level = led_level, m = led_mode;
    if(now_ms < identify_until_ms)
    {
        level = 255;
        m     = SAT_LED_FAST_BLINK;
    }
    bool gate = m == SAT_LED_SLOW_BLINK   ? (now_ms % 1000) < 500
                : m == SAT_LED_FAST_BLINK ? (now_ms % 250) < 125
                                          : true;
    uint8_t phase = (uint8_t)(ticks / (4 * TICKS_US)); // ~1 kHz software PWM
    put(board.led, gate && phase < level);
}

// A frame has gone through: act on what it brought.
static void frame_done(void)
{
    if(st.took && st.taken[0].kind == SAT_OUT_LED && sat_slot_ok(st.taken[0]))
    {
        led_level = st.taken[0].d0;
        led_mode  = st.taken[0].d1;
    }
    for(uint8_t i = 0; i < st.n_cmds; i++)
    {
        sat_slot c = st.cmds[i];
        if(c.kind == SAT_CMD_CLOSE_RING_HERE)
            forced_last = true;
        else if(c.kind == SAT_CMD_RING_AUTO)
            forced_last = false;
        else if(c.kind == SAT_CMD_RESET)
            reset_pending = true;
        else if(c.kind == SAT_CMD_IDENTIFY)
            identify_until_ms = now_ms + 1000u * c.d1;
    }
}

int main(void)
{
    SystemInit();
    funGpioInitAll();
    adc_init();

    mode(kIdPin, GPIO_CNF_IN_ANALOG);
    mode(kIdFeed, GPIO_CNF_IN_FLOATING);
    sat_kind_io io     = {id_read, id_feed, 0, 1023};
    uint8_t     detail = 0;
    int         kind   = sat_detect_kind(&io, &detail);
    setup_board(kind, detail);

    uart_init();
    route(is_last());
    st.me.last = routed_last;

    uint32_t last_rx = SysTick->CNT, ms_tick = SysTick->CNT;
    uint8_t  out[SAT_MAX_FRAME_LEN + SAT_SLOT_LEN];

    for(;;)
    {
        uint32_t t = SysTick->CNT;

        // Received bytes, in order, straight into the stream.
        uint32_t head = (RX_LEN - DMA1_Channel5->CNTR) % RX_LEN;
        while(rx_tail != head)
        {
            uint8_t b = rx_ring[rx_tail];
            rx_tail   = (rx_tail + 1) % RX_LEN;
            last_rx   = t;
            tx_push(out, stream_rx(&st, b, inputs, out));
            if(stream_done(&st))
            {
                frame_done();
                stream_reset(&st); // ready for the next frame's SOF
            }
        }
        tx_pump();

        // Two idle byte times end a frame, finished or not (contract §2.2).
        if(st.active && t - last_rx > IDLE_GAP_TICKS)
            stream_reset(&st);

        if(reset_pending && tx_idle())
            NVIC_SystemReset();

        inputs_poll();
        adc_service();
        if(t - ms_tick >= TICKS_MS)
        {
            ms_tick += TICKS_MS;
            now_ms++;
            every_ms();
        }
        led_update(t);
    }
}
