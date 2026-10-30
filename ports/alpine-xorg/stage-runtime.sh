#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 ALPINE_ROOTFS DESTINATION" >&2
  exit 2
fi
ROOTFS=$(cd "$1" && pwd -P)
DEST=$2
mkdir -p "$DEST"

resolve_file() {
  local src=$1 resolved
  if [ -L "$src" ]; then
    resolved=$(readlink -f -- "$src") || {
      echo "ERROR: dangling runtime symlink: $src" >&2
      return 1
    }
  else
    resolved=$src
  fi
  [ -f "$resolved" ] || {
    echo "ERROR: runtime file does not resolve to a regular file: $src" >&2
    return 1
  }
  printf '%s\n' "$resolved"
}

copy_resolved() {
  local src=$1 rel=$2 resolved
  resolved=$(resolve_file "$src")
  mkdir -p "$DEST/$(dirname "$rel")"
  cp -f -- "$resolved" "$DEST/$rel"
}

copy_tree_resolved() {
  local srcdir=$1 destdir=$2 src rel resolved
  [ -d "$srcdir" ] || return 0
  mkdir -p "$DEST/$destdir"

  while IFS= read -r -d '' src; do
    rel=${src#"$srcdir"/}
    resolved=$(resolve_file "$src") || return 1
    mkdir -p "$DEST/$destdir/$(dirname "$rel")"
    cp -f -- "$resolved" "$DEST/$destdir/$rel"
  done < <(find -L "$srcdir" -type f -print0 | sort -z)
}

[ -f "$ROOTFS/usr/libexec/Xorg" ] || {
  echo "ERROR: Alpine Xorg server missing: /usr/libexec/Xorg" >&2
  exit 8
}
copy_resolved "$ROOTFS/usr/libexec/Xorg" usr/libexec/Xorg
if [ -e "$ROOTFS/usr/bin/Xorg" ] || [ -L "$ROOTFS/usr/bin/Xorg" ]; then
  copy_resolved "$ROOTFS/usr/bin/Xorg" usr/bin/Xorg
else
  copy_resolved "$ROOTFS/usr/libexec/Xorg" usr/bin/Xorg
fi
if [ -e "$ROOTFS/usr/bin/X" ] || [ -L "$ROOTFS/usr/bin/X" ]; then
  copy_resolved "$ROOTFS/usr/bin/X" usr/bin/X
fi

copy_tree_resolved "$ROOTFS/usr/lib/xorg" usr/lib/xorg

for libdir in lib usr/lib; do
  [ -d "$ROOTFS/$libdir" ] || continue
  shopt -s nullglob
  for src in "$ROOTFS/$libdir"/*; do
    [ -f "$src" ] || [ -L "$src" ] || continue
    base=${src##*/}
    case "$base" in
      *.so|*.so.*) ;;
      *) continue ;;
    esac
    case "$base" in
      ld-musl-x86_64.so.1|libc.musl-x86_64.so.1) continue ;;
    esac
    copy_resolved "$src" "$libdir/$base"
  done
  shopt -u nullglob
done

for runtime_bin in xkbcomp xterm xeyes xclock cwm feh fastfetch nano rnano nasm ndisasm; do
  [ -e "$ROOTFS/usr/bin/$runtime_bin" ] || [ -L "$ROOTFS/usr/bin/$runtime_bin" ] || {
    echo "ERROR: Alpine runtime executable missing: /usr/bin/$runtime_bin" >&2
    exit 7
  }
  copy_resolved "$ROOTFS/usr/bin/$runtime_bin" "usr/bin/$runtime_bin"
done
copy_tree_resolved "$ROOTFS/usr/share/X11/xkb" usr/share/X11/xkb
copy_tree_resolved "$ROOTFS/usr/share/X11/xorg.conf.d" usr/share/X11/xorg.conf.d

for app_defaults in \
  usr/share/X11/app-defaults/XEyes usr/lib/X11/app-defaults/XEyes \
  usr/share/X11/app-defaults/XClock usr/lib/X11/app-defaults/XClock \
  usr/share/X11/app-defaults/XClock-color usr/lib/X11/app-defaults/XClock-color; do
  if [ -f "$ROOTFS/$app_defaults" ]; then
    copy_resolved "$ROOTFS/$app_defaults" "$app_defaults"
  fi
done

copy_tree_resolved "$ROOTFS/etc/terminfo" etc/terminfo
copy_tree_resolved "$ROOTFS/usr/share/terminfo" usr/share/terminfo
for term_entry in xterm xterm-256color; do
  source_entry="$DEST/etc/terminfo/x/$term_entry"
  [ -s "$source_entry" ] || {
    echo "ERROR: Alpine ncurses terminfo entry missing after staging: /etc/terminfo/x/$term_entry" >&2
    exit 8
  }
  for terminfo_root in usr/share/terminfo lib/terminfo lib/term; do
    target_entry="$DEST/$terminfo_root/x/$term_entry"
    mkdir -p "$(dirname "$target_entry")"

    cp -f "$source_entry" "$target_entry"
  done
done

copy_tree_resolved "$ROOTFS/usr/share/nano" usr/share/nano
if [ -e "$ROOTFS/etc/nanorc" ] || [ -L "$ROOTFS/etc/nanorc" ]; then
  copy_resolved "$ROOTFS/etc/nanorc" etc/nanorc
fi
copy_tree_resolved "$ROOTFS/usr/share/fonts" usr/share/fonts

copy_tree_resolved "$ROOTFS/etc/fonts" etc/fonts
copy_tree_resolved "$ROOTFS/usr/share/fontconfig" usr/share/fontconfig
copy_tree_resolved "$ROOTFS/usr/share/hwdata" usr/share/hwdata
copy_tree_resolved "$ROOTFS/usr/share/fastfetch" usr/share/fastfetch

copy_tree_resolved "$ROOTFS/usr/lib/imlib2" usr/lib/imlib2

if [ -e "$ROOTFS/usr/share/misc/magic.mgc" ] || [ -L "$ROOTFS/usr/share/misc/magic.mgc" ]; then
  copy_resolved "$ROOTFS/usr/share/misc/magic.mgc" usr/share/misc/magic.mgc
fi
if [ -f "$ROOTFS/etc/os-release" ] || [ -L "$ROOTFS/etc/os-release" ]; then
  copy_resolved "$ROOTFS/etc/os-release" etc/os-release
fi

for rel in \
  usr/libexec/Xorg usr/bin/Xorg usr/bin/xkbcomp usr/bin/xterm usr/bin/xeyes usr/bin/xclock usr/bin/cwm usr/bin/feh usr/bin/fastfetch \
  usr/bin/nano usr/bin/rnano usr/bin/nasm usr/bin/ndisasm etc/os-release etc/nanorc \
  usr/share/nano/asm.nanorc usr/share/nano/c.nanorc \
  usr/lib/xorg/modules/input/evdev_drv.so \
  usr/lib/xorg/modules/drivers/fbdev_drv.so \
  usr/lib/libevdev.so.2 usr/lib/libmtdev.so.1 usr/lib/libudev.so.1 \
  usr/lib/libX11.so.6 usr/lib/libX11-xcb.so.1 usr/lib/libXi.so.6 usr/lib/libxcb.so.1 \
  usr/lib/libXau.so.6 usr/lib/libXdmcp.so.6 usr/lib/libxkbfile.so.1 \
  usr/lib/libICE.so.6 usr/lib/libXaw.so.7 usr/lib/libXext.so.6 \
  usr/lib/libXft.so.2 usr/lib/libXrandr.so.2 usr/lib/libXmu.so.6 usr/lib/libXpm.so.4 \
  usr/lib/libXt.so.6 usr/lib/libXrender.so.1 \
  usr/share/X11/app-defaults/XEyes usr/lib/X11/app-defaults/XEyes \
  usr/share/X11/app-defaults/XClock usr/lib/X11/app-defaults/XClock \
  usr/share/X11/app-defaults/XClock-color usr/lib/X11/app-defaults/XClock-color \
  usr/lib/libxcb-damage.so.0 \
  usr/lib/libxcb-present.so.0 usr/lib/libxcb-xfixes.so.0 usr/lib/libfontconfig.so.1 \
  usr/lib/libfreetype.so.6 usr/lib/libbz2.so.1 usr/lib/libncursesw.so.6 \
  etc/fonts/fonts.conf; do
  [ -f "$DEST/$rel" ] || {
    echo "ERROR: required canonical Xorg runtime file missing after staging: /$rel" >&2
    exit 8
  }
done
for term_entry in xterm xterm-256color; do
  for terminfo_root in etc/terminfo usr/share/terminfo lib/terminfo lib/term; do
    [ -s "$DEST/$terminfo_root/x/$term_entry" ] || {
      echo "ERROR: terminfo entry not staged: /$terminfo_root/x/$term_entry" >&2
      exit 8
    }
  done
done
if find "$DEST" -type l -print -quit | grep -q .; then
  echo "ERROR: stage contains a symlink; FAT runtime paths must be regular files." >&2
  exit 8
fi
printf '%s\n' 'Xorg runtime staging: canonical paths and shared-library SONAME aliases verified'
