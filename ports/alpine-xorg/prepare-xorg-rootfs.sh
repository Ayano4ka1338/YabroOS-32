#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
"$ROOT/ports/alpine-xorg/fetch-runtime.sh"
FB="$ROOT/ports/alpine-xorg/alpine/rootfs/usr/lib/xorg/modules/drivers/fbdev_drv.so"
[ -s "$FB" ] || { echo "ERROR: Alpine fbdev driver not installed: $FB" >&2; exit 20; }
echo "XORG ROOTFS READY: $FB"
