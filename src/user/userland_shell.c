//Yabrus SHell - (c) Ayano4ka1338, 2026

#include <stdint.h>
#define SYS_read 0
#define SYS_write 1
#define SYS_poll 7
#define SYS_stat 4
#define SYS_open 2
#define SYS_close 3
#define SYS_pipe 22
#define SYS_sched_yield 24
#define SYS_dup 32
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
#define SYS_sysinfo 99
#define SYS_getdents64 217
#define SYS_ioctl 16
#define SYS_setpgid 109
#define SYS_getpgrp 122
#define SYS_access 21
#define SYS_unlink 87
#define SYS_cls 168
#define SYS_shutdown 169
#define SYS_fork 57
#define SYS_exit_group 231
#define SYS_access 21
#define SYS_unlink 87

#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024
#define AT_FDCWD -100
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCSPGRP 0x5410
#define SHELL_TERMIOS_SIZE 60
#define TERMIOS_ICANON 0x00000002U
#define TERMIOS_ECHO 0x00000008U
#define TERMIOS_ISIG 0x00000001U
#define BUILTIN_NOT_FOUND (-125)
#define ENV_MAX 32
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
static int starts_with(const char*a,const char*b){long i=0;while(b[i]){if(a[i]!=b[i])return 0;i++;}return 1;}
static void copy(char*d,const char*s){long i=0;while(s[i]&&i<511){d[i]=s[i];i++;}d[i]=0;}
static void fail(const char*s){put("ysh: ");put(s);put("\n");}
static char env_store[ENV_MAX][128];
static int env_count=0;
static int last_status=0;
static unsigned char shell_saved_termios[SHELL_TERMIOS_SIZE];
static unsigned char shell_raw_termios[SHELL_TERMIOS_SIZE];
static int shell_termios_saved=0;
static long shell_pgid=0;

static uint32_t termios_u32(const unsigned char *p){
	return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void termios_put_u32(unsigned char *p,uint32_t v){
	p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void shell_terminal_raw(void){
	if(!shell_termios_saved){
		if(sc3(SYS_ioctl,0,TCGETS,(long)shell_saved_termios)<0)return;
		for(int i=0;i<SHELL_TERMIOS_SIZE;i++)shell_raw_termios[i]=shell_saved_termios[i];
		uint32_t lf=termios_u32(shell_raw_termios+12);
		lf &= ~(TERMIOS_ICANON|TERMIOS_ECHO|TERMIOS_ISIG);
		termios_put_u32(shell_raw_termios+12,lf);
		shell_termios_saved=1;
	}
	(void)sc3(SYS_ioctl,0,TCSETS,(long)shell_raw_termios);
}
static void shell_terminal_cooked(void){
	if(shell_termios_saved)(void)sc3(SYS_ioctl,0,TCSETS,(long)shell_saved_termios);
}
static void shell_terminal_foreground(long pgid){
	if(pgid<=0)return;
	int group=(int)pgid;
	(void)sc3(SYS_ioctl,0,TIOCSPGRP,(long)&group);
}
static void shell_terminal_init(void){
	(void)sc2(SYS_setpgid,0,0);
	shell_pgid=sc0(SYS_getpgrp);
	if(shell_pgid<=0)shell_pgid=sc0(SYS_getpid);
	shell_terminal_foreground(shell_pgid);
	shell_terminal_raw();
}

static void env_init(void){
	env_count=0;
	copy(env_store[env_count++],"PATH=/usr/bin:/bin:/");
	copy(env_store[env_count++],"HOME=/");
	copy(env_store[env_count++],"SHELL=/bin/sh");

	copy(env_store[env_count++],"TERM=xterm");
	copy(env_store[env_count++],"TERMINFO=/lib/term");
}
static const char *env_value(const char *name){
	long nl=slen(name);
	for(int i=0;i<env_count;i++){
		long j=0; while(env_store[i][j] && env_store[i][j]!='=' && j<127)j++;
		if(j==nl && env_store[i][j]=='='){
			long k=0; while(k<nl && env_store[i][k]==name[k]) k++;
			if(k==nl) return env_store[i]+j+1;
		}
	}
	return 0;
}
static int env_set(const char *name,const char *value){
	long nl=slen(name),vl=slen(value);
	if(!nl || nl>=64 || vl+nl+2>128) return -1;
	for(long i=0;i<nl;i++) if(name[i]=='='||name[i]=='/'||name[i]==' '||name[i]=='\t') return -1;
	for(int i=0;i<env_count;i++){
		long j=0; while(env_store[i][j] && env_store[i][j]!='=' && j<127)j++;
		if(j==nl){
			long kcmp=0; while(kcmp<nl && env_store[i][kcmp]==name[kcmp]) kcmp++;
			if(kcmp!=nl) continue;
			long k=0; while(k<nl){env_store[i][k]=name[k];k++;} env_store[i][k++]='=';
			for(long q=0;q<vl;q++)env_store[i][k++]=value[q]; env_store[i][k]=0; return 0;
		}
	}
	if(env_count>=ENV_MAX)return -1;
	long k=0;while(k<nl)env_store[env_count][k]=name[k],k++;env_store[env_count][k++]='=';
	for(long q=0;q<vl;q++)env_store[env_count][k++]=value[q];env_store[env_count][k]=0;env_count++;return 0;
}
static int env_unset(const char *name){
	long nl=slen(name);
	for(int i=0;i<env_count;i++){
		long j=0;while(env_store[i][j]&&env_store[i][j]!='='&&j<127)j++;
		if(j==nl){
			long kcmp=0; while(kcmp<nl && env_store[i][kcmp]==name[kcmp]) kcmp++;
			if(kcmp!=nl) continue;
			for(int k=i;k+1<env_count;k++)copy(env_store[k],env_store[k+1]);env_count--;return 0;
		}
	}
	return 0;
}
static int build_envp(char **out,int cap){for(int i=0;i<env_count&&i<cap-1;i++)out[i]=env_store[i];out[env_count<cap?env_count:cap-1]=0;return env_count;}
static const char *var_value(const char *name){
	if(eq(name,"?")){static char b[24]; long v=last_status,i=0;char t[20];long j=0;unsigned long x=(unsigned long)v;if(!x)t[j++]='0';while(x){t[j++]=(char)('0'+x%10);x/=10;}while(j)b[i++]=t[--j];b[i]=0;return b;}
	return env_value(name);
}
static void expand_vars(const char *in,char *out,long cap){
	long i=0,o=0;
	while(in[i]&&o<cap-1){
		if(in[i]=='$'){
			long j=i+1;char name[64];long n=0;
			if(in[j]=='?'){name[n++]='?';j++;}
			else {while(in[j]&&((in[j]>='A'&&in[j]<='Z')||(in[j]>='a'&&in[j]<='z')||(in[j]>='0'&&in[j]<='9')||in[j]=='_')&&n<63)name[n++]=in[j++];}
			if(n){name[n]=0;const char*v=var_value(name);if(v)while(*v&&o<cap-1)out[o++]=*v++;i=j;continue;}
		}
		out[o++]=in[i++];
	}
	out[o]=0;
}

static void cursor_show(void){}
static void cursor_hide(void){}

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
static int cmd_wc(char **a,int n){
	if(n>2){fail("usage: wc [FILE]");return 2;}
	long fd=0;
	const char *label="-";
	if(n==2){fd=sc3(SYS_open,(long)a[1],0,0);label=a[1];if(fd<0){fail("wc: open failed");return 1;}}
	char b[512];long lines=0,words=0,bytes=0,inword=0;
	for(;;){long r=sc3(SYS_read,fd,(long)b,sizeof(b));if(r<0){if(n==2)sc1(SYS_close,fd);fail("wc: read failed");return 1;}if(r==0)break;bytes+=r;for(long i=0;i<r;i++){if(b[i]=='\n')lines++;if(b[i]==' '||b[i]=='\t'||b[i]=='\n'||b[i]=='\r')inword=0;else if(!inword){words++;inword=1;}}}
	if(n==2)sc1(SYS_close,fd);print_num(lines);put(" ");print_num(words);put(" ");print_num(bytes);put(" ");put(label);put("\n");return 0;
}

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
static int cmd_grep(char **a,int n){
	if(n<2||n>3){fail("usage: grep PATTERN [FILE]");return 2;}
	long fd=0; const char *label="-";
	if(n==3){fd=sc3(SYS_open,(long)a[2],0,0);label=a[2];if(fd<0){fail("grep: open failed");return 2;}}
	char b[4096];long total=0;
	for(;;){long r=sc3(SYS_read,fd,(long)b+total,sizeof(b)-1-total);if(r<0){if(n==3)sc1(SYS_close,fd);fail("grep: read failed");return 1;}if(r==0)break;total+=r;if(total==(long)sizeof(b)-1)break;}
	if(n==3)sc1(SYS_close,fd);
	int found=0;long st=0;long plen=slen(a[1]);
	for(long i=0;i<=total;i++){if(i==total||b[i]=='\n'){long len=i-st,match=0;for(long j=0;j+plen<=len;j++){long k=0;while(k<plen&&b[st+j+k]==a[1][k])k++;if(k==plen){match=1;break;}}if(match){putn(b+st,len);put("\n");found=1;}st=i+1;}}
	return found?0:1;
}
static int cmd_ps(void){put("  PID STATE CMD\n");put("    1 RUN   ");char b[256];long fd=sc3(SYS_open,(long)"/proc/1/cmdline",0,0);if(fd>=0){long n=sc3(SYS_read,fd,(long)b,sizeof(b)-1);sc1(SYS_close,fd);if(n>0)putn(b,n);}put("\n");return 0;}
static int cmd_kill(char **a,int n){if(n!=2){fail("usage: kill PID");return 2;}long pid=to_num(a[1]);if(pid<0||sc2(SYS_kill,pid,15)<0){fail("kill: failed");return 1;}return 0;}
static int cmd_whoami(void){put("root\n");return 0;}
static int cmd_id(void){put("uid=0(root) gid=0(root) groups=0(root)\n");return 0;}

static long read_proc_text(const char *path, char *buf, long cap){
	long fd=sc3(SYS_open,(long)path,0,0);
	if(fd<0)return -1;
	long n=sc3(SYS_read,fd,(long)buf,(unsigned long)(cap-1));
	sc1(SYS_close,fd);
	if(n<0)return n;
	if(n>=cap)n=cap-1;
	buf[n]=0;
	return n;
}

static void print_kb(uint64_t kb){print_num((long)kb);put(" kB");}

static int cmd_mem(void){
	char b[2048];
	long n=read_proc_text("/proc/meminfo",b,sizeof(b));
	if(n<0){fail("mem: cannot read /proc/meminfo");return 1;}
	uint64_t total=0,freeb=0,avail=0;
	char *p=b;
	while(*p){
		char *line=p; while(*p && *p!='\n')p++; if(*p)*p++=0;
		if(!starts_with(line,"MemTotal:" ) && !starts_with(line,"MemFree:" ) && !starts_with(line,"MemAvailable:")) continue;
		char *q=line; while(*q && (*q<'0'||*q>'9'))q++;
		uint64_t v=0; while(*q>='0'&&*q<='9'){v=v*10+(uint64_t)(*q-'0');q++;}
		if(starts_with(line,"MemTotal:")) total=v;
		else if(starts_with(line,"MemFree:")) freeb=v;
		else avail=v;
	}
	uint64_t used=(total>avail)?(total-avail):0;
	uint64_t pct=total?(used*100ULL/total):0;
	put("Memory usage\n");
	put("  Total:     ");print_kb(total);put("\n");
	put("  Used:      ");print_kb(used);put("\n");
	put("  Free:      ");print_kb(freeb);put("\n");
	put("  Available: ");print_kb(avail);put("\n");
	put("  Usage:     ");print_num((long)pct);put("%\n");
	return 0;
}

static int cmd_cpuinfo(void){
	char b[4096];
	long n=read_proc_text("/proc/cpuinfo",b,sizeof(b));
	if(n<0){fail("cpuinfo: cannot read /proc/cpuinfo");return 1;}
	char *p=b; int printed=0;
	while(*p){
		char *line=p; while(*p && *p!='\n')p++; if(*p)*p++=0;
		if(starts_with(line,"processor" ) || starts_with(line,"vendor_id") || starts_with(line,"model name") || starts_with(line,"cpu family") || starts_with(line,"model") || starts_with(line,"stepping")){put(line);put("\n");printed=1;}
	}
	if(!printed){put("/proc/cpuinfo is unavailable or empty\n");return 1;}
	return 0;
}

struct shell_sysinfo { long uptime; uint64_t loads[3],totalram,freeram,sharedram,bufferram,totalhigh,freehigh; uint32_t mem_unit,_pad[3]; };
static int cmd_sysinfo_builtin(void){
	struct shell_sysinfo x;
	for(unsigned long i=0;i<sizeof(x);i++)((unsigned char*)&x)[i]=0;
	long r=sc1(SYS_sysinfo,(long)&x);
	if(r<0){fail("sysinfo: syscall failed");return 1;}
	uint64_t unit=x.mem_unit?x.mem_unit:1;
	uint64_t total=x.totalram*unit/1024ULL, free=x.freeram*unit/1024ULL;
	uint64_t used=total>free?total-free:0;
	put("System memory\n  Total: ");print_kb(total);put("\n  Used:  ");print_kb(used);put("\n  Free:  ");print_kb(free);put("\n");
	return 0;
}

static int cmd_uname(void){char u[390];if(sc1(SYS_uname,(long)u)<0){fail("uname");return 1;}put(u);put(" ");put(u+65);put(" ");put(u+130);put(" ");put(u+195);put("\n");return 0;}
static int cmd_uptime(void){
	char b[128];
	long fd=sc3(SYS_open,(long)"/proc/uptime",0,0);
	if(fd<0){fail("uptime: cannot open /proc/uptime");return 1;}
	long r=sc3(SYS_read,fd,(long)b,sizeof(b));
	(void)sc1(SYS_close,fd);
	if(r<0){fail("uptime: read failed");return 1;}
	if(r==0){fail("uptime: empty /proc/uptime");return 1;}
	putn(b,r);
	return 0;
}
static int shell_has_xterm(void){
	const char *term=env_value("TERM");
	const char *display=env_value("DISPLAY");
	return term && starts_with(term,"xterm") && display && display[0];
}
static void terminal_clear_screen(void){
	if(shell_has_xterm()) put("\033[2J\033[H");
	else (void)sc0(SYS_cls);
}
static int cmd_cls(void){
	terminal_clear_screen();
	return 0;
}
static int cmd_pftest(void){

	volatile unsigned char *p=(volatile unsigned char*)0;
	*p=0x42;
	return 1;
}

#define LINUX_REBOOT_MAGIC1 0xfee1deadL
#define LINUX_REBOOT_MAGIC2 0x28121969L
#define LINUX_REBOOT_CMD_RESTART 0x01234567L
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedcL
static int cmd_power_control(char **a,int n,int restart){
	(void)a;
	if(n!=1){fail(restart?"usage: reboot":"usage: shutdown");return 2;}
	put(restart?"Rebooting...\n":"Shutting down...\n");
	long r=sc3(SYS_shutdown,LINUX_REBOOT_MAGIC1,LINUX_REBOOT_MAGIC2,
	           restart?LINUX_REBOOT_CMD_RESTART:LINUX_REBOOT_CMD_POWER_OFF);

	if(r<0){fail(restart?"reboot: kernel rejected request":"shutdown: kernel rejected request");return 1;}
	fail(restart?"reboot: kernel returned unexpectedly":"shutdown: kernel returned unexpectedly");
	return 1;
}
static int cmd_shutdown(char **a,int n){return cmd_power_control(a,n,0);}
static int cmd_reboot(char **a,int n){return cmd_power_control(a,n,1);}

static int cmd_cd(char **a,int n){
	int first=1, dash_means_oldpwd=1;
	if(n>1 && eq(a[1],"--")){first=2;dash_means_oldpwd=0;}
	if(n-first>1){fail("cd: usage: cd [DIRECTORY]");return 2;}
	char oldcwd[128],newcwd[128];
	oldcwd[0]=0;
	if(sc2(SYS_getcwd,(long)oldcwd,sizeof(oldcwd))<0) oldcwd[0]=0;
	const char *target=0; int print_newcwd=0;
	if(first>=n){
		target=env_value("HOME");
		if(!target || !target[0]){fail("cd: HOME is not set");return 1;}
	}else if(dash_means_oldpwd && eq(a[first],"-")){
		target=env_value("OLDPWD");
		if(!target || !target[0]){fail("cd: OLDPWD is not set");return 1;}
		print_newcwd=1;
	}else target=a[first];
	long r=sc1(SYS_chdir,(long)target);
	if(r<0){
		put("ysh: cd: ");put(target);put(": ");
		if(r==-20)put("Not a directory");
		else if(r==-13)put("Permission denied");
		else put("No such directory or path");
		put("\n");return 1;
	}
	if(sc2(SYS_getcwd,(long)newcwd,sizeof(newcwd))<0){fail("cd: changed directory, but getcwd failed");return 1;}
	if(oldcwd[0] && env_set("OLDPWD",oldcwd)<0){fail("cd: cannot update OLDPWD");return 1;}
	if(env_set("PWD",newcwd)<0){fail("cd: cannot update PWD");return 1;}
	if(print_newcwd){put(newcwd);put("\n");}
	return 0;
}

static int cmd_sh(void){long pid=sc0(SYS_fork);if(pid<0){fail("sh: fork failed");return 1;}if(pid==0){char *av[]={(char*)"/YSH.ELF",0};char *ev[]={(char*)"PATH=/usr/bin:/bin:/",(char*)"HOME=/",0};sc3(SYS_execve,(long)"/YSH.ELF",(long)av,(long)ev);sc1(SYS_exit,127);for(;;){}}long st=0;return sc4(SYS_wait4,pid,(long)&st,0,0)==pid?((st>>8)&255):1;}

static int path_exists(const char *path){
	long fd=sc3(SYS_open,(long)path,0,0);
	if(fd<0)return 0;
	sc1(SYS_close,fd);
	return 1;
}

static void join_path(char *out,const char *dir,const char *cmd){
	long i=0,j=0;
	while(dir[i]&&i<127){out[i]=dir[i];i++;}
	if(i>0&&out[i-1]!='/'&&i<127)out[i++]='/';
	while(cmd[j]&&i<127)out[i++]=cmd[j++];
	out[i]=0;
}

static void resolve_elf(char *out,const char *cmd){

	if(cmd[0]=='/') { copy(out,cmd); return; }
	join_path(out,"/",cmd);
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
		char *envp[ENV_MAX+1]; build_envp(envp,ENV_MAX+1);
		long er=sc3(SYS_execve,(long)path,(long)av,(long)envp);
		put("run: exec failed: ");put(path);put(" rc=");print_num(er);put("\n");
		sc1(SYS_exit,127);
		for(;;){}
	}
	long st=0;
	long r=sc4(SYS_wait4,pid,(long)&st,0,0);
	if(r!=pid){fail("run: wait failed");return 1;}
	int rc=(int)((st>>8)&255);

	return rc;
}

static int is_builtin(const char *cmd);
static int is_applet(const char *cmd);
static void resolve(char*out,const char*cmd);

static int cmd_env_builtin(void){
	for(int i=0;i<env_count;i++){put(env_store[i]);put("\n");}
	return 0;
}

static int cmd_stat_builtin(char **a,int n){
	if(n!=2){fail("usage: stat FILE");return 2;}
	struct kstat st;
	long r=sc2(SYS_stat,(long)a[1],(long)&st);
	if(r<0){fail("stat: failed");return 1;}
	put("  File: ");put(a[1]);put("\n");
	put("  Size: ");print_num((long)st.size);put("\n");
	put("  Inode: ");print_num((long)st.ino);put("\n");
	put("  Mode: ");print_num((long)(st.mode & 07777));put("\n");
	put("  Type: ");
	switch(st.mode & S_IFMT){
		case S_IFDIR: put("directory"); break;
		case 0100000: put("regular file"); break;
		case 0120000: put("symbolic link"); break;
		default: put("other"); break;
	}
	put("\n");
	return 0;
}

static int cmd_which_builtin(char **a,int n){
	if(n!=2){fail("usage: which COMMAND");return 2;}
	if(is_builtin(a[1])){put(a[1]);put(": shell builtin\n");return 0;}
	if(is_applet(a[1])){put("/YABRUSBOX.ELF\n");return 0;}
	char path[128];resolve(path,a[1]);
	long fd=sc3(SYS_open,(long)path,0,0);
	if(fd<0){put(a[1]);put(": not found\n");return 1;}
	sc1(SYS_close,fd);put(path);put("\n");return 0;
}

static int cmd_history_builtin(char **a,int n);

static int builtin(char **a,int n){
	if(n==0)return BUILTIN_NOT_FOUND;
	if(eq(a[0],"exit")){
		long code=0;
		if(n>1){for(long i=0;a[1][i]>='0'&&a[1][i]<='9';i++)code=code*10+a[1][i]-'0';}
		sc1(SYS_exit_group,code);for(;;){}
	}
	if(eq(a[0],"cd")) return cmd_cd(a,n);
	if(eq(a[0],"pwd")){char b[256];if(sc2(SYS_getcwd,(long)b,sizeof(b))<0){fail("pwd: getcwd failed");return 1;}put(b);put("\n");return 0;}
	if(eq(a[0],"export")){
		if(n==1){for(int i=0;i<env_count;i++){put("declare -x ");put(env_store[i]);put("\n");}return 0;}
		for(int i=1;i<n;i++){char *eqp=a[i];long k=0;while(eqp[k]&&eqp[k]!='=')k++;if(!eqp[k]){const char*v=env_value(eqp);if(!v)v="";if(env_set(eqp,v)<0){fail("export: invalid name");return 1;}}else{eqp[k]=0;if(env_set(eqp,eqp+k+1)<0){fail("export: invalid assignment");return 1;}}}
		return 0;
	}
	if(eq(a[0],"unset")){
		for(int i=1;i<n;i++)env_unset(a[i]);
		return 0;
	}
	if(eq(a[0],"env")){
		if(n>1){fail("env: options are not supported");return 2;}
		return cmd_env_builtin();
	}
	if(eq(a[0],"history")) return cmd_history_builtin(a,n);
	if(eq(a[0],"cls")) return cmd_cls();
	if(eq(a[0],"shutdown")||eq(a[0],"poweroff")) return cmd_shutdown(a,n);
	if(eq(a[0],"reboot")) return cmd_reboot(a,n);
	if(eq(a[0],"pftest")) { if(n!=1){fail("usage: pftest");return 2;} return cmd_pftest(); }
	if(eq(a[0],"stat")) return cmd_stat_builtin(a,n);
	if(eq(a[0],"which")) return cmd_which_builtin(a,n);
	if(eq(a[0],"mem")){if(n!=1){fail("usage: mem");return 2;}return cmd_mem();}
	if(eq(a[0],"cpuinfo")){if(n!=1){fail("usage: cpuinfo");return 2;}return cmd_cpuinfo();}
	if(eq(a[0],"sysinfo")){if(n!=1){fail("usage: sysinfo");return 2;}return cmd_sysinfo_builtin();}
	if(eq(a[0],"uptime")){if(n!=1){fail("usage: uptime");return 2;}return cmd_uptime();}
		if(eq(a[0],"help")){
		put("YabroOS-32 Shell 0.0.3-alpha\n\n");
		put("Navigation:  cd [DIR], pwd\n");
		put("Environment: export, unset, env; use $VAR and $? for expansion\n");
		put("Line editing: Up/Down history; arrows, Home/End, Ctrl+A/E/B/F/U/K/W/L; Tab completion\n");
		put("System:      stat, which, mem, cpuinfo, sysinfo, uptime, shutdown/poweroff, reboot, cls\n");
		put("Programs:    run FILE, pftest, exit\n");
		put("Scripting:   ;, &&, ||, |, <, >, >>, &, command history\n");
		put("Utilities:   ls cat echo mkdir rmdir cp rm mv touch head tail wc grep\n");
		put("             uname uptime ps kill whoami id true false sh\n");
		put("Development: nano FILE edits text\n");
		put("             tcc FILE.c -o FILE builds; nasm -f elf64 FILE.asm -o FILE.o\n");
		put("             ndisasm disassembles raw x86 machine code.\n");
		put("YabrusBox applets are external tools; cd is a shell builtin so it\n");
		put("can change this shell's working directory.\n");
		return 0;
	}
	return BUILTIN_NOT_FOUND;
}

static int is_builtin(const char *cmd){

	return eq(cmd,"cd")||eq(cmd,"shutdown")||eq(cmd,"poweroff")||eq(cmd,"reboot")||eq(cmd,"cls")||eq(cmd,"pftest")||eq(cmd,"run")||eq(cmd,"pwd")||eq(cmd,"export")||eq(cmd,"unset")||eq(cmd,"env")||eq(cmd,"history")||eq(cmd,"stat")||eq(cmd,"which")||eq(cmd,"mem")||eq(cmd,"cpuinfo")||eq(cmd,"sysinfo")||eq(cmd,"uptime")||eq(cmd,"exit")||eq(cmd,"help");
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

	const char *pv=env_value("PATH");
	if(!pv||!pv[0])pv="/";
	long i=0;
	while(1){
		char dir[128]; long d=0;
		while(pv[i]&&pv[i]!=':'&&d<127)dir[d++]=pv[i++];
		dir[d]=0;
		if(d==0)copy(dir,"/");
		join_path(out,dir,cmd);
		if(path_exists(out))return;
		if(!pv[i])break;
		i++;
	}

	resolve_elf(out,cmd);
}

static int launch(char **av,int n,int in_fd,int out_fd,long job_pgid){
	long pid=sc0(SYS_fork); if(pid<0){fail("fork failed");return -1;}
	if(pid==0){
		(void)sc2(SYS_setpgid,0,job_pgid>0?job_pgid:sc0(SYS_getpid));
		if(in_fd!=0) { if(sc2(SYS_dup2,in_fd,0)!=0)sc1(SYS_exit,126); sc1(SYS_close,in_fd); }
		if(out_fd!=1) { if(sc2(SYS_dup2,out_fd,1)!=1)sc1(SYS_exit,126); sc1(SYS_close,out_fd); }
		int br=builtin(av,n); if(br!=BUILTIN_NOT_FOUND){sc1(SYS_exit,br);for(;;){}}
		char path[128];resolve(path,av[0]);
		char *bbav[32]; char *envp[ENV_MAX+1]; build_envp(envp,ENV_MAX+1);
		char *exec_argv[32];
		if(eq(path,"/YABRUSBOX.ELF") && is_applet(av[0])){

			bbav[0]=av[0];
			for(int i=1;i<n && i<31;i++) bbav[i]=av[i];
			bbav[n<31?n:31]=0;
			for(int i=0;i<=n;i++) exec_argv[i]=bbav[i];
		} else {
			for(int i=0;i<n && i<31;i++) exec_argv[i]=av[i];
			exec_argv[n<31?n:31]=0;
		}
		long er=sc3(SYS_execve,(long)path,(long)exec_argv,(long)envp);
		put("ysh: exec failed: ");put(path);put(" rc=");print_num(er);put("\n");sc1(SYS_exit,127);for(;;){}
	}
	(void)sc2(SYS_setpgid,pid,job_pgid>0?job_pgid:pid);
	return (int)pid;
}

static char parse_storage[2048];

static int append_var_value(const char *name, char *out, long *o, long cap){
	const char *v=var_value(name);
	if(!v)return 0;
	while(*v && *o<cap-1)out[(*o)++]=*v++;
	return 1;
}

static int parse_word(const char **pp, char *out, long *o, long cap){
	const char *p=*pp;
	int single=0,dbl=0,had=0;
	while(*p){
		char c=*p;
		if(!single && !dbl && (c==' '||c=='\t'||c=='\n'||c=='|'||c=='>'||c=='<'))break;
		had=1;
		if(c=='\'' && !dbl){single=!single;p++;continue;}
		if(c=='"' && !single){dbl=!dbl;p++;continue;}
		if(c=='\\' && !single){
			p++;
			if(!*p)return -1;
			if(*o>=cap-1)return -1;
			out[(*o)++]=*p++;
			continue;
		}
		if(c=='$' && !single){
			char name[64];long n=0,j=1;
			if(p[1]=='?'){name[n++]='?';j=2;}
			else {
				while(p[j] && ((p[j]>='A'&&p[j]<='Z')||(p[j]>='a'&&p[j]<='z')||(p[j]>='0'&&p[j]<='9')||p[j]=='_') && n<63)name[n++]=p[j++];
			}
			if(n){
				name[n]=0;
				append_var_value(name,out,o,cap);
				p+=j;
				continue;
			}
		}
		if(*o>=cap-1)return -1;
		out[(*o)++]=c;
		p++;
	}
	if(single||dbl)return -1;
	*pp=p;
	return had?1:0;
}

static int parse(char *line,char *(*stages)[32],int *counts,char redirs[4][3][128]){
	int sc=0,n=0;
	const char *p=line;
	long wpos=0;
	for(int i=0;i<4;i++){counts[i]=0;for(int j=0;j<3;j++)redirs[i][j][0]=0;}
	while(*p){
		while(*p==' '||*p=='\t'||*p=='\n')p++;
		if(!*p)break;

		if(*p=='|'){
			if(n==0||sc>=3)return -1;
			stages[sc][n]=0; counts[sc]=n; sc++; n=0; p++; continue;
		}

		if(*p=='>'||*p=='<'){
			char op=*p++;
			int app=0;
			if(op=='>'&&*p=='>'){app=1;p++;}
			while(*p==' '||*p=='\t')p++;
			if(!*p||*p=='|'||*p=='>'||*p=='<')return -1;
			char target[128]; long to=0; const char *q=p;
			int rc=parse_word(&q,target,&to,sizeof(target));
			if(rc<=0||*q=='|'||*q=='>'||*q=='<')return -1;
			target[to]=0;
			long ri=(op=='<')?0:(app?2:1);
			for(long k=0;k<=to;k++)redirs[sc][ri][k]=target[k];
			p=q;
			continue;
		}

		if(n>=31)return -1;
		long begin=wpos;
		const char *q=p;
		int rc=parse_word(&q,parse_storage,&wpos,sizeof(parse_storage));
		if(rc<=0)return -1;
		if(wpos-begin>=128)return -1;
		parse_storage[wpos++]=0;
		stages[sc][n++]=parse_storage+begin;
		p=q;
	}
	if(n==0){if(sc>0)return -1;return 0;}
	stages[sc][n]=0; counts[sc]=n;
	return sc+1;
}

static int run_background=0;

static int run_parent_builtin_with_redirections(char **av,int ac,char redirs[3][128]){
	long infd=-1,outfd=-1,savedout=-1;
	int rc=1;
	if(redirs[0][0]){
		infd=sc3(SYS_open,(long)redirs[0],0,0);
		if(infd<0){fail("builtin: input redirection open failed");return 1;}
	}
	if(redirs[1][0] || redirs[2][0]){
		const char *path=redirs[2][0]?redirs[2]:redirs[1];
		int flags=O_WRONLY|O_CREAT|(redirs[2][0]?O_APPEND:O_TRUNC);
		outfd=sc3(SYS_open,(long)path,flags,0666);
		if(outfd<0){if(infd>=0)sc1(SYS_close,infd);fail("builtin: output redirection open failed");return 1;}
		savedout=sc1(SYS_dup,1);
		if(savedout<0 || sc2(SYS_dup2,outfd,1)!=1){
			if(savedout>=0)sc1(SYS_close,savedout);
			sc1(SYS_close,outfd);if(infd>=0)sc1(SYS_close,infd);
			fail("builtin: cannot set up output redirection");return 1;
		}
	}

	rc=builtin(av,ac);
	if(savedout>=0){
		if(sc2(SYS_dup2,savedout,1)!=1){fail("builtin: cannot restore stdout");rc=1;}
		sc1(SYS_close,savedout);
	}
	if(outfd>=0)sc1(SYS_close,outfd);
	if(infd>=0)sc1(SYS_close,infd);
	return rc;
}

static void run_simple(char *line){
	char *stages[4][32];int counts[4];char redirs[4][3][128];int ns=parse(line,stages,counts,redirs);if(ns<=0){if(ns<0)fail("syntax error");return;}
	if(!run_background && ns==1 && counts[0]>0 &&
	   (eq(stages[0][0],"cd")||eq(stages[0][0],"export")||eq(stages[0][0],"unset")||eq(stages[0][0],"exit")) &&
	   (redirs[0][0][0]||redirs[0][1][0]||redirs[0][2][0])){
		last_status=run_parent_builtin_with_redirections(stages[0],counts[0],redirs[0]);
		return;
	}
	if(!run_background && ns==1 && counts[0]>0 && !redirs[0][0][0]&&!redirs[0][1][0]&&!redirs[0][2][0]){
		const char *cmd=stages[0][0];
		if(is_builtin(cmd)){
			last_status=builtin(stages[0],counts[0]);
			return;
		}
	}
	int prev=0;int pids[4];int pc=0;long job_pgid=0;
	for(int i=0;i<ns;i++){
		int pipefd[2]={-1,-1};
		int out=1;
		if(i<ns-1){if(sc1(SYS_pipe,(long)pipefd)!=0){fail("pipe failed");return;}out=pipefd[1];}

		int redir_in=-1, redir_out=-1;
		if(redirs[i][0][0]) {
			redir_in=(int)sc3(SYS_open,(long)redirs[i][0],0,0);
			if(redir_in<0){fail("redirection: input open failed");if(i<ns-1){sc1(SYS_close,pipefd[0]);sc1(SYS_close,pipefd[1]);}if(prev)sc1(SYS_close,prev);return;}
		}
		if(redirs[i][1][0] || redirs[i][2][0]) {
			const char *path=redirs[i][2][0]?redirs[i][2]:redirs[i][1];
			int fl=O_WRONLY|O_CREAT|(redirs[i][2][0]?O_APPEND:O_TRUNC);
			redir_out=(int)sc3(SYS_open,(long)path,fl,0666);
			if(redir_out<0){fail("redirection: output open failed");if(redir_in>=0)sc1(SYS_close,redir_in);if(i<ns-1){sc1(SYS_close,pipefd[0]);sc1(SYS_close,pipefd[1]);}if(prev)sc1(SYS_close,prev);return;}
		}
		int child_in=redir_in>=0?redir_in:prev;
		int child_out=redir_out>=0?redir_out:out;
		int launched=launch(stages[i],counts[i],child_in,child_out,job_pgid);
		pids[pc++]=launched;
		if(job_pgid==0&&launched>0){
			job_pgid=launched;

			if(!run_background)shell_terminal_foreground(job_pgid);
		}
		if(redir_in>=0)sc1(SYS_close,redir_in);
		if(redir_out>=0)sc1(SYS_close,redir_out);
		if(prev)sc1(SYS_close,prev);
		if(i<ns-1){sc1(SYS_close,pipefd[1]);prev=pipefd[0];}
	}
	if(prev)sc1(SYS_close,prev);
	int last_st=0;
	if(run_background){
		if(pc>0 && pids[pc-1]>0){put("[bg ");print_num(pids[pc-1]);put("]\n");}
		last_status=0;
		return;
	}
	if(pc>0 && pids[pc-1]>0){
		shell_terminal_foreground(job_pgid);
		long r=sc4(SYS_wait4,pids[pc-1],(long)&last_st,0,0);
		if(r!=pids[pc-1]) last_st=127<<8;
	}
	for(int i=0;i<pc;i++){
		if(pids[i]<=0 || i==pc-1)continue;
		int child_st=0;
		(void)sc4(SYS_wait4,pids[i],(long)&child_st,0,0);
	}
	if(pc>0&&pids[pc-1]>0)last_status=(last_st>>8)&255;
	if(shell_pgid>0)shell_terminal_foreground(shell_pgid);

}

static int strip_background(char *s){
	long len=slen(s);
	int single=0,dbl=0,esc=0;
	long last_nonspace=-1;
	for(long i=0;i<len;i++){
		char c=s[i];
		if(esc){esc=0;continue;}
		if(c=='\\' && !single){esc=1;continue;}
		if(c=='"' && !single){dbl=!dbl;continue;}
		if(c=='\'' && !dbl){single=!single;continue;}
		if(single||dbl)continue;
		if(c!=' ' && c!='\t' && c!='\n' && c!='\r')last_nonspace=i;
	}
	if(last_nonspace>=0 && s[last_nonspace]=='&'){
		for(long i=last_nonspace;i<len;i++)s[i]=0;
		run_background=1;
		return 1;
	}
	run_background=0;
	return 0;
}

static int find_control(char *s, int *pos, int *oplen, int *opkind){
	int single=0, dbl=0, esc=0;
	int logic_pos=-1, logic_len=0, logic_kind=0;
	for(int i=0;s[i];i++){
		char c=s[i];
		if(esc){esc=0;continue;}
		if(c=='\\' && !single){esc=1;continue;}
		if(c=='"' && !single){dbl=!dbl;continue;}
		if(c=='\'' && !dbl){single=!single;continue;}
		if(single||dbl)continue;
		if(c==';'){
			*pos=i;*oplen=1;*opkind=1;return 1;
		}
		if(logic_pos<0 && c=='&'&&s[i+1]=='&'){
			logic_pos=i;logic_len=2;logic_kind=2;i++;
			continue;
		}
		if(logic_pos<0 && c=='|'&&s[i+1]=='|'){
			logic_pos=i;logic_len=2;logic_kind=3;i++;
			continue;
		}
	}
	if(logic_pos>=0){*pos=logic_pos;*oplen=logic_len;*opkind=logic_kind;return 1;}
	return 0;
}
static void run_line(char *line){
	strip_background(line);
	int bg_saved=run_background;

	int pos,oplen,opkind;
	if(!find_control(line,&pos,&oplen,&opkind)){
		run_simple(line);
		run_background=0;
		return;
	}
	if(bg_saved){fail("background operator must be at end of a simple command");run_background=0;last_status=2;return;}

	char left[512], right[512];
	long n=pos;
	if(n>=511)n=511;
	for(long i=0;i<n;i++) left[i]=line[i];
	left[n]=0;

	long j=pos+oplen,k=0;
	while(line[j] && k<511) right[k++]=line[j++];
	right[k]=0;

	while(n>0 && (left[n-1]==' '||left[n-1]=='	')) left[--n]=0;
	long r0=0;
	while(right[r0]==' '||right[r0]=='	') r0++;

	if(!left[0] || !right[r0]){
		fail("syntax error");
		last_status=2;
		return;
	}

	run_line(left);
	int st=last_status;
	if(opkind==1 || (opkind==2 && st==0) || (opkind==3 && st!=0))
		run_line(right+r0);
}

#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_HOME        0x100
#define KEY_END         0x101
#define KEY_DELETE      0x102
#define KEY_CTRL_LEFT   0x103
#define KEY_CTRL_RIGHT  0x104
#define KEY_ESCAPE      0x105
#define HISTORY_MAX 64
#define LINE_MAX 511

static char history[HISTORY_MAX][LINE_MAX + 1];
static int history_count=0;
static int history_pos=-1;
static char history_draft[LINE_MAX + 1];
static long history_draft_len=0, history_draft_pos=0;
static int history_draft_active=0;
static long line_render_pos=0;

static void line_redraw_prompt(const char *prompt, const char *line,
                              long len, long pos, long old_len){
	put("\r"); put(prompt);
	if(len>0) putn(line,len);
	if(old_len>len){for(long i=len;i<old_len;i++)put(" ");}
	put("\r"); put(prompt);
	for(long i=0;i<pos;i++) putn(&line[i],1);
	line_render_pos=pos;
}
static void line_redraw_after_listing(const char *prompt,const char *line,long len,long pos){
	put(prompt);if(len>0)putn(line,len);
	put("\r");put(prompt);for(long i=0;i<pos;i++)putn(&line[i],1);
	line_render_pos=pos;
}

static void line_redraw(const char *prompt,const char *line,long len,long pos,long old_len){
	line_redraw_prompt(prompt,line,len,pos,old_len);
}

#define COMPLETION_MAX 64
#define COMPLETION_NAME 128
struct completion_set { char names[COMPLETION_MAX][COMPLETION_NAME]; unsigned char is_dir[COMPLETION_MAX]; int count; };
struct ysh_dirent64 { uint64_t ino; int64_t off; uint16_t reclen; uint8_t type; char name[1]; } __attribute__((packed));

static void completion_add(struct completion_set *set,const char *name,int is_dir){
	if(!name||!name[0]||set->count>=COMPLETION_MAX)return;
	for(int i=0;i<set->count;i++)if(eq(set->names[i],name))return;
	long n=slen(name);if(n>=COMPLETION_NAME)return;
	for(long i=0;i<=n;i++)set->names[set->count][i]=name[i];
	set->is_dir[set->count]=(unsigned char)(is_dir!=0);set->count++;
}
static int completion_copy_join(char *out,long cap,const char *a,const char *b){
	long n=0;while(a&&a[n]&&n<cap-1){out[n]=a[n];n++;}
	long j=0;while(b&&b[j]&&n<cap-1){out[n++]=b[j++];}
	if(b&&b[j])return -1;out[n]=0;return 0;
}
static int completion_scan_dir(struct completion_set *set,const char *open_dir,
                               const char *token_dir,const char *leaf_prefix,
                               int command_mode){
	long fd=sc3(SYS_open,(long)open_dir,0,0);if(fd<0)return 0;
	char buf[2048];
	for(;;){long got=sc3(SYS_getdents64,fd,(long)buf,sizeof(buf));if(got<=0)break;
		long off=0;
		while(off+20<=got){
			uint8_t *rec=(uint8_t *)buf+off;
			uint16_t reclen=(uint16_t)(rec[16]|((uint16_t)rec[17]<<8));
			uint8_t dtype=rec[18];
			if(reclen<20||reclen>(uint16_t)(got-off))break;
			char name[COMPLETION_NAME];long n=0;
			while(19+n<reclen&&rec[19+n]&&n<COMPLETION_NAME-1){name[n]=(char)rec[19+n];n++;}name[n]=0;
			if(n>0&&!eq(name,".")&&!eq(name,"..")&&starts(name,leaf_prefix)){
				int is_dir=(dtype==4);
				if(!command_mode||(!is_dir&&(dtype==8||dtype==10))){
					char candidate[COMPLETION_NAME];
					if(completion_copy_join(candidate,sizeof(candidate),token_dir,name)==0){
						long cn=slen(candidate);
						if(is_dir&&cn+1<COMPLETION_NAME){candidate[cn++]='/';candidate[cn]=0;}
						completion_add(set,candidate,is_dir);
					}
				}
			}
			off+=reclen;
		}
	}
	(void)sc1(SYS_close,fd);return 0;
}
static int completion_command_position(const char *line,long start){
	long i=start-1;while(i>=0&&(line[i]==' '||line[i]=='\t'))i--;
	if(i<0||line[i]=='|'||line[i]=='&'||line[i]==';')return 1;
	return 0;
}
static void completion_add_builtins(struct completion_set *set,const char *prefix){
	static const char *names[]={
		"help","cd","pwd","export","unset","env","history","cls","shutdown","poweroff","reboot","pftest","run","stat","which","mem","cpuinfo","sysinfo","exit",
		"ls","cat","echo","mkdir","rmdir","cp","rm","mv","touch","head","tail","wc","grep","uname","uptime","ps","kill","whoami","id","true","false","sh","tcc","cc","yabrusbox","busybox",0};
	for(int i=0;names[i];i++)if(starts(names[i],prefix))completion_add(set,names[i],0);
}
static void line_tab_complete(const char *prompt,char *line,long *len,long *pos,long *old_len){
	if(!line||!len||!pos||!old_len||*pos<0||*pos>*len)return;

	long start=*pos,end=*pos;
	while(start>0){char c=line[start-1];if(c==' '||c=='\t'||c=='|'||c=='&'||c==';'||c=='<'||c=='>')break;start--;}
	while(end<*len){char c=line[end];if(c==' '||c=='\t'||c=='|'||c=='&'||c==';'||c=='<'||c=='>')break;end++;}
	long token_len=end-start;
	if(token_len<0||token_len>=COMPLETION_NAME)return;
	char token[COMPLETION_NAME];for(long i=0;i<token_len;i++)token[i]=line[start+i];token[token_len]=0;
	long slash=-1;for(long i=0;i<token_len;i++)if(token[i]=='/')slash=i;
	int command_mode=(slash<0&&completion_command_position(line,start));
	char token_dir[COMPLETION_NAME]="";char leaf[COMPLETION_NAME];
	const char *open_dir=".";
	if(slash>=0){
		for(long i=0;i<=slash;i++)token_dir[i]=token[i];token_dir[slash+1]=0;
		for(long i=0;i<token_len-slash-1;i++)leaf[i]=token[slash+1+i];leaf[token_len-slash-1]=0;
		if(slash==0)open_dir="/";
		else {token[slash]=0;open_dir=token;}
	}else{for(long i=0;i<=token_len;i++)leaf[i]=token[i];}
	struct completion_set set;set.count=0;
	if(command_mode){
		completion_add_builtins(&set,leaf);
		const char *path=env_value("PATH");if(!path||!path[0])path="/";
		long i=0;
		while(1){char dir[COMPLETION_NAME];long d=0;while(path[i]&&path[i]!=':'&&d<COMPLETION_NAME-1)dir[d++]=path[i++];dir[d]=0;if(!d)copy(dir,"/");
			(void)completion_scan_dir(&set,dir,"",leaf,1);
			if(!path[i])break;i++;
		}
	}else{
		if(slash<0){char cwd[COMPLETION_NAME];if(sc2(SYS_getcwd,(long)cwd,sizeof(cwd))<0)copy(cwd,"/");open_dir=cwd;token_dir[0]=0;}
		(void)completion_scan_dir(&set,open_dir,token_dir,leaf,0);
	}
	if(set.count==0)return;
	long typed=token_len;
	const char *replacement=0;long replacement_len=0;int add_space=0;
	if(set.count==1){
		replacement=set.names[0];replacement_len=slen(replacement);add_space=!set.is_dir[0]&&end==*len;
	}else{
		long common=slen(set.names[0]);
		for(int i=1;i<set.count;i++){long j=0;while(j<common&&set.names[i][j]==set.names[0][j])j++;common=j;}
		if(common>typed&&common<=COMPLETION_NAME-1){replacement=set.names[0];replacement_len=common;}
		else{
			put("\n");
			for(int i=0;i<set.count;i++){put(set.names[i]);put("\n");}
			line_redraw_after_listing(prompt,line,*len,*pos);*old_len=*len;return;
		}
	}
	long tail_len=*len-end;
	long newlen=start+replacement_len+tail_len+(add_space?1:0);
	if(newlen>LINE_MAX)return;

	long new_tail=start+replacement_len+(add_space?1:0);
	if(new_tail>end){for(long i=tail_len-1;i>=0;i--)line[new_tail+i]=line[end+i];}
	else {for(long i=0;i<tail_len;i++)line[new_tail+i]=line[end+i];}
	for(long i=0;i<replacement_len;i++)line[start+i]=replacement[i];
	if(add_space)line[start+replacement_len]=' ';
	*len=newlen;*pos=start+replacement_len+(add_space?1:0);line[newlen]=0;
	line_redraw_prompt(prompt,line,*len,*pos,*old_len);*old_len=*len;
}

static void prompt_append(char *out,long cap,long *used,const char *text){
	while(*text && *used<cap-1) out[(*used)++]=*text++;
	out[*used]=0;
}

static void make_prompt(char *out,long cap,int sh_compat_mode){
	long used=0; char cwd[128]; out[0]=0;
	prompt_append(out,cap,&used,sh_compat_mode?"root@YabroOS-32:":"YabroOS-32:");
	if(sc2(SYS_getcwd,(long)cwd,sizeof(cwd))<0) copy(cwd,"?");
	prompt_append(out,cap,&used,cwd);
	prompt_append(out,cap,&used,sh_compat_mode?"# ":" $ ");
}

struct shell_pollfd { int fd; short events; short revents; };
#define SHELL_POLLIN 0x001

static int read_byte_timeout(unsigned char *out,int timeout_ms){
	struct shell_pollfd pfd;
	pfd.fd=0;pfd.events=SHELL_POLLIN;pfd.revents=0;
	long ready=sc3(SYS_poll,(long)&pfd,1,timeout_ms);
	if(ready==0)return 0;
	if(ready<0)return -1;
	if(!(pfd.revents&SHELL_POLLIN))return -1;
	long r=sc3(SYS_read,0,(long)out,1);
	if(r==1)return 1;
	if(r==0||r==-11)return 0;
	return -1;
}

static int csi_modifier_has_ctrl(const unsigned char *seq,long n){
	int value=0,have=0;
	for(long i=0;i<n-1;i++){
		unsigned char c=seq[i];
		if(c>='0'&&c<='9'){if(value<10000)value=value*10+(int)(c-'0');have=1;}
		else if(c==';'){value=0;have=0;}
	}
	return have && value>1 && (((value-1)&4)!=0);
}

static int decode_csi_key(void){
	unsigned char seq[16];
	long n=0;
	while(n<(long)sizeof(seq)){
		unsigned char c=0;
		int got=read_byte_timeout(&c,50);
		if(got!=1)return KEY_ESCAPE;
		seq[n++]=c;
		if(c>=0x40&&c<=0x7e)break;
	}
	if(n==0||!(seq[n-1]>=0x40&&seq[n-1]<=0x7e))return KEY_ESCAPE;
	unsigned char final=seq[n-1];
	if(final=='A')return KEY_UP;
	if(final=='B')return KEY_DOWN;
	if(final=='C')return csi_modifier_has_ctrl(seq,n)?KEY_CTRL_RIGHT:KEY_RIGHT;
	if(final=='D')return csi_modifier_has_ctrl(seq,n)?KEY_CTRL_LEFT:KEY_LEFT;
	if(final=='H')return KEY_HOME;
	if(final=='F')return KEY_END;
	if(final=='~'){
		int value=0;
		for(long i=0;i<n-1&&seq[i]>='0'&&seq[i]<='9';i++){if(value<10000)value=value*10+(int)(seq[i]-'0');}
		if(value==1||value==7)return KEY_HOME;
		if(value==4||value==8)return KEY_END;
		if(value==3)return KEY_DELETE;
	}
	return KEY_ESCAPE;
}

static int decode_escape_key(void){
	unsigned char c=0;
	int got=read_byte_timeout(&c,50);
	if(got!=1)return KEY_ESCAPE;
	if(c=='[')return decode_csi_key();
	if(c=='O'){
		got=read_byte_timeout(&c,50);
		if(got!=1)return KEY_ESCAPE;
		switch(c){
			case 'A':return KEY_UP;case 'B':return KEY_DOWN;
			case 'C':return KEY_RIGHT;case 'D':return KEY_LEFT;
			case 'H':return KEY_HOME;case 'F':return KEY_END;
		}
	}
	return KEY_ESCAPE;
}

static int read_shell_key(void){
	unsigned char c=0;
	for(;;){
		long r=sc3(SYS_read,0,(long)&c,1);
		if(r==1){
			if(c==27)return decode_escape_key();
			return (int)c;
		}
		if(r==-11){(void)sc0(SYS_sched_yield);continue;}
		if(r==0){(void)sc0(SYS_sched_yield);continue;}
		return -1;
	}
}
static int line_word_space(char c){return c==' '||c=='\t';}
static void line_history_move(const char *prompt,char *line,long *len,long *pos,long *old_len,int direction){
	if(direction<0){
		if(history_count<=0)return;
		if(history_pos<0){history_draft_len=*len;history_draft_pos=*pos;for(long i=0;i<*len;i++)history_draft[i]=line[i];history_draft[*len]=0;history_draft_active=1;history_pos=history_count-1;}
		else if(history_pos>0)history_pos--;
		else return;
		long n=slen(history[history_pos]);if(n>LINE_MAX)n=LINE_MAX;
		for(long i=0;i<n;i++)line[i]=history[history_pos][i];line[n]=0;*len=n;*pos=n;
	}else{
		if(history_pos<0)return;
		if(history_pos<history_count-1){history_pos++;long n=slen(history[history_pos]);if(n>LINE_MAX)n=LINE_MAX;for(long i=0;i<n;i++)line[i]=history[history_pos][i];line[n]=0;*len=n;*pos=n;}
		else{history_pos=-1;if(history_draft_active){*len=history_draft_len;*pos=history_draft_pos;for(long i=0;i<*len;i++)line[i]=history_draft[i];line[*len]=0;}else{*len=*pos=0;line[0]=0;}history_draft_active=0;}
	}
	line_redraw_prompt(prompt,line,*len,*pos,*old_len);*old_len=*len;
}
static int line_editor_key(const char *prompt,char *line,long *len,long *pos,long *old_len,int key){
	int changed=0;
	if(key==KEY_UP||key==16){line_history_move(prompt,line,len,pos,old_len,-1);return 1;}
	if(key==KEY_DOWN||key==14){line_history_move(prompt,line,len,pos,old_len,1);return 1;}
	if(key==KEY_LEFT||key==2){if(*pos>0)(*pos)--;changed=1;}
	else if(key==KEY_RIGHT||key==6){if(*pos<*len)(*pos)++;changed=1;}
	else if(key==KEY_HOME||key==1){*pos=0;changed=1;}
	else if(key==KEY_END||key==5){*pos=*len;changed=1;}
	else if(key==KEY_CTRL_LEFT){while(*pos>0&&line_word_space(line[*pos-1]))(*pos)--;while(*pos>0&&!line_word_space(line[*pos-1]))(*pos)--;changed=1;}
	else if(key==KEY_CTRL_RIGHT){while(*pos<*len&&!line_word_space(line[*pos]))(*pos)++;while(*pos<*len&&line_word_space(line[*pos]))(*pos)++;changed=1;}
	else if(key=='\t'){line_tab_complete(prompt,line,len,pos,old_len);return 1;}
	else if(key==8||key==127){if(*pos>0){for(long i=*pos;i<*len;i++)line[i-1]=line[i];(*pos)--;(*len)--;line[*len]=0;}changed=1;}
	else if(key==KEY_DELETE||key==4){if(*pos<*len){for(long i=*pos+1;i<*len;i++)line[i-1]=line[i];(*len)--;line[*len]=0;}changed=1;}
	else if(key==23){while(*pos>0&&line_word_space(line[*pos-1])){for(long i=*pos;i<*len;i++)line[i-1]=line[i];(*pos)--;(*len)--;}while(*pos>0&&!line_word_space(line[*pos-1])){for(long i=*pos;i<*len;i++)line[i-1]=line[i];(*pos)--;(*len)--;}line[*len]=0;changed=1;}
	else if(key==21){for(long i=*pos;i<*len;i++)line[i-*pos]=line[i];*len-=*pos;*pos=0;line[*len]=0;changed=1;}
	else if(key==11){*len=*pos;line[*len]=0;changed=1;}
	else if(key==12){
		terminal_clear_screen();
		line_render_pos=0;long none=0;line_redraw_prompt(prompt,line,*len,*pos,none);*old_len=*len;return 1;
	}
	else if(key==KEY_ESCAPE){return 1;}
	else if(key>=32&&key<127&&*len<LINE_MAX){for(long i=*len;i>*pos;i--)line[i]=line[i-1];line[*pos]=(char)key;(*pos)++;(*len)++;line[*len]=0;changed=1;}
	if(changed){line_redraw_prompt(prompt,line,*len,*pos,*old_len);*old_len=*len;}
	return 1;
}

static void history_save(const char *line, long len){
	if(len<=0) return;
	if(history_count>0){
		long last=slen(history[history_count-1]);
		if(last==len){
			long i=0; while(i<len && history[history_count-1][i]==line[i]) i++;
			if(i==len) return;
		}
	}
	if(history_count<HISTORY_MAX){
		for(long i=0;i<len;i++) history[history_count][i]=line[i];
		history[history_count][len]=0;
		history_count++;
	} else {
		for(int h=1;h<HISTORY_MAX;h++) copy(history[h-1],history[h]);
		for(long i=0;i<len;i++) history[HISTORY_MAX-1][i]=line[i];
		history[HISTORY_MAX-1][len]=0;
	}
}

static int cmd_history_builtin(char **a,int n){
	if(n==2&&eq(a[1],"-c")){history_count=0;history_pos=-1;history_draft_active=0;put("history cleared\n");return 0;}
	if(n!=1){fail("usage: history [-c]");return 2;}
	for(int i=0;i<history_count;i++){put("  ");print_num(i+1);put("  ");put(history[i]);put("\n");}
	return 0;
}

int ysh_main(int argc, char **argv, char **envp){

	int sh_compat_mode = argv && argv[0] &&
		(eq(argv[0],"/bin/sh") || eq(argv[0],"sh"));
	env_init();

	if(envp){
		for(int i=0;envp[i] && i<128;i++){
			long n=slen(envp[i]);
			if(n<=1 || n>=128) continue;
			char entry[128];copy(entry,envp[i]);
			long k=0;while(entry[k] && entry[k]!='=')k++;
			if(!entry[k] || k==0) continue;
			entry[k]=0;

			int already_present=0;
			for(int j=0;j<env_count;j++){
				long m=0;while(env_store[j][m] && env_store[j][m]!='=' && m<127)m++;
				if(m==k){long t=0;while(t<k && env_store[j][t]==entry[t])t++;if(t==k){already_present=1;break;}}
			}
			if(!already_present && env_count>=ENV_MAX-3) continue;
			(void)env_set(entry,entry+k+1);
		}
	}
	char initial_cwd[128];
	if(sc2(SYS_getcwd,(long)initial_cwd,sizeof(initial_cwd))>=0)
		(void)env_set("PWD",initial_cwd);
	if(argc >= 3 && argv && argv[1] && eq(argv[1], "-c")){
		char line[LINE_MAX + 1];
		copy(line, argv[2]);
		run_line(line);
		return last_status;
	}
	shell_terminal_init();
	if(!sh_compat_mode){put("YabroOS-32 Shell 0.0.3-alpha\n");put("Type 'help' for commands.\n\n");}
	char line[LINE_MAX + 1];

	for(;;){
		char prompt[192];make_prompt(prompt,sizeof(prompt),sh_compat_mode);
		put(prompt);
		line_render_pos=0;
		long pos=0, len=0, old_len=0;int cancelled=0;
		history_pos=-1;
		cursor_show();
		for(;;){
			int key=read_shell_key();
			if(key<0){cursor_hide();shell_terminal_cooked();sc1(SYS_exit_group,0);for(;;){}}
			if(key==3){cursor_hide();put("^C\n");len=pos=0;line[0]=0;old_len=0;cancelled=1;history_pos=-1;history_draft_active=0;break;}
			cursor_hide();
			if(key=='\r'||key=='\n'){
				put("\n");line[len]=0;if(len)history_save(line,len);history_pos=-1;history_draft_active=0;break;
			}
			if(key==4&&len==0){cursor_hide();shell_terminal_cooked();sc1(SYS_exit_group,0);for(;;){}}
			(void)line_editor_key(prompt,line,&len,&pos,&old_len,key);
			cursor_show();
		}
		if(len && !cancelled){shell_terminal_cooked();run_line(line);shell_terminal_raw();}
	}
}

__attribute__((naked,noreturn)) void _start(void){
	__asm__ volatile(
		"mov (%rsp), %rdi\n"
		"lea 8(%rsp), %rsi\n"
		"lea 16(%rsp,%rdi,8), %rdx\n"
		"and $-16, %rsp\n"
		"call ysh_main\n"
		"mov %eax, %edi\n"
		"mov $60, %eax\n"
		"syscall\n"
		"ud2\n"
	);
}
