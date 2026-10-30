#!/bin/sh

set -eu
OUT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
SPECS_SRC="$OUT/lib/musl-gcc.specs"
SPECS_TMP="${TMPDIR:-/tmp}/yabro-musl-gcc-specs.$$"
trap 'rm -f "$SPECS_TMP"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if [ ! -f "$SPECS_SRC" ]; then
    echo "musl-gcc: missing specs file: $SPECS_SRC" >&2
    exit 1
fi
python3 - "$SPECS_SRC" "$OUT" "$SPECS_TMP" <<'PY'
import re
import sys
from pathlib import Path
src, out, dest = sys.argv[1:]
text = Path(src).read_text()
text = re.sub(r'/[^\s"\\]*musl-port/native-musl/out(?=[/}])', lambda _m: out, text)
Path(dest).write_text(text)
PY
"${REALGCC:-/bin/gcc}" "$@" -specs "$SPECS_TMP"
