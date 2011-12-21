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
#include "HashTable.h"

extern snapshot_t *p_snapshots;

/** usefull subopt definitions */

/* define your specific NFS export options here : */
enum
{
  YOUR_OPTION_1 = 0,
  YOUR_OPTION_2 = 1,
  YOUR_OPTION_3 = 2,
  YOUR_OPTION_4 = 3,
};

const char *fs_specific_opts[] = {
  "option1",
  "option2",
  "option3",
  "option4",
  NULL
};

/**
 * generous gift from GNU
 * so you can use it even on Cray, etc...
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
fsal_status_t ZFSFSAL_BuildExportContext(fsal_export_context_t * exp_context, /* OUT */
                                         fsal_path_t * p_export_path,      /* IN */
                                         char *fs_specific_options /* IN */
    )
{
  char subopts[256];
  char *p_subop;
  char *value;
  zfsfsal_export_context_t * p_export_context = (zfsfsal_export_context_t *)exp_context;

  /* sanity check */
  if(!p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);

  if((fs_specific_options != NULL) && (fs_specific_options[0] != '\0'))
    {

      /* copy the option string (because it is modified by getsubopt call) */
      strncpy(subopts, fs_specific_options, 256);
      subopts[255] = '\0';
      p_subop = subopts;        /* set initial pointer */

      /* parse the FS specific option string */

      switch (Getsubopt(&p_subop, fs_specific_opts, &value))
        {
        case YOUR_OPTION_1:
          /* analyze your option 1 and fill the export_context structure */
          break;

        case YOUR_OPTION_2:
          /* analyze your option 2 and fill the export_context structure */
          break;

        case YOUR_OPTION_3:
          /* analyze your option 3 and fill the export_context structure */
          break;

        case YOUR_OPTION_4:
          /* analyze your option 4 and fill the export_context structure */
          break;

        default:
          {
            LogCrit(COMPONENT_FSAL,
                "FSAL LOAD PARAMETER: ERROR: Invalid suboption found in EXPORT::FS_Specific : %s : xxxxxx expected.",
                 value);
            Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_BuildExportContext);
          }
        }

    }

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  /* Initialize the libzfs library here */
  p_export_context->p_vfs = p_snapshots[0].p_vfs;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}
