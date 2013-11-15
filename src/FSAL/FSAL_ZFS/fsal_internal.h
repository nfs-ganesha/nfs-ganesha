/**
 *
 * \file    fsal_internal.h
 * \date    $Date: 2006/01/24 13:45:37 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 *
 */

#include  "fsal.h"
#include <libzfswrap.h>

/* linkage to the exports and handle ops initializers
 */

void zfs_export_ops_init(struct export_ops *ops);
void zfs_handle_ops_init(struct fsal_obj_ops *ops);

/* libzfswrap handler, used only when the FSAL is created and destroyed */
extern libzfswrap_handle_t *p_zhd;

void ZFSFSAL_VFS_RDLock();
void ZFSFSAL_VFS_RDLock();
void ZFSFSAL_VFS_Unlock();

typedef struct zfs_file_handle {
	inogen_t zfs_handle;
	char i_snap;
} zfs_file_handle_t;

#define ZFS_SNAP_DIR ".zfs"

#define ZFS_SNAP_DIR_INODE 2

typedef struct {
	char *psz_name;
	libzfswrap_vfs_t *p_vfs;
	unsigned int index;
} snapshot_t;

/* defined the set of attributes supported with POSIX */
#define ZFS_SUPPORTED_ATTRIBUTES (                         \
		ATTR_TYPE     | ATTR_SIZE     |		   \
		ATTR_FSID     | ATTR_FILEID   |		   \
		ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     |	\
		ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    |	\
		ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED |	\
		ATTR_CHGTIME)

static inline size_t zfs_sizeof_handle(struct zfs_file_handle *hdl)
{
	return (size_t) sizeof(struct zfs_file_handle);
}

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern struct fsal_staticfsinfo_t global_fs_info;

#endif
