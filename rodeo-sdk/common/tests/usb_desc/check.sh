#!/bin/bash
# Every firmware's USB descriptors, byte for byte, against golden/: a change to
# common/usb_desc.c, usb_uac1.h, a firmware's tusb_config.h or TinyUSB that
# changes what a computer sees fails here. Needs the tinyusb submodule.
#
#   ./check.sh            compare (make usb-desc)
#   ./check.sh --update   rewrite golden/ (make usb-desc-update): when the
#                         change is meant, review the diff and commit it
set -euo pipefail
cd "$(dirname "$0")"
SDK=../../..
ROOT=$SDK/..
TINYUSB=$SDK/tinyusb/src
CC=${CC:-clang}
[ -f "$TINYUSB/tusb.h" ] || { echo "usb-desc: needs the tinyusb submodule (git submodule update --init rodeo-sdk/tinyusb)"; exit 1; }

# name  source dir  disk mode?  extra flags
FIRMWARES=(
  "host         $SDK/examples/host/src                0"
  "usb_midi_cdc $SDK/examples/usb_midi_cdc/src        0"
  "usb_storage  $SDK/examples/usb_storage/src         1"
  "usb_device   $SDK/examples/usb_device/src          1"
  "usb_audio    $SDK/examples/usb_audio/src           0"
  "usb_audio_24 $SDK/examples/usb_audio/src           0 -DAUDIO_BITS=24"
  "rodeo_demo   $ROOT/instruments/rodeo-demo/firmware/src 1"
)

update=0; [ "${1:-}" = --update ] && update=1
out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
fail=0
for f in "${FIRMWARES[@]}"; do
  read -r name src disk extra <<<"$f"
  flags=(-std=gnu11 -Wall -I. -I"$src" -I"$SDK/common" -I"$TINYUSB")
  [ "$disk" = 1 ] && flags+=(-DHAS_DISK_MODE)
  [ -n "${extra:-}" ] && flags+=($extra)
  srcs=(dump.c "$SDK/common/usb_desc.c")
  [ -f "$src/usb_descriptors.c" ] && srcs+=("$src/usb_descriptors.c")
  "$CC" "${flags[@]}" "${srcs[@]}" -o "$out/$name"
  "$out/$name" >"$out/$name.txt"
  if [ $update = 1 ]; then
    cp "$out/$name.txt" "golden/$name.txt"
  elif diff -u "golden/$name.txt" "$out/$name.txt" >"$out/$name.diff"; then
    echo "usb-desc: $name unchanged"
  else
    echo "usb-desc: $name CHANGED:"; cat "$out/$name.diff"; fail=1
  fi
done
[ $update = 1 ] && echo "usb-desc: golden/ rewritten; review it with git diff"
if [ $fail = 1 ]; then
  echo "usb-desc: what a computer sees has changed. If that's meant (and a"
  echo "changed layout has its own product ID), run make usb-desc-update and commit golden/."
  exit 1
fi
