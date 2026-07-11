#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include "vfs.h"
#include <stdint.h>

/* ── RamFS internal node ── */
typedef struct ramfs_node {
    char name[64];
    uint32_t type;               /* VFS_TYPE_FILE or VFS_TYPE_DIR */
    uint32_t size;               /* data length (0 for dirs) */
    uint8_t *data;               /* file content buffer (NULL for dirs) */
    struct ramfs_node *parent;
    struct ramfs_node *children; /* first child (for dirs) */
    struct ramfs_node *next;     /* next sibling in parent's children list */
} ramfs_node_t;

/* Create root directory node → returned to VFS as mount point */
vfs_node_t *ramfs_init(void);

#endif
