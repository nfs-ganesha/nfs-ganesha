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
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 * 
 * @{
 */

/**
 * build the export entry
 */
fsal_status_t GPFSFSAL_BuildExportContext(gpfsfsal_export_context_t * p_export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */
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

  char type[256];

  size_t pathlen, outlen;
  int rc, fd;

  char *handle;
  size_t handle_len = 0;
  struct statfs stat_buf;

  fsal_status_t status;
  fsal_op_context_t op_context;

  /* sanity check */
  if((p_export_context == NULL) || (p_export_path == NULL))
    {
      LogCrit(COMPONENT_FSAL, "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    }

  /* save file descriptor to root of GPFS share */
  fd = open(p_export_path->path, O_RDONLY | O_DIRECTORY);
  if(fd < 0)
    {
      close(open_by_handle_fd);
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Could not open GPFS mount point %s: rc = %d",
               p_export_path->path, errno);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }
  p_export_context->mount_root_fd = fd;

  /* save filesystem ID */
  rc = statfs(p_export_path->path, &stat_buf);
  if(rc)
    {
      LogFullDebug(COMPONENT_FSAL, "statfs call failed on file %s: %d\n", p_export_path->path, rc);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }
  p_export_context->fsid[0] = stat_buf.f_fsid.__val[0];
  p_export_context->fsid[1] = stat_buf.f_fsid.__val[1];

  /* save file handle to root of GPFS share */
  op_context.export_context = p_export_context;
  // op_context.credential = ???
  status = fsal_internal_get_handle(&op_context,
                                    p_export_path,
                                    &(p_export_context->mount_root_handle));
  if(FSAL_IS_ERROR(status))
    {
      close(open_by_handle_fd);
      close(p_export_context->mount_root_fd);
      LogMajor(COMPONENT_FSAL,
               "FSAL BUILD EXPORT CONTEXT: ERROR: Conversion from gpfs"
               "filesystem root path to handle failed : %d", status.minor);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t GPFSFSAL_InitClientContext(gpfsfsal_op_context_t * p_thr_context)
{

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t GPFSFSAL_CleanUpExportContext(gpfsfsal_export_context_t * p_export_context) 
{
  if(p_export_context == NULL) 
  {
    LogCrit(COMPONENT_FSAL, "NULL mandatory argument passed to %s()", __FUNCTION__);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_CleanUpExportContext);
  }
  
  close(p_export_context->mount_root_fd);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
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

fsal_status_t GPFSFSAL_GetClientContext(gpfsfsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    gpfsfsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups  /* IN */
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

      for(i = 0; i < p_thr_context->credential.nbgroups; i++)
        LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                     p_thr_context->credential.alt_groups[i]);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
