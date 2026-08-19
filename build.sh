#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

rm -rf build iso_root yabroos-32.iso userland_selected.elf test.txt
mkdir -p build/obj iso_root/boot/limine iso_root/EFI/BOOT iso_root/boot

K=src/kernel
U=src/user
T=src/tests

KERNEL_CFLAGS='-ffreestanding -nostdlib -m64 -mcmodel=kernel -mno-red-zone -fno-pie -fno-stack-protector -O2 -fno-tree-vectorize -fno-builtin -mno-sse -mno-sse2 -mno-mmx -msoft-float'
USER_CFLAGS='-m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -no-pie -fno-pie -mno-red-zone -fno-stack-protector -O2'

printf '%s\n' '===> 1. Rust kernel'
if command -v rustup >/dev/null 2>&1; then
  rustup target add x86_64-unknown-none >/dev/null 2>&1 || true
  if ! rustup run nightly rustc --crate-type staticlib --target x86_64-unknown-none \
      -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off \
      "$K/main.rs" -o build/libkernel.a; then
    rustup run stable rustc --crate-type staticlib --target x86_64-unknown-none \
      -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off \
      "$K/main.rs" -o build/libkernel.a
  fi
elif command -v rustc >/dev/null 2>&1; then
  rustc --crate-type staticlib --target x86_64-unknown-none \
    -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off \
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

printf '%s\n' '===> 4. User ELF'
USER_TEST=${USER_TEST:-shell}
bash musl-port/realmusl4-source/build-realmusl4.sh
case "$USER_TEST" in
  shell)  TEST_SRC="$U/userland_shell.c"; TEST_OBJ=build/userland_shell.o; TEST_ELF=build/userland_shell.elf; TEST_LD='-Ttext=0x1e00000' ;;
  hello)  TEST_SRC="$U/userland_hello.c"; TEST_OBJ=build/userland_hello.o; TEST_ELF=build/userland_hello.elf; TEST_LD='-Ttext=0x1e00000' ;;
  *) echo "ERROR: unknown USER_TEST=$USER_TEST (use shell, hello, or munmap)" >&2; exit 2 ;;
esac

gcc $USER_CFLAGS -c "$TEST_SRC" -o "$TEST_OBJ"
ld -m elf_x86_64 -static -nostdlib $TEST_LD "$TEST_OBJ" -o "$TEST_ELF"
cp "$TEST_ELF" userland_selected.elf

printf '%s\n' '===> 4.5. YabrusBox'
gcc $USER_CFLAGS -c "$U/userland_yabrusbox.c" -o build/userland_yabrusbox.o
ld -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 build/userland_yabrusbox.o -o build/YABRUSBOX.ELF

printf '%s\n' '===> 5. initrd'
if [ ! -d limine-bin ]; then echo 'ERROR: limine-bin/ is missing.' >&2; exit 2; fi
LIMINE_DIR="$(find limine-bin -type f -name 'limine-bios.sys' -exec dirname {} \; | head -n 1)"
LIMINE_CLI="$(find limine-bin -type f -name 'limine' -executable | head -n 1)"
[ -n "$LIMINE_DIR" ] || { echo 'ERROR: limine-bios.sys not found' >&2; exit 2; }

cp "$LIMINE_DIR/limine-bios.sys" iso_root/boot/limine/
cp "$LIMINE_DIR/limine-bios-cd.bin" iso_root/boot/limine/
cp "$LIMINE_DIR/limine-uefi-cd.bin" iso_root/boot/limine/
cp "$LIMINE_DIR/limine-uefi-cd.bin" iso_root/EFI/BOOT/BOOTX64.EFI
cp limine.conf iso_root/boot/limine/limine.conf

dd if=/dev/zero of=iso_root/boot/initrd.fat bs=1M count=64 status=none
mkfs.fat -F32 iso_root/boot/initrd.fat >/dev/null
printf 'Test file content for initrd\n' > test.txt
mmd -i iso_root/boot/initrd.fat ::/bin
mmd -i iso_root/boot/initrd.fat ::/dev
mmd -i iso_root/boot/initrd.fat ::/etc
mmd -i iso_root/boot/initrd.fat ::/proc
mmd -i iso_root/boot/initrd.fat ::/tmp
mcopy -i iso_root/boot/initrd.fat test.txt ::/TEST.TXT
mcopy -i iso_root/boot/initrd.fat userland_selected.elf ::/HELLO.ELF
mcopy -i iso_root/boot/initrd.fat build/userland_shell.elf ::/YSH.ELF
mcopy -i iso_root/boot/initrd.fat build/YABRUSBOX.ELF ::/YABRUSBOX.ELF
mcopy -i iso_root/boot/initrd.fat build/userland_shell.elf ::/bin/YSH.ELF
mcopy -i iso_root/boot/initrd.fat build/YABRUSBOX.ELF ::/bin/YABRUSBOX.ELF
mcopy -i iso_root/boot/initrd.fat musl-port/realmusl4-source/REALMUSL4.ELF ::/MUSL4.ELF

mdir -i iso_root/boot/initrd.fat ::/bin >/dev/null
mdir -i iso_root/boot/initrd.fat ::/YSH.ELF >/dev/null
mdir -i iso_root/boot/initrd.fat ::/YABRUSBOX.ELF >/dev/null

printf '%s\n' '===> 6. ISO'
xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  -eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
  -isohybrid-gpt-basdat -o yabroos-32.iso iso_root >/dev/null 2>&1
if [ -n "$LIMINE_CLI" ]; then "$LIMINE_CLI" bios-install yabroos-32.iso >/dev/null; fi

printf '\nBUILD OK: yabroos-32.iso\n'
