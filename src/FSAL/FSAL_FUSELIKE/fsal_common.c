/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * Common FS tools for internal use in the FSAL.
 *
 */
#include "config.h"

#include <string.h>
#include "fsal_common.h"
#include "fsal_internal.h"

void FSAL_internal_append_path(char *tgt, char *parent, char *child)
{
  size_t len;

  len = strlen(parent);

  if((len > 0) && (parent[len - 1] != '/'))
    snprintf(tgt, FSAL_MAX_PATH_LEN, "%s/%s", parent, child);
  else
    snprintf(tgt, FSAL_MAX_PATH_LEN, "%s%s", parent, child);

}
