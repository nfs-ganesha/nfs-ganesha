/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.25 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "abstract_mem.h"
#include "SemN.h"

#include <pthread.h>

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;

libzfswrap_handle_t *p_zhd;

size_t i_snapshots;
snapshot_t *p_snapshots;
pthread_rwlock_t vfs_lock;


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


