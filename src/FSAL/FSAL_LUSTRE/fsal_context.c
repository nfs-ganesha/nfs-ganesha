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
fsal_status_t LUSTREFSAL_BuildExportContext(fsal_export_context_t *exp_context,     /* OUT */
                                            fsal_path_t * p_export_path,        /* IN */
                                            char *fs_specific_options   /* IN */
    )
{
  /* Get the mount point for this lustre FS,
   * so it can be used for building .lustre/fid paths.
   */

  FILE *fp;
  struct mntent *p_mnt;
  struct stat pathstat;
  lustrefsal_export_context_t * p_export_context = (lustrefsal_export_context_t *)exp_context;

  char rpath[MAXPATHLEN];
  char mntdir[MAXPATHLEN];
  char fs_spec[MAXPATHLEN];
  char *ptr;

  char type[256];

  size_t pathlen, outlen;
  int rc;

  /* sanity check */
  if((p_export_context == NULL) || (p_export_path == NULL))
    {
      LogCrit(COMPONENT_FSAL, "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    }

  /* convert to canonical path */
  if(!realpath(p_export_path->path, rpath))
    {
      rc = errno;
      LogCrit(COMPONENT_FSAL, "Error %d in realpath(%s): %s",
                      rc, p_export_path->path, strerror(rc));
      Return(posix2fsal_error(rc), rc, INDEX_FSAL_BuildExportContext);
    }

  /* open mnt file */
  outlen = 0;

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
      /* get the longer path that matches export path */

      if(p_mnt->mnt_dir != NULL)
        {

          pathlen = strlen(p_mnt->mnt_dir);

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
      LogCrit(COMPONENT_FSAL, "No mount entry matches '%s' in %s", rpath, MOUNTED);
      endmntent(fp);
      Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_BuildExportContext);
    }

  /* display the mnt entry found */
  LogEvent(COMPONENT_FSAL, "'%s' matches mount point '%s', type=%s, fs=%s", rpath,
                  mntdir, type, fs_spec);

  /* Check it is a Lustre FS */
  if(!llapi_is_lustre_mnttype(type))
    {
      LogCrit(COMPONENT_FSAL,
                      "/!\\ ERROR /!\\ '%s' (type: %s) is not recognized as a Lustre Filesystem",
                      rpath, type);
      endmntent(fp);
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_BuildExportContext);
    }

  /* retrieve export info */
  if(stat(rpath, &pathstat) != 0)
    {
      rc = errno;
      LogCrit(COMPONENT_FSAL, "/!\\ ERROR /!\\ Couldn't stat '%s': %s", rpath,
                      strerror(rc));
      endmntent(fp);

      Return(posix2fsal_error(rc), rc, INDEX_FSAL_BuildExportContext);
    }

  /* all checks are OK, fill export context */
  strncpy(p_export_context->mount_point, mntdir, FSAL_MAX_PATH_LEN);
  p_export_context->mnt_len = strlen(mntdir);
  ptr = strrchr(fs_spec, '/');
  if (ptr) {
      ptr++;
      LogDebug(COMPONENT_FSAL, "Lustre fsname for %s is '%s'", mntdir, ptr);
      strncpy(p_export_context->fsname, ptr, MAX_LUSTRE_FSNAME);
  }
  p_export_context->dev_id = pathstat.st_dev;

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  endmntent(fp);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}
