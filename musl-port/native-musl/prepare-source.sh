#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
VERSION="${MUSL_VERSION:-1.2.5}"
EXPECTED="${MUSL_SHA256:-$(grep -E "[[:space:]]musl-$VERSION\.tar\.gz$" "$ROOT/musl-port/MUSL_SOURCE.sha256" | cut -d ' ' -f 1 | head -n 1)}"
CACHE="$ROOT/musl-port/sources"
SRC="$ROOT/musl-port/native-musl/src"
ARCHIVE="$CACHE/musl-$VERSION.tar.gz"

mkdir -p "$CACHE"
if [ ! -s "$ARCHIVE" ]; then
  [ "${MUSL_ALLOW_NETWORK:-1}" = 1 ] || { echo "ERROR: cached musl source missing: $ARCHIVE" >&2; exit 2; }
  command -v curl >/dev/null 2>&1 || { echo 'ERROR: curl is required to download musl.' >&2; exit 2; }
  TMP="$ARCHIVE.part.$$"
  trap 'rm -f "$TMP"' EXIT
  curl -fL --retry 5 --retry-all-errors --connect-timeout 20 --max-time 180 \
    -o "$TMP" "https://musl.libc.org/releases/musl-$VERSION.tar.gz"
  mv "$TMP" "$ARCHIVE"
  trap - EXIT
fi
[ -n "$EXPECTED" ] || { echo "ERROR: no SHA256 entry for musl-$VERSION.tar.gz" >&2; exit 3; }
ACTUAL="$(sha256sum "$ARCHIVE" | cut -d ' ' -f 1)"
[ "$ACTUAL" = "$EXPECTED" ] || { echo "ERROR: musl SHA256 mismatch: $ACTUAL" >&2; exit 3; }

if [ "${1:-}" = --fresh ] || [ ! -f "$SRC/musl-$VERSION/configure" ] || \
   ! grep -Fq 'YABROOS direct symtab fallback' "$SRC/musl-$VERSION/ldso/dynlink.c" 2>/dev/null; then
  rm -rf "$SRC"
  mkdir -p "$SRC"
  tar -xzf "$ARCHIVE" -C "$SRC"
  python3 "$ROOT/musl-port/native-musl/patch-source.py" "$SRC/musl-$VERSION/ldso/dynlink.c"
fi
printf 'musl %s source ready: %s\n' "$VERSION" "$SRC/musl-$VERSION"
