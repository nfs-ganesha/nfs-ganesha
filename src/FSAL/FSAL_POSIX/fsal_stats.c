/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_stats.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.2 $
 * \brief   Statistics functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

/**
 * FSAL_get_stats:
 * Retrieve call statistics for current thread.
 *
 * \param stats (output):
 *        Pointer to the call statistics structure.
 * \param reset (input):
 *        Boolean that indicates if the stats must be reset.
 *
 * \return Nothing.
 */

void POSIXFSAL_get_stats(fsal_statistics_t * stats,     /* OUT */
                         fsal_boolean_t reset   /* IN */
    )
{

  /* sanity check. */
  if(!stats)
    return;

  /* returns stats for this thread. */
  fsal_internal_getstats(stats);

  return;
}
