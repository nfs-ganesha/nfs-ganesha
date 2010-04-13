/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_stats.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:37:28 $
 * \version $Revision: 1.3 $
 * \brief   Statistics functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

void FSAL_get_stats(fsal_statistics_t * stats,  /* OUT */
                    fsal_boolean_t reset        /* IN */
    )
{

  /* for logging */
  SetFuncID(INDEX_FSAL_get_stats);

  /* sanity check. */
  if(!stats)
    return;

  /* returns stats for this thread. */
  fsal_internal_getstats(stats);

  return;
}
