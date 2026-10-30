#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd -P)"

rm -rf \
  "$ROOT/build" "$ROOT/iso_root" \
  "$ROOT/musl-port/native-musl/out" "$ROOT/musl-port/native-musl/src" \
  "$ROOT/ports/alpine-xorg/patched-musl" \
  "$ROOT/ports/ports-doom-real/build" \
  "$ROOT/ports/tcc/build" "$ROOT/ports/tcc/rootfs" \
  "$ROOT/ports/alpine-xorg/alpine/rootfs" \
  "$ROOT/musl-port/realmusl4-source"
rm -f \
  "$ROOT"/yabroos-*.iso "$ROOT/.yabroos-32.iso.tmp" \
  "$ROOT/userland_selected.elf" "$ROOT"/userland_*.o "$ROOT"/userland_*.elf \
  "$ROOT/ports/ports-doom-real/DOOM.ELF" \
  "$ROOT"/MUSL0.ELF "$ROOT"/MUSL1.ELF "$ROOT"/MUSL2.ELF "$ROOT/test.txt" \
  "$ROOT/musl-port"/*.o
find "$ROOT" -type f \( -name '*.o' -o -name '*.lo' \) \
  -not -path "$ROOT/limine-bin/*" -delete
rm -f "$ROOT/debug.log"
printf '%s\n' 'Done: binaries removed.'
