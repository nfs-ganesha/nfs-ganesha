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
extern int *pi_indexes;
extern size_t i_snapshots;

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfsfsal_handle_t *handle)
{
  /* Check for the zpool (index == 0) */
  if(handle->data.i_snap == 0)
    return pp_vfs[0];

  /* Handle the indirection */
  int i;
  for(i = 1; i <= i_snapshots; i++)
  {
    if(pi_indexes[i] == handle->data.i_snap)
      return pp_vfs[i];
  }

  return NULL;
}

