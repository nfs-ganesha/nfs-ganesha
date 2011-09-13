/*
 * Common FSAL methods
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "log_macros.h"
#include "fsal.h"
#include "FSAL/common_methods.h"


/* Methods shared by most/all fsals.
 * These are either used in place of or can be called from the fsal specific
 * method to handle common (base class) operations
 */

/* Client context */

fsal_status_t COMMON_InitClientContext(fsal_op_context_t * p_thr_context)
{
  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);
}

fsal_status_t COMMON_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups /* IN */ )
{
  fsal_count_t ng = nb_alt_groups;
  unsigned int i;

  /* sanity check */
  if(!p_thr_context || !p_export_context ||
     ((ng > 0) && (alt_groups == NULL)))
	  Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the export specific context */
  p_thr_context->export_context = p_export_context;
  p_thr_context->credential.user = uid;
  p_thr_context->credential.group = gid;

  if(ng > FSAL_NGROUPS_MAX) /* this artificially truncates the group list ! */
	  ng = FSAL_NGROUPS_MAX;
  p_thr_context->credential.nbgroups = ng;

  for(i = 0; i < ng; i++)
	  p_thr_context->credential.alt_groups[i] = alt_groups[i];

  if(isFullDebug(COMPONENT_FSAL)) {
	  /* traces: prints p_credential structure */

	  LogFullDebug(COMPONENT_FSAL, "credential modified:\tuid = %d, gid = %d",
		       p_thr_context->credential.user,
		       p_thr_context->credential.group);
	  for(i = 0; i < p_thr_context->credential.nbgroups; i++)
		  LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
			       p_thr_context->credential.alt_groups[i]);
  }
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);
}

