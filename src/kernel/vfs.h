// Virtual filesystem interface - (c) Ayano4ka1338, 2026

#ifndef YABROOS_VFS_H
#define YABROOS_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX 31
#define VFS_PATH_MAX 260

enum vfs_node_type {
	VFS_NODE_DIR          = 1,
	VFS_NODE_FILE         = 2,
	VFS_NODE_PROC_FILE    = 3,
	VFS_NODE_SYMLINK      = 4,
	VFS_NODE_DEV_NULL     = 10,
	VFS_NODE_DEV_ZERO     = 11,
	VFS_NODE_DEV_CONSOLE  = 12,
	VFS_NODE_DEV_TTY      = 13,
	VFS_NODE_DEV_RANDOM   = 14,
	VFS_NODE_DEV_URANDOM  = 15,
	VFS_NODE_DEV_FB        = 18,
	VFS_NODE_SYS_FILE     = 16,
	VFS_NODE_BOOT_FILE    = 17,
	VFS_NODE_DEV_INPUT   = 19,
	VFS_NODE_DEV_INPUT_MOUSE = 20
};

struct vfs_node;
struct vfs_fs_type;
struct vfs_mount;

typedef int (*vfs_mount_fn)(struct vfs_fs_type*, struct vfs_node*, struct vfs_mount*);

struct vfs_fs_type {
	const char   *name;
	vfs_mount_fn  mount;
};

struct vfs_node {
	char           name[VFS_NAME_MAX + 1];
	uint64_t       ino;
	uint32_t       mode;
	uint16_t       type;
	uint16_t       flags;
	struct vfs_node *parent;
	struct vfs_node *children;
	struct vfs_node *next;
	struct vfs_mount *mounted;
	const char     *link_target;
	char            boot_path[VFS_PATH_MAX];
	uint8_t        *data;
	uint64_t        size;
	uint64_t        capacity;
};

struct vfs_mount {
	const char     *fs_name;
	struct vfs_node *mountpoint;
	struct vfs_node *root;
	struct vfs_mount *next;
};

void        vfs_init(void);
uint64_t    kernel_monotonic_ns(void);

int         vfs_register_filesystem(struct vfs_fs_type*);
int         vfs_mount(const char*, const char*);

struct vfs_node* vfs_lookup(const char*);
struct vfs_node* vfs_lookup_child(struct vfs_node*, const char*);
struct vfs_node* vfs_child_at(struct vfs_node*, uint64_t);
uint64_t         vfs_child_count(struct vfs_node*);

int              vfs_is_dir(const struct vfs_node*);
uint16_t         vfs_type(const struct vfs_node*);
uint32_t         vfs_mode(const struct vfs_node*);
uint64_t         vfs_ino(const struct vfs_node*);
const char*      vfs_link_target(const struct vfs_node*);
struct vfs_node* vfs_root(void);
int              vfs_mounted(const char*);

void             vfs_debug_dump(void);

size_t           vfs_list_dir(const char *path, char *out, size_t cap);
size_t           vfs_read_file(const char *path, char *out, size_t cap);
struct vfs_node* vfs_create_file(const char *path, int truncate);
struct vfs_node* vfs_mkdir(const char *path, uint32_t mode);
int              vfs_unlink(const char *path);
int              vfs_rmdir(const char *path);
int              vfs_rename(const char *oldpath, const char *newpath);

int              vfs_file_truncate(struct vfs_node *n, uint64_t len);
int              vfs_write_kernel_file(struct vfs_node *n, const uint8_t *data, uint64_t len);
uint64_t         vfs_file_write(struct vfs_node *n, uint64_t off, const uint8_t *src, uint64_t len);

int              vfs_file_is_regular(const struct vfs_node *n);
int              vfs_is_boot_file(const struct vfs_node *n);
const char*      vfs_node_name(const struct vfs_node *n);
const char*      vfs_boot_path(const struct vfs_node *n);
uint8_t*         vfs_file_data(struct vfs_node *n);
uint64_t         vfs_file_size(const struct vfs_node *n);

int              vfs_import_boot_file(const char *name, uint64_t size);
#endif
