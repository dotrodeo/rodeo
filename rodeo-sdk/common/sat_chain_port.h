#pragma once
//
// The satellite chain's UART on the Seed: USART3 at 1 Mbaud, both directions on
// DMA, carrying the exchanges SatChain asks for. Target only; SatChain itself
// (sat_chain.h) is pure logic and host-tested.
//
//   rodeo::SatChain     chain;
//   rodeo::SatChainPort port;
//
//   chain.Init(rodeo::SatChain::Config(), System::GetUs());
//   port.Init();
//   for(;;)
//   {
//       port.Process(chain);          // as often as the main loop comes round
//       float cutoff = chain.Pot(0, 2);
//   }
//
// Pins and streams (rev-b.md, "Satellite chain on USART3"): TX on PC10 (Seed pin
// 3), RX on PC11 (pin 2), alternate function 7. RX on DMA1_Stream7 and TX on
// DMA2_Stream5, which libDaisy doesn't use. Buffers sit in SRAM1, which libDaisy
// maps uncached, so neither side needs cache maintenance. No interrupts: Process()
// polls the DMA counters, so nothing here competes with audio for priority.
//
#include "sat_chain.h"

namespace rodeo
{
class SatChainPort
{
  public:
    /** Pins, USART3 and both DMA streams. Call once, after the Seed's Init(). */
    void Init();

    /** Collect a finished or timed-out reply, and start the next exchange the
     *  chain wants. Never blocks. */
    void Process(SatChain& chain);

    /** Bytes received so far in the exchange in flight (for diagnostics). */
    uint16_t Received() const;

  private:
    void Start(const SatChain::Exchange& ex, uint32_t now_us);
    void Stop();

    bool     busy_ = false;
    uint32_t t_start_ = 0, timeout_us_ = 0;
    uint16_t expect_ = 0; // 0 until the reply's header says
    uint16_t armed_ = 0;  // bytes the RX DMA was given room for
};
} // namespace rodeo
