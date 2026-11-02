// Disk mode: the SD card as a USB drive, so the sample library can be changed
// from the computer without taking the card out.
//
// The model is the SDK's usb_storage example (bring-up 09): the computer and the
// Rodeo never have the card at the same time. Going in, the Rodeo stops loading,
// unmounts the card and re-enumerates as "Rodeo Demo (disk mode)", serial + USB
// storage; USB audio and MIDI are gone until it comes back, but the synth keeps
// playing from the TRS MIDI jack, with the samples it had. Ejecting the drive on
// the computer hands the card back: the Rodeo re-enumerates as itself and loads
// the library again. If the computer goes away instead (the cable pulled), it
// comes back on its own after a few seconds; nothing can be writing then.
#pragma once

namespace disk
{
/** Hand the card to the computer. False (and nothing changes) if there is no
 *  card to hand over. */
bool Enter();

/** Whether the computer has the card. */
bool Active();

/** From the main loop. True once, when disk mode has just ended and the card is
 *  the Rodeo's again (remounted, its sounds listed, ready for samples::Step()). */
bool Task();

/** Leave now, ejected or not (the serial port's 'n', for development: the
 *  computer will say the drive wasn't ejected properly). */
void Leave();
} // namespace disk
