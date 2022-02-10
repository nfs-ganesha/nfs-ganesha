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

#define MAX_ITEMS		1024		/* relaxed */

static rados_t clnt;
rados_ioctx_t rados_recov_io_ctx;
struct gsh_refstr *rados_recov_oid;
struct gsh_refstr *rados_recov_old_oid;

struct rados_kv_parameter rados_kv_param;

static struct config_item rados_kv_params[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
		       rados_kv_parameter, ceph_conf),
	CONF_ITEM_STR("userid", 1, MAXPATHLEN, NULL,
		       rados_kv_parameter, userid),
	CONF_ITEM_STR("pool", 1, MAXPATHLEN, DEFAULT_RADOS_GRACE_POOL,
		       rados_kv_parameter, pool),
	CONF_ITEM_STR("namespace", 1, NI_MAXHOST, NULL,
			rados_kv_parameter, namespace),
	CONF_ITEM_STR("grace_oid", 1, NI_MAXHOST, DEFAULT_RADOS_GRACE_OID,
		       rados_kv_parameter, grace_oid),
	CONF_ITEM_STR("nodeid", 1, NI_MAXHOST, NULL, rados_kv_parameter,
			nodeid),
	CONFIG_EOL
};

static void *rados_kv_param_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL)
		return &rados_kv_param;
	else
		return NULL;
}

struct config_block rados_kv_param_blk = {
	.dbus_interface_name = "org.ganesha.nfsd.config.rados_kv",
	.blk_desc.name = "RADOS_KV",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = rados_kv_param_init,
	.blk_desc.u.blk.params = rados_kv_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static int convert_opaque_val(struct display_buffer *dspbuf,
			      void *value,
			      int len,
			      int max)
{
	unsigned int i = 0;
	int b_left = display_start(dspbuf);
	int cpy = len;

	if (b_left <= 0)
		return 0;

	/* Check that the length is ok
	 * If the value is empty, display EMPTY value. */
	if (len <= 0 || len > max)
		return 0;

	/* If the value is NULL, display NULL value. */
	if (value == NULL)
		return 0;

	/* Determine if the value is entirely printable characters, */
	/* and it contains no slash character (reserved for filename) */
	for (i = 0; i < len; i++)
		if ((!isprint(((char *)value)[i])) ||
		    (((char *)value)[i] == '/'))
			break;

	if (i == len) {
		/* Entirely printable character, so we will just copy the
		 * characters into the buffer (to the extent there is room
		 * for them).
		 */
		b_left = display_len_cat(dspbuf, value, cpy);
	} else {
		b_left = display_opaque_bytes(dspbuf, value, cpy);
	}

	if (b_left <= 0)
		return 0;

	return b_left;
}

char *rados_kv_create_val(nfs_client_id_t *clientid, size_t *size)
{
	char *src = clientid->cid_client_record->cr_client_val;
	int src_len = clientid->cid_client_record->cr_client_val_len;
	const char *str_client_addr = "(unknown)";
	char cidstr[PATH_MAX] = { 0, }, *val;
	struct display_buffer dspbuf = {sizeof(cidstr), cidstr, cidstr};
	char cidstr_lenx[5];
	int total_len, cidstr_len, cidstr_lenx_len, str_client_addr_len;
	int ret;
	size_t lsize;

	/* get the caller's IP addr */
	if (clientid->gsh_client != NULL)
		str_client_addr = clientid->gsh_client->hostaddr_str;

	str_client_addr_len = strlen(str_client_addr);

	ret = convert_opaque_val(&dspbuf, src, src_len, sizeof(cidstr));
	assert(ret > 0);

	cidstr_len = display_buffer_len(&dspbuf);

	cidstr_lenx_len = snprintf(cidstr_lenx, sizeof(cidstr_lenx), "%d",
				   cidstr_len);

	if (unlikely(cidstr_lenx_len >= sizeof(cidstr_lenx) ||
		     cidstr_lenx_len < 0)) {
		/* cidrstr can at most be PATH_MAX or 1024, so at most
		 * 4 characters plus NUL are necessary, so we won't
		 * overrun, nor can we get a -1 with EOVERFLOW or EINVAL
		 */
		LogFatal(COMPONENT_CLIENTID,
			 "snprintf returned unexpected %d", cidstr_lenx_len);
	}

	lsize = str_client_addr_len + 2 + cidstr_lenx_len + 1 + cidstr_len + 2;

	/* hold both long form clientid and IP */
	val = gsh_malloc(lsize);
	memcpy(val, str_client_addr, str_client_addr_len);
	total_len = str_client_addr_len;
	memcpy(val + total_len, "-(", 2);
	total_len += 2;
	memcpy(val + total_len, cidstr_lenx, cidstr_lenx_len);
	total_len += cidstr_lenx_len;
	val[total_len] = ':';
	total_len += 1;
	memcpy(val + total_len, cidstr, cidstr_len);
	total_len += cidstr_len;
	memcpy(val + total_len, ")", 2);

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]", val);

	if (size)
		*size = lsize;

	return val;
}

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

	rados_write_op_omap_set(write_op, (const char * const*)keys,
					  (const char * const*)vals, lens, 1);
	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object,
				     NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to put kv ret=%d, key=%s, val=%s",
			 ret, key, val);
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

	rados_read_op_omap_get_vals_by_keys(read_op, (const char * const*)keys,
					    1, &iter_vals, NULL);
	ret = rados_read_op_operate(read_op, rados_recov_io_ctx, object, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to get kv ret=%d, key=%s",
			 ret, key);
		goto out;
	}

	ret = rados_omap_get_next(iter_vals, &key_out, &val_out, &val_len_out);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to get kv ret=%d, key=%s",
			 ret, key);
		goto out;
	}

	/* All internal so buffer length is known to be ok */
	memcpy(val, val_out, val_len_out+1);
	LogDebug(COMPONENT_CLIENTID, "%s: key=%s val=%s", __func__, key, val);
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

	rados_write_op_omap_rm_keys(write_op, (const char * const*)keys, 1);
	ret = rados_write_op_operate(write_op, rados_recov_io_ctx, object,
				     NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to del kv ret=%d, key=%s",
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
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to lst kv ret=%d", ret);
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
	(void) load_config_from_parse(parse_tree,
				      &rados_kv_param_blk,
				      NULL,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_INIT,
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
		LogEvent(COMPONENT_CLIENTID, "Failed to create: %d", ret);
		return ret;
	}

	ret = rados_conf_read_file(clnt, conf);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to read conf: %d", ret);
		rados_shutdown(clnt);
		return ret;
	}

	ret = rados_connect(clnt);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to connect: %d", ret);
		rados_shutdown(clnt);
		return ret;
	}

	ret = rados_pool_create(clnt, pool);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create pool: %d", ret);
		rados_shutdown(clnt);
		return ret;
	}

	ret = rados_ioctx_create(clnt, pool, io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create ioctx");
		rados_shutdown(clnt);
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

int rados_kv_init(void)
{
	int ret;
	size_t len, host_len;
	struct gsh_refstr *recov_oid = NULL, *old_oid = NULL;
	char host[NI_MAXHOST];

	if (nfs_param.core_param.clustered) {
		ret = snprintf(host, sizeof(host), "node%d", g_nodeid);

		if (unlikely(ret >= sizeof(host))) {
			LogCrit(COMPONENT_CLIENTID,
				"node%d too long", g_nodeid);
			return -ENAMETOOLONG;
		} else if (unlikely(ret < 0)) {
			ret = errno;
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				ret, strerror(ret), ret);
			return -ret;
		}
	} else {
		ret = gethostname(host, sizeof(host));

		if (ret) {
			ret = errno;
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to gethostname: %s (%d)",
				 strerror(ret), ret);
			return -ret;
		}
	}

	host_len = strlen(host);
	len = host_len + 6 + 1;
	recov_oid = gsh_refstr_alloc(len);
	gsh_refstr_get(recov_oid);

	/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
	(void) snprintf(recov_oid->gr_val, len, "%s_recov", host);
	rcu_set_pointer(&rados_recov_oid, recov_oid);

	len = host_len + 4 + 1;
	old_oid = gsh_refstr_alloc(len);
	gsh_refstr_get(old_oid);

	/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
	(void) snprintf(old_oid->gr_val, len, "%s_old", host);
	rcu_set_pointer(&rados_recov_old_oid, old_oid);

	ret = rados_kv_connect(&rados_recov_io_ctx, rados_kv_param.userid,
			rados_kv_param.ceph_conf, rados_kv_param.pool,
			rados_kv_param.namespace);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			"Failed to connect to cluster: %d", ret);
		goto out;
	}

	rados_write_op_t op = rados_create_write_op();

	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, rados_recov_io_ctx,
				     old_oid->gr_val, NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create object");
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
		LogEvent(COMPONENT_CLIENTID, "Failed to create object");
		rados_release_write_op(op);
		rados_kv_shutdown();
		goto out;
	}
	rados_release_write_op(op);

	LogEvent(COMPONENT_CLIENTID, "Rados kv store init done");
	ret = 0;
out:
	gsh_refstr_put(recov_oid);
	gsh_refstr_put(old_oid);
	return ret;
}

void rados_kv_add_clid(nfs_client_id_t *clientid)
{
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	struct gsh_refstr *recov_oid;
	int ret;

	rados_kv_create_key(clientid, ckey, sizeof(ckey));
	cval = rados_kv_create_val(clientid, NULL);

	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_put(ckey, cval, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to add clid %lu",
			 clientid->cid_clientid);
		gsh_free(cval);
	} else {
		clientid->cid_recov_tag = cval;
	}
}

void rados_kv_rm_clid(nfs_client_id_t *clientid)
{
	char ckey[RADOS_KEY_MAX_LEN];
	struct gsh_refstr *recov_oid;
	int ret;

	rados_kv_create_key(clientid, ckey, sizeof(ckey));

	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_del(ckey, recov_oid->gr_val);
	gsh_refstr_put(recov_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to del clid %lu",
			 clientid->cid_clientid);
		return;
	}

	free(clientid->cid_recov_tag);
	clientid->cid_recov_tag = NULL;
}

static void rados_kv_pop_clid_entry(char *key, char *val, size_t val_len,
				    struct pop_args *pop_args)
{
	int ret;
	char *dupval;
	char *cl_name, *rfh_names, *rfh_name;
	struct gsh_refstr *old_oid;
	clid_entry_t *clid_ent;
	add_clid_entry_hook add_clid_entry = pop_args->add_clid_entry;
	add_rfh_entry_hook add_rfh_entry = pop_args->add_rfh_entry;
	bool old = pop_args->old;
	bool takeover = pop_args->takeover;

	/* extract clid records */
	dupval = gsh_malloc(val_len +  1);
	memcpy(dupval, val, val_len);
	dupval[val_len] = '\0';

	cl_name = strtok(dupval, "#");
	if (!cl_name)
		cl_name = dupval;
	clid_ent = add_clid_entry(cl_name);

	rfh_names = strtok(NULL, "#");
	rfh_name = strtok(rfh_names, "#");
	while (rfh_name) {
		add_rfh_entry(clid_ent, rfh_name);
		rfh_name = strtok(NULL, "#");
	}

	rcu_read_lock();
	old_oid = gsh_refstr_get(rcu_dereference(rados_recov_old_oid));
	rcu_read_unlock();
	if (!old) {
		ret = rados_kv_put(key, dupval, old_oid->gr_val);
		if (ret < 0) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to move %s", key);
		}
	}
	gsh_free(dupval);

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
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to del %s", key);
		}
	}
	gsh_refstr_put(old_oid);
}

static void
rados_kv_read_recov_clids_recover(add_clid_entry_hook add_clid_entry,
				       add_rfh_entry_hook add_rfh_entry)
{
	int ret;
	struct gsh_refstr *recov_oid, *old_oid;
	struct pop_args args = {
		.add_clid_entry = add_clid_entry,
		.add_rfh_entry = add_rfh_entry,
		.old = true,
		.takeover = false,
	};

	rcu_read_lock();
	old_oid = gsh_refstr_get(rcu_dereference(rados_recov_old_oid));
	rcu_read_unlock();
	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				old_oid->gr_val);
	gsh_refstr_put(old_oid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
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
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to recover, processing recov entries");
	}
}

void rados_kv_read_recov_clids_takeover(nfs_grace_start_t *gsp,
					add_clid_entry_hook add_clid_entry,
					add_rfh_entry_hook add_rfh_entry)
{
	int ret;
	char object_takeover[NI_MAXHOST];
	struct pop_args args = {
		.add_clid_entry = add_clid_entry,
		.add_rfh_entry = add_rfh_entry,
		.old = false,
		.takeover = true,
	};

	if (!gsp) {
		rados_kv_read_recov_clids_recover(add_clid_entry,
						  add_rfh_entry);
		return;
	}

	ret = snprintf(object_takeover, sizeof(object_takeover), "%s_recov",
		       gsp->ipaddr);

	if (unlikely(ret >= sizeof(object_takeover))) {
		LogCrit(COMPONENT_CLIENTID,
			"object_takeover too long %s_recov", gsp->ipaddr);
	} else if (unlikely(ret < 0)) {
		LogCrit(COMPONENT_CLIENTID,
			"Unexpected return from snprintf %d error %s (%d)",
			ret, strerror(errno), errno);
	}

	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				object_takeover);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to takeover");
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
		LogEvent(COMPONENT_CLIENTID, "Failed to cleanup old");
	rados_release_write_op(write_op);
	gsh_refstr_put(old_oid);
}

void rados_kv_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	int ret;
	char ckey[RADOS_KEY_MAX_LEN];
	char *cval;
	struct gsh_refstr *recov_oid;

	cval = gsh_malloc(RADOS_VAL_MAX_LEN);

	rados_kv_create_key(delr_clid, ckey, sizeof(ckey));
	rcu_read_lock();
	recov_oid = gsh_refstr_get(rcu_dereference(rados_recov_oid));
	rcu_read_unlock();
	ret = rados_kv_get(ckey, cval, recov_oid->gr_val);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to get %s", ckey);
		goto out;
	}

	LogDebug(COMPONENT_CLIENTID, "%s: key=%s val=%s", __func__,
			ckey, cval);
	rados_kv_append_val_rdfh(cval, delr_handle->nfs_fh4_val,
				      delr_handle->nfs_fh4_len);

	ret = rados_kv_put(ckey, cval, recov_oid->gr_val);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to add rdfh for clid %lu",
			 delr_clid->cid_clientid);
	}
out:
	gsh_refstr_put(recov_oid);
	gsh_free(cval);
}

int rados_kv_get_nodeid(char **pnodeid)
{
	/* return the nodeid if we have one */
	if (rados_kv_param.nodeid)
		*pnodeid = gsh_strdup(rados_kv_param.nodeid);
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
