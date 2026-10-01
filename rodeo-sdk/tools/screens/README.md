# Host screen previews

`host_display.h` renders an instrument's real OLED screens on your laptop —
the same draw calls and the same fonts the board uses, with nothing flashed.

It works because libDaisy's `OledDisplay` is a template over its driver and
everything under it is plain C++; only the SSD130x transport needs an STM32. A
stub driver holding a framebuffer is therefore pixel-exact, which is the whole
point. **A preview that is not the real drawing code is a preview that can lie
to you.**

## Using it

An instrument writes its own `preview.cpp`: a rig that drives its UI, and a list
of screens to capture. See `rodeo-002/firmware/tools/screens/preview.cpp` for a
worked example — it drives a real `Ui` with real encoder input, so the screens
are whatever the instrument would actually show, *including anything the
interaction gets wrong*. That is what makes it a review tool and not a mock-up.

```
make          # render to build/*.pbm and print them
make ascii    # print only
```

Build with **C++17**, not the firmware's gnu++14: libDaisy's `FixedCapStr`
wants `<string_view>`. Nothing else differs.

## What it has caught

* A 12-pixel offset on every screen, from reading the framebuffer through a
  `reinterpret_cast` past `OledDisplay`'s vtable. Hence `Framebuffer()`.
* Whether a 12-character preset name clears the voice-mode field in the header,
  which is a question about pixels and was faster to answer than to reason about.
