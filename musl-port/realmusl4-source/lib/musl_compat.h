/* Musl integration source. */
#ifndef YABROOS_MUSL_COMPAT_H
#define YABROOS_MUSL_COMPAT_H
typedef unsigned long size_t;
void *malloc(size_t); void free(void*); void *calloc(size_t,size_t); void *realloc(void*,size_t);
long write(int,const void*,size_t); long read(int,void*,size_t); long close(int); void _exit(int);
void *yabroos_mmap(size_t); int yabroos_munmap(void*,size_t);
#endif
