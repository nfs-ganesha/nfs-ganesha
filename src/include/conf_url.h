/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
	int (*url_fetch)(const char *url, FILE **f, void (**)(FILE *, void *),
			 void **fbuf);
};

/** @brief package initializer
 */

void config_url_init(void);
void config_url_shutdown(void);
int register_url_provider(struct gsh_url_provider *nurl_p);
int config_url_fetch(const char *url, FILE **f, void (**rel)(FILE *, void *),
		     void **fbuf);
void config_url_release(FILE *f, char *fbuf);

int gsh_rados_url_setup_watch(void);
void gsh_rados_url_shutdown_watch(void);

struct plugin_module {
	struct glist_head link;
};

struct gsh_config_provider {
	struct glist_head link;
	int (*init_block)(config_file_t, struct config_error_type *);
	void (*config_shutdown)(void);
	int (*config_fetch)(const char *config, FILE **f, char **fbuf);
};
int register_config_locked(struct gsh_config_provider *);
int register_config(struct gsh_config_provider *);
int unregister_config_locked(struct gsh_config_provider *);
int unregister_config(struct gsh_config_provider *);

int config_plugin_load(char *);
int read_plugin_config(config_file_t foo, struct config_error_type *);

#endif /* CONF_URL_H */
