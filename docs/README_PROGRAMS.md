# YabroOS-32 Programs and Userspace Development

This document describes the userspace programs included in YabroOS-32 0.0.1 Alpha and the current way to build a small userspace ELF.

## Included programs

### YSH

`YSH.ELF` is the userspace shell. It provides shell state and builtins such as:

- `cd`
- `run`
- `help`
- `exit`

Other commands are normally provided by YabrusBox.

Start it from the kernel shell with:

```text
run YSH.ELF
```

### YabrusBox

`YABRUSBOX.ELF` is a multicall utility program. The current applet set includes:

```text
ls cat pwd echo env mkdir rmdir cp mv rm touch head tail wc grep
uname uptime ps kill whoami id true false sh
```

The same ELF can be invoked through an applet name:

```text
yabrusbox ls
yabrusbox cat TEST.TXT
yabrusbox uptime
```

YSH uses the same userspace utility model for external commands.

### HELLO.ELF

`HELLO.ELF` is a minimal userspace program used for loader and process tests.

### MUSL4.ELF

`MUSL4.ELF` is the current REALMUSL4 bring-up program. It is included as an ABI and libc integration milestone, not as the final libc environment.

## Writing a YabroOS-32 program

The current low-level userspace ABI is freestanding. A small program can call the kernel through the x86_64 `syscall` instruction. The repository already contains examples in `src/user/` and the syscall definitions used by YSH and YabrusBox.

A minimal program can look like this:

```c
#include <stdint.h>

static long sys_write(long fd, const char *buf, long len) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(1), "D"(fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static void sys_exit(long code) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(60), "D"(code)
        : "rcx", "r11", "memory"
    );
    for (;;) {}
}

void _start(void) {
    static const char msg[] = "Hello from YabroOS-32\n";
    sys_write(1, msg, sizeof(msg) - 1);
    sys_exit(0);
}
```

Build it with the same freestanding model used by the repository:

```sh
gcc -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
  -fno-builtin -no-pie -fno-pie -mno-red-zone \
  -fno-stack-protector -O2 -c hello.c -o hello.o
ld -m elf_x86_64 -static -nostdlib -Ttext=0x1e00000 \
  hello.o -o HELLO.ELF
```

The resulting ELF must use the ABI supported by the current YabroOS-32 ELF loader. The simplest approach is to start from `src/user/userland_hello.c`.

## Adding a YabrusBox applet

YabrusBox is a multicall ELF. Add the applet implementation to `src/user/userland_yabrusbox.c`, register its name in the dispatcher, and rebuild `YABRUSBOX.ELF`. Keep filesystem operations in userspace through the existing syscall/VFS interface rather than adding a special kernel command.

## Current limitations

This is an alpha ABI. POSIX coverage, libc integration, terminal behavior, pipes, environment handling, networking, graphics and Xorg compatibility are incomplete. Do not assume that a Linux userspace binary can run unchanged.

## Version

**YabroOS-32 0.0.1 Alpha**
