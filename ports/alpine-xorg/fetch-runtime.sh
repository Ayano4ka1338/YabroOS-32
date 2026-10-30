#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
printf '[XORG] canonical project root: %s\n' "$ROOT"
ALP="$ROOT/ports/alpine-xorg/alpine"
ROOTFS="$ALP/rootfs"
CACHE="$ALP/packages"
WORK="$ALP/.fetch-runtime.$$"
PACKAGE_ROOTFS="$WORK/rootfs"
ALPINE_BRANCH="${ALPINE_BRANCH:-v3.23}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
ALPINE_MIRROR="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"

mkdir -p "$CACHE" "$ROOTFS" "$PACKAGE_ROOTFS"
trap 'rm -rf "$WORK"' EXIT

PACKAGES="
    xf86-input-evdev libevdev mtdev eudev-libs
    xorg-server xorg-server-common xf86-video-fbdev xkbcomp xkeyboard-config
    libxkbfile libx11 libxcb xeyes xclock libxi
    font-cursor-misc font-misc-misc encodings font-alias mkfontscale fontconfig font-dejavu
    cwm libxrandr xterm ncurses-terminfo-base libncursesw libice libsm libuuid
    libxaw libxext libxft libxmu libxpm libxt libxrender freetype libexpat
    brotli-libs bzip2 libbz2 libpng zlib libxau libxdmcp
    pixman libpciaccess nettle libxfont2 libxshmfence libdrm libxcvt
    libbsd libmd gmp libfontenc
    feh imlib2 libxinerama libcurl libmagic file fastfetch hwdata-pci
    nano nano-syntax nasm ca-certificates-bundle c-ares libcrypto3 libidn2 libunistring
    nghttp2-libs libpsl libssl3 zstd-libs giflib libheif aom-libs libde265 libid3tag
    libjpeg-turbo libsharpyuv x265-libs numactl tiff libwebp libwebpdemux xz-libs libstdc++ libgcc
"

apk_field() {
    _apk_file=$1
    _field=$2
    tar -xOzf "$_apk_file" .PKGINFO 2>/dev/null |
        sed -n "s/^$_field = //p" | head -n 1
}

package_arch_ok() {
    check_pkg=$1
    check_arch=$2
    case "$check_arch" in
        x86_64) return 0 ;;
        noarch)
            case "$check_pkg" in
                nano-syntax|encodings|font-alias|font-cursor-misc|font-dejavu|font-misc-misc|xkeyboard-config|xorg-server-common|ncurses-terminfo-base|ca-certificates-bundle|hwdata-pci)
                    return 0
                    ;;
            esac
            ;;
    esac
    return 1
}

pkg_record() {
    record_pkg=$1
    record_idx=$2
    record_active=0
    record_version=''
    while IFS= read -r record_line || [ -n "$record_line" ]; do
        case "$record_line" in
            "P:$record_pkg")
                record_active=1
                record_version=''
                ;;
            P:*)
                record_active=0
                record_version=''
                ;;
            V:*)
                if [ "$record_active" -eq 1 ]; then record_version=${record_line#V:}; fi
                ;;
            A:*)
                if [ "$record_active" -eq 1 ]; then
                    record_arch=${record_line#A:}
                    case "$record_arch" in
                        x86_64|noarch)
                            if [ -n "$record_version" ]; then printf '%s\n' "$record_version"; return 0; fi
                            ;;
                    esac
                fi
                ;;
        esac
    done < "$record_idx"
    return 0
}

fetch_url() {
    _url=$1
    _out=$2
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 1 -o "$_out" "$_url"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$_out" "$_url"
    else
        echo "ERROR: curl or wget is required to download Alpine APKs" >&2
        return 1
    fi
}

tar_apk() {
    if tar --version 2>/dev/null | grep -q 'GNU tar'; then
        tar --warning=no-unknown-keyword "$@"
    else
        tar "$@"
    fi
}

refresh_index() {
    repo_name=$1
    index_dir="$WORK/index-$repo_name"
    mkdir -p "$index_dir"
    index_file="$index_dir/APKINDEX"
    archive_file="$WORK/APKINDEX-$repo_name.tar.gz"
    tmp_index="$index_file.tmp"
    index_url="$ALPINE_MIRROR/$ALPINE_BRANCH/$repo_name/$ALPINE_ARCH/APKINDEX.tar.gz"

    echo "[XORG] refreshing Alpine $ALPINE_BRANCH/$repo_name APKINDEX"
    if ! fetch_url "$index_url" "$archive_file"; then
        rm -f "$archive_file" "$tmp_index"
        echo "ERROR: failed to refresh Alpine package index: $index_url" >&2
        return 1
    fi
    if ! tar -xOzf "$archive_file" APKINDEX > "$tmp_index" 2>/dev/null ||
       [ ! -s "$tmp_index" ] || ! grep -q '^P:' "$tmp_index"; then
        rm -f "$archive_file" "$tmp_index"
        echo "ERROR: downloaded Alpine APKINDEX is invalid: $index_url" >&2
        return 1
    fi
    mv -f "$tmp_index" "$index_file"
    rm -f "$archive_file"
    case "$repo_name" in
        main) INDEX_MAIN_READY=1 ;;
        community) INDEX_COMMUNITY_READY=1 ;;
    esac
}

INDEX_MAIN_READY=0
INDEX_COMMUNITY_READY=0
ensure_index() {
    repo_name=$1
    case "$repo_name" in
        main)
            [ "$INDEX_MAIN_READY" -eq 1 ] && return 0
            refresh_index main
            ;;
        community)
            [ "$INDEX_COMMUNITY_READY" -eq 1 ] && return 0
            refresh_index community
            ;;
    esac
}

unpack_package() {
    unpack_apk=$1
    data_member=$(tar -tzf "$unpack_apk" 2>/dev/null | grep -E '(^|/)data\.tar\.gz$' | sed -n '1p' || true)
    if [ -n "$data_member" ]; then

        payload="$WORK/apk-data.tar.gz"
        tar_apk -xOzf "$unpack_apk" "$data_member" > "$payload"
        tar_apk -xzf "$payload" -C "$PACKAGE_ROOTFS"
        rm -f "$payload"
    else
        tar_apk -xzf "$unpack_apk" -C "$PACKAGE_ROOTFS"
    fi
}

fetch() {
    want_pkg=$1
    selected_apk=''

    for candidate in "$CACHE/$want_pkg"-*.apk; do
        [ -s "$candidate" ] || continue
        cached_name=$(apk_field "$candidate" pkgname || true)
        cached_arch=$(apk_field "$candidate" arch || true)
        if [ "$cached_name" = "$want_pkg" ] && package_arch_ok "$want_pkg" "$cached_arch" &&
           tar -tzf "$candidate" >/dev/null 2>&1; then
            selected_apk=$candidate
            break
        fi
        echo "[XORG] ignoring invalid cached APK without deleting it: $candidate (name=$cached_name arch=$cached_arch)" >&2
    done

    if [ -z "$selected_apk" ]; then
        ensure_index main
        selected_repo=main
        index_file="$WORK/index-main/APKINDEX"
        version=$(pkg_record "$want_pkg" "$index_file" || true)
        if [ -z "$version" ]; then
            ensure_index community
            selected_repo=community
            index_file="$WORK/index-community/APKINDEX"
            version=$(pkg_record "$want_pkg" "$index_file" || true)
        fi
        [ -n "$version" ] || {
            echo "ERROR: package $want_pkg not found in Alpine $ALPINE_BRANCH indexes" >&2
            return 20
        }

        selected_apk="$CACHE/$want_pkg-$version.apk"
        if [ -e "$selected_apk" ]; then
            old_name=$(apk_field "$selected_apk" pkgname || true)
            old_arch=$(apk_field "$selected_apk" arch || true)
            if [ "$old_name" != "$want_pkg" ] || ! package_arch_ok "$want_pkg" "$old_arch" ||
               ! tar -tzf "$selected_apk" >/dev/null 2>&1; then

                mv "$selected_apk" "$selected_apk.rejected-$$"
            else
                unpack_package "$selected_apk"
                return 0
            fi
        fi

        package_url="$ALPINE_MIRROR/$ALPINE_BRANCH/$selected_repo/$ALPINE_ARCH/$want_pkg-$version.apk"
        partial="$selected_apk.part.$$"
        echo "[XORG] downloading $want_pkg-$version"
        if ! fetch_url "$package_url" "$partial" || [ ! -s "$partial" ]; then
            rm -f "$partial"
            echo "ERROR: failed to download Alpine package: $package_url" >&2
            return 21
        fi
        pkg_name=$(apk_field "$partial" pkgname || true)
        pkg_arch=$(apk_field "$partial" arch || true)
        if [ "$pkg_name" != "$want_pkg" ] || ! package_arch_ok "$want_pkg" "$pkg_arch" ||
           ! tar -tzf "$partial" >/dev/null 2>&1; then
            mv "$partial" "$selected_apk.rejected-$$"
            echo "ERROR: package metadata mismatch for $selected_apk (name=$pkg_name arch=$pkg_arch)" >&2
            return 22
        fi
        mv "$partial" "$selected_apk"
    fi

    pkg_name=$(apk_field "$selected_apk" pkgname || true)
    pkg_arch=$(apk_field "$selected_apk" arch || true)
    if [ "$pkg_name" != "$want_pkg" ] || ! package_arch_ok "$want_pkg" "$pkg_arch"; then
        echo "ERROR: package metadata mismatch for $selected_apk (name=$pkg_name arch=$pkg_arch)" >&2
        return 22
    fi
    unpack_package "$selected_apk"
}

for package in $PACKAGES; do
    fetch "$package"
done

rm -f "$PACKAGE_ROOTFS/usr/share/X11/xorg.conf.d/10-quirks.conf" 2>/dev/null || true

cp -a "$PACKAGE_ROOTFS/." "$ROOTFS/"
mkdir -p "$ROOTFS/etc"
cp "$ROOT/ports/alpine-xorg/templates/os-release" "$ROOTFS/etc/os-release"

mkdir -p "$ROOTFS/usr/lib/xorg/modules/drivers" "$ROOTFS/etc/X11"

cp "$ROOT/ports/alpine-xorg/templates/xorg.conf" "$ROOTFS/etc/X11/xorg.conf"

mkdir -p "$ROOTFS/usr/share/X11/app-defaults" "$ROOTFS/usr/lib/X11/app-defaults"
cp "$ROOT/ports/alpine-xorg/templates/XEyes" "$ROOTFS/usr/share/X11/app-defaults/XEyes"
cp -f "$ROOTFS/usr/share/X11/app-defaults/XEyes" "$ROOTFS/usr/lib/X11/app-defaults/XEyes"

for clock_defaults in XClock XClock-color; do
  clock_defaults_src="$ROOTFS/usr/share/X11/app-defaults/$clock_defaults"
  [ -s "$clock_defaults_src" ] || {
    echo "ERROR: stock xclock app-defaults resource missing: $clock_defaults_src" >&2
    exit 25
  }
  cp -f "$clock_defaults_src" "$ROOTFS/usr/lib/X11/app-defaults/$clock_defaults"
done

verify_x86_64_elf() {
  path=$1
  [ -f "$path" ] || { echo "ERROR: required input runtime file missing: $path" >&2; exit 25; }
  desc=$(file -L "$path" 2>/dev/null || true)
  case "$desc" in
    *"ELF 64-bit"*"x86-64"*) ;;
    *) echo "ERROR: wrong ELF architecture for $path: $desc" >&2; exit 25 ;;
  esac
}
verify_x86_64_elf "$ROOTFS/usr/lib/xorg/modules/input/evdev_drv.so"
verify_x86_64_elf "$ROOTFS/usr/lib/libevdev.so.2"
verify_x86_64_elf "$ROOTFS/usr/lib/libmtdev.so.1"
verify_x86_64_elf "$ROOTFS/usr/lib/libudev.so.1"
verify_x86_64_elf "$ROOTFS/usr/bin/nano"
verify_x86_64_elf "$ROOTFS/usr/bin/rnano"
verify_x86_64_elf "$ROOTFS/usr/bin/nasm"
verify_x86_64_elf "$ROOTFS/usr/bin/ndisasm"

[ -f "$ROOTFS/usr/lib/xorg/modules/input/evdev_drv.so" ] || {
  echo "ERROR: xf86-input-evdev did not install evdev_drv.so" >&2; exit 24;
}
grep -q 'ModulePath "/"' "$ROOTFS/etc/X11/xorg.conf" || {
  echo "ERROR: Xorg ModulePath missing" >&2; exit 24;
}
grep -q 'Load "evdev"' "$ROOTFS/etc/X11/xorg.conf" || {
  echo "ERROR: Xorg evdev module load missing" >&2; exit 24;
}
grep -q 'InputDevice "YabroMouse" "CorePointer"' "$ROOTFS/etc/X11/xorg.conf" || {
  echo "ERROR: Xorg CorePointer is not connected to YabroMouse" >&2; exit 24;
}
grep -q 'Option "Device" "/dev/input/event1"' "$ROOTFS/etc/X11/xorg.conf" || {
  echo "ERROR: Xorg mouse device is not /dev/input/event1" >&2; exit 24;
}
grep -q 'InputDevice "YabroKeyboard" "CoreKeyboard"' "$ROOTFS/etc/X11/xorg.conf" || {
  echo "ERROR: Xorg CoreKeyboard is not connected to YabroKeyboard" >&2; exit 24;
}

bash "$ROOT/ports/alpine-xorg/verify-xorg-host.sh"

echo "[XORG] runtime populated with explicit evdev keyboard/mouse in $ROOTFS"
