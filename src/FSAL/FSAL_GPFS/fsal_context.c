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
fsal_status_t FSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
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
  int rc;

  char *  handle  ;
  size_t  handle_len = 0 ;

  /* sanity check */
  if((p_export_context == NULL) || (p_export_path == NULL))
    {
      DisplayLogLevel(NIV_CRIT, "NULL mandatory argument passed to %s()", __FUNCTION__);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);
    } 

  /* Do the path_to_fshandle call to init the gpfs's libhandle */
  strncpy(  p_export_context->mount_point, fs_specific_options, MAXPATHLEN ) ;

  /* /!\ fs_specific_options contains the full path of the gpfs filesystem root */
  if( ( rc = path_to_fshandle( fs_specific_options,  (void **)(&handle), &handle_len) ) < 0 )
    Return( ERR_FSAL_FAULT, errno, INDEX_FSAL_BuildExportContext ) ;

  memcpy(  p_export_context->mnt_fshandle_val, handle, handle_len ) ;
  p_export_context->mnt_fshandle_len = handle_len ;

  if( ( rc = path_to_handle( fs_specific_options,  (void **)(&handle), &handle_len) ) < 0 )
    Return( ERR_FSAL_FAULT, errno, INDEX_FSAL_BuildExportContext ) ;

  memcpy(  p_export_context->mnt_handle_val, handle, handle_len ) ;
  p_export_context->mnt_handle_len = handle_len ;

  p_export_context->dev_id = 1 ; /** @todo BUGAZOMEU : put something smarter here, using setmntent */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_thr_context)
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

fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
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
#if defined( _DEBUG_FSAL )

  /* traces: prints p_credential structure */

  DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "credential modified:");
  DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tuid = %d, gid = %d",
                    p_thr_context->credential.user, p_thr_context->credential.group);

  for(i = 0; i < p_thr_context->credential.nbgroups; i++)
    DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tAlt grp: %d",
                      p_thr_context->credential.alt_groups[i]);

#endif

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
