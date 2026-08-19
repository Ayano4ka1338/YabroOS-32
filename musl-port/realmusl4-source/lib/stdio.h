/* Musl integration source. */
#ifndef YABROOS_STDIO_H
#define YABROOS_STDIO_H
#include "musl_compat.h"
int puts(const char *s);
int printf(const char *fmt, ...);
#endif
