/*
 *
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal.h
 * @brief Miscelaneous FSAL definitions
 */

#ifndef FSAL_H
#define FSAL_H


/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "common_utils.h"

/**
 * @brief Delegations types list for the Delegations parameter in FSAL.
 * This is actually defined in exports.c
 */
extern struct config_item_list deleg_types[];

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

#include "fsal_api.h"
#include "FSAL/access_check.h"	/* rethink where this should go */

void display_fsinfo(struct fsal_staticfsinfo_t *info);

/**
 * @brief If we don't know how big a buffer we want for a link, use
 * this value.
 */

static const size_t fsal_default_linksize = 4096;

int fsal_load_init(void *node, const char *name,
		   struct fsal_module **fsal_hdl_p,
		   struct config_error_type *err_type);

struct fsal_args {
	char *name;
};

void *fsal_init(void *link_mem, void *self_struct);

struct subfsal_args {
	char *name;
	void *fsal_node;
};

int subfsal_commit(void *node, void *link_mem, void *self_struct,
		   struct config_error_type *err_type);

void destroy_fsals(void);
void emergency_cleanup_fsals(void);

const char *msg_fsal_err(fsal_errors_t fsal_err);

#endif				/* !FSAL_H */
/** @} */
