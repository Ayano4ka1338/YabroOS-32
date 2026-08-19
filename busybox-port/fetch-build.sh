#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VER=1.37.0
URL="https://busybox.net/downloads/busybox-${VER}.tar.bz2"
OUT="$ROOT/out"
SRC="$ROOT/src"
MUSL="$ROOT/../musl-port/upstream-musl-1.2.6/sysroot"
mkdir -p "$SRC" "$OUT"
if [ ! -f "$SRC/busybox-${VER}.tar.bz2" ]; then
  echo "==> fetching BusyBox ${VER}"
  curl -fL "$URL" -o "$SRC/busybox-${VER}.tar.bz2"
fi
if [ ! -d "$SRC/busybox-${VER}" ]; then
  tar -xjf "$SRC/busybox-${VER}.tar.bz2" -C "$SRC"
fi
cd "$SRC/busybox-${VER}"
make distclean >/dev/null 2>&1 || true
cp "$ROOT/busybox.config" .config
make olddefconfig
make -j"${JOBS:-2}" \
  CC="${CC:-gcc}" \
  CFLAGS="-O2 -fno-tree-vectorize -fno-tree-slp-vectorize -mno-red-zone -fno-stack-protector" \
  LDFLAGS="-static -L$MUSL/lib" \
  CONFIG_PREFIX="$OUT/rootfs"
cp busybox "$OUT/busybox"
file "$OUT/busybox"
echo "BUILD OK: $OUT/busybox"
