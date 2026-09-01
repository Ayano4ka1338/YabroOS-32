//Yabrus SHell - (c) Ayano4ka1338, 2026
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
#define SYS_unlink 87
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
static char env_store[16][128];
static int env_count=0;
static int last_status=0;

static void env_init(void){
	env_count=0;
	copy(env_store[env_count++],"PATH=/bin");
	copy(env_store[env_count++],"HOME=/");
	copy(env_store[env_count++],"SHELL=/bin/sh");
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
	if(env_count>=16)return -1;
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
		char *envp[17]; build_envp(envp,17);
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
	if(eq(a[0],"pwd")){char b[256];long r=sc2(SYS_getcwd,(long)b,sizeof(b));if(r<0){fail("pwd: getcwd failed");return 1;}putn(b,r);put("\n");return 0;}
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
	if(eq(a[0],"stat")) return cmd_stat_builtin(a,n);
	if(eq(a[0],"which")) return cmd_which_builtin(a,n);
	if(eq(a[0],"help")){
		put("builtins: cd pwd export unset env stat which run help exit\n");
		put("Use $VAR and $? for expansion. PATH is /bin.\n");
		put("YabrusBox applets: ls cat pwd echo env mkdir rmdir cp rm mv touch head tail wc grep uname uptime ps kill whoami id true false sh\n");
		return 0;
	}
	return BUILTIN_NOT_FOUND;
}

static int is_builtin(const char *cmd){

	return eq(cmd,"run")||eq(cmd,"cd")||eq(cmd,"pwd")||eq(cmd,"export")||eq(cmd,"unset")||eq(cmd,"env")||eq(cmd,"stat")||eq(cmd,"which")||eq(cmd,"exit")||eq(cmd,"help");
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

static int launch(char **av,int n,int in_fd,int out_fd){
	long pid=sc0(SYS_fork); if(pid<0){fail("fork failed");return -1;}
	if(pid==0){
		if(in_fd!=0) { if(sc2(SYS_dup2,in_fd,0)!=0)sc1(SYS_exit,126); sc1(SYS_close,in_fd); }
		if(out_fd!=1) { if(sc2(SYS_dup2,out_fd,1)!=1)sc1(SYS_exit,126); sc1(SYS_close,out_fd); }
		int br=builtin(av,n); if(br!=BUILTIN_NOT_FOUND){sc1(SYS_exit,br);for(;;){}}
		char path[128];resolve(path,av[0]);
		char *bbav[12]; char *envp[17]; build_envp(envp,17);
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

static int parse(char *line,char *(*stages)[8],int *counts,char redirs[4][3][128]){
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

		if(n>=7)return -1;
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

static void run_simple(char *line){
	char *stages[4][8];int counts[4];char redirs[4][3][128];int ns=parse(line,stages,counts,redirs);if(ns<=0){if(ns<0)fail("syntax error");return;}
	if(ns==1 && counts[0]>0 && !redirs[0][0][0]&&!redirs[0][1][0]&&!redirs[0][2][0]){
		const char *cmd=stages[0][0];
		if(is_builtin(cmd)){
			last_status=builtin(stages[0],counts[0]);
			return;
		}
	}
	int prev=0;int pids[4];int pc=0;
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
		pids[pc++]=launch(stages[i],counts[i],child_in,child_out);
		if(redir_in>=0)sc1(SYS_close,redir_in);
		if(redir_out>=0)sc1(SYS_close,redir_out);
		if(prev)sc1(SYS_close,prev);
		if(i<ns-1){sc1(SYS_close,pipefd[1]);prev=pipefd[0];}
	}
	if(prev)sc1(SYS_close,prev);
	int last_st=0;
	if(pc>0 && pids[pc-1]>0){

		long r=sc4(SYS_wait4,pids[pc-1],(long)&last_st,0,0);
		if(r!=pids[pc-1]) last_st=127<<8;
	}
	for(int i=0;i<pc;i++){
		if(pids[i]<=0 || i==pc-1)continue;
		int child_st=0;
		(void)sc4(SYS_wait4,pids[i],(long)&child_st,0,0);
	}
	if(pc>0&&pids[pc-1]>0)last_status=(last_st>>8)&255;
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


	int pos,oplen,opkind;
	if(!find_control(line,&pos,&oplen,&opkind)){
		run_simple(line);
		return;
	}

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

#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)
#define HISTORY_MAX 16
#define LINE_MAX 511

static char history[HISTORY_MAX][LINE_MAX + 1];
static int history_count=0;
static int history_pos=-1;

static void line_redraw(const char *line, long len, long pos, long old_len){


	put("\rYabroOS-32> ");
	if(len) putn(line,len);
	if(old_len>len){
		for(long i=len;i<old_len;i++) put(" ");
	}
	put("\rYabroOS-32> ");
	for(long i=0;i<pos;i++) putn(&line[i],1);
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

void _start(void){
	env_init();
	put("YabroOS-32 YSH 0.0.2-alpha\n");put("type 'help' for commands\n");
	char line[LINE_MAX + 1];
	for(;;){
		put("YabroOS-32> ");
		long pos=0, len=0, old_len=0;
		history_pos=-1;
		cursor_show();
		for(;;){
			char c=0;
			long r=sc3(SYS_read,0,(long)&c,1);
			if(r==-11){sc0(SYS_sched_yield);continue;}
			if(r<=0){cursor_hide();sc1(SYS_exit_group,0);for(;;){}}

			if(c=='\r')continue;
			cursor_hide();

			if(c=='\n'){
				put("\n");
				line[len]=0;
				if(len) history_save(line,len);
				break;
			}

			if(c==(char)0x80 || c==(char)0x81){
				int target=history_pos;
				if(c==(char)0x80){
					if(history_count==0) { cursor_show(); continue; }
					if(target<0) target=history_count-1;
					else if(target>0) target--;
					else { cursor_show(); continue; }
				} else {
					if(target<0) { cursor_show(); continue; }
					if(target<history_count-1) target++;
					else {
						history_pos=-1; len=0; pos=0;
						line[0]=0;
						line_redraw(line,len,pos,old_len); old_len=len;
						cursor_show(); continue;
					}
				}
				history_pos=target;
				len=slen(history[target]);
				if(len>LINE_MAX) len=LINE_MAX;
				for(long i=0;i<len;i++) line[i]=history[target][i];
				line[len]=0; pos=len;
				line_redraw(line,len,pos,old_len); old_len=len;
				cursor_show(); continue;
			}

			if(c==(char)0x82){
				if(pos>0) pos--;
				line_redraw(line,len,pos,old_len); old_len=len;
				cursor_show(); continue;
			}
			if(c==(char)0x83){
				if(pos<len) pos++;
				line_redraw(line,len,pos,old_len); old_len=len;
				cursor_show(); continue;
			}

			if(c==8||c==127){
				if(pos>0){
					for(long i=pos;i<len;i++) line[i-1]=line[i];
					pos--; len--;
					line[len]=0;
					line_redraw(line,len,pos,old_len); old_len=len;
				}
				cursor_show();
				continue;
			}

			if((unsigned char)c>=32 && (unsigned char)c<127 && len<LINE_MAX){
				for(long i=len;i>pos;i--) line[i]=line[i-1];
				line[pos++]=c; len++; line[len]=0;
				line_redraw(line,len,pos,old_len); old_len=len;
			}
			cursor_show();
		}
		if(len) run_line(line);
	}
}
