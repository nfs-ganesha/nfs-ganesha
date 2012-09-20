/*
 *
 *
 * Copyright IBM (2012)
 * contributeur : Frank Filz <ffilz@us.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

#ifndef _TIMERS_H
#define _TIMERS_H

#include <time.h>
#include "abstract_atomic.h"

#define MSEC_PER_SEC  1000UL

/* Implement fast milisecond epoch timers. */
typedef int64_t msectimer_t;

/* The functions below will fetch a timer value */
/*
static inline msectimer_t timer_get(void)
*/

static inline msectimer_t atomic_fetch_msectimer_t(msectimer_t * timer)
{
  return atomic_fetch_int64_t(timer);
}

static inline void atomic_store_msectimer_t(msectimer_t *var, msectimer_t val)
{
  atomic_store_int64_t(var, val);
}

/******************************************************************************
 *                                                                            *
 * TIMER DEFINITIONS FOR LINUX                                                *
 *                                                                            *
 ******************************************************************************/

#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE CLOCK_MONOTONIC
#endif

#define NSEC_PER_MSEC 1000000L

/* Fetch the time */
static inline msectimer_t timer_get(void)
{
  /* Get time in user space only */ 
  struct timespec tspec;
  int rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &tspec);

  if(rc != 0)
    return -1;

  return tspec.tv_sec * MSEC_PER_SEC + tspec.tv_nsec / NSEC_PER_MSEC;
}

#endif /*  _TIMERS_H */
