
#ifndef YABROOS_MUSL_COMPAT_H
#define YABROOS_MUSL_COMPAT_H

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef long time_t;
typedef unsigned long uintptr_t;

extern int errno;

void *malloc(size_t);
void free(void *);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);

long write(int, const void *, size_t);
long read(int, void *, size_t);
long close(int);
long lseek(int, off_t, int);
long open(const char *, int, int);
long stat(const char *, void *);
long fstat(int, void *);
int getpid(void);
int getppid(void);
void _exit(int);
int fork(void);
int execve(const char *, char *const[], char *const[]);
int waitpid(int, int *, int);
int dup(int);
int dup2(int, int);
void *yabroos_mmap(size_t);
int yabroos_munmap(void *, size_t);
long brk(void *);
long clock_gettime(int, void *);
long nanosleep(const void *, void *);
int isatty(int);
long ioctl(int, unsigned long, unsigned long);
long getrandom(void *, size_t, unsigned);

char *getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *);

#endif
