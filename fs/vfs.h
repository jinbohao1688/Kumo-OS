#ifndef FS_VFS_H
#define FS_VFS_H

#include <stdint.h>

#define VFS_TYPE_FILE  0
#define VFS_TYPE_DIR   1

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2

/* ── Forward structs ── */
struct vfs_node;

/* ── Per-filesystem operations ── */
typedef struct vfs_node_ops {
    int (*open)(struct vfs_node *node, int flags);
    int (*read)(struct vfs_node *node, uint32_t offset, uint8_t *buf, uint32_t size);
    int (*write)(struct vfs_node *node, uint32_t offset, const uint8_t *buf, uint32_t size);
    int (*close)(struct vfs_node *node);
    struct vfs_node *(*lookup)(struct vfs_node *dir, const char *name);
    struct vfs_node *(*create)(struct vfs_node *dir, const char *name, uint32_t type);
    int (*readdir)(struct vfs_node *dir, uint32_t index, char *name_buf);
} vfs_node_ops_t;

/* ── VFS node (file or directory) ── */
typedef struct vfs_node {
    char name[64];
    uint32_t type;           /* VFS_TYPE_FILE or VFS_TYPE_DIR */
    uint32_t size;           /* file size (unused for dirs) */
    vfs_node_ops_t *ops;
    void *fs_data;           /* filesystem-private node (e.g. ramfs_node_t *) */
} vfs_node_t;

/* ── Open file description (one per fd) ── */
typedef struct vfs_file {
    vfs_node_t *node;
    uint32_t offset;
    int flags;
} vfs_file_t;

/* ── VFS API ── */
void vfs_init(void);
int  vfs_mount(const char *path, vfs_node_t *root);

/* FD operations — operate on the current task's fd table */
int  vfs_open(const char *path, int flags);
int  vfs_read(int fd, uint8_t *buf, uint32_t size);
int  vfs_write(int fd, const uint8_t *buf, uint32_t size);
int  vfs_close(int fd);

/* Path → node lookup */
vfs_node_t *vfs_lookup(const char *path);

/* Directory iteration — returns 0 on success, -1 if index out of range */
int vfs_readdir(const char *path, uint32_t index, char *name_buf);

#endif
