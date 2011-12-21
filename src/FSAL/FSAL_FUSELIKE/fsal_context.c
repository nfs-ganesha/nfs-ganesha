/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

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
#include "fsal_common.h"
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 * 
 * @{
 */

/**
 * Parse FS specific option string
 * to build the export entry option.
 */
fsal_status_t FUSEFSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options     /* IN */
    )
{
  int rc;

  /* sanity check */
  if(!p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);

  /* TODO */
  /* no FS specific option string supported for now */
  /* retrieves the root handle */
  /* store root full path */

  /* unused */
  memset(p_export_context, 0, sizeof(fusefsal_export_context_t));

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);

}

fsal_status_t FUSEFSAL_InitClientContext(fsal_op_context_t *context)
{

  int rc, i;
  fusefsal_op_context_t * p_thr_context = (fusefsal_op_context_t *)context;

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  /* set credential info */
  p_thr_context->credential.user = 0;
  p_thr_context->credential.group = 0;
  p_thr_context->credential.nbgroups = 0;

  /* build fuse context */
  p_thr_context->ganefuse_context.ganefuse = NULL;
  p_thr_context->ganefuse_context.uid = 0;
  p_thr_context->ganefuse_context.gid = 0;
  p_thr_context->ganefuse_context.pid = getpid();
  p_thr_context->ganefuse_context.private_data = fs_private_data;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

 /**
 * FSAL_GetClientContext :
 * Get a user credential from its uid.
 * 
 * \param p_cred (in out, fusefsal_cred_t *)
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

fsal_status_t FUSEFSAL_GetClientContext(fsal_op_context_t *context,  /* IN/OUT  */
                                        fsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups      /* IN */
    )
{

  fusefsal_op_context_t * p_thr_context = (fusefsal_op_context_t *)context;
  fsal_status_t st;

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the specific export context */
  p_thr_context->export_context = (fusefsal_export_context_t *) p_export_context;

  /* set credential info */
  p_thr_context->credential.user = uid;
  p_thr_context->credential.group = gid;
  p_thr_context->credential.nbgroups = 0; /* no alt groups at present */

  /* build fuse context */
  p_thr_context->ganefuse_context.ganefuse = NULL;
  p_thr_context->ganefuse_context.uid = uid;
  p_thr_context->ganefuse_context.gid = gid;
  p_thr_context->ganefuse_context.pid = getpid();
  p_thr_context->ganefuse_context.private_data = fs_private_data;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
