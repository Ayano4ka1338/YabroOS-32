#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD_DIR="$ROOT/build"
mkdir -p "$BUILD_DIR"
WAD="freedoom2.wad"

if [ ! -f "$WAD" ]; then
  curl -L \
    https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip \
    -o /tmp/freedoom.zip

  unzip -j /tmp/freedoom.zip freedoom-0.13.0/freedoom2.wad -d "$(dirname "$WAD")"
fi
if [ ! -f "$BUILD_DIR/PureDOOM.h" ]; then
	if command -v curl >/dev/null 2>&1; then
		curl -L --fail --silent --show-error "$PUREDOOM_URL" -o "$BUILD_DIR/PureDOOM.h"
	elif command -v wget >/dev/null 2>&1; then
		wget -qO "$BUILD_DIR/PureDOOM.h" "$PUREDOOM_URL"
	else
		echo "ERROR: curl or wget is required to fetch PureDOOM.h" >&2
		exit 2
	fi
fi

CC="${CC:-gcc}"
LD="${LD:-ld}"

CFLAGS=(
	-m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs
	-fno-builtin -fno-pie -fno-stack-protector -fno-strict-aliasing
	-O2 -Wno-unused-function
	"-I$ROOT/include"
	"-I$BUILD_DIR"
)

"$CC" "${CFLAGS[@]}" -c src/doom_yabroos.c -o "$BUILD_DIR/doom_yabroos.o"

"$LD" -m elf_x86_64 -static -nostdlib \
	-Tdoom.ld \
	"$BUILD_DIR/doom_yabroos.o" \
	-o "$BUILD_DIR/DOOM.ELF"

cp "$BUILD_DIR/DOOM.ELF" "$ROOT/DOOM.ELF"
echo "DOOM OK: $ROOT/DOOM.ELF"
