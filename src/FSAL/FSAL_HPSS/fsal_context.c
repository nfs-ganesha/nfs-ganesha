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

/** usefull subopt definitions */
enum
{
  FILESET_OPTION = 0,
  COS_OPTION = 1
};

const char *fs_specific_opts[] = {
  "fileset",
  "cos",
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
fsal_status_t HPSSFSAL_BuildExportContext(hpssfsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options     /* IN */
    )
{
  char subopts[256];
  char *p_subop;
  char *value;

  char *filesetname = NULL;
  int read_cos = 0;             /* 0 => not set */

  int rc;

  /* sanity check */
  if(!p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);

  if((fs_specific_options != NULL) && (fs_specific_options[0] != '\0'))
    {

      /* cleans the export context */
      memset(p_export_context, 0, sizeof(hpssfsal_export_context_t));

      /* copy the option string (because it is modified by getsubopt call) */
      strncpy(subopts, fs_specific_options, 256);
      p_subop = subopts;        /* set initial pointer */

      /* parse the FS specific option string */

      switch (Getsubopt(&p_subop, fs_specific_opts, &value))
        {
        case FILESET_OPTION:
          filesetname = value;
          break;

        case COS_OPTION:
          read_cos = atoi(value);
          if(read_cos <= 0)
            {
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for EXPORT::FS_Specific::%s : ( %s ) positive integer expected.",
                   fs_specific_opts[COS_OPTION], value);
              Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_BuildExportContext);
            }
          break;

        default:
          {
            LogCrit(COMPONENT_FSAL,
                "FSAL LOAD PARAMETER: ERROR: Invalid suboption found in EXPORT::FS_Specific : %s : %s or %s expected.",
                 value, fs_specific_opts[FILESET_OPTION], fs_specific_opts[COS_OPTION]);
            Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_BuildExportContext);
          }
        }

    }

  /* fills the export context structure */

  p_export_context->default_cos = read_cos;

  rc = HPSSFSAL_GetFilesetRoot(filesetname, &p_export_context->fileset_root_handle);

  if(rc != 0)
    {
      LogCrit(COMPONENT_FSAL,
          "FSAL LOAD PARAMETER: ERROR: Could not get root handle for fileset \"%s\"",
           (filesetname == NULL ? filesetname : "<root>"));
      Return(ERR_FSAL_INVAL, -rc, INDEX_FSAL_BuildExportContext);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);

}

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t HPSSFSAL_CleanUpExportContext(hpssfsal_export_context_t * p_export_context) 
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

fsal_status_t HPSSFSAL_InitClientContext(hpssfsal_op_context_t * p_thr_context)
{

  int rc, i;

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  /* LoadThreadState */

  /* We set umask to 0.
   * The specified umask is applied later (before calling HPSS API functions)
   */
  if(rc = hpss_LoadThreadState(0, 0, NULL))
    {
      Return(ERR_FSAL_PERM, -rc, INDEX_FSAL_GetClientContext);
    }

  /* get associated user p_cred */
  if(rc = hpss_GetThreadUcred(&(p_thr_context->credential.hpss_usercred)))
    {
      Return(ERR_FSAL_PERM, -rc, INDEX_FSAL_InitClientContext);
    }

  /* sets the credential time */
  p_thr_context->credential.last_update = time(NULL);

#if (HPSS_MAJOR_VERSION == 5)

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential created:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.SecPWent.Uid,
                    p_thr_context->credential.hpss_usercred.SecPWent.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.SecPWent.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#elif ( HPSS_MAJOR_VERSION == 6 )

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential created:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.Uid,
                    p_thr_context->credential.hpss_usercred.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#elif ( HPSS_MAJOR_VERSION == 7 )

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential created:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.Uid,
                    p_thr_context->credential.hpss_usercred.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#endif

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

 /**
 * FSAL_GetClientContext :
 * Get a user credential from its uid.
 * 
 * \param p_cred (in out, hpssfsal_cred_t *)
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

fsal_status_t HPSSFSAL_GetClientContext(hpssfsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                        hpssfsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups      /* IN */
    )
{

  fsal_count_t ng = nb_alt_groups;
  unsigned int i;
  fsal_status_t st;

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* if the credential is too old, renew it */
  if(time(NULL) - p_thr_context->credential.last_update > (int)CredentialLifetime)
    {
      st = HPSSFSAL_InitClientContext(p_thr_context);
      if(FSAL_IS_ERROR(st))
        return st;
    }

  /* set the export specific context */
  p_thr_context->export_context = p_export_context;

  /* Extracted from  /opt/hpss/src/nfs/nfsd/nfs_Dispatch.c */

#if HPSS_MAJOR_VERSION == 5
  strcpy(p_thr_context->credential.hpss_usercred.SecPWent.Name, "NFS.User");
  p_thr_context->credential.hpss_usercred.SecLabel = 0; /* Symbol? */
  p_thr_context->credential.hpss_usercred.CurAccount = ACCT_REC_DEFAULT;
  p_thr_context->credential.hpss_usercred.DefAccount = ACCT_REC_DEFAULT;
  p_thr_context->credential.hpss_usercred.SecPWent.Uid = uid;
  p_thr_context->credential.hpss_usercred.SecPWent.Gid = gid;
#elif  (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)
  strcpy(p_thr_context->credential.hpss_usercred.Name, "NFS.User");
  p_thr_context->credential.hpss_usercred.CurAccount = ACCT_REC_DEFAULT;
  p_thr_context->credential.hpss_usercred.DefAccount = ACCT_REC_DEFAULT;
  p_thr_context->credential.hpss_usercred.Uid = uid;
  p_thr_context->credential.hpss_usercred.Gid = gid;
#endif

  if(ng > HPSS_NGROUPS_MAX)
    ng = HPSS_NGROUPS_MAX;

  if((ng > 0) && (alt_groups == NULL))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  p_thr_context->credential.hpss_usercred.NumGroups = ng;

  for(i = 0; i < ng; i++)
    p_thr_context->credential.hpss_usercred.AltGroups[i] = alt_groups[i];

#if (HPSS_MAJOR_VERSION == 5)

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential modified:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.SecPWent.Uid,
                    p_thr_context->credential.hpss_usercred.SecPWent.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.SecPWent.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#elif ( HPSS_MAJOR_VERSION == 6 )

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential modified:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.Uid,
                    p_thr_context->credential.hpss_usercred.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#elif ( HPSS_MAJOR_VERSION == 7 )

  /* traces: prints p_credential structure */
  if (isFullDebug(COMPONENT_FSAL))
    {
      LogFullDebug(COMPONENT_FSAL, "credential modified:");
      LogFullDebug(COMPONENT_FSAL, "\tuid = %d, gid = %d",
                    p_thr_context->credential.hpss_usercred.Uid,
                    p_thr_context->credential.hpss_usercred.Gid);
      LogFullDebug(COMPONENT_FSAL, "\tName = %s",
                    p_thr_context->credential.hpss_usercred.Name);

      for(i = 0; i < p_thr_context->credential.hpss_usercred.NumGroups; i++)
	LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
                      p_thr_context->credential.hpss_usercred.AltGroups[i]);
    }
#endif

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
