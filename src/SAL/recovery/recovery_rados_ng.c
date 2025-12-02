// SPDX-License-Identifier: LGPL-3.0-or-later
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
 *
 * recovery_rados_ng: a "safe by design" recovery backing store
 *
 * At startup, create a global write op, and set it up to clear out all of
 * the old keys. We then will spool up new client creation (and removals) to
 * that transaction during the grace period.
 *
 * When lifting the grace period, synchronously commit the transaction
 * to the kvstore. After that point, all client creation and removal is done
 * synchronously to the kvstore.
 *
 * This allows for better resilience when the server crashes during the grace
 * period. No changes are made to the backing store until the grace period
 * has been lifted.
 */

#include "config.h"
#include <netdb.h>
#include <rados/librados.h>
#include <urcu-bp.h>
#include "log.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "recovery_rados.h"
#include "FSAL/fsal_init.h"

static rados_write_op_t grace_op;
static pthread_mutex_t grace_op_lock;
bool takeover;
bool no_cleanup;
char object_takeover[NI_MAXHOST];

static int rados_ng_put(char *key, char *val, char *object)
{
	int ret;
	char *keys[1];
	char *vals[1];
	size_t lens[1];
	rados_write_op_t write_op = NULL;
	bool in_grace;

	keys[0] = key;
	vals[0] = val;
	lens[0] = strlen(val);

	/* When there is an active grace_op, spool up the changes to it */
	PTHREAD_MUTEX_lock(&grace_op_lock);
	in_grace = grace_op;
	write_op = grace_op;
	if (!write_op)
		write_op = rados_create_write_op();
	rados_write_op_omap_set(write_op, (const char *const *)keys,
				(const char *const *)vals, lens, 1);
	PTHREAD_MUTEX_unlock(&grace_op_lock);
	if (in_grace)
		return 0;

	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object, NULL,
				     0);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to put kv ret=%d, key=%s, val=%s", ret, key,
			 val);
	}
	rados_release_write_op(write_op);

	return ret;
}

static int rados_ng_del(char *key, char *object)
{
	int ret;
	char *keys[1];
	rados_write_op_t write_op;
	bool in_grace;

	keys[0] = key;

	PTHREAD_MUTEX_lock(&grace_op_lock);
	in_grace = grace_op;
	write_op = in_grace ? grace_op : rados_create_write_op();
	rados_write_op_omap_rm_keys(write_op, (const char *const *)keys, 1);
	PTHREAD_MUTEX_unlock(&grace_op_lock);

	if (in_grace)
		return 0;

	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object, NULL,
				     0);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to del kv ret=%d, key=%s",
			 ret, key);
	}
	rados_release_write_op(write_op);

	return ret;
}

/* Cleanup on shutdown */
void rados_ng_cleanup(void)
{
	PTHREAD_MUTEX_destroy(&grace_op_lock);
}

MODULE_FINI void rados_ng_unload(void)
{
	rados_ng_cleanup();
}

static int rados_ng_init(void)
{
	int ret;
	size_t len;
	struct gsh_refstr *recov_oid;
	rados_write_op_t op;

	PTHREAD_MUTEX_init(&grace_op_lock, NULL);

	ret = set_nodeid();
	if (ret < 0) {
		LogCrit(COMPONENT_RECOVERY, "Failed to set nodeid : %d", ret);
		return ret;
	}

	len = strlen(nodeid) + 6 + 1;
	recov_oid = gsh_refstr_alloc(len);
	gsh_refstr_get(recov_oid);

	/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
	(void)snprintf(recov_oid->gr_val, len, "%s_recov", nodeid);
	rcu_set_pointer(&rados_recov_oid, recov_oid);

	ret = rados_kv_connect(&rados_recov_io_ctx, rados_kv_param.userid,
			       rados_kv_param.ceph_conf, rados_kv_param.pool,
			       rados_kv_param.namespace);
	if (ret < 0) {
		gsh_refstr_put(recov_oid);
		LogCrit(COMPONENT_RECOVERY, "Failed to connect to cluster: %d",
			ret);
		return ret;
	}

	op = rados_create_write_op();
	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, rados_recov_io_ctx, recov_oid->gr_val,
				     NULL, 0);
	gsh_refstr_put(recov_oid);
	if (ret < 0 && ret != -EEXIST) {
		LogCrit(COMPONENT_RECOVERY, "Failed to create object");
		rados_release_write_op(op);
		rados_kv_shutdown();
		return ret;
	}
	rados_release_write_op(op);

	LogEvent(COMPONENT_RECOVERY,
		 "rados-ng recovery backend initialization complete");

	return 0;
}

static void rados_ng_add_clid(nfs_client_id_t *clientid)
{
	struct gsh_refstr *recov_oid;
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	int ret;

	rados_kv_create_key(clientid, ckey, sizeof(ckey));
	cval = nfs4_create_clid_name(clientid, NULL);

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "adding %s :: %s",
		    ckey, cval);
	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_ng_put(ckey, cval, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to add clid %lu",
			 clientid->cid_clientid);
		gsh_free(cval);
	} else {
		clientid->cid_recov_tag = cval;
	}
}

static void rados_ng_rm_clid(nfs_client_id_t *clientid)
{
	char ckey[RADOS_KEY_MAX_LEN];
	int ret;
	struct gsh_refstr *recov_oid;

	rados_kv_create_key(clientid, ckey, sizeof(ckey));

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "removing %s",
		    ckey);
	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_ng_del(ckey, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to del clid %lu",
			 clientid->cid_clientid);
		return;
	}

	free(clientid->cid_recov_tag);
	clientid->cid_recov_tag = NULL;
}

void rados_ng_pop_clid_entry(char *key, char *val, size_t val_len,
			     struct pop_args *pop_args)
{
	char *dupval, *cl_name;
	char *rfh_names, *rfh_name;
	clid_entry_t *clid_ent;

	/* extract clid records */
	dupval = gsh_malloc(val_len + 1);
	memcpy(dupval, val, val_len);
	dupval[val_len] = '\0';
	cl_name = strtok(dupval, "#");
	if (!cl_name)
		cl_name = dupval;
	clid_ent = nfs4_add_clid_entry(cl_name, true);

	rfh_names = strtok(NULL, "#");
	rfh_name = strtok(rfh_names, "#");
	while (rfh_name) {
		nfs4_add_rfh_entry(clid_ent, rfh_name);
		rfh_name = strtok(NULL, "#");
	}
	gsh_free(dupval);
}

static void rados_ng_read_recov_clids_recover(void)
{
	int ret;
	struct gsh_refstr *recov_oid;
	struct pop_args args = { false, false };

	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_traverse(rados_ng_pop_clid_entry, &args,
				recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to recover, processing old entries");
		return;
	}
}

static void rados_ng_read_recov_clids_takeover(nfs_grace_start_t *gsp)
{
	int ret;
	struct pop_args args = { false, true };

	if (!gsp) {
		takeover = false;
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Recovery object in use %s",
			    rados_recov_oid->gr_val);
		rados_ng_read_recov_clids_recover();
		return;
	}

	/* Its takeover activity */
	switch (gsp->event) {
	case EVENT_TAKE_IP:
		ret = snprintf(object_takeover, sizeof(object_takeover),
			       "%s_recov", gsp->ipaddr);
		if (unlikely(ret >= sizeof(object_takeover))) {
			LogCrit(COMPONENT_RECOVERY,
				"object_takeover too long %s_recov",
				gsp->ipaddr);
			no_cleanup = true;
			return;
		} else if (unlikely(ret < 0)) {
			LogCrit(COMPONENT_RECOVERY, "snprintf %d error %s (%d)",
				ret, strerror(errno), errno);
			no_cleanup = true;
			return;
		}
		break;
	case EVENT_TAKE_NODEID:
		ret = snprintf(object_takeover, sizeof(object_takeover),
			       "node%d_recov", gsp->nodeid);
		if (unlikely(ret >= sizeof(object_takeover))) {
			LogCrit(COMPONENT_RECOVERY,
				"object_takeover too long node%d_recov",
				gsp->nodeid);
			no_cleanup = true;
			return;
		} else if (unlikely(ret < 0)) {
			LogCrit(COMPONENT_RECOVERY, "snprintf %d error %s (%d)",
				ret, strerror(errno), errno);
			no_cleanup = true;
			return;
		}
		break;
	default:
		LogWarn(COMPONENT_RECOVERY,
			"Recovery unknown/unsupported event %d", gsp->event);
		no_cleanup = true;
		return;
	}

	takeover = true;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "Recovery object in use %s", object_takeover);

	ret = rados_kv_traverse(rados_ng_pop_clid_entry, &args,
				object_takeover);
	if (ret < 0) {
		if (gsp->event == EVENT_TAKE_IP)
			LogWarn(COMPONENT_RECOVERY,
				"Failed to takeover IP - %s", gsp->ipaddr);
		else
			LogWarn(COMPONENT_RECOVERY,
				"Failed to takeover node - %d", gsp->nodeid);
	}
}

static void rados_ng_cleanup_old(void)
{
	int ret;
	struct gsh_refstr *recov_oid;

	if (no_cleanup) {
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Recovery object was not set properly, no cleanup");
		no_cleanup = false;
	}
	PTHREAD_MUTEX_lock(&grace_op_lock);

	/* Create new grace_op to spool changes until grace period is done */
	grace_op = rados_create_write_op();
	rados_write_op_omap_clear(grace_op);

	/* Commit pregrace transaction */
	if (!takeover) {
		rcu_read_lock();
		recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
		rcu_read_unlock();
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Recovery object to be cleaned %s",
			    recov_oid->gr_val);
		ret = rados_write_op_operate(grace_op, rados_recov_io_ctx,
					     recov_oid->gr_val, NULL, 0);
		gsh_refstr_put(recov_oid);
	} else {
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Recovery object to be cleaned %s",
			    object_takeover);
		ret = rados_write_op_operate(grace_op, rados_recov_io_ctx,
					     object_takeover, NULL, 0);
	}
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to commit grace period transactions: %s",
			 strerror(ret));
	}
	rados_release_write_op(grace_op);
	grace_op = NULL;
	PTHREAD_MUTEX_unlock(&grace_op_lock);
}

struct nfs4_recovery_backend rados_ng_backend = {
	.recovery_init = rados_ng_init,
	.recovery_shutdown = rados_kv_shutdown,
	.end_grace = rados_ng_cleanup_old,
	.recovery_read_clids = rados_ng_read_recov_clids_takeover,
	.add_clid = rados_ng_add_clid,
	.rm_clid = rados_ng_rm_clid,
	.add_revoke_fh = rados_kv_add_revoke_fh,
	.get_nodeid = rados_kv_get_nodeid,
};

void rados_ng_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &rados_ng_backend;
}
