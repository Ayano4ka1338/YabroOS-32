#include <stdint.h>
#include "yabro_graphics.h"

#define DOOM_SCREEN_BYTES (320ULL * 200ULL)
#define DOOM_SCREEN_POOL_BYTES (DOOM_SCREEN_BYTES * 4ULL)
static unsigned char doom_screen_pool[DOOM_SCREEN_POOL_BYTES] __attribute__((aligned(4096)));
static unsigned char doom_screen_buffer_static[DOOM_SCREEN_BYTES] __attribute__((aligned(16)));
static unsigned char doom_final_screen_static[DOOM_SCREEN_BYTES * 4ULL] __attribute__((aligned(16)));

typedef struct doom_alloc_header {
	uint64_t magic;
	uint64_t map_size;
} doom_alloc_header_t;
#define DOOM_ALLOC_MAGIC 0x594142524F4D414CULL

#define DOOM_IMPLEMENTATION
#include "PureDOOM.h"

#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_POLL       7
#define SYS_LSEEK      8
#define SYS_MMAP       9
#define SYS_BRK       12
#ifndef PROT_READ
#define PROT_READ 1ULL
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 2ULL
#endif
#ifndef PROT_EXEC
#define PROT_EXEC 4ULL
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 2ULL
#endif
#ifndef MAP_ANON
#define MAP_ANON 0x20ULL
#endif
#define SYS_MUNMAP     11

#define SYS_EXIT       60
#define SYS_CLOCK_GETTIME 228
#define SYS_NANOSLEEP 35

#define O_RDONLY       0
#define O_WRONLY       1
#define O_CREAT        64
#define O_TRUNC        512

#define SEEK_SET       0
#define SEEK_CUR       1
#define SEEK_END       2

#define POLLIN         1

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#define EV_SYN         0
#define EV_KEY         1
#define EV_REL         2

#define REL_X          0
#define REL_Y          1

#define BTN_LEFT       272
#define BTN_RIGHT      273
#define BTN_MIDDLE     274

#define YABRO_KEY_ESC        1
#define YABRO_KEY_1          2
#define YABRO_KEY_2          3
#define YABRO_KEY_3          4
#define YABRO_KEY_4          5
#define YABRO_KEY_5          6
#define YABRO_KEY_6          7
#define YABRO_KEY_7          8
#define YABRO_KEY_8          9
#define YABRO_KEY_9          10
#define YABRO_KEY_0          11
#define YABRO_KEY_MINUS      12
#define YABRO_KEY_EQUAL      13
#define YABRO_KEY_BACKSPACE  14
#define YABRO_KEY_TAB        15
#define YABRO_KEY_Q          16
#define YABRO_KEY_W          17
#define YABRO_KEY_E          18
#define YABRO_KEY_R          19
#define YABRO_KEY_T          20
#define YABRO_KEY_Y          21
#define YABRO_KEY_U          22
#define YABRO_KEY_I          23
#define YABRO_KEY_O          24
#define YABRO_KEY_P          25
#define YABRO_KEY_LBRACKET   26
#define YABRO_KEY_RBRACKET   27
#define YABRO_KEY_ENTER      28
#define YABRO_KEY_LCTRL      29
#define YABRO_KEY_A          30
#define YABRO_KEY_S          31
#define YABRO_KEY_D          32
#define YABRO_KEY_F          33
#define YABRO_KEY_G          34
#define YABRO_KEY_H          35
#define YABRO_KEY_J          36
#define YABRO_KEY_K          37
#define YABRO_KEY_L          38
#define YABRO_KEY_SEMICOLON  39
#define YABRO_KEY_APOSTROPHE 40
#define YABRO_KEY_GRAVE      41
#define YABRO_KEY_LSHIFT     42
#define YABRO_KEY_BACKSLASH  43
#define YABRO_KEY_Z          44
#define YABRO_KEY_X          45
#define YABRO_KEY_C          46
#define YABRO_KEY_V          47
#define YABRO_KEY_B          48
#define YABRO_KEY_N          49
#define YABRO_KEY_M          50
#define YABRO_KEY_COMMA      51
#define YABRO_KEY_DOT        52
#define YABRO_KEY_SLASH      53
#define YABRO_KEY_RSHIFT     54
#define YABRO_KEY_KPASTERISK 55
#define YABRO_KEY_LALT       56
#define YABRO_KEY_SPACE      57
#define YABRO_KEY_CAPSLOCK   58
#define YABRO_KEY_F1         59
#define YABRO_KEY_F2         60
#define YABRO_KEY_F3         61
#define YABRO_KEY_F4         62
#define YABRO_KEY_F5         63
#define YABRO_KEY_F6         64
#define YABRO_KEY_F7         65
#define YABRO_KEY_F8         66
#define YABRO_KEY_F9         67
#define YABRO_KEY_F10        68
#define YABRO_KEY_UP         103
#define YABRO_KEY_LEFT       105
#define YABRO_KEY_RIGHT      106
#define YABRO_KEY_DOWN       108

struct yabro_input_event {
	int64_t tv_sec;
	int64_t tv_usec;
	uint16_t type;
	uint16_t code;
	int32_t value;
} __attribute__((packed));

struct yabro_pollfd {
	int fd;
	short events;
	short revents;
};

struct yabro_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

static long sc3(long n, long a1, long a2, long a3) {
	long r;
	__asm__ volatile("syscall"
		: "=a"(r)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3)
		: "rcx", "r11", "memory");
	return r;
}


static long sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
	long r;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
		: "=a"(r)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
		: "rcx", "r11", "memory");
	return r;
}

static long sc4(long n, long a1, long a2, long a3, long a4) {
	long r;
	register long r10 __asm__("r10") = a4;
	__asm__ volatile("syscall"
		: "=a"(r)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
		: "rcx", "r11", "memory");
	return r;
}

static void yabro_exit(int code) {
	sc3(SYS_EXIT, code, 0, 0);
	for (;;) {}
}

static uint64_t yabro_realtime_sec(void) {
	struct yabro_timespec ts;
	if (sc3(SYS_CLOCK_GETTIME, CLOCK_REALTIME, (long)&ts, 0) < 0)
		return 0;
	return (uint64_t)ts.tv_sec;
}

static uint64_t yabro_rdtsc(void) {
	uint32_t lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}








static uint64_t yabro_tsc_hz(void) {
	static uint64_t hz;
	if (hz)
		return hz;

	uint64_t s0 = yabro_realtime_sec();
	uint64_t t0 = yabro_rdtsc();
	uint64_t s1 = s0;
	while (s1 == s0)
		s1 = yabro_realtime_sec();

	uint64_t t1 = yabro_rdtsc();
	while (s1 < s0 + 2) {
		s1 = yabro_realtime_sec();
	}
	uint64_t t2 = yabro_rdtsc();

	uint64_t ds = s1 - s0;
	uint64_t dt = t2 - t0;
	if (ds == 0 || dt == 0)
		return 1000000000ULL;

	hz = dt / ds;
	if (hz < 100000000ULL || hz > 10000000000ULL)
		hz = 1000000000ULL;
	return hz;
}

static uint64_t yabro_frame_now_ticks(void) {
	return yabro_rdtsc();
}

static void yabro_frame_pace(uint64_t *next_tsc) {
	const uint64_t hz = yabro_tsc_hz();
	const uint64_t frame_tsc = hz / 35ULL;
	uint64_t now = yabro_frame_now_ticks();

	if (*next_tsc == 0)
		*next_tsc = now;

	while ((int64_t)(now - *next_tsc) < 0) {
		__asm__ volatile("pause");
		now = yabro_frame_now_ticks();
	}

	do {
		*next_tsc += frame_tsc;
	} while ((int64_t)(*next_tsc - now) <= 0);
}







static uint32_t *doom_fb_ptr = 0;
static uint64_t doom_fb_size = 0;
static int doom_fb_initialized = 0;

static void doom_video_panic(const char *s);
static void doom_print_yabro(const char *s);

static void *doom_malloc_yabro(int size) {
	if (size <= 0) return 0;






	uint64_t want = (uint64_t)size;
	if (want > UINT64_MAX - 4095ULL - 16ULL) return 0;

	uint64_t map_size = (want + 16ULL + 4095ULL) & ~4095ULL;
	long mr = sc6(SYS_MMAP, 0, (long)map_size,
				  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mr < 0) {
		doom_print_yabro("[DOOM malloc] mmap failed\n");
		return 0;
	}

	doom_alloc_header_t *h = (doom_alloc_header_t *)(uintptr_t)mr;
	h->magic = DOOM_ALLOC_MAGIC;
	h->map_size = map_size;

	void *ret = (void *)(uintptr_t)(mr + sizeof(*h));
	doom_memset(ret, 0, size);
	return ret;
}

static void doom_free_yabro(void *ptr) {
	if (!ptr) return;

	doom_alloc_header_t *h =
		(doom_alloc_header_t *)((uintptr_t)ptr - sizeof(*h));

	if (h->magic != DOOM_ALLOC_MAGIC ||
		h->map_size < 4096ULL || (h->map_size & 4095ULL)) {

		doom_print_yabro("[DOOM free] invalid allocation\n");
		return;
	}

	uint64_t map_base = (uint64_t)(uintptr_t)h;
	uint64_t map_size = h->map_size;
	h->magic = 0;
	(void)sc3(SYS_MUNMAP, (long)map_base, (long)map_size, 0);
}

static void doom_print_yabro(const char *s) {
	if (!s) return;
	uint64_t n = 0;
	while (s[n]) n++;
	if (n) sc3(SYS_WRITE, 1, (long)s, (long)n);
}

static void doom_video_panic(const char *s) {
	doom_print_yabro(s);
	yabro_exit(3);
}

struct doom_file {
	int fd;
};

static void *doom_open_yabro(const char *path, const char *mode) {





	if (path) {
		const char *p = path;
		while (*p == '/') p++;
		if (p[0] == 'd' && p[1] == 'o' && p[2] == 'o' &&
			p[3] == 'm' && p[4] == '2' && p[5] == '.' &&
			p[6] == 'w' && p[7] == 'a' && p[8] == 'd' &&
			p[9] == 0) {
			path = "/freedoom2.wad";
		}
	}

	int flags = O_RDONLY;
	if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
		flags = O_WRONLY | O_CREAT;
		if (mode[0] == 'w') flags |= O_TRUNC;
	}

	int fd = (int)sc3(SYS_OPEN, (long)path, flags, 0644);
	if (fd < 0) return 0;

	struct doom_file *f = (struct doom_file *)doom_malloc_yabro(sizeof(*f));
	if (!f) {
		sc3(SYS_CLOSE, fd, 0, 0);
		return 0;
	}

	f->fd = fd;
	return f;
}

static void doom_close_yabro(void *handle) {
	struct doom_file *f = (struct doom_file *)handle;
	if (!f) return;
	sc3(SYS_CLOSE, f->fd, 0, 0);
}

static int doom_read_yabro(void *handle, void *buf, int count) {
	struct doom_file *f = (struct doom_file *)handle;
	if (!f || count < 0) return -1;
	return (int)sc3(SYS_READ, f->fd, (long)buf, count);
}

static int doom_write_yabro(void *handle, const void *buf, int count) {
	struct doom_file *f = (struct doom_file *)handle;
	if (!f || count < 0) return -1;
	return (int)sc3(SYS_WRITE, f->fd, (long)buf, count);
}

static int doom_seek_yabro(void *handle, int offset, doom_seek_t origin) {
	struct doom_file *f = (struct doom_file *)handle;
	if (!f) return -1;

	int whence = SEEK_SET;
	if (origin == DOOM_SEEK_CUR) whence = SEEK_CUR;
	else if (origin == DOOM_SEEK_END) whence = SEEK_END;

	return (int)sc3(SYS_LSEEK, f->fd, offset, whence);
}

static int doom_tell_yabro(void *handle) {
	return doom_seek_yabro(handle, 0, DOOM_SEEK_CUR);
}

static int doom_eof_yabro(void *handle) {
	struct doom_file *f = (struct doom_file *)handle;
	if (!f) return 1;

	long cur = sc3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
	if (cur < 0) return 1;

	long end = sc3(SYS_LSEEK, f->fd, 0, SEEK_END);
	if (end < 0) return 1;

	sc3(SYS_LSEEK, f->fd, cur, SEEK_SET);
	return cur >= end;
}









static char doom_home[] = "/";
static char doom_waddir[] = "";

static char *doom_getenv_yabro(const char *var) {
	if (!var) return 0;

	if (var[0] == 'H' && var[1] == 'O' && var[2] == 'M' &&
		var[3] == 'E' && var[4] == 0)
		return doom_home;

	if (var[0] == 'D' && var[1] == 'O' && var[2] == 'O' &&
		var[3] == 'M' && var[4] == 'W' && var[5] == 'A' &&
		var[6] == 'D' && var[7] == 'D' && var[8] == 'I' &&
		var[9] == 'R' && var[10] == 0)
		return doom_waddir;

	return 0;
}

static void doom_gettime_yabro(int *sec, int *usec) {
	struct yabro_timespec ts;
	if (sc3(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&ts, 0) < 0) {
		*sec = 0;
		*usec = 0;
		return;
	}
	*sec = (int)ts.tv_sec;
	*usec = (int)(ts.tv_nsec / 1000);
}

static doom_key_t doom_key_from_linux(uint16_t code) {
	switch (code) {
		case YABRO_KEY_ESC:        return DOOM_KEY_ESCAPE;
		case YABRO_KEY_ENTER:      return DOOM_KEY_ENTER;
		case YABRO_KEY_TAB:        return DOOM_KEY_TAB;
		case YABRO_KEY_BACKSPACE:  return DOOM_KEY_BACKSPACE;
		case YABRO_KEY_SPACE:      return DOOM_KEY_SPACE;
		case YABRO_KEY_APOSTROPHE: return DOOM_KEY_APOSTROPHE;
		case YABRO_KEY_COMMA:      return DOOM_KEY_COMMA;
		case YABRO_KEY_MINUS:      return DOOM_KEY_MINUS;
		case YABRO_KEY_DOT:        return DOOM_KEY_PERIOD;
		case YABRO_KEY_SLASH:      return DOOM_KEY_SLASH;
		case YABRO_KEY_SEMICOLON:  return DOOM_KEY_SEMICOLON;
		case YABRO_KEY_EQUAL:      return DOOM_KEY_EQUALS;
		case YABRO_KEY_LBRACKET:   return DOOM_KEY_LEFT_BRACKET;
		case YABRO_KEY_RBRACKET:   return DOOM_KEY_RIGHT_BRACKET;
		case YABRO_KEY_BACKSLASH:  return '\\';

		case YABRO_KEY_UP:         return DOOM_KEY_UP_ARROW;
		case YABRO_KEY_DOWN:       return DOOM_KEY_DOWN_ARROW;
		case YABRO_KEY_LEFT:       return DOOM_KEY_LEFT_ARROW;
		case YABRO_KEY_RIGHT:      return DOOM_KEY_RIGHT_ARROW;

		case YABRO_KEY_LCTRL:      return DOOM_KEY_CTRL;
		case YABRO_KEY_LALT:       return DOOM_KEY_ALT;
		case YABRO_KEY_LSHIFT:
		case YABRO_KEY_RSHIFT:     return DOOM_KEY_SHIFT;

		case YABRO_KEY_A: return DOOM_KEY_A;
		case YABRO_KEY_B: return DOOM_KEY_B;
		case YABRO_KEY_C: return DOOM_KEY_C;
		case YABRO_KEY_D: return DOOM_KEY_D;
		case YABRO_KEY_E: return DOOM_KEY_E;
		case YABRO_KEY_F: return DOOM_KEY_F;
		case YABRO_KEY_G: return DOOM_KEY_G;
		case YABRO_KEY_H: return DOOM_KEY_H;
		case YABRO_KEY_I: return DOOM_KEY_I;
		case YABRO_KEY_J: return DOOM_KEY_J;
		case YABRO_KEY_K: return DOOM_KEY_K;
		case YABRO_KEY_L: return DOOM_KEY_L;
		case YABRO_KEY_M: return DOOM_KEY_M;
		case YABRO_KEY_N: return DOOM_KEY_N;
		case YABRO_KEY_O: return DOOM_KEY_O;
		case YABRO_KEY_P: return DOOM_KEY_P;
		case YABRO_KEY_Q: return DOOM_KEY_Q;
		case YABRO_KEY_R: return DOOM_KEY_R;
		case YABRO_KEY_S: return DOOM_KEY_S;
		case YABRO_KEY_T: return DOOM_KEY_T;
		case YABRO_KEY_U: return DOOM_KEY_U;
		case YABRO_KEY_Y: return DOOM_KEY_Y;
		case YABRO_KEY_V: return DOOM_KEY_V;
		case YABRO_KEY_W: return DOOM_KEY_W;
		case YABRO_KEY_X: return DOOM_KEY_X;
		case YABRO_KEY_Z: return DOOM_KEY_Z;

		case YABRO_KEY_0: return DOOM_KEY_0;
		case YABRO_KEY_1: return DOOM_KEY_1;
		case YABRO_KEY_2: return DOOM_KEY_2;
		case YABRO_KEY_3: return DOOM_KEY_3;
		case YABRO_KEY_4: return DOOM_KEY_4;
		case YABRO_KEY_5: return DOOM_KEY_5;
		case YABRO_KEY_6: return DOOM_KEY_6;
		case YABRO_KEY_7: return DOOM_KEY_7;
		case YABRO_KEY_8: return DOOM_KEY_8;
		case YABRO_KEY_9: return DOOM_KEY_9;

		case YABRO_KEY_F1:  return DOOM_KEY_F1;
		case YABRO_KEY_F2:  return DOOM_KEY_F2;
		case YABRO_KEY_F3:  return DOOM_KEY_F3;
		case YABRO_KEY_F4:  return DOOM_KEY_F4;
		case YABRO_KEY_F5:  return DOOM_KEY_F5;
		case YABRO_KEY_F6:  return DOOM_KEY_F6;
		case YABRO_KEY_F7:  return DOOM_KEY_F7;
		case YABRO_KEY_F8:  return DOOM_KEY_F8;
		case YABRO_KEY_F9:  return DOOM_KEY_F9;
		case YABRO_KEY_F10: return DOOM_KEY_F10;
		case 87: return DOOM_KEY_F11;
		case 88: return DOOM_KEY_F12;

		default: return DOOM_KEY_UNKNOWN;
	}
}

static int doom_poll_input(int fd) {
	struct yabro_pollfd p = { fd, POLLIN, 0 };
	long r = sc3(SYS_POLL, (long)&p, 1, 0);
	if (r <= 0 || !(p.revents & POLLIN)) return 0;

	int handled = 0;
	for (int i = 0; i < 32; ++i) {
		struct yabro_input_event e;
		long n = sc3(SYS_READ, fd, (long)&e, sizeof(e));
		if (n != (long)sizeof(e)) break;
		if (e.type != EV_KEY) continue;

		doom_key_t key = doom_key_from_linux(e.code);
		if (key == DOOM_KEY_UNKNOWN) continue;

		if (e.value == 0)
			doom_key_up(key);
		else if (e.value == 1 || e.value == 2)
			doom_key_down(key);

		handled++;
	}
	return handled;
}






#define YABRO_MOUSE_SPEED 6








static int doom_poll_mouse(int fd) {
	if (fd < 0) return 0;

	struct yabro_pollfd p = { fd, POLLIN, 0 };
	long r = sc3(SYS_POLL, (long)&p, 1, 0);
	if (r <= 0 || !(p.revents & POLLIN)) return 0;

	int rel_x = 0;
	int rel_y = 0;
	int handled = 0;


	for (int i = 0; i < 512; ++i) {
		struct yabro_input_event e;
		long n = sc3(SYS_READ, fd, (long)&e, sizeof(e));
		if (n != (long)sizeof(e)) break;

		if (e.type == EV_REL) {
			if (e.code == REL_X) {
				rel_x += (int)e.value;
				handled++;
			} else if (e.code == REL_Y) {
				rel_y += (int)e.value;
				handled++;
			}
		} else if (e.type == EV_KEY) {
			doom_button_t button;
			if (e.code == BTN_LEFT)
				button = DOOM_LEFT_BUTTON;
			else if (e.code == BTN_RIGHT)
				button = DOOM_RIGHT_BUTTON;
			else if (e.code == BTN_MIDDLE)
				button = DOOM_MIDDLE_BUTTON;
			else
				continue;

			if (e.value == 0)
				doom_button_up(button);
			else if (e.value == 1)
				doom_button_down(button);
			else
				continue;

			handled++;
		}
	}


	if (rel_x || rel_y) {
		doom_mouse_move(rel_x * YABRO_MOUSE_SPEED,
						rel_y * YABRO_MOUSE_SPEED);
	}

	return handled;
}

static int doom_map_framebuffer(int fbfd, const struct yabro_fb_info *info) {
	if (doom_fb_ptr && doom_fb_size == info->size) return 0;

	doom_fb_ptr = (uint32_t *)(uintptr_t)
		yabro_mmap_fb(fbfd, info->size, PROT_READ | PROT_WRITE);
	doom_fb_size = info->size;

	if ((uintptr_t)doom_fb_ptr >= 0x0000800000000000ULL)
		return -1;

	return 0;
}

static void doom_draw_frame(int fbfd, const struct yabro_fb_info *info) {
	const uint8_t *src = doom_get_framebuffer(4);

	static int video_debug_once = 0;
	if (!video_debug_once) {
		doom_print_yabro("[VIDEO] first frame\n");
		video_debug_once = 1;
	}
	if (!src) {
		doom_video_panic("[VIDEO] doom framebuffer is NULL\n");
		return;
	}
	if (doom_map_framebuffer(fbfd, info) < 0) yabro_exit(3);

	uint32_t *fb = doom_fb_ptr;
	if (!fb) {
		doom_video_panic("[VIDEO] framebuffer map failed\n");
		return;
	}
	const int sw = 320;
	const int sh = 200;
	int dw = (int)(info->pitch / 4);
	int dh = (int)(info->size / info->pitch);

	if (dw <= 0 || dh <= 0) {
		doom_video_panic("[VIDEO] invalid derived framebuffer size\n");
		return;
	}

	int sx = dw / sw;
	int sy = dh / sh;
	int scale = sx < sy ? sx : sy;
	if (scale < 1) scale = 1;

	int rw = sw * scale;
	int rh = sh * scale;
	int ox = (dw - rw) / 2;
	int oy = (dh - rh) / 2;
	uint64_t stride = info->pitch / 4;
	uint64_t fb_pixels = info->size / 4;

	if (!video_debug_once) {
		doom_print_yabro("[VIDEO] fb geometry check\n");
	}

	if (stride == 0 || stride != (uint64_t)dw || fb_pixels < (uint64_t)dw * (uint64_t)dh) {
		doom_video_panic("[VIDEO] invalid framebuffer geometry\n");
		return;
	}

	if (!doom_fb_initialized) {
		for (int y = 0; y < dh; ++y) {
			uint64_t row_base = (uint64_t)y * stride;
			if (row_base + (uint64_t)dw > fb_pixels) {
				doom_video_panic("[VIDEO] clear overflow\n");
				return;
			}
			uint32_t *row = fb + row_base;
			for (int x = 0; x < dw; ++x) row[x] = 0;
		}
		doom_fb_initialized = 1;
	}

	for (int y = 0; y < rh; ++y) {
		int syy = y / scale;
		uint64_t row_base = (uint64_t)(oy + y) * stride;
		if (row_base + (uint64_t)(ox + rw) > fb_pixels) {
			doom_print_yabro("[VIDEO] draw overflow\n");
			doom_print_yabro("[VIDEO] framebuffer dimensions mismatch\n");
			return;
		}
		uint32_t *row = fb + row_base;

		for (int x = 0; x < rw; ++x) {
			int sxx = x / scale;
			const uint8_t *p = src + ((uint64_t)syy * sw + sxx) * 4;

			row[ox + x] =
				((uint32_t)p[0] << 16) |
				((uint32_t)p[1] << 8)  |
				((uint32_t)p[2])       |
				0xFF000000U;
		}
	}
}

void _start(void) {
	struct yabro_fb_info fb_info;

	int fbfd = yabro_open("/dev/fb0", 2);
	if (fbfd < 0) yabro_exit(2);

	if (yabro_fb_get_info(fbfd, &fb_info) < 0 ||
		fb_info.width == 0 || fb_info.height == 0 ||
		fb_info.bpp != 32 || fb_info.pitch < fb_info.width * 4) {
		yabro_close(fbfd);
		yabro_exit(2);
	}

	int inputfd = yabro_open("/dev/input/event0", O_RDONLY);
	if (inputfd < 0) {
		yabro_close(fbfd);
		yabro_exit(2);
	}


	int mousefd = yabro_open("/dev/input/event1", O_RDONLY);
	if (mousefd < 0) {

		mousefd = -1;
		doom_print_yabro("[INPUT] PS/2 mouse device not available\n");
	} else {
		doom_print_yabro("[INPUT] PS/2 mouse opened\n");
	}

	doom_set_print(doom_print_yabro);
	doom_set_malloc(doom_malloc_yabro, doom_free_yabro);
	doom_set_file_io(
		doom_open_yabro,
		doom_close_yabro,
		doom_read_yabro,
		doom_write_yabro,
		doom_seek_yabro,
		doom_tell_yabro,
		doom_eof_yabro
	);
	doom_set_gettime(doom_gettime_yabro);
	doom_set_getenv(doom_getenv_yabro);
	doom_set_exit(yabro_exit);

	doom_set_default_int("key_up",         DOOM_KEY_W);
	doom_set_default_int("key_down",       DOOM_KEY_S);
	doom_set_default_int("key_strafeleft", DOOM_KEY_A);
	doom_set_default_int("key_straferight",DOOM_KEY_D);
	doom_set_default_int("key_use",        DOOM_KEY_E);
	doom_set_default_int("mouse_move",     1);

	doom_set_default_int("mouse_sensitivity", 9);

	char *argv[] = {
		(char *)"DOOM.ELF",
		(char *)"-iwad",
		(char *)"/doom2.wad",
		0
	};

	doom_set_resolution(320, 200);
	doom_init(3, argv, 0);

	doom_print_yabro("[VIDEO] Doom initialized, entering loop\n");


	singletics = true;
	uint64_t next_frame_tsc = 0;

	for (;;) {











		yabro_frame_pace(&next_frame_tsc);
		doom_poll_input(inputfd);
		doom_poll_mouse(mousefd);
		doom_force_update();
		doom_draw_frame(fbfd, &fb_info);
	}
}
