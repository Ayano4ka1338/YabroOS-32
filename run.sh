#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"
ISO="$ROOT/yabroos-32.iso"
if [ ! -f "$ISO" ]; then
    echo "run.sh: missing ISO: $ISO" >&2
    echo "Build it first with: ./build.sh" >&2
    exit 2
fi
MEMORY="${YABRO_QEMU_MEMORY:-384M}"
exec qemu-system-x86_64 -cpu core2duo -cdrom "$ISO" -m "$MEMORY" -vga std -rtc base=localtime,clock=host \
    -debugcon "file:$ROOT/debug.log" -global isa-debugcon.iobase=0xe9
