# YabroOS-32 0.0.1 Alpha

YabroOS-32 is an experimental x86_64 operating system. This release is **0.0.1 Alpha**.

The current system combines a Rust kernel layer, freestanding C kernel subsystems, x86_64 system calls, a VFS, YSH, YabrusBox, and an incremental musl userspace bring-up.

## Current status

The 0.0.1 Alpha baseline includes: 

- x86_64 Limine boot
- Rust and C kernel components
- PMM and VMM
- ELF userspace loading
- fork, exec, wait and basic process handling
- syscall ABI
- VFS with `/bin`, `/dev`, `/etc`, `/proc` and `/tmp`
- YSH shell
- YabrusBox multicall utilities
- file operations including `mkdir`, `rmdir`, `rm`, `cp`, `mv`, `cat`, `echo` and `ls`
- `REALMUSL4.ELF`, exposed as `MUSL4.ELF` in the image

This is an alpha operating system. Interfaces can still change.

## Source tree

```text
src/
├── kernel/      Rust/C kernel, VFS and syscall entry
├── user/        YSH, YabrusBox and sample userspace programs
└── tests/       userspace and VFS smoke tests

musl-port/       REALMUSL4 and libc bring-up work
limine-bin/      bootloader files used by the build
docs/            project documentation and historical notes
```

## Build on Debian

Install the required host tools:

```sh
sudo apt update
sudo apt install build-essential binutils rustup xorriso dosfstools mtools git ca-certificates
rustup default stable
rustup target add x86_64-unknown-none
chmod +x *.sh
./build.sh
```

## Build on Fedora

```sh
sudo dnf install gcc gcc-c++ binutils make rustup xorriso dosfstools mtools git ca-certificates
rustup default stable
rustup target add x86_64-unknown-none
chmod +x *.sh
./build.sh
```

## Build on Arch Linux

```sh
sudo pacman -S --needed base-devel binutils rustup xorriso dosfstools mtools git ca-certificates
rustup default stable
rustup target add x86_64-unknown-none
chmod +x *.sh
./build.sh
```

The build also requires the Limine files already stored in `limine-bin/`.

## Running

The build produces `yabroos-32.iso`. Boot it with your preferred x86_64 virtual machine or compatible hardware.

The kernel shell prompt is:

```text
YabroOS-32>
```

To start the userspace shell from the kernel shell:

```text
run YSH.ELF
```

Inside YSH, userspace commands are provided by YabrusBox.

## Debug policy

The production build does not intentionally print the historical stage and audit traces. Debugging material is kept outside the normal runtime path.

## Documentation

See [`docs/README_PROGRAMS.md`](docs/README_PROGRAMS.md) for available programs, YabrusBox applets, and instructions for writing a userspace program for YabroOS-32.

## Version

**YabroOS-32 0.0.1 Alpha**

## Screenshot

<img width="1273" height="637" alt="image" src="https://github.com/user-attachments/assets/0f10035a-ef05-44d1-918a-42e0d1345329" />
