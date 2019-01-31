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

#ifndef CONF_URL_H
#define CONF_URL_H

#include <stdio.h>
#include "gsh_list.h"

struct gsh_url_provider {
	struct glist_head link;
	const char *name;
	void (*url_init)(void); /* XXX needs config info */
	void (*url_shutdown)(void);
	int (*url_fetch)(const char *url, FILE **f, char **fbuf);
};

/** @brief package initializer
 */

void config_url_init(void);
void config_url_shutdown(void);
int register_url_provider(struct gsh_url_provider *nurl_p);
int config_url_fetch(const char *url, FILE **f, char **fbuf);
void config_url_release(FILE *f, char *fbuf);

#endif /* CONF_URL_H */
