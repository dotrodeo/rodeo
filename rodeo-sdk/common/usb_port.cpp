#include "usb_port.h"

#include "daisy.h"
#include "tusb.h"

static_assert(BOARD_TUD_RHPORT == RODEO_USB_RHPORT,
              "tusb_config.h must set BOARD_TUD_RHPORT to RODEO_USB_RHPORT");

extern "C"
{
    static void TinyUsbIrq(void) { tusb_int_handler(RODEO_USB_RHPORT, true); }

    uint32_t tusb_time_millis_api(void) { return daisy::System::GetNow(); }
}

namespace rodeo
{
namespace
{
#ifdef RODEO_USB_HS
constexpr IRQn_Type kIrq = OTG_HS_IRQn;
#else
constexpr IRQn_Type kIrq = OTG_FS_IRQn;
#endif

// 256 entries covers the H750's 16 + 150 vectors; VTOR needs the table aligned
// to its size rounded up to a power of two. DTCM, so no cache between the CPU's
// writes and the vector fetch.
alignas(1024) uint32_t ram_vectors[256] __attribute__((section(".dtcmram_bss")));

void RouteIrqToTinyUsb()
{
    uint32_t const* vectors = (uint32_t const*)SCB->VTOR;
    if(vectors != ram_vectors)
    {
        for(int i = 0; i < 256; i++)
            ram_vectors[i] = vectors[i];
    }
    ram_vectors[16 + kIrq] = (uint32_t)&TinyUsbIrq;
    __disable_irq();
    SCB->VTOR = (uint32_t)ram_vectors;
    __DSB();
    __ISB();
    __enable_irq();
}
} // namespace

void UsbPortInit()
{
    RouteIrqToTinyUsb();

    GPIO_InitTypeDef g = {};
    g.Mode             = GPIO_MODE_AF_PP;
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_HIGH;
#ifdef RODEO_USB_HS
    // PB14 DM, PB15 DP: the OTG_HS core's (USB1's) embedded full-speed PHY.
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin       = GPIO_PIN_14 | GPIO_PIN_15;
    g.Alternate = GPIO_AF12_OTG1_FS;
    HAL_GPIO_Init(GPIOB, &g);
#else
    // PA11 DM, PA12 DP: the OTG_FS core (USB2).
    __HAL_RCC_GPIOA_CLK_ENABLE();
    g.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    g.Alternate = GPIO_AF10_OTG2_HS; // the FS core's AF on the H750
    HAL_GPIO_Init(GPIOA, &g);
#endif

    // libDaisy's System::Init already clocks both cores' USB kernel clock from
    // HSI48 (one USBSEL for both).
    HAL_PWREx_EnableUSBVoltageDetector();
#ifdef RODEO_USB_HS
    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB1_OTG_HS_ULPI_CLK_DISABLE(); // no external PHY
#else
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
#endif

    // Neither port has VBUS on its sense pin (PA9 / PB13), so sensing is off and
    // TinyUSB forces the B-session valid.
    tud_configure_dwc2_t cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
    cfg.vbus_sensing         = false;
    tud_configure(RODEO_USB_RHPORT, TUD_CFGID_DWC2, &cfg);

    // Audio (SAI DMA, libDaisy's priority 0) must be able to preempt USB.
    NVIC_SetPriority(kIrq, 6);
}
} // namespace rodeo
