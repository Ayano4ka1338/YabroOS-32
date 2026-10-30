#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd -P)"
cd "$ROOT"
printf '[BUILD] canonical project root: %s\n' "$ROOT"
bash "$ROOT/clean.sh"
mkdir -p build/obj iso_root/boot/limine iso_root/EFI/BOOT iso_root/boot

K=src/kernel
U=src/user

KERNEL_CFLAGS="-ffreestanding -nostdlib -m64 -mcmodel=kernel -mno-red-zone -fno-pie -fno-stack-protector -O2 -fno-tree-vectorize -fno-builtin -mno-sse -mno-sse2 -mno-mmx -msoft-float -Wframe-larger-than=16384 -DYABRO_DEBUG_SYSCALL=${YABRO_DEBUG_SYSCALL:-0} -DYABRO_DEBUG_COW=${YABRO_DEBUG_COW:-0} -DYABRO_DEBUG_FB=${YABRO_DEBUG_FB:-0}"
USER_CFLAGS='-m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -no-pie -fno-pie -mno-red-zone -fno-stack-protector -O2'

printf '%s\n' '===> 1. Rust kernel'
if command -v rustup >/dev/null 2>&1; then
  rustup target add x86_64-unknown-none >/dev/null 2>&1 || true
  if ! rustup run nightly rustc --crate-type staticlib --target x86_64-unknown-none \
      -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=${YABRO_OVERFLOW_CHECKS:-off} \
      "$K/main.rs" -o build/libkernel.a; then
    rustup run stable rustc --crate-type staticlib --target x86_64-unknown-none \
      -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=${YABRO_OVERFLOW_CHECKS:-off} \
      "$K/main.rs" -o build/libkernel.a
  fi
elif command -v rustc >/dev/null 2>&1; then
  rustc --crate-type staticlib --target x86_64-unknown-none \
    -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=${YABRO_OVERFLOW_CHECKS:-off} \
    "$K/main.rs" -o build/libkernel.a
else
  echo 'ERROR: rustup/rustc is required.' >&2
  exit 2
fi

printf '%s\n' '===> 2. C kernel/VFS'
gcc $KERNEL_CFLAGS -c "$K/main.c" -o build/obj/main.o
gcc $KERNEL_CFLAGS -c "$K/pmm.c" -o build/obj/pmm.o
gcc $KERNEL_CFLAGS -c "$K/vmm.c" -o build/obj/vmm.o
gcc $KERNEL_CFLAGS -c "$K/vfs.c" -o build/obj/vfs.o
as -64 "$K/syscall_entry.S" -o build/obj/syscall_entry.o

printf '%s\n' '===> 3. Link kernel'
ld -m elf_x86_64 -T "$K/linker.ld" -nostdlib -static \
  build/obj/main.o build/obj/pmm.o build/obj/vmm.o build/obj/vfs.o \
  build/obj/syscall_entry.o build/libkernel.a -o iso_root/boot/kernel.elf

printf '%s\n' '===> 4.1. Native musl ABI foundation'
bash musl-port/native-musl/build-musl-native.sh

printf '%s\n' '===> 4. User ELF'
USER_TEST=${USER_TEST:-shell}
case "$USER_TEST" in
  shell)  TEST_SRC="$U/userland_shell.c"; TEST_OBJ=build/userland_shell.o; TEST_ELF=build/userland_shell.elf; TEST_LD='-Ttext=0x1e00000' ;;
  hello)  TEST_SRC="$U/userland_hello.c"; TEST_OBJ=build/userland_hello.o; TEST_ELF=build/userland_hello.elf; TEST_LD='-Ttext=0x1e00000' ;;
  *) echo "ERROR: unknown USER_TEST=$USER_TEST (use shell or hello)" >&2; exit 2 ;;
esac

gcc $USER_CFLAGS -c "$TEST_SRC" -o "$TEST_OBJ"
ld -m elf_x86_64 -static -nostdlib $TEST_LD "$TEST_OBJ" -o "$TEST_ELF"
cp "$TEST_ELF" userland_selected.elf

printf '%s\n' '===> 4.44. cwm is provided by the Alpine X11 runtime package'

printf '%s\n' '===> 4.45. startx + xinit'
gcc $USER_CFLAGS -c "$U/startx.c" -o build/startx.o
ld -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 build/startx.o -o build/startx.elf

gcc $USER_CFLAGS -c "$U/xinit.c" -o build/xinit.o
ld -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 build/xinit.o -o build/xinit.elf
printf '%s\n' '===> 4.5. YabrusBox'
gcc $USER_CFLAGS -c "$U/userland_yabrusbox.c" -o build/userland_yabrusbox.o
ld -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 build/userland_yabrusbox.o -o build/YABRUSBOX.ELF
printf '%s\n' '===> 4.8. Doom'
bash ports/ports-doom-real/build.sh

printf '%s\n' '===> 4.9. Unified musl ABI ELF'
make -C ports/musl-abi/oneelf MUSL_CC="${MUSL_CC:-musl-gcc}"

printf '%s\n' '===> 4.91. Tiny C Compiler + static musl sysroot'
bash ports/tcc/build.sh

printf '%s\n' '===> 4.95. Alpine Xorg runtime'
bash ports/alpine-xorg/fetch-runtime.sh

rm -f "$ROOT/ports/alpine-xorg/alpine/rootfs/./usr/libexec/Xorg.wrap"

XORG_ROOTFS=ports/alpine-xorg/alpine/rootfs
mkdir -p "$XORG_ROOTFS/lib"
for xorg_input_lib in libevdev.so.2 libmtdev.so.1 libudev.so.1; do
  cp -Lf "$XORG_ROOTFS/usr/lib/$xorg_input_lib" "$XORG_ROOTFS/lib/$xorg_input_lib"
done

printf '%s\n' '===> 5. initrd'

LIMINE_DIR="$ROOT/limine-bin"
LIMINE_CLI="$ROOT/build/limine-cli"
printf '%s\n' '===> Build Limine host utility'
gcc -O2 -std=c11 limine-bin/limine.c -o "$LIMINE_CLI"

cp "$LIMINE_DIR/limine-bios.sys" iso_root/boot/limine/
cp "$LIMINE_DIR/limine-bios-cd.bin" iso_root/boot/limine/
cp "$LIMINE_DIR/limine-uefi-cd.bin" iso_root/boot/limine/
cp "$LIMINE_DIR/BOOTX64.EFI" iso_root/EFI/BOOT/BOOTX64.EFI
cp limine.conf iso_root/boot/limine/limine.conf

initrd_mb=170
printf '===> initrd size: %s MiB\n' "$initrd_mb"
dd if=/dev/zero of=iso_root/boot/initrd.fat bs=1M count="$initrd_mb" status=none

mkfs.fat -F32 iso_root/boot/initrd.fat >/dev/null
mmd -i iso_root/boot/initrd.fat ::/bin
mmd -i iso_root/boot/initrd.fat ::/dev
mmd -i iso_root/boot/initrd.fat ::/dev/pts
mmd -i iso_root/boot/initrd.fat ::/etc
mmd -i iso_root/boot/initrd.fat ::/lib
mmd -i iso_root/boot/initrd.fat ::/usr
mmd -i iso_root/boot/initrd.fat ::/usr/bin
mmd -i iso_root/boot/initrd.fat ::/usr/libexec
mmd -i iso_root/boot/initrd.fat ::/usr/lib
mmd -i iso_root/boot/initrd.fat ::/proc
mmd -i iso_root/boot/initrd.fat ::/tmp
mmd -i iso_root/boot/initrd.fat ::/var
mmd -i iso_root/boot/initrd.fat ::/var/log
mmd -i iso_root/boot/initrd.fat ::/var/lib
mmd -i iso_root/boot/initrd.fat ::/var/lib/xkb
mmd -i iso_root/boot/initrd.fat ::/var/cache
mmd -i iso_root/boot/initrd.fat ::/var/cache/fontconfig
mmd -i iso_root/boot/initrd.fat ::/usr/share
mmd -i iso_root/boot/initrd.fat ::/usr/share/fonts
mcopy -o -i iso_root/boot/initrd.fat userland_selected.elf ::/HELLO.ELF
mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/YSH.ELF

mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/bin/sh
mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/usr/bin/ysh
mcopy -o -i iso_root/boot/initrd.fat build/startx.elf ::/startx
mcopy -o -i iso_root/boot/initrd.fat build/xinit.elf ::/xinit

mcopy -o -i iso_root/boot/initrd.fat build/startx.elf ::/usr/bin/startx
mcopy -o -i iso_root/boot/initrd.fat build/xinit.elf ::/usr/bin/xinit
mcopy -o -i iso_root/boot/initrd.fat ports/cwm/cwmrc ::/.cwmrc
mcopy -o -i iso_root/boot/initrd.fat build/YABRUSBOX.ELF ::/YABRUSBOX.ELF
mcopy -o -i iso_root/boot/initrd.fat ports/ports-doom-real/DOOM.ELF ::/DOOM.ELF

mmd -i iso_root/boot/initrd.fat ::/etc/X11

XORG_CONF_BUILD=build/xorg.conf.yabro
cp ports/alpine-xorg/alpine/rootfs/etc/X11/xorg.conf "$XORG_CONF_BUILD"

XORG_SCREEN_ID=$(python3 src/build/xorg_config.py screen-id "$XORG_CONF_BUILD")
python3 src/build/xorg_config.py layout "$XORG_SCREEN_ID" "$XORG_CONF_BUILD" > "$XORG_CONF_BUILD.tmp"
mv "$XORG_CONF_BUILD.tmp" "$XORG_CONF_BUILD"

python3 src/build/xorg_config.py strip-input "$XORG_CONF_BUILD" > "$XORG_CONF_BUILD.tmp"
mv "$XORG_CONF_BUILD.tmp" "$XORG_CONF_BUILD"

cat "$ROOT/ports/alpine-xorg/templates/yabro-input-devices.conf" >> "$XORG_CONF_BUILD"

mcopy -o -i iso_root/boot/initrd.fat build/musl-abi/yabro-musl-tests.elf ::/MUSL-TESTS.ELF

mkdir -p ports/alpine-xorg/alpine/rootfs/etc
printf '%s\n' '/lib:/usr/lib' > ports/alpine-xorg/alpine/rootfs/etc/ld-musl-x86_64.path

mcopy -o -i iso_root/boot/initrd.fat ports/alpine-xorg/alpine/rootfs/etc/ld-musl-x86_64.path ::/etc/ld-musl-x86_64.path >/dev/null

echo '===> 5.1. Build patched shared musl runtime'
bash ports/alpine-xorg/build-patched-musl.sh

find ports/alpine-xorg/alpine/rootfs/lib -maxdepth 1 -type f ! -name 'ld-musl-x86_64.so.1' ! -name 'libc.musl-x86_64.so.1' -print | \
  while IFS= read -r f; do mcopy -o -i iso_root/boot/initrd.fat "$f" ::/lib/; done
mcopy -o -i iso_root/boot/initrd.fat ports/alpine-xorg/patched-musl/lib/libc.so ::/lib/libc.musl-x86_64.so.1
mcopy -o -i iso_root/boot/initrd.fat ports/alpine-xorg/patched-musl/lib/ld-musl-x86_64.so.1 ::/lib/ld-musl-x86_64.so.1

mkdir -p ports/tcc/rootfs/usr/lib
cp -f ports/alpine-xorg/patched-musl/lib/libc.so ports/tcc/rootfs/usr/lib/libc.so

XORG_RUNTIME_STAGE="$ROOT/build/xorg-runtime"
bash ports/alpine-xorg/stage-runtime.sh "$XORG_ROOTFS" "$XORG_RUNTIME_STAGE"

while IFS= read -r -d '' d; do
  rel=${d#"$XORG_RUNTIME_STAGE"/}
  [ -n "$rel" ] || continue
  mmd -o -i iso_root/boot/initrd.fat "::$rel" >/dev/null 2>&1 || true
done < <(find "$XORG_RUNTIME_STAGE" -mindepth 1 -type d -print0 | sort -z)
while IFS= read -r -d '' f; do
  rel=${f#"$XORG_RUNTIME_STAGE"/}
  mcopy -o -i iso_root/boot/initrd.fat "$f" "::$rel" >/dev/null
done < <(find "$XORG_RUNTIME_STAGE" -type f -print0 | sort -z)

mcopy -o -i iso_root/boot/initrd.fat build/startx.elf ::/startx
mcopy -o -i iso_root/boot/initrd.fat build/startx.elf ::/usr/bin/startx
mcopy -o -i iso_root/boot/initrd.fat build/xinit.elf ::/xinit
mcopy -o -i iso_root/boot/initrd.fat build/xinit.elf ::/usr/bin/xinit
mcopy -o -i iso_root/boot/initrd.fat ports/ports-doom-real/DOOM.ELF ::/DOOM.ELF
mcopy -o -i iso_root/boot/initrd.fat ports/ports-doom-real/DOOM.ELF ::/usr/bin/doom
mcopy -o -i iso_root/boot/initrd.fat ports/ports-doom-real/freedoom2.wad ::/freedoom2.wad

mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/YSH.ELF
mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/bin/sh
mcopy -o -i iso_root/boot/initrd.fat build/userland_shell.elf ::/usr/bin/ysh
mcopy -o -i iso_root/boot/initrd.fat ports/cwm/cwmrc ::/.cwmrc
mcopy -o -i iso_root/boot/initrd.fat src/photo_2026-10-14_18-21-20.jpg ::/photo.jpg

mcopy -o -i iso_root/boot/initrd.fat "$XORG_CONF_BUILD" ::/etc/X11/xorg.conf

for PORT_ROOTFS in ports/tcc/rootfs; do
  [ -d "$PORT_ROOTFS" ] || continue
  while IFS= read -r -d '' d; do
    rel=${d#"$PORT_ROOTFS"/}
    [ -n "$rel" ] || continue
    mmd -o -i iso_root/boot/initrd.fat "::$rel" >/dev/null 2>&1 || true
  done < <(find "$PORT_ROOTFS" -mindepth 1 -type d -print0 | sort -z)
  while IFS= read -r -d '' f; do
    rel=${f#"$PORT_ROOTFS"/}
    mcopy -o -i iso_root/boot/initrd.fat "$f" "::$rel" >/dev/null
  done < <(find "$PORT_ROOTFS" -type f -print0 | sort -z)
done

ISO_FINAL="$ROOT/yabroos-32.iso"
ISO_TMP="$ROOT/.yabroos-32.iso.tmp"
rm -f "$ISO_TMP"
printf '===> 6. ISO\n'
xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
  -isohybrid-gpt-basdat -o "$ISO_TMP" iso_root
"$LIMINE_CLI" bios-install "$ISO_TMP"
mv -f "$ISO_TMP" "$ISO_FINAL"
printf '\nBUILD OK: %s\n' "$ISO_FINAL"
