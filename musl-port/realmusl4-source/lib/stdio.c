/* Musl integration source. */
#include "stdio.h"
#include "string.h"
#include "../arch/x86_64/syscall_arch.h"
static void putn(const char*s,size_t n){(void)__yabroos_syscall3(1,1,(long)s,(long)n);}
int puts(const char*s){size_t n=strlen(s);putn(s,n);putn("\n",1);return 0;}
static void put_uint(unsigned long v){char b[32];size_t i=0;if(!v){putn("0",1);return;}while(v){b[i++]=(char)('0'+v%10);v/=10;}while(i)putn(&b[--i],1);}
int printf(const char*fmt,...){__builtin_va_list ap;__builtin_va_start(ap,fmt);int count=0;for(size_t i=0;fmt[i];i++){if(fmt[i]!='%'){putn(&fmt[i],1);count++;continue;}i++;if(fmt[i]=='s'){const char*s=__builtin_va_arg(ap,const char*);size_t n=strlen(s);putn(s,n);count+=(int)n;}else if(fmt[i]=='d'){long v=__builtin_va_arg(ap,long);if(v<0){putn("-",1);v=-v;}put_uint((unsigned long)v);}else if(fmt[i]=='u'){put_uint(__builtin_va_arg(ap,unsigned long));}else if(fmt[i]=='c'){char c=(char)__builtin_va_arg(ap,int);putn(&c,1);count++;}else{putn("%",1);putn(&fmt[i],1);count+=2;}}__builtin_va_end(ap);return count;}
