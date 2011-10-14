/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.7 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <sys/statvfs.h>

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: NULL pointer passed as input parameter.
 *      - ERR_FSAL_SERVERFAULT: Unexpected error.
 */
fsal_status_t GPFSFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * p_dynamicinfo  /* OUT */
    )
{
  struct statvfs buffstatvfs;
  int rc, errsv;
  /* sanity checks. */
  if(!p_filehandle || !p_dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  TakeTokenFSCall();
  rc = fstatvfs(((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd,
		&buffstatvfs);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_dynamic_fsinfo);

  p_dynamicinfo->total_bytes = buffstatvfs.f_frsize * buffstatvfs.f_blocks;
  p_dynamicinfo->free_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bfree;
  p_dynamicinfo->avail_bytes = buffstatvfs.f_frsize * buffstatvfs.f_bavail;

  p_dynamicinfo->total_files = buffstatvfs.f_files;
  p_dynamicinfo->free_files = buffstatvfs.f_ffree;
  p_dynamicinfo->avail_files = buffstatvfs.f_favail;

  p_dynamicinfo->time_delta.seconds = 1;
  p_dynamicinfo->time_delta.nseconds = 0;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
