/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Scality Inc., 2016
 * Author: Guillaume Gimenez ploki@blackmilk.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* random.c
 */


#include "scality_methods.h"

#define RANDOM_DEV "/dev/urandom"
static __thread FILE *urandom__ = NULL;


static inline int
init_urandom(void)
{
	if ( NULL == urandom__ ) {
		urandom__ = fopen(RANDOM_DEV, "r");
		if ( NULL == urandom__ ) {
			LogCrit(COMPONENT_FSAL,
				"Unable to open "RANDOM_DEV": %s", strerror(errno));
			return -1;
		}
	}
	return 0;
}

ssize_t
random_read(char *buf, size_t sz)
{
	ssize_t ret;
	ret = init_urandom();
	if ( ret < 0 ) return ret;
	ret = fread(buf, 1, sz, urandom__);
	if ( ret != sz )  {
		if ( ferror(urandom__) )
			LogCrit(COMPONENT_FSAL,
				"read("RANDOM_DEV") failed: %s", strerror(errno));
		else
			LogCrit(COMPONENT_FSAL,
				"read("RANDOM_DEV") short read");
	}
	return ret;
}

#define HEX_LUT "0123456789abcdef"
ssize_t
random_hex(char *buf, size_t sz)
{
	size_t i;
	ssize_t ret = random_read(buf, sz);
	if ( ret < 0 )
		return ret;
	for ( i = 0 ; i < sz ; ++i )
		buf[i] = (buf[i]%(sizeof(HEX_LUT)-1))[HEX_LUT];
	return ret;
}
