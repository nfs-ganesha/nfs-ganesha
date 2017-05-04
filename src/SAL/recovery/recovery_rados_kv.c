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

#define KEY_MAX_LEN		NAME_MAX
#define VAL_MAX_LEN		PATH_MAX

#define MAX_ITEMS		1024		/* relaxed */

static rados_t cluster;
static rados_ioctx_t io_ctx;
static bool clustered;
static char myhostname[NI_MAXHOST];
static char myobject_old[NI_MAXHOST];
static char myobject_recov[NI_MAXHOST];
static char object_takeover[NI_MAXHOST];

static struct rados_kv_parameter {
	/** Connection to ceph cluster */
	char *ceph_conf;
	/** User ID to ceph cluster */
	char *userid;
	/** Pool for client info */
	char *pool;
} rados_kv_param;

static struct config_item rados_kv_params[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
		       rados_kv_parameter, ceph_conf),
	CONF_ITEM_STR("userid", 1, MAXPATHLEN, NULL,
		       rados_kv_parameter, userid),
	CONF_ITEM_STR("pool", 1, MAXPATHLEN, NULL,
		       rados_kv_parameter, pool),
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

static void create_key(nfs_client_id_t *clientid, char *key)
{
	snprintf(key, KEY_MAX_LEN, "%lu", (uint64_t)clientid->cid_clientid);
}

static void create_val(nfs_client_id_t *clientid, char *val)
{
	char *src = clientid->cid_client_record->cr_client_val;
	int src_len = clientid->cid_client_record->cr_client_val_len;
	const char *str_client_addr = "(unknown)";
	char cidstr[PATH_MAX] = { 0, };
	struct display_buffer dspbuf = {sizeof(cidstr), cidstr, cidstr};
	char cidstr_len[20];
	int total_len;
	int ret;

	/* get the caller's IP addr */
	if (clientid->gsh_client != NULL)
		str_client_addr = clientid->gsh_client->hostaddr_str;

	ret = convert_opaque_val(&dspbuf, src, src_len, PATH_MAX);
	assert(ret > 0);

	snprintf(cidstr_len, sizeof(cidstr_len), "%zd", strlen(cidstr));
	total_len = strlen(cidstr) + strlen(str_client_addr) + 5 +
		    strlen(cidstr_len);

	/* hold both long form clientid and IP */
	snprintf(val, total_len, "%s-(%s:%s)",
		 str_client_addr, cidstr_len, cidstr);

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
		 clientid->cid_recov_tag);
}

static int rados_kv_put(char *key, char *val, char *object)
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
	ret = rados_write_op_operate(write_op, io_ctx, object, NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to put kv ret=%d, key=%s, val=%s",
			 ret, key, val);
	}
	rados_release_write_op(write_op);

	return ret;
}

static int rados_kv_get(char *key, char **val, size_t *val_len,
			char *object)
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
	ret = rados_read_op_operate(read_op, io_ctx, object, 0);
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
	rados_omap_get_end(iter_vals);

	val_out[val_len_out] = '\0';
	*val = val_out;
	*val_len = val_len_out;
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
	ret = rados_write_op_operate(write_op, io_ctx, object, NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to del kv ret=%d, key=%s",
			 ret, key);
	}
	rados_release_write_op(write_op);

	return ret;
}

typedef void (*pop_clid_entry_t)(char *, char*, add_clid_entry_hook,
				 add_rfh_entry_hook, bool, bool);
typedef struct pop_args {
	add_clid_entry_hook add_clid_entry;
	add_rfh_entry_hook add_rfh_entry;
	bool old;
	bool takeover;
} *pop_args_t;

static int rados_kv_traverse(pop_clid_entry_t pop_func, pop_args_t pop_args,
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
	ret = rados_read_op_operate(read_op, io_ctx, object, 0);
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
		pop_func(key_out, val_out, pop_args->add_clid_entry,
			 pop_args->add_rfh_entry, pop_args->old,
			 pop_args->takeover);
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

static void append_val_rdfh(char *val, char *rdfh, int rdfh_len)
{
	char rdfhstr[NAME_MAX];
	int rdfhstr_len;
	int ret;

	/* Convert nfs_fh4_val into base64 encoded string */
	ret = base64url_encode(rdfh, rdfh_len, rdfhstr, NAME_MAX);
	assert(ret != -1);
	rdfhstr_len = strlen(rdfhstr);

	strncat(val, "#", 1);
	strncat(val, rdfhstr, rdfhstr_len);
	val[rdfhstr_len + 1] = '\0';
}

int rados_kv_set_param_from_conf(config_file_t parse_tree,
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

void rados_kv_init(void)
{
	int ret;

	clustered = nfs_param.core_param.clustered;
	if (!clustered) {
		ret = gethostname(myhostname, sizeof(myhostname));
		if (ret) {
			LogEvent(COMPONENT_CLIENTID, "Failed to gethostname");
			return;
		}
		snprintf(myobject_old, NI_MAXHOST, "%s_old", myhostname);
		snprintf(myobject_recov, NI_MAXHOST, "%s_recov", myhostname);
	} else {
		snprintf(myobject_old, NI_MAXHOST, "node%d_old", g_nodeid);
		snprintf(myobject_recov, NI_MAXHOST, "node%d_recov", g_nodeid);
	}
	ret = rados_create(&cluster, rados_kv_param.userid);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to rados create");
		return;
	}
	ret = rados_conf_read_file(cluster, rados_kv_param.ceph_conf);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to read ceph_conf");
		rados_shutdown(cluster);
		return;
	}
	ret = rados_connect(cluster);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to connect to cluster");
		rados_shutdown(cluster);
		return;
	}
	ret = rados_pool_create(cluster, rados_kv_param.pool);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create pool");
		rados_shutdown(cluster);
		return;
	}
	ret = rados_ioctx_create(cluster, rados_kv_param.pool, &io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create ioctx");
		rados_shutdown(cluster);
		return;
	}

	rados_write_op_t op = rados_create_write_op();

	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, io_ctx, myobject_old, NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create object");
		rados_release_write_op(op);
		rados_shutdown(cluster);
		return;
	}
	rados_release_write_op(op);

	op = rados_create_write_op();
	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
	ret = rados_write_op_operate(op, io_ctx, myobject_recov, NULL, 0);
	if (ret < 0 && ret != -EEXIST) {
		LogEvent(COMPONENT_CLIENTID, "Failed to create object");
		rados_release_write_op(op);
		rados_shutdown(cluster);
		return;
	}
	rados_release_write_op(op);

	LogEvent(COMPONENT_CLIENTID, "Rados kv store init done");
}

void rados_kv_add_clid(nfs_client_id_t *clientid)
{
	char ckey[KEY_MAX_LEN];
	char *cval;
	int ret;

	cval = gsh_malloc(VAL_MAX_LEN);

	create_key(clientid, ckey);
	create_val(clientid, cval);

	ret = rados_kv_put(ckey, cval, myobject_recov);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to add clid %lu",
			 clientid->cid_clientid);
		goto out;
	}

	clientid->cid_recov_tag = gsh_malloc(strlen(cval) + 1);
	strncpy(clientid->cid_recov_tag, cval, strlen(cval) + 1);
out:
	gsh_free(cval);
}

void rados_kv_rm_clid(nfs_client_id_t *clientid)
{
	char ckey[KEY_MAX_LEN];
	int ret;

	create_key(clientid, ckey);

	ret = rados_kv_del(ckey, myobject_recov);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to del clid %lu",
			 clientid->cid_clientid);
		return;
	}

	free(clientid->cid_recov_tag);
	clientid->cid_recov_tag = NULL;
}

static void rados_kv_pop_clid_entry(char *key,
				    char *val,
				    add_clid_entry_hook add_clid_entry,
				    add_rfh_entry_hook add_rfh_entry,
				    bool old,
				    bool takeover)
{
	int ret;
	char *dupval;
	char *cl_name, *rfh_names, *rfh_name;
	clid_entry_t *clid_ent;

	/* extract clid records */
	dupval = gsh_strdup(val);
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
	gsh_free(dupval);

	if (!old) {
		ret = rados_kv_put(key, val, myobject_old);
		if (ret < 0) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to move %s", key);
		}
	}

	if (!takeover) {
		if (old) {
			ret = rados_kv_del(key, myobject_old);
		} else {
			ret = rados_kv_del(key, myobject_recov);
		}
		if (ret < 0) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to del %s", key);
		}
	}
}

void rados_kv_read_recov_clids_recover(add_clid_entry_hook add_clid_entry,
				       add_rfh_entry_hook add_rfh_entry)
{
	int ret;
	struct pop_args args = {
		.add_clid_entry = add_clid_entry,
		.add_rfh_entry = add_rfh_entry,
		.old = true,
		.takeover = false,
	};

	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args, myobject_old);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to recover, processing old entries");
		return;
	}

	args.old = false;
	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args, myobject_recov);
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
	struct pop_args args = {
		.add_clid_entry = add_clid_entry,
		.add_rfh_entry = add_rfh_entry,
		.old = false,
		.takeover = true,
	};

	snprintf(object_takeover, NI_MAXHOST, "%s_recov", gsp->ipaddr);
	ret = rados_kv_traverse(rados_kv_pop_clid_entry, &args,
				object_takeover);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to takeover");
	}
}

void rados_kv_cleanup_old(void)
{
	int ret;
	rados_write_op_t write_op = rados_create_write_op();

	rados_write_op_omap_clear(write_op);
	ret = rados_write_op_operate(write_op, io_ctx, myobject_old, NULL, 0);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to clearup old");
	}
	rados_release_write_op(write_op);
}

void rados_kv_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	int ret;
	char ckey[KEY_MAX_LEN];
	char *cval;
	char *val_out;
	size_t val_out_len;

	cval = gsh_malloc(VAL_MAX_LEN);

	create_key(delr_clid, ckey);
	ret = rados_kv_get(ckey, &val_out, &val_out_len, myobject_recov);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "Failed to get %s", ckey);
		goto out;
	}

	strncpy(cval, val_out, val_out_len);
	append_val_rdfh(cval, delr_handle->nfs_fh4_val,
			      delr_handle->nfs_fh4_len);

	ret = rados_kv_put(ckey, cval, myobject_recov);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to add rdfh for clid %lu",
			 delr_clid->cid_clientid);
	}

out:
	gsh_free(cval);
}

bool rados_kv_check_clid(nfs_client_id_t *clientid, clid_entry_t *clid_ent)
{
	LogDebug(COMPONENT_CLIENTID, "compare %s to %s",
		 clientid->cid_recov_tag, clid_ent->cl_name);

	if (!clientid->cid_recov_tag)
		return false;
	if (!strncmp(clientid->cid_recov_tag,
		     clid_ent->cl_name, PATH_MAX))
		return true;
	return false;
}

struct nfs4_recovery_backend rados_kv_backend = {
	.recovery_init = rados_kv_init,
	.recovery_cleanup = rados_kv_cleanup_old,
	.recovery_read_clids_recover = rados_kv_read_recov_clids_recover,
	.recovery_read_clids_takeover = rados_kv_read_recov_clids_takeover,
	.add_clid = rados_kv_add_clid,
	.rm_clid = rados_kv_rm_clid,
	.add_revoke_fh = rados_kv_add_revoke_fh,
	.check_clid = rados_kv_check_clid,
};

void rados_kv_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &rados_kv_backend;
}
