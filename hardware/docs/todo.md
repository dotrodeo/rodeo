# Hardware todo

Boards and hardware work not started yet, newest first. Changes to boards that
already exist go in [`revisions.md`](revisions.md) instead.

* **Single button** (kind 2, specified 2026-10-26): [`rodeo-button-1`](../rodeo-button-1/README.md).
  Schematic and layout: first pass done 2026-10-26, for Derek's review; open: red or RGB LED.
* **Rotary switch** (kind 4, specified 2026-10-26): [`rodeo-rotary-switch-1`](../rodeo-rotary-switch-1/README.md).
  Alps SRBV141404 (4 positions, M9, end stops); its footprint and model are in `lib/`. It needs its own placement (the switch is too big for the single pot's).
* **Satellite dev board** (2026-10-19). A plain satellite for people building their
  own: the chain's IN and OUT connectors, the CH32V003, the kind-14 ID resistor
  (140 kΩ, contract §1.6), and the spare GPIO on headers. It lets users
  prototype satellites for parts we don't make a board for. Decided 2026-10-26: it
  reports **kind 14, custom board**, which runs its own firmware and declares its
  slots in its DISCOVER reply. To decide: what its GPIO headers carry (3V3, GND,
  the aux 5 V), and whether its firmware starts from the stock image with a user
  hook or from scratch.
* **¼" jack board**, with the CV decision.
* **USB OTG_HS breakout** for the SDK's USB work.
