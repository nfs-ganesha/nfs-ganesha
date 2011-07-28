/**
 * Common FS tools for internal use in the FSAL.
 *
 */

#ifndef FSAL_COMMON_H
#define FSAL_COMMON_H

#include "fsal.h"

/* >> You can define here the functions that are to be used
 * all over your FSAL but you don't want to be called externaly <<
 *
 * Ex: YouFS_GetRoot( zfsfsal_handle_t * out_hdl, char * server_name, ... );
 */

#define ZFS_SNAP_DIR ".zfs"
#define ZFS_SNAP_DIR_INODE 2

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfsfsal_handle_t *handle);
void ZFSFSAL_VFS_RDLock();
void ZFSFSAL_VFS_WRLock();
void ZFSFSAL_VFS_Unlock();

#endif
