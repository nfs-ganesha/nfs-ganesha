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
#include "gsh_rpc.h"

typedef struct __nfs_start_info {
	int dump_default_config;
	int lw_mark_trigger;
	bool drop_caps;
} nfs_start_info_t;

struct nfs_init {
	pthread_mutex_t init_mutex;
	pthread_cond_t init_cond;
	bool init_complete;
};

extern struct nfs_init nfs_init;

pthread_t gsh_dbus_thrid;

void nfs_init_init(void);
void nfs_init_complete(void);
void nfs_init_wait(void);

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
void nfs_prereq_init(const char *program_name, const char *host_name,
		     int debug_level, const char *log_path, bool dump_trace);

/**
 * nfs_set_param_from_conf:
 * Load parameters from config file.
 */
int nfs_set_param_from_conf(config_file_t config_struct,
			    nfs_start_info_t *p_start_info,
			    struct config_error_type *err_type);

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

/**
 * check for useable malloc implementation
 */
static inline void nfs_check_malloc(void)
{
	/* Check malloc(0) - Ganesha assumes malloc(0) returns non-NULL pointer.
	 * Note we use malloc and calloc directly here and not gsh_malloc and
	 * gsh_calloc because we don't want those functions to abort(), we
	 * want to log a descriptive message.
	 */
	void *p;

	p = malloc(0);
	if (p == NULL)
		LogFatal(COMPONENT_MAIN,
			 "Ganesha assumes malloc(0) returns a non-NULL pointer.");
	free(p);
	p = calloc(0, 0);
	if (p == NULL)
		LogFatal(COMPONENT_MAIN,
			 "Ganesha assumes calloc(0, 0) returns a non-NULL pointer.");
	free(p);
}

/* in nfs_rpc_dispatcher_thread.c */

int free_nfs_request(request_data_t *);

/* in nfs_worker_thread.c */

enum xprt_stat nfs_rpc_valid_NFS(struct svc_req *);
enum xprt_stat nfs_rpc_valid_NLM(struct svc_req *);
enum xprt_stat nfs_rpc_valid_MNT(struct svc_req *);
enum xprt_stat nfs_rpc_valid_RQUOTA(struct svc_req *);

#endif				/* !NFS_INIT_H */
