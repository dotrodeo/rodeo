// MIDI in and out through the Rev B MIDI jack board (USART1, 31250 baud).
//
// Connect the board's OUT to its IN with a 3.5 mm TRS cable and it is its own
// test: every 100 ms it sends a numbered note on, note off and controller
// change, and every second a short SysEx, and checks each comes back in order.
// The Seed's USB serial prints, every second:
//
//   sent 120 notes, 120 back in order, 0 missing (offs 120, cc 120)
//   sysex 12 sent, 12 back   0 from outside
//
// "missing" counts numbers skipped in what came back. The tip/ring pair is
// opto-isolated on IN, so a cable that swaps tip and ring (a Type B cable or a
// wrong adapter) shows as nothing back at all.
//
// Without the loop cable, OUT can drive a synth (short notes climbing through
// all 128 on channel 1, the mod wheel on channel 16; the note number is the
// count) and IN prints whatever a keyboard sends:
//
//   in: note on  ch 1  60 vel 100
//
// 'B' over the serial reboots into DFU.
#include "daisy_seed.h"
#include "board.h"
#include "board_daisy.h"
#include "dfu.h"

using namespace daisy;

constexpr uint8_t kNoteChannel = 0;  // channel 1
constexpr uint8_t kCcChannel   = 15; // channel 16
constexpr uint8_t kSysex[]     = {0xF0, 0x7D, 'R', 'O', 'D', 'E', 'O', 0x00, 0xF7};
constexpr int     kSysexSeq    = 7; // the byte that carries the count

DaisySeed            hw;
MidiUartHandler      midi;
static volatile bool to_bootloader = false;

static void UsbRx(uint8_t* buf, uint32_t* len)
{
    for(uint32_t i = 0; i < *len; i++)
        if(buf[i] == 'B')
            to_bootloader = true;
}

struct Counts
{
    uint32_t sent = 0, back = 0, missing = 0;
    uint8_t  next = 0; // the number expected next
    // A number came back: in order, or after a gap (counted as missing).
    void Got(uint8_t n)
    {
        back++;
        missing += static_cast<uint8_t>(n - next) & 0x7F;
        next = (n + 1) & 0x7F;
    }
};

static Counts notes, offs, ccs, sysex;
static uint32_t other = 0;

static void Send(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t msg[3] = {a, b, c};
    midi.SendMessage(msg, 3);
}

static void Handle(MidiEvent& e)
{
    const bool note_ch = e.channel == kNoteChannel;
    if(e.type == NoteOn && note_ch && e.data[1] == 100)
        return notes.Got(e.data[0]);
    if(e.type == NoteOff && note_ch)
        return offs.Got(e.data[0]);
    if(e.type == ControlChange && e.channel == kCcChannel && e.data[0] == 1)
        return ccs.Got(e.data[1]);
    if(e.type == SystemCommon && e.sc_type == SystemExclusive && e.sysex_message_len >= 6
       && e.sysex_data[0] == 0x7D && e.sysex_data[1] == 'R')
        return sysex.Got(e.sysex_data[kSysexSeq - 1]);

    // Anything else is from outside: print it.
    other++;
    switch(e.type)
    {
        case NoteOn:
            hw.PrintLine("in: note on  ch %d  %d vel %d", e.channel + 1, e.data[0], e.data[1]);
            break;
        case NoteOff: hw.PrintLine("in: note off ch %d  %d", e.channel + 1, e.data[0]); break;
        case ControlChange:
            hw.PrintLine("in: cc       ch %d  %d = %d", e.channel + 1, e.data[0], e.data[1]);
            break;
        case PitchBend:
            hw.PrintLine("in: bend     ch %d  %d", e.channel + 1,
                         ((e.data[1] << 7) | e.data[0]) - 8192);
            break;
        case SystemRealTime: other--; break; // clock and active sensing: too many to print
        default: hw.PrintLine("in: type %d ch %d", static_cast<int>(e.type), e.channel + 1);
    }
}

int main(void)
{
    hw.Init();
    hw.StartLog(false);
    hw.usb_handle.SetReceiveCallback(UsbRx, UsbHandle::FS_INTERNAL);

    MidiUartHandler::Config cfg;
    cfg.transport_config.periph = UartHandler::Config::Peripheral::USART_1;
    cfg.transport_config.rx     = board::ToDaisy(board::pin::kMidiRx);
    cfg.transport_config.tx     = board::ToDaisy(board::pin::kMidiTx);
    midi.Init(cfg);
    midi.StartReceive();

    hw.PrintLine("midi: patch OUT to IN, or plug in a keyboard and a synth");
    uint32_t last_send = 0, last_print = 0;
    uint8_t  n = 0;
    for(;;)
    {
        if(to_bootloader)
            ResetToDfu();

        midi.Listen();
        while(midi.HasEvents())
        {
            MidiEvent e = midi.PopEvent();
            Handle(e);
        }

        uint32_t now = System::GetNow();
        if(now - last_send >= 100)
        {
            last_send = now;
            Send(0x90 | kNoteChannel, n, 100);
            Send(0x80 | kNoteChannel, n, 0);
            Send(0xB0 | kCcChannel, 1, n);
            notes.sent++;
            offs.sent++;
            ccs.sent++;
            if(n % 10 == 0)
            {
                uint8_t s[sizeof(kSysex)];
                memcpy(s, kSysex, sizeof(s));
                s[kSysexSeq] = sysex.sent & 0x7F;
                midi.SendMessage(s, sizeof(s));
                sysex.sent++;
            }
            n = (n + 1) & 0x7F;
            hw.SetLed(notes.back > 0 && notes.missing == 0 && (n & 4));
        }
        if(now - last_print >= 1000)
        {
            last_print = now;
            hw.PrintLine("sent %lu notes, %lu back in order, %lu missing (offs %lu, cc %lu)",
                         (unsigned long)notes.sent, (unsigned long)notes.back,
                         (unsigned long)notes.missing, (unsigned long)offs.back,
                         (unsigned long)ccs.back);
            hw.PrintLine("  sysex %lu sent, %lu back   %lu from outside",
                         (unsigned long)sysex.sent,
                         (unsigned long)sysex.back, (unsigned long)other);
        }
    }
}
