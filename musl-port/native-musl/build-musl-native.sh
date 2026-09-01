#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="${MUSL_VERSION:-1.2.5}"
SHA="${MUSL_SHA256:-a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"

OUT="$ROOT/musl-port/native-musl/out"
SRC="$ROOT/musl-port/native-musl/src"
TAR="$ROOT/musl-port/sources/musl-$VER.tar.gz"

mkdir -p "$OUT" "$SRC" "$(dirname "$TAR")"

[ -f "$TAR" ] || curl -fL "https://musl.libc.org/releases/musl-$VER.tar.gz" -o "$TAR"
echo "$SHA  $TAR" | sha256sum -c -

rm -rf "$SRC/musl-$VER" "$SRC/build"
tar -xf "$TAR" -C "$SRC"
cd "$SRC/musl-$VER"

python3 - <<'PY'
from pathlib import Path
p = Path("ldso/dynlink.c")
s = p.read_text()

if "static void rebuild_global_scope(void)" not in s:
    s = s.replace(
        "static void revert_syms(struct dso *old_tail)",
        """static void rebuild_global_scope(void)
{
\tstruct dso *p;
\tsyms_tail = 0;
\tfor (p=head; p; p=p->next) p->syms_next = 0;
\tif (!head) return;
\tsyms_tail = head;
\tfor (p=head->next; p; p=p->next) {
\t\tsyms_tail->syms_next = p;
\t\tsyms_tail = p;
\t}
}

static void revert_syms(struct dso *old_tail)""",
        1,
    )

s = s.replace(
    "\tfor (; dso; dso=use_deps ? *deps++ : dso->syms_next) {",
    "\tfor (; dso; dso = use_deps ? *deps++ : (dso == head ? dso->next : dso->syms_next)) {",
    1,
)

s = s.replace(
    """\t\tif ((ght = dso->ghashtab)) {
\t\t\tsym = gnu_lookup_filtered(gh, ght, dso, s, gho, ghm);
\t\t} else {""",
    """\t\tif ((ght = dso->ghashtab)) {
\t\t\tsym = gnu_lookup_filtered(gh, ght, dso, s, gho, ghm);
\t\t\tif (!sym) sym = gnu_lookup(gh, ght, dso, s);
\t\t} else {""",
    1,
)

needle = "\tfor (struct dso *p=head; p; p=p->next)\n\t\tadd_syms(p);\n"
if needle in s and "rebuild_global_scope();" not in s:
    s = s.replace(needle, needle + "\trebuild_global_scope();\n", 1)

p.write_text(s)
PY

python3 - <<'PY'
from pathlib import Path
p = Path("tools/musl-gcc.specs.sh")
s = p.read_text()
s = s.replace(
    '%{!shared: $libdir/Scrt1.o} $libdir/crti.o crtbeginS.o%s',
    '%{shared:;static-pie:$libdir/rcrt1.o; :$libdir/Scrt1.o} $libdir/crti.o crtbeginS.o%s',
)
s = s.replace(
    '-dynamic-linker $ldso -nostdlib %{shared:-shared} %{static:-static} %{rdynamic:-export-dynamic}',
    '-dynamic-linker $ldso -nostdlib %{shared:-shared} %{static:-static} %{static-pie:-static -pie --no-dynamic-linker} %{rdynamic:-export-dynamic}',
)
p.write_text(s)
PY

mkdir -p "$SRC/build"
cd "$SRC/build"

CC="${CC:-$(command -v gcc)}"
AR="${AR:-$(command -v ar)}"
RANLIB="${RANLIB:-$(command -v ranlib)}"

CC="$CC" AR="$AR" RANLIB="$RANLIB" \
  ../musl-$VER/configure \
    --prefix="$OUT" \
    --disable-shared \
    --enable-wrapper=gcc \
    --target=x86_64-linux-musl

make -j"$JOBS"
make install

echo "Done: $OUT"
