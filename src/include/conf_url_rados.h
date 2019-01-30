/* ----------------------------------------------------------------------------
 * Copyright (C) 2017, Red Hat, Inc.
 * contributeur : Matt Benjamin  mbenjamin@redhat.com
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
 * ---------------------------------------
 */

#ifndef CONF_URL_RADOS_H
#define CONF_URL_RADOS_H

#include "config.h"

#ifdef RADOS_URLS

#include <stdio.h>
#include "gsh_list.h"
#include <rados/librados.h>

void conf_url_rados_pkginit(void);
int rados_url_setup_watch(void);
void rados_url_shutdown_watch(void);

#else

static inline void conf_url_rados_pkginit(void)
{
}

static inline int rados_url_setup_watch(void)
{
	return 0;
}

static inline void rados_url_shutdown_watch(void)
{
}

#endif /* RADOS_URLS */
#endif /* CONF_URL_RADOS_H */
