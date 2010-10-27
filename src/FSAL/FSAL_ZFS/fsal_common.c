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

#include <assert.h>

#include "fsal_common.h"
#include "fsal_internal.h"

extern size_t i_snapshots;
extern snapshot_t *p_snapshots;
extern pthread_rwlock_t vfs_lock;

libzfswrap_vfs_t *ZFSFSAL_GetVFS(zfsfsal_handle_t *handle)
{
  /* This function must be called with the reader lock locked */
  assert(pthread_rwlock_trywrlock(&vfs_lock) != 0);
  /* Check for the zpool (index == 0) */
  if(handle->data.i_snap == 0)
    return p_snapshots[0].p_vfs;

  /* Handle the indirection */
  int i;
  for(i = 1; i < i_snapshots + 1; i++)
  {
    if(p_snapshots[i].index == handle->data.i_snap)
    {
      LogFullDebug(COMPONENT_FSAL, "Looking up inside the snapshot n°%d", handle->data.i_snap);
      return p_snapshots[i].p_vfs;
    }
  }

  LogMajor(COMPONENT_FSAL, "Unable to get the right VFS");
  return NULL;
}

void ZFSFSAL_VFS_RDLock()
{
  pthread_rwlock_rdlock(&vfs_lock);
}

void ZFSFSAL_VFS_WRLock()
{
  pthread_rwlock_wrlock(&vfs_lock);
}

void ZFSFSAL_VFS_Unlock()
{
  pthread_rwlock_unlock(&vfs_lock);
}
