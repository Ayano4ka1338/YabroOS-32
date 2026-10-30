#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
RF="$ROOT/ports/alpine-xorg/alpine/rootfs"
XORG="$RF/usr/libexec/Xorg"

[ -f "$XORG" ] || { echo "ERROR: missing Xorg: $XORG" >&2; exit 2; }
command -v readelf >/dev/null || { echo "ERROR: readelf is required to verify Xorg dependencies" >&2; exit 2; }

SEEN_ELFS='|'
verify_needed_tree() {
  local elf="$1" needed dep resolved candidate
  case "$SEEN_ELFS" in *"|$elf|"*) return 0 ;; esac
  SEEN_ELFS="${SEEN_ELFS}${elf}|"

  while IFS= read -r needed; do
    [ -n "$needed" ] || continue
    [ "$needed" = "libc.musl-x86_64.so.1" ] && continue

    resolved=''
    for candidate in "$RF/lib/$needed" "$RF/usr/lib/$needed"; do
      if [ -e "$candidate" ]; then resolved="$candidate"; break; fi
    done
    if [ -z "$resolved" ]; then
      echo "ERROR: missing Xorg runtime library '$needed' required by '${elf#"$RF"/}'" >&2
      return 1
    fi
    verify_needed_tree "$resolved"
  done < <(readelf -d "$elf" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
}

for so in libpixman-1.so.0 libpciaccess.so.0 libnettle.so.8 libXau.so.6 libXdmcp.so.6 \
          libXfont2.so.2 libxshmfence.so.1 libudev.so.1 libdrm.so.2 libxcvt.so.0; do
  if [ ! -e "$RF/usr/lib/$so" ] && [ ! -e "$RF/lib/$so" ]; then
    echo "ERROR: required Xorg runtime library missing: $so" >&2
    exit 3
  fi
done

for rel in usr/libexec/Xorg usr/lib/xorg/modules/drivers/fbdev_drv.so usr/lib/xorg/modules/input/evdev_drv.so; do
  [ -f "$RF/$rel" ] || { echo "ERROR: required Xorg runtime component missing: $rel" >&2; exit 4; }
  echo "== ELF: /$rel =="
  readelf -d "$RF/$rel" | grep 'NEEDED' || true
  verify_needed_tree "$RF/$rel"
done

echo 'Xorg dependency check: server and active fbdev/evdev drivers have complete runtime dependencies.'
