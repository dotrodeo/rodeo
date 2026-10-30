// Rev B bring-up 09: the internal microSD card as a USB drive.
//
// The model is usb_device's RAM-disk disk mode, now on the real card: the
// computer and the instrument never have the file system at the same time.
//
//   * Normal mode, "Rodeo": serial + MIDI (echoes). The Seed has the card,
//     through FatFS on SDMMC1 in 1-bit mode (as in examples/sd_card), and creates
//     README.TXT on it if there isn't one.
//   * 'd' on the serial port: the Seed unmounts the card and re-enumerates as
//     "Rodeo (disk mode)", serial + USB storage. The computer mounts the card as a
//     drive; copy files to it.
//   * Eject the drive on the computer (or 'n' on the serial port): the Seed
//     re-enumerates as "Rodeo", remounts the card and lists what it finds, every
//     file with its size and CRC-32 and the first line of each .TXT, so a file
//     copied from the computer can be checked byte for byte (crc32 on a Mac:
//     `crc32 file`, or `python3 -c "import zlib,sys;print(hex(zlib.crc32(open(sys.argv[1],'rb').read())))" file`).
//   * 'r' lists the card again; 'B' reboots into the STM32's DFU bootloader.
//
//   make                 Rev B's USB-C, through the power board (OTG_HS, pins 36/37)
//   make RODEO_USB=fs    the Seed's own micro-USB
//
// Full-speed USB moves roughly 0.5-1 MB/s; the card's 1-bit bus is several times
// faster, so USB sets the pace. The card must be FAT16 or FAT32 for the Seed to
// read it (libDaisy's FatFS has no exFAT); the computer will mount an exFAT card
// as a drive, but the Seed's listing will say it can't.
#include <string.h>

#include "daisy_seed.h"
#include "fatfs.h"
#include "diskio.h"
#include "tusb.h"
#include "usb_port.h"
#include "dfu.h"

using namespace daisy;

static DaisySeed      hw;
static SdmmcHandler   sdmmc;
static FatFSInterface fsi DSY_SDRAM_BSS; // FatFS reads sectors into it: see sd_stage

extern "C"
{
    volatile bool usb_disk_mode = false;
}

//------------- The card -------------//

static constexpr uint32_t kSectorSize = 512;

static BYTE          pdrv;                 // FatFS drive number of the SD card
static bool          card_mounted = false; // FatFS has it (normal mode)
static bool          card_ready   = false; // initialised, for USB storage (disk mode)
static uint32_t      card_sectors = 0;
static volatile bool disk_ejected = false; // the computer ejected the drive
static volatile bool leave_disk   = false; // set from the eject callback, acted on in main()
static char          report[1536];         // printed on the serial port once it reopens
static bool          report_pending = false;

// SDMMC1's DMA reaches AXI SRAM and the SDRAM, not DTCM (the stack, and statics
// under the bootloader) or SRAM1. USB storage's buffers go through this one, in
// the SDRAM; 32-byte aligned for the driver's cache maintenance. FatFS moves a
// partial sector through a FIL's own buffer, so a FIL in DTCM reads a short file
// as nothing: those go in the SDRAM too.
alignas(32) static uint8_t sd_stage[CFG_TUD_MSC_EP_BUFSIZE] DSY_SDRAM_BSS;
static_assert(sizeof(sd_stage) % 32 == 0 && sizeof(sd_stage) % kSectorSize == 0, "staging");

static uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n)
{
    crc = ~crc;
    while(n--)
    {
        crc ^= *p++;
        for(int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool Mount()
{
    card_mounted = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
    return card_mounted;
}

static void WriteReadmeIfMissing()
{
    char    name[32];
    FILINFO fi;
    snprintf(name, sizeof(name), "%sREADME.TXT", fsi.GetSDPath());
    if(f_stat(name, &fi) == FR_OK)
        return;
    const char* text = "Rodeo SD card. Put files here, eject the drive, and Rodeo\r\n"
                       "lists their names, sizes, CRC-32s and the first line of each\r\n"
                       ".TXT on its serial port.\r\n";
    static FIL f DSY_SDRAM_BSS; // see sd_stage
    UINT       bw = 0;
    if(f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        f_write(&f, text, strlen(text), &bw);
        f_close(&f);
    }
}

/** The card's size and root folder: name, size, CRC-32, and a .TXT's first line. */
static void CardReport(char* out, size_t len)
{
    size_t n = snprintf(out, len, "\r\nSD card, as the Seed sees it:\r\n");
    if(!card_mounted)
    {
        snprintf(out + n, len - n,
                 "  can't mount it: is a card in, and is it FAT16 or FAT32?\r\n");
        return;
    }
    DWORD  free_clusters;
    FATFS* fsp;
    if(f_getfree(fsi.GetSDPath(), &free_clusters, &fsp) == FR_OK)
        n += snprintf(out + n, len - n, "  %lu MB, %lu MB free\r\n",
                      (unsigned long)((fsp->n_fatent - 2) * fsp->csize / 2048),
                      (unsigned long)(free_clusters * fsp->csize / 2048));
    DIR     dir;
    FILINFO fi;
    if(f_opendir(&dir, fsi.GetSDPath()) != FR_OK)
    {
        snprintf(out + n, len - n, "  (can't open the root folder)\r\n");
        return;
    }
    while(f_readdir(&dir, &fi) == FR_OK && fi.fname[0] && n < len - 100)
    {
        if(fi.fattrib & (AM_DIR | AM_HID | AM_SYS) || fi.fname[0] == '.')
            continue;
        char path[300];
        snprintf(path, sizeof(path), "%s%s", fsi.GetSDPath(), fi.fname);
        static FIL f DSY_SDRAM_BSS; // see sd_stage
        uint32_t   crc       = 0;
        char       first[48] = "";
        if(f_open(&f, path, FA_READ) == FR_OK)
        {
            UINT br;
            bool firstblock = true;
            while(f_read(&f, sd_stage, sizeof(sd_stage), &br) == FR_OK && br)
            {
                crc = Crc32(crc, sd_stage, br);
                if(firstblock)
                {
                    size_t k = 0;
                    while(k < br && k < sizeof(first) - 1 && sd_stage[k] != '\r'
                          && sd_stage[k] != '\n')
                    {
                        first[k] = (sd_stage[k] >= 32 && sd_stage[k] < 127) ? (char)sd_stage[k]
                                                                            : '?';
                        k++;
                    }
                    first[k]   = 0;
                    firstblock = false;
                }
            }
            f_close(&f);
        }
        const char* ext  = strrchr(fi.fname, '.');
        bool        text = ext && (strcmp(ext, ".TXT") == 0 || strcmp(ext, ".txt") == 0);
        n += snprintf(out + n,
                      len - n,
                      "  %-20s %9lu bytes  crc32 %08lx%s%s%s\r\n",
                      fi.fname,
                      (unsigned long)fi.fsize,
                      (unsigned long)crc,
                      text ? "  \"" : "",
                      text ? first : "",
                      text ? "\"" : "");
    }
    f_closedir(&dir);
}

/** Drop off the bus and come back with the other configuration. */
static void Reenumerate(bool disk)
{
    tud_disconnect();
    System::Delay(300); // long enough for the host to notice the device left
    usb_disk_mode = disk;
    tud_connect();
}

static void EnterDiskMode()
{
    // The computer gets the card to itself: unmount ours first.
    f_mount(nullptr, fsi.GetSDPath(), 0);
    card_mounted = false;
    disk_ejected = false;
    // Initialised afresh, so a card put in since boot is found.
    DWORD sectors = 0;
    card_ready    = disk_initialize(pdrv) == 0
                 && disk_ioctl(pdrv, GET_SECTOR_COUNT, &sectors) == RES_OK && sectors > 0;
    card_sectors = card_ready ? sectors : 0;
    Reenumerate(true);
}

static void LeaveDiskMode()
{
    disk_ioctl(pdrv, CTRL_SYNC, nullptr);
    card_ready = false;
    Reenumerate(false);
    Mount();
    CardReport(report, sizeof(report));
    report_pending = true;
}

//------------- TinyUSB mass-storage callbacks (disk mode) -------------//

extern "C"
{
    void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
    {
        (void)lun;
        memcpy(vendor_id, "RODEO   ", 8);
        memcpy(product_id, "SD card         ", 16);
        memcpy(product_rev, "1.0 ", 4);
    }

    bool tud_msc_test_unit_ready_cb(uint8_t lun)
    {
        if(disk_ejected || !card_ready)
        {
            tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); // medium not present
            return false;
        }
        return true;
    }

    void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
    {
        (void)lun;
        *block_count = card_sectors;
        *block_size  = kSectorSize;
    }

    bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
    {
        (void)lun;
        (void)power_condition;
        if(load_eject && !start)
        {
            // The computer ejected the drive: hand the card back to the Seed.
            disk_ejected = true;
            leave_disk   = true;
        }
        return true;
    }

    // TinyUSB passes the command's first LBA and a byte offset into the transfer;
    // the buffer is at most CFG_TUD_MSC_EP_BUFSIZE, whole sectors.
    int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
    {
        (void)lun;
        uint32_t const sector = lba + offset / kSectorSize;
        uint32_t const count  = bufsize / kSectorSize;
        if(!card_ready || offset % kSectorSize || bufsize % kSectorSize
           || bufsize > sizeof(sd_stage) || sector + count > card_sectors)
            return -1;
        if(disk_read(pdrv, sd_stage, sector, count) != RES_OK)
            return -1;
        memcpy(buffer, sd_stage, bufsize);
        return (int32_t)bufsize;
    }

    int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
    {
        (void)lun;
        uint32_t const sector = lba + offset / kSectorSize;
        uint32_t const count  = bufsize / kSectorSize;
        if(!card_ready || offset % kSectorSize || bufsize % kSectorSize
           || bufsize > sizeof(sd_stage) || sector + count > card_sectors)
            return -1;
        memcpy(sd_stage, buffer, bufsize);
        if(disk_write(pdrv, sd_stage, sector, count) != RES_OK)
            return -1;
        return (int32_t)bufsize;
    }

    bool tud_msc_is_writable_cb(uint8_t lun)
    {
        (void)lun;
        return true;
    }

    int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
    {
        (void)scsi_cmd;
        (void)buffer;
        (void)bufsize;
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); // invalid command
        return -1;
    }
}

//------------- Serial and MIDI -------------//

static void CdcPrint(const char* s)
{
    if(!tud_cdc_connected())
        return;
    tud_cdc_write_str(s);
    tud_cdc_write_flush();
}

static void CdcTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
    {
        if(usb_disk_mode)
            CdcPrint(card_ready
                         ? "\r\nRodeo SD test, DISK MODE: the card is the computer's.\r\n"
                           "Eject it to hand it back, or n = leave disk mode now.\r\n"
                         : "\r\nRodeo SD test, DISK MODE, but there is no card.\r\n"
                           "n = leave disk mode.\r\n");
        else
        {
            CdcPrint("\r\nRodeo SD test: TinyUSB " TUSB_VERSION_STRING
                     ", serial + MIDI on " RODEO_USB_PORT_NAME ".\r\n"
                     "d = disk mode (the card as a USB drive); r = list the card;\r\n"
                     "B = reboot into DFU (development)\r\n");
            if(!report_pending)
            {
                CardReport(report, sizeof(report));
                report_pending = true;
            }
        }
        if(report_pending && !usb_disk_mode)
        {
            CdcPrint(report);
            report_pending = false;
        }
    }
    was_connected = connected;

    while(tud_cdc_available())
    {
        char c = (char)tud_cdc_read_char();
        switch(c)
        {
            case 'd':
                if(!usb_disk_mode)
                {
                    CdcPrint("entering disk mode: this port will close and reopen\r\n");
                    System::Delay(50);
                    EnterDiskMode();
                    return;
                }
                break;
            case 'n':
                if(usb_disk_mode)
                {
                    CdcPrint("leaving disk mode\r\n");
                    System::Delay(50);
                    leave_disk = true;
                }
                break;
            case 'r':
                if(!usb_disk_mode)
                {
                    if(!card_mounted)
                        Mount();
                    CardReport(report, sizeof(report));
                    CdcPrint(report);
                }
                break;
            case 'B':
                CdcPrint("rebooting into the DFU bootloader (on the Seed's micro-USB)\r\n");
                System::Delay(50);
                ResetToDfu();
                break;
            default: break;
        }
    }
}

static void MidiTask()
{
    uint8_t packet[4];
    while(tud_midi_available() && tud_midi_packet_read(packet))
        tud_midi_packet_write(packet);
}

int main(void)
{
    hw.Init();

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults(); // 50 MHz
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
    sdmmc.Init(sd_cfg);
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
    fsi.Init(fsi_cfg);
    pdrv = (BYTE)(fsi.GetSDPath()[0] - '0');
    if(Mount())
        WriteReadmeIfMissing();

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    uint32_t blink = 0;
    for(;;)
    {
        tud_task();
        CdcTask();
        if(!usb_disk_mode)
            MidiTask();

        if(leave_disk)
        {
            leave_disk = false;
            if(usb_disk_mode)
            {
                System::Delay(100); // let the eject's status reach the computer
                LeaveDiskMode();
            }
        }

        uint32_t now    = System::GetNow();
        uint32_t period = usb_disk_mode ? 1000 : (tud_mounted() ? 250 : 500);
        if(now - blink >= period)
        {
            blink = now;
            static bool led = false;
            led             = !led;
            hw.SetLed(led);
        }
    }
}
