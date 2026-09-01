
#include "stdio.h"
#include "string.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024

static void putn_fd(int fd, const char *s, size_t n) {
	while (n) {
		long r = write(fd, s, n);
		if (r <= 0) return;
		s += r; n -= (size_t)r;
	}
}

static int emit_char(char *dst, size_t cap, size_t *pos, char c) {
	if (dst && *pos + 1 < cap) dst[*pos] = c;
	(*pos)++;
	if (dst && cap) dst[*pos < cap ? *pos : cap - 1] = 0;
	return 1;
}

static void emit_str(char *dst, size_t cap, size_t *pos, const char *s) {
	while (*s) emit_char(dst, cap, pos, *s++);
}

static void emit_uint(char *dst, size_t cap, size_t *pos, unsigned long v, int base) {
	char b[32]; size_t n = 0;
	const char *digits = "0123456789abcdef";
	if (!v) { emit_char(dst, cap, pos, '0'); return; }
	while (v) { b[n++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
	while (n) emit_char(dst, cap, pos, b[--n]);
}

static int format_to(char *dst, size_t cap, const char *fmt, __builtin_va_list ap) {
	size_t pos = 0;
	for (size_t i = 0; fmt[i]; i++) {
		if (fmt[i] != '%') { emit_char(dst, cap, &pos, fmt[i]); continue; }
		i++;
		if (fmt[i] == '%') { emit_char(dst, cap, &pos, '%'); continue; }
		if (fmt[i] == 's') { emit_str(dst, cap, &pos, __builtin_va_arg(ap, const char *)); continue; }
		if (fmt[i] == 'c') { emit_char(dst, cap, &pos, (char)__builtin_va_arg(ap, int)); continue; }
		if (fmt[i] == 'd' || fmt[i] == 'i') {
			int v = __builtin_va_arg(ap, int);
			if (v < 0) { emit_char(dst, cap, &pos, '-'); emit_uint(dst, cap, &pos, (unsigned long)(-(long)v), 10); }
			else emit_uint(dst, cap, &pos, (unsigned long)v, 10);
			continue;
		}
		if (fmt[i] == 'u') { emit_uint(dst, cap, &pos, __builtin_va_arg(ap, unsigned int), 10); continue; }
		if (fmt[i] == 'l' && fmt[i + 1] == 'd') {
			i++; long v = __builtin_va_arg(ap, long);
			if (v < 0) { emit_char(dst, cap, &pos, '-'); emit_uint(dst, cap, &pos, (unsigned long)(-v), 10); }
			else emit_uint(dst, cap, &pos, (unsigned long)v, 10);
			continue;
		}
		if (fmt[i] == 'x') { emit_uint(dst, cap, &pos, __builtin_va_arg(ap, unsigned int), 16); continue; }
		emit_char(dst, cap, &pos, '%'); emit_char(dst, cap, &pos, fmt[i]);
	}
	if (dst && cap) dst[pos < cap ? pos : cap - 1] = 0;
	return (int)pos;
}

int snprintf(char *dst, size_t cap, const char *fmt, ...) {
	__builtin_va_list ap; __builtin_va_start(ap, fmt);
	int n = format_to(dst, cap, fmt, ap);
	__builtin_va_end(ap);
	return n;
}

int printf(const char *fmt, ...) {
	char b[1024];
	__builtin_va_list ap; __builtin_va_start(ap, fmt);
	int n = format_to(b, sizeof(b), fmt, ap);
	__builtin_va_end(ap);
	putn_fd(1, b, (size_t)(n < (int)sizeof(b) ? n : (int)sizeof(b) - 1));
	return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
	if (!f) return -1;
	char b[1024];
	__builtin_va_list ap; __builtin_va_start(ap, fmt);
	int n = format_to(b, sizeof(b), fmt, ap);
	__builtin_va_end(ap);
	size_t len = (size_t)(n < (int)sizeof(b) ? n : (int)sizeof(b) - 1);
	size_t done = fwrite(b, 1, len, f);
	return done == len ? n : -1;
}

int puts(const char *s) {
	size_t n = strlen(s);
	putn_fd(1, s, n); putn_fd(1, "\n", 1);
	return 0;
}

static int mode_flags(const char *m) {
	if (!m || !m[0]) return -1;
	if (m[0] == 'r') return (m[1] == '+') ? O_RDWR : O_RDONLY;
	if (m[0] == 'w') return ((m[1] == '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	if (m[0] == 'a') return ((m[1] == '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	return -1;
}

FILE *fopen(const char *path, const char *mode) {
	int flags = mode_flags(mode);
	if (flags < 0) return 0;
	int fd = (int)open(path, flags, 0666);
	if (fd < 0) return 0;
	FILE *f = (FILE *)malloc(sizeof(FILE));
	if (!f) { close(fd); return 0; }
	f->fd = fd; f->eof = 0; f->err = 0;
	if (mode[0] == 'a') (void)lseek(fd, 0, SEEK_END);
	return f;
}

int fclose(FILE *f) {
	if (!f) return EOF;
	int r = (int)close(f->fd);
	free(f);
	return r < 0 ? EOF : 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
	if (!f || !size) return 0;
	size_t total = size * nmemb;
	size_t done = 0;
	while (done < total) {
		long r = read(f->fd, (char *)ptr + done, total - done);
		if (r < 0) { f->err = 1; break; }
		if (r == 0) { f->eof = 1; break; }
		done += (size_t)r;
	}
	return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
	if (!f || !size) return 0;
	size_t total = size * nmemb, done = 0;
	while (done < total) {
		long r = write(f->fd, (const char *)ptr + done, total - done);
		if (r < 0) { f->err = 1; break; }
		if (r == 0) { f->err = 1; break; }
		done += (size_t)r;
	}
	return done / size;
}

int fseek(FILE *f, long off, int whence) {
	if (!f || lseek(f->fd, off, whence) < 0) { if (f) f->err = 1; return -1; }
	f->eof = 0; return 0;
}
long ftell(FILE *f) { return f ? lseek(f->fd, 0, SEEK_CUR) : -1; }
int feof(FILE *f) { return f ? f->eof : 0; }
int ferror(FILE *f) { return f ? f->err : 1; }
int fflush(FILE *f) { (void)f; return 0; }
