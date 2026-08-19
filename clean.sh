#!/usr/bin/env bash
set -euo pipefail
rm -rf build iso_root yabroos-32.iso debug.log
rm -f userland_selected.elf
rm -f userland_*.o userland_*.elf
rm -f MUSL0.ELF MUSL1.ELF MUSL2.ELF
rm -f test.txt
rm -rf musl-port/*.o musl-port/lib/*.o
