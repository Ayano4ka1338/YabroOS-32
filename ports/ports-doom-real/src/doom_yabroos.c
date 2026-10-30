#include <stdint.h>
#include "yabro_graphics.h"

#define DOOM_SCREEN_BYTES (320ULL * 200ULL)
#define DOOM_SCREEN_POOL_BYTES (DOOM_SCREEN_BYTES * 4ULL)
static unsigned char doom_screen_pool[DOOM_SCREEN_POOL_BYTES] __attribute__((aligned(4096)));
static unsigned char doom_screen_buffer_static[DOOM_SCREEN_BYTES] __attribute__((aligned(16)));
static unsigned char doom_final_screen_static[DOOM_SCREEN_BYTES * 4ULL] __attribute__((aligned(16)));
static unsigned char* const screen_buffer = doom_screen_buffer_static;
static unsigned char* const final_screen_buffer = doom_final_screen_static;

typedef struct doom_alloc_header {
    uint64_t magic;
    uint64_t map_size;
} doom_alloc_header_t;
#define DOOM_ALLOC_MAGIC 0x594142524F4D414CULL

#define DOOM_IMPLEMENTATION
#include "PureDOOM.h"

#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3
#define SYS_POLL       7
#define SYS_SOCKET    41
#define SYS_CONNECT   42
#define SYS_LSEEK      8
#define SYS_MMAP       9
#define SYS_BRK       12
#ifndef PROT_READ
#define PROT_READ 1ULL
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 2ULL
#endif
#ifndef PROT_EXEC
#define PROT_EXEC 4ULL
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 2ULL
#endif
#ifndef MAP_ANON
#define MAP_ANON 0x20ULL
#endif
#define SYS_MUNMAP     11

#define SYS_EXIT       60
#define SYS_CLOCK_GETTIME 228
#define SYS_NANOSLEEP 35

#define O_RDONLY       0
#define O_WRONLY       1
#define O_CREAT        64
#define O_TRUNC        512

#define SEEK_SET       0
#define SEEK_CUR       1
#define SEEK_END       2

#define POLLIN         1
#define AF_UNIX        1
#define SOCK_STREAM    1

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#define EV_SYN         0
#define EV_KEY         1

#define YABRO_KEY_ESC        1
#define YABRO_KEY_1          2
#define YABRO_KEY_2          3
#define YABRO_KEY_3          4
#define YABRO_KEY_4          5
#define YABRO_KEY_5          6
#define YABRO_KEY_6          7
#define YABRO_KEY_7          8
#define YABRO_KEY_8          9
#define YABRO_KEY_9          10
#define YABRO_KEY_0          11
#define YABRO_KEY_MINUS      12
#define YABRO_KEY_EQUAL      13
#define YABRO_KEY_BACKSPACE  14
#define YABRO_KEY_TAB        15
#define YABRO_KEY_Q          16
#define YABRO_KEY_W          17
#define YABRO_KEY_E          18
#define YABRO_KEY_R          19
#define YABRO_KEY_T          20
#define YABRO_KEY_Y          21
#define YABRO_KEY_U          22
#define YABRO_KEY_I          23
#define YABRO_KEY_O          24
#define YABRO_KEY_P          25
#define YABRO_KEY_LBRACKET   26
#define YABRO_KEY_RBRACKET   27
#define YABRO_KEY_ENTER      28
#define YABRO_KEY_LCTRL      29
#define YABRO_KEY_A          30
#define YABRO_KEY_S          31
#define YABRO_KEY_D          32
#define YABRO_KEY_F          33
#define YABRO_KEY_G          34
#define YABRO_KEY_H          35
#define YABRO_KEY_J          36
#define YABRO_KEY_K          37
#define YABRO_KEY_L          38
#define YABRO_KEY_SEMICOLON  39
#define YABRO_KEY_APOSTROPHE 40
#define YABRO_KEY_GRAVE      41
#define YABRO_KEY_LSHIFT     42
#define YABRO_KEY_BACKSLASH  43
#define YABRO_KEY_Z          44
#define YABRO_KEY_X          45
#define YABRO_KEY_C          46
#define YABRO_KEY_V          47
#define YABRO_KEY_B          48
#define YABRO_KEY_N          49
#define YABRO_KEY_M          50
#define YABRO_KEY_COMMA      51
#define YABRO_KEY_DOT        52
#define YABRO_KEY_SLASH      53
#define YABRO_KEY_RSHIFT     54
#define YABRO_KEY_KPASTERISK 55
#define YABRO_KEY_LALT       56
#define YABRO_KEY_SPACE      57
#define YABRO_KEY_CAPSLOCK   58
#define YABRO_KEY_F1         59
#define YABRO_KEY_F2         60
#define YABRO_KEY_F3         61
#define YABRO_KEY_F4         62
#define YABRO_KEY_F5         63
#define YABRO_KEY_F6         64
#define YABRO_KEY_F7         65
#define YABRO_KEY_F8         66
#define YABRO_KEY_F9         67
#define YABRO_KEY_F10        68
#define YABRO_KEY_UP         103
#define YABRO_KEY_LEFT       105
#define YABRO_KEY_RIGHT      106
#define YABRO_KEY_DOWN       108

struct yabro_input_event {
    int64_t tv_sec;
    int64_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} __attribute__((packed));

struct yabro_pollfd {
    int fd;
    short events;
    short revents;
};

struct yabro_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static long sc3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return r;
}


static long sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return r;
}

static long sc4(long n, long a1, long a2, long a3, long a4) {
    long r;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory");
    return r;
}

static void yabro_exit(int code) {
    sc3(SYS_EXIT, code, 0, 0);
    for (;;) {}
}

static uint64_t yabro_monotonic_us(void) {
    struct yabro_timespec ts;
    if (sc3(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&ts, 0) < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

static void doom_print_yabro(const char *s);

static void *doom_malloc_yabro(int size) {
    if (size <= 0) return 0;

    uint64_t want = (uint64_t)size;
    if (want > UINT64_MAX - 4095ULL - 16ULL) return 0;

    uint64_t map_size = (want + 16ULL + 4095ULL) & ~4095ULL;
    long mr = sc6(SYS_MMAP, 0, (long)map_size,
                  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mr < 0) {
        doom_print_yabro("[DOOM malloc] mmap failed\n");
        return 0;
    }

    doom_alloc_header_t *h = (doom_alloc_header_t *)(uintptr_t)mr;
    h->magic = DOOM_ALLOC_MAGIC;
    h->map_size = map_size;

    void *ret = (void *)(uintptr_t)(mr + sizeof(*h));
    doom_memset(ret, 0, size);
    return ret;
}

static void doom_free_yabro(void *ptr) {
    if (!ptr) return;

    doom_alloc_header_t *h =
        (doom_alloc_header_t *)((uintptr_t)ptr - sizeof(*h));

    if (h->magic != DOOM_ALLOC_MAGIC ||
        h->map_size < 4096ULL || (h->map_size & 4095ULL)) {
        doom_print_yabro("[DOOM free] invalid allocation\n");
        return;
    }

    uint64_t map_base = (uint64_t)(uintptr_t)h;
    uint64_t map_size = h->map_size;
    h->magic = 0;
    (void)sc3(SYS_MUNMAP, (long)map_base, (long)map_size, 0);
}

static void doom_print_yabro(const char *s) {
    if (!s) return;
    uint64_t n = 0;
    while (s[n]) n++;
    if (n) sc3(SYS_WRITE, 1, (long)s, (long)n);
}

struct doom_file {
    int fd;
};

static void *doom_open_yabro(const char *path, const char *mode) {
    if (path) {
        const char *p = path;
        while (*p == '/') p++;
        if (p[0] == 'd' && p[1] == 'o' && p[2] == 'o' &&
            p[3] == 'm' && p[4] == '2' && p[5] == '.' &&
            p[6] == 'w' && p[7] == 'a' && p[8] == 'd' &&
            p[9] == 0) {
            path = "/freedoom2.wad";
        }
    }

    int flags = O_RDONLY;
    if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
        flags = O_WRONLY | O_CREAT;
        if (mode[0] == 'w') flags |= O_TRUNC;
    }

    int fd = (int)sc3(SYS_OPEN, (long)path, flags, 0644);
    if (fd < 0) return 0;

    struct doom_file *f = (struct doom_file *)doom_malloc_yabro(sizeof(*f));
    if (!f) {
        sc3(SYS_CLOSE, fd, 0, 0);
        return 0;
    }

    f->fd = fd;
    return f;
}

static void doom_close_yabro(void *handle) {
    struct doom_file *f = (struct doom_file *)handle;
    if (!f) return;
    sc3(SYS_CLOSE, f->fd, 0, 0);
}

static int doom_read_yabro(void *handle, void *buf, int count) {
    struct doom_file *f = (struct doom_file *)handle;
    if (!f || count < 0) return -1;
    return (int)sc3(SYS_READ, f->fd, (long)buf, count);
}

static int doom_write_yabro(void *handle, const void *buf, int count) {
    struct doom_file *f = (struct doom_file *)handle;
    if (!f || count < 0) return -1;
    return (int)sc3(SYS_WRITE, f->fd, (long)buf, count);
}

static int doom_seek_yabro(void *handle, int offset, doom_seek_t origin) {
    struct doom_file *f = (struct doom_file *)handle;
    if (!f) return -1;

    int whence = SEEK_SET;
    if (origin == DOOM_SEEK_CUR) whence = SEEK_CUR;
    else if (origin == DOOM_SEEK_END) whence = SEEK_END;

    return (int)sc3(SYS_LSEEK, f->fd, offset, whence);
}

static int doom_tell_yabro(void *handle) {
    return doom_seek_yabro(handle, 0, DOOM_SEEK_CUR);
}

static int doom_eof_yabro(void *handle) {
    struct doom_file *f = (struct doom_file *)handle;
    if (!f) return 1;

    long cur = sc3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
    if (cur < 0) return 1;

    long end = sc3(SYS_LSEEK, f->fd, 0, SEEK_END);
    if (end < 0) return 1;

    sc3(SYS_LSEEK, f->fd, cur, SEEK_SET);
    return cur >= end;
}


static char doom_home[] = "/";
static char doom_waddir[] = "";

static char *doom_getenv_yabro(const char *var) {
    if (!var) return 0;

    if (var[0] == 'H' && var[1] == 'O' && var[2] == 'M' &&
        var[3] == 'E' && var[4] == 0)
        return doom_home;

    if (var[0] == 'D' && var[1] == 'O' && var[2] == 'O' &&
        var[3] == 'M' && var[4] == 'W' && var[5] == 'A' &&
        var[6] == 'D' && var[7] == 'D' && var[8] == 'I' &&
        var[9] == 'R' && var[10] == 0)
        return doom_waddir;

    return 0;
}

static void doom_gettime_yabro(int *sec, int *usec) {
    struct yabro_timespec ts;
    if (sc3(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&ts, 0) < 0) {
        *sec = 0;
        *usec = 0;
        return;
    }
    *sec = (int)ts.tv_sec;
    *usec = (int)(ts.tv_nsec / 1000);
}

static doom_key_t doom_key_from_linux(uint16_t code) {
    switch (code) {
        case YABRO_KEY_ESC:        return DOOM_KEY_ESCAPE;
        case YABRO_KEY_ENTER:      return DOOM_KEY_ENTER;
        case YABRO_KEY_TAB:        return DOOM_KEY_TAB;
        case YABRO_KEY_BACKSPACE:  return DOOM_KEY_BACKSPACE;
        case YABRO_KEY_SPACE:      return DOOM_KEY_SPACE;
        case YABRO_KEY_APOSTROPHE: return DOOM_KEY_APOSTROPHE;
        case YABRO_KEY_COMMA:      return DOOM_KEY_COMMA;
        case YABRO_KEY_MINUS:      return DOOM_KEY_MINUS;
        case YABRO_KEY_DOT:        return DOOM_KEY_PERIOD;
        case YABRO_KEY_SLASH:      return DOOM_KEY_SLASH;
        case YABRO_KEY_SEMICOLON:  return DOOM_KEY_SEMICOLON;
        case YABRO_KEY_EQUAL:      return DOOM_KEY_EQUALS;
        case YABRO_KEY_LBRACKET:   return DOOM_KEY_LEFT_BRACKET;
        case YABRO_KEY_RBRACKET:   return DOOM_KEY_RIGHT_BRACKET;
        case YABRO_KEY_BACKSLASH:  return '\\';

        case YABRO_KEY_UP:         return DOOM_KEY_UP_ARROW;
        case YABRO_KEY_DOWN:       return DOOM_KEY_DOWN_ARROW;
        case YABRO_KEY_LEFT:       return DOOM_KEY_LEFT_ARROW;
        case YABRO_KEY_RIGHT:      return DOOM_KEY_RIGHT_ARROW;

        case YABRO_KEY_LCTRL:      return DOOM_KEY_CTRL;
        case YABRO_KEY_LALT:       return DOOM_KEY_ALT;
        case YABRO_KEY_LSHIFT:
        case YABRO_KEY_RSHIFT:     return DOOM_KEY_SHIFT;

        case YABRO_KEY_A: return DOOM_KEY_A;
        case YABRO_KEY_B: return DOOM_KEY_B;
        case YABRO_KEY_C: return DOOM_KEY_C;
        case YABRO_KEY_D: return DOOM_KEY_D;
        case YABRO_KEY_E: return DOOM_KEY_E;
        case YABRO_KEY_F: return DOOM_KEY_F;
        case YABRO_KEY_G: return DOOM_KEY_G;
        case YABRO_KEY_H: return DOOM_KEY_H;
        case YABRO_KEY_I: return DOOM_KEY_I;
        case YABRO_KEY_J: return DOOM_KEY_J;
        case YABRO_KEY_K: return DOOM_KEY_K;
        case YABRO_KEY_L: return DOOM_KEY_L;
        case YABRO_KEY_M: return DOOM_KEY_M;
        case YABRO_KEY_N: return DOOM_KEY_N;
        case YABRO_KEY_O: return DOOM_KEY_O;
        case YABRO_KEY_P: return DOOM_KEY_P;
        case YABRO_KEY_Q: return DOOM_KEY_Q;
        case YABRO_KEY_R: return DOOM_KEY_R;
        case YABRO_KEY_S: return DOOM_KEY_S;
        case YABRO_KEY_T: return DOOM_KEY_T;
        case YABRO_KEY_U: return DOOM_KEY_U;
        case YABRO_KEY_Y: return DOOM_KEY_Y;
        case YABRO_KEY_V: return DOOM_KEY_V;
        case YABRO_KEY_W: return DOOM_KEY_W;
        case YABRO_KEY_X: return DOOM_KEY_X;
        case YABRO_KEY_Z: return DOOM_KEY_Z;

        case YABRO_KEY_0: return DOOM_KEY_0;
        case YABRO_KEY_1: return DOOM_KEY_1;
        case YABRO_KEY_2: return DOOM_KEY_2;
        case YABRO_KEY_3: return DOOM_KEY_3;
        case YABRO_KEY_4: return DOOM_KEY_4;
        case YABRO_KEY_5: return DOOM_KEY_5;
        case YABRO_KEY_6: return DOOM_KEY_6;
        case YABRO_KEY_7: return DOOM_KEY_7;
        case YABRO_KEY_8: return DOOM_KEY_8;
        case YABRO_KEY_9: return DOOM_KEY_9;

        case YABRO_KEY_F1:  return DOOM_KEY_F1;
        case YABRO_KEY_F2:  return DOOM_KEY_F2;
        case YABRO_KEY_F3:  return DOOM_KEY_F3;
        case YABRO_KEY_F4:  return DOOM_KEY_F4;
        case YABRO_KEY_F5:  return DOOM_KEY_F5;
        case YABRO_KEY_F6:  return DOOM_KEY_F6;
        case YABRO_KEY_F7:  return DOOM_KEY_F7;
        case YABRO_KEY_F8:  return DOOM_KEY_F8;
        case YABRO_KEY_F9:  return DOOM_KEY_F9;
        case YABRO_KEY_F10: return DOOM_KEY_F10;
        case 87: return DOOM_KEY_F11;
        case 88: return DOOM_KEY_F12;

        default: return DOOM_KEY_UNKNOWN;
    }
}

static int doom_poll_x11_events(int fd);

#define X11_SETUP_CAP (64u * 1024u)
#define DOOM_X11_WIN_W 640u
#define DOOM_X11_WIN_H 400u
#define DOOM_X11_PIXELS (DOOM_X11_WIN_W * DOOM_X11_WIN_H)
#define DOOM_X11_IMAGE_BYTES (DOOM_X11_PIXELS * 4u)
#define X11_OP_CREATE_WINDOW 1u
#define X11_OP_CHANGE_WINDOW_ATTRIBUTES 2u
#define X11_OP_CREATE_GC 55u
#define X11_OP_MAP_WINDOW 8u
#define X11_OP_CHANGE_PROPERTY 18u
#define X11_OP_GET_INPUT_FOCUS 43u
#define X11_EVENT_KEY_PRESS 2u
#define X11_EVENT_KEY_RELEASE 3u
#define X11_EVENT_FOCUS_OUT 10u
#define X11_EVENT_DESTROY_NOTIFY 17u
#define X11_CW_EVENT_MASK 0x00000800u
#define X11_KEY_PRESS_MASK 0x00000001u
#define X11_KEY_RELEASE_MASK 0x00000002u
#define X11_FOCUS_CHANGE_MASK 0x00200000u
#define X11_STRUCTURE_NOTIFY_MASK 0x00020000u
#define X11_OP_PUT_IMAGE 72u
#define X11_Z_PIXMAP 2u

struct doom_sockaddr_un { uint16_t family; char path[108]; };
static uint8_t doom_x11_setup_body[X11_SETUP_CAP] __attribute__((aligned(16)));
static uint8_t doom_x11_image[DOOM_X11_IMAGE_BYTES] __attribute__((aligned(16)));
static int doom_x11_fd = -1;
static uint32_t doom_x11_window, doom_x11_gc;
static uint32_t doom_x11_rmask, doom_x11_gmask, doom_x11_bmask;
static uint16_t doom_x11_width, doom_x11_height, doom_x11_max_request;
static uint8_t doom_x11_depth, doom_x11_bpp, doom_x11_pad, doom_x11_image_order;
static uint64_t doom_x11_row_bytes;
static uint8_t doom_x11_event_buf[32];
static uint32_t doom_x11_event_used;
static int doom_x11_close_requested;

static uint16_t xrd16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t xrd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void xwr16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void xwr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t xpad4(uint32_t n){return (n+3u)&~3u;}

static int doom_x11_read_full(int fd,void *buf,uint64_t size){
    uint8_t *p=(uint8_t*)buf;uint64_t done=0;
    while(done<size){long r=sc3(SYS_READ,fd,(long)(p+done),(long)(size-done));if(r<=0)return -1;done+=(uint64_t)r;}
    return 0;
}
static int doom_x11_write_full(int fd,const void *buf,uint64_t size){
    const uint8_t *p=(const uint8_t*)buf;uint64_t done=0;
    while(done<size){long r=sc3(SYS_WRITE,fd,(long)(p+done),(long)(size-done));if(r<=0)return -1;done+=(uint64_t)r;}
    return 0;
}
static uint32_t doom_x11_make_id(uint32_t base,uint32_t mask,uint32_t serial){
    if(!mask || (base & mask)) return 0;
    uint32_t low=0,src=1;
    for(uint32_t bit=1;bit;bit<<=1){if(mask&bit){if(serial&src)low|=bit;src<<=1;if(!src)break;}}
    if(!low)return 0;
    return (base&~mask)|low;
}
static uint32_t doom_x11_pack_component(uint8_t c,uint32_t mask){
    if(!mask)return 0;
    uint32_t orig=mask,shift=0;
    while((mask&1u)==0u){mask>>=1;shift++;}
    uint64_t scaled=((uint64_t)c*(uint64_t)mask+127u)/255u;
    return ((uint32_t)(scaled<<shift))&orig;
}
static void doom_x11_log_num(const char *prefix,uint64_t v){
    char b[24];long n=0;
    do{b[n++]=(char)('0'+v%10u);v/=10u;}while(v&&n<(long)sizeof(b)-1);
    for(long i=0;i<n/2;i++){char t=b[i];b[i]=b[n-1-i];b[n-1-i]=t;}
    b[n]=0;doom_print_yabro(prefix);doom_print_yabro(b);doom_print_yabro("\n");
}

static int doom_poll_x11_events(int fd){
    int handled=0;
    for(uint32_t budget=0;budget<64;budget++){
        struct yabro_pollfd p={fd,POLLIN,0};
        long ready=sc3(SYS_POLL,(long)&p,1,0);
        if(ready<=0 || !(p.revents&POLLIN)) break;
        long n=sc3(SYS_READ,fd,(long)(doom_x11_event_buf+doom_x11_event_used),32u-doom_x11_event_used);
        if(n<=0) return -1;
        doom_x11_event_used+=(uint32_t)n;
        if(doom_x11_event_used<32) continue;

        uint8_t type=doom_x11_event_buf[0]&0x7fu;
        if(type==X11_EVENT_KEY_PRESS || type==X11_EVENT_KEY_RELEASE){
            uint8_t xcode=doom_x11_event_buf[1];
            if(xcode>=9){
                doom_key_t key=doom_key_from_linux((uint16_t)(xcode-8));
                if(key!=DOOM_KEY_UNKNOWN){
                    if(type==X11_EVENT_KEY_PRESS) doom_key_down(key);
                    else doom_key_up(key);
                    handled++;
                }
            }
        } else if(type==X11_EVENT_FOCUS_OUT){
            for(uint16_t code=1;code<256;code++){
                doom_key_t key=doom_key_from_linux(code);
                if(key!=DOOM_KEY_UNKNOWN) doom_key_up(key);
            }
        } else if(type==X11_EVENT_DESTROY_NOTIFY){
            doom_x11_close_requested=1;
        }
        doom_x11_event_used=0;
        if(doom_x11_close_requested) return -1;
    }
    return handled;
}

static int doom_x11_open_window(void){
    int fd=(int)sc3(SYS_SOCKET,AF_UNIX,SOCK_STREAM,0);
    if(fd<0){doom_print_yabro("[DOOM X11] socket failed\n");return -1;}
    struct doom_sockaddr_un sa;
    for(uint64_t i=0;i<sizeof(sa);i++)((uint8_t*)&sa)[i]=0;
    sa.family=AF_UNIX;
    const char *path="/tmp/.X11-unix/X0";uint32_t pathlen=0;
    while(path[pathlen]&&pathlen<107){sa.path[pathlen]=path[pathlen];pathlen++;}
    long cr=sc3(SYS_CONNECT,fd,(long)&sa,(long)(2u+pathlen+1u));
    if(cr<0){doom_print_yabro("[DOOM X11] connect :0 failed\n");sc3(SYS_CLOSE,fd,0,0);return -1;}

    uint8_t setup[12]={'l',0,11,0,0,0,0,0,0,0,0,0};
    if(doom_x11_write_full(fd,setup,sizeof(setup))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t hdr[8];
    if(doom_x11_read_full(fd,hdr,sizeof(hdr))<0||hdr[0]!=1){doom_print_yabro("[DOOM X11] SetupSuccess missing\n");sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint32_t bodylen=(uint32_t)xrd16(hdr+6)*4u;
    if(bodylen<72u||bodylen>sizeof(doom_x11_setup_body)||doom_x11_read_full(fd,doom_x11_setup_body,bodylen)<0){doom_print_yabro("[DOOM X11] setup body invalid\n");sc3(SYS_CLOSE,fd,0,0);return -1;}
    const uint8_t *b=doom_x11_setup_body;
    uint32_t rid_base=xrd32(b+4),rid_mask=xrd32(b+8);
    doom_x11_max_request=xrd16(b+18);if(!doom_x11_max_request)doom_x11_max_request=65535;
    uint8_t nroots=b[20],nformats=b[21];doom_x11_image_order=b[22];
    uint16_t vendor_len=xrd16(b+16);
    uint32_t fmt_off=32u+xpad4(vendor_len);
    if(fmt_off+(uint32_t)nformats*8u>bodylen){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint32_t screen_off=fmt_off+(uint32_t)nformats*8u;
    if(!nroots||screen_off+40u>bodylen){doom_print_yabro("[DOOM X11] screen missing\n");sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint32_t root=xrd32(b+screen_off),black=xrd32(b+screen_off+12),root_visual=xrd32(b+screen_off+32);
    uint16_t screen_w=xrd16(b+screen_off+20),screen_h=xrd16(b+screen_off+22);
    doom_x11_depth=b[screen_off+38];uint8_t ndepths=b[screen_off+39];
    doom_x11_bpp=0;doom_x11_pad=32;
    for(uint8_t i=0;i<nformats;i++){
        const uint8_t *f=b+fmt_off+(uint32_t)i*8u;
        if(f[0]==doom_x11_depth){doom_x11_bpp=f[1];if(f[2])doom_x11_pad=f[2];break;}
    }
    if(doom_x11_bpp!=32||doom_x11_depth==0||screen_off+40u>bodylen){
        doom_print_yabro("[DOOM X11] unsupported root pixmap format (requires 32bpp)\n");sc3(SYS_CLOSE,fd,0,0);return -1;
    }
    uint32_t voff=screen_off+40u;int have_visual=0;
    for(uint8_t d=0;d<ndepths;d++){
        if(voff+8u>bodylen)break;
        uint8_t ddepth=b[voff];uint16_t nv=xrd16(b+voff+2);voff+=8u;
        if((uint64_t)voff+(uint64_t)nv*24u>bodylen)break;
        for(uint16_t v=0;v<nv;v++){
            const uint8_t *vis=b+voff+(uint32_t)v*24u;
            if(xrd32(vis)==root_visual&&ddepth==doom_x11_depth){
                doom_x11_rmask=xrd32(vis+8);doom_x11_gmask=xrd32(vis+12);doom_x11_bmask=xrd32(vis+16);have_visual=1;
            }
        }
        voff+=(uint32_t)nv*24u;
    }
    if(!have_visual||!doom_x11_rmask||!doom_x11_gmask||!doom_x11_bmask){doom_print_yabro("[DOOM X11] root visual masks not found\n");sc3(SYS_CLOSE,fd,0,0);return -1;}

    doom_x11_width=(screen_w>=704&&screen_h>=480)?DOOM_X11_WIN_W:320u;
    doom_x11_height=(doom_x11_width==DOOM_X11_WIN_W)?DOOM_X11_WIN_H:200u;
    if(doom_x11_width>screen_w)doom_x11_width=320u;
    if(doom_x11_height>screen_h)doom_x11_height=200u;
    uint16_t winx=(screen_w>doom_x11_width)?(uint16_t)((screen_w-doom_x11_width)/2u):0;
    uint16_t winy=(screen_h>doom_x11_height)?(uint16_t)((screen_h-doom_x11_height)/2u):0;
    doom_x11_row_bytes=((uint64_t)doom_x11_width*doom_x11_bpp+doom_x11_pad-1u)/doom_x11_pad*(doom_x11_pad/8u);
    if(!doom_x11_row_bytes||doom_x11_row_bytes>(uint64_t)DOOM_X11_WIN_W*4u){sc3(SYS_CLOSE,fd,0,0);return -1;}

    doom_x11_window=doom_x11_make_id(rid_base,rid_mask,1);
    doom_x11_gc=doom_x11_make_id(rid_base,rid_mask,2);
    if(!doom_x11_window||!doom_x11_gc||doom_x11_window==doom_x11_gc){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t cw[36];for(uint32_t i=0;i<sizeof(cw);i++)cw[i]=0;
    cw[0]=X11_OP_CREATE_WINDOW;cw[1]=0; /* CopyFromParent: matches the proven stock xclock client path. */ xwr16(cw+2,9);xwr32(cw+4,doom_x11_window);xwr32(cw+8,root);
    xwr16(cw+12,winx);xwr16(cw+14,winy);xwr16(cw+16,doom_x11_width);xwr16(cw+18,doom_x11_height);
    xwr16(cw+20,0);xwr16(cw+22,1);xwr32(cw+24,root_visual);xwr32(cw+28,2);xwr32(cw+32,black);
    if(doom_x11_write_full(fd,cw,sizeof(cw))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t gc[20];for(uint32_t i=0;i<sizeof(gc);i++)gc[i]=0;
    gc[0]=X11_OP_CREATE_GC;xwr16(gc+2,5);xwr32(gc+4,doom_x11_gc);xwr32(gc+8,doom_x11_window);xwr32(gc+12,4);xwr32(gc+16,xrd32(b+screen_off+8));
    if(doom_x11_write_full(fd,gc,sizeof(gc))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
   uint8_t prop[28]={X11_OP_CHANGE_PROPERTY,0,7,0,0,0,0,0,39,0,0,0,31,0,0,0,8,0,0,0,4,0,0,0,'D','O','O','M'};
    xwr32(prop+4,doom_x11_window);
    if(doom_x11_write_full(fd,prop,sizeof(prop))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t select_events[16]={X11_OP_CHANGE_WINDOW_ATTRIBUTES,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0};
    xwr32(select_events+4,doom_x11_window);
    xwr32(select_events+8,X11_CW_EVENT_MASK);
    xwr32(select_events+12,X11_KEY_PRESS_MASK|X11_KEY_RELEASE_MASK|X11_FOCUS_CHANGE_MASK|X11_STRUCTURE_NOTIFY_MASK);
    if(doom_x11_write_full(fd,select_events,sizeof(select_events))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t map[8]={X11_OP_MAP_WINDOW,0,2,0,0,0,0,0};xwr32(map+4,doom_x11_window);
    if(doom_x11_write_full(fd,map,sizeof(map))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    uint8_t barrier[4]={X11_OP_GET_INPUT_FOCUS,0,1,0};
    if(doom_x11_write_full(fd,barrier,sizeof(barrier))<0){sc3(SYS_CLOSE,fd,0,0);return -1;}
    int had_error=0,got_reply=0;
    for(uint32_t tries=0;tries<64&&!got_reply;tries++){
        uint8_t packet[32];if(doom_x11_read_full(fd,packet,sizeof(packet))<0)break;
        uint8_t type=packet[0]&0x7fu;
        if(type==0){had_error=1;doom_print_yabro("[DOOM X11] server rejected a window request\n");continue;}
        if(type==1&&xrd16(packet+2)==6){got_reply=1;break;}
    }
    if(!got_reply||had_error){doom_print_yabro("[DOOM X11] create/map synchronization failed\n");sc3(SYS_CLOSE,fd,0,0);return -1;}
    doom_x11_fd=fd;
    doom_x11_log_num("[DOOM X11] managed window id=",doom_x11_window);
    return fd;
}

static int doom_present_x11_buffer(const uint8_t *src){
    if(!src)return -1;
    uint16_t w=doom_x11_width,h=doom_x11_height;
    uint64_t stride=doom_x11_row_bytes;
    for(uint32_t y=0;y<h;y++){
        uint32_t sy=(uint32_t)y*200u/h;
        uint8_t *row=doom_x11_image+(uint64_t)y*stride;
        for(uint32_t x=0;x<w;x++){
            uint32_t sx=x*320u/w;
            const uint8_t *p=src+((uint64_t)sy*320u+sx)*4u;
            uint32_t pixel=doom_x11_pack_component(p[0],doom_x11_rmask)|
                           doom_x11_pack_component(p[1],doom_x11_gmask)|
                           doom_x11_pack_component(p[2],doom_x11_bmask);
            uint8_t *d=row+(uint64_t)x*4u;
            if(doom_x11_image_order==0){d[0]=(uint8_t)pixel;d[1]=(uint8_t)(pixel>>8);d[2]=(uint8_t)(pixel>>16);d[3]=(uint8_t)(pixel>>24);}
            else {d[0]=(uint8_t)(pixel>>24);d[1]=(uint8_t)(pixel>>16);d[2]=(uint8_t)(pixel>>8);d[3]=(uint8_t)pixel;}
        }
    }
    uint64_t max_bytes=(uint64_t)doom_x11_max_request*4u;
    if(max_bytes<=24u)return -1;max_bytes-=24u;
    uint32_t rows=(uint32_t)(max_bytes/stride);if(!rows)return -1;if(rows>h)rows=h;
    for(uint32_t y=0;y<h;){
        uint32_t ch=rows;if(ch>(uint32_t)h-y)ch=(uint32_t)h-y;
        uint64_t payload=stride*ch,total=24u+payload;
        if((total&3u)||total/4u>doom_x11_max_request||total/4u>65535u)return -1;
        uint8_t q[24];for(uint32_t i=0;i<sizeof(q);i++)q[i]=0;
        q[0]=X11_OP_PUT_IMAGE;q[1]=X11_Z_PIXMAP;xwr16(q+2,(uint16_t)(total/4u));
        xwr32(q+4,doom_x11_window);xwr32(q+8,doom_x11_gc);
        xwr16(q+12,w);xwr16(q+14,(uint16_t)ch);xwr16(q+16,0);xwr16(q+18,(uint16_t)y);
        q[20]=0;q[21]=doom_x11_depth;
        if(doom_x11_write_full(doom_x11_fd,q,sizeof(q))<0||
           doom_x11_write_full(doom_x11_fd,doom_x11_image+(uint64_t)y*stride,payload)<0)return -1;
        y+=ch;
    }
    return 0;
}

static void yabro_frame_pace(uint64_t *next_frame_us,int xfd){
    const uint64_t frame_us=1000000ULL/35ULL;
    uint64_t now=yabro_monotonic_us();
    if(*next_frame_us==0){
        *next_frame_us=now+frame_us;
    }else if(now>=*next_frame_us){
        do{*next_frame_us+=frame_us;}while(*next_frame_us<=now);
    }
    while(now<*next_frame_us){
        uint64_t rem=*next_frame_us-now;
        int timeout_ms=(int)((rem+999ULL)/1000ULL);if(timeout_ms<1)timeout_ms=1;
        struct yabro_pollfd p={xfd,POLLIN,0};
        long rc=sc3(SYS_POLL,(long)&p,1,timeout_ms);
        if(rc>0&&(p.revents&POLLIN) && doom_poll_x11_events(xfd)<0) yabro_exit(0);
        now=yabro_monotonic_us();
        if(!now)break;
    }
    *next_frame_us+=frame_us;
}

void _start(void) {
    int wadfd=(int)sc3(SYS_OPEN,(long)"/freedoom2.wad",O_RDONLY,0);
    if(wadfd<0){doom_print_yabro("[DOOM] missing /freedoom2.wad; rebuild the image or copy an IWAD to /freedoom2.wad\n");yabro_exit(2);}
    uint8_t wad_magic[4]={0,0,0,0};
    long wad_read=sc3(SYS_READ,wadfd,(long)wad_magic,4);
    sc3(SYS_CLOSE,wadfd,0,0);
    if(wad_read!=4 || wad_magic[0]!='I' || wad_magic[1]!='W' || wad_magic[2]!='A' || wad_magic[3]!='D'){
        doom_print_yabro("[DOOM] /freedoom2.wad is not a valid IWAD\n");yabro_exit(2);
    }
    int xfd=doom_x11_open_window();
    if(xfd<0){doom_print_yabro("[DOOM] cannot create X11 window; check DISPLAY/Xorg\n");yabro_exit(3);}

    doom_set_print(doom_print_yabro);
    doom_set_malloc(doom_malloc_yabro,doom_free_yabro);
    doom_set_file_io(doom_open_yabro,doom_close_yabro,doom_read_yabro,doom_write_yabro,
                     doom_seek_yabro,doom_tell_yabro,doom_eof_yabro);
    doom_set_gettime(doom_gettime_yabro);
    doom_set_getenv(doom_getenv_yabro);
    doom_set_exit(yabro_exit);
    doom_set_default_int("key_up",DOOM_KEY_W);
    doom_set_default_int("key_down",DOOM_KEY_S);
    doom_set_default_int("key_strafeleft",DOOM_KEY_A);
    doom_set_default_int("key_straferight",DOOM_KEY_D);
    doom_set_default_int("key_use",DOOM_KEY_E);
    doom_set_default_int("mouse_move",0);

    char *argv[]={(char*)"DOOM.ELF",(char*)"-iwad",(char*)"/doom2.wad",0};
    doom_set_resolution(320,200);
    doom_init(3,argv,0);
    doom_print_yabro("[VIDEO] Doom initialized, entering loop\n");
    doom_print_yabro("[DOOM X11] presenting frames in a managed window\n");
    uint64_t next_frame_us=0;
    int input_logged=0,update_logged=0,frame_logged=0;
    for(;;){
        if(doom_poll_x11_events(xfd)<0 || doom_x11_close_requested) yabro_exit(0);
        if(!input_logged){doom_print_yabro("[DOOM LOOP] X11 input polling returned\n");input_logged=1;}
        doom_update();
        if(!update_logged){doom_print_yabro("[DOOM LOOP] update returned\n");update_logged=1;}
        const uint8_t *frame=doom_get_framebuffer(4);
        if(!frame||doom_present_x11_buffer(frame)<0){doom_print_yabro("[DOOM X11] connection closed or PutImage failed; exiting\n");yabro_exit(0);}
        if(!frame_logged){doom_print_yabro("[DOOM LOOP] X11 PutImage returned\n");frame_logged=1;}
        yabro_frame_pace(&next_frame_us,xfd);
    }
}
