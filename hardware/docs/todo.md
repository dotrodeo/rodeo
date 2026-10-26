# Hardware todo

Boards and hardware work not started yet, newest first. Changes to boards that
already exist go in [`revisions.md`](revisions.md) instead.

* **Satellite dev board** (2026-10-19). A plain satellite for people building their
  own: the chain's IN and OUT connectors, the CH32V003, an ID resistor (a footprint,
  or a kind set aside for custom boards), and the spare GPIO on headers. It lets
  users prototype satellites for parts we don't make a board for. To decide: which
  kind number it reports (and whether the user can change it), what its GPIO headers
  carry (3V3, GND, the aux 5 V), and whether firmware for it is a stock image with a
  user hook or the user's own.
* **¼" jack board**, with the CV decision.
* **USB OTG_HS breakout** for the SDK's USB work.
