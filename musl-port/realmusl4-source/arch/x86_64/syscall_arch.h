/* Musl integration source. */
#ifndef YABROOS_MUSL_SYSCALL_ARCH_H
#define YABROOS_MUSL_SYSCALL_ARCH_H


static inline long __yabroos_syscall0(long n) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall1(long n,long a) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall2(long n,long a,long b) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall3(long n,long a,long b,long c) {
    long r; __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall4(long n,long a,long b,long c,long d) {
    register long r10 __asm__("r10") = d; long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall5(long n,long a,long b,long c,long d,long e) {
    register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "rcx", "r11", "memory"); return r;
}
static inline long __yabroos_syscall6(long n,long a,long b,long c,long d,long e,long f) {
    register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f; long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory"); return r;
}
#endif
