
#ifndef YABROOS_STDIO_H
#define YABROOS_STDIO_H
#include "musl_compat.h"

typedef struct YFILE { int fd; int eof; int err; } FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int feof(FILE *);
int ferror(FILE *);
int fflush(FILE *);

int puts(const char *);
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);

#endif
