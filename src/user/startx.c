#include <stdint.h>

#define SYS_write 1
#define SYS_cls 168
#define SYS_execve 59
#define SYS_exit 60

static __attribute__((noinline)) long sc0(long n){
    long r; __asm__ volatile("syscall":"=a"(r):"a"(n):"rcx","r11","memory"); return r;
}
static __attribute__((noinline)) long sc1(long n,long a){
    long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory"); return r;
}
static __attribute__((noinline)) long sc3(long n,long a,long b,long c){
    long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory"); return r;
}
static long slen(const char*s){long n=0;while(s&&s[n])n++;return n;}
static void put(const char*s){(void)sc3(SYS_write,1,(long)s,slen(s));}

static void clear_old_framebuffer(void){
    (void)sc0(SYS_cls);
}

void startx_entry(uint64_t *sp){
    long argc=(long)sp[0];
    if(argc < 1){ put("startx: invalid argc\n"); sc1(SYS_exit,2); for(;;){} }
    char *av[3];
    av[0]=(char*)"YABRO-STARTX-BUILD-20261022-CWM-V14-FB-HANDOFF";
    av[1]=(char*)"/usr/bin/cwm";
    av[2]=0;
    put("[STARTX-CWM-V14-FB-HANDOFF] launching only /usr/bin/cwm; no legacy WM in runtime image\n");
    clear_old_framebuffer();
    char *envp[]={
        (char*)"PATH=/usr/bin:/bin:/",
        (char*)"HOME=/",
        (char*)"XKB_CONFIG_ROOT=/usr/share/X11/xkb",
        (char*)"XKB_DEFAULT_RULES=evdev",
        (char*)"XKB_DEFAULT_MODEL=pc105",
        (char*)"XKB_DEFAULT_LAYOUT=us",
        (char*)"XKB_LOG_LEVEL=100",
        (char*)"XKB_DEBUG=1",
        (char*)"YABRO_STARTX_BUILD_ID=20261022-cwm-v14-fb-handoff",
        0
    };
    long r=sc3(SYS_execve,(long)"/usr/bin/xinit",(long)av,(long)envp);
    put("startx: exec /usr/bin/xinit failed rc=");
    char b[32]; long i=0,j=0; unsigned long x=(unsigned long)(r<0?-r:r);
    if(r<0)b[i++]='-'; if(!x)b[i++]='0'; while(x){b[i+j]=(char)('0'+x%10);j++;x/=10;} b[i+j]=0; put(b); put("\n");
    sc1(SYS_exit,127); for(;;){}
}

__asm__(
".global _start\n.type _start,@function\n_start:\n"
"mov %rsp,%rdi\n"
"and $-16,%rsp\n"
"call startx_entry\n"
"ud2\n"
".size _start,.-_start\n");
