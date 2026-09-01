
#include "musl_compat.h"
#include "string.h"
#include "../arch/x86_64/syscall_arch.h"

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_stat 4
#define SYS_fstat 5
#define SYS_lseek 8
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_brk 12
#define SYS_ioctl 16
#define SYS_dup 32
#define SYS_dup2 33
#define SYS_nanosleep 35
#define SYS_getpid 39
#define SYS_fork 57
#define SYS_execve 59
#define SYS_wait4 61
#define SYS_getppid 110
#define SYS_clock_gettime 228
#define SYS_exit_group 231
#define SYS_getrandom 318

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define ENOMEM 12
#define EINVAL 22
#define ENOENT 2
#define EBADF 9
#define ENOSYS 38
#define ENOTTY 25
#define EFAULT 14

int errno;

typedef struct { size_t size; } hdr_t;
static unsigned char *heap_cur, *heap_end;
static int heap_init;

static long fail_errno(long r) {
	if (r >= 0) return r;
	errno = (int)(-r);
	return -1;
}

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15u; }
static long sys_brk(unsigned long p) { return __yabroos_syscall1(SYS_brk, (long)p); }
static void heap_setup(void) {
	if (heap_init) return;
	unsigned long b = (unsigned long)sys_brk(0);
	heap_cur = (unsigned char *)b;
	heap_end = heap_cur;
	heap_init = 1;
}

void *malloc(size_t n) {
	if (!n) n = 1;
	heap_setup();
	size_t need = align16(n + sizeof(hdr_t));
	if ((size_t)(heap_end - heap_cur) < need) {
		size_t grow = (need + 4095u) & ~(size_t)4095u;
		unsigned char *want = heap_end + grow;
		if (sys_brk((unsigned long)want) != (long)want) {
			errno = ENOMEM;
			return 0;
		}
		heap_end = want;
	}
	hdr_t *h = (hdr_t *)heap_cur;
	h->size = n;
	heap_cur += need;
	return (void *)(h + 1);
}

void free(void *p) { (void)p; }

void *calloc(size_t n, size_t s) {
	if (s && n > (size_t)-1 / s) { errno = ENOMEM; return 0; }
	size_t z = n * s;
	unsigned char *p = (unsigned char *)malloc(z);
	if (!p) return 0;
	memset(p, 0, z);
	return p;
}

void *realloc(void *p, size_t n) {
	if (!p) return malloc(n);
	if (!n) { free(p); return 0; }
	hdr_t *h = ((hdr_t *)p) - 1;
	void *q = malloc(n);
	if (!q) return 0;
	size_t c = h->size < n ? h->size : n;
	memcpy(q, p, c);
	return q;
}

long write(int fd, const void *buf, size_t n) {
	return fail_errno(__yabroos_syscall3(SYS_write, fd, (long)buf, (long)n));
}
long read(int fd, void *buf, size_t n) {
	return fail_errno(__yabroos_syscall3(SYS_read, fd, (long)buf, (long)n));
}
long close(int fd) {
	return fail_errno(__yabroos_syscall1(SYS_close, fd));
}
long open(const char *path, int flags, int mode) {
	return fail_errno(__yabroos_syscall3(SYS_open, (long)path, flags, mode));
}
long stat(const char *path, void *st) {
	return fail_errno(__yabroos_syscall2(SYS_stat, (long)path, (long)st));
}
long fstat(int fd, void *st) {
	return fail_errno(__yabroos_syscall2(SYS_fstat, fd, (long)st));
}
long lseek(int fd, off_t off, int whence) {
	return fail_errno(__yabroos_syscall3(SYS_lseek, fd, off, whence));
}
int getpid(void) { return (int)__yabroos_syscall0(SYS_getpid); }
int getppid(void) { return (int)__yabroos_syscall0(SYS_getppid); }

void _exit(int code) {
	__yabroos_syscall1(SYS_exit_group, code);
	for (;;) __asm__ volatile("hlt");
}

int fork(void) {
	long r = __yabroos_syscall0(SYS_fork);
	return (int)fail_errno(r);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
	long r = __yabroos_syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
	return (int)fail_errno(r);
}

int waitpid(int pid, int *status, int options) {
	long r = __yabroos_syscall4(SYS_wait4, pid, (long)status, options, 0);
	return (int)fail_errno(r);
}

int dup(int oldfd) { return (int)fail_errno(__yabroos_syscall1(SYS_dup, oldfd)); }
int dup2(int oldfd, int newfd) { return (int)fail_errno(__yabroos_syscall2(SYS_dup2, oldfd, newfd)); }

void *yabroos_mmap(size_t len) {
	long r = __yabroos_syscall6(SYS_mmap, 0, (long)len,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r < 0) { errno = (int)(-r); return 0; }
	return (void *)r;
}
int yabroos_munmap(void *p, size_t len) {
	return (int)fail_errno(__yabroos_syscall2(SYS_munmap, (long)p, (long)len));
}
long brk(void *p) { return fail_errno(sys_brk((unsigned long)p)); }
long clock_gettime(int id, void *tp) {
	return fail_errno(__yabroos_syscall2(SYS_clock_gettime, id, (long)tp));
}
long nanosleep(const void *req, void *rem) {
	return fail_errno(__yabroos_syscall2(SYS_nanosleep, (long)req, (long)rem));
}
long ioctl(int fd, unsigned long req, unsigned long arg) {
	return fail_errno(__yabroos_syscall3(SYS_ioctl, fd, req, arg));
}

typedef struct { unsigned short rows, cols, xpixel, ypixel; } yabroos_winsize;

int isatty(int fd) {
	yabroos_winsize ws;
	long r = __yabroos_syscall3(SYS_ioctl, fd, 0x5413, (long)&ws);
	if (r == 0) return 1;
	if (r < 0) {
		if (-r == ENOTTY) return 0;
		errno = (int)(-r);
	}
	return 0;
}

static char **env_vec;
static size_t env_count;
static int env_ready;
extern char **__yabroos_envp;

static void env_init(void) {
	if (env_ready) return;
	env_ready = 1;
	env_vec = __yabroos_envp;
	if (!env_vec) { env_count = 0; return; }
	while (env_vec[env_count]) env_count++;
}

static int env_name_len(const char *s) {
	int n = 0;
	while (s[n] && s[n] != '=') n++;
	return n;
}

static int env_find(const char *name) {
	env_init();
	int nl = (int)strlen(name);
	for (size_t i = 0; i < env_count; i++) {
		const char *e = env_vec[i];
		if (env_name_len(e) == nl && strncmp(e, name, (size_t)nl) == 0) return (int)i;
	}
	return -1;
}

char *getenv(const char *name) {
	int i = env_find(name);
	if (i < 0) return 0;
	char *e = env_vec[i];
	int n = env_name_len(e);
	return e[n] == '=' ? e + n + 1 : 0;
}

int setenv(const char *name, const char *value, int overwrite) {
	if (!name || !*name || strchr(name, '=') || !value) { errno = EINVAL; return -1; }
	env_init();
	int i = env_find(name);
	if (i >= 0 && !overwrite) return 0;
	int nl = (int)strlen(name), vl = (int)strlen(value);
	char *s = (char *)malloc((size_t)nl + (size_t)vl + 2);
	if (!s) return -1;
	memcpy(s, name, (size_t)nl);
	s[nl] = '=';
	memcpy(s + nl + 1, value, (size_t)vl + 1);
	if (i >= 0) { env_vec[i] = s; return 0; }
	char **nv = (char **)malloc((env_count + 2) * sizeof(char *));
	if (!nv) return -1;
	for (size_t j = 0; j < env_count; j++) nv[j] = env_vec[j];
	nv[env_count] = s;
	nv[env_count + 1] = 0;
	env_vec = nv;
	env_count++;
	return 0;
}

int unsetenv(const char *name) {
	if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
	int i = env_find(name);
	if (i < 0) return 0;
	for (size_t j = (size_t)i; j + 1 < env_count; j++) env_vec[j] = env_vec[j + 1];
	env_vec[--env_count] = 0;
	return 0;
}
long getrandom(void *buf, size_t len, unsigned flags)
{
	return fail_errno(
		__yabroos_syscall3(
			SYS_getrandom,
			(long)buf,
			(long)len,
			(long)flags
		)
	);
}
