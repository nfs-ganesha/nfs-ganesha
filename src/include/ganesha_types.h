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
