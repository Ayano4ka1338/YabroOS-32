
#ifndef YABROOS_STRING_H
#define YABROOS_STRING_H
typedef unsigned long size_t;
size_t strlen(const char*);
void *memset(void*,int,size_t);
void *memcpy(void*,const void*,size_t);
void *memmove(void*,const void*,size_t);
int memcmp(const void*,const void*,size_t);
char *strcpy(char*,const char*);
char *strncpy(char*,const char*,size_t);
int strcmp(const char*,const char*);
int strncmp(const char*,const char*,size_t);
char *strchr(const char*,int);
char *strstr(const char*,const char*);
#endif
