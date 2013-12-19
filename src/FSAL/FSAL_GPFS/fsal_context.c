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

extern int g_nodeid;

/**
 * build the export entry
 */
fsal_status_t GPFSFSAL_BuildExportContext(fsal_export_context_t *export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */
    )
{
  int                  rc, fd, mntexists;
  FILE               * fp;
  struct mntent      * p_mnt;
  char               * mnt_dir = NULL;
  struct statfs        stat_buf;
  gpfs_fsal_up_ctx_t * gpfs_fsal_up_ctx;
  bool_t               start_fsal_up_thread = FALSE;
  int                  nodeid;
  struct grace_period_arg gpa;

  fsal_status_t status;
  fsal_op_context_t op_context;
  gpfsfsal_export_context_t *p_export_context = (gpfsfsal_export_context_t *)export_context;

  /* Make sure the FSAL UP context list is initialized */
  if(glist_null(&gpfs_fsal_up_ctx_list))
    init_glist(&gpfs_fsal_up_ctx_list);

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
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Checking Export Path %s against GPFS fs %s",
                       p_export_path->path, p_mnt->mnt_dir);

          /* If export path is shorter than fs path, then this isn't a match */
          if(strlen(p_export_path->path) < strlen(p_mnt->mnt_dir))
            continue;

          /* If export path doesn't have a path separator after mnt_dir, then it
           * isn't a proper sub-directory of mnt_dir.
           */
          if((p_export_path->path[strlen(p_mnt->mnt_dir)] != '/') &&
             (p_export_path->path[strlen(p_mnt->mnt_dir)] != '\0'))
            continue;

          if (strncmp(p_mnt->mnt_dir, p_export_path->path, strlen(p_mnt->mnt_dir)) == 0)
            {
              mnt_dir = gsh_strdup(p_mnt->mnt_dir);
              mntexists = 1;
              break;
            }
        }
  
  endmntent(fp);

  if (mntexists == 0)
    {
      LogMajor(COMPONENT_FSAL,
               "GPFS mount point %s does not exist.",
               p_export_path->path);
      gsh_free(mnt_dir);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* save file descriptor to root of GPFS share */
  fd = open(p_export_path->path, O_RDONLY | O_DIRECTORY);
  if(fd < 0)
    {
      if(errno == ENOENT)
        LogMajor(COMPONENT_FSAL,
                 "GPFS export path %s does not exist.",
                 p_export_path->path);
      else if (errno == ENOTDIR)
        LogMajor(COMPONENT_FSAL,
                 "GPFS export path %s is not a directory.",
                 p_export_path->path);
      else
        LogMajor(COMPONENT_FSAL,
                 "Could not open GPFS export path %s: rc = %d(%s)",
                 p_export_path->path, errno, strerror(errno));

      if(mnt_dir != NULL)
        gsh_free(mnt_dir);

      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  p_export_context->mount_root_fd = fd;

  LogFullDebug(COMPONENT_FSAL,
               "GPFSFSAL_BuildExportContext: %d",
               p_export_context->mount_root_fd);

  /* if the nodeid has not been obtained, get it now */
  if (!g_nodeid)
  {
    gpa.mountdirfd = fd;

    nodeid = gpfs_ganesha(OPENHANDLE_GET_NODEID, &gpa);
    if (nodeid >= 0)
    {
      /* GPFS starts with 0, we want node ids to be > 0 */
      g_nodeid = nodeid + 1;
      LogFullDebug(COMPONENT_FSAL, "nodeid = (%d)", g_nodeid);
    }
  }

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  /* save filesystem ID */
  rc = statfs(p_export_path->path, &stat_buf);

  if(rc)
    {
      close(fd);
      LogMajor(COMPONENT_FSAL,
               "statfs call failed on file %s: %d(%s)",
               p_export_path->path, errno, strerror(errno));

      if(mnt_dir != NULL)
        gsh_free(mnt_dir);

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

      if(mnt_dir != NULL)
        gsh_free(mnt_dir);

      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  gpfs_fsal_up_ctx = gpfsfsal_find_fsal_up_context(p_export_context);

  if(gpfs_fsal_up_ctx == NULL)
    {
      gpfs_fsal_up_ctx = gsh_calloc(1, sizeof(gpfs_fsal_up_ctx_t));

      if(gpfs_fsal_up_ctx == NULL || mnt_dir == NULL)
        {
          LogFatal(COMPONENT_FSAL,
                   "Out of memory can not continue.");
        }

      /* Initialize the gpfs_fsal_up_ctx */
      init_glist(&gpfs_fsal_up_ctx->gf_exports);
      gpfs_fsal_up_ctx->gf_fs = mnt_dir;
      gpfs_fsal_up_ctx->gf_fsid[0] = p_export_context->fsid[0];
      gpfs_fsal_up_ctx->gf_fsid[1] = p_export_context->fsid[1];

      /* Add it to the list of contexts */
      glist_add_tail(&gpfs_fsal_up_ctx_list, &gpfs_fsal_up_ctx->gf_list);

      start_fsal_up_thread = TRUE;
    }
  else
    {
      if(mnt_dir != NULL)
        gsh_free(mnt_dir);
    }

  /* Add this export context to the list for it's gpfs_fsal_up_ctx */
  glist_add_tail(&gpfs_fsal_up_ctx->gf_exports, &p_export_context->fe_list);
  p_export_context->fe_fsal_up_ctx = gpfs_fsal_up_ctx;

  if(start_fsal_up_thread)
    {
      pthread_attr_t attr_thr;

      memset(&attr_thr, 0, sizeof(attr_thr));

      /* Initialization of thread attributes borrowed from nfs_init.c */
      if(pthread_attr_init(&attr_thr) != 0)
        LogCrit(COMPONENT_THREAD, "can't init pthread's attributes");

      if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
        LogCrit(COMPONENT_THREAD, "can't set pthread's scope");

      if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
        LogCrit(COMPONENT_THREAD, "can't set pthread's join state");

      if(pthread_attr_setstacksize(&attr_thr, 2116488) != 0)
        LogCrit(COMPONENT_THREAD, "can't set pthread's stack size");

      rc = pthread_create(&gpfs_fsal_up_ctx->gf_thread,
                          &attr_thr,
                          GPFSFSAL_UP_Thread,
                          gpfs_fsal_up_ctx);

      if(rc != 0)
        {
          LogFatal(COMPONENT_THREAD,
                   "Could not create GPFSFSAL_UP_Thread, error = %d (%s)",
                   errno, strerror(errno));
        }
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

fsal_status_t GPFSFSAL_CleanUpExportContext(fsal_export_context_t * export_context) 
{
  gpfsfsal_export_context_t *p_export_context = (gpfsfsal_export_context_t *)export_context;

  if(export_context == NULL) 
  {
    LogCrit(COMPONENT_FSAL,
            "NULL mandatory argument passed to %s()", __FUNCTION__);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_CleanUpExportContext);
  }

  if(p_export_context->mount_root_fd != 0)
    close(p_export_context->mount_root_fd);

  if(p_export_context->fe_fsal_up_ctx != NULL)
    {
      /* Start to clean up FSAL_UP stuff. There is actually more to do here...*/
      glist_del(&p_export_context->fe_list);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

/* @} */
