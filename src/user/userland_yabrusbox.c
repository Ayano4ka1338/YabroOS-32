//Yabrus utils box - (c) Ayano4ka1338, 2026
#include <stdint.h>
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_lseek 8
#define SYS_stat 4
#define SYS_unlink 87
#define SYS_rename 82
#define SYS_mkdir 83
#define SYS_rmdir 84
#define SYS_kill 62
#define SYS_getdents64 217
#define SYS_getcwd 79
#define SYS_chdir 80
#define SYS_uname 63
#define SYS_execve 59
#define SYS_exit_group 231
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024
#define AT_FDCWD -100
#define S_IFMT 0170000
#define S_IFDIR 0040000
struct kstat { uint64_t dev,ino,nlink,mode,uid,gid,rdev,size,blksize,blocks,atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns; uint32_t unused[2]; };

static char **g_envp = (char**)0;
static int g_envc = 0;

static long sc0(long n){long r;__asm__ volatile("syscall":"=a"(r):"a"(n):"rcx","r11","memory");return r;}
static long sc1(long n,long a){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory");return r;}
static long sc2(long n,long a,long b){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory");return r;}
static long sc3(long n,long a,long b,long c){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static void putn(const char*s,long n){if(n>0)sc3(SYS_write,1,(long)s,n);}
static void put(const char*s){long n=0;while(s[n])n++;putn(s,n);}
static int eq(const char*a,const char*b){long i=0;while(a[i]&&b[i]&&a[i]==b[i])i++;return a[i]==b[i];}
static long slen(const char*s){long n=0;while(s[n])n++;return n;}
static void num(long v){char b[32],t[24];long i=0,j=0;unsigned long x=(unsigned long)(v<0?-v:v);if(v<0)b[i++]='-';if(!x)t[j++]='0';while(x){t[j++]=(char)('0'+x%10);x/=10;}while(j)b[i++]=t[--j];b[i]=0;put(b);}
static void err(const char*cmd,const char*path){put("yabrusbox: ");put(cmd);put(": ");put(path);put("\n");}
static void exitc(long c){sc1(SYS_exit_group,c);for(;;)__asm__ volatile("hlt");}

static int cmd_echo(int ac,char**av){
	int first=2, newline=1;
	if(ac>2 && eq(av[2],"-n")){ newline=0; first=3; }
	for(int i=first;i<ac;i++){if(i>first)put(" ");put(av[i]);}
	if(newline)put("\n");
	return 0;
}
static int cat_fd(long fd){
	char b[1024];
	int rc=0;
	for(;;){
		long n=sc3(SYS_read,fd,(long)b,sizeof(b));
		if(n<0){rc=1;break;}
		if(n==0)break;
		putn(b,n);
	}
	return rc;
}
static int cmd_cat(int ac,char**av){
	int rc=0;

	int first=2;
	if(ac>0 && eq(av[0],"cat")) first=1;
	if(ac<=first) return cat_fd(0);
	for(int i=first;i<ac;i++){
		if(eq(av[i],"-")) { if(cat_fd(0))rc=1; continue; }
		long fd=sc3(SYS_open,(long)av[i],0,0);
		if(fd<0){err("cat",av[i]);rc=1;continue;}
		if(cat_fd(fd))rc=1;
		sc1(SYS_close,fd);
	}
	return rc;
}
static int make_ls_path(const char *arg, char *out, long cap){
	if(!arg || !out || cap < 2) return -1;
	if(arg[0]=='/') { long i=0; while(arg[i] && i<cap-1){out[i]=arg[i];i++;} out[i]=0; return arg[i]? -1:0; }
	if(eq(arg,".") || arg[0]==0){
		long n=sc2(SYS_getcwd,(long)out,cap);
		if(n<0) return -1;
		return 0;
	}

	char cwd[256];
	long n=sc2(SYS_getcwd,(long)cwd,sizeof(cwd));
	if(n<0) return -1;
	long pos=0;
	while(cwd[pos] && pos<cap-1){out[pos]=cwd[pos];pos++;}
	if(pos>1 && pos<cap-1) out[pos++]='/';
	else if(pos==1 && out[0]=='/' ) {  }
	for(long i=0;arg[i] && pos<cap-1;i++) out[pos++]=arg[i];
	if(arg[0] && pos<cap-1) out[pos]=0; else out[pos]=0;
	if (pos >= cap) return -1;
	out[pos]=0;
	return 0;
}

static void ls_print_name(const char *name, unsigned char type, int longfmt){
	if(longfmt){
		if(type==4) put("d ");
		else if(type==10) put("l ");
		else put("- ");
	}
	put(name);
	if(type==4) put("/");
	else if(type==10) put("@");
	put("\n");
}

static int name_cmp(const char *a,const char *b){
	long i=0;
	while(a[i] && b[i]){
		unsigned char ca=(unsigned char)a[i], cb=(unsigned char)b[i];
		if(ca>='a'&&ca<='z') ca=(unsigned char)(ca-'a'+'A');
		if(cb>='a'&&cb<='z') cb=(unsigned char)(cb-'a'+'A');
		if(ca!=cb) return ca<cb?-1:1;
		i++;
	}
	if(!a[i]&&!b[i]) return 0;
	return a[i]?1:-1;
}

static int ls_one(const char *arg, int all, int longfmt){
	char path[256];
	if(make_ls_path(arg,path,sizeof(path))<0){err("ls",arg);return 1;}
	long fd=sc3(SYS_open,(long)path,0,0);
	if(fd<0){err("ls",arg);return 1;}

	char names[128][64];
	unsigned char types[128];
	int count=0,rc=0;
	uint8_t b[4096];
	for(;;){
		long n=sc3(SYS_getdents64,fd,(long)b,sizeof(b));
		if(n<0){rc=1;break;}
		if(n==0)break;
		long off=0;
		while(off<n){
			if(n-off<19){rc=1;break;}
			uint16_t reclen=(uint16_t)b[off+16] | ((uint16_t)b[off+17]<<8);
			unsigned char type=b[off+18];
			if(reclen<24 || (reclen&7) || reclen>n-off){rc=1;break;}
			char *name=(char*)(b+off+19);
			long max=(long)reclen-19, len=0;
			while(len<max && name[len])len++;
			if(len>=max || len>=63){rc=1;break;}
			int hidden=(len==1&&name[0]=='.') ||
					   (len==2&&name[0]=='.'&&name[1]=='.');
			if(all || !hidden){
				if(count>=128){rc=1;break;}
				for(long i=0;i<=len;i++) names[count][i]=name[i];
				types[count]=type;
				count++;
			}
			off+=reclen;
		}
		if(rc)break;
	}
	sc1(SYS_close,fd);
	if(rc){err("ls",arg);return 1;}

	for(int i=1;i<count;i++){
		int j=i;
		while(j>0 && name_cmp(names[j-1],names[j])>0){
			char tmp[64]; unsigned char tt=types[j];
			for(int k=0;k<64;k++) tmp[k]=names[j][k];
			for(int k=0;k<64;k++) names[j][k]=names[j-1][k];
			for(int k=0;k<64;k++) names[j-1][k]=tmp[k];
			types[j]=types[j-1]; types[j-1]=tt;
			j--;
		}
	}
	for(int i=0;i<count;i++) ls_print_name(names[i],types[i],longfmt);
	return 0;
}
static int cmd_ls(int ac,char**av){
	int all=0,longfmt=0,first=2,rc=0,targets=0;
	for(int i=2;i<ac;i++){
		const char *a=av[i];
		if(a[0]=='-' && a[1]){
			for(int j=1;a[j];j++){
				if(a[j]=='a') all=1;
				else if(a[j]=='l') longfmt=1;
				else {put("ls: invalid option -");putn(&a[j],1);put("\n");return 2;}
			}
			first=i+1;
		}
	}
	for(int i=first;i<ac;i++){
		if(av[i][0]=='-') continue;
		targets++;
		if(targets>1){put("\n");put(av[i]);put(":\n");}
		if(ls_one(av[i],all,longfmt)) rc=1;
	}
	if(!targets) rc=ls_one(".",all,longfmt);
	return rc;
}
static int cmd_pwd(void){char b[256];long n=sc2(SYS_getcwd,(long)b,sizeof(b));if(n<0)return 1;putn(b,n);put("\n");return 0;}
static int cmd_mkdir(int ac,char**av){int rc=0;for(int i=2;i<ac;i++)if(sc2(SYS_mkdir,(long)av[i],0755)<0){err("mkdir",av[i]);rc=1;}return rc;}
static const char *base_name_b(const char *p){const char *b=p?p:"";if(!p)return "";for(long i=0;p[i];i++)if(p[i]=='/')b=p+i+1;return b;}
static int stat_is_dir_b(const char *p){struct kstat st;long r=sc2(SYS_stat,(long)p,(long)&st);return r==0 && ((st.mode&S_IFMT)==S_IFDIR);}
static int make_child_path_b(const char *dir,const char *name,char *out,long cap){long n=0;if(!dir||!name||!out||cap<2)return -1;while(dir[n]&&n<cap-1){out[n]=dir[n];n++;}if(dir[n])return -1;if(n&&out[n-1]!='/'){if(n+1>=cap)return -1;out[n++]='/';}for(long i=0;name[i];i++){if(n+1>=cap)return -1;out[n++]=name[i];}out[n]=0;return 0;}
static int rm_path_b(const char *path,int recursive,int force){
	struct kstat st;
	long sr=sc2(SYS_stat,(long)path,(long)&st);
	if(sr<0){if(force)return 0;err("rm",path);return 1;}
	if((st.mode&S_IFMT)==S_IFDIR){
		if(!recursive){put("yabrusbox: rm: cannot remove directory: ");put(path);put("\n");return 1;}
		long fd=sc3(SYS_open,(long)path,0,0); if(fd<0){err("rm",path);return 1;}
		uint8_t b[2048]; int rc=0;
		for(;;){long n=sc3(SYS_getdents64,fd,(long)b,sizeof(b));if(n<0){rc=1;break;}if(n==0)break;long off=0;while(off<n){if(n-off<19){rc=1;break;}uint16_t rl=(uint16_t)b[off+16]|((uint16_t)b[off+17]<<8);if(rl<24||(rl&7)||rl>n-off){rc=1;break;}char *name=(char*)(b+off+19);if(name[0]!='.'||name[1]!='\0'&&!(name[1]=='.'&&name[2]=='\0')){char child[256];long j=0;while(path[j]&&j<250){child[j]=path[j];j++;}if(j&&child[j-1]!='/')child[j++]='/';for(long k=0;name[k]&&j<255;k++)child[j++]=name[k];child[j]=0;if(rm_path_b(child,1,force))rc=1;}off+=rl;}if(rc)break;}
		sc1(SYS_close,fd); if(rc)return 1;
		long r=sc1(SYS_rmdir,(long)path); if(r<0&&!force){err("rm",path);return 1;} return 0;
	}
	long r=sc1(SYS_unlink,(long)path); if(r<0&&!force){err("rm",path);return 1;} return 0;
}
static int cmd_rm(int ac,char**av){
	int force=0,recursive=0,first=2;
	while(first<ac && av[first][0]=='-' && av[first][1]){for(long j=1;av[first][j];j++){if(av[first][j]=='f')force=1;else if(av[first][j]=='r'||av[first][j]=='R')recursive=1;else {put("yabrusbox: rm: invalid option -");putn(&av[first][j],1);put("\n");return 2;}}first++;}
	if(first>=ac){put("usage: rm [-rf] FILE...\n");return 2;}
	int rc=0;for(int i=first;i<ac;i++)if(rm_path_b(av[i],recursive,force))rc=1;return rc;
}
static int mv_one_b(const char *src,const char *dst){
	if(eq(src,dst))return 0;
	long r=sc2(SYS_rename,(long)src,(long)dst);
	if(r<0){put("yabrusbox: mv: ");put(src);put(": rename failed\n");return 1;}
	return 0;
}
static int cmd_mv(int ac,char**av){
	if(ac<4){put("usage: mv SOURCE... DEST\n");return 2;}
	const char *dest=av[ac-1];int multi=(ac>4);if(multi&&!stat_is_dir_b(dest)){put("yabrusbox: mv: destination is not a directory\n");return 1;}
	int rc=0;char target[256];
	for(int i=2;i<ac-1;i++){
		const char *out=dest;
		if(stat_is_dir_b(dest)){if(make_child_path_b(dest,base_name_b(av[i]),target,sizeof(target))<0){err("mv",dest);rc=1;continue;}out=target;}
		if(mv_one_b(av[i],out))rc=1;
	}
	return rc;
}
static int cmd_touch(int ac,char**av){int rc=0;for(int i=2;i<ac;i++){long fd=sc3(SYS_open,(long)av[i],O_CREAT,0666);if(fd<0){err("touch",av[i]);rc=1;continue;}sc1(SYS_close,fd);}return rc;}
static int cmd_head(int ac,char**av){if(ac!=3){put("usage: head FILE\n");return 2;}long fd=sc3(SYS_open,(long)av[2],0,0);if(fd<0){err("head",av[2]);return 1;}char b[512];long lines=0;for(;;){long n=sc3(SYS_read,fd,(long)b,sizeof(b));if(n<=0)break;for(long i=0;i<n;i++){putn(&b[i],1);if(b[i]=='\n'&&++lines>=10){sc1(SYS_close,fd);return 0;}}}sc1(SYS_close,fd);return 0;}
static int cmd_wc(int ac,char**av){if(ac<2||ac>3){put("usage: wc [FILE]\n");return 2;}long fd=0;const char*label="-";if(ac==3){fd=sc3(SYS_open,(long)av[2],0,0);label=av[2];if(fd<0){err("wc",av[2]);return 1;}}char b[512];long lines=0,words=0,bytes=0,inword=0;for(;;){long n=sc3(SYS_read,fd,(long)b,sizeof(b));if(n<0){if(ac==3)sc1(SYS_close,fd);return 1;}if(n==0)break;bytes+=n;for(long i=0;i<n;i++){if(b[i]=='\n')lines++;if(b[i]==' '||b[i]=='\t'||b[i]=='\n'||b[i]=='\r')inword=0;else if(!inword){words++;inword=1;}}}if(ac==3)sc1(SYS_close,fd);num(lines);put(" ");num(words);put(" ");num(bytes);put(" ");put(label);put("\n");return 0;}
static int cmd_rmdir(int ac,char**av){int rc=0;if(ac<3)return 2;for(int i=2;i<ac;i++)if(sc1(SYS_rmdir,(long)av[i])<0){err("rmdir",av[i]);rc=1;}return rc;}
static int cp_one(const char *src,const char *dst){
	if(eq(src,dst)){put("yabrusbox: cp: source and destination are the same\n");return 1;}
	struct kstat st; if(sc2(SYS_stat,(long)src,(long)&st)<0){err("cp",src);return 1;}
	if((st.mode&S_IFMT)==S_IFDIR){put("yabrusbox: cp: omitting directory: ");put(src);put("\n");return 1;}
	long in=sc3(SYS_open,(long)src,0,0);if(in<0){err("cp",src);return 1;}
	long out=sc3(SYS_open,(long)dst,O_WRONLY|O_CREAT|O_TRUNC,0666);if(out<0){sc1(SYS_close,in);err("cp",dst);return 1;}
	char b[1024];int rc=0;for(;;){long r=sc3(SYS_read,in,(long)b,sizeof(b));if(r<0){rc=1;break;}if(r==0)break;long done=0;while(done<r){long w=sc3(SYS_write,out,(long)b+done,(unsigned long)(r-done));if(w<=0){rc=1;break;}done+=w;}if(rc)break;}
	sc1(SYS_close,in);sc1(SYS_close,out);if(rc)err("cp",dst);return rc;
}
static int cmd_cp(int ac,char**av){
	if(ac<4){put("usage: cp SOURCE... DEST\n");return 2;}
	const char *dest=av[ac-1];int multi=(ac>4);if(multi&&!stat_is_dir_b(dest)){put("yabrusbox: cp: destination is not a directory\n");return 1;}
	int rc=0;char target[256];
	for(int i=2;i<ac-1;i++){
		const char *out=dest;
		if(stat_is_dir_b(dest)){if(make_child_path_b(dest,base_name_b(av[i]),target,sizeof(target))<0){err("cp",dest);rc=1;continue;}out=target;}
		if(cp_one(av[i],out))rc=1;
	}
	return rc;
}
static int cmd_tail(int ac,char**av){
	if(ac!=3){put("usage: tail FILE\n");return 2;}
	long fd=sc3(SYS_open,(long)av[2],0,0);
	if(fd<0){err("tail",av[2]);return 1;}
	char b[16384];
	long total=0;
	for(;;){
		long room=(long)sizeof(b)-total;
		if(room<=0) break;
		long r=sc3(SYS_read,fd,(long)b+total,(unsigned long)room);
		if(r<0){sc1(SYS_close,fd);err("tail",av[2]);return 1;}
		if(r==0) break;
		total+=r;
	}
	sc1(SYS_close,fd);
	long lines=0;
	for(long i=0;i<total;i++) if(b[i]=='\n') lines++;
	long skip=lines>10?lines-10:0;
	long pos=0, seen=0;
	while(pos<total && seen<skip){if(b[pos++]=='\n')seen++;}
	putn(b+pos,total-pos);
	return 0;
}
static int cmd_grep(int ac,char**av){if(ac<3||ac>4){put("usage: grep PATTERN [FILE]\n");return 2;}long fd=0;if(ac==4){fd=sc3(SYS_open,(long)av[3],0,0);if(fd<0){err("grep",av[3]);return 2;}}char b[4096];long total=0;for(;;){long r=sc3(SYS_read,fd,(long)b+total,sizeof(b)-1-total);if(r<0){if(ac==4)sc1(SYS_close,fd);return 1;}if(r==0)break;total+=r;if(total==(long)sizeof(b)-1)break;}if(ac==4)sc1(SYS_close,fd);long plen=slen(av[2]),st=0;int found=0;for(long i=0;i<=total;i++){if(i==total||b[i]=='\n'){long len=i-st,match=0;for(long j=0;j+plen<=len;j++){long k=0;while(k<plen&&b[st+j+k]==av[2][k])k++;if(k==plen){match=1;break;}}if(match){putn(b+st,len);put("\n");found=1;}st=i+1;}}return found?0:1;}
static int cmd_ps(void){put("PID STATE CMD\n1 RUN   ");long fd=sc3(SYS_open,(long)"/proc/1/cmdline",0,0);if(fd>=0){char b[256];long n=sc3(SYS_read,fd,(long)b,sizeof(b)-1);if(n>0)putn(b,n);sc1(SYS_close,fd);}put("\n");return 0;}
static long to_num(const char*s){long v=0;int sign=1;if(*s=='-'){sign=-1;s++;}if(!*s)return-1;while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}return sign*v;}
static int cmd_kill(int ac,char**av){if(ac!=3){put("usage: kill PID\n");return 2;}long pid=to_num(av[2]);if(pid<1||sc2(SYS_kill,pid,15)<0){err("kill",av[2]);return 1;}return 0;}
static int cmd_whoami(void){put("root\n");return 0;}
static int cmd_id(void){put("uid=0(root) gid=0(root) groups=0(root)\n");return 0;}
static int cmd_uname(void){uint8_t u[390];if(sc1(SYS_uname,(long)u)<0)return 1;put((char*)u);put(" ");put((char*)u+65);put(" ");put((char*)u+130);put(" ");put((char*)u+195);put("\n");return 0;}
static int cmd_uptime(void){char b[128];long fd=sc3(SYS_open,(long)"/proc/uptime",0,0);if(fd<0){err("uptime","/proc/uptime");return 1;}long n=sc3(SYS_read,fd,(long)b,sizeof(b)-1);sc1(SYS_close,fd);if(n<0){err("uptime","/proc/uptime");return 1;}if(n>0)putn(b,n);return 0;}
static int cmd_env(void){
	if(!g_envp) return 0;
	for(int i=0;g_envp[i];i++){put(g_envp[i]);put("\n");}
	return 0;
}
static int cmd_true(void){return 0;}
static int cmd_false(void){return 1;}
static int cmd_sh(void){
	char *a[]={(char*)"/YSH.ELF",0};
	sc3(SYS_execve,(long)"/YSH.ELF",(long)a,(long)g_envp);
	return 127;
}

static const char *base_name(const char *p){
	const char *b=p;
	if(!p)return "";
	for(long i=0;p[i];i++) if(p[i]=='/') b=p+i+1;
	return b;
}

static int is_applet_name(const char *c){
	return eq(c,"ls")||eq(c,"cat")||eq(c,"pwd")||eq(c,"echo")||eq(c,"env")||
		   eq(c,"mkdir")||eq(c,"rmdir")||eq(c,"cp")||eq(c,"rm")||eq(c,"mv")||
		   eq(c,"touch")||eq(c,"head")||eq(c,"tail")||eq(c,"wc")||eq(c,"grep")||
		   eq(c,"uname")||eq(c,"uptime")||eq(c,"ps")||eq(c,"kill")||eq(c,"whoami")||
		   eq(c,"id")||eq(c,"true")||eq(c,"false")||eq(c,"sh");
}

static int dispatch(int ac,char**av){
	const char *applet=0;
	int shift=0;
	if(ac>0){
		const char *b=base_name(av[0]);
		if(is_applet_name(b) && !eq(b,"YABRUSBOX.ELF")){ applet=b; shift=0; }
	}
	if(!applet && ac>=2 && (eq(av[1],"--help")||eq(av[1],"--list"))){
		put("YabrusBox YabroOS-32 v0.0.2-alpha\n");
		put("applets: ls cat pwd echo env mkdir rmdir cp rm mv touch head tail wc grep uname uptime ps kill whoami id true false sh\n");
		return 0;
	}
	if(!applet && ac>=2 && (eq(av[0],"yabrusbox")||eq(av[0],"busybox")||eq(base_name(av[0]),"YABRUSBOX.ELF"))){
		applet=av[1]; shift=1;
	}
	if(!applet){
		put("YabrusBox YabroOS-32 v0.0.2-alpha\n");
		put("applets: ls cat pwd echo env mkdir rmdir cp rm mv touch head tail wc grep uname uptime ps kill whoami id true false sh\n");
		return 0;
	}

	char *args[16];
	int outc=0;
	args[outc++]=(char*)"yabrusbox";
	args[outc++]=(char*)applet;
	int first_user=1+shift;
	if(shift==0 && ac>0 && is_applet_name(base_name(av[0]))) first_user=1;
	for(int i=first_user;i<ac && outc<15;i++) args[outc++]=av[i];
	args[outc]=0;
	int fake_ac=outc;
	char **a=args;
	if(eq(applet,"echo"))return cmd_echo(fake_ac,a);
	if(eq(applet,"cat"))return cmd_cat(fake_ac,a);
	if(eq(applet,"ls"))return cmd_ls(fake_ac,a);
	if(eq(applet,"pwd"))return cmd_pwd();
	if(eq(applet,"rmdir"))return cmd_rmdir(fake_ac,a);
	if(eq(applet,"cp"))return cmd_cp(fake_ac,a);
	if(eq(applet,"tail"))return cmd_tail(fake_ac,a);
	if(eq(applet,"grep"))return cmd_grep(fake_ac,a);
	if(eq(applet,"ps"))return cmd_ps();
	if(eq(applet,"kill"))return cmd_kill(fake_ac,a);
	if(eq(applet,"whoami"))return cmd_whoami();
	if(eq(applet,"id"))return cmd_id();
	if(eq(applet,"mkdir"))return cmd_mkdir(fake_ac,a);
	if(eq(applet,"rm"))return cmd_rm(fake_ac,a);
	if(eq(applet,"mv"))return cmd_mv(fake_ac,a);
	if(eq(applet,"touch"))return cmd_touch(fake_ac,a);
	if(eq(applet,"head"))return cmd_head(fake_ac,a);
	if(eq(applet,"wc"))return cmd_wc(fake_ac,a);
	if(eq(applet,"uname"))return cmd_uname();
	if(eq(applet,"uptime"))return cmd_uptime();
	if(eq(applet,"env"))return cmd_env();
	if(eq(applet,"true"))return cmd_true();
	if(eq(applet,"false"))return cmd_false();
	if(eq(applet,"sh"))return cmd_sh();
	put("yabrusbox: applet not found: ");put(applet);put("\n");return 127;
}

void bb_main(uint64_t *sp){
	long argc=(long)sp[0];
	char**argv=(char**)&sp[1];
	g_envp=argv+argc+1;
	g_envc=0;
	while(g_envp[g_envc] && g_envc<128) g_envc++;
	int rc=dispatch((int)argc,argv);
	exitc(rc);
}
__asm__(
".global _start\n.type _start,@function\n_start:\n"
"mov %rsp,%rdi\n"
"and $-16,%rsp\n"
"call bb_main\n"
"ud2\n"
".size _start,.-_start\n");
