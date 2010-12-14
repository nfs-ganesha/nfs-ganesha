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
 * don genereux du gnu
 * permet de tourner meme sur cray ...
 */
static int Getsubopt(char **optionp, const char *const *tokens, char **valuep)
{
  char *endp, *vstart;
  int cnt;

  if(**optionp == '\0')
    return -1;

  /* Find end of next token.  */
  endp = strchr(*optionp, ',');
  if(endp == NULL)
    endp = strchr(*optionp, '\0');

  /* Find start of value.  */
  vstart = memchr(*optionp, '=', endp - *optionp);
  if(vstart == NULL)
    vstart = endp;

  /* Try to match the characters between *OPTIONP and VSTART against
     one of the TOKENS.  */
  for(cnt = 0; tokens[cnt] != NULL; ++cnt)
    if(memcmp(*optionp, tokens[cnt], vstart - *optionp) == 0
       && tokens[cnt][vstart - *optionp] == '\0')
      {
        /* We found the current option in TOKENS.  */
        *valuep = vstart != endp ? vstart + 1 : NULL;

        if(*endp != '\0')
          *endp++ = '\0';
        *optionp = endp;

        return cnt;
      }

  /* The current suboption does not match any option.  */
  *valuep = *optionp;

  if(*endp != '\0')
    *endp++ = '\0';
  *optionp = endp;

  return -1;
}

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
fsal_status_t POSIXFSAL_BuildExportContext(posixfsal_export_context_t * p_export_context,       /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options    /* IN */
    )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}



/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t POSIXFSAL_CleanUpExportContext(posixfsal_export_context_t * p_export_context)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

fsal_status_t POSIXFSAL_InitClientContext(posixfsal_op_context_t * p_thr_context)
{

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

 /**
 * FSAL_GetUserCred :
 * Get a user credential from its uid.
 *
 * \param p_cred (in out, posixfsal_cred_t *)
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

fsal_status_t POSIXFSAL_GetClientContext(posixfsal_op_context_t * p_thr_context,        /* IN/OUT  */
                                         posixfsal_export_context_t * p_export_context, /* IN */
                                         fsal_uid_t uid,        /* IN */
                                         fsal_gid_t gid,        /* IN */
                                         fsal_gid_t * alt_groups,       /* IN */
                                         fsal_count_t nb_alt_groups     /* IN */
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
