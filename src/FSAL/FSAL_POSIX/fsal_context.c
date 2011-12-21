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
