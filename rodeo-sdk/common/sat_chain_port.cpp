#include "sat_chain_port.h"

#include "daisy_seed.h"

using namespace daisy;

namespace rodeo
{
namespace
{
// DMA reaches SRAM1, not the DTCM where ordinary statics live, and libDaisy
// maps SRAM1 uncached.
DMA_BUFFER_MEM_SECTION uint8_t tx_buf[SAT_MAX_FRAME_LEN];
DMA_BUFFER_MEM_SECTION uint8_t rx_buf[SAT_MAX_FRAME_LEN + SAT_SLOT_LEN];

DMA_Stream_TypeDef* const kRx = DMA1_Stream7;
DMA_Stream_TypeDef* const kTx = DMA2_Stream5;

// Every flag of each stream, for its interrupt-flag clear register (HIFCR):
// stream 7 is bits 22-27 of DMA1's, stream 5 bits 6-11 of DMA2's.
constexpr uint32_t kRxFlags = DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7
                              | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
constexpr uint32_t kTxFlags = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5
                              | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;

// USART3's kernel clock. HAL_RCCEx_GetPeriphCLKFreq() returns 0 for the USART
// group in this HAL, which left BRR at 0 and the UART dead: read the clock
// selection instead. libDaisy leaves it at its reset value, PCLK1.
uint32_t Usart3Clock()
{
    switch(RCC->D2CCIP2R & RCC_D2CCIP2R_USART28SEL)
    {
        case 0: return HAL_RCC_GetPCLK1Freq();
        case RCC_D2CCIP2R_USART28SEL_0 | RCC_D2CCIP2R_USART28SEL_1: // HSI
            return HSI_VALUE >> ((RCC->CR & RCC_CR_HSIDIV) >> RCC_CR_HSIDIV_Pos);
        case RCC_D2CCIP2R_USART28SEL_2: return CSI_VALUE;
        default: return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART234578);
    }
}

void Disable(DMA_Stream_TypeDef* s)
{
    s->CR &= ~DMA_SxCR_EN;
    while(s->CR & DMA_SxCR_EN) {}
}
} // namespace

void SatChainPort::Init()
{
    // PC10 TX, PC11 RX, AF7. RX also has the motherboard's 10 k pull-up.
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {};
    g.Pin              = GPIO_PIN_10 | GPIO_PIN_11;
    g.Mode             = GPIO_MODE_AF_PP;
    g.Pull             = GPIO_PULLUP;
    g.Speed            = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate        = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &g);

    // USART3 by its registers: libDaisy's own HAL_UART_MspInit would claim it
    // otherwise. 8N1, 16x oversampling, no FIFO. Overrun detection off, so a
    // late DMA can't latch the receiver; a lost byte shows up as a short reply.
    __HAL_RCC_USART3_CLK_ENABLE();
    USART3->CR1 = 0;
    uint32_t clk = Usart3Clock();
    USART3->BRR  = (clk + SAT_BAUD / 2) / SAT_BAUD;
    USART3->CR2  = 0;
    USART3->CR3  = USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_OVRDIS;
    USART3->CR1  = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    Disable(kRx);
    Disable(kTx);
    // DMAMUX1 channel n serves DMA1 stream n (0-7) and DMA2 stream n-8 (8-15).
    DMAMUX1_Channel7->CCR  = DMA_REQUEST_USART3_RX;
    DMAMUX1_Channel13->CCR = DMA_REQUEST_USART3_TX;
    kRx->PAR = reinterpret_cast<uint32_t>(&USART3->RDR);
    kTx->PAR = reinterpret_cast<uint32_t>(&USART3->TDR);
    kRx->FCR = 0; // direct mode, byte by byte
    kTx->FCR = 0;
    busy_    = false;
}

void SatChainPort::Start(const SatChain::Exchange& ex, uint32_t now_us)
{
    for(uint16_t i = 0; i < ex.tx_len; i++)
        tx_buf[i] = ex.tx[i];

    // Arm the receiver first: on a short ring the reply starts within microseconds.
    expect_ = ex.rx_len;
    armed_  = ex.rx_len ? ex.rx_len : static_cast<uint16_t>(sizeof rx_buf);
    Disable(kRx);
    DMA1->HIFCR = kRxFlags;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    USART3->RQR = USART_RQR_RXFRQ; // drop anything left from before
    kRx->M0AR   = reinterpret_cast<uint32_t>(rx_buf);
    kRx->NDTR   = armed_;
    kRx->CR     = DMA_SxCR_MINC | DMA_SxCR_EN; // peripheral to memory, bytes

    Disable(kTx);
    DMA2->HIFCR = kTxFlags;
    USART3->ICR = USART_ICR_TCCF;
    kTx->M0AR   = reinterpret_cast<uint32_t>(tx_buf);
    kTx->NDTR   = ex.tx_len;
    kTx->CR     = DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_EN; // memory to peripheral

    t_start_    = now_us;
    timeout_us_ = ex.timeout_us;
    busy_       = true;
    exchanges++;
}

void SatChainPort::Stop()
{
    Disable(kRx);
    Disable(kTx);
    busy_ = false;
}

uint16_t SatChainPort::Received() const
{
    return busy_ ? static_cast<uint16_t>(armed_ - kRx->NDTR) : 0;
}

void SatChainPort::Process(SatChain& chain)
{
    uint32_t now = System::GetUs();
    if(busy_)
    {
        uint16_t got = Received();
        // A reply of unknown length (DISCOVER) says how long it is in its header.
        if(!expect_ && got >= SAT_HEADER_LEN && rx_buf[SAT_H_SOF] == SAT_SOF
           && rx_buf[SAT_H_N] <= SAT_MAX_SLOTS)
            expect_ = static_cast<uint16_t>(SAT_FRAME_LEN(rx_buf[SAT_H_N]));
        bool done     = expect_ && got >= expect_;
        bool timedout = now - t_start_ >= timeout_us_;
        if(!done && !timedout)
            return;
        Stop();
        last_len = done ? expect_ : got;
        for(int i = 0; i < 8; i++)
            last_head[i] = rx_buf[i];
        chain.Reply(rx_buf, done ? expect_ : got, now);
    }
    SatChain::Exchange ex;
    if(chain.Poll(now, ex))
        Start(ex, now);
}
} // namespace rodeo
