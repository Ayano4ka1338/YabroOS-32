/* Minimal userspace program. */
typedef unsigned long size_t;
typedef long ssize_t;

static inline long linux_syscall3(
    long nr,
    long a1,
    long a2,
    long a3
) {
    long ret;

    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr),
          "D"(a1),
          "S"(a2),
          "d"(a3)
        : "rcx", "r11", "memory"
    );

    return ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    return linux_syscall3(
        1,
        fd,
        (long)buf,
        (long)count
    );
}

static inline void exit_group(int status) {
    linux_syscall3(231, status, 0, 0);
}

void _start(void) {
    static const char msg[] = "[V12.34] IRETQ reached userspace\n";

    write(1, msg, sizeof(msg) - 1);

    exit_group(0);

    for (;;)
        __asm__ volatile ("hlt");
}
