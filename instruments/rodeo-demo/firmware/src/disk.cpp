// Disk mode. See disk.h; the mass-storage callbacks are usb_storage's.
#include "disk.h"

#include <string.h>
#include "daisy_seed.h"
#include "diskio.h"
#include "tusb.h"
#include "samples.h"
#include "synth.h"
#include "usb_audio.h"

using namespace daisy;

extern "C"
{
    volatile bool usb_disk_mode = false; // read by the SDK's usb_desc.c
}

namespace
{
constexpr uint32_t kSectorSize = 512;
// How long the computer may be gone before disk mode gives up: the
// re-enumeration takes about a second.
constexpr uint32_t kHostGoneMs = 3000;

uint8_t       pdrv         = 0;
bool          card_ready   = false; // initialised, for USB storage
uint32_t      card_sectors = 0;
volatile bool ejected      = false; // the computer ejected the drive
volatile bool leave        = false; // acted on in Task()
uint32_t      last_seen    = 0;     // when the computer was last there

// SDMMC1's DMA reaches the SDRAM, not DTCM, where USB storage's buffer may be:
// sectors go through this, 32-byte aligned for the driver's cache maintenance.
alignas(32) uint8_t sd_stage[CFG_TUD_MSC_EP_BUFSIZE] DSY_SDRAM_BSS;
static_assert(sizeof(sd_stage) % 32 == 0 && sizeof(sd_stage) % kSectorSize == 0, "staging");

/** Drop off the bus and come back with the other configuration. */
void Reenumerate(bool disk)
{
    tud_disconnect();
    System::Delay(300); // long enough for the computer to notice the device left
    usb_audio::Reset();
    usb_disk_mode = disk;
    tud_connect();
}
} // namespace

namespace disk
{
bool Enter()
{
    if(usb_disk_mode)
        return true;
    // Initialised afresh, so a card put in since power-up is found
    pdrv          = samples::Drive();
    DWORD sectors = 0;
    card_ready    = disk_initialize(pdrv) == 0
                 && disk_ioctl(pdrv, GET_SECTOR_COUNT, &sectors) == RES_OK && sectors > 0;
    if(!card_ready)
        return false; // nothing to hand over
    samples::Unmount();
    card_sectors = sectors;
    ejected      = false;
    leave        = false;
    last_seen    = System::GetNow();
    Reenumerate(true);
    return true;
}

bool Active()
{
    return usb_disk_mode;
}

void Leave()
{
    leave = true;
}

bool Task()
{
    if(!usb_disk_mode)
        return false;
    uint32_t now = System::GetNow();
    if(tud_mounted())
        last_seen = now;
    else if(now - last_seen >= kHostGoneMs)
        leave = true;
    if(!leave)
        return false;

    leave = false;
    System::Delay(100); // let the eject's status reach the computer
    disk_ioctl(pdrv, CTRL_SYNC, nullptr);
    card_ready = false;
    Reenumerate(false);
    synth::DropSamples(); // the library they play from is about to be rewritten
    samples::Remount();
    return true;
}
} // namespace disk

//------------- TinyUSB mass-storage callbacks -------------//

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
        if(ejected || !card_ready)
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
            // The computer ejected the drive: hand the card back.
            ejected = true;
            leave   = true;
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
