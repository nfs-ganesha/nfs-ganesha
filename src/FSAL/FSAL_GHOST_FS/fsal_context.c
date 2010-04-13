/**
 *
 * \file    fsal_creds.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:00 $
 * \version $Revision: 1.6 $
 * \brief   FSAL credentials handling functions.
 */
/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include <pwd.h>
#include <string.h>
#include <errno.h>

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
fsal_status_t FSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */
    )
{
  SetFuncID(INDEX_FSAL_BuildExportContext);

  memset(p_export_context, 0, sizeof(fsal_export_context_t));
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_thr_context)
{
  SetFuncID(INDEX_FSAL_InitClientContext);

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* not used */
  p_thr_context->export_context = NULL;

  /* root authentication */
  p_thr_context->credential.user = 0;
  p_thr_context->credential.group = 0;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

 /**
 * FSAL_GetClientContext :
 * Get a user credential from its uid.
 */
fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups  /* IN */
    )
{
  SetFuncID(INDEX_FSAL_GetClientContext);

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the export specific context */
  p_thr_context->export_context = p_export_context;

  /* filling cred */
  p_thr_context->credential.user = uid;
  p_thr_context->credential.group = gid;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
