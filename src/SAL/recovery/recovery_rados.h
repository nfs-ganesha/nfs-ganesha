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

#define RADOS_KEY_MAX_LEN	NAME_MAX
#define RADOS_VAL_MAX_LEN	PATH_MAX

extern rados_t		rados_recov_cluster;
extern rados_ioctx_t	rados_recov_io_ctx;
extern char		rados_recov_oid[NI_MAXHOST];

struct rados_kv_parameter {
	/** Connection to ceph cluster */
	char *ceph_conf;
	/** User ID to ceph cluster */
	char *userid;
	/** Pool for client info */
	char *pool;
};
extern struct rados_kv_parameter rados_kv_param;

typedef void (*pop_clid_entry_t)(char *, char*, add_clid_entry_hook,
				 add_rfh_entry_hook, bool old, bool takeover);
typedef struct pop_args {
	add_clid_entry_hook add_clid_entry;
	add_rfh_entry_hook add_rfh_entry;
	bool old;
	bool takeover;
} *pop_args_t;

int rados_kv_connect(rados_ioctx_t *io_ctx, const char *userid,
			const char *conf, const char *pool);
void rados_kv_shutdown(void);
int rados_kv_get(char *key, char *val, char *object);
void rados_kv_create_key(nfs_client_id_t *clientid, char *key);
void rados_kv_create_val(nfs_client_id_t *clientid, char *val);
int rados_kv_traverse(pop_clid_entry_t pop_func, pop_args_t pop_args,
			const char *object);
void rados_kv_append_val_rdfh(char *val, char *rdfh, int rdfh_len);
#endif	/* _RECOVERY_RADOS_H */
