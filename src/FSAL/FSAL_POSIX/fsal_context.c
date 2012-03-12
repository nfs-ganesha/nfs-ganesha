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
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

const char *fs_specific_opts[] = {
  NULL
};

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
fsal_status_t POSIXFSAL_BuildExportContext(fsal_export_context_t * p_export_context,       /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options    /* IN */
    )
{
  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t POSIXFSAL_InitClientContext(fsal_op_context_t * thr_context)
{
  posixfsal_op_context_t * p_thr_context = (posixfsal_op_context_t *) thr_context;
  fsal_posixdb_status_t st;

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  st = fsal_posixdb_connect(&global_posixdb_params, &(p_thr_context->p_conn));
  if(FSAL_POSIXDB_IS_ERROR(st))
    {
      LogCrit(COMPONENT_FSAL,
              "CRITICAL ERROR: Worker could not connect to database !!!");
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_InitClientContext);
    }
  else
    {
      LogEvent(COMPONENT_FSAL, "Worker successfuly connected to database");
    }

  /* sets the credential time */
/*  p_thr_context->credential.last_update = time( NULL );*/

  /* traces: prints p_credential structure */

  /*
     LogDebug(COMPONENT_FSAL, "credential created:");
     LogDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
     p_thr_context->credential.hpss_usercred.SecPWent.Uid, p_thr_context->credential.hpss_usercred.SecPWent.Gid);
     LogDebug(COMPONENT_FSAL, "\tName = %s",
     p_thr_context->credential.hpss_usercred.SecPWent.Name);

     for ( i=0; i< p_thr_context->credential.hpss_usercred.NumGroups; i++ )
     LogDebug(COMPONENT_FSAL, "\tAlt grp: %d",
     p_thr_context->credential.hpss_usercred.AltGroups[i] );
   */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

/* @} */
