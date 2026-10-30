#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd -P)"
cd "$ROOT"

BUILD_DIR="$ROOT/build"
VENDOR_DIR="$ROOT/vendor"
PUREDOOM="$VENDOR_DIR/PureDOOM.h"
mkdir -p "$BUILD_DIR" "$VENDOR_DIR"

PUREDOOM_URL="${PUREDOOM_URL:-https://raw.githubusercontent.com/Daivuk/PureDOOM/master/PureDOOM.h}"

if [ ! -f "$PUREDOOM" ]; then
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 1 "$PUREDOOM_URL" -o "$PUREDOOM.part" && mv "$PUREDOOM.part" "$PUREDOOM"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$PUREDOOM.part" "$PUREDOOM_URL" && mv "$PUREDOOM.part" "$PUREDOOM"
    else
        echo "ERROR: curl or wget is required to fetch PureDOOM.h" >&2
        exit 2
    fi
fi

FREEDOOM_VERSION="${FREEDOOM_VERSION:-0.13.0}"
FREEDOOM_WAD="$ROOT/freedoom2.wad"
FREEDOOM_ARCHIVE="$BUILD_DIR/freedoom-${FREEDOOM_VERSION}.zip"
FREEDOOM_URL="${FREEDOOM_URL:-https://github.com/freedoom/freedoom/releases/download/v${FREEDOOM_VERSION}/freedoom-${FREEDOOM_VERSION}.zip}"
if [ ! -s "$FREEDOOM_WAD" ]; then
    if [ ! -s "$FREEDOOM_ARCHIVE" ]; then
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 --retry-delay 1 "$FREEDOOM_URL" -o "$FREEDOOM_ARCHIVE.part"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$FREEDOOM_ARCHIVE.part" "$FREEDOOM_URL"
        else
            echo "ERROR: curl or wget is required to fetch Freedoom Phase 2 data" >&2
            exit 2
        fi
        mv "$FREEDOOM_ARCHIVE.part" "$FREEDOOM_ARCHIVE"
    fi
    python3 - "$FREEDOOM_ARCHIVE" "$FREEDOOM_WAD.part" <<'PYWAD'
import os
import shutil
import sys
import zipfile

archive, destination = sys.argv[1:]
try:
    with zipfile.ZipFile(archive) as zf:
        matches = [name for name in zf.namelist()
                   if name.rstrip('/').split('/')[-1].lower() == 'freedoom2.wad']
        if not matches:
            raise SystemExit('Freedoom archive does not contain freedoom2.wad')
        info = zf.getinfo(matches[0])
        if info.file_size < 1_000_000:
            raise SystemExit('Freedoom Phase 2 WAD is unexpectedly small')
        with zf.open(matches[0]) as source, open(destination, 'wb') as target:
            shutil.copyfileobj(source, target)
    with open(destination, 'rb') as wad:
        if wad.read(4) != b'IWAD':
            raise SystemExit('Freedoom Phase 2 file is not a valid IWAD')
except (OSError, zipfile.BadZipFile, RuntimeError) as exc:
    raise SystemExit('Could not extract Freedoom Phase 2 IWAD: ' + str(exc))
PYWAD
    mv "$FREEDOOM_WAD.part" "$FREEDOOM_WAD"
fi

if [ "$(dd if="$FREEDOOM_WAD" bs=1 count=4 2>/dev/null)" != "IWAD" ]; then
    echo "ERROR: $FREEDOOM_WAD is not a valid IWAD file" >&2
    exit 2
fi

CC="${CC:-gcc}"
LD="${LD:-ld}"

CFLAGS=(
    -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs
    -fno-builtin -fno-pie -fno-stack-protector -fno-strict-aliasing
    -O2 -Wno-unused-function
    "-I$ROOT/include"
    "-I$VENDOR_DIR"
)

"$CC" "${CFLAGS[@]}" -c src/doom_yabroos.c -o "$BUILD_DIR/doom_yabroos.o"

"$LD" -m elf_x86_64 -static -nostdlib \
    -Tdoom.ld \
    "$BUILD_DIR/doom_yabroos.o" \
    -o "$BUILD_DIR/DOOM.ELF"

cp "$BUILD_DIR/DOOM.ELF" "$ROOT/DOOM.ELF"
echo "DOOM OK: $ROOT/DOOM.ELF"
echo "Freedoom IWAD ready: $FREEDOOM_WAD"
echo "Runtime IWAD path: /freedoom2.wad"
