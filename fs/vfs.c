#include "vfs.h"
#include "../sched/task.h"
#include "../mm/kheap.h"
#include "../drivers/serial.h"
#include <stddef.h>

/* ── Global root ── */
static vfs_node_t *g_root;

/* ── vfs_init ── */
void vfs_init(void)
{
    g_root = NULL;
    serial_write_string("VFS: init\n");
}

/* ── vfs_mount ──
 * Mount a filesystem root at path.  Only "/" supported for now. */
int vfs_mount(const char *path, vfs_node_t *root)
{
    if (path[0] != '/' || path[1] != 0) {
        serial_write_string("VFS: mount: only '/' supported\n");
        return -1;
    }
    g_root = root;
    serial_write_string("VFS: mounted root\n");
    return 0;
}

/* ── Tokenise path → next name component ── */
static int path_next(const char **pp, char *name_buf)
{
    const char *p = *pp;

    while (*p == '/') p++;
    if (*p == 0) return -1;

    int i = 0;
    while (*p != '/' && *p != 0 && i < 63)
        name_buf[i++] = *p++;
    name_buf[i] = 0;
    *pp = p;
    return 0;
}

/* ── vfs_lookup ──
 * Walk the path from root, following directory children via ops->lookup. */
vfs_node_t *vfs_lookup(const char *path)
{
    if (!g_root) return NULL;

    vfs_node_t *cur = g_root;
    const char *p = path;
    char name[64];

    while (path_next(&p, name) == 0) {
        if (cur->type != VFS_TYPE_DIR) return NULL;
        if (!cur->ops->lookup) return NULL;
        cur = cur->ops->lookup(cur, name);
        if (!cur) return NULL;
    }
    return cur;
}

/* ── vfs_open ──
 * Lookup path. If found, open it. If not found, create via parent->ops->create.
 * Returns fd index or -1. */
int vfs_open(const char *path, int flags)
{
    task_t *t = task_current();
    if (!t) return -1;

    /* Find free fd slot */
    int fd = -1;
    for (int i = 0; i < MAX_FD_PER_TASK; i++) {
        if (t->fd_table[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        serial_write_string("VFS: open: no free fd slot\n");
        return -1;
    }

    /* Try to find existing node */
    vfs_node_t *node = vfs_lookup(path);

    if (!node) {
        /* Doesn't exist — find parent and create.
         * For now, only single-component paths ("/foo") are supported. */
        const char *last_slash = NULL;
        for (const char *q = path; *q; q++)
            if (*q == '/') last_slash = q;

        if (!last_slash) return -1;
        const char *fname = last_slash + 1;
        if (*fname == 0) return -1;

        vfs_node_t *parent;
        if (last_slash == path) {
            parent = g_root;                 /* "/foo" → parent = root */
        } else {
            serial_write_string("VFS: open: nested paths not supported yet\n");
            return -1;
        }

        if (parent->type != VFS_TYPE_DIR || !parent->ops->create) return -1;

        node = parent->ops->create(parent, fname, VFS_TYPE_FILE);
        if (!node) {
            serial_write_string("VFS: open: create failed\n");
            return -1;
        }
    }

    /* Allocate fd */
    vfs_file_t *file = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (!file) {
        serial_write_string("VFS: open: out of memory\n");
        return -1;
    }
    file->node   = node;
    file->offset = 0;
    file->flags  = flags;

    t->fd_table[fd] = file;

    serial_write_string("VFS: open fd=");
    serial_write_hex(fd);
    serial_write_string(" flags=");
    serial_write_hex(flags);
    serial_write_string("\n");

    return fd;
}

/* ── vfs_read ── */
int vfs_read(int fd, uint8_t *buf, uint32_t size)
{
    task_t *t = task_current();
    if (!t || fd < 0 || fd >= MAX_FD_PER_TASK || !t->fd_table[fd])
        return -1;

    vfs_file_t *file = t->fd_table[fd];
    if (!file->node->ops->read) return -1;

    int n = file->node->ops->read(file->node, file->offset, buf, size);
    if (n > 0) file->offset += n;
    return n;
}

/* ── vfs_write ── */
int vfs_write(int fd, const uint8_t *buf, uint32_t size)
{
    task_t *t = task_current();
    if (!t || fd < 0 || fd >= MAX_FD_PER_TASK || !t->fd_table[fd])
        return -1;

    vfs_file_t *file = t->fd_table[fd];
    if (!file->node->ops->write) return -1;

    int n = file->node->ops->write(file->node, file->offset, buf, size);
    if (n > 0) file->offset += n;
    return n;
}

/* ── vfs_close ── */
int vfs_close(int fd)
{
    task_t *t = task_current();
    if (!t || fd < 0 || fd >= MAX_FD_PER_TASK || !t->fd_table[fd])
        return -1;

    vfs_file_t *file = t->fd_table[fd];
    kfree(file);
    t->fd_table[fd] = NULL;

    serial_write_string("VFS: close fd=");
    serial_write_hex(fd);
    serial_write_string("\n");

    return 0;
}

/* ── vfs_readdir ──
 * Look up the directory at `path`, then call its ops->readdir
 * to get the child at `index`.  Writes name into name_buf (64 bytes). */
int vfs_readdir(const char *path, uint32_t index, char *name_buf)
{
    vfs_node_t *dir = vfs_lookup(path);
    if (!dir || dir->type != VFS_TYPE_DIR) return -1;
    if (!dir->ops->readdir) return -1;
    /* FIXME: validate name_buf as user pointer */
    return dir->ops->readdir(dir, index, name_buf);
}
