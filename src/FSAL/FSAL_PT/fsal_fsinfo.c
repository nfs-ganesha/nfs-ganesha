// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_fsinfo.c
// Description: FSAL file system info operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
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

#include "fsi_ipc_ccl.h"
#include "pt_ganesha.h"
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
fsal_status_t
PTFSAL_dynamic_fsinfo(fsal_handle_t        * p_filehandle, /* IN */
                      fsal_op_context_t    * p_context,    /* IN */
                      fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */)
{
  int rc, errsv;
  char fsi_name[PATH_MAX];

  /* sanity checks. */
  if(!p_filehandle || !p_dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  memset(fsi_name, 0, sizeof(fsi_name));
  rc = ptfsal_handle_to_name(p_filehandle, p_context, fsi_name);
  errsv = errno;
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_dynamic_fsinfo);
  FSI_TRACE(FSI_DEBUG, "Mount Root Name: %s", fsi_name);

  rc = ptfsal_dynamic_fsinfo(p_filehandle, p_context, p_dynamicinfo); 
  errsv = errno;
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_dynamic_fsinfo);
  

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
