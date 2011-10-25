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
fsal_status_t VFSFSAL_BuildExportContext(fsal_export_context_t * context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options      /* IN */
    )
{
  /* Get the mount point for this lustre FS,
   * so it can be used for building .lustre/fid paths.
   */

  vfsfsal_export_context_t * p_export_context = (vfsfsal_export_context_t *) context;
  FILE *fp;
  struct mntent *p_mnt;

  char rpath[MAXPATHLEN];
  char mntdir[MAXPATHLEN];
  char fs_spec[MAXPATHLEN];

  char *first_vfs_dir = NULL;
  char type[MAXNAMLEN];

  size_t pathlen, outlen;
  int rc;
  int mnt_id = 0 ;

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
      /* get the longer path vfs related export that matches export path */

      if(p_mnt->mnt_dir != NULL)
        {

          pathlen = strlen(p_mnt->mnt_dir);

          if(first_vfs_dir == NULL)
            first_vfs_dir = p_mnt->mnt_dir;

          if((pathlen > outlen) && !strcmp(p_mnt->mnt_dir, "/"))
            {
              LogDebug(COMPONENT_FSAL,
                              "Root mountpoint is allowed for matching %s, type=%s, fs=%s",
                              rpath, p_mnt->mnt_type, p_mnt->mnt_fsname);
              outlen = pathlen;
              strncpy(mntdir, p_mnt->mnt_dir, MAXPATHLEN);
              strncpy(type, p_mnt->mnt_type, MAXNAMLEN);
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
              strncpy(type, p_mnt->mnt_type, MAXNAMLEN);
              strncpy(fs_spec, p_mnt->mnt_fsname, MAXPATHLEN);
            }
        }
    }

  if(outlen <= 0)
    {
      if(p_export_path == NULL)
        strncpy(mntdir, first_vfs_dir, MAXPATHLEN);
      else
        {
          LogCrit(COMPONENT_FSAL, "No mount entry matches '%s' in %s", rpath, MOUNTED);
          endmntent(fp);
          Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_BuildExportContext);
        }
    }

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  /* save file descriptor to root of VFS export */
  if( ( p_export_context->mount_root_fd = open(mntdir, O_RDONLY | O_DIRECTORY) ) < 0 )
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Could not open VFS mount point %s: rc = %d",
               mntdir, errno);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* Keep fstype in export_context */
  strncpy(  p_export_context->fstype, type, MAXNAMLEN ) ;

  if( !strncmp( type, "xfs", MAXNAMLEN ) )
   {
     LogMajor( COMPONENT_FSAL,
               "Trying to export XFS filesystem via FSAL_VFS for mount point %s. Use FSAL_XFS instead", mntdir ) ;
     ReturnCode(ERR_FSAL_INVAL, 0);
   }

  p_export_context->root_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( vfs_fd_to_handle( p_export_context->mount_root_fd,
			&p_export_context->root_handle,
                        &mnt_id ) ) {
	  LogMajor(COMPONENT_FSAL,
		   "vfs_fd_to_handle: root_path: %s, root_fd=%d, errno=(%d) %s",
		   mntdir, p_export_context->mount_root_fd,
		   errno, strerror(errno));
	 Return(posix2fsal_error(errno), errno, INDEX_FSAL_BuildExportContext) ;
  }

#ifdef TODO
  if(isFullDebug(COMPONENT_FSAL))
    {
      char str[1024] ;

      sprint_mem( str, p_export_context->root_handle.handle ,p_export_context->root_handle.handle_bytes ) ;
      LogFullDebug(COMPONENT_FSAL,
                   "=====> root Handle: type=%u bytes=%u|%s\n",
                   p_export_context->root_handle.handle_type,  p_export_context->root_handle.handle_bytes, str ) ;

    }
#endif

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t VFSFSAL_InitClientContext(fsal_op_context_t * p_context)
{
  vfsfsal_op_context_t * p_thr_context = (vfsfsal_op_context_t *)p_context;
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

fsal_status_t VFSFSAL_GetClientContext(fsal_op_context_t * thr_context,    /* IN/OUT  */
                                       fsal_export_context_t * p_export_context,     /* IN */
                                       fsal_uid_t uid,  /* IN */
                                       fsal_gid_t gid,  /* IN */
                                       fsal_gid_t * alt_groups, /* IN */
                                       fsal_count_t nb_alt_groups       /* IN */
    )
{
  vfsfsal_op_context_t * p_thr_context = (vfsfsal_op_context_t *) thr_context;
  fsal_count_t ng = nb_alt_groups;
  unsigned int i;

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the export specific context */
  p_thr_context->export_context = (vfsfsal_export_context_t *) p_export_context;

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
