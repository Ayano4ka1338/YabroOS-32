<div align="center">

<img width="1280" height="800" alt="photo_2026-10-30_15-10-28" src="https://github.com/user-attachments/assets/18c86a94-ead1-4f1d-94a7-22481ba867db" />


<br>

# YabroOS-32

**Hobby x86_64 operating system with a hybrid C/Rust kernel.**

![status](https://img.shields.io/badge/status-v0.1%20experimental-orange)
![arch](https://img.shields.io/badge/arch-x86__64-blue)
![boot](https://img.shields.io/badge/boot-Limine%20BIOS%20%2F%20UEFI-lightgrey)
![license](https://img.shields.io/badge/license-none%20yet-lightgrey)

</div>

## Key features

- **Hybrid kernel** written in C and Rust (`no_std`), with assembly for syscall entry.
- **Boot** through Limine on both BIOS and UEFI.
- **Memory management**: physical and virtual memory, ELF64 userspace loading.
- **Virtual filesystem** with `/proc`, `/dev` and in-memory files and directories.
- **Framebuffer** console and `/dev/fb0`, with an Xorg video path.
- **Input**: PS/2 keyboard and mouse, exposed under `/dev/input/`.
- **Shell (YSH)** with a file and system command set.
- **Native musl** toolchain and a musl ABI test suite.
- **Built-in compiler**: TCC with a static musl sysroot, so you can compile C inside the OS.
- **Graphical session** via `startx` / `xinit` with the `cwm` window manager.
- **Power control** through ACPI: `poweroff`, `reboot`.

## Current stage

YabroOS-32 is at **v0.1, experimental**. It boots under QEMU, runs an interactive shell, and can start a graphical session. Interfaces and hardware support are still changing. Use QEMU first.

## What is in the build

`./build.sh` produces `yabroos-32.iso` containing:

- **Kernel**: C and Rust sources, linked to `kernel.elf`, with the Limine bootloader.
- **Userspace**: YSH shell, YabrusBox applets, `startx` and `xinit`.
- **Musl**: native musl ABI build and a test ELF (`ports/musl-abi`).
- **TCC**: Tiny C Compiler with a static musl sysroot.
- **Xorg runtime**: Alpine packages staged into the image, with `cwm` as the window manager.
- **Doom**: port with Freedoom Phase 2 data.

By default the shell is the boot program. To boot the hello-world test program instead, use `USER_TEST=hello ./build.sh`.

## Build requirements

Build on a 64-bit Linux host.

**Debian / Ubuntu:**

```bash
sudo apt install build-essential binutils make python3 curl wget tar \
  xorriso dosfstools mtools qemu-system-x86 musl-tools
```

**Rust** (needed for the kernel), with the bare-metal target:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
rustup target add x86_64-unknown-none
```

The first build downloads Alpine packages, TCC sources and Freedoom data, so it needs internet access. Later builds use the cache.

## Building and running

```bash
chmod +x build.sh run.sh clean.sh
./build.sh          # build the ISO
./run.sh            # start it in QEMU (384 MiB RAM by default)
./clean.sh          # remove build output
```

Change the RAM size with `YABRO_QEMU_MEMORY=512M ./run.sh`. QEMU writes debug output to `debug.log`.

## Basic commands

These run inside the YSH shell after boot.

**Help and system**

```text
help                 list commands
uname                system name and version
uptime               time since boot
mem                  memory usage
ps                   list processes
env                  show environment variables
history              show command history
cls                  clear the screen
```

**Files and directories**

```text
pwd                  print the current directory
cd DIR               change directory
ls [DIR]             list files
cat FILE             print a file
head FILE            first 10 lines of a file
wc [FILE]            count lines, words and bytes
echo TEXT            print text
mkdir DIR            create a directory
touch FILE           create an empty file
cp SRC... DEST       copy files
mv SRC... DEST       move or rename files
rm [-rf] FILE...     remove files (-r for directories, -f to ignore missing)
```

**Compiling**

```text
tcc FILE.c           compile a C program with TCC
cc FILE.c            same as tcc
```

**Power**

```text
poweroff             power off (ACPI)
shutdown             same as poweroff
reboot               restart
exit                 leave the shell
```

**Graphics: `startx`**

```text
startx               start the Xorg session with the cwm window manager
```

`startx` starts the X server through `xinit` and launches `/usr/bin/cwm`. It needs the Xorg runtime in the image and `/etc/X11/xorg.conf`, which are included in the standard build. If the session does not start, check the console output and `debug.log`.

## Project layout

| Path | Contents |
| --- | --- |
| `src/kernel/` | Kernel sources (C, Rust, assembly), VFS, memory management |
| `src/user/` | YSH shell, YabrusBox, `startx`, `xinit` |
| `limine-bin/` | Limine bootloader files |
| `musl-port/` | musl sources and native build scripts |
| `ports/` | Alpine Xorg runtime, `cwm`, musl ABI tests, Doom, TCC |
| `build.sh`, `run.sh`, `clean.sh` | Build, run and clean scripts |
| `limine.conf` | Boot configuration |

