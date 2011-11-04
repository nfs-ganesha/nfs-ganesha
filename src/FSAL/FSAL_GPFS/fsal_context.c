/**
 *
 * \file    fsal_creds.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.15 $
 * \brief   FSAL credentials handling functions.
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

/**
 * build the export entry
 */
fsal_status_t GPFSFSAL_BuildExportContext(fsal_export_context_t *export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */
    )
{
  int rc, fd, mntexists;
  FILE *fp;
  struct mntent *p_mnt;
  struct statfs stat_buf;

  fsal_status_t status;
  fsal_op_context_t op_context;
  gpfsfsal_export_context_t *p_export_context = (gpfsfsal_export_context_t *)export_context;

  /* sanity check */
  if((p_export_context == NULL) || (p_export_path == NULL))
    {
      LogCrit(COMPONENT_FSAL,
              "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    }

  /* open mnt file */
  fp = setmntent(MOUNTED, "r");

  if(fp == NULL)
    {
      rc = errno;
      LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", rc, MOUNTED,
                      strerror(rc));
      Return(posix2fsal_error(rc), rc, INDEX_FSAL_BuildExportContext);
    }

  /* Check if mount point is really a gpfs share. If not, we can't continue.*/
  mntexists = 0;
  while((p_mnt = getmntent(fp)) != NULL)
    if(p_mnt->mnt_dir != NULL  && p_mnt->mnt_type != NULL)
      /* There is probably a macro for "gpfs" type ... not sure where it is. */
      if (strncmp(p_mnt->mnt_type, "gpfs", 4) == 0)
        if (strncmp(p_mnt->mnt_dir, p_export_path->path, strlen(p_mnt->mnt_dir)) == 0)
          mntexists = 1;
  
  if (mntexists == 0)
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Could not open GPFS mount point %s does not exist.",
               p_export_path->path);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* save file descriptor to root of GPFS share */
  fd = open(p_export_path->path, O_RDONLY | O_DIRECTORY);
  if(fd < 0)
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Could not open GPFS mount point %s: rc = %d",
               p_export_path->path, errno);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }
  p_export_context->mount_root_fd = fd;
  LogFullDebug(COMPONENT_FSAL, "GPFSFSAL_BuildExportContext: %d", p_export_context->mount_root_fd);

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  /* save filesystem ID */
  rc = statfs(p_export_path->path, &stat_buf);
  if(rc)
    {
      LogMajor(COMPONENT_FSAL,
               "statfs call failed on file %s: %d", p_export_path->path, rc);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }
  p_export_context->fsid[0] = stat_buf.f_fsid.__val[0];
  p_export_context->fsid[1] = stat_buf.f_fsid.__val[1];

  /* save file handle to root of GPFS share */
  op_context.export_context = export_context;
  // op_context.credential = ???
  status = fsal_internal_get_handle(&op_context,
                                    p_export_path,
                                    (fsal_handle_t *)(&(p_export_context->mount_root_handle)));
  if(FSAL_IS_ERROR(status))
    {
      close(p_export_context->mount_root_fd);
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Conversion from gpfs filesystem root path to handle failed : %d",
               status.minor);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t GPFSFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context) 
{
  if(p_export_context == NULL) 
  {
    LogCrit(COMPONENT_FSAL,
            "NULL mandatory argument passed to %s()", __FUNCTION__);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_CleanUpExportContext);
  }
  
  close(((gpfsfsal_export_context_t *)p_export_context)->mount_root_fd);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

/* @} */
