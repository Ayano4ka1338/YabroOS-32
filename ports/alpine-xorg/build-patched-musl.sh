#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
OUT="$ROOT/ports/alpine-xorg/patched-musl"
MUSL_VERSION="${MUSL_VERSION:-1.2.5}"
SRCROOT="$ROOT/musl-port/native-musl/src/musl-$MUSL_VERSION"
AR="${AR:-$(command -v ar)}"
RANLIB="${RANLIB:-$(command -v ranlib)}"
CC="${CC:-$(command -v gcc)}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

if [ ! -f "$SRCROOT/configure" ]; then
  "$ROOT/musl-port/native-musl/prepare-source.sh"
fi
[ -f "$SRCROOT/configure" ] || { echo "ERROR: musl source unavailable: $SRCROOT" >&2; exit 2; }
DYNLINK="$SRCROOT/ldso/dynlink.c"
grep -Fq 'static inline struct symdef find_sym2' "$DYNLINK" || {
  echo "ERROR: patched musl source is missing find_sym2" >&2
  exit 2
}
grep -Eq 'dso[[:space:]]*=[[:space:]]*use_deps[[:space:]]*\?[[:space:]]*\*deps\+\+[[:space:]]*:[[:space:]]*dso->next' "$DYNLINK" || {
  echo "ERROR: patched musl source is missing startup DSO traversal fix" >&2
  exit 2
}
grep -Fq 'YABROOS direct symtab fallback' "$DYNLINK" || {
  echo "ERROR: patched musl source is missing direct symtab fallback" >&2
  exit 2
}
grep -Fq 'if (!sym) sym = gnu_lookup(gh, ght, dso, s);' "$DYNLINK" || {
  echo "ERROR: patched musl source is missing GNU hash retry" >&2
  exit 2
}

rm -rf "$OUT" "$ROOT/musl-port/native-musl/src/shared-build"
mkdir -p "$OUT" "$ROOT/musl-port/native-musl/src/shared-build"
cd "$ROOT/musl-port/native-musl/src/shared-build"

echo "===> musl shared configure"
CC="$CC" CROSS_COMPILE="" AR="$AR" RANLIB="$RANLIB" \
  sh "$SRCROOT/configure" \
    --prefix=/ \
    --syslibdir=/lib \
    --enable-shared \
    --enable-wrapper=gcc \
    --target=x86_64-linux-musl

echo "===> musl shared build"
make -j"$JOBS" CROSS_COMPILE="" AR="$AR" RANLIB="$RANLIB"
echo "===> musl shared install"
make install DESTDIR="$OUT" CROSS_COMPILE="" AR="$AR" RANLIB="$RANLIB"

if [ -e "$OUT$OUT" ] || [ -d "$OUT/home" ] && [ -d "$OUT/home/$(basename "$(dirname "$OUT")")" ]; then
  echo "ERROR: malformed musl staging path detected under $OUT" >&2
  find "$OUT" -maxdepth 4 -type f | sort | head -80 >&2 || true
  exit 3
fi

if [ -e "$OUT/usr/lib/libc.so" ] || [ -e "$OUT/usr/lib/ld-musl-x86_64.so.1" ]; then
  echo "ERROR: musl runtime was installed under $OUT/usr/lib; shared prefix must be /" >&2
  exit 3
fi
[ -f "$OUT/lib/libc.so" ] || { echo "ERROR: shared libc.so was not installed at $OUT/lib/libc.so" >&2; exit 3; }

if [ -e "$OUT/lib/ld-musl-x86_64.so.1" ] || [ -L "$OUT/lib/ld-musl-x86_64.so.1" ]; then
  rm -f "$OUT/lib/ld-musl-x86_64.so.1"
fi
cp -f "$OUT/lib/libc.so" "$OUT/lib/ld-musl-x86_64.so.1"
cp -f "$OUT/lib/libc.so" "$OUT/lib/libc.musl-x86_64.so.1"
[ -f "$OUT/lib/ld-musl-x86_64.so.1" ] || { echo "ERROR: shared ld-musl-x86_64.so.1 was not materialized at $OUT/lib/ld-musl-x86_64.so.1" >&2; exit 3; }

file "$OUT/lib/libc.so" "$OUT/lib/ld-musl-x86_64.so.1"
readelf -h "$OUT/lib/ld-musl-x86_64.so.1" | grep -q 'Class:.*ELF64' || {
  echo "ERROR: patched musl loader is not ELF64" >&2; exit 4;
}
readelf -h "$OUT/lib/ld-musl-x86_64.so.1" | grep -q 'Machine:.*X86-64' || {
  echo "ERROR: patched musl loader is not x86-64" >&2; exit 4;
}

echo "PATCHED MUSL SHARED RUNTIME READY: $OUT"
