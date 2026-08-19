/* Musl integration source. */
typedef unsigned long size_t;
#include "../arch/x86_64/syscall_arch.h"
#define SYS_read 0
#define SYS_write 1
#define SYS_close 3
#define SYS_brk 12
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_exit_group 231
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20

typedef struct { size_t size; } hdr_t;
static unsigned char *heap_cur, *heap_end; static int heap_init;
static long sys_brk(unsigned long p){return __yabroos_syscall1(SYS_brk,(long)p);}
static void heap_setup(void){if(heap_init)return;unsigned long b=(unsigned long)sys_brk(0);heap_cur=(unsigned char*)b;heap_end=heap_cur;heap_init=1;}
static size_t align16(size_t n){return (n+15u)&~(size_t)15u;}
void *malloc(size_t n){
 if(!n)n=1; heap_setup(); size_t need=align16(n+sizeof(hdr_t));
 if((size_t)(heap_end-heap_cur)<need){size_t grow=(need+4095u)&~(size_t)4095u;unsigned char *want=heap_end+grow;if(sys_brk((unsigned long)want)!=(long)want)return 0;heap_end=want;}
 hdr_t *h=(hdr_t*)heap_cur;h->size=n;heap_cur+=need;return (void*)(h+1);
}
void free(void *p){(void)p;}
void *calloc(size_t n,size_t s){if(s&&n>(size_t)-1/s)return 0;size_t z=n*s;unsigned char*p=(unsigned char*)malloc(z);if(!p)return 0;for(size_t i=0;i<z;i++)p[i]=0;return p;}
void *realloc(void *p,size_t n){if(!p)return malloc(n);if(!n){free(p);return 0;}hdr_t*h=((hdr_t*)p)-1;void*q=malloc(n);if(!q)return 0;size_t c=h->size<n?h->size:n;unsigned char*a=p,*b=q;for(size_t i=0;i<c;i++)b[i]=a[i];return q;}
long write(int fd,const void *buf,size_t n){return __yabroos_syscall3(SYS_write,fd,(long)buf,(long)n);}
long read(int fd,void *buf,size_t n){return __yabroos_syscall3(SYS_read,fd,(long)buf,(long)n);}
long close(int fd){return __yabroos_syscall1(SYS_close,fd);}
void _exit(int code){__yabroos_syscall1(SYS_exit_group,code);for(;;)__asm__ volatile("hlt");}
void *yabroos_mmap(size_t len){long r=__yabroos_syscall6(SYS_mmap,0,(long)len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);return r<0?0:(void*)r;}
int yabroos_munmap(void*p,size_t len){return(int)__yabroos_syscall2(SYS_munmap,(long)p,(long)len);}
