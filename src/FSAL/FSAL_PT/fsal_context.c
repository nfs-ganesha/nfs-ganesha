// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_context.c
// Description: FSAL context operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_context.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <mntent.h>             /* for handling mntent */
#include <libgen.h>             /* for dirname */
#include <sys/vfs.h>            /* for fsid */

#include "pt_ganesha.h"

/**
 * build the export entry
 */
fsal_status_t
PTFSAL_BuildExportContext(fsal_export_context_t * export_context,     /* OUT */
                          fsal_path_t           * p_export_path,      /* IN */
                          char                  * fs_specific_options /* IN */)
{
  fsal_status_t             status;
  fsal_op_context_t         op_context;
  ptfsal_export_context_t * p_export_context = 
    (ptfsal_export_context_t *)export_context;
  exportlist_t            * p_exportlist = NULL;
  char                    * endptr = NULL;
  int                       n; 

  FSI_TRACE(FSI_DEBUG, "Begin-------------------\n");

  /* sanity check */
  if((p_export_context == NULL) || (p_export_path == NULL))
    {
      LogCrit(COMPONENT_FSAL,
              "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    }

  FSI_TRACE(FSI_DEBUG, "PT FS Export ID=%s Mount Path=%s", 
            fs_specific_options, p_export_path->path);
  status = PTFSAL_GetExportEntry(fs_specific_options, &p_exportlist);
  if(FSAL_IS_ERROR(status)) {
    LogMajor(COMPONENT_FSAL,
             "FSAL BUILD EXPORT CONTEXT: ERROR: Conversion from "  
             "ptfs filesystem root path to handle failed : %d",
             status.minor);
    ReturnCode(ERR_FSAL_INVAL, 0);
  }  

  p_export_context->fe_static_fs_info = &global_fs_info;
  strncpy(p_export_context->mount_point, p_export_path->path, 
          sizeof(p_export_context->mount_point));
  p_export_context->mount_point[sizeof(p_export_context->mount_point)-1] = '\0';
  p_export_context->fsid[0] = 0;
  p_export_context->fsid[1] = p_exportlist->id;
  op_context.export_context = export_context;
  op_context.credential.user = 0;
  op_context.credential.group = 0;
  p_export_context->ganesha_export_id = p_exportlist->id;

  errno = 0;
  p_export_context->pt_export_id  = strtoll(fs_specific_options, &endptr, 10);
  if (p_export_context->pt_export_id  == LLONG_MIN ||
      p_export_context->pt_export_id  == LLONG_MAX || errno != 0) {
    LogMajor(COMPONENT_FSAL,
             "FSAL BUILD EXPORT CONTEXT: ERROR: "
             "Get Export ID failed : %d", errno);
    ReturnCode(ERR_FSAL_INVAL, 0);  
  }

  status = PTFSAL_GetMountRootFD(&op_context);
  if(FSAL_IS_ERROR(status)) {
    LogMajor(COMPONENT_FSAL,
             "FSAL BUILD EXPORT CONTEXT: ERROR: "
             "Get mount root fd failed : %d",
             status.minor);
    ReturnCode(ERR_FSAL_INVAL, 0);
  }

  FSI_TRACE(FSI_DEBUG, 
            "Export Id=%d, PT FS Export ID=%ld Mount Path=%s Mount root fd=%d",
            p_export_context->ganesha_export_id, 
            p_export_context->pt_export_id,
            p_export_context->mount_point,
            p_export_context->mount_root_fd);
  
  FSI_TRACE(FSI_DEBUG, "End-----------------------------\n");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, ptfsal_export_context_t)
 */
fsal_status_t
PTFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context)
{
  FSI_TRACE(FSI_DEBUG, "Begin----------");
  if(p_export_context == NULL) {
    LogCrit(COMPONENT_FSAL,
            "NULL mandatory argument passed to %s()", __FUNCTION__);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_CleanUpExportContext);
  }

  FSI_TRACE(FSI_DEBUG, "End----------");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

/* Look up export entry based on fs specific options which stores 
 * PT FS export ID 
 */
fsal_status_t
PTFSAL_GetExportEntry(char          * p_fs_info,   /* IN */
		      exportlist_t ** exportlist /* OUT */)
{
  exportlist_t * p_exportlist;
  if (p_fs_info == NULL) {
    FSI_TRACE(FSI_DEBUG, "NULL mandatory FS information\n");
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext); 
  }
  FSI_TRACE(FSI_DEBUG, "FS info: %s", p_fs_info);

  p_exportlist = nfs_param.pexportlist;
  while (p_exportlist != NULL) {
    FSI_TRACE(FSI_DEBUG, "FS info in Export list: %s", 
              p_exportlist->FS_specific);
    if(strncmp(p_fs_info, p_exportlist->FS_specific, 
       sizeof(p_exportlist->FS_specific))) {
      p_exportlist = p_exportlist->exp_list.next;
      continue;
    } else {
      FSI_TRACE(FSI_DEBUG, "Equal\n");
      break;
    }
  }
  if (p_exportlist == NULL) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
  }
  *exportlist = p_exportlist;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

/*
 * PTFSAL_GetMountRootFD
 * 
 * Stub function for future use
 */
fsal_status_t
PTFSAL_GetMountRootFD(fsal_op_context_t * p_context)
{
  fsal_path_t root_path;

  ptfsal_op_context_t     * fsi_op_context = (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;
  
  /* PT basically doesn't need mount root FD, so we could set it to zero */
  fsi_export_context->mount_root_fd = 0;

  /* Get the file handle */
  root_path.len = 0;
  strcpy(root_path.path, "");
  fsal_status_t status = fsal_internal_get_handle( p_context, &root_path,
                            &(p_context->export_context->mount_root_handle) );
  if (FSAL_IS_ERROR(status)) {
    FSI_TRACE(FSI_ERR, "fsal_internal_get_handle returned error %d",
               status.minor);
    ReturnCode(ERR_FSAL_INVAL, 0);
  }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}


