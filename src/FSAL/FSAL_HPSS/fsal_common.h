/**
 * Common HPSS tools for internal use in the FSAL.
 *
 */

#ifndef FSAL_COMMON_H
#define FSAL_COMMON_H

#include "fsal.h"
#include "HPSSclapiExt/hpssclapiext.h"

/* if fileset_name is null, root fileset is returned */
int HPSSFSAL_GetFilesetRoot(char *fileset_name, ns_ObjHandle_t * p_root_hdl);

void HPSSFSAL_BuildCos(fsal_uint_t CosId,
                       hpss_cos_hints_t * hints, hpss_cos_priorities_t * hintpri);

int HPSSFSAL_IsStaleHandle(ns_ObjHandle_t * p_hdl, TYPE_CRED_HPSS * p_cred);

#endif
