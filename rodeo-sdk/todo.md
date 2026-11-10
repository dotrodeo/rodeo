# SDK todo

SDK work noticed and not started yet, newest first.

* **USB-C doesn't come back after the computer's USB goes idle** (seen
  2026-11-10, rodeo-demo on Rev B). The Rodeo stayed powered overnight; in the
  morning the Mac saw nothing on the USB-C, not even an attempt to connect, and
  the Seed LED showed the slow not-connected heartbeat. A reset (any reset) brought
  it back. Unplugging and replugging the cable while it runs is fine, so the
  suspect is the bus suspend when the Mac sleeps or the hub idles: the firmware
  doesn't recover from it. `usb_port.cpp` runs with VBUS sensing off (neither
  port has VBUS on its sense pin), so the device never sees a disconnect either.
  To do: reproduce (sleep the Mac with the Rodeo connected, wake it, watch the
  LED), then make the SDK's USB recover — handle suspend and resume, or notice
  a dead link and restart the core — so every firmware gets it.
