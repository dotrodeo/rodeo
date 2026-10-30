// The internal microSD card on the Rev B motherboard, in 1-bit mode.
//
// SDMMC1 runs on D0, CMD and CLK only (board::kSdD0/kSdCmd/kSdClk); D1-D3's
// pins went to the satellite chain and the second codec's reset. libDaisy's
// SDMMC driver and FatFS do the rest, with the bus width set to 1 bit.
//
// On start, and again on 'R' over the Seed's USB serial, it:
//
//   1. mounts the card and prints its size and free space,
//   2. lists the root folder,
//   3. writes a 4 MiB test file and reads it back, timing both,
//   4. checks every word read matches what was written, and deletes the file.
//
// and prints PASS or FAIL. The rates show what 1-bit costs: the hardware notes
// estimate 3-5 MB/s through FatFS at 50 MHz. The Seed's LED blinks fast after a
// pass and slowly after a failure. 'B' reboots into DFU.
//
// The card must be FAT16 or FAT32. libDaisy builds FatFS without exFAT, which
// is how 64 GB and larger cards come formatted, so reformat those as FAT32.
#include "daisy_seed.h"
#include "fatfs.h"
#include "dfu.h"

using namespace daisy;

constexpr char     kTestFile[] = "rodeo_sd_test.bin";
constexpr uint32_t kChunk      = 32 * 1024;
constexpr uint32_t kFileBytes  = 4 * 1024 * 1024;

DaisySeed      hw;
SdmmcHandler   sdmmc;
DIR            dir;
FILINFO        info;

// SDMMC1's DMA reaches AXI SRAM and the SDRAM, not DTCM (the stack, and statics
// under the bootloader) or SRAM1. FatFS reads sectors into the file system's and
// the file's own buffers, so those go in the SDRAM with the data. 32-byte
// aligned for the driver's cache maintenance, since the SDRAM is cached.
FatFSInterface fsi DSY_SDRAM_BSS;
FIL            file DSY_SDRAM_BSS;
alignas(32) static uint32_t buf[kChunk / 4] DSY_SDRAM_BSS;

static volatile bool to_bootloader = false, run_again = false;

static void UsbRx(uint8_t* b, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
    {
        if(b[i] == 'B')
            to_bootloader = true;
        if(b[i] == 'R' || b[i] == 'r')
            run_again = true;
    }
}

// Each word depends on where it sits in the file, so a block read back from
// the wrong place fails the check, not just a corrupted one.
static uint32_t Pattern(uint32_t word) { return (word * 2654435761u) ^ 0xA5C3E1F7u; }

static uint32_t KBps(uint32_t bytes, uint32_t us)
{
    return us ? static_cast<uint32_t>(static_cast<uint64_t>(bytes) * 1000u / us) : 0;
}

static bool Fail(const char* what, FRESULT r)
{
    hw.PrintLine("  %s failed: FRESULT %d", what, static_cast<int>(r));
    hw.PrintLine("FAIL");
    return false;
}

static bool RunTest()
{
    FATFS&      fs   = fsi.GetSDFileSystem();
    const char* root = fsi.GetSDPath();

    hw.PrintLine("sd_card: SDMMC1, 1-bit");
    FRESULT r = f_mount(&fs, root, 1); // 1: mount now, so a missing card shows here
    if(r != FR_OK)
        return Fail("mount (card in? FAT formatted?)", r);

    DWORD  free_clusters;
    FATFS* fsp;
    if((r = f_getfree(root, &free_clusters, &fsp)) != FR_OK)
        return Fail("getfree", r);
    const uint32_t sectors_per_mb = 2048; // 512-byte sectors
    hw.PrintLine("  card %lu MB, %lu MB free",
                 static_cast<unsigned long>((fsp->n_fatent - 2) * fsp->csize / sectors_per_mb),
                 static_cast<unsigned long>(free_clusters * fsp->csize / sectors_per_mb));

    hw.PrintLine("  root folder:");
    if((r = f_opendir(&dir, root)) != FR_OK)
        return Fail("opendir", r);
    int shown = 0;
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        if(shown++ < 12)
            hw.PrintLine("    %s%s  %lu", info.fname, (info.fattrib & AM_DIR) ? "/" : "",
                         static_cast<unsigned long>(info.fsize));
    }
    f_closedir(&dir);
    if(shown > 12)
        hw.PrintLine("    ... %d more", shown - 12);
    if(shown == 0)
        hw.PrintLine("    (empty)");

    // Write.
    if((r = f_open(&file, kTestFile, FA_CREATE_ALWAYS | FA_WRITE)) != FR_OK)
        return Fail("open for write", r);
    uint32_t t0 = System::GetUs();
    for(uint32_t off = 0; off < kFileBytes; off += kChunk)
    {
        for(uint32_t w = 0; w < kChunk / 4; w++)
            buf[w] = Pattern(off / 4 + w);
        UINT n;
        r = f_write(&file, buf, kChunk, &n);
        if(r != FR_OK || n != kChunk)
        {
            f_close(&file);
            return Fail("write", r);
        }
    }
    if((r = f_close(&file)) != FR_OK) // flushes, so it counts in the time
        return Fail("close after write", r);
    const uint32_t write_us = System::GetUs() - t0;

    // Read back and check. The check runs inside the timed loop, but it is a
    // few ms per MiB against the card's hundreds.
    if((r = f_open(&file, kTestFile, FA_READ)) != FR_OK)
        return Fail("open for read", r);
    uint32_t bad = 0, first_bad = 0;
    t0           = System::GetUs();
    for(uint32_t off = 0; off < kFileBytes; off += kChunk)
    {
        UINT n;
        r = f_read(&file, buf, kChunk, &n);
        if(r != FR_OK || n != kChunk)
        {
            f_close(&file);
            return Fail("read", r);
        }
        for(uint32_t w = 0; w < kChunk / 4; w++)
            if(buf[w] != Pattern(off / 4 + w) && bad++ == 0)
                first_bad = off + w * 4;
    }
    const uint32_t read_us = System::GetUs() - t0;
    f_close(&file);
    f_unlink(kTestFile);

    hw.PrintLine("  wrote %lu KiB in %lu ms: %lu KB/s",
                 static_cast<unsigned long>(kFileBytes / 1024),
                 static_cast<unsigned long>(write_us / 1000),
                 static_cast<unsigned long>(KBps(kFileBytes, write_us)));
    hw.PrintLine("  read  %lu KiB in %lu ms: %lu KB/s",
                 static_cast<unsigned long>(kFileBytes / 1024),
                 static_cast<unsigned long>(read_us / 1000),
                 static_cast<unsigned long>(KBps(kFileBytes, read_us)));
    if(bad)
    {
        hw.PrintLine("  %lu words wrong, first at byte %lu", static_cast<unsigned long>(bad),
                     static_cast<unsigned long>(first_bad));
        hw.PrintLine("FAIL");
        return false;
    }
    hw.PrintLine("  every word read back matched");
    hw.PrintLine("PASS");
    return true;
}

int main(void)
{
    hw.Init();
    hw.StartLog(true); // wait for a terminal, so the result is seen
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults(); // 50 MHz
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
    sdmmc.Init(sd_cfg);

    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
    fsi.Init(fsi_cfg);

    bool pass = RunTest();
    for(;;)
    {
        if(run_again)
        {
            run_again = false;
            hw.PrintLine("");
            f_mount(nullptr, fsi.GetSDPath(), 0); // so a swapped card is mounted afresh
            pass = RunTest();
        }
        if(to_bootloader)
            ResetToDfu();
        hw.SetLed((System::GetNow() / (pass ? 100 : 1000)) % 2);
    }
}
