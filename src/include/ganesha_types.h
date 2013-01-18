/*
 * Copyright © CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */


/**
 * @file   ganesha_types.h
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul  9 16:59:11 2012
 *
 * @brief Miscelalneous types used throughout Ganesha.
 *
 * This file contains miscellaneous types used through multiple layers
 * in Ganesha.
 */

#ifndef GANESHA_TYPES__
#define GANESHA_TYPES__

/**
 * @brief Inline functions for timespec math
 *
 * This is for timespec math.  If you want to do
 * do the same kinds of math on timeval values,
 * See timeradd(3) in GLIBC.
 *
 * The primary purpose of nsecs_elapsed_t is for a compact
 * and quick way to handle time issues relative to server
 * start and server EPOCH (which is not quite the same thing
 * but too complicated to explain here).
 */

#define NS_PER_SEC 1000000000

/* An elapsed time in nanosecs works because an unsigned
 * 64 bit can hold ~584 years of nanosecs.  If any code I have
 * ever written stays up that long, I would be amazed (and dead
 * a very long time...)
 */

typedef uint64_t nsecs_elapsed_t;

/**
 * @brief Get the abs difference between two timespecs in nsecs
 *
 * useful for cheap time calculation. Works with Dr. Who...
 *
 * @param start timespec of before end
 * @param end   timespec after start time
 *
 * @return elapsed time in nsecs
 */

static
inline nsecs_elapsed_t timespec_diff(struct timespec *start,
				     struct timespec *end)
{
	if((end->tv_sec > start->tv_sec) ||
	   (end->tv_sec == start->tv_sec &&
	    end->tv_nsec >= start->tv_nsec)) {
		return (end->tv_sec - start->tv_sec) * NS_PER_SEC
			+ (end->tv_nsec - start->tv_nsec);
	} else {
		return (start->tv_sec - end->tv_sec) * NS_PER_SEC
			+ (start->tv_nsec - end->tv_nsec);
	}
}

/**
 * @brief Convert a timespec to an elapsed time interval
 *
 * This will work for wallclock time until 2554.
 */

static
inline nsecs_elapsed_t timespec_to_nsecs(struct timespec *timespec)
{
	return timespec->tv_sec * NS_PER_SEC + timespec->tv_nsec;
}

/**
 * @brief Convert an elapsed time interval to a timespec
 */

static
inline void nsecs_to_timespec(nsecs_elapsed_t interval,
			      struct timespec *timespec)
{
	timespec->tv_sec = interval / NS_PER_SEC;
	timespec->tv_nsec = interval % NS_PER_SEC;
}

/**
 * @brief Add an interval to a timespec
 *
 * @param interval in nsecs
 * @param timespec call by reference time
 */

static
inline void timespec_add_nsecs(nsecs_elapsed_t interval,
			      struct timespec *timespec)
{
	timespec->tv_sec += (interval / NS_PER_SEC);
	timespec->tv_nsec += (interval % NS_PER_SEC);
	if(timespec->tv_nsec > NS_PER_SEC) {
		timespec->tv_sec += (timespec->tv_nsec / NS_PER_SEC);
		timespec->tv_nsec = timespec->tv_nsec % NS_PER_SEC;
	}
}

/**
 * @brief Add an interval to a timespec
 *
 * @param interval in nsecs
 * @param timespec call by reference time
 */

static
inline void timespec_sub_nsecs(nsecs_elapsed_t interval,
			      struct timespec *t)
{
	struct timespec ts;

	nsecs_to_timespec(interval, &ts);

	if(ts.tv_nsec > t->tv_nsec) {
		t->tv_sec -= (ts.tv_sec +1);
		t->tv_nsec = ts.tv_nsec - t->tv_nsec;
	} else {
		t->tv_sec -= ts.tv_sec;
		t->tv_nsec -= ts.tv_nsec;
	}
}

/**
 * @brief Compare two times
 *
 * Determine if @c t1 is less-than, equal-to, or greater-than @c t2.
 *
 * @param[in] t1 First time
 * @param[in] t2 Second time
 *
 * @retval -1 @c t1 is less-than @c t2
 * @retval 0 @c t1 is equal-to @c t2
 * @retval 1 @c t1 is greater-than @c t2
 */

static inline int
gsh_time_cmp(struct timespec t1,
             struct timespec t2)
{
        if (t1.tv_sec < t2.tv_sec) {
                return -1;
        } else if (t1.tv_sec > t2.tv_sec) {
                return 1;
        } else {
                if (t1.tv_nsec < t2.tv_nsec) {
                        return -1;
                } else if (t1.tv_nsec > t2.tv_nsec) {
                        return 1;
                }
        }

        return 0;
}

/**
 * Get the time right now as a timespec
 *
 * @param ts [in] pointer to a timespec struct
 */

static inline void now(struct timespec *ts)
{
	int rc;

	rc = clock_gettime(CLOCK_REALTIME, ts);
	if(rc != 0) {
		LogCrit(COMPONENT_MAIN, "Failed to get timestamp");
		assert(0);  /* if this is broken, we are toast so die */
	}
}

/**
 * @brief Buffer descriptor
 *
 * This structure is used to describe a counted buffer as an
 * address/length pair.
 */

struct gsh_buffdesc {
	void *addr;  /*< First octet/byte of the buffer */
	size_t len;  /*< Length of the buffer */
};

#endif /* !GANESHA_TYPES__ */
