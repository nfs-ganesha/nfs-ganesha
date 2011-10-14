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
fsal_status_t ZFSFSAL_dynamic_fsinfo(fsal_handle_t * filehandle,   /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * dynamicinfo    /* OUT */
    )
{

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  TakeTokenFSCall();

  struct statvfs statfs;
  int rc = libzfswrap_statfs(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs,
			     &statfs);

  ReleaseTokenFSCall();

  /* >> interpret returned status << */
  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_dynamic_fsinfo);

  dynamicinfo->total_bytes = statfs.f_frsize * statfs.f_blocks;
  dynamicinfo->free_bytes = statfs.f_frsize * statfs.f_bfree;
  dynamicinfo->avail_bytes = statfs.f_frsize * statfs.f_bavail;

  dynamicinfo->total_files = statfs.f_files;
  dynamicinfo->free_files = statfs.f_ffree;
  dynamicinfo->avail_files = statfs.f_favail;

  dynamicinfo->time_delta.seconds = 1;
  dynamicinfo->time_delta.nseconds = 0;


  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);
}
