#include "ramfs.h"
#include "../mm/kheap.h"
#include "../drivers/serial.h"
#include <stddef.h>

/* ── Forward declarations (ops callbacks) ── */
static int           ramfs_open(vfs_node_t *node, int flags);
static int           ramfs_read(vfs_node_t *node, uint32_t offset, uint8_t *buf, uint32_t size);
static int           ramfs_write(vfs_node_t *node, uint32_t offset, const uint8_t *buf, uint32_t size);
static int           ramfs_close(vfs_node_t *node);
static vfs_node_t   *ramfs_lookup(vfs_node_t *dir, const char *name);
static vfs_node_t   *ramfs_create(vfs_node_t *dir, const char *name, uint32_t type);
static int           ramfs_readdir(vfs_node_t *dir, uint32_t index, char *name_buf);

static vfs_node_ops_t g_ramfs_ops = {
    .open    = ramfs_open,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .close   = ramfs_close,
    .lookup  = ramfs_lookup,
    .create  = ramfs_create,
    .readdir = ramfs_readdir,
};

/* ── Allocate a vfs_node wrapping a new ramfs_node ── */
static vfs_node_t *ramfs_make_vfs_node(const char *name, uint32_t type)
{
    ramfs_node_t *rn = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!rn) return NULL;

    /* Copy name */
    int i;
    for (i = 0; name[i] && i < 63; i++)
        rn->name[i] = name[i];
    rn->name[i] = 0;

    rn->type     = type;
    rn->size     = 0;
    rn->data     = NULL;
    rn->parent   = NULL;
    rn->children = NULL;
    rn->next     = NULL;

    vfs_node_t *vn = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!vn) {
        kfree(rn);
        return NULL;
    }

    /* Copy name into VFS node too */
    for (i = 0; name[i] && i < 63; i++)
        vn->name[i] = name[i];
    vn->name[i] = 0;

    vn->type    = type;
    vn->size    = 0;
    vn->ops     = &g_ramfs_ops;
    vn->fs_data = rn;

    return vn;
}

/* ── ramfs_init — create root directory node ── */
vfs_node_t *ramfs_init(void)
{
    vfs_node_t *root = ramfs_make_vfs_node("/", VFS_TYPE_DIR);
    if (root) {
        ramfs_node_t *rn = (ramfs_node_t *)root->fs_data;
        rn->parent = rn;   /* root is its own parent */
        serial_write_string("RamFS: root created\n");
    }
    return root;
}

/* ── ramfs_open — nothing special (file content is in memory) ── */
static int ramfs_open(vfs_node_t *node, int flags)
{
    (void)node;
    (void)flags;
    return 0;
}

/* ── ramfs_read ── */
static int ramfs_read(vfs_node_t *node, uint32_t offset, uint8_t *buf, uint32_t size)
{
    ramfs_node_t *rn = (ramfs_node_t *)node->fs_data;

    if (rn->type != VFS_TYPE_FILE) return -1;
    if (!rn->data || offset >= rn->size) return 0;

    uint32_t to_read = size;
    if (offset + to_read > rn->size)
        to_read = rn->size - offset;

    for (uint32_t i = 0; i < to_read; i++)
        buf[i] = rn->data[offset + i];

    return (int)to_read;
}

/* ── ramfs_write ── */
static int ramfs_write(vfs_node_t *node, uint32_t offset, const uint8_t *buf, uint32_t size)
{
    ramfs_node_t *rn = (ramfs_node_t *)node->fs_data;

    if (rn->type != VFS_TYPE_FILE) return -1;
    if (size == 0) return 0;

    uint32_t needed = offset + size;

    if (needed > rn->size) {
        /* Expand buffer */
        uint8_t *new_data = (uint8_t *)kmalloc(needed);
        if (!new_data) return -1;

        /* Copy old content */
        for (uint32_t i = 0; i < rn->size; i++)
            new_data[i] = rn->data[i];

        /* Zero-fill gap (if offset > old size) */
        for (uint32_t i = rn->size; i < offset; i++)
            new_data[i] = 0;

        if (rn->data) kfree(rn->data);
        rn->data = new_data;
        rn->size = needed;
        node->size = needed;
    }

    /* Copy new data */
    for (uint32_t i = 0; i < size; i++)
        rn->data[offset + i] = buf[i];

    return (int)size;
}

/* ── ramfs_close — no-op (file stays in memory) ── */
static int ramfs_close(vfs_node_t *node)
{
    (void)node;
    return 0;
}

/* ── ramfs_lookup — find child by name in directory ── */
static vfs_node_t *ramfs_lookup(vfs_node_t *dir, const char *name)
{
    if (dir->type != VFS_TYPE_DIR) return NULL;

    ramfs_node_t *rdir = (ramfs_node_t *)dir->fs_data;
    ramfs_node_t *child = rdir->children;

    while (child) {
        /* Compare names */
        int match = 1;
        for (int i = 0; i < 64; i++) {
            if (child->name[i] != name[i]) {
                match = 0;
                break;
            }
            if (name[i] == 0) break;   /* both ended */
        }
        if (match) {
            /* Re-wrap in a vfs_node. Since we have the ramfs_node,
             * we need to reconstruct or cache the vfs_node wrapper.
             *
             * For simplicity: allocate a new vfs_node wrapper each time.
             * (A production FS would cache the vfs_node in the ramfs_node
             *  to avoid re-allocation on every lookup.) */
            vfs_node_t *vn = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            if (!vn) return NULL;

            /* Copy name */
            int j;
            for (j = 0; child->name[j] && j < 63; j++)
                vn->name[j] = child->name[j];
            vn->name[j] = 0;

            vn->type    = child->type;
            vn->size    = child->size;
            vn->ops     = &g_ramfs_ops;
            vn->fs_data = child;

            return vn;
        }
        child = child->next;
    }
    return NULL;
}

/* ── ramfs_create — create a new file/dir in a directory ── */
static vfs_node_t *ramfs_create(vfs_node_t *dir, const char *name, uint32_t type)
{
    if (dir->type != VFS_TYPE_DIR) return NULL;

    ramfs_node_t *rdir = (ramfs_node_t *)dir->fs_data;

    /* Allocate new ramfs node */
    ramfs_node_t *rn = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!rn) return NULL;

    int i;
    for (i = 0; name[i] && i < 63; i++)
        rn->name[i] = name[i];
    rn->name[i] = 0;

    rn->type     = type;
    rn->size     = 0;
    rn->data     = NULL;
    rn->parent   = rdir;
    rn->children = NULL;
    rn->next     = NULL;

    /* Prepend to parent's children list */
    rn->next       = rdir->children;
    rdir->children = rn;

    /* Wrap in vfs_node */
    vfs_node_t *vn = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!vn) {
        /* Rollback: remove from children list */
        rdir->children = rn->next;
        kfree(rn);
        return NULL;
    }

    for (i = 0; name[i] && i < 63; i++)
        vn->name[i] = name[i];
    vn->name[i] = 0;

    vn->type    = type;
    vn->size    = 0;
    vn->ops     = &g_ramfs_ops;
    vn->fs_data = rn;

    serial_write_string("RamFS: created '");
    serial_write_string(rn->name);
    serial_write_string("'\n");

    return vn;
}

/* ── ramfs_readdir — return child name at index ── */
static int ramfs_readdir(vfs_node_t *dir, uint32_t index, char *name_buf)
{
    if (dir->type != VFS_TYPE_DIR) return -1;

    ramfs_node_t *rdir = (ramfs_node_t *)dir->fs_data;
    ramfs_node_t *child = rdir->children;

    uint32_t i = 0;
    while (child) {
        if (i == index) {
            /* Copy name to user buffer (FIXME: validate) */
            int j;
            for (j = 0; child->name[j] && j < 63; j++)
                name_buf[j] = child->name[j];
            name_buf[j] = 0;
            return 0;
        }
        child = child->next;
        i++;
    }
    return -1;   /* index out of range */
}
