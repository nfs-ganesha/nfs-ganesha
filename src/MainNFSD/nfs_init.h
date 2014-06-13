/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 *
 * ---------------------------------------
 */

/**
 * @file    nfs_init.h
 * @brief   NFSd initialization prototypes.
 */

#ifndef NFS_INIT_H
#define NFS_INIT_H

#include "log.h"
#include "nfs_core.h"

typedef struct __nfs_start_info {
	int dump_default_config;
	int lw_mark_trigger;
} nfs_start_info_t;

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
void nfs_prereq_init(char *program_name, char *host_name, int debug_level,
		     char *log_path);

/**
 * nfs_set_param_from_conf:
 * Load parameters from config file.
 */
int nfs_set_param_from_conf(config_file_t config_struct,
			    nfs_start_info_t *p_start_info);

/**
 * Initialization that needs config file parse but must be done
 * before any services actually start (exports, net sockets...)
 */

int init_server_pkgs(void);

/**
 * nfs_start:
 * start NFS service
 */
void nfs_start(nfs_start_info_t *p_start_info);

#endif				/* !NFS_INIT_H */
