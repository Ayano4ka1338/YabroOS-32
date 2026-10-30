#include <stdint.h>

#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_stat        4
#define SYS_sched_yield 24
#define SYS_fork        57
#define SYS_execve      59
#define SYS_exit        60
#define SYS_wait4       61
#define SYS_kill        62
#define SYS_CONSOLE_FB_RESTORE 454
#define SYS_unlink      87
#define SYS_read        0
#define SYS_sched_yield 24
#define SYS_close       3
#define SYS_socket     41
#define SYS_connect    42
#define SYS_fcntl      72
#define F_SETFL         4
#define O_NONBLOCK      0x800

#define AF_UNIX 1
#define SOCK_STREAM 1

struct kstat { uint64_t dev,ino,nlink,mode,uid,gid,rdev,size,blksize,blocks,atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns; uint32_t unused[2]; };

static __attribute__((noinline)) long sc0(long n){long r;__asm__ volatile("syscall":"=a"(r):"a"(n):"rcx","r11","memory");return r;}
static __attribute__((noinline)) long sc1(long n,long a){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory");return r;}
static __attribute__((noinline)) long sc2(long n,long a,long b){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory");return r;}
static __attribute__((noinline)) long sc3(long n,long a,long b,long c){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static __attribute__((noinline)) long sc4(long n,long a,long b,long c,long d){long r;register long r10 __asm__("r10")=d;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory");return r;}

static char cwm_display_env[32] = "DISPLAY=:0";
static char *cwm_client_envp[] = {
    (char*)"PATH=/usr/bin:/bin:/",
    (char*)"HOME=/",
    (char*)"SHELL=/bin/sh",
    (char*)"TERM=xterm",
    (char*)"TERMINFO=/lib/term",
    cwm_display_env,
    0
};
static long slen(const char*s){long n=0;while(s&&s[n])n++;return n;}
static void put(const char*s){sc3(SYS_write,1,(long)s,slen(s));}
static void copystr(char*d,const char*s,long cap){long i=0;if(cap<=0)return;while(s&&s[i]&&i<cap-1){d[i]=s[i];i++;}d[i]=0;}
static int eq(const char*a,const char*b){long i=0;while(a[i]&&b[i]&&a[i]==b[i])i++;return a[i]==0&&b[i]==0;}
static void restore_console_only(void){

    (void)sc0(SYS_CONSOLE_FB_RESTORE);
}
static void restore_console_and_exit(long status){
    restore_console_only();
    sc1(SYS_exit,status);
    for(;;){}
}
static void die(const char*m){put("xinit: ");put(m);put("\n");restore_console_and_exit(1);}
static int exists(const char*p){struct kstat st;return sc2(SYS_stat,(long)p,(long)&st)==0;}
static void x_memmove(void *dst,const void *src,long n){
    uint8_t *d=(uint8_t*)dst; const uint8_t *s=(const uint8_t*)src;
    if(d==s || n<=0) return;
    if(d<s){ for(long i=0;i<n;i++) d[i]=s[i]; }
    else { for(long i=n-1;i>=0;i--) d[i]=s[i]; }
}
static int make_path(char*out,const char*arg){if(!arg||!arg[0])return -1;if(arg[0]=='/')copystr(out,arg,128);else{out[0]='/';copystr(out+1,arg,127);}return 0;}
static void socket_path(char*out,long num){copystr(out,"/tmp/.X11-unix/X",128);long p=0;while(out[p])p++;char t[12];long k=0;if(num==0)t[k++]='0';else{long x=num;while(x){t[k++]=(char)('0'+x%10);x/=10;}}for(long i=0;i<k;i++)out[p+i]=t[k-1-i];out[p+k]=0;}
static int x11_server_ready(const char *path){
    int fd=(int)sc3(SYS_socket,AF_UNIX,SOCK_STREAM,0);
    if(fd<0)return -1;
    struct { uint16_t family; char path[108]; } sa;
    for(long i=0;i<(long)sizeof(sa);i++)((uint8_t*)&sa)[i]=0;
    sa.family=AF_UNIX;
    long n=0;while(path[n]&&n<107){sa.path[n]=path[n];n++;}
    if(sc3(SYS_connect,fd,(long)&sa,2+n+1)<0){sc1(SYS_close,fd);return -1;}

    uint8_t req[12]={'l',0,11,0,0,0,0,0,0,0,0,0};
    long sent=0,idle=0;
    while(sent<(long)sizeof(req)){
        long r=sc3(SYS_write,fd,(long)(req+sent),(long)sizeof(req)-sent);
        if(r==-11){sc0(SYS_sched_yield);if(++idle>100000){sc1(SYS_close,fd);return -1;}continue;}
        if(r<=0){sc1(SYS_close,fd);return -1;}
        sent+=r;idle=0;
    }
    uint8_t hdr[8];long got=0;idle=0;
    while(got<(long)sizeof(hdr)){
        long r=sc3(SYS_read,fd,(long)(hdr+got),(long)sizeof(hdr)-got);
        if(r==-11){sc0(SYS_sched_yield);if(++idle>100000){sc1(SYS_close,fd);return -1;}continue;}
        if(r<=0){sc1(SYS_close,fd);return -1;}
        got+=r;idle=0;
    }
    sc1(SYS_close,fd);
    return hdr[0]==1 ? 0 : -1;
}
static int display_num(const char*s){if(!s||s[0]!=':')return -1;long n=0,i=1;if(s[i]<'0'||s[i]>'9')return -1;while(s[i]>='0'&&s[i]<='9'){n=n*10+s[i]-'0';i++;}return s[i]==0&&n>=0&&n<=99?(int)n:-1;}

static void x_put_num(long v){
    char b[32]; long i=0,j=0; unsigned long u=(unsigned long)(v<0?-v:v);
    if(v<0) b[i++]='-';
    if(!u) b[i++]='0';
    while(u){ b[i+j]=(char)('0'+(u%10)); j++; u/=10; }
    while(j>0){ b[i++]=b[j-1]; j--; }
    b[i]=0; put(b);
}
static void report_wait_status(const char *label,long st){
    put(label); put(" raw="); x_put_num(st); put("\n");
    if((((unsigned long)st)&0xffUL)==0){
        put("[XINIT] exit_code="); x_put_num((long)(((unsigned long)st>>8)&0xffUL)); put("\n");
    } else {
        put("[XINIT] signal="); x_put_num((long)((unsigned long)st&0xffUL)); put("\n");
    }
}
static void dump_xorg_log_tail(void){
    int fd=(int)sc3(SYS_open,(long)"/var/log/Xorg.0.log",0,0);
    if(fd<0){put("[XINIT] cannot open /var/log/Xorg.0.log\n");return;}
    static uint8_t buf[8192]; long used=0;
    for(;;){
        if(used==(long)sizeof(buf)){ x_memmove(buf,buf+4096,4096); used=4096; }
        long n=sc3(SYS_read,fd,(long)(buf+used),(long)sizeof(buf)-used);
        if(n<=0) break; used+=n;
    }
    sc3(SYS_close,fd,0,0);
    put("----- Xorg.0.log tail -----\n");
    if(used>4096) sc3(SYS_write,1,(long)(buf+used-4096),4096);
    else if(used>0) sc3(SYS_write,1,(long)buf,used);
    put("\n----- end Xorg.0.log tail -----\n");
}
int xinit_main(int argc,char **argv){
    char *client_args[48]; int clientc=0;
    char *server_args[48]; int serverc=0; int after=0;
    for(int i=1;i<argc;i++){
        if(argv[i] && eq(argv[i],"--")){after=1;continue;}
        if(after){if(serverc<47)server_args[serverc++]=argv[i];}
        else {if(clientc<47)client_args[clientc++]=argv[i];}
    }

    char display[16]; copystr(display,":0",sizeof(display));
    int dnum=0;
    for(int i=0;i<serverc;i++){int n=display_num(server_args[i]);if(n>=0){dnum=n;copystr(display,server_args[i],sizeof(display));break;}}
    char sock[128]; socket_path(sock,dnum);

    copystr(cwm_display_env,"DISPLAY=",sizeof(cwm_display_env));
    long display_env_len=8;
    for(long i=0;display[i] && display_env_len<(long)sizeof(cwm_display_env)-1;i++)
        cwm_display_env[display_env_len++]=display[i];
    cwm_display_env[display_env_len]=0;

    if(exists(sock)) {
        long ur = sc1(SYS_unlink,(long)sock);
        if(ur<0) { put("xinit: unlink stale X11 socket failed\n"); }
    }

    char client[128];
    if(clientc==0) copystr(client,"/usr/bin/cwm",sizeof(client));
    else if(make_path(client,client_args[0])<0) die("invalid client");
    put("[XINIT] selected client="); put(client); put("\n");
    if(eq(client,"/usr/bin/cwm")) put("[XINIT-CWM-V13] cwm is the only session WM; legacy framebuffer WM is not built or launched\n");
    if(!exists(client)){put("xinit: client not found: ");put(client);put("\n");sc1(SYS_exit,127);for(;;){}}

    if(!exists("/etc/X11/xorg.conf")){
        put("xinit: missing /etc/X11/xorg.conf\n");
        sc1(SYS_exit,127); for(;;){}
    }

    long xpid=sc0(SYS_fork);
    if(xpid<0) die("fork Xorg failed");
    if(xpid==0){
        char *xav[64]; int xc=0;
        xav[xc++]=(char*)"Xorg";
        int display_inserted=0;
        for(int i=0;i<serverc && xc<61;i++){
            int n=display_num(server_args[i]);
            if(n>=0){if(!display_inserted){xav[xc++]=display;display_inserted=1;}}
            else xav[xc++]=server_args[i];
        }
        if(!display_inserted) xav[xc++]=display;

        if(serverc==0){

            xav[xc++]=(char*)"-br";
            put("[XINIT-CWM-V13] forcing black root background; no legacy WM client\n");
            xav[xc++]=(char*)"-nolisten";
            xav[xc++]=(char*)"tcp";

            xav[xc++]=(char*)"-layout";
            xav[xc++]=(char*)"YabroLayout";

            xav[xc++]=(char*)"-keyboard";
            xav[xc++]=(char*)"YabroKeyboard";
            xav[xc++]=(char*)"-pointer";
            xav[xc++]=(char*)"YabroMouse";
            put("[XINIT] forcing Xorg layout YabroLayout\n");
            put("[XINIT] forcing core keyboard YabroKeyboard and pointer YabroMouse\n");
            xav[xc++]=(char*)"-logverbose";
            xav[xc++]=(char*)"7";
        }

        int explicit_config=0;
        for(int i=0;i<serverc;i++) if(eq(server_args[i],"-config")) explicit_config=1;
        if(!explicit_config){
            xav[xc++]=(char*)"-config";
            xav[xc++]=(char*)"xorg.conf";
            put("[XINIT] forcing Xorg config -config xorg.conf (under /etc/X11)\n");
        }
        xav[xc]=0;
        char envd[32];copystr(envd,"DISPLAY=",sizeof(envd));long p=8;for(long i=0;display[i]&&p<31;i++)envd[p++]=display[i];envd[p]=0;

        char *envp[]={
            (char*)"PATH=/usr/bin:/bin:/",
            (char*)"HOME=/",
            (char*)"XKB_CONFIG_ROOT=/usr/share/X11/xkb",
            (char*)"XKB_DEFAULT_RULES=evdev",
            (char*)"XKB_DEFAULT_MODEL=pc105",
            (char*)"XKB_DEFAULT_LAYOUT=us",
            (char*)"XKB_DEFAULT_VARIANT=",
            (char*)"XKB_DEFAULT_OPTIONS=",
            (char*)"YABRO_XINIT_BUILD_ID=20261014-cwm-v17-desktop-apps",
            envd,0
        };
        long r=sc3(SYS_execve,(long)"/usr/libexec/Xorg",(long)xav,(long)envp);
        put("xinit: exec /usr/libexec/Xorg failed rc=");

        char b[32]; long i=0,j=0; unsigned long v=(unsigned long)(r<0?-r:r); if(r<0)b[i++]='-'; if(!v)b[i++]='0'; while(v){b[i+j]=(char)('0'+v%10);j++;v/=10;} b[i+j]=0; put(b);put("\n");
        sc1(SYS_exit,127);for(;;){}
    }

    put("[XINIT] xorg_pid="); x_put_num(xpid); put("\n");
    put("xinit: Xorg started; starting client\n");

    int socket_seen=0;
    for(long i=0;i<5000000;i++){
        if(exists(sock) && x11_server_ready(sock)==0){ socket_seen=1; break; }
        long st=0;long wr=sc4(SYS_wait4,xpid,(long)&st,1,0);
        if(wr==xpid){
            restore_console_only();
            put("xinit: Xorg exited before creating the display socket\n");
            report_wait_status("[XINIT] Xorg:",st);
            dump_xorg_log_tail();
            sc1(SYS_exit,1); for(;;){}
        }
        sc0(SYS_sched_yield);
    }
    if(!socket_seen){
        put("xinit: timed out waiting for Xorg socket\n");
        sc2(SYS_kill,xpid,15);
        long st=0; (void)sc4(SYS_wait4,xpid,(long)&st,0,0);
        restore_console_only();
        report_wait_status("[XINIT] Xorg after timeout:",st);
        dump_xorg_log_tail();
        sc1(SYS_exit,1);for(;;){}
    }

    put("[XINIT] display socket exists; launching client to perform X11 setup\n");

    long cpid=sc0(SYS_fork);
    if(cpid<0){
        put("xinit: fork client failed rc=");
        char b[32];long i=0,j=0;unsigned long v=(unsigned long)(-cpid);b[i++]='-';if(!v)b[i++]='0';while(v){b[i+j]=(char)('0'+v%10);j++;v/=10;}b[i+j]=0;put(b);put("\n");
        sc2(SYS_kill,xpid,15);
        long st=0; (void)sc4(SYS_wait4,xpid,(long)&st,0,0);
        restore_console_only();
        dump_xorg_log_tail();
        sc1(SYS_exit,1);for(;;){}
    } else if(cpid==0){

        char *cav[48]; int cc=0; cav[cc++]=client;
        for(int i=1;i<clientc && cc<47;i++) cav[cc++]=client_args[i];
        cav[cc]=0;

        put("[XINIT] client DISPLAY="); put(display); put("\n");
        put("[YABRO-X11-CWM-V13] child exec client\n");
        put("[XINIT] client env vector is static\n");
        long r=sc3(SYS_execve,(long)client,(long)cav,(long)cwm_client_envp);
        put("xinit: child exec client failed rc=");
        char b[32];long i=0,j=0;unsigned long v=(unsigned long)(r<0?-r:r);if(r<0)b[i++]='-';if(!v)b[i++]='0';while(v){b[i+j]=(char)('0'+v%10);j++;v/=10;}b[i+j]=0;put(b);put("\n");
        sc1(SYS_exit,127); for(;;){}
    } else {
        put("[YABRO-X11-CWM-V13] client child started pid=");
        char b[32];long i=0,j=0;unsigned long v=(unsigned long)cpid;if(!v)b[i++]='0';while(v){b[i+j]=(char)('0'+v%10);j++;v/=10;}b[i+j]=0;put(b);put("\n");
        int st=0;
        long wr=sc4(SYS_wait4,cpid,(long)&st,0,0);
        if(wr!=cpid){ put("xinit: wait client failed\n"); }
    }
    long xst=0; long xwr=sc4(SYS_wait4,xpid,(long)&xst,1,0);
    if(xwr==xpid){
        restore_console_only();
        report_wait_status("[XINIT] Xorg after client:",xst);
        dump_xorg_log_tail();
    } else {
        put("[XINIT] client ended while Xorg is alive; terminating Xorg\n");
        sc2(SYS_kill,xpid,15);
        long reap=0; (void)sc4(SYS_wait4,xpid,(long)&reap,0,0);
        restore_console_only();
        report_wait_status("[XINIT] Xorg after terminate:",reap);
        dump_xorg_log_tail();
    }
    sc1(SYS_exit,0); for(;;){}
}

__asm__(
".global _start\n.type _start,@function\n_start:\n"
"mov %rsp,%rdi\n"
"and $-16,%rsp\n"
"call xinit_entry\n"
"ud2\n"
".size _start,.-_start\n");

void xinit_entry(uint64_t*sp){long argc=(long)sp[0];char**argv=(char**)&sp[1];int rc=xinit_main((int)argc,argv);sc1(SYS_exit,rc);for(;;){}}
