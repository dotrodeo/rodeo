// The sample library. See samples.h.
#include "samples.h"

#include <stdio.h>
#include <string.h>
#include "daisy_seed.h"
#include "fatfs.h"

using namespace daisy;

namespace
{
constexpr int kMaxSounds  = 64;
constexpr int kMaxSamples = 512;
// 48 MB of 16-bit samples: about 4 minutes of stereo at 48 kHz. The rest of the
// SDRAM holds the reverb and the file system.
constexpr size_t kPoolSamples = 24u * 1024 * 1024;
// One read per Step(). Small enough that the main loop gets back to USB within
// about 2 ms at the card's 1-bit rate.
constexpr uint32_t kChunk = 2048;

// SDMMC1's DMA reaches the SDRAM but not DTCM, where statics live under the
// bootloader: the file system, the open file and the read buffer go in the
// SDRAM, 32-byte aligned for the driver's cache maintenance.
SdmmcHandler   sdmmc;
FatFSInterface fsi DSY_SDRAM_BSS;
FIL            file DSY_SDRAM_BSS;
DIR            dir DSY_SDRAM_BSS;
FILINFO        info DSY_SDRAM_BSS;
alignas(32) uint8_t chunk[kChunk] DSY_SDRAM_BSS;
int16_t        pool[kPoolSamples] DSY_SDRAM_BSS;

// The tables are in the SDRAM too: DTCM also holds the stack. None of them is
// read before Scan() fills it in (the SDRAM isn't zeroed at start-up).
samples::Sound  sounds[kMaxSounds] DSY_SDRAM_BSS;
samples::Sample sample_list[kMaxSamples] DSY_SDRAM_BSS;
char            paths[kMaxSamples][64] DSY_SDRAM_BSS; // each sample's file, until it loads
int             n_sounds = 0, n_samples = 0;

// The loader: which sample, and how far into its file.
struct Loading
{
    int      sample = 0; // the next to load, or the one open
    bool     open   = false;
    uint32_t pos    = 0;  // the file offset of the next frame to load
    uint32_t end    = 0;  // ... and of the end of the data
    uint8_t  bits   = 0;  // 8, 16, 24, or 32 (float)
    uint8_t  in_channels = 0;
    uint16_t align  = 0;  // bytes a frame in the file
    uint32_t frames = 0;  // frames stored so far
} ld;

size_t   pool_used = 0;
int      loaded = 0, skipped = 0;
uint32_t bytes_read = 0, t_start = 0, t_done = 0;
bool     have_card = false;

bool IsWav(const char* name)
{
    size_t n = strlen(name);
    return n > 4 && (strcasecmp(name + n - 4, ".wav") == 0) && name[0] != '.';
}

void CopyName(char* to, const char* from, size_t n)
{
    strncpy(to, from, n - 1);
    to[n - 1] = 0;
    char* dot = strrchr(to, '.');
    if(dot && strcasecmp(dot, ".wav") == 0)
        *dot = 0;
}

// Names into a list, in order, case-insensitively. Short lists: insertion sort.
void SortNames(char (*names)[64], int n)
{
    char t[64];
    for(int i = 1; i < n; i++)
        for(int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--)
        {
            memcpy(t, names[j], 64);
            memcpy(names[j], names[j - 1], 64);
            memcpy(names[j - 1], t, 64);
        }
}

// The WAVs (or, with dirs, the folders) in a folder, sorted.
int List(const char* path, bool dirs, char (*names)[64], int max)
{
    int n = 0;
    if(f_opendir(&dir, path) != FR_OK)
        return 0;
    while(n < max && f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        bool is_dir = info.fattrib & AM_DIR;
        if(info.fattrib & (AM_HID | AM_SYS) || info.fname[0] == '.')
            continue;
        if(dirs ? !is_dir : (is_dir || !IsWav(info.fname)))
            continue;
        if(strlen(info.fname) >= 64)
            continue; // a path that long won't fit; rename the file
        strcpy(names[n++], info.fname);
    }
    f_closedir(&dir);
    SortNames(names, n);
    return n;
}

bool AddSample(const char* path)
{
    if(n_samples >= kMaxSamples || strlen(path) >= sizeof(paths[0]))
        return false;
    snprintf(paths[n_samples], sizeof(paths[0]), "%s", path);
    sample_list[n_samples] = {nullptr, 0, 0, 1.f};
    n_samples++;
    return true;
}

// Scratch for the scan: up to 64 names a folder.
char names[64][64] DSY_SDRAM_BSS;
char kit_names[kMaxSounds][64] DSY_SDRAM_BSS;

void Scan(const char* root)
{
    char path[160];
    snprintf(path, sizeof path, "%ssamples", root);
    int n = List(path, false, names, 64);
    for(int i = 0; i < n && n_sounds < kMaxSounds; i++)
    {
        char file_path[160];
        snprintf(file_path, sizeof file_path, "%.95s/%.63s", path, names[i]);
        samples::Sound& s = sounds[n_sounds];
        CopyName(s.name, names[i], sizeof s.name);
        s.kit   = false;
        s.first = (uint16_t)n_samples;
        if(!AddSample(file_path))
            break;
        s.count = 1;
        s.ready = false;
        n_sounds++;
    }

    snprintf(path, sizeof path, "%skits", root);
    int kits = List(path, true, kit_names, kMaxSounds);
    for(int k = 0; k < kits && n_sounds < kMaxSounds; k++)
    {
        char kit_path[160];
        snprintf(kit_path, sizeof kit_path, "%.95s/%.63s", path, kit_names[k]);
        int files = List(kit_path, false, names, 64);
        if(files == 0)
            continue;
        samples::Sound& s = sounds[n_sounds];
        CopyName(s.name, kit_names[k], sizeof s.name);
        s.kit   = true;
        s.first = (uint16_t)n_samples;
        s.count = 0;
        for(int i = 0; i < files; i++)
        {
            char file_path[160];
            snprintf(file_path, sizeof file_path, "%.95s/%.63s", kit_path, names[i]);
            if(!AddSample(file_path))
                break;
            s.count++;
        }
        s.ready = false;
        n_sounds++;
    }
}

uint32_t Le(const uint8_t* p, int n)
{
    uint32_t v = 0;
    for(int i = n - 1; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

bool Read(void* to, UINT n)
{
    UINT got = 0;
    return f_read(&file, to, n, &got) == FR_OK && got == n;
}

// Open a WAV and find its format and data. Reads through `chunk`, which DMA can
// reach. False for anything this loader can't play.
bool OpenWav(const char* path)
{
    if(f_open(&file, path, FA_READ) != FR_OK)
        return false;
    ld.open = true;
    uint8_t* h = chunk;
    if(!Read(h, 12) || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0)
        return false;
    bool     have_fmt = false;
    uint32_t rate     = 0;
    for(;;)
    {
        if(!Read(h, 8))
            return false;
        uint32_t size = Le(h + 4, 4);
        if(memcmp(h, "fmt ", 4) == 0)
        {
            if(size < 16 || size > 64 || !Read(h, size))
                return false;
            uint16_t format = (uint16_t)Le(h, 2);
            if(format == 0xFFFE && size >= 26)
                format = (uint16_t)Le(h + 24, 2); // WAVE_FORMAT_EXTENSIBLE's subformat
            ld.in_channels = (uint8_t)Le(h + 2, 2);
            rate           = Le(h + 4, 4);
            ld.align       = (uint16_t)Le(h + 12, 2);
            ld.bits        = (uint8_t)Le(h + 14, 2);
            bool pcm       = format == 1 && (ld.bits == 8 || ld.bits == 16 || ld.bits == 24);
            bool flt       = format == 3 && ld.bits == 32;
            if(!(pcm || flt) || ld.in_channels == 0 || rate == 0
               || ld.align != ld.in_channels * ld.bits / 8)
                return false;
            if(size & 1)
                f_lseek(&file, f_tell(&file) + 1);
            have_fmt = true;
        }
        else if(memcmp(h, "data", 4) == 0)
        {
            if(!have_fmt)
                return false;
            // A data size past the end of the file (some recorders write a
            // placeholder) means "the rest of the file".
            uint32_t rest = f_size(&file) - f_tell(&file);
            uint32_t len  = size < rest ? size : rest;
            ld.pos        = f_tell(&file);
            ld.end        = ld.pos + len - len % ld.align;
            break;
        }
        else
        {
            if(f_lseek(&file, f_tell(&file) + size + (size & 1)) != FR_OK)
                return false;
        }
    }

    uint8_t  channels = ld.in_channels >= 2 ? 2 : 1;
    uint32_t frames   = (ld.end - ld.pos) / ld.align;
    if(frames == 0 || pool_used + (size_t)frames * channels > kPoolSamples)
        return false; // empty, or no room left
    samples::Sample& s = sample_list[ld.sample];
    s.data     = &pool[pool_used];
    s.frames   = 0; // grows as it loads; voices only see it once its sound is ready
    s.channels = channels;
    s.speed    = (float)rate / 48000.f;
    ld.frames  = frames;
    return true;
}

// One input sample (of `bits`) at p, as 16-bit.
int16_t To16(const uint8_t* p, uint8_t bits)
{
    switch(bits)
    {
        case 8: return (int16_t)((p[0] - 128) << 8);
        case 16: return (int16_t)(p[0] | (p[1] << 8));
        case 24:
        {
            // Round to 16 bits rather than truncate (a truncated 24-bit file
            // carries a small DC offset into every voice).
            int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            v         = (v + 128) >> 8;
            return (int16_t)(v > 32767 ? 32767 : v);
        }
        default:
        {
            float f;
            memcpy(&f, p, 4);
            f = f > 1.f ? 1.f : (f < -1.f ? -1.f : f);
            return (int16_t)(f * 32767.f);
        }
    }
}

void Finish(bool ok)
{
    if(ld.open)
        f_close(&file);
    ld.open = false;
    samples::Sample& s = sample_list[ld.sample];
    if(ok)
    {
        pool_used += (size_t)s.frames * s.channels;
        loaded++;
    }
    else
    {
        s = {nullptr, 0, 0, 1.f};
        skipped++;
    }
    ld.sample++;
    // A sound is ready once its last sample is done.
    for(int i = 0; i < n_sounds; i++)
        if(!sounds[i].ready && ld.sample >= sounds[i].first + sounds[i].count)
            sounds[i].ready = true;
}
} // namespace

namespace samples
{
bool Mount()
{
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1; // D1-D3's pins went elsewhere on Rev B
    sdmmc.Init(sd_cfg);
    FatFSInterface::Config fsi_cfg;
    fsi_cfg.media = FatFSInterface::Config::MEDIA_SD;
    fsi.Init(fsi_cfg);
    return Remount();
}

bool Step()
{
    if(ld.sample >= n_samples)
    {
        if(!t_done)
            t_done = System::GetNow();
        return false;
    }
    if(!ld.open)
    {
        if(!OpenWav(paths[ld.sample]))
            Finish(false);
        return true;
    }

    // Read whole sectors from a sector boundary, so FatFS hands them to the
    // SDMMC's DMA straight into `chunk`, aligned: its DMA needs a word-aligned
    // address, and WAV data seldom starts on a sector. Then take the whole frames
    // that arrived; one cut by the end of the chunk is read again next time.
    Sample&  s     = sample_list[ld.sample];
    uint32_t start = ld.pos & ~511u;
    uint32_t want  = ld.end - start < kChunk ? ld.end - start : kChunk;
    UINT     got   = 0;
    if(f_lseek(&file, start) != FR_OK || f_read(&file, chunk, want, &got) != FR_OK || got != want)
    {
        Finish(false);
        return true;
    }
    bytes_read += got;
    uint32_t frames = (start + got - ld.pos) / ld.align;

    const uint8_t* in  = chunk + (ld.pos - start);
    int16_t*       out = const_cast<int16_t*>(s.data) + (size_t)s.frames * s.channels;
    for(uint32_t f = 0; f < frames; f++, in += ld.align)
        for(int c = 0; c < s.channels; c++)
            *out++ = To16(in + c * (ld.bits / 8), ld.bits);
    s.frames += frames;
    ld.pos += frames * ld.align;
    if(ld.pos >= ld.end)
        Finish(true);
    return true;
}

void Unmount()
{
    if(ld.open)
        f_close(&file);
    ld.open   = false;
    ld.sample = n_samples; // nothing more to load
    if(!t_done)
        t_done = System::GetNow();
    if(have_card)
        f_mount(nullptr, fsi.GetSDPath(), 0);
    have_card = false;
}

bool Remount()
{
    // Sounds first, so the audio callback finds none while the tables refill
    n_sounds  = 0;
    n_samples = 0;
    ld        = Loading();
    pool_used = 0;
    loaded = skipped = 0;
    bytes_read       = 0;
    t_start          = System::GetNow();
    t_done           = 0;
    have_card = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
    if(have_card)
        Scan(fsi.GetSDPath());
    if(n_samples == 0)
        t_done = t_start;
    return have_card;
}

uint8_t Drive()
{
    return (uint8_t)(fsi.GetSDPath()[0] - '0');
}

int Sounds()
{
    return n_sounds;
}

const Sound& GetSound(int i)
{
    return sounds[i];
}

const Sample* ForNote(int sound, int note)
{
    if(sound < 0 || sound >= n_sounds || !sounds[sound].ready)
        return nullptr;
    const Sound& s = sounds[sound];
    int          i = s.kit ? note - kKitFirstNote : 0;
    if(i < 0 || i >= s.count)
        return nullptr;
    const Sample* x = &sample_list[s.first + i];
    return x->data && x->frames ? x : nullptr;
}

Stats GetStats()
{
    Stats st;
    st.sounds     = n_sounds;
    st.samples    = n_samples;
    st.loaded     = loaded;
    st.skipped    = skipped;
    st.bytes_read = bytes_read;
    st.bytes_used = (uint32_t)(pool_used * sizeof(int16_t));
    st.card       = have_card;
    st.loading    = ld.sample < n_samples;
    st.load_ms    = (t_done ? t_done : System::GetNow()) - t_start;
    return st;
}
} // namespace samples
