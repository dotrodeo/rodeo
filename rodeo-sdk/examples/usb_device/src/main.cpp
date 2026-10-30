// USB device test: a bare Daisy Seed on its micro-USB, on TinyUSB, as a USB audio
// interface (two-way stereo, 48 kHz, 24-bit) with USB-MIDI and a serial port, plus a
// disk mode that offers a RAM disk as USB storage. See planning/REV_B_PLAN.md,
// "USB".
//
//   * Computer -> Seed: whatever the computer plays to "Rodeo" comes out of the Seed's
//     audio outputs. Asynchronous: the Seed's codec clock sets the rate and a feedback
//     endpoint keeps the computer in step.
//   * Seed -> computer: what "Rodeo" records, chosen on the serial port:
//       c  the Seed's audio inputs (default)
//       l  loopback: what the computer is playing to the Seed, returned digitally
//       t  a 1 kHz test tone at -12 dBFS (left) and 1.5 kHz (right)
//   * Serial: 's' prints stream state and counters.
//   * Disk mode ('d' on the serial port): the Seed unmounts its own file system and
//     re-enumerates as "Rodeo (disk mode)", serial + USB storage, offering a 16 MB FAT
//     RAM disk in its SDRAM. Eject it on the computer (or 'n' on the serial port) and
//     it re-enumerates as the audio device, remounts the disk and reports what it
//     finds: every file with its size and CRC-32, and the first line of each .TXT.
//     This is the model for Rev B's SD card: the computer and the instrument never
//     have the file system at the same time.
//   * MIDI: echoes what it receives.
//
// DaisySeed brings libDaisy's own USB code with it, including an OTG_FS interrupt
// handler, so rodeo::UsbPortInit() points the interrupt at TinyUSB by copying the
// vector table to RAM and replacing that one entry; Rev B's port (OTG_HS, with
// `make RODEO_USB=hs`) needs the same, because libDaisy's system.cpp defines that
// handler for every app. See common/usb_port.h.
#include <math.h>
#include "daisy_seed.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usb_port.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "dfu.h"

using namespace daisy;

static DaisySeed hw;

extern "C"
{
    volatile bool usb_disk_mode = false;
}

//------------- RAM disk -------------//
// 16 MB of the Seed's 64 MB SDRAM, 512-byte sectors, FAT16. Stands in for Rev B's SD
// card: the same FatFs on one side and USB mass storage on the other.

static constexpr uint32_t kSectorSize = 512;
static constexpr uint32_t kSectors    = 32768; // 16 MiB
static uint8_t DSY_SDRAM_BSS ramdisk[kSectors * kSectorSize];

extern "C"
{
    static DSTATUS RamDiskInit(BYTE) { return 0; }
    static DSTATUS RamDiskStatus(BYTE) { return 0; }
    static DRESULT RamDiskRead(BYTE, BYTE* buf, DWORD sector, UINT count)
    {
        if(sector + count > kSectors)
            return RES_PARERR;
        memcpy(buf, &ramdisk[sector * kSectorSize], count * kSectorSize);
        return RES_OK;
    }
    static DRESULT RamDiskWrite(BYTE, const BYTE* buf, DWORD sector, UINT count)
    {
        if(sector + count > kSectors)
            return RES_PARERR;
        memcpy(&ramdisk[sector * kSectorSize], buf, count * kSectorSize);
        return RES_OK;
    }
    static DRESULT RamDiskIoctl(BYTE, BYTE cmd, void* buf)
    {
        switch(cmd)
        {
            case CTRL_SYNC: return RES_OK;
            case GET_SECTOR_COUNT: *(DWORD*)buf = kSectors; return RES_OK;
            case GET_SECTOR_SIZE: *(WORD*)buf = kSectorSize; return RES_OK;
            case GET_BLOCK_SIZE: *(DWORD*)buf = 1; return RES_OK;
            default: return RES_PARERR;
        }
    }

    // FAT timestamps: no RTC on a bare Seed, so a fixed date (2026-10-18). Overrides the
    // weak one in ST's diskio.c, which returns 0 (1980-00-00, which macOS shows as 1969).
    // libDaisy's FatFSInterface (sys/fatfs.cpp) defines one too; this example doesn't
    // link it.
    DWORD get_fattime(void)
    {
        return ((DWORD)(2026 - 1980) << 25) | ((DWORD)10 << 21) | ((DWORD)18 << 16);
    }
}

static const Diskio_drvTypeDef ramdisk_driver = {RamDiskInit,
                                                 RamDiskStatus,
                                                 RamDiskRead,
                                                 RamDiskWrite,
                                                 RamDiskIoctl};

static FATFS        fs;
static char         disk_path[4];
static bool         disk_mounted  = false;
static volatile bool disk_ejected = false; // the computer ejected the disk
static volatile bool leave_disk   = false; // set from the eject callback, acted on in main()
static char          report[1024];         // printed on the serial port once it reopens
static bool          report_pending = false;

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

static bool DiskInit()
{
    if(FATFS_LinkDriver(&ramdisk_driver, disk_path) != 0)
        return false;
    static BYTE work[_MAX_SS * 8];
    if(f_mkfs(disk_path, FM_FAT, 0, work, sizeof(work)) != FR_OK)
        return false;
    if(f_mount(&fs, disk_path, 1) != FR_OK)
        return false;
    disk_mounted = true;

    FIL         f;
    char        name[32];
    const char* text = "Rodeo RAM disk. Put files here, eject the disk, and the Seed\r\n"
                       "reports their names, sizes, CRC-32s and the first line of each\r\n"
                       ".TXT on its serial port.\r\n";
    snprintf(name, sizeof(name), "%sREADME.TXT", disk_path);
    UINT bw = 0;
    if(f_open(&f, name, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        f_write(&f, text, strlen(text), &bw);
        f_close(&f);
    }
    return true;
}

/** List the disk's root: name, size, CRC-32, and a .TXT's first line. */
static void DiskReport(char* out, size_t len)
{
    size_t  n = snprintf(out, len, "\r\nRAM disk, as the Seed sees it:\r\n");
    DIR     dir;
    FILINFO fi;
    if(f_opendir(&dir, disk_path) != FR_OK)
    {
        snprintf(out + n, len - n, "  (can't open the disk)\r\n");
        return;
    }
    while(f_readdir(&dir, &fi) == FR_OK && fi.fname[0] && n < len - 100)
    {
        if(fi.fattrib & (AM_DIR | AM_HID | AM_SYS) || fi.fname[0] == '.')
            continue;
        char path[300];
        snprintf(path, sizeof(path), "%s%s", disk_path, fi.fname);
        FIL      f;
        uint32_t crc   = 0;
        char     first[48] = "";
        if(f_open(&f, path, FA_READ) == FR_OK)
        {
            static uint8_t buf[4096];
            UINT           br;
            bool           firstblock = true;
            while(f_read(&f, buf, sizeof(buf), &br) == FR_OK && br)
            {
                crc = Crc32(crc, buf, br);
                if(firstblock)
                {
                    size_t k = 0;
                    while(k < br && k < sizeof(first) - 1 && buf[k] != '\r' && buf[k] != '\n')
                    {
                        first[k] = (buf[k] >= 32 && buf[k] < 127) ? (char)buf[k] : '?';
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
                      "  %-20s %8lu bytes  crc32 %08lx%s%s%s\r\n",
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
    // The computer gets the disk to itself: unmount ours first.
    f_mount(nullptr, disk_path, 0);
    disk_mounted = false;
    disk_ejected = false;
    Reenumerate(true);
}

static void LeaveDiskMode()
{
    Reenumerate(false);
    disk_mounted = f_mount(&fs, disk_path, 1) == FR_OK;
    DiskReport(report, sizeof(report));
    report_pending = true;
}

//------------- Audio state -------------//

static constexpr size_t kBlock       = 48; // frames per callback: 1 ms at 48 kHz
static constexpr size_t kFrameBytes  = AUDIO_N_CHANNELS * AUDIO_BYTES_PER_SAMPLE;
static constexpr size_t kBlockBytes  = kBlock * kFrameBytes;
// Start playing once 2 ms are queued: half the OUT buffer, the feedback's target level
static constexpr uint16_t kPrefill   = CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 2;

enum class MicSource
{
    Codec,
    Loopback,
    Tone
};

static volatile bool      spk_streaming = false;
static volatile bool      mic_streaming = false;
static volatile MicSource mic_source    = MicSource::Codec;
static bool               spk_playing   = false;

static volatile uint32_t spk_underruns = 0; // computer -> Seed: buffer ran dry
static volatile uint32_t mic_overruns  = 0; // Seed -> computer: buffer full, samples dropped
static volatile uint32_t spk_bytes     = 0;
static volatile uint32_t mic_bytes     = 0;
static volatile uint16_t spk_level_min = 0xFFFF, spk_level_max = 0;

// Diagnostics
static volatile uint32_t tx_packets = 0, tx_bytes = 0;
static volatile uint16_t tx_pkt_min = 0xFFFF, tx_pkt_max = 0;
static volatile uint16_t mic_fill_max = 0;
static volatile uint32_t callbacks = 0;

static volatile bool    spk_mute   = false;
static volatile int16_t spk_volume = 0; // 1/256 dB, master
static volatile float   spk_gain   = 1.0f;

static uint8_t usb_rx[kBlockBytes];
static uint8_t usb_tx[kBlockBytes];
static float   loop_l[kBlock], loop_r[kBlock];
static float   tone_phase_l = 0.f, tone_phase_r = 0.f;

static inline float Unpack24(const uint8_t* p)
{
    int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
    return (float)v * (1.0f / 8388608.0f);
}

static inline void Pack24(uint8_t* p, float x)
{
    if(x > 0.99999988f)
        x = 0.99999988f;
    if(x < -1.0f)
        x = -1.0f;
    int32_t v = (int32_t)(x * 8388608.0f);
    p[0]      = (uint8_t)v;
    p[1]      = (uint8_t)(v >> 8);
    p[2]      = (uint8_t)(v >> 16);
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    size_t const frames = size / 2;
    callbacks++;

    //--- computer -> Seed
    size_t got = 0;
    if(spk_streaming)
    {
        uint16_t level = tud_audio_available();
        if(level < spk_level_min)
            spk_level_min = level;
        if(level > spk_level_max)
            spk_level_max = level;
        if(!spk_playing && level >= kPrefill)
            spk_playing = true;
        if(spk_playing)
        {
            got = tud_audio_read(usb_rx, frames * kFrameBytes);
            spk_bytes += got;
            if(got < frames * kFrameBytes)
            {
                spk_underruns++;
                if(got == 0)
                    spk_playing = false; // queue up again before resuming
            }
        }
    }
    else
    {
        spk_playing = false;
    }

    float const gain = spk_mute ? 0.f : spk_gain;
    for(size_t i = 0; i < frames; i++)
    {
        float l = 0.f, r = 0.f;
        if((i + 1) * kFrameBytes <= got)
        {
            l = Unpack24(&usb_rx[i * kFrameBytes]);
            r = Unpack24(&usb_rx[i * kFrameBytes + 3]);
        }
        loop_l[i]      = l;
        loop_r[i]      = r;
        out[2 * i]     = l * gain;
        out[2 * i + 1] = r * gain;
    }

    //--- Seed -> computer
    if(mic_streaming)
    {
        for(size_t i = 0; i < frames; i++)
        {
            float l, r;
            switch(mic_source)
            {
                case MicSource::Loopback:
                    l = loop_l[i];
                    r = loop_r[i];
                    break;
                case MicSource::Tone:
                    l = 0.25f * sinf(tone_phase_l);
                    r = 0.25f * sinf(tone_phase_r);
                    tone_phase_l += 2.f * (float)M_PI * 1000.f / AUDIO_SAMPLE_RATE;
                    tone_phase_r += 2.f * (float)M_PI * 1500.f / AUDIO_SAMPLE_RATE;
                    if(tone_phase_l > 2.f * (float)M_PI)
                        tone_phase_l -= 2.f * (float)M_PI;
                    if(tone_phase_r > 2.f * (float)M_PI)
                        tone_phase_r -= 2.f * (float)M_PI;
                    break;
                default:
                    l = in[2 * i];
                    r = in[2 * i + 1];
                    break;
            }
            Pack24(&usb_tx[i * kFrameBytes], l);
            Pack24(&usb_tx[i * kFrameBytes + 3], r);
        }
        // TinyUSB's IN FIFO overwrites its oldest data when full, so check the room
        // first: drop whole new frames (and count it) rather than corrupt queued ones.
        tu_fifo_t* ff   = tud_audio_get_ep_in_ff();
        uint16_t   fill = tu_fifo_count(ff);
        if(fill > mic_fill_max)
            mic_fill_max = fill;
        uint16_t room = tu_fifo_remaining(ff);
        uint16_t want = (uint16_t)(frames * kFrameBytes);
        if(room < want)
        {
            mic_overruns++;
            want = (uint16_t)(room / kFrameBytes * kFrameBytes);
        }
        if(want)
            mic_bytes += tud_audio_write(usb_tx, want);
    }
}

//------------- TinyUSB audio callbacks -------------//

extern "C"
{
    bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        (void)rhport;
        uint8_t const itf = tu_u16_low(req->wIndex);
        uint8_t const alt = tu_u16_low(req->wValue);
        if(itf == ITF_NUM_AUDIO_SPK)
            spk_streaming = alt != 0;
        if(itf == ITF_NUM_AUDIO_MIC)
        {
            if(alt != 0)
                tud_audio_clear_ep_in_ff();
            mic_streaming = alt != 0;
        }
        return true;
    }

    bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        (void)rhport;
        uint8_t const itf = tu_u16_low(req->wIndex);
        if(itf == ITF_NUM_AUDIO_SPK)
            spk_streaming = false;
        if(itf == ITF_NUM_AUDIO_MIC)
            mic_streaming = false;
        return true;
    }

    bool tud_audio_tx_done_isr(uint8_t  rhport,
                               uint16_t n_bytes_sent,
                               uint8_t  func_id,
                               uint8_t  ep_in,
                               uint8_t  cur_alt_setting)
    {
        (void)rhport;
        (void)func_id;
        (void)ep_in;
        (void)cur_alt_setting;
        tx_packets++;
        tx_bytes += n_bytes_sent;
        if(n_bytes_sent < tx_pkt_min)
            tx_pkt_min = n_bytes_sent;
        if(n_bytes_sent > tx_pkt_max)
            tx_pkt_max = n_bytes_sent;
        return true;
    }

    void tud_audio_feedback_params_cb(uint8_t                  func_id,
                                      uint8_t                  alt_itf,
                                      audio_feedback_params_t* p)
    {
        (void)func_id;
        (void)alt_itf;
        // Track the OUT buffer's fill level around half full: the computer is told to
        // send a little more or less so the codec's own clock never starves or floods.
        p->method      = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
        p->sample_freq = AUDIO_SAMPLE_RATE;
    }

    // Endpoint requests: the sampling frequency (one rate, 48 kHz)
    bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const* req, uint8_t* buf)
    {
        (void)rhport;
        (void)buf;
        return TU_U16_HIGH(req->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ
               && req->bRequest == AUDIO10_CS_REQ_SET_CUR;
    }

    bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        if(TU_U16_HIGH(req->wValue) == AUDIO10_EP_CTRL_SAMPLING_FREQ
           && req->bRequest == AUDIO10_CS_REQ_GET_CUR)
        {
            uint8_t freq[3] = {(uint8_t)(AUDIO_SAMPLE_RATE & 0xFF),
                               (uint8_t)((AUDIO_SAMPLE_RATE >> 8) & 0xFF),
                               (uint8_t)((AUDIO_SAMPLE_RATE >> 16) & 0xFF)};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, freq, sizeof(freq));
        }
        return false;
    }

    // Feature unit: master mute and volume, applied to the Seed's outputs
    bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req, uint8_t* buf)
    {
        (void)rhport;
        uint8_t const entity = TU_U16_HIGH(req->wIndex);
        uint8_t const ctrl   = TU_U16_HIGH(req->wValue);
        if(entity != UAC1_SPK_FEATURE_UNIT || req->bRequest != AUDIO10_CS_REQ_SET_CUR)
            return false;
        if(ctrl == AUDIO10_FU_CTRL_MUTE)
        {
            spk_mute = buf[0] != 0;
            return true;
        }
        if(ctrl == AUDIO10_FU_CTRL_VOLUME)
        {
            spk_volume = (int16_t)tu_unaligned_read16(buf);
            spk_gain   = powf(10.f, (spk_volume / 256.f) / 20.f);
            return true;
        }
        return false;
    }

    bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req)
    {
        uint8_t const entity = TU_U16_HIGH(req->wIndex);
        uint8_t const ctrl   = TU_U16_HIGH(req->wValue);
        if(entity != UAC1_SPK_FEATURE_UNIT)
            return false;
        if(ctrl == AUDIO10_FU_CTRL_MUTE && req->bRequest == AUDIO10_CS_REQ_GET_CUR)
        {
            uint8_t m = spk_mute;
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &m, 1);
        }
        if(ctrl == AUDIO10_FU_CTRL_VOLUME)
        {
            int16_t v;
            switch(req->bRequest)
            {
                case AUDIO10_CS_REQ_GET_CUR: v = spk_volume; break;
                case AUDIO10_CS_REQ_GET_MIN: v = -60 * 256; break; // -60 dB
                case AUDIO10_CS_REQ_GET_MAX: v = 0; break;         // 0 dB
                case AUDIO10_CS_REQ_GET_RES: v = 256; break;       // 1 dB steps
                default: return false;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &v, sizeof(v));
        }
        return false;
    }
}

//------------- TinyUSB mass-storage callbacks (disk mode) -------------//

extern "C"
{
    void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
    {
        (void)lun;
        memcpy(vendor_id, "RODEO   ", 8);
        memcpy(product_id, "RAM disk        ", 16);
        memcpy(product_rev, "1.0 ", 4);
    }

    bool tud_msc_test_unit_ready_cb(uint8_t lun)
    {
        if(disk_ejected)
        {
            tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); // medium not present
            return false;
        }
        return true;
    }

    void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
    {
        (void)lun;
        *block_count = kSectors;
        *block_size  = kSectorSize;
    }

    bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
    {
        (void)lun;
        (void)power_condition;
        if(load_eject && !start)
        {
            // The computer ejected the disk: hand it back to the Seed.
            disk_ejected = true;
            leave_disk   = true;
        }
        return true;
    }

    int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
    {
        (void)lun;
        if(lba >= kSectors)
            return -1;
        memcpy(buffer, &ramdisk[lba * kSectorSize + offset], bufsize);
        return (int32_t)bufsize;
    }

    int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
    {
        (void)lun;
        if(lba >= kSectors)
            return -1;
        memcpy(&ramdisk[lba * kSectorSize + offset], buffer, bufsize);
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

static void PrintStats()
{
    static const char* const src_name[] = {"codec in", "loopback", "test tone"};
    char                     buf[512];
    snprintf(buf,
             sizeof(buf),
             "\r\nto Seed:   %s, %s, %lu bytes, %lu underruns, buffer %u..%u of %u\r\n"
             "from Seed: %s, source %s, %lu bytes, %lu overruns\r\n"
             "volume %d dB%s, up %lu ms\r\n"
             "IN packets %lu (%lu bytes, %u..%u each), IN fifo max %u of %u; audio callbacks %lu\r\n",
             spk_streaming ? "streaming" : "idle",
             spk_playing ? "playing" : "filling",
             (unsigned long)spk_bytes,
             (unsigned long)spk_underruns,
             (unsigned)(spk_level_min == 0xFFFF ? 0 : spk_level_min),
             (unsigned)spk_level_max,
             (unsigned)CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ,
             mic_streaming ? "streaming" : "idle",
             src_name[(int)mic_source],
             (unsigned long)mic_bytes,
             (unsigned long)mic_overruns,
             spk_volume / 256,
             spk_mute ? " (muted)" : "",
             (unsigned long)System::GetNow(),
             (unsigned long)tx_packets,
             (unsigned long)tx_bytes,
             (unsigned)(tx_pkt_min == 0xFFFF ? 0 : tx_pkt_min),
             (unsigned)tx_pkt_max,
             (unsigned)mic_fill_max,
             (unsigned)CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ,
             (unsigned long)callbacks);
    CdcPrint(buf);
    tx_pkt_min = 0xFFFF;
    tx_pkt_max = 0;
    mic_fill_max = 0;
    spk_level_min = 0xFFFF; // the range is per report
    spk_level_max = 0;
}

static void CdcTask()
{
    static bool was_connected = false;
    bool        connected     = tud_cdc_connected();
    if(connected && !was_connected)
    {
        if(usb_disk_mode)
            CdcPrint("\r\nRodeo USB device test, DISK MODE: the RAM disk is the computer's.\r\n"
                     "Eject it to hand it back, or n = leave disk mode now.\r\n");
        else
            CdcPrint("\r\nRodeo USB device test: TinyUSB " TUSB_VERSION_STRING
                     ", UAC1 2x2 48 kHz 24-bit + MIDI + serial.\r\n"
                     "s = stats; recording source: c = codec in, l = loopback, t = test tone;\r\n"
                     "d = disk mode; r = list the RAM disk; B = reboot into DFU (development)\r\n");
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
            case 's': PrintStats(); break;
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
                if(!usb_disk_mode && disk_mounted)
                {
                    DiskReport(report, sizeof(report));
                    CdcPrint(report);
                }
                break;
            case 'B':
                // Straight into the STM32's own DFU bootloader, for reflashing without
                // the buttons (development only)
                CdcPrint("rebooting into the DFU bootloader\r\n");
                System::Delay(50);
                ResetToDfu();
                break;
            case 'c':
                mic_source = MicSource::Codec;
                CdcPrint("source: codec in\r\n");
                break;
            case 'l':
                mic_source = MicSource::Loopback;
                CdcPrint("source: loopback\r\n");
                break;
            case 't':
                mic_source = MicSource::Tone;
                CdcPrint("source: test tone\r\n");
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
    hw.SetAudioBlockSize(kBlock);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    DiskInit();

    rodeo::UsbPortInit();
    tusb_rhport_init_t dev_init = {TUSB_ROLE_DEVICE, TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    hw.StartAudio(AudioCallback);

    uint32_t blink = 0;
    for(;;)
    {
        tud_task();
        CdcTask();
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

        uint32_t now = System::GetNow();
        uint32_t period = usb_disk_mode                    ? 1000
                          : spk_streaming || mic_streaming ? 60
                          : (tud_mounted() ? 250 : 500);
        if(now - blink >= period)
        {
            blink = now;
            static bool led = false;
            led             = !led;
            hw.SetLed(led);
        }
    }
}
