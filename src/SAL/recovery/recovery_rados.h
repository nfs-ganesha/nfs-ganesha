/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates.
 * Author: Jeff Layton <jlayton@redhat.com>
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
 */
#ifndef _RECOVERY_RADOS_H
#define _RECOVERY_RADOS_H
#include <stdio.h>
#include "gsh_refstr.h"

/* A rados key is a decimal string of a uint64_t value which has at most 20
 * digits plus 1 for NUL
 */
#define RADOS_KEY_MAX_LEN	21
#define RADOS_VAL_MAX_LEN	PATH_MAX

extern rados_t			rados_recov_cluster;
extern rados_ioctx_t		rados_recov_io_ctx;
extern struct gsh_refstr	*rados_recov_oid;
extern struct gsh_refstr	*rados_recov_old_oid;

struct rados_kv_parameter {
	/** Connection to ceph cluster */
	char *ceph_conf;
	/** User ID to ceph cluster */
	char *userid;
	/** Pool for client info */
	char *pool;
	/** Namespace for objects within the pool **/
	char *namespace;
	/** rados_cluster grace database OID */
	char *grace_oid;
	/** rados_cluster node_id */
	char *nodeid;
};
extern struct rados_kv_parameter rados_kv_param;

/* Callback for rados_kv_traverse */
struct pop_args {
	add_clid_entry_hook add_clid_entry;
	add_rfh_entry_hook add_rfh_entry;
	bool old;
	bool takeover;
};
typedef void (*pop_clid_entry_t)(char *, char *, size_t,  struct pop_args *);

int rados_kv_connect(rados_ioctx_t *io_ctx, const char *userid,
			const char *conf, const char *pool, const char *ns);
void rados_kv_shutdown(void);
int rados_kv_put(char *key, char *val, char *object);
int rados_kv_get(char *key, char *val, char *object);
void rados_kv_add_clid(nfs_client_id_t *clientid);
void rados_kv_rm_clid(nfs_client_id_t *clientid);
void rados_kv_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle);

/**
 * Convert a clientid into a rados key.
 *
 * @param(in)     clientid  The clientid to conevrt into a key
 * @param(in/out) key       The string buffer to put the key in
 * @param(in)     size      Buffer size - expected to be RADOS_KEY_MAX_LEN
 */
static inline
void rados_kv_create_key(nfs_client_id_t *clientid, char *key, size_t size)
{
	assert(size == RADOS_KEY_MAX_LEN);

	/* Can't overrun RADOS_KEY_MAX_LEN and shouldn't return EOVERFLOW or
	 * EINVAL
	 */
	(void) snprintf(key, size, "%lu", (uint64_t)clientid->cid_clientid);
}

char *rados_kv_create_val(nfs_client_id_t *clientid, size_t *len);
int rados_kv_traverse(pop_clid_entry_t callback, struct pop_args *args,
			const char *object);
void rados_kv_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle);

void rados_ng_pop_clid_entry(char *key, char *val, size_t val_len,
			     struct pop_args *pop_args);
int rados_kv_get_nodeid(char **pnodeid);
#endif	/* _RECOVERY_RADOS_H */
