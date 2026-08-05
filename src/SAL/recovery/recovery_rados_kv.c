// SPDX-License-Identifier: LGPL-3.0-or-later
/*
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

#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"
#include "netdb.h"
#include <rados/librados.h>
#include <rados_grace.h>
#include "recovery_rados.h"
#include <urcu-bp.h>

#define MAX_ITEMS 1024 /* relaxed */

static rados_t clnt;
rados_ioctx_t rados_recov_io_ctx;
struct gsh_refstr *rados_recov_oid;
struct gsh_refstr *rados_recov_old_oid;
int node_id = -1;
char *nodeid;

struct rados_kv_parameter rados_kv_param;

static struct config_item rados_kv_params[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL, rados_kv_parameter,
		       ceph_conf),
	CONF_ITEM_STR("userid", 1, MAXPATHLEN, NULL, rados_kv_parameter,
		      userid),
	CONF_ITEM_STR("pool", 1, MAXPATHLEN, DEFAULT_RADOS_GRACE_POOL,
		      rados_kv_parameter, pool),
	CONF_ITEM_STR("namespace", 1, NI_MAXHOST, NULL, rados_kv_parameter,
		      namespace),
	CONF_ITEM_STR("grace_oid", 1, NI_MAXHOST, DEFAULT_RADOS_GRACE_OID,
		      rados_kv_parameter, grace_oid),
	CONF_ITEM_STR("nodeid", 1, NI_MAXHOST, NULL, rados_kv_parameter,
		      nodeid),
	CONFIG_EOL
};

struct config_block rados_kv_param_blk = {
	.dbus_interface_name = "org.ganesha.nfsd.config.rados_kv",
	.blk_desc.name = "RADOS_KV",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE, /* too risky to have more */
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = rados_kv_params,
	.blk_desc.u.blk.commit = noop_conf_commit,
	.mem_comp = MEM_COMP_CONFIG
};

int rados_kv_put(char *key, char *val, char *object)
{
	int ret;
	char *keys[1];
	char *vals[1];
	size_t lens[1];
	rados_write_op_t write_op;

	keys[0] = key;
	vals[0] = val;
	lens[0] = strlen(val);
	write_op = rados_create_write_op();

	rados_write_op_omap_set(write_op, (const char *const *)keys,
				(const char *const *)vals, lens, 1);
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

int rados_kv_get(char *key, char *val, char *object)
{
	int ret;
	char *keys[1];
	char *key_out = NULL;
	char *val_out = NULL;
	size_t val_len_out = 0;
	rados_omap_iter_t iter_vals;
	rados_read_op_t read_op;

	keys[0] = key;
	read_op = rados_create_read_op();

	rados_read_op_omap_get_vals_by_keys(read_op, (const char *const *)keys,
					    1, &iter_vals, NULL);
	ret = rados_read_op_operate(read_op, rados_recov_io_ctx, object, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to get kv ret=%d, key=%s",
			 ret, key);
		goto out;
	}

	ret = rados_omap_get_next(iter_vals, &key_out, &val_out, &val_len_out);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to get kv ret=%d, key=%s",
			 ret, key);
		goto out;
	}

	/* All internal so buffer length is known to be ok */
	memcpy(val, val_out, val_len_out + 1);
	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "%s: key=%s val=%s",
		    __func__, key, val);
	rados_omap_get_end(iter_vals);
out:
	rados_release_read_op(read_op);
	return ret;
}

static int rados_kv_del(char *key, char *object)
{
	int ret;
	char *keys[1];
	rados_write_op_t write_op;

	keys[0] = key;
	write_op = rados_create_write_op();

	rados_write_op_omap_rm_keys(write_op, (const char *const *)keys, 1);
	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object, NULL,
				     0);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to del kv ret=%d, key=%s",
			 ret, key);
	}
	rados_release_write_op(write_op);

	return ret;
}

int rados_kv_traverse(pop_clid_entry_t callback, struct pop_args *args,
		      const char *object)
{
	int ret;
	char *key_out = NULL;
	char *val_out = NULL;
	size_t val_len_out = 0;
	bool pmore = false;
	char *start = "";
	rados_omap_iter_t iter_vals;
	rados_read_op_t read_op;

again:
	read_op = rados_create_read_op();
	rados_read_op_omap_get_vals2(read_op, start, "", MAX_ITEMS, &iter_vals,
				     (unsigned char *)&pmore, NULL);
	ret = rados_read_op_operate(read_op, rados_recov_io_ctx, object, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to lst kv ret=%d", ret);
		goto out;
	}

	while (true) {
		rados_omap_get_next(iter_vals, &key_out, &val_out,
				    &val_len_out);
		if (val_len_out == 0 && key_out == NULL && val_out == NULL)
			break;
		start = key_out;
		callback(key_out, val_out, val_len_out, args);
	}
	rados_omap_get_end(iter_vals);

	/* more items, next round */
	if (pmore) {
		rados_release_read_op(read_op);
		goto again;
	}

out:
	rados_release_read_op(read_op);
	return ret;
}

static void rados_kv_append_val_rdfh(char *val, char *rdfh, int rdfh_len)
{
	char rdfhstr[NAME_MAX];
	int ret;
	size_t buflen;

	/* Convert nfs_fh4_val into base64 encoded string */
	ret = base64url_encode(rdfh, rdfh_len, rdfhstr, NAME_MAX);
	assert(ret != -1);

	buflen = RADOS_VAL_MAX_LEN - (strlen(val) + 1);
	strncat(val, "#", buflen);
	buflen--;
	strncat(val, rdfhstr, buflen);
}

int rados_load_config_from_parse(config_file_t parse_tree,
				 struct config_error_type *err_type)
{
	(void)load_config_from_parse(parse_tree, &rados_kv_param_blk,
				     &rados_kv_param, true, err_type);
	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_RECOVERY,
			"Error while parsing RadosKV specific configuration");
		return -1;
	}

	return 0;
}

int rados_kv_connect(rados_ioctx_t *io_ctx, const char *userid,
		     const char *conf, const char *pool, const char *ns)
{
	int ret;

	ret = rados_create(&clnt, userid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to create: %d", ret);
		return ret;
	}

	ret = rados_conf_read_file(clnt, conf);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to read conf: %d", ret);
		rados_shutdown(clnt);
		clnt = NULL;
		return ret;
	}

	ret = rados_connect(clnt);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to connect: %d", ret);
		rados_shutdown(clnt);
		clnt = NULL;
		return ret;
	}

	ret = rados_pool_create(clnt, pool);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_RECOVERY, "Failed to create pool: %d", ret);
		rados_shutdown(clnt);
		clnt = NULL;
		return ret;
	}

	ret = rados_ioctx_create(clnt, pool, io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to create ioctx");
		rados_shutdown(clnt);
		clnt = NULL;
		return ret;
	}

	rados_ioctx_set_namespace(*io_ctx, ns);
	return ret;
}

void rados_kv_shutdown(void)
{
	struct gsh_refstr *recov_oid;

	if (rados_recov_io_ctx) {
		rados_ioctx_destroy(rados_recov_io_ctx);
		rados_recov_io_ctx = NULL;
	}
	if (clnt) {
		rados_shutdown(clnt);
		clnt = NULL;
	}
	recov_oid = rcu_xchg_pointer(&rados_recov_oid, NULL);
	synchronize_rcu();
	if (recov_oid)
		gsh_refstr_put(recov_oid);
}

/* Set the nodeid -
 *   if g_nodeid is set, its numeric, prepend "node"
 *   if nodeid is set in ganesha.conf
 *      check if its numeric value, if numeric prepend "node"
 *      if non-numeric then use it as is
 *   else use hostname as nodeid */
int set_nodeid(void)
{
	int ret;
	bool use_host_name = false;
	bool prepend = false;
	long maxlen = sysconf(_SC_HOST_NAME_MAX);
	if (maxlen < 0) {
		LogWarn(COMPONENT_RECOVERY,
			"sysconf(_SC_HOST_NAME_MAX) failed, falling back to MAXNAMLEN");
		maxlen = MAXNAMLEN;
	}

	nodeid = gsh_malloc(maxlen + 1, MEM_COMP_RECOVERY);

	/* check nodeid override with "I" option */
	if (g_nodeid >= 0) {
		node_id = g_nodeid;
		prepend = true;
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Global nodeid, \"node\" will be prepended");
	} else if (!rados_kv_param.nodeid) {
		/* Need to use hostname */
		use_host_name = true;
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "No nodeid, hostname will be used");
	} else {
		/* Process the provided nodeid */
		char *endptr;

		errno = 0;
		node_id = strtol(rados_kv_param.nodeid, &endptr, 10);
		if (errno) {
			LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				    "conversion failed, %d, using nodeid as is",
				    errno);
		} else {
			if (*endptr != '\0') {
				node_id = -1;
				LogDebugAlt(
					COMPONENT_CLIENTID, COMPONENT_RECOVERY,
					"Not a numeric string, using nodeid as is");
			}
			if (node_id >= 0) {
				prepend = true;
				LogDebugAlt(
					COMPONENT_CLIENTID, COMPONENT_RECOVERY,
					"Numeric nodeid, \"node\" will be prepended");
			}
		}
	}
	if (prepend) {
		/* numeric nodeid, prepend "node" */
		ret = snprintf(nodeid, maxlen, "node%d", node_id);
		if (unlikely(ret > maxlen)) {
			LogCrit(COMPONENT_RECOVERY, "node%d too long", node_id);
			return -ENAMETOOLONG;
		} else if (unlikely(ret < 0)) {
			ret = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Unexpected return from snprintf %d error %s",
				ret, strerror(ret));
			return -ret;
		}
	} else if (use_host_name) {
		/* no nodeid set in ganesha.conf, use hostname */
		ret = gethostname(nodeid, maxlen);
		if (ret) {
			ret = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Failed to gethostname: %s (%d)", strerror(ret),
				ret);
			return -ret;
		}
	} else {
		/* non-numeric nodeid set in ganesha.conf, use it as is */
		ret = snprintf(nodeid, maxlen, "%s", rados_kv_param.nodeid);
		if (unlikely(ret > maxlen)) {
			LogCrit(COMPONENT_RECOVERY, "%s too long",
				rados_kv_param.nodeid);
			return -ENAMETOOLONG;
		} else if (unlikely(ret < 0)) {
			ret = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Unexpected return from snprintf %d error %s",
				ret, strerror(ret));
			return -ret;
		}
	}
	LogEvent(COMPONENT_RECOVERY, "Nodeid : %s ", nodeid);
	return 0;
}

int rados_kv_init(void)
{
	int ret;
	size_t len, nodeid_len;
	struct gsh_refstr *recov_oid = NULL, *old_oid = NULL;

	set_nodeid();
	nodeid_len = strlen(nodeid);
	len = nodeid_len + 6 + 1;
	recov_oid = gsh_refstr_alloc(len, MEM_COMP_RECOVERY);
	gsh_refstr_get(recov_oid);

	/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
	(void)snprintf(recov_oid->gr_val, len, "%s_recov", nodeid);
	rcu_set_pointer(&rados_recov_oid, recov_oid);

	len = nodeid_len + 4 + 1;
	old_oid = gsh_refstr_alloc(len, MEM_COMP_RECOVERY);
	gsh_refstr_get(old_oid);

	/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
	(void)snprintf(old_oid->gr_val, len, "%s_old", nodeid);
	rcu_set_pointer(&rados_recov_old_oid, old_oid);

	ret = rados_kv_connect(&rados_recov_io_ctx, rados_kv_param.userid,
			       rados_kv_param.ceph_conf, rados_kv_param.pool,
			       rados_kv_param.namespace);
	if (ret < 0) {
		LogCrit(COMPONENT_RECOVERY,
			"Failed to connect to rados kv store: %d", ret);
		goto out;
	}

	rados_write_op_t op = rados_create_write_op();

	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, rados_recov_io_ctx, old_oid->gr_val,
				     NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogCrit(COMPONENT_RECOVERY,
			"Failed to create recovery (rados) object");
		rados_release_write_op(op);
		rados_kv_shutdown();
		goto out;
	}
	rados_release_write_op(op);

	op = rados_create_write_op();
	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, rados_recov_io_ctx, recov_oid->gr_val,
				     NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogCrit(COMPONENT_RECOVERY,
			"Failed to create recovery (rados) object");
		rados_release_write_op(op);
		rados_kv_shutdown();
		goto out;
	}
	rados_release_write_op(op);

	LogEvent(COMPONENT_RECOVERY,
		 "rados-kv recovery backend initialization complete");
	ret = 0;
out:
	gsh_refstr_put(recov_oid);
	gsh_refstr_put(old_oid);
	return ret;
}

void rados_kv_add_clid_impl(nfs_client_id_t *clientid, char *recov_obj)
{
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	int ret;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "Recovery object in use : %s", recov_obj);
	rados_kv_create_key(clientid, ckey, sizeof(ckey));
	cval = nfs4_create_clid_name(clientid, NULL);
	ret = rados_kv_put(ckey, cval, recov_obj);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to add clid %lu",
			 clientid->cid_clientid);
		gsh_free(cval, MEM_COMP_RECOVERY);
	} else {
		clientid->cid_recov_tag = cval;
	}
}

void rados_kv_add_clid(nfs_client_id_t *clientid)
{
	struct gsh_refstr *recov_oid;

	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	rados_kv_add_clid_impl(clientid, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
}

void rados_kv_rm_clid_impl(nfs_client_id_t *clientid, char *recov_obj)
{
	char ckey[RADOS_KEY_MAX_LEN];
	int ret;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "Recovery object in use : %s", recov_obj);
	rados_kv_create_key(clientid, ckey, sizeof(ckey));
	ret = rados_kv_del(ckey, recov_obj);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to del clid %lu",
			 clientid->cid_clientid);
		return;
	}
	gsh_free(clientid->cid_recov_tag, MEM_COMP_RECOVERY);
	clientid->cid_recov_tag = NULL;
}

void rados_kv_rm_clid(nfs_client_id_t *clientid)
{
	struct gsh_refstr *recov_oid;

	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	rados_kv_rm_clid_impl(clientid, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
}

static void rados_kv_pop_clid_entry(char *key, char *val, size_t val_len,
				    struct pop_args *pop_args)
{
	int ret;
	char *dupval;
	char *cl_name, *rfh_names, *rfh_name;
	struct gsh_refstr *old_oid;
	clid_entry_t *clid_ent;
	bool old = pop_args->old;
	bool takeover = pop_args->takeover;

	/* extract clid records */
	dupval = gsh_malloc(val_len + 1, MEM_COMP_RECOVERY);
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

	rcu_read_lock();
	old_oid = gsh_refstr_get(rcu_dereference(rados_recov_old_oid));
	rcu_read_unlock();
	if (!old) {
		ret = rados_kv_put(key, dupval, old_oid->gr_val);
		if (ret < 0) {
			LogEvent(COMPONENT_RECOVERY, "Failed to move %s", key);
		}
	}
	gsh_free(dupval, MEM_COMP_RECOVERY);

	if (!takeover) {
		if (old) {
			ret = rados_kv_del(key, old_oid->gr_val);
		} else {
			struct gsh_refstr *recov_oid;

			rcu_read_lock();
			recov_oid = gsh_refstr_get(
				rcu_dereference(rados_recov_oid));
			rcu_read_unlock();
			ret = rados_kv_del(key, recov_oid->gr_val);
			gsh_refstr_put(recov_oid);
		}
		if (ret < 0) {
			LogEvent(COMPONENT_RECOVERY, "Failed to del %s", key);
		}
	}
	gsh_refstr_put(old_oid);
}

static void rados_kv_read_recov_clids_recover(void)
{
	int ret;
	struct gsh_refstr *recov_oid, *old_oid;
	struct pop_args args = { true, false };

	rcu_read_lock();
	old_oid = gsh_refstr_get(rcu_dereference(rados_recov_old_oid));
	rcu_read_unlock();
	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				old_oid->gr_val);
	gsh_refstr_put(old_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to recover, processing old entries");
		return;
	}

	args.old = false;
	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to recover, processing recov entries");
	}
}

void rados_kv_read_recov_clids_takeover(nfs_grace_start_t *gsp)
{
	int ret;
	char object_takeover[NI_MAXHOST];
	struct pop_args args = { false, true };

	if (!gsp) {
		rados_kv_read_recov_clids_recover();
		return;
	}

	ret = snprintf(object_takeover, sizeof(object_takeover), "%s_recov",
		       gsp->ipaddr);

	if (unlikely(ret >= sizeof(object_takeover))) {
		LogCrit(COMPONENT_RECOVERY, "object_takeover too long %s_recov",
			gsp->ipaddr);
	} else if (unlikely(ret < 0)) {
		LogCrit(COMPONENT_RECOVERY,
			"Unexpected return from snprintf %d error %s (%d)", ret,
			strerror(errno), errno);
	}

	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				object_takeover);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to takeover");
	}
}

void rados_kv_cleanup_old(void)
{
	int ret;
	struct gsh_refstr *old_oid;
	rados_write_op_t write_op = rados_create_write_op();

	rcu_read_lock();
	old_oid = gsh_refstr_get(rcu_dereference(rados_recov_old_oid));
	rcu_read_unlock();

	rados_write_op_omap_clear(write_op);
	ret = rados_write_op_operate(write_op, rados_recov_io_ctx,
				     old_oid->gr_val, NULL, 0);
	if (ret < 0)
		LogEvent(COMPONENT_RECOVERY, "Failed to cleanup old");
	rados_release_write_op(write_op);
	gsh_refstr_put(old_oid);
}

void rados_kv_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	int ret;
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	struct gsh_refstr *recov_oid;

	cval = gsh_malloc(RADOS_VAL_MAX_LEN, MEM_COMP_RECOVERY);

	rados_kv_create_key(delr_clid, ckey, sizeof(ckey));
	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_get(ckey, cval, recov_oid->gr_val);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to get %s", ckey);
		goto out;
	}

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "%s: key=%s val=%s",
		    __func__, ckey, cval);
	rados_kv_append_val_rdfh(cval, delr_handle->nfs_fh4_val,
				 delr_handle->nfs_fh4_len);

	ret = rados_kv_put(ckey, cval, recov_oid->gr_val);
	if (ret < 0) {
		LogEvent(COMPONENT_RECOVERY, "Failed to add rdfh for clid %lu",
			 delr_clid->cid_clientid);
	}
out:
	gsh_refstr_put(recov_oid);
	gsh_free(cval, MEM_COMP_RECOVERY);
}

int rados_kv_get_nodeid(char **pnodeid)
{
	*pnodeid = nodeid;
	return 0;
}

struct nfs4_recovery_backend rados_kv_backend = {
	.recovery_init = rados_kv_init,
	.recovery_shutdown = rados_kv_shutdown,
	.end_grace = rados_kv_cleanup_old,
	.recovery_read_clids = rados_kv_read_recov_clids_takeover,
	.add_clid = rados_kv_add_clid,
	.rm_clid = rados_kv_rm_clid,
	.add_revoke_fh = rados_kv_add_revoke_fh,
	.get_nodeid = rados_kv_get_nodeid,
};

void rados_kv_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &rados_kv_backend;
}
