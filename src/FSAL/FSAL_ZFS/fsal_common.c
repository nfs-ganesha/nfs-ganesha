/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * Common FS tools for internal use in the FSAL.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal_common.h"
#include "fsal_internal.h"

extern libzfswrap_vfs_t **pp_vfs;
extern size_t i_snapshots;

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfsfsal_handle_t *handle)
{
  if(handle->data.i_snap <= i_snapshots)
    return pp_vfs[handle->data.i_snap];
  else
    return NULL;
}

