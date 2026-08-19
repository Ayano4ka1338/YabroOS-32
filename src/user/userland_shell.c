/* YSH user shell. */
#include <stdint.h>
#define SYS_read 0
#define SYS_write 1
#define SYS_stat 4
#define SYS_open 2
#define SYS_close 3
#define SYS_pipe 22
#define SYS_sched_yield 24
#define SYS_dup2 33
#define SYS_getpid 39
#define SYS_execve 59
#define SYS_exit 60
#define SYS_wait4 61
#define SYS_chdir 80
#define SYS_getcwd 79
#define SYS_lseek 8
#define SYS_uname 63
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_kill 62
#define SYS_rename 82
#define SYS_unlink 87
#define SYS_getdents64 217
#define SYS_access 21
#define SYS_fork 57
#define SYS_exit_group 231
#define SYS_access 21

#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024
#define AT_FDCWD -100
#define BUILTIN_NOT_FOUND (-125)
#define S_IFMT 0170000
#define S_IFDIR 0040000
struct kstat { uint64_t dev,ino,nlink,mode,uid,gid,rdev,size,blksize,blocks,atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns; uint32_t unused[2]; };

static long sc0(long n){long r;__asm__ volatile("syscall":"=a"(r):"a"(n):"rcx","r11","memory");return r;}
static long sc1(long n,long a){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory");return r;}
static long sc2(long n,long a,long b){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory");return r;}
static long sc3(long n,long a,long b,long c){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static long sc4(long n,long a,long b,long c,long d){long r;register long r10 __asm__("r10")=d;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory");return r;}
static void putn(const char*s,long n){if(n>0)sc3(SYS_write,1,(long)s,n);} static void put(const char*s){long n=0;while(s[n])n++;putn(s,n);}
static int eq(const char*a,const char*b){long i=0;while(a[i]&&b[i]&&a[i]==b[i])i++;return a[i]==b[i];}
static int starts(const char*a,const char*b){long i=0;while(b[i]){if(a[i]!=b[i])return 0;i++;}return 1;}
static long slen(const char*s){long n=0;while(s[n])n++;return n;}
static void copy(char*d,const char*s){long i=0;while(s[i]&&i<127){d[i]=s[i];i++;}d[i]=0;}
static void fail(const char*s){put("ysh: ");put(s);put("\n");}

static void cursor_show(void){ put("_"); }
static void cursor_hide(void){ put("\b \b"); }

static void print_num(long v){char b[32];long i=0;unsigned long x=(unsigned long)(v<0?-v:v);if(v<0)b[i++]='-';char t[24];long j=0;if(!x)t[j++]='0';while(x){t[j++]=(char)('0'+x%10);x/=10;}while(j)b[i++]=t[--j];b[i]=0;put(b);}

static int cmd_mkdir(char **a,int n){int rc=0;for(int i=1;i<n;i++)if(sc2(SYS_mkdir,(long)a[i],0755)<0){fail("mkdir: failed");rc=1;}return rc;}
static int cmd_rm(char **a,int n){
    int rc=0, force=0, first=1;
    if(n>1 && eq(a[1],"-f")){force=1;first=2;}
    if(n<=first){fail("usage: rm [-f] FILE...");return 2;}
    for(int i=first;i<n;i++){long r=sc1(SYS_unlink,(long)a[i]);if(r<0&&!force){fail("rm: failed");rc=1;}}
    return rc;
}
static const char *base_name_s(const char *p){const char *b=p;for(long i=0;p&&p[i];i++)if(p[i]=='/')b=p+i+1;return b;}
static int make_child_path_s(const char *dir,const char *name,char *out,long cap){long n=0;while(dir[n]&&n<cap-1){out[n]=dir[n];n++;}if(n&&out[n-1]!='/'){if(n+1>=cap)return -1;out[n++]='/';}for(long i=0;name[i]&&n<cap-1;i++)out[n++]=name[i];if(name[0]&&n>=cap)return -1;out[n]=0;return 0;}
static int stat_is_dir_s(const char *p){struct kstat st;long r=sc2(SYS_stat,(long)p,(long)&st);return r==0 && ((st.mode&S_IFMT)==S_IFDIR);}
static int cmd_mv(char **a,int n){if(n!=3){fail("usage: mv SOURCE DEST");return 2;}char dest[256];const char *target=a[2];if(stat_is_dir_s(target)){if(make_child_path_s(target,base_name_s(a[1]),dest,sizeof(dest))<0){fail("mv: failed");return 1;}target=dest;}return sc2(SYS_rename,(long)a[1],(long)target)<0 ? (fail("mv: failed"),1) : 0;}
static int cmd_touch(char **a,int n){int rc=0;for(int i=1;i<n;i++){long fd=sc3(SYS_open,(long)a[i],O_CREAT,0666);if(fd<0){fail("touch: failed");rc=1;}else sc1(SYS_close,fd);}return rc;}
static int cmd_head(char **a,int n){if(n!=2){fail("usage: head FILE");return 2;}long fd=sc3(SYS_open,(long)a[1],0,0);if(fd<0){fail("head: open failed");return 1;}char b[512];long lines=0;for(;;){long r=sc3(SYS_read,fd,(long)b,sizeof(b));if(r<=0)break;for(long i=0;i<r;i++){putn(&b[i],1);if(b[i]=='\n'&&++lines>=10){sc1(SYS_close,fd);return 0;}}}sc1(SYS_close,fd);return 0;}
static int cmd_wc(char **a,int n){if(n!=2){fail("usage: wc FILE");return 2;}long fd=sc3(SYS_open,(long)a[1],0,0);if(fd<0){fail("wc: open failed");return 1;}char b[512];long lines=0,words=0,bytes=0,inword=0;for(;;){long r=sc3(SYS_read,fd,(long)b,sizeof(b));if(r<=0)break;bytes+=r;for(long i=0;i<r;i++){if(b[i]=='\n')lines++;if(b[i]==' '||b[i]=='\t'||b[i]=='\n'||b[i]=='\r')inword=0;else if(!inword){words++;inword=1;}}}sc1(SYS_close,fd);print_num(lines);put(" ");print_num(words);put(" ");print_num(bytes);put(" ");put(a[1]);put("\n");return 0;}

static long to_num(const char*s){long v=0;int sign=1;if(*s=='-'){sign=-1;s++;}if(!*s)return-1;while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}return sign*v;}
static int cmd_rmdir(char **a,int n){int rc=0;for(int i=1;i<n;i++)if(sc1(SYS_rmdir,(long)a[i])<0){fail("rmdir: failed");rc=1;}return rc;}
static int cmd_cp(char **a,int n){
    if(n!=3){fail("usage: cp SOURCE DEST");return 2;}
    char dest[256];const char *target=a[2];if(stat_is_dir_s(target)){if(make_child_path_s(target,base_name_s(a[1]),dest,sizeof(dest))<0){fail("cp: destination path too long");return 1;}target=dest;}
    long in=sc3(SYS_open,(long)a[1],0,0);if(in<0){fail("cp: source open failed");return 1;}
    long out=sc3(SYS_open,(long)target,O_WRONLY|O_CREAT|O_TRUNC,0666);if(out<0){sc1(SYS_close,in);fail("cp: destination open failed");return 1;}
    char b[1024];int rc=0;for(;;){long r=sc3(SYS_read,in,(long)b,sizeof(b));if(r<0){rc=1;break;}if(r==0)break;long done=0;while(done<r){long w=sc3(SYS_write,out,(long)b+done,(unsigned long)(r-done));if(w<=0){rc=1;break;}done+=w;}if(rc)break;}
    sc1(SYS_close,in);sc1(SYS_close,out);return rc;
}
static int cmd_tail(char **a,int n){
    if(n!=2){fail("usage: tail FILE");return 2;}
    long fd=sc3(SYS_open,(long)a[1],0,0);
    if(fd<0){fail("tail: open failed");return 1;}
    char b[16384]; long total=0;
    for(;;){long room=(long)sizeof(b)-total;if(room<=0)break;long r=sc3(SYS_read,fd,(long)b+total,(unsigned long)room);if(r<0){sc1(SYS_close,fd);fail("tail: read failed");return 1;}if(r==0)break;total+=r;}
    sc1(SYS_close,fd);
    long lines=0;for(long i=0;i<total;i++)if(b[i]=='\n')lines++;
    long skip=lines>10?lines-10:0,pos=0,seen=0;
    while(pos<total&&seen<skip){if(b[pos++]=='\n')seen++;}
    putn(b+pos,total-pos);return 0;
}
static int cmd_grep(char **a,int n){if(n<3){fail("usage: grep PATTERN FILE");return 2;}long fd=sc3(SYS_open,(long)a[2],0,0);if(fd<0){fail("grep: open failed");return 2;}char b[4096];long total=0;for(;;){long r=sc3(SYS_read,fd,(long)b+total,sizeof(b)-1-total);if(r<=0)break;total+=r;if(total==(long)sizeof(b)-1)break;}sc1(SYS_close,fd);int found=0;long st=0;for(long i=0;i<=total;i++){if(i==total||b[i]=='\n'){long len=i-st;int match=0;for(long j=0;j+ (long)slen(a[1])<=len;j++){long k=0;while(k<slen(a[1])&&b[st+j+k]==a[1][k])k++;if(k==slen(a[1])){match=1;break;}}if(match){putn(b+st,len);put("\n");found=1;}st=i+1;}}return found?0:1;}
static int cmd_ps(void){put("  PID STATE CMD\n");put("    1 RUN   ");char b[256];long fd=sc3(SYS_open,(long)"/proc/1/cmdline",0,0);if(fd>=0){long n=sc3(SYS_read,fd,(long)b,sizeof(b)-1);sc1(SYS_close,fd);if(n>0)putn(b,n);}put("\n");return 0;}
static int cmd_kill(char **a,int n){if(n!=2){fail("usage: kill PID");return 2;}long pid=to_num(a[1]);if(pid<0||sc2(SYS_kill,pid,15)<0){fail("kill: failed");return 1;}return 0;}
static int cmd_whoami(void){put("root\n");return 0;}
static int cmd_id(void){put("uid=0(root) gid=0(root) groups=0(root)\n");return 0;}

static int cmd_uname(void){char u[390];if(sc1(SYS_uname,(long)u)<0){fail("uname");return 1;}put(u);put(" ");put(u+65);put(" ");put(u+130);put(" ");put(u+195);put("\n");return 0;}
static int cmd_uptime(void){char b[128];long fd=sc3(SYS_open,(long)"/proc/uptime",0,0);if(fd<0){fail("uptime");return 1;}long r=sc3(SYS_read,fd,(long)b,sizeof(b)-1);sc1(SYS_close,fd);if(r>0)putn(b,r);return r>0?0:1;}
static int cmd_sh(void){long pid=sc0(SYS_fork);if(pid<0){fail("sh: fork failed");return 1;}if(pid==0){char *av[]={(char*)"/YSH.ELF",0};char *ev[]={(char*)"PATH=/bin",(char*)"HOME=/",0};sc3(SYS_execve,(long)"/YSH.ELF",(long)av,(long)ev);sc1(SYS_exit,127);for(;;){}}long st=0;return sc4(SYS_wait4,pid,(long)&st,0,0)==pid?((st>>8)&255):1;}

static void resolve_elf(char *out,const char *cmd){
    if(cmd[0]=='/'){copy(out,cmd);return;}
    out[0]='/';
    long i=1;
    long j=0;
    while(cmd[j] && j<120){
        char c=cmd[j++];
        if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
        out[i++]=c;
    }
    if(i<5 || !(out[i-4]=='.'&&out[i-3]=='E'&&out[i-2]=='L'&&out[i-1]=='F')){
        if(i+4<127){out[i++]='.';out[i++]='E';out[i++]='L';out[i++]='F';}
    }
    out[i]=0;
}

static int cmd_run(char **a,int n){
    if(n<2){fail("usage: run FILE [ARGS ...]");return 2;}
    char path[128];
    if(a[1][0]=='/') copy(path,a[1]);
    else { path[0]='/'; copy(path+1,a[1]); }

    if(a[1][0]!='/') resolve_elf(path,a[1]);
    long pid=sc0(SYS_fork);
    if(pid<0){fail("run: fork failed");return 1;}
    if(pid==0){
        char *av[16];
        int ac=0;
        av[ac++]=path;
        for(int i=2;i<n && ac<15;i++) av[ac++]=a[i];
        av[ac]=0;
        char *envp[]={(char*)"PATH=/bin",(char*)"HOME=/",(char*)"SHELL=/bin/sh",0};
        long er=sc3(SYS_execve,(long)path,(long)av,(long)envp);
        put("run: exec failed: ");put(path);put(" rc=");print_num(er);put("\n");
        sc1(SYS_exit,127);
        for(;;){}
    }
    long st=0;
    long r=sc4(SYS_wait4,pid,(long)&st,0,0);
    if(r!=pid){fail("run: wait failed");return 1;}
    return (int)((st>>8)&255);
}

static int builtin(char **a,int n){
    if(n==0)return BUILTIN_NOT_FOUND;
    if(eq(a[0],"exit")){
        long code=0;
        if(n>1){for(long i=0;a[1][i]>='0'&&a[1][i]<='9';i++)code=code*10+a[1][i]-'0';}
        sc1(SYS_exit_group,code);for(;;){}
    }
    if(eq(a[0],"cd")){
        const char*p=n>1?a[1]:"/";
        if(sc1(SYS_chdir,(long)p)<0){fail("cd: no such directory");return 1;}
        return 0;
    }
    if(eq(a[0],"help")){
        put("builtins: cd run help exit\n");
        put("YabrusBox applets: ls cat pwd echo env mkdir rmdir cp rm mv touch head tail wc grep uname uptime ps kill whoami id true false sh\n");
        return 0;
    }
    return BUILTIN_NOT_FOUND;
}

static int is_builtin(const char *cmd){

    return eq(cmd,"run")||eq(cmd,"cd")||eq(cmd,"exit")||eq(cmd,"help");
}

static int is_applet(const char *cmd){
    return eq(cmd,"ls")||eq(cmd,"cat")||eq(cmd,"pwd")||eq(cmd,"echo")||
           eq(cmd,"env")||eq(cmd,"mkdir")||eq(cmd,"rmdir")||eq(cmd,"cp")||
           eq(cmd,"rm")||eq(cmd,"mv")||eq(cmd,"touch")||eq(cmd,"head")||
           eq(cmd,"tail")||eq(cmd,"wc")||eq(cmd,"grep")||eq(cmd,"uname")||
           eq(cmd,"uptime")||eq(cmd,"ps")||eq(cmd,"kill")||eq(cmd,"whoami")||
           eq(cmd,"id")||eq(cmd,"true")||eq(cmd,"false")||eq(cmd,"sh");
}

static void resolve(char*out,const char*cmd){
    if(cmd[0]=='/'){copy(out,cmd);return;}
    if(eq(cmd,"yabrusbox")||eq(cmd,"busybox")||is_applet(cmd)){copy(out,"/YABRUSBOX.ELF");return;}

    resolve_elf(out,cmd);
}

static int launch(char **av,int n,int in_fd,int out_fd,const char *rin,const char*rout,const char*rappend){
    int pf[2]; long pid=sc0(SYS_fork); if(pid<0){fail("fork failed");return -1;}
    if(pid==0){
        if(in_fd!=0) { if(sc2(SYS_dup2,in_fd,0)!=0)sc1(SYS_exit,126); sc1(SYS_close,in_fd); }
        if(out_fd!=1) { if(sc2(SYS_dup2,out_fd,1)!=1)sc1(SYS_exit,126); sc1(SYS_close,out_fd); }
        if(rin){long fd=sc3(SYS_open,(long)rin,0,0);if(fd<0)sc1(SYS_exit,126);sc2(SYS_dup2,fd,0);sc1(SYS_close,fd);}
        if(rout){int fl=O_WRONLY|O_CREAT|(rappend?O_APPEND:O_TRUNC);long fd=sc3(SYS_open,(long)rout,fl,0666);if(fd<0)sc1(SYS_exit,126);sc2(SYS_dup2,fd,1);sc1(SYS_close,fd);}
        int br=builtin(av,n); if(br!=BUILTIN_NOT_FOUND){sc1(SYS_exit,br);for(;;){}}
        char path[128];resolve(path,av[0]);
        char *bbav[12]; char *envp[]={(char*)"PATH=/bin",(char*)"HOME=/",(char*)"SHELL=/bin/sh",0};
        char *exec_argv[12];
        if(eq(path,"/YABRUSBOX.ELF") && is_applet(av[0])){

            bbav[0]=av[0];
            for(int i=1;i<n && i<11;i++) bbav[i]=av[i];
            bbav[n]=0;
            for(int i=0;i<=n;i++) exec_argv[i]=bbav[i];
        } else {
            for(int i=0;i<n && i<11;i++) exec_argv[i]=av[i];
            exec_argv[n]=0;
        }
        long er=sc3(SYS_execve,(long)path,(long)exec_argv,(long)envp);
        put("ysh: exec failed: ");put(path);put(" rc=");print_num(er);put("\n");sc1(SYS_exit,127);for(;;){}
    }
    return (int)pid;
}

static int parse(char *line,char *(*stages)[8],int *counts,char redirs[4][3][128]){
    int sc=0,n=0;char *p=line;for(int i=0;i<4;i++)for(int j=0;j<3;j++)redirs[i][j][0]=0;
    stages[0][0]=0;
    while(*p){while(*p==' '||*p=='\t'||*p=='\n')p++;if(!*p)break; if(*p=='|'){if(n==0)return-1;stages[sc][n]=0;sc++;n=0;if(sc>=4)return-1;p++;continue;}
        if(*p=='>'||*p=='<'){char op=*p++;int app=0;if(op=='>'&&*p=='>'){app=1;p++;}while(*p==' '||*p=='\t')p++;char *q=redirs[sc][op=='<'?0:(app?2:1)];int k=0;while(*p&&*p!=' '&&*p!='\t'&&*p!='|'&&k<127)q[k++]=*p++;q[k]=0;continue;}
        if(n>=7)return-1; stages[sc][n]=p; char *start=p;int quoted=0;while(*p&&((quoted)||(*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='|'&&*p!='>'&&*p!='<'))){if(*p=='"'){quoted=!quoted;for(char*x=p;x[0];x++)x[0]=x[1];continue;}p++;}if(*p){*p=0;p++;}n++;stages[sc][n]=0; (void)start;
    }
    counts[sc]=n; if(n==0)return 0; return sc+1;
}

static void run_line(char *line){
    char *stages[4][8];int counts[4];char redirs[4][3][128];int ns=parse(line,stages,counts,redirs);if(ns<=0){if(ns<0)fail("syntax error");return;}
    if(ns==1 && counts[0]>0 && !redirs[0][0][0]&&!redirs[0][1][0]&&!redirs[0][2][0]){
        const char *cmd=stages[0][0];
        if(is_builtin(cmd)){
            (void)builtin(stages[0],counts[0]);
            return;
        }
    }
    int prev=0;int pids[4];int pc=0;
    for(int i=0;i<ns;i++){
        int pipefd[2]={-1,-1};int out=1;if(i<ns-1){if(sc1(SYS_pipe,(long)pipefd)!=0){fail("pipe failed");return;}out=pipefd[1];}
        pids[pc++]=launch(stages[i],counts[i],prev,out,redirs[i][0][0]?redirs[i][0]:0,redirs[i][1][0]?redirs[i][1]:0,redirs[i][2][0]?redirs[i][2]:0);
        if(prev)sc1(SYS_close,prev);if(i<ns-1){sc1(SYS_close,pipefd[1]);prev=pipefd[0];}
    }
    if(prev)sc1(SYS_close,prev);int st=0;for(int i=0;i<pc;i++)if(pids[i]>0){for(int k=0;k<128;k++){long r=sc4(SYS_wait4,pids[i],(long)&st,0,0);if(r==pids[i])break;if(r!=-11)break;sc0(SYS_sched_yield);}}
}

void _start(void){
    put("YabroOS-32 YSH 0.0.1-alpha\n");put("type 'help' for commands\n");
    char line[512];
    for(;;){
        put("YabroOS-32> ");
        long pos=0;
        cursor_show();
        for(;;){
            char c=0;
            long r=sc3(SYS_read,0,(long)&c,1);
            if(r==-11){sc0(SYS_sched_yield);continue;}
            if(r<=0){cursor_hide();sc1(SYS_exit_group,0);for(;;){}}
            if(c=='\r')continue;

            cursor_hide();
            if(c=='\n'){put("\n");line[pos]=0;break;}
            if(c==8||c==127){
                if(pos){pos--;put("\b \b");}
                cursor_show();
                continue;
            }
            if(pos<511){line[pos++]=c;putn(&c,1);}
            cursor_show();
        }
        if(pos)run_line(line);
    }
}
