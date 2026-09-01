// Virtual filesystem implementation - (c) Ayano4ka1338, 2026

#include "vfs.h"
#define VFS_MAX_NODES 16384
#define VFS_MAX_MOUNTS 8
#define VFS_MAX_FS 8
#define VFS_PATH_MAX 260
#define VFS_FILE_CAP 4096
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000
static struct vfs_node nodes[VFS_MAX_NODES];
static uint64_t vfs_boot_rtc_sec=0; static uint8_t node_data[VFS_MAX_NODES][VFS_FILE_CAP]; static struct vfs_mount mounts[VFS_MAX_MOUNTS]; static struct vfs_fs_type *filesystems[VFS_MAX_FS]; static int node_used,mount_used,fs_used; static struct vfs_node *root_node; static uint64_t next_ino=1;
static int eq(const char*a,const char*b){while(*a&&*b&&*a==*b){++a;++b;}return *a==*b;}
static int system_dir_fix(char *s){
	if(!s) return 0;
	if(eq(s,"LIB")) { s[0]='l';s[1]='i';s[2]='b';s[3]=0; return 1; }
	if(eq(s,"USR")) { s[0]='u';s[1]='s';s[2]='r';s[3]=0; return 1; }
	if(eq(s,"DEV")) { s[0]='d';s[1]='e';s[2]='v';s[3]=0; return 1; }
	if(eq(s,"ETC")) { s[0]='e';s[1]='t';s[2]='c';s[3]=0; return 1; }
	if(eq(s,"BIN")) { s[0]='b';s[1]='i';s[2]='n';s[3]=0; return 1; }
	if(eq(s,"SBIN")) { s[0]='s';s[1]='b';s[2]='i';s[3]='n';s[4]=0; return 1; }
	if(eq(s,"VAR")) { s[0]='v';s[1]='a';s[2]='r';s[3]=0; return 1; }
	if(eq(s,"HOME")) { s[0]='h';s[1]='o';s[2]='m';s[3]='e';s[4]=0; return 1; }
	if(eq(s,"TMP")) { s[0]='t';s[1]='m';s[2]='p';s[3]=0; return 1; }
	return 0;
}
static size_t slen(const char*s){size_t n=0;while(s&&s[n])++n;return n;}
static void cp_path(char*d,const char*s){
	if(!d)return;
	size_t i=0;
	if(s){while(s[i] && i+1<VFS_PATH_MAX){d[i]=s[i];i++;}}
	d[i]=0;
}
extern size_t bootfs_read_root_file(const char *path, uint64_t offset, uint8_t *dst, size_t len);

static void cp(char*d,const char*s){size_t i=0;while(s[i]&&i<VFS_NAME_MAX){d[i]=s[i];i++;}d[i]=0;}
static struct vfs_node* new_node(const char*n,uint16_t t,uint32_t m){if(node_used>=VFS_MAX_NODES)return 0;struct vfs_node*x=&nodes[node_used++];for(size_t i=0;i<sizeof(*x);i++)((uint8_t*)x)[i]=0;cp(x->name,n);x->ino=next_ino++;x->type=t;x->mode=m; x->data=node_data[node_used-1]; x->size=0; x->capacity=VFS_FILE_CAP; return x;}
static void add(struct vfs_node*p,struct vfs_node*x){x->parent=p;x->next=p->children;p->children=x;}
static struct vfs_node* mkd(struct vfs_node*p,const char*n){struct vfs_node*x=new_node(n,VFS_NODE_DIR,S_IFDIR|0755);if(x)add(p,x);return x;}
static struct vfs_node* mkf(struct vfs_node*p,const char*n,uint16_t t){struct vfs_node*x=new_node(n,t,S_IFREG|0444);if(x)add(p,x);return x;}
static struct vfs_node* mkl(struct vfs_node*p,const char*n,const char*t){struct vfs_node*x=new_node(n,VFS_NODE_SYMLINK,S_IFLNK|0777);if(x){x->link_target=t;add(p,x);}return x;}
struct vfs_node*vfs_lookup_child(struct vfs_node*d,const char*n){if(!d||d->type!=VFS_NODE_DIR)return 0;for(struct vfs_node*x=d->children;x;x=x->next)if(eq(x->name,n))return x;return 0;}
struct vfs_node*vfs_child_at(struct vfs_node*d,uint64_t i){if(!d||d->type!=VFS_NODE_DIR)return 0;struct vfs_node*x=d->children;while(x&&i--)x=x->next;return x;}
uint64_t vfs_child_count(struct vfs_node*d){uint64_t n=0;if(!d||d->type!=VFS_NODE_DIR)return 0;for(struct vfs_node*x=d->children;x;x=x->next)n++;return n;}
static struct vfs_node* mr(struct vfs_node*x){return x&&x->mounted&&x->mounted->root?x->mounted->root:x;}
struct vfs_node*vfs_lookup(const char*p){if(!p||p[0]!='/')return 0;if(p[1]==0)return root_node;struct vfs_node*cur=root_node;size_t i=1;while(p[i]){while(p[i]=='/')i++;if(!p[i])break;char part[VFS_NAME_MAX+1];size_t j=0;while(p[i]&&p[i]!='/'){if(j<VFS_NAME_MAX)part[j++]=p[i];i++;}part[j]=0;if(eq(part,"."))continue;if(eq(part,"..")){

			if(cur->parent && cur->parent->mounted && cur->parent->mounted->root==cur){
				cur = cur->parent->parent ? cur->parent->parent : cur->parent;
			} else if(cur->parent) {
				cur=cur->parent;
			}
			continue;
		}cur=mr(cur);cur=vfs_lookup_child(cur,part);if(!cur)return 0;cur=mr(cur);}return cur;}
static struct vfs_node* mountpoint(const char*t){if(!t||t[0]!='/')return 0;if(eq(t,"/"))return root_node;char p[VFS_PATH_MAX];size_t n=slen(t);if(n>=sizeof(p))return 0;for(size_t i=0;i<=n;i++)p[i]=t[i];return vfs_lookup(p);}
int vfs_register_filesystem(struct vfs_fs_type*f){if(!f||!f->name||!f->mount||fs_used>=VFS_MAX_FS)return-1;for(int i=0;i<fs_used;i++)if(eq(filesystems[i]->name,f->name))return-2;filesystems[fs_used++]=f;return 0;}
int vfs_mount(const char*name,const char*target){if(!name||!target||mount_used>=VFS_MAX_MOUNTS)return-1;struct vfs_fs_type*f=0;for(int i=0;i<fs_used;i++)if(eq(filesystems[i]->name,name)){f=filesystems[i];break;}if(!f)return-2;struct vfs_node*mp=mountpoint(target);if(!mp||mp->type!=VFS_NODE_DIR)return-3;if(mp->mounted)return-4;struct vfs_mount*m=&mounts[mount_used++];for(size_t i=0;i<sizeof(*m);i++)((uint8_t*)m)[i]=0;m->fs_name=f->name;m->mountpoint=mp;if(f->mount(f,mp,m)){--mount_used;return-5;}mp->mounted=m;return 0;}
static int proc_mount(struct vfs_fs_type*f,struct vfs_node*mp,struct vfs_mount*m){(void)f;struct vfs_node*r=new_node("proc",VFS_NODE_DIR,S_IFDIR|0755);if(!r)return-1;r->parent=mp;m->root=r;struct vfs_node*self=mkd(r,"self"),*pid=mkd(r,"1");if(!self||!pid)return-1;struct vfs_node*sfd=mkd(self,"fd"),*pfd=mkd(pid,"fd");if(!sfd||!pfd)return-1;mkf(r,"cpuinfo",VFS_NODE_PROC_FILE);mkf(r,"meminfo",VFS_NODE_PROC_FILE);mkf(r,"uptime",VFS_NODE_PROC_FILE);mkf(r,"version",VFS_NODE_PROC_FILE);const char*n[]={"status","cmdline","maps"};for(int i=0;i<3;i++){mkf(self,n[i],VFS_NODE_PROC_FILE);mkf(pid,n[i],VFS_NODE_PROC_FILE);}mkl(self,"exe","/HELLO.ELF");mkl(pid,"exe","/HELLO.ELF");for(int i=0;i<3;i++){char q[2]={(char)('0'+i),0};mkl(sfd,q,i==0?"/dev/tty":"/dev/console");mkl(pfd,q,i==0?"/dev/tty":"/dev/console");}return 0;}
static int dev_mount(struct vfs_fs_type*f,struct vfs_node*mp,struct vfs_mount*m){(void)f;struct vfs_node*r=new_node("dev",VFS_NODE_DIR,S_IFDIR|0755);if(!r)return-1;r->parent=mp;m->root=r;mkf(r,"null",VFS_NODE_DEV_NULL);mkf(r,"zero",VFS_NODE_DEV_ZERO);mkf(r,"console",VFS_NODE_DEV_CONSOLE);mkf(r,"tty",VFS_NODE_DEV_TTY);mkf(r,"random",VFS_NODE_DEV_RANDOM);mkf(r,"urandom",VFS_NODE_DEV_URANDOM);mkf(r,"fb0",VFS_NODE_DEV_FB); struct vfs_node *input=mkd(r,"input"); if(!input)return-1; mkf(input,"event0",VFS_NODE_DEV_INPUT); mkf(input,"event1",VFS_NODE_DEV_INPUT_MOUSE);mkl(r,"stdin","/dev/tty");mkl(r,"stdout","/dev/console");mkl(r,"stderr","/dev/console");return 0;}
static struct vfs_fs_type procfs={"procfs",proc_mount},devfs={"devfs",dev_mount};

static void canonicalize_system_path(const char *src, char *dst, size_t cap) {
	if (!dst || cap == 0) return;
	size_t di = 0, i = 0;
	if (!src || src[0] != '/') { dst[0] = 0; return; }
	if (di + 1 < cap) dst[di++] = '/';
	i = 1;
	while (src[i] && di + 1 < cap) {
		while (src[i] == '/') i++;
		if (!src[i]) break;
		size_t start = i;
		while (src[i] && src[i] != '/') i++;
		size_t n = i - start;
		char part[VFS_NAME_MAX + 1];
		if (n > VFS_NAME_MAX) n = VFS_NAME_MAX;
		for (size_t j = 0; j < n; ++j) part[j] = src[start + j];
		part[n] = 0;
		system_dir_fix(part);
		if (di > 1 && dst[di-1] != '/' && di + 1 < cap) dst[di++] = '/';
		for (size_t j = 0; j < n && di + 1 < cap; ++j) dst[di++] = part[j];
	}
	dst[di] = 0;
}

static struct vfs_node *vfs_parent_for_create(const char *path, char *name) {
	if (!path || path[0] != '/' || !name) return 0;
	size_t n=slen(path); if(n<2 || n>=VFS_PATH_MAX) return 0;
	char parent[VFS_PATH_MAX]; for(size_t i=0;i<=n;i++) parent[i]=path[i];
	while(n>1 && parent[n-1]=='/') parent[--n]=0;
	size_t slash=n; while(slash>0 && parent[slash-1]!='/') slash--;
	if(slash==0 || slash>=n) return 0;
	size_t nl=n-slash; if(nl>VFS_NAME_MAX)return 0;
	for(size_t i=0;i<nl;i++) name[i]=parent[slash+i];
	name[nl]=0;
	if(slash==1) parent[1]=0; else parent[slash-1]=0;
	return vfs_lookup(parent);
}

struct vfs_node *vfs_create_file(const char *path, int truncate) {
	struct vfs_node *n=vfs_lookup(path);
	if(n) {
		if(n->type==VFS_NODE_FILE) {
			if(truncate) n->size=0;
			return n;
		}
		return 0;
	}
	char name[VFS_NAME_MAX+1];
	struct vfs_node *p=vfs_parent_for_create(path,name);
	if(!p || p->type!=VFS_NODE_DIR) return 0;
	n=new_node(name,VFS_NODE_FILE,S_IFREG|0666);
	if(!n)return 0;
	add(p,n);
	return n;
}

struct vfs_node *vfs_mkdir(const char *path, uint32_t mode) {
	if(!path || path[0]!='/' || path[1]==0) return 0;
	char canon[VFS_PATH_MAX];
	canonicalize_system_path(path, canon, sizeof(canon));
	if(!canon[0]) return 0;
	if(vfs_lookup(canon)) return 0;
	char name[VFS_NAME_MAX+1];
	struct vfs_node *p=vfs_parent_for_create(canon,name);
	if(!p || p->type!=VFS_NODE_DIR) return 0;
	struct vfs_node *n=new_node(name,VFS_NODE_DIR,S_IFDIR|(mode&0777));
	if(!n)return 0;
	add(p,n);
	return n;
}

int vfs_unlink(const char *path) {
	struct vfs_node *n=vfs_lookup(path);
	if(!n || n==root_node || n->type==VFS_NODE_DIR || n->mounted) return -1;
	struct vfs_node *p=n->parent;
	if(!p || p->type!=VFS_NODE_DIR) return -1;
	struct vfs_node **pp=&p->children;
	while(*pp && *pp!=n) pp=&(*pp)->next;
	if(*pp!=n) return -1;
	*pp=n->next; n->parent=0; n->next=0; n->size=0;
	return 0;
}

int vfs_rmdir(const char *path) {
	struct vfs_node *n=vfs_lookup(path);
	if(!n || n==root_node || n->type!=VFS_NODE_DIR || n->mounted) return -1;
	if(n->children) return -2;
	struct vfs_node *p=n->parent;
	if(!p || p->type!=VFS_NODE_DIR) return -1;
	struct vfs_node **pp=&p->children;
	while(*pp && *pp!=n) pp=&(*pp)->next;
	if(*pp!=n) return -1;
	*pp=n->next; n->parent=0; n->next=0;
	return 0;
}

static int vfs_is_ancestor(const struct vfs_node *ancestor,const struct vfs_node *node) {
	for(const struct vfs_node *p=node;p;p=p->parent) if(p==ancestor) return 1;
	return 0;
}

int vfs_rename(const char *oldpath, const char *newpath) {
	struct vfs_node *n=vfs_lookup(oldpath);
	if(!n || n==root_node || n->mounted) return -1;
	char name[VFS_NAME_MAX+1];
	struct vfs_node *p=vfs_parent_for_create(newpath,name);
	if(!p || p->type!=VFS_NODE_DIR || !name[0]) return -1;
	if(n->type==VFS_NODE_DIR && vfs_is_ancestor(n,p)) return -1;
	struct vfs_node *existing=vfs_lookup(newpath);
	if(existing==n) return 0;
	if(existing) {
		if(existing->mounted || existing->type==VFS_NODE_DIR) return -1;
		struct vfs_node *ep=existing->parent;
		struct vfs_node **epp=ep?&ep->children:0;
		while(epp && *epp && *epp!=existing) epp=&(*epp)->next;
		if(!epp || *epp!=existing) return -1;
		*epp=existing->next; existing->parent=0; existing->next=0;
	}
	struct vfs_node *op=n->parent;
	if(op) {
		struct vfs_node **pp=&op->children;
		while(*pp && *pp!=n) pp=&(*pp)->next;
		if(*pp==n) *pp=n->next;
	}
	cp(n->name,name); n->parent=p; n->next=p->children; p->children=n;
	return 0;
}

int vfs_file_truncate(struct vfs_node *n, uint64_t len) {
	if(!vfs_file_is_regular(n) || len>n->capacity) return -1;
	if(len>n->size) for(uint64_t i=n->size;i<len;i++) n->data[i]=0;
	n->size=len;
	return 0;
}

int vfs_write_kernel_file(struct vfs_node *n, const uint8_t *data, uint64_t len) {
	if(!vfs_file_is_regular(n) || (!data && len) || len>n->capacity) return -1;
	for(uint64_t i=0;i<len;i++) n->data[i]=data[i];
	n->size=len;
	return 0;
}

int vfs_file_is_regular(const struct vfs_node *n){ return n && n->type==VFS_NODE_FILE; }
uint8_t *vfs_file_data(struct vfs_node *n){ return n && vfs_file_is_regular(n) ? n->data : 0; }
int vfs_is_boot_file(const struct vfs_node *n);
uint64_t vfs_file_size(const struct vfs_node *n){ return n && (vfs_file_is_regular(n) || vfs_is_boot_file(n)) ? n->size : 0; }
uint64_t vfs_file_write(struct vfs_node *n,uint64_t off,const uint8_t *src,uint64_t len){
	if(!vfs_file_is_regular(n) || !src || off>=n->capacity) return 0;
	uint64_t room=n->capacity-off, w=len<room?len:room;
	for(uint64_t i=0;i<w;i++) n->data[off+i]=src[i];
	if(off+w>n->size)n->size=off+w;
	return w;
}
int vfs_import_boot_file(const char *path, uint64_t size) {
	if (!path || path[0] == 0 || size == 0) return -1;



	const char *p = path;
	while (*p == '/') p++;
	if (*p == 0) return -1;

	char part[VFS_NAME_MAX + 1];
	struct vfs_node *parent = root_node;

	while (*p) {
		size_t nlen = 0;
		while (*p && *p != '/') {
			if (nlen < VFS_NAME_MAX) part[nlen++] = *p;
			p++;
		}
		while (*p == '/') p++;
		if (nlen == 0) continue;
		part[nlen] = 0;

		system_dir_fix(part);

		int last = (*p == 0);
		struct vfs_node *child = vfs_lookup_child(parent, part);

		if (!last) {
			if (!child) {
				child = mkd(parent, part);
				if (!child) return -1;
			}
			if (child->type != VFS_NODE_DIR) return -1;
			parent = child;
			continue;
		}

		if (child) {
			if (child->type == VFS_NODE_BOOT_FILE) {
				child->size = size;
				child->capacity = size;
				cp_path(child->boot_path, path);
				return 0;
			}
			return -1;
		}

		struct vfs_node *file = new_node(part, VFS_NODE_BOOT_FILE, S_IFREG|0444);
		if (!file) return -1;
		file->data = 0;
		file->size = size;
		file->capacity = size;
		cp_path(file->boot_path, path);
		add(parent, file);
		return 0;
	}
	return -1;
}

int vfs_is_boot_file(const struct vfs_node *n){ return n && n->type==VFS_NODE_BOOT_FILE; }
const char *vfs_boot_path(const struct vfs_node *n){
	return (n && n->type==VFS_NODE_BOOT_FILE && n->boot_path[0]) ? n->boot_path : (n ? n->name : 0);
}
const char *vfs_node_name(const struct vfs_node *n){ return n ? n->name : 0; }

void vfs_init(void){
	for(size_t i=0;i<sizeof(nodes);i++)((uint8_t*)nodes)[i]=0;
	for(size_t i=0;i<sizeof(mounts);i++)((uint8_t*)mounts)[i]=0;
	node_used=mount_used=fs_used=0; next_ino=1;
	root_node=new_node("",VFS_NODE_DIR,S_IFDIR|0755);
	mkd(root_node,"proc"); mkd(root_node,"dev"); mkd(root_node,"tmp"); mkd(root_node,"etc"); mkd(root_node,"bin");

	struct vfs_node *n;
	static const uint8_t os_release[]="NAME=YabroOS-32\nVERSION=0.0.2-alpha\nID=yabroos-32\nPRETTY_NAME=\"YabroOS-32 v0.0.2-alpha\"\n";
	static const uint8_t hostname[]="yabroos-32\n";
	static const uint8_t motd[]="Welcome to YabroOS-32 v0.0.2.\n";
	n=vfs_create_file("/etc/os-release",0); if(n) vfs_write_kernel_file(n,os_release,sizeof(os_release)-1);
	n=vfs_create_file("/etc/hostname",0); if(n) vfs_write_kernel_file(n,hostname,sizeof(hostname)-1);
	n=vfs_create_file("/etc/motd",0); if(n) vfs_write_kernel_file(n,motd,sizeof(motd)-1);

	vfs_register_filesystem(&procfs); vfs_register_filesystem(&devfs);
	(void)vfs_mount("procfs","/proc"); (void)vfs_mount("devfs","/dev");
}

int vfs_is_dir(const struct vfs_node*n){return n&&n->type==VFS_NODE_DIR;} uint16_t vfs_type(const struct vfs_node*n){return n?n->type:0;} uint32_t vfs_mode(const struct vfs_node*n){return n?n->mode:0;} uint64_t vfs_ino(const struct vfs_node*n){return n?n->ino:0;} const char*vfs_link_target(const struct vfs_node*n){return n?n->link_target:0;} struct vfs_node*vfs_root(void){return root_node;} int vfs_mounted(const char*t){if(!t||!root_node)return 0;if(eq(t,"/"))return 1;struct vfs_node*n=root_node;size_t i=1;while(t[i]){while(t[i]=='/')i++;if(!t[i])break;char part[VFS_NAME_MAX+1];size_t j=0;while(t[i]&&t[i]!='/'){if(j<VFS_NAME_MAX)part[j++]=t[i];i++;}part[j]=0;n=vfs_lookup_child(n,part);if(!n)return 0;}return n->mounted!=0;}
static void vfs_out_ch(char *out,size_t cap,size_t *pos,char c){if(*pos+1<cap){out[*pos]=c;(*pos)++;out[*pos]=0;}}
static void vfs_out_str(char *out,size_t cap,size_t *pos,const char *s){if(!s)return;while(*s)vfs_out_ch(out,cap,pos,*s++);}
static int vfs_name_cmp(const struct vfs_node *a,const struct vfs_node *b){
	const unsigned char *x=(const unsigned char*)a->name,*y=(const unsigned char*)b->name;
	while(*x&&*y){
		unsigned char cx=*x,cy=*y;
		if(cx>='A'&&cx<='Z')cx=(unsigned char)(cx-'A'+'a');
		if(cy>='A'&&cy<='Z')cy=(unsigned char)(cy-'A'+'a');
		if(cx!=cy)return cx<cy?-1:1;
		++x;++y;
	}
	if(!*x&&!*y)return 0;
	return *x?1:-1;
}

size_t vfs_list_dir(const char *path,char *out,size_t cap){
	if(!out||cap==0)return 0;out[0]=0;

	char canon[VFS_PATH_MAX]; size_t n=0;
	const char *src=path?path:"/";
	if(src[0]!='/')return 0;
	while(src[n] && n+1<VFS_PATH_MAX){canon[n]=src[n];n++;}
	if(src[n])return 0;
	while(n>1 && canon[n-1]=='/')n--;
	canon[n]=0;
	struct vfs_node*d=vfs_lookup(canon);
	if(!d||d->type!=VFS_NODE_DIR)return 0;

	struct vfs_node *items[VFS_MAX_NODES];
	size_t count=0;
	for(struct vfs_node*x=d->children;x && count<VFS_MAX_NODES;x=x->next){
		if(x->name[0])items[count++]=x;
	}
	for(size_t i=1;i<count;i++){
		struct vfs_node *v=items[i]; size_t j=i;
		while(j>0 && vfs_name_cmp(items[j-1],v)>0){items[j]=items[j-1];--j;}
		items[j]=v;
	}
	size_t pos=0;
	for(size_t i=0;i<count;i++){
		struct vfs_node*x=items[i];
		vfs_out_str(out,cap,&pos,x->name);
		if(x->type==VFS_NODE_DIR)vfs_out_ch(out,cap,&pos,'/');
		else if(x->type==VFS_NODE_SYMLINK)vfs_out_ch(out,cap,&pos,'@');
		vfs_out_ch(out,cap,&pos,'\n');
	}
	return pos;
}
void vfs_debug_dump(void){  }

size_t vfs_read_file(const char *path,char *out,size_t cap){
	if(!path||!out||cap==0)return 0;
	struct vfs_node*n=vfs_lookup(path);
	if(!n || n->type==VFS_NODE_DIR || n->type==VFS_NODE_SYMLINK)return 0;
	if(vfs_file_is_regular(n)) {
		size_t i=0; while(i<n->size && i+1<cap){out[i]=n->data[i];i++;} out[i]=0; return i;
	}
	if(vfs_is_boot_file(n)) {
		if(!n->boot_path[0]) return 0;

		return bootfs_read_root_file(n->boot_path, 0, (uint8_t*)out, cap);
	}
	const char *s=0;
	if(eq(path,"/proc/version")) s="YabroOS-32 v0.0.2-alpha (procfs+musl-abi)\n";
	else if(eq(path,"/proc/uptime")) {
		static char uptime_buf[64];
		uint64_t ns=kernel_monotonic_ns(), sec=ns/1000000000ULL, frac=(ns%1000000000ULL)/10000000ULL;
		char tmp[24]; int ti=0,pos=0; uint64_t v=sec;
		if(v==0) tmp[ti++]='0'; else while(v&&ti<(int)sizeof(tmp)){tmp[ti++]=(char)('0'+v%10);v/=10;}
		while(ti) uptime_buf[pos++]=tmp[--ti];
		uptime_buf[pos++]='.';uptime_buf[pos++]=(char)('0'+frac/10);uptime_buf[pos++]=(char)('0'+frac%10);
		uptime_buf[pos++]=' ';uptime_buf[pos++]='0';uptime_buf[pos++]='.';uptime_buf[pos++]='0';uptime_buf[pos++]='0';uptime_buf[pos++]='\n';uptime_buf[pos]=0;s=uptime_buf;
	}
	else if(eq(path,"/proc/cpuinfo")) s="processor\t: 0\nmodel name\t: YabroOS-32 virtual CPU\n";
	else if(eq(path,"/proc/meminfo")) s="MemTotal:       65536 kB\nMemFree:        32768 kB\n";
	else if(eq(path,"/proc/self/status") || eq(path,"/proc/1/status")) s="Name:\tinit\nState:\tR (running)\nPid:\t1\n";
	else if(eq(path,"/proc/self/cmdline") || eq(path,"/proc/1/cmdline")) s="/HELLO.ELF\n";
	else if(eq(path,"/proc/self/maps") || eq(path,"/proc/1/maps")) s="00400000-00401000 r-xp /HELLO.ELF\n";
	else if(n->type==VFS_NODE_DEV_NULL) return 0;
	else if(n->type==VFS_NODE_DEV_ZERO) s="[dev/zero]\n";
	else if(n->type==VFS_NODE_DEV_CONSOLE || n->type==VFS_NODE_DEV_TTY) s="[terminal device]\n";
	else if(n->type==VFS_NODE_DEV_RANDOM || n->type==VFS_NODE_DEV_URANDOM) s="[random device]\n";
	if(!s)return 0;
	size_t i=0;while(s[i] && i+1<cap){out[i]=s[i];i++;}out[i]=0;return i;
}
