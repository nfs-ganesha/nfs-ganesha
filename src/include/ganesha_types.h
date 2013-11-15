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

#include <stdint.h>

/* An elapsed time in nanosecs works because an unsigned
 * 64 bit can hold ~584 years of nanosecs.  If any code I have
 * ever written stays up that long, I would be amazed (and dead
 * a very long time...)
 */

typedef uint64_t nsecs_elapsed_t;

static const nsecs_elapsed_t NS_PER_USEC = 1000;
static const nsecs_elapsed_t NS_PER_MSEC = 1000000;
static const nsecs_elapsed_t NS_PER_SEC = 1000000000;

/**
 * @brief Buffer descriptor
 *
 * This structure is used to describe a counted buffer as an
 * address/length pair.
 */

struct gsh_buffdesc {
	void *addr;		/*< First octet/byte of the buffer */
	size_t len;		/*< Length of the buffer */
};

#endif				/* !GANESHA_TYPES__ */
