#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
ELF="$ROOT/build/musl-abi/yabro-musl-tests.elf"


INITRD_ROOT="${INITRD_ROOT:-$ROOT/initrd}"
DEST="${MUSL_INITRD_DEST:-/yabro-musl-tests.elf}"

if [ ! -f "$ELF" ]; then
	echo "[musl] missing $ELF; build it first" >&2
	exit 1
fi

mkdir -p "$INITRD_ROOT$(dirname "$DEST")"
cp "$ELF" "$INITRD_ROOT$DEST"
echo "[musl] installed $DEST"
