---
name: flash-rodeo
description: Build and flash Rev B firmware (rodeo-demo or an SDK example) onto the Rodeo over USB-C through the Daisy bootloader, then check it. Use whenever firmware under instruments/ or rodeo-sdk/examples/ needs to go onto the board, or the board stops showing up over USB.
---

# Flash the Rodeo

The bring-up Seed runs the Daisy bootloader (extdfu, `APP_TYPE=BOOT_SRAM` by default
in `rodeo.mk`). Flashing is: get it into the bootloader, `make program-dfu`, check.
Derek has the board on his desk; ask him for a button press only where this says to.

## 1. Build

```bash
make -C instruments/rodeo-demo/firmware          # or rodeo-sdk/examples/<name>
```

Fix warnings you introduced (an unused function after a removal, say) before flashing.

## 2. Into the bootloader (pick the first that works)

**a. Over MIDI (rodeo-demo, or any firmware with the SDK's host protocol).** The
protocol's dfu command, `F0 7D 52 44 02 09 F7`. mido leaves out F0/F7:

```bash
python3 - <<'PY'
import mido, os, sys
name = next(n for n in mido.get_output_names() if 'rodeo' in n.lower() or 'daisy' in n.lower())
with mido.open_output(name) as o:
    o.send(mido.Message('sysex', data=[0x7D, 0x52, 0x44, 0x02, 0x09]))
print('sent dfu to', name); sys.stdout.flush(); os._exit(0)
PY
```

The `os._exit(0)` matters: mido/rtmidi scripts otherwise hang on exit.

**b. Over serial.** Send `B` on the firmware's serial port (`/dev/cu.usbmodem*`):

```bash
P=$(ls /dev/cu.usbmodem* | head -1); python3 - "$P" <<'PY'
import os, sys
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK); os.write(fd, b'B'); os.close(fd)
PY
```

**c. By hand.** Ask Derek to tap RESET; the bootloader waits about 2 s at power-up, so
have `make program-dfu` ready to run the moment he says so.

The bootloader then waits indefinitely. Its DFU device is on the USB-C, alt name
`@Flash /0x90000000`. Wait for it rather than sleeping a guessed time (macOS has no
`timeout`):

```bash
for i in $(seq 1 30); do dfu-util -l 2>/dev/null | grep -q 0x90000000 && break; sleep 0.5; done
```

## 3. Flash

```bash
make -C instruments/rodeo-demo/firmware program-dfu
```

## 4. Check

- The MIDI port comes back (`python3 -c "import mido;print(mido.get_output_names())"`).
- rodeo-demo: `python3 instruments/rodeo-demo/firmware/tools/midi_protocol_test.py`
  must pass. It restores the preset, shape and detune it changes.
- Serial `s` prints the state, if there is one to look at.

## When the board does not show up

- **Seed LED heartbeat:** toggling every 500 ms (slow, ~1 Hz) means USB is *not* mounted; every 250 ms (fast) means mounted.
- **Slow heartbeat, Mac sees nothing on the USB-C** (seen after a night powered with the
  Mac asleep; parked as a TODO in `rodeo-sdk/todo.md`, do not fix it unasked): any reset
  brings it back. Ask Derek to replug the USB-C, or press RESET; BOOT+RESET then RESET
  worked when nothing else did.
- **ROM DFU** (micro-USB, hold BOOT and tap RESET) is only for reinstalling the
  bootloader or `BOOT_NONE` builds (`make program-boot`). With a computer on the USB-C
  the ROM's leave never starts the program, so Derek has to tap RESET afterwards. Do
  not ask for BOOT+RESET for an ordinary flash.
- Check what macOS sees: `system_profiler SPUSBDataType | grep -i -A6 'rodeo\|daisy\|dfu'`.
