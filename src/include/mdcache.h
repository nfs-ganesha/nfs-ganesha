/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2016 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @file mdcache_int.h
 * @brief MDCache main internal interface.
 *
 * Main data structures and profiles for MDCache
 */

#ifndef MDCACHE_H
#define MDCACHE_H

#include "config.h"
#include "fsal_types.h"
#include "fsal_up.h"

/* Create an MDCACHE instance at the top of a stack */
fsal_status_t
mdcache_fsal_create_export(struct fsal_module *fsal_hdl, void *parse_node,
			   struct config_error_type *err_type,
			   const struct fsal_up_vector *super_up_ops);

/* Update an MDCACHE instance at the top of a stack */
fsal_status_t
mdcache_fsal_update_export(struct fsal_module *sub_fsal, void *parse_node,
			   struct config_error_type *err_type,
			   struct fsal_export *original);

/* Clean up on init failure */
void mdcache_export_uninit(void);

/* Initialize the MDCACHE package. */
fsal_status_t mdcache_pkginit(void);

/* Parse mdcache config */
int mdcache_set_param_from_conf(config_file_t parse_tree,
				struct config_error_type *err_type);

bool mdcache_lru_fds_available(void);
void init_fds_limit(void);
#endif /* MDCACHE_H */
