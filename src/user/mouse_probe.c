#include <stdint.h>
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_POLL 7
#define SYS_IOCTL 16
#define O_RDONLY 0
#define POLLIN 1
#define EVIOCGVERSION 0x80044501ULL
#define EVIOCGID 0x80084502ULL
#define EVIOCGNAME(n) (0x80004506ULL | (((uint64_t)(n) & 0x3ffULL) << 16))
#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
struct pollfd { int fd; short events, revents; };
struct input_event { int64_t tv_sec,tv_usec; uint16_t type,code; int32_t value; } __attribute__((packed));
struct input_id { uint16_t bustype,vendor,product,version; };
static long sc(long n,long a,long b,long c){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static void out(const char*s){uint64_t n=0;while(s[n])n++;sc(SYS_WRITE,1,(long)s,n);}
static void num(int x){char b[16];int n=0;if(x<0){out("-");x=-x;}if(!x){out("0");return;}while(x){b[n++]=(char)('0'+x%10);x/=10;}while(n) {char c=b[--n];sc(SYS_WRITE,1,(long)&c,1);}}
void _start(void){
 int fd=(int)sc(SYS_OPEN,(long)"/dev/input/event1",O_RDONLY,0);
 if(fd<0){out("MOUSE OPEN FAIL\n");sc(60,1,0,0);}
 uint32_t ver=0; struct input_id id; char name[64]={0};
 if(sc(SYS_IOCTL,fd,EVIOCGVERSION,(long)&ver)<0||sc(SYS_IOCTL,fd,EVIOCGID,(long)&id)<0||sc(SYS_IOCTL,fd,EVIOCGNAME(64),(long)name)<0){out("MOUSE IOCTL FAIL\n");sc(60,1,0,0);}
 out("MOUSE OPEN OK\nMOUSE VERSION OK\nMOUSE ID OK\nMOUSE NAME ");out(name);out("\nMOVE OR CLICK MOUSE...\n");
 struct pollfd p={fd,POLLIN,0};
 for(;;){long r=sc(SYS_POLL,(long)&p,1,0);if(r>0&&p.revents&POLLIN){struct input_event e;while(sc(SYS_READ,fd,(long)&e,sizeof(e))==(long)sizeof(e)){out("EVENT type=");num(e.type);out(" code=");num(e.code);out(" value=");num(e.value);out("\n");} } __asm__ volatile("pause");}
}
