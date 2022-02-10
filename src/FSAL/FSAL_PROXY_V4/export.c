/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/* Export-related methods */

#include "config.h"

#include "fsal.h"
#include "fsal_convert.h"
#include <pthread.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "proxyv4_fsal_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

#ifdef _USE_GSSRPC
static struct config_item_list sec_types[] = {
	CONFIG_LIST_TOK("krb5", RPCSEC_GSS_SVC_NONE),
	CONFIG_LIST_TOK("krb5i", RPCSEC_GSS_SVC_INTEGRITY),
	CONFIG_LIST_TOK("krb5p", RPCSEC_GSS_SVC_PRIVACY),
	CONFIG_LIST_EOL
};
#endif

static struct config_item proxyv4_export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_UI32("Retry_SleepTime", 0, 60, 10,
		       proxyv4_client_params, retry_sleeptime),
	CONF_MAND_IP_ADDR("Srv_Addr", "127.0.0.1",
			  proxyv4_client_params, srv_addr),
	CONF_ITEM_UI32("NFS_Service", 0, UINT32_MAX, 100003,
		       proxyv4_client_params, srv_prognum),
	CONF_ITEM_UI64("NFS_SendSize", 512 + SEND_RECV_HEADER_SPACE,
		       FSAL_MAXIOSIZE,
		       DEFAULT_MAX_WRITE_READ + SEND_RECV_HEADER_SPACE,
		       proxyv4_client_params, srv_sendsize),
	CONF_ITEM_UI64("NFS_RecvSize", 512 + SEND_RECV_HEADER_SPACE,
		       FSAL_MAXIOSIZE,
		       DEFAULT_MAX_WRITE_READ + SEND_RECV_HEADER_SPACE,
		       proxyv4_client_params, srv_recvsize),
	CONF_ITEM_UI16("NFS_Port", 0, UINT16_MAX, 2049,
		       proxyv4_client_params, srv_port),
	CONF_ITEM_BOOL("Use_Privileged_Client_Port", true,
		       proxyv4_client_params, use_privileged_client_port),
	CONF_ITEM_UI32("RPC_Client_Timeout", 1, 60*4, 60,
		       proxyv4_client_params, srv_timeout),
#ifdef _USE_GSSRPC
	CONF_ITEM_STR("Remote_PrincipalName", 0, MAXNAMLEN, NULL,
		      proxyv4_client_params, remote_principal),
	CONF_ITEM_STR("KeytabPath", 0, MAXPATHLEN, "/etc/krb5.keytab"
		      proxyv4_client_params, keytab),
	CONF_ITEM_UI32("Credential_LifeTime", 0, 86400*2, 86400,
		       proxyv4_client_params, cred_lifetime),
	CONF_ITEM_TOKEN("Sec_Type", RPCSEC_GSS_SVC_NONE, sec_types,
			proxyv4_client_params, sec_type),
	CONF_ITEM_BOOL("Active_krb5", false,
		       proxyv4_client_params, active_krb5),
#endif
#ifdef PROXYV4_HANDLE_MAPPING
	CONF_ITEM_BOOL("Enable_Handle_Mapping", false,
		       proxyv4_client_params, enable_handle_mapping),
	CONF_ITEM_STR("HandleMap_DB_Dir", 0, MAXPATHLEN,
		      "/var/ganesha/handlemap",
		      proxyv4_client_params, hdlmap.databases_directory),
	CONF_ITEM_STR("HandleMap_Tmp_Dir", 0, MAXPATHLEN,
		      "/var/ganesha/tmp",
		      proxyv4_client_params, hdlmap.temp_directory),
	CONF_ITEM_UI32("HandleMap_DB_Count", 1, 16, 8,
		       proxyv4_client_params, hdlmap.database_count),
	CONF_ITEM_UI32("HandleMap_HashTable_Size", 1, 127, 103,
		       proxyv4_client_params, hdlmap.hashtable_size),
#endif
	CONFIG_EOL
};

static int remote_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	struct proxyv4_client_params *pcp =
		(struct proxyv4_client_params *) link_mem;
	struct proxyv4_fsal_module *proxyv4_module;

	proxyv4_module = container_of(op_ctx->fsal_module,
				      struct proxyv4_fsal_module,
				      module);

	if (proxyv4_module->module.fs_info.maxwrite + SEND_RECV_HEADER_SPACE >
	    pcp->srv_sendsize ||
	    proxyv4_module->module.fs_info.maxread +
	    SEND_RECV_HEADER_SPACE >
	    pcp->srv_recvsize) {
		LogCrit(COMPONENT_CONFIG,
			"FSAL_PROXY_V4 CONF : maxwrite/maxread + header > Max_SendSize/Max_RecvSize");
		err_type->invalid = true;
		return 1;
	}

	/* Other verifications/parameter checking to be added here */

	return 0;
}

static struct config_block proxyv4_export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.proxyv4-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = proxyv4_export_params,
	.blk_desc.u.blk.commit = remote_commit
};

static void proxyv4_release(struct fsal_export *exp_hdl)
{
	struct proxyv4_export *proxyv4_exp =
		container_of(exp_hdl, struct proxyv4_export, exp);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	proxyv4_close_thread(proxyv4_exp);

	gsh_free(proxyv4_exp);
}

static attrmask_t proxyv4_get_supported_attrs(struct fsal_export *exp_hdl)
{
	return fsal_supported_attrs(&exp_hdl->fsal->fs_info);
}

void proxyv4_export_ops_init(struct export_ops *ops)
{
	ops->release = proxyv4_release;
	ops->lookup_path = proxyv4_lookup_path;
	ops->wire_to_host = proxyv4_wire_to_host;
	ops->create_handle = proxyv4_create_handle;
	ops->get_fs_dynamic_info = proxyv4_get_dynamic_info;
	ops->fs_supported_attrs = proxyv4_get_supported_attrs;
	ops->alloc_state = proxyv4_alloc_state;
	ops->free_state = proxyv4_free_state;
}

fsal_status_t proxyv4_create_export(struct fsal_module *fsal_hdl,
				    void *parse_node,
				    struct config_error_type *err_type,
				    const struct fsal_up_vector *up_ops)
{
	fsal_status_t fsal_status = {0, 0};
	struct proxyv4_export *exp = gsh_calloc(1, sizeof(*exp));
	int rc;

	/* export initial values */
	proxyv4_export_init(exp);

	/* general export init */
	fsal_export_init(&exp->exp);

	/* proxyv4 export option parsing */
	rc = load_config_from_node(parse_node,
				   &proxyv4_export_param,
				   &exp->info,
				   true,
				   err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Incorrect or missing parameters for export %s",
			CTX_FULLPATH(op_ctx));
		fsal_status = fsalstat(ERR_FSAL_INVAL, rc);
		goto err_free;
	}

	/* proxyv4 export init */
	proxyv4_export_ops_init(&exp->exp.exp_ops);
	exp->exp.fsal = fsal_hdl;
	exp->exp.up_ops = up_ops;
	op_ctx->fsal_export = &exp->exp;
	rc = fsal_attach_export(fsal_hdl, &exp->exp.exports);
	if (rc != 0) {
		fsal_status = posix2fsal_status(rc);
		goto err_free;
	}

#ifdef PROXYV4_HANDLE_MAPPING
	rc = HandleMap_Init(&exp->info.hdlmap);
	if (rc < 0) {
		fsal_status = fsalstat(ERR_FSAL_INVAL, -rc);
		goto err_cleanup;
	}
#endif

	/* create export client/server connection */
	rc = proxyv4_init_rpc(exp);
	if (rc) {
		fsal_status = fsalstat(ERR_FSAL_FAULT, rc);
		goto err_cleanup;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 err_cleanup:
	fsal_detach_export(fsal_hdl, &exp->exp.exports);
 err_free:
	free_export_ops(&exp->exp);
	gsh_free(exp);
	return fsal_status;
}
