#ifndef YABRO_GRAPHICS_H
#define YABRO_GRAPHICS_H
#include <stdint.h>

#define PROT_READ  1ULL
#define PROT_WRITE 2ULL
#define MAP_SHARED 1ULL
#define FBIOGET_INFO 0x46f0ULL

struct yabro_fb_info {
	uint64_t width;
	uint64_t height;
	uint64_t pitch;
	uint64_t bpp;
	uint64_t size;
	uint64_t phys_offset;
};

static inline long yabro_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
	long r;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8") = a5;
	register long r9  __asm__("r9") = a6;
	__asm__ volatile("syscall" : "=a"(r)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		: "rcx","r11","memory");
	return r;
}
static inline long yabro_syscall3(long n, long a1, long a2, long a3) {
	return yabro_syscall6(n,a1,a2,a3,0,0,0);
}
static inline int yabro_open(const char *path, int flags) {
	return (int)yabro_syscall3(2,(long)path,flags,0);
}
static inline int yabro_close(int fd) {
	return (int)yabro_syscall3(3,fd,0,0);
}
static inline int yabro_fb_get_info(int fd, struct yabro_fb_info *info) {
	return (int)yabro_syscall3(16,fd,FBIOGET_INFO,(long)info);
}
static inline void *yabro_mmap_fb(int fd, uint64_t size, uint64_t prot) {
	return (void*)(uintptr_t)yabro_syscall6(9,0,(long)size,(long)prot,(long)MAP_SHARED,fd,0);
}
static inline int yabro_fb_info(struct yabro_fb_info *info) {
	int fd=yabro_open("/dev/fb0",2);
	if(fd<0) return fd;
	int r=yabro_fb_get_info(fd,info);
	yabro_close(fd);
	return r;
}
static inline int yabro_input_read(void) {
	return (int)yabro_syscall3(452,0,0,0);
}
static inline int yabro_input_wait(void) {
	return (int)yabro_syscall3(453,0,0,0);
}
#endif
