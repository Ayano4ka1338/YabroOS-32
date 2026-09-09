CC := gcc
AS := as
LD := ld
CARGO := cargo
RUSTC := rustc

K := src/kernel
U := src/user
T := src/tests

KERNEL_CFLAGS := -ffreestanding -nostdlib -m64 -mcmodel=kernel -mno-red-zone -fno-pie -fno-stack-protector -O2 -fno-tree-vectorize -fno-builtin -mno-sse -mno-sse2 -mno-mmx -msoft-float
USER_CFLAGS   := -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -no-pie -fno-pie -mno-red-zone -fno-stack-protector -O2

KERNEL_OBJS := build/obj/main.o build/obj/pmm.o build/obj/vmm.o build/obj/vfs.o build/obj/syscall_entry.o

USER_TEST ?= shell
ifeq ($(USER_TEST),shell)
    TEST_SRC := $(U)/userland_shell.c
    TEST_OBJ := build/userland_shell.o
    TEST_ELF := build/userland_shell.elf
    TEST_LD  := -Ttext=0x1e00000
else ifeq ($(USER_TEST),hello)
    TEST_SRC := $(U)/userland_hello.c
    TEST_OBJ := build/userland_hello.o
    TEST_ELF := build/userland_hello.elf
    TEST_LD  := -Ttext=0x1e00000
else
    $(error ERROR: unknown USER_TEST=$(USER_TEST). Use 'shell' or 'hello')
endif

LIMINE_DIR := $(shell find limine-bin -type f -name 'limine-bios.sys' -exec dirname {} \; | head -n 1)
LIMINE_CLI := $(shell find limine-bin -type f -name 'limine' -executable | head -n 1)

.PHONY: all clean rust_kernel c_kernel link_kernel musl_base userland doom musl_abi initrd iso

all: iso

rust_kernel:
	@echo "===> 1. Rust kernel"
	@mkdir -p build
	@if command -v rustup >/dev/null 2>&1; then \
		rustup target add x86_64-unknown-none >/dev/null 2>&1 || true; \
		PATH="$(HOME)/.cargo/bin:$(PATH)" rustup run nightly rustc --crate-type staticlib --target x86_64-unknown-none -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off $(K)/main.rs -o build/libkernel.a 2>/dev/null || \
		PATH="$(HOME)/.cargo/bin:$(PATH)" rustup run stable rustc --crate-type staticlib --target x86_64-unknown-none -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off $(K)/main.rs -o build/libkernel.a; \
	elif command -v rustc >/dev/null 2>&1; then \
		rustc --crate-type staticlib --target x86_64-unknown-none -C relocation-model=static -C panic=abort -C opt-level=3 -C overflow-checks=off $(K)/main.rs -o build/libkernel.a; \
	else \
		echo "ERROR: rustup/rustc is required." >&2; exit 2; \
	fi

build/obj/%.o: $(K)/%.c
	@mkdir -p build/obj
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

build/obj/syscall_entry.o: $(K)/syscall_entry.S
	@mkdir -p build/obj
	$(AS) -64 $< -o $@

c_kernel: $(KERNEL_OBJS)

iso_root/boot/kernel.elf: rust_kernel c_kernel
	@echo "===> 3. Link kernel"
	@mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	$(LD) -m elf_x86_64 -T $(K)/linker.ld -nostdlib -static $(KERNEL_OBJS) build/libkernel.a -o iso_root/boot/kernel.elf

musl_base:
	@echo "===> 4.1. Native musl ABI foundation"
	@if [ "$${BUILD_UPSTREAM_MUSL:-1}" = "1" ]; then \
		bash musl-port/native-musl/build-musl-native.sh; \
		if [ ! -x musl-port/native-musl/out/bin/musl-gcc ]; then echo "ERROR: musl-gcc missing" >&2; exit 3; fi; \
	fi

userland: musl_base
	@echo "===> 4. User ELF ($(USER_TEST))"
	@mkdir -p build
	$(CC) $(USER_CFLAGS) -c $(TEST_SRC) -o $(TEST_OBJ)
	$(LD) -m elf_x86_64 -static -nostdlib $(TEST_LD) $(TEST_OBJ) -o $(TEST_ELF)
	@cp $(TEST_ELF) userland_selected.elf
	@echo "===> 4.5. YabrusBox"
	$(CC) $(USER_CFLAGS) -c $(U)/userland_yabrusbox.c -o build/userland_yabrusbox.o
	$(LD) -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 build/userland_yabrusbox.o -o build/YABRUSBOX.ELF

doom:
	@echo "===> 4.8. Doom"
	@bash ports/ports-doom-real/build.sh
	@if [ ! -f ports/ports-doom-real/DOOM.ELF ]; then echo "ERROR: Doom ELF missing" >&2; exit 4; fi

musl_abi:
	@echo "===> 4.9. Unified musl ABI ELF"
	@$(MAKE) -C ports/musl-abi/oneelf MUSL_CC="$${MUSL_CC:-musl-gcc}"
	@if [ ! -f build/musl-abi/yabro-musl-tests.elf ]; then echo "ERROR: musl test ELF missing" >&2; exit 4; fi

iso_root/boot/initrd.fat: userland doom musl_abi
	@echo "===> 5. initrd"
	@if [ -z "$(LIMINE_DIR)" ]; then echo "ERROR: limine-bios.sys not found" >&2; exit 2; fi
	@cp "$(LIMINE_DIR)/limine-bios.sys" iso_root/boot/limine/
	@cp "$(LIMINE_DIR)/limine-bios-cd.bin" iso_root/boot/limine/
	@cp "$(LIMINE_DIR)/limine-uefi-cd.bin" iso_root/boot/limine/
	@cp "$(LIMINE_DIR)/limine-uefi-cd.bin" iso_root/EFI/BOOT/BOOTX64.EFI
	@cp limine.conf iso_root/boot/limine/limine.conf
	@rm -f iso_root/boot/initrd.fat
	dd if=/dev/zero of=iso_root/boot/initrd.fat bs=1M count=64 status=none
	mkfs.fat -F32 iso_root/boot/initrd.fat >/dev/null
	mmd -i iso_root/boot/initrd.fat ::/bin ::/dev ::/etc ::/lib ::/usr ::/usr/lib ::/proc ::/tmp ::/etc/X11
	mcopy -i iso_root/boot/initrd.fat userland_selected.elf ::/HELLO.ELF
	mcopy -i iso_root/boot/initrd.fat build/userland_shell.elf ::/YSH.ELF
	mcopy -i iso_root/boot/initrd.fat build/YABRUSBOX.ELF ::/YABRUSBOX.ELF
	mcopy -i iso_root/boot/initrd.fat build/userland_shell.elf ::/bin/YSH.ELF
	mcopy -i iso_root/boot/initrd.fat build/YABRUSBOX.ELF ::/bin/YABRUSBOX.ELF
	mcopy -i iso_root/boot/initrd.fat ports/ports-doom-real/DOOM.ELF ::/DOOM.ELF
	mcopy -i iso_root/boot/initrd.fat ports/ports-doom-real/freedoom2.wad ::/freedoom2.wad
	mcopy -i iso_root/boot/initrd.fat build/musl-abi/yabro-musl-tests.elf ::/MUSL-TESTS.ELF

iso: iso_root/boot/kernel.elf iso_root/boot/initrd.fat
	@echo "===> 6. ISO"
	@rm -f yabroos-32.iso
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-eltorito-alt-boot -e EFI/BOOT/BOOTX64.EFI -no-emul-boot \
		-isohybrid-gpt-basdat -o yabroos-32.iso iso_root >/dev/null 2>&1
	@if [ -n "$(LIMINE_CLI)" ]; then "$(LIMINE_CLI)" bios-install yabroos-32.iso >/dev/null; fi
	@echo "\nBUILD OK: yabroos-32.iso\n"

clean:
	@echo "===> Cleaning..."
	rm -rf build iso_root yabroos-32.iso userland_selected.elf test.txt

