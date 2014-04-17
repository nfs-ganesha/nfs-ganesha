/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * file    mnt_Mnt.c
 * brief   MOUNTPROC_MNT for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_EXPORT in V1, V3.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "client_mgr.h"
#include "export_mgr.h"

/**
 * @brief The Mount proc mount function, for all versions.
 *
 * The MOUNT proc proc function, for all versions.
 *
 * @param[in]  arg     The export path to be mounted
 * @param[in]  export  The export list
 * @param[in]  req_ctx  ignored
 * @param[in]  worker  ignored
 * @param[in]  req     ignored
 * @param[out] res     Result structure.
 *
 */

int mnt_Mnt(nfs_arg_t *arg, exportlist_t *export,
	    struct req_op_context *req_ctx, nfs_worker_data_t *worker,
	    struct svc_req *req, nfs_res_t *res)
{

	exportlist_t *p_current_item = NULL;
	struct gsh_export *exp = NULL;
	struct fsal_obj_handle *pfsal_handle = NULL;
	bool release_handle = false;
	struct fsal_export *exp_hdl;
	int auth_flavor[NB_AUTH_FLAVOR];
	int index_auth = 0;
	int i = 0;
	char dumpfh[1024];
	export_perms_t export_perms;
	int retval = NFS_REQ_OK;
	nfs_fh3 *fh3 = (nfs_fh3 *) &res->res_mnt3.mountres3_u.mountinfo.fhandle;

	LogDebug(COMPONENT_NFSPROTO,
		 "REQUEST PROCESSING: Calling mnt_Mnt path=%s", arg->arg_mnt);

	/* Paranoid command to clean the result struct. */
	memset(res, 0, sizeof(nfs_res_t));

	/* Quick escape if an unsupported MOUNT version */
	if (req->rq_vers != MOUNT_V3) {
		res->res_mnt1.status = NFSERR_ACCES;
		goto out;
	}

	if (arg->arg_mnt == NULL) {
		LogCrit(COMPONENT_NFSPROTO,
			"NULL path passed as Mount argument !!!");
		retval = NFS_REQ_DROP;
		goto out;
	}

	/* If the path ends with a '/', get rid of it */
	/** @todo: should it be a while()?? */
	if (arg->arg_mnt[strlen(arg->arg_mnt) - 1] == '/')
		arg->arg_mnt[strlen(arg->arg_mnt) - 1] = '\0';

	/*  Find the export for the dirname (using as well Path or Tag) */
	if (arg->arg_mnt[0] == '/')
		exp = get_gsh_export_by_path(arg->arg_mnt, false);
	else
		exp = get_gsh_export_by_tag(arg->arg_mnt);

	if (exp == NULL) {
		/* No export found, return ACCESS error. */
		LogEvent(COMPONENT_NFSPROTO,
			 "MOUNT: Export entry for %s not found", arg->arg_mnt);

		/* entry not found. */
		/* @todo : not MNT3ERR_NOENT => ok */
		res->res_mnt3.fhs_status = MNT3ERR_ACCES;
		goto out;
	}

	p_current_item = &exp->export;

	/* set the export in the context */
	req_ctx->export = exp;
	req_ctx->fsal_export = req_ctx->export->export.export_hdl;

	/* Check access based on client. Don't bother checking TCP/UDP as some
	 * clients use UDP for MOUNT even when they will use TCP for NFS.
	 */
	nfs_export_check_access(req_ctx->caller_addr, p_current_item,
				&export_perms);

	if ((export_perms.options & EXPORT_OPTION_NFSV3) == 0) {
		LogInfo(COMPONENT_NFSPROTO,
			"MOUNT: Export entry %s does not support NFS v3 for client %s",
			p_current_item->fullpath,
			req_ctx->client->hostaddr_str);
		res->res_mnt3.fhs_status = MNT3ERR_ACCES;
		goto out;
	}

	/* retrieve the associated NFS handle */
	if (arg->arg_mnt[0] != '/' || !strcmp(arg->arg_mnt,
					      p_current_item->fullpath)) {
		PTHREAD_RWLOCK_rdlock(&exp->lock);
		if (p_current_item->exp_root_cache_inode != NULL) {
			pfsal_handle =
			    p_current_item->exp_root_cache_inode->obj_handle;
			PTHREAD_RWLOCK_unlock(&exp->lock);
		} else {
			PTHREAD_RWLOCK_unlock(&exp->lock);
			res->res_mnt3.fhs_status = MNT3ERR_ACCES;
			goto out;
		}
	} else {
		exp_hdl = p_current_item->export_hdl;
		LogEvent(COMPONENT_NFSPROTO,
			 "MOUNT: Performance warning: Export entry is not cached");

		if (FSAL_IS_ERROR(exp_hdl->ops->lookup_path(exp_hdl, req_ctx,
							    arg->arg_mnt,
							    &pfsal_handle))) {
			res->res_mnt3.fhs_status = MNT3ERR_ACCES;
			goto out;

		release_handle = true;
		}
	}

	/* convert the fsal_handle to a file handle */
	/** @todo:
	 * The mountinfo.fhandle definition is an overlay on/of nfs_fh3.
	 * redefine and eliminate one or the other.
	 */
	res->res_mnt3.fhs_status = nfs3_AllocateFH(fh3);

	if (res->res_mnt3.fhs_status == MNT3_OK) {
		if (!nfs3_FSALToFhandle(fh3, pfsal_handle, exp)) {
			res->res_mnt3.fhs_status = MNT3ERR_INVAL;
		} else {
			if (isDebug(COMPONENT_NFSPROTO))
				sprint_fhandle3(dumpfh, fh3);
			res->res_mnt3.fhs_status = MNT3_OK;
		}
	}

	if (release_handle)
		pfsal_handle->ops->release(pfsal_handle);

	/* Return the supported authentication flavor in V3 based
	 * on the client's export permissions.
	 */
	if (export_perms.options & EXPORT_OPTION_AUTH_NONE)
		auth_flavor[index_auth++] = AUTH_NONE;
	if (export_perms.options & EXPORT_OPTION_AUTH_UNIX)
		auth_flavor[index_auth++] = AUTH_UNIX;
#ifdef _HAVE_GSSAPI
	if (nfs_param.krb5_param.active_krb5 == TRUE) {
		if (export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE)
			auth_flavor[index_auth++] = MNT_RPC_GSS_NONE;
		if (export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG)
			auth_flavor[index_auth++] = MNT_RPC_GSS_INTEGRITY;
		if (export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
			auth_flavor[index_auth++] = MNT_RPC_GSS_PRIVACY;
	}
#endif

	LogDebug(COMPONENT_NFSPROTO,
		 "MOUNT: Entry supports %d different flavours handle=%s for client %s",
		 index_auth, dumpfh, req_ctx->client->hostaddr_str);

	mountres3_ok * const RES_MOUNTINFO =
	    &res->res_mnt3.mountres3_u.mountinfo;

	RES_MOUNTINFO->auth_flavors.auth_flavors_val =
		gsh_calloc(index_auth, sizeof(int));

	if (RES_MOUNTINFO->auth_flavors.auth_flavors_val == NULL) {
		retval = NFS_REQ_DROP;
		goto out;
	}

	RES_MOUNTINFO->auth_flavors.auth_flavors_len = index_auth;
	for (i = 0; i < index_auth; i++)
		RES_MOUNTINFO->auth_flavors.auth_flavors_val[i] =
		    auth_flavor[i];

 out:
	if (exp != NULL) {
		req_ctx->export = NULL;
		req_ctx->fsal_export = NULL;
		put_gsh_export(exp);
	}
	return retval;

}				/* mnt_Mnt */

/**
 * mnt_Mnt_Free: Frees the result structure allocated for mnt_Mnt.
 *
 * Frees the result structure allocated for mnt_Mnt.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */

void mnt1_Mnt_Free(nfs_res_t *res)
{
	return;
}

void mnt3_Mnt_Free(nfs_res_t *res)
{
	if (res->res_mnt3.fhs_status == MNT3_OK) {
		gsh_free(res->res_mnt3.mountres3_u.mountinfo.auth_flavors.
			 auth_flavors_val);
		gsh_free(res->res_mnt3.mountres3_u.mountinfo.fhandle.
			 fhandle3_val);
	}
	return;
}
