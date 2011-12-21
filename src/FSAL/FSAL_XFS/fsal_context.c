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

/**
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 *
 * @{
 */

/**
 * build the export entry
 */
fsal_status_t XFSFSAL_BuildExportContext(fsal_export_context_t *export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options      /* IN */
    )
{
  /* Get the mount point for this lustre FS,
   * so it can be used for building .lustre/fid paths.
   */

  FILE *fp;
  struct mntent *p_mnt;

  char rpath[MAXPATHLEN];
  char mntdir[MAXPATHLEN];
  char fs_spec[MAXPATHLEN];

  char *first_xfs_dir = NULL;
  char type[256];

  size_t pathlen, outlen;
  int rc;

  char *handle;
  size_t handle_len = 0;
  xfsfsal_export_context_t *p_export_context = export_context;

  /* sanity check */
  if(p_export_context == NULL)
    {
      LogCrit(COMPONENT_FSAL, "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    }

  outlen = 0;

  if(p_export_path != NULL)
    strncpy(rpath, p_export_path->path, MAXPATHLEN);

  /* open mnt file */
  fp = setmntent(MOUNTED, "r");

  if(fp == NULL)
    {
      rc = errno;
      LogCrit(COMPONENT_FSAL, "Error %d in setmntent(%s): %s", rc, MOUNTED,
                      strerror(rc));
      Return(posix2fsal_error(rc), rc, INDEX_FSAL_BuildExportContext);
    }

  while((p_mnt = getmntent(fp)) != NULL)
    {
      /* get the longer path xfs related export that matches export path */

      if(p_mnt->mnt_dir != NULL)
        {

          pathlen = strlen(p_mnt->mnt_dir);

          if(strncmp(p_mnt->mnt_type, "xfs", 256))
            continue;

          if(first_xfs_dir == NULL)
            first_xfs_dir = p_mnt->mnt_dir;

          if((pathlen > outlen) && !strcmp(p_mnt->mnt_dir, "/"))
            {
              LogDebug(COMPONENT_FSAL,
                              "Root mountpoint is allowed for matching %s, type=%s, fs=%s",
                              rpath, p_mnt->mnt_type, p_mnt->mnt_fsname);
              outlen = pathlen;
              strncpy(mntdir, p_mnt->mnt_dir, MAXPATHLEN);
              strncpy(type, p_mnt->mnt_type, 256);
              strncpy(fs_spec, p_mnt->mnt_fsname, MAXPATHLEN);
            }
          /* in other cases, the filesystem must be <mountpoint>/<smthg> or <mountpoint>\0 */
          else if((pathlen > outlen) &&
                  !strncmp(rpath, p_mnt->mnt_dir, pathlen) &&
                  ((rpath[pathlen] == '/') || (rpath[pathlen] == '\0')))
            {
              LogFullDebug(COMPONENT_FSAL, "%s is under mountpoint %s, type=%s, fs=%s",
                              rpath, p_mnt->mnt_dir, p_mnt->mnt_type, p_mnt->mnt_fsname);

              outlen = pathlen;
              strncpy(mntdir, p_mnt->mnt_dir, MAXPATHLEN);
              strncpy(type, p_mnt->mnt_type, 256);
              strncpy(fs_spec, p_mnt->mnt_fsname, MAXPATHLEN);
            }
        }
    }

  if(outlen <= 0)
    {
      if(p_export_path == NULL)
        strncpy(mntdir, first_xfs_dir, MAXPATHLEN);
      else
        {
          LogCrit(COMPONENT_FSAL, "No mount entry matches '%s' in %s", rpath, MOUNTED);
          endmntent(fp);
          Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_BuildExportContext);
        }
    }

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  /* Do the path_to_fshandle call to init the xfs's libhandle */
  strncpy(p_export_context->mount_point, mntdir, MAXPATHLEN);

  if((rc = path_to_fshandle(mntdir, (void **)(&handle), &handle_len)) < 0)
    Return(ERR_FSAL_FAULT, errno, INDEX_FSAL_BuildExportContext);

  memcpy(p_export_context->mnt_fshandle_val, handle, handle_len);
  p_export_context->mnt_fshandle_len = handle_len;

  if((rc = path_to_handle(mntdir, (void **)(&handle), &handle_len)) < 0)
    Return(ERR_FSAL_FAULT, errno, INDEX_FSAL_BuildExportContext);

  memcpy(p_export_context->mnt_handle_val, handle, handle_len);
  p_export_context->mnt_handle_len = handle_len;

  p_export_context->dev_id = 1;  /** @todo BUGAZOMEU : put something smarter here, using setmntent */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}
