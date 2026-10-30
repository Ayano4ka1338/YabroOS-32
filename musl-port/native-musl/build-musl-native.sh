#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
OUT="$ROOT/musl-port/native-musl/out"
SRC="$ROOT/musl-port/native-musl/src"
VERSION="${MUSL_VERSION:-1.2.5}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
AR="${AR:-$(command -v ar)}"
RANLIB="${RANLIB:-$(command -v ranlib)}"
CC="${CC:-$(command -v gcc)}"

rm -rf "$OUT"
"$ROOT/musl-port/native-musl/prepare-source.sh" --fresh
mkdir -p "$OUT" "$SRC/build"
cd "$SRC/build"

[ -x "$AR" ] && [ -x "$RANLIB" ] && [ -x "$CC" ] || {
  echo 'ERROR: host gcc, ar and ranlib are required to build musl.' >&2; exit 4;
}
CC="$CC" CROSS_COMPILE='' AR="$AR" RANLIB="$RANLIB" \
  "../musl-$VERSION/configure" --prefix="$OUT" --disable-shared \
  --enable-wrapper=gcc --target=x86_64-linux-musl
make -j"$JOBS" CROSS_COMPILE='' AR="$AR" RANLIB="$RANLIB"
make install CROSS_COMPILE='' AR="$AR" RANLIB="$RANLIB"
install -m 755 "$ROOT/musl-port/native-musl/musl-gcc-wrapper.sh" "$OUT/bin/musl-gcc"

TEST_C="$OUT/.selftest.c"
TEST_BIN="$OUT/.selftest"
trap 'rm -f "$TEST_C" "$TEST_BIN"' EXIT
printf '#include <stdio.h>\nint main(void) { puts("musl-gcc-ok"); return 0; }\n' > "$TEST_C"
"$OUT/bin/musl-gcc" -static -O2 "$TEST_C" -o "$TEST_BIN"
"$TEST_BIN" | grep -qx musl-gcc-ok
printf 'Native musl sysroot ready: %s\n' "$OUT"
