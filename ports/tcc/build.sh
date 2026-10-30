#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
PORT="$ROOT/ports/tcc"
CACHE="$PORT/.cache"
WORK="$PORT/build"
STAGE="$PORT/rootfs"
MUSL_OUT="$ROOT/musl-port/native-musl/out"
MUSL_CC="${MUSL_CC:-$MUSL_OUT/bin/musl-gcc}"
TCC_VERSION="${TCC_VERSION:-0.9.27}"
TCC_ARCHIVE="$CACHE/tcc-$TCC_VERSION.source"
TCC_URL="${TCC_SOURCE_URL:-https://download-mirror.savannah.gnu.org/releases/tinycc/tcc-$TCC_VERSION.tar.bz2}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

mkdir -p "$CACHE"
[ -x "$MUSL_CC" ] || {
    echo "ERROR: native musl compiler is missing: $MUSL_CC" >&2
    echo "Run the Native musl ABI foundation stage first." >&2
    exit 2
}
command -v make >/dev/null 2>&1 || { echo 'ERROR: make is required to build TCC.' >&2; exit 2; }
command -v readelf >/dev/null 2>&1 || { echo 'ERROR: readelf is required to validate TCC.' >&2; exit 2; }

fetch_tcc() {
    local target="$1" tmp="$1.part.$$" url
    local urls=()
    if [ -n "${TCC_SOURCE_URL:-}" ]; then
        urls=("$TCC_SOURCE_URL")
    else
        urls=(
            "$TCC_URL"
            "https://repo.or.cz/tinycc.git/snapshot/release_0_9_27.tar.gz"
            "https://github.com/Tiny-C-Compiler/tinycc-mirror-repository/archive/refs/tags/release_0_9_27.tar.gz"
            "https://github.com/Tiny-C-Compiler/tinycc-mirror-repository/archive/refs/tags/0.9.27.tar.gz"
        )
    fi
    for url in "${urls[@]}"; do
        rm -f "$tmp"
        echo "[TCC] source: $url"
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 2 --retry-all-errors --connect-timeout 20 --max-time 240 -o "$tmp" "$url" || true
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$tmp" "$url" || true
        else
            echo 'ERROR: curl or wget is required to download TCC.' >&2
            return 1
        fi
        if [ -s "$tmp" ] && tar -tf "$tmp" >/dev/null 2>&1; then
            mv -f "$tmp" "$target"
            return 0
        fi
        rm -f "$tmp"
    done
    echo 'ERROR: unable to download a valid Tiny C Compiler source archive.' >&2
    return 1
}

if [ ! -s "$TCC_ARCHIVE" ] || ! tar -tf "$TCC_ARCHIVE" >/dev/null 2>&1; then
    rm -f "$TCC_ARCHIVE"
    echo "===> Downloading Tiny C Compiler $TCC_VERSION"
    fetch_tcc "$TCC_ARCHIVE"
fi

rm -rf "$WORK" "$STAGE"
mkdir -p "$WORK/source" "$WORK/dest" "$STAGE/usr/bin" "$STAGE/usr/libexec" "$STAGE/usr/lib" "$STAGE/usr/include"
tar -xf "$TCC_ARCHIVE" -C "$WORK/source"
TCC_SRC="$(find "$WORK/source" -mindepth 1 -maxdepth 3 -type f -name configure -print -quit | xargs -r dirname)"
[ -n "$TCC_SRC" ] && [ -f "$TCC_SRC/Makefile" -o -f "$TCC_SRC/configure" ] || {
    echo 'ERROR: TCC source archive has no configure script.' >&2
    exit 3
}

cd "$TCC_SRC"
echo "===> Configure TCC $TCC_VERSION for x86_64-musl"
./configure \
    --prefix=/usr \
    --bindir=/usr/bin \
    --libdir=/usr/lib \
    --tccdir=/usr/lib/tcc \
    --includedir=/usr/lib/tcc/include \
    --mandir=/usr/share/man \
    --docdir=/usr/share/doc/tcc \
    --cpu=x86_64 \
    --triplet=x86_64-linux-musl \
    --cc="$MUSL_CC" \
    --extra-cflags='-O2 -fno-stack-protector' \
    --extra-ldflags=-static \
    --sysincludepaths=/usr/lib/tcc/include:/usr/include \
    --libpaths=/usr/lib \
    --crtprefix=/usr/lib \
    --elfinterp=/lib/ld-musl-x86_64.so.1 \
    --enable-static \
    --config-musl

make -j"$JOBS" CC="$MUSL_CC" AR="${AR:-$(command -v ar)}"
make install DESTDIR="$WORK/dest" CC="$MUSL_CC" AR="${AR:-$(command -v ar)}"

UPSTREAM_TCC="$WORK/dest/usr/bin/tcc"
[ -s "$UPSTREAM_TCC" ] || { echo 'ERROR: TCC build did not install /usr/bin/tcc.' >&2; exit 4; }
if readelf -l "$UPSTREAM_TCC" | grep -q 'INTERP'; then
    echo 'ERROR: TCC itself is dynamically linked; expected a static musl ELF.' >&2
    exit 4
fi
readelf -h "$UPSTREAM_TCC" | grep -q 'Class:.*ELF64' || { echo 'ERROR: TCC is not ELF64.' >&2; exit 4; }
readelf -h "$UPSTREAM_TCC" | grep -q 'Machine:.*X86-64' || { echo 'ERROR: TCC is not x86-64.' >&2; exit 4; }

install -m 755 "$UPSTREAM_TCC" "$STAGE/usr/libexec/tcc.real"
if [ -d "$WORK/dest/usr/lib/tcc" ]; then
    cp -a "$WORK/dest/usr/lib/tcc/." "$STAGE/usr/lib/tcc/"
fi
[ -s "$STAGE/usr/lib/tcc/libtcc1.a" ] || {
    echo 'ERROR: TCC runtime library libtcc1.a was not installed.' >&2
    exit 5
}

cp -a "$MUSL_OUT/include/." "$STAGE/usr/include/"
for f in "$MUSL_OUT"/lib/*.a "$MUSL_OUT"/lib/*.o; do
    [ -f "$f" ] || continue
    cp -f "$f" "$STAGE/usr/lib/"
done
[ -s "$STAGE/usr/lib/libc.a" ] && [ -s "$STAGE/usr/lib/crt1.o" ] || {
    echo 'ERROR: static musl sysroot is missing libc.a or crt1.o.' >&2
    exit 5
}

"$MUSL_CC" -static -Os -s -fno-stack-protector \
    -o "$STAGE/usr/bin/tcc" "$PORT/src/tcc-wrapper.c"
install -m 755 "$STAGE/usr/bin/tcc" "$STAGE/usr/bin/cc"

"$UPSTREAM_TCC" -v >/dev/null 2>&1
"$UPSTREAM_TCC" -B"$STAGE/usr/lib/tcc" -I"$STAGE/usr/include" \
    -c "$PORT/examples/hello.c" -o "$WORK/hello.o"
[ -s "$WORK/hello.o" ] || { echo 'ERROR: TCC could not compile the test source.' >&2; exit 6; }
for wrapper in "$STAGE/usr/bin/tcc" "$STAGE/usr/bin/cc"; do
    readelf -h "$wrapper" | grep -q 'Class:.*ELF64' || {
        echo "ERROR: compiler wrapper is not ELF64: $wrapper" >&2; exit 6;
    }
    readelf -h "$wrapper" | grep -q 'Machine:.*X86-64' || {
        echo "ERROR: compiler wrapper is not x86-64: $wrapper" >&2; exit 6;
    }
    if readelf -l "$wrapper" | grep -q 'INTERP'; then
        echo "ERROR: compiler wrapper is not static: $wrapper" >&2; exit 6;
    fi
done

printf 'TCC %s ready: %s\n' "$TCC_VERSION" "$STAGE/usr/bin/tcc"
printf 'TCC static sysroot: %s/usr/include and %s/usr/lib\n' "$STAGE" "$STAGE"
