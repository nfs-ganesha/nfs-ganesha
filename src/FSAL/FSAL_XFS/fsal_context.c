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
fsal_status_t XFSFSAL_BuildExportContext(xfsfsal_export_context_t * p_export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options      /* IN */
    )
{
  /* Get the mount point for this lustre FS,
   * so it can be used for building .lustre/fid paths.
   */

  FILE *fp;
  struct mntent *p_mnt;
  struct stat pathstat;

  char rpath[MAXPATHLEN];
  char mntdir[MAXPATHLEN];
  char fs_spec[MAXPATHLEN];

  char *first_xfs_dir = NULL;
  char type[256];

  size_t pathlen, outlen;
  int rc;

  char *handle;
  size_t handle_len = 0;

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

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t XFSFSAL_CleanUpExportContext(xfsfsal_export_context_t * p_export_context)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

fsal_status_t XFSFSAL_InitClientContext(xfsfsal_op_context_t * p_thr_context)
{

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

 /**
 * FSAL_GetUserCred :
 * Get a user credential from its uid.
 *
 * \param p_cred (in out, fsal_cred_t *)
 *        Initialized credential to be changed
 *        for representing user.
 * \param uid (in, fsal_uid_t)
 *        user identifier.
 * \param gid (in, fsal_gid_t)
 *        group identifier.
 * \param alt_groups (in, fsal_gid_t *)
 *        list of alternative groups.
 * \param nb_alt_groups (in, fsal_count_t)
 *        number of alternative groups.
 *
 * \return major codes :
 *      - ERR_FSAL_PERM : the current user cannot
 *                        get credentials for this uid.
 *      - ERR_FSAL_FAULT : Bad adress parameter.
 *      - ERR_FSAL_SERVERFAULT : unexpected error.
 */

fsal_status_t XFSFSAL_GetClientContext(xfsfsal_op_context_t * p_thr_context,    /* IN/OUT  */
                                       xfsfsal_export_context_t * p_export_context,     /* IN */
                                       fsal_uid_t uid,  /* IN */
                                       fsal_gid_t gid,  /* IN */
                                       fsal_gid_t * alt_groups, /* IN */
                                       fsal_count_t nb_alt_groups       /* IN */
    )
{

  fsal_count_t ng = nb_alt_groups;
  unsigned int i;

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the export specific context */
  p_thr_context->export_context = p_export_context;

  /* Extracted from  /opt/hpss/src/nfs/nfsd/nfs_Dispatch.c */
  p_thr_context->credential.user = uid;
  p_thr_context->credential.group = gid;

  if(ng > FSAL_NGROUPS_MAX)
    ng = FSAL_NGROUPS_MAX;
  if((ng > 0) && (alt_groups == NULL))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  p_thr_context->credential.nbgroups = ng;

  for(i = 0; i < ng; i++)
    p_thr_context->credential.alt_groups[i] = alt_groups[i];

  if(isFullDebug(COMPONENT_FSAL))
    {
      /* traces: prints p_credential structure */

      LogFullDebug(COMPONENT_FSAL, "credential modified:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                        p_thr_context->credential.user, p_thr_context->credential.group);

      if (isFullDebug(COMPONENT_FSAL))
        {
          for(i = 0; i < p_thr_context->credential.nbgroups; i++)
            LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                         p_thr_context->credential.alt_groups[i]);
        }
   }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);
}

/* @} */
