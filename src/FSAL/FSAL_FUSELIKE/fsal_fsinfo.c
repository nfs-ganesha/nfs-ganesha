/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:20:22 $
 * \version $Revision: 1.12 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"

#ifdef _LINUX
#include <sys/vfs.h>
#endif

#ifdef _APPLE
#include <sys/param.h>
#include <sys/mount.h>
#endif

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t FUSEFSAL_dynamic_fsinfo(fsal_handle_t *handle,   /* IN */
                                      fsal_op_context_t * p_context,        /* IN */
                                      fsal_dynamicfsinfo_t * dynamicinfo        /* OUT */
    )
{

  int rc;
  struct statvfs stbuff;
  char object_path[FSAL_MAX_PATH_LEN];
  fusefsal_handle_t *filehandle = (fusefsal_handle_t *)handle;

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  /* get the full path for the object */
  rc = NamespacePath(filehandle->data.inode,
                     filehandle->data.device, filehandle->data.validator, object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_dynamic_fsinfo);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  if(p_fs_ops->statfs)
    {
      TakeTokenFSCall();
      rc = p_fs_ops->statfs(object_path, &stbuff);
      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_dynamic_fsinfo);

      dynamicinfo->total_bytes = stbuff.f_frsize * stbuff.f_blocks;
      dynamicinfo->free_bytes = stbuff.f_frsize * stbuff.f_bfree;
      dynamicinfo->avail_bytes = stbuff.f_frsize * stbuff.f_bavail;

      dynamicinfo->total_files = stbuff.f_files;
      dynamicinfo->free_files = stbuff.f_ffree;
      dynamicinfo->avail_files = stbuff.f_favail;
    }
  else
    {
      /* return dummy values for beeing compliant with any client behavior */

      LogDebug(COMPONENT_FSAL,
               "FSAL_dynamic_fsinfo: statfs is not implemented on this filesystem. Returning dummy values.");

      dynamicinfo->total_bytes = INT_MAX;
      dynamicinfo->free_bytes = INT_MAX;
      dynamicinfo->avail_bytes = INT_MAX;

      dynamicinfo->total_files = 1024 * 1024;
      dynamicinfo->free_files = 1024 * 1024;
      dynamicinfo->avail_files = 1024 * 1024;
    }

  /* return time precision depending on utime calls implemented */
  if(p_fs_ops->utimens)
    {
      dynamicinfo->time_delta.seconds = 0;
      dynamicinfo->time_delta.nseconds = 1;
    }
  else
    {
      dynamicinfo->time_delta.seconds = 1;
      dynamicinfo->time_delta.nseconds = 0;
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
