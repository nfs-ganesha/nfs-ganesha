// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * @file  nfs_export_list.c
 * @brief Routines for managing the export list.
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>		/* for having isalnum */
#include <stdlib.h>		/* for having atoi */
#include <dirent.h>		/* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include "log.h"
#include "gsh_rpc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "idmapper.h"
#include "export_mgr.h"
#include "uid2grp.h"
#include "client_mgr.h"

/* Export permissions for root op context */
uint32_t root_op_export_options = EXPORT_OPTION_ROOT |
				  EXPORT_OPTION_ACCESS_MASK |
				  EXPORT_OPTION_AUTH_TYPES |
				  EXPORT_OPTION_PROTOCOLS |
				  EXPORT_OPTION_TRANSPORTS;

uint32_t root_op_export_set = EXPORT_OPTION_SQUASH_TYPES |
			      EXPORT_OPTION_ANON_UID_SET |
			      EXPORT_OPTION_ANON_GID_SET |
			      EXPORT_OPTION_ACCESS_MASK |
			      EXPORT_OPTION_AUTH_TYPES |
			      EXPORT_OPTION_PROTOCOLS |
			      EXPORT_OPTION_TRANSPORTS;

void squash_setattr(struct fsal_attrlist *attr)
{
	if (attr->valid_mask & ATTR_OWNER &&
	    op_ctx->export_perms.anonymous_uid != 0) {
		if (op_ctx->export_perms.options & EXPORT_OPTION_ALL_ANONYMOUS)
			attr->owner = op_ctx->export_perms.anonymous_uid;
		else if (((op_ctx->export_perms.options &
			   EXPORT_OPTION_ROOT_SQUASH) ||
			  (op_ctx->export_perms.options &
			   EXPORT_OPTION_ROOT_ID_SQUASH))
			 && (attr->owner == 0)
			 && ((op_ctx->cred_flags & UID_SQUASHED) != 0))
			attr->owner = op_ctx->export_perms.anonymous_uid;
	}

	if (attr->valid_mask & ATTR_GROUP &&
	    op_ctx->export_perms.anonymous_gid != 0) {
		/* If all squashed, then always squash the owner_group.
		 *
		 * If root squashed, then squash owner_group if
		 * caller_gid has been squashed or one of the caller's
		 * alternate groups has been squashed.
		 */
		if (op_ctx->export_perms.options & EXPORT_OPTION_ALL_ANONYMOUS)
			attr->group = op_ctx->export_perms.anonymous_gid;
		else if (((op_ctx->export_perms.options &
			   EXPORT_OPTION_ROOT_SQUASH) ||
			  (op_ctx->export_perms.options &
			   EXPORT_OPTION_ROOT_ID_SQUASH))
			 && (attr->group == 0)
			 && ((op_ctx->cred_flags & (GID_SQUASHED |
						     GARRAY_SQUASHED)) != 0))
			attr->group = op_ctx->export_perms.anonymous_gid;
	}
}

/**
 * @brief Compares two RPC creds
 *
 * @param[in] cred1 First RPC cred
 * @param[in] cred2 Second RPC cred
 *
 * @return true if same, false otherwise
 */
bool nfs_compare_clientcred(nfs_client_cred_t *cred1,
			    nfs_client_cred_t *cred2)
{
#ifdef _HAVE_GSSAPI
	gss_name_t cred1_cred_name;
	gss_name_t cred2_cred_name;
	OM_uint32 maj_stat, min_stat;
	int status;
#endif


	if (cred1 == NULL)
		return false;
	if (cred2 == NULL)
		return false;

	if (cred1->flavor != cred2->flavor)
		return false;

	switch (cred1->flavor) {
	case AUTH_UNIX:
		if (cred1->auth_union.auth_unix.aup_uid !=
		    cred2->auth_union.auth_unix.aup_uid)
			return false;
		if (cred1->auth_union.auth_unix.aup_gid !=
		    cred2->auth_union.auth_unix.aup_gid)
			return false;
		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		maj_stat = gss_inquire_context(&min_stat,
			cred1->auth_union.auth_gss.gd->ctx,
			&cred1_cred_name, NULL, NULL, NULL, NULL, NULL, NULL);

		if (maj_stat != GSS_S_COMPLETE &&
		    maj_stat != GSS_S_CONTEXT_EXPIRED)
			return false;

		 maj_stat = gss_inquire_context(&min_stat,
			cred2->auth_union.auth_gss.gd->ctx,
			&cred2_cred_name, NULL, NULL, NULL, NULL, NULL, NULL);

		if (maj_stat != GSS_S_COMPLETE &&
		    maj_stat != GSS_S_CONTEXT_EXPIRED) {
			gss_release_name(&min_stat, &cred1_cred_name);
			return false;
		}

		maj_stat = gss_compare_name(&min_stat, cred1_cred_name,
					    cred2_cred_name, &status);
		/* release the names */
		gss_release_name(&min_stat, &cred1_cred_name);
		gss_release_name(&min_stat, &cred2_cred_name);

		if (maj_stat != GSS_S_COMPLETE)
			return false;

		if (status == 0)
			return false;

		break;
#endif

	default:
		if (memcmp
		    (&cred1->auth_union, &cred2->auth_union, cred1->length))
			return false;
		break;
	}

	/* If this point is reached, structures are the same */
	return true;
}

int nfs_rpc_req2client_cred(struct svc_req *req, nfs_client_cred_t *pcred)
{
	/* Structure for managing basic AUTH_UNIX authentication */
	struct authunix_parms *aup = NULL;

	/* Stuff needed for managing RPCSEC_GSS */
#ifdef _HAVE_GSSAPI
	struct svc_rpc_gss_data *gd = NULL;
#endif

	pcred->length = req->rq_msg.cb_cred.oa_length;
	pcred->flavor = req->rq_msg.cb_cred.oa_flavor;

	switch (req->rq_msg.cb_cred.oa_flavor) {
	case AUTH_NONE:
		/* Do nothing... */
		break;

	case AUTH_UNIX:
		aup = (struct authunix_parms *)req->rq_msg.rq_cred_body;

		pcred->auth_union.auth_unix.aup_uid = aup->aup_uid;
		pcred->auth_union.auth_unix.aup_gid = aup->aup_gid;
		pcred->auth_union.auth_unix.aup_time = aup->aup_time;

		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		/* Extract the information from the RPCSEC_GSS
		 * opaque structure
		 */
		gd = SVCAUTH_PRIVATE(req->rq_auth);

		pcred->auth_union.auth_gss.svc = (unsigned int)(gd->sec.svc);
		pcred->auth_union.auth_gss.qop = (unsigned int)(gd->sec.qop);
		pcred->auth_union.auth_gss.gd = gd;
		break;
#endif

	default:
		/* Unsupported authentication flavour */
		return -1;
	}

	return 1;
}

/**
 * @brief Get numeric credentials from request
 *
 * @todo This MUST be refactored to not use TI-RPC private structures.
 * Instead, export appropriate functions from lib(n)tirpc.
 *
 * fills out creds in op_ctx
 *
 * @param[in]  req              Incoming request.
 *
 * @return NFS4_OK if successful, NFS4ERR_ACCESS otherwise.
 *
 */
nfsstat4 nfs_req_creds(struct svc_req *req)
{
	unsigned int i;
	const char *auth_label = "UNKNOWN";
	gid_t **garray_copy = &op_ctx->caller_garray_copy;
#ifdef _HAVE_GSSAPI
	struct svc_rpc_gss_data *gd = NULL;
	char principal[MAXNAMLEN + 1];
#endif

	/* Make sure we clear out all the cred_flags except CREDS_LOADED and
	 * CREDS_ANON.
	 */
	op_ctx->cred_flags &= CREDS_LOADED | CREDS_ANON;

	switch (req->rq_msg.cb_cred.oa_flavor) {
	case AUTH_NONE:
		/* Nothing to be done here... */
		op_ctx->cred_flags |= CREDS_LOADED | CREDS_ANON;
		auth_label = "AUTH_NONE";
		break;

	case AUTH_SYS:
		if ((op_ctx->cred_flags & CREDS_LOADED) == 0) {
			struct authunix_parms *creds = (struct authunix_parms *)
				req->rq_msg.rq_cred_body;

			/* We map the rq_cred to Authunix_parms */
			op_ctx->original_creds.caller_uid = creds->aup_uid;
			op_ctx->original_creds.caller_gid = creds->aup_gid;
			op_ctx->original_creds.caller_glen = creds->aup_len;
			op_ctx->original_creds.caller_garray = creds->aup_gids;
			op_ctx->cred_flags |= CREDS_LOADED;
		}

		/* Copy original_creds creds */
		op_ctx->creds = op_ctx->original_creds;

		/* Do we trust AUTH_SYS creds for groups or not ? */
		if ((op_ctx->export_perms.options & EXPORT_OPTION_MANAGE_GIDS)
		    != 0) {
			op_ctx->cred_flags |= MANAGED_GIDS;
			garray_copy = &op_ctx->managed_garray_copy;
		}

		auth_label = "AUTH_SYS";
		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		if ((op_ctx->cred_flags & CREDS_LOADED) == 0) {
			/* Get the gss data to process them */
			gd = SVCAUTH_PRIVATE(req->rq_auth);

			memcpy(principal, gd->cname.value, gd->cname.length);
			principal[gd->cname.length] = 0;

			LogMidDebug(COMPONENT_DISPATCH,
				     "Mapping RPCSEC_GSS principal %s to uid/gid",
				     principal);

			/* Convert to uid */
#if _MSPAC_SUPPORT
			if (!principal2uid(principal,
					   &op_ctx->original_creds.caller_uid,
					   &op_ctx->original_creds.caller_gid,
					   gd)) {
#else
			if (!principal2uid(principal,
					   &op_ctx->original_creds.caller_uid,
					   &op_ctx->original_creds.caller_gid)
			   ) {
#endif
				LogInfo(COMPONENT_IDMAPPER,
					"Could not map principal %s to uid",
					principal);
				/* For compatibility with Linux knfsd, we set
				 * the uid/gid to anonymous when a name->uid
				 * mapping can't be found.
				 */
				op_ctx->cred_flags |= CREDS_ANON |
						       CREDS_LOADED;
				auth_label = "RPCSEC_GSS (no mapping)";
				break;
			}

			op_ctx->cred_flags |= CREDS_LOADED;
		}

		auth_label = "RPCSEC_GSS";
		op_ctx->cred_flags |= MANAGED_GIDS;
		garray_copy = &op_ctx->managed_garray_copy;

		break;
#endif				/* _USE_GSSRPC */

	default:
		LogMidDebug(COMPONENT_DISPATCH,
			     "FAILURE: Request xid=%" PRIu32
			     ", has unsupported authentication %" PRIu32,
			     req->rq_msg.rm_xid,
			     req->rq_msg.cb_cred.oa_flavor);
		/* Reject the request for weak authentication and
		 * return to worker
		 */
		return NFS4ERR_ACCESS;

		break;
	}

	/****************************************************************/
	/* Now check for anon creds or id squashing			*/
	/****************************************************************/
	if ((op_ctx->cred_flags & CREDS_ANON) != 0 ||
	    ((op_ctx->export_perms.options & EXPORT_OPTION_ALL_ANONYMOUS) != 0)
	    ||
	    ((op_ctx->export_perms.options & EXPORT_OPTION_ROOT_SQUASH) != 0 &&
	      op_ctx->fsal_export->exp_ops.is_superuser(op_ctx->fsal_export,
					      &op_ctx->original_creds))) {
		/* Squash uid, gid, and discard groups */
		op_ctx->creds.caller_uid = op_ctx->export_perms.anonymous_uid;
		op_ctx->creds.caller_gid = op_ctx->export_perms.anonymous_gid;
		op_ctx->creds.caller_glen = 0;
		LogMidDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			    "%s creds squashed to uid=%u, gid=%u",
			    auth_label,
			    op_ctx->creds.caller_uid,
			    op_ctx->creds.caller_gid);
		op_ctx->cred_flags |= UID_SQUASHED | GID_SQUASHED;
		return NFS4_OK;
	} else if ((op_ctx->export_perms.options &
		    EXPORT_OPTION_ROOT_ID_SQUASH) != 0 &&
		   op_ctx->fsal_export->exp_ops.is_superuser(
			op_ctx->fsal_export, &op_ctx->original_creds)) {
		/* Only squash root id, leave gid and groups alone for now */
		op_ctx->creds.caller_uid = op_ctx->export_perms.anonymous_uid;
		op_ctx->cred_flags |= UID_SQUASHED;
	} else {
		/* Use original_creds uid */
		op_ctx->creds.caller_uid = op_ctx->original_creds.caller_uid;
	}

	/****************************************************************/
	/* Now sqush group or use original_creds gid			*/
	/****************************************************************/
	if (((op_ctx->export_perms.options & EXPORT_OPTION_ROOT_SQUASH) != 0 ||
	     (op_ctx->export_perms.options & EXPORT_OPTION_ROOT_ID_SQUASH) != 0)
	     && op_ctx->original_creds.caller_gid == 0) {
		/* Squash gid */
		op_ctx->creds.caller_gid = op_ctx->export_perms.anonymous_gid;
		op_ctx->cred_flags |= GID_SQUASHED;
	} else {
		/* Use original_creds gid */
		op_ctx->creds.caller_gid = op_ctx->original_creds.caller_gid;
	}

	/****************************************************************/
	/* Check if we have manage_gids.				*/
	/****************************************************************/
	if ((op_ctx->cred_flags & MANAGED_GIDS) != 0) {
		/* Fetch the group data if required */
		if (op_ctx->caller_gdata == NULL &&
		    !uid2grp(op_ctx->original_creds.caller_uid,
			     &op_ctx->caller_gdata)) {
			/** @todo: do we really want to bail here? */
			LogInfo(COMPONENT_DISPATCH,
				"Attempt to fetch managed_gids failed");
			return NFS4ERR_ACCESS;
		}

		op_ctx->creds.caller_glen = op_ctx->caller_gdata->nbgroups;
		op_ctx->creds.caller_garray = op_ctx->caller_gdata->groups;
	} else {
		/* Use the original_creds group list */
		op_ctx->creds.caller_glen = op_ctx->original_creds.caller_glen;
		op_ctx->creds.caller_garray =
					op_ctx->original_creds.caller_garray;
	}

	/****************************************************************/
	/* Check the garray for gid 0 to squash				*/
	/****************************************************************/

	/* If no root squashing in caller_garray, return now */
	if ((op_ctx->export_perms.options & EXPORT_OPTION_SQUASH_TYPES) == 0 ||
	    op_ctx->creds.caller_glen == 0)
		goto out;

	for (i = 0; i < op_ctx->creds.caller_glen; i++) {
		if (op_ctx->creds.caller_garray[i] == 0) {
			/* Meed to make a copy, or use the old copy */
			if ((*garray_copy) == NULL) {
				/* Make a copy of the active garray */
				(*garray_copy) =
					gsh_malloc(op_ctx->creds.caller_glen *
						   sizeof(gid_t));

				memcpy((*garray_copy),
				       op_ctx->creds.caller_garray,
				       op_ctx->creds.caller_glen *
				       sizeof(gid_t));
			}

			/* Now squash the root id. Since the original copy is
			 * always the same, any root ids in it were still in
			 * the same place, so even if using a copy that had a
			 * different anonymous_gid, we're fine.
			 */
			(*garray_copy)[i] = op_ctx->export_perms.anonymous_gid;

			/* Indicate we squashed the caller_garray */
			op_ctx->cred_flags |= GARRAY_SQUASHED;
		}
	}

	/* If we squashed the caller_garray, use the squashed copy */
	if ((op_ctx->cred_flags & GARRAY_SQUASHED) != 0)
		op_ctx->creds.caller_garray = *garray_copy;

out:

	LogMidDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
		    "%s creds mapped to uid=%u%s, gid=%u%s, glen=%d%s",
		    auth_label,
		    op_ctx->creds.caller_uid,
		    (op_ctx->cred_flags & UID_SQUASHED) != 0
			? " (squashed)"
			: "",
		    op_ctx->creds.caller_gid,
		    (op_ctx->cred_flags & GID_SQUASHED) != 0
			? " (squashed)"
			: "",
		    op_ctx->creds.caller_glen,
		    (op_ctx->cred_flags & MANAGED_GIDS) != 0
			? ((op_ctx->cred_flags & GARRAY_SQUASHED) != 0
				? " (managed and squashed)"
				: " (managed)")
			: ((op_ctx->cred_flags & GARRAY_SQUASHED) != 0
				? " (squashed)"
				: ""));

	return NFS4_OK;
}

/**
 * @brief Initialize request context and credentials.
 *
 */
void init_credentials(void)
{
	memset(&op_ctx->creds, 0, sizeof(op_ctx->creds));
	memset(&op_ctx->original_creds, 0, sizeof(op_ctx->original_creds));
	op_ctx->creds.caller_uid = op_ctx->export_perms.anonymous_uid;
	op_ctx->creds.caller_gid = op_ctx->export_perms.anonymous_gid;
	op_ctx->caller_gdata = NULL;
	op_ctx->caller_garray_copy = NULL;
	op_ctx->managed_garray_copy = NULL;
	op_ctx->cred_flags = 0;
}

/**
 * @brief Release temporary credential resources.
 *
 */
void clean_credentials(void)
{
	/* If Manage_gids is used, unref the group list. */
	if (op_ctx->caller_gdata != NULL)
		uid2grp_unref(op_ctx->caller_gdata);

	/* Have we made a local copy of the managed_gids garray? */
	if (op_ctx->managed_garray_copy != NULL)
		gsh_free(op_ctx->managed_garray_copy);

	/* Have we made a local copy of the AUTH_SYS garray? */
	if (op_ctx->caller_garray_copy != NULL)
		gsh_free(op_ctx->caller_garray_copy);

	/* Prepare the request context and creds for re-use */
	init_credentials();
}

/**
 * @brief Validate export permissions
 *
 * @param[in]  req              Incoming request.
 *
 * @return NFS4_OK if successful, NFS4ERR_ACCESS or NFS4ERR_WRONGSEC otherwise.
 *
 */
nfsstat4 nfs4_export_check_access(struct svc_req *req)
{
	xprt_type_t xprt_type = svc_get_xprt_type(req->rq_xprt);
	int port = get_port(op_ctx->caller_addr);

	LogMidDebugAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
		       "about to call export_check_access");
	export_check_access();

	/* Check if any access at all */
	if ((op_ctx->export_perms.options & EXPORT_OPTION_ACCESS_MASK) == 0) {
		LogInfoAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			"Access not allowed on Export_Id %d %s for client %s",
			op_ctx->ctx_export->export_id,
			CTX_PSEUDOPATH(op_ctx),
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");
		return NFS4ERR_ACCESS;
	}

	/* Check protocol version */
	if ((op_ctx->export_perms.options & EXPORT_OPTION_NFSV4) == 0) {
		LogInfoAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			"NFS4 not allowed on Export_Id %d %s for client %s",
			op_ctx->ctx_export->export_id,
			CTX_PSEUDOPATH(op_ctx),
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");
		return NFS4ERR_ACCESS;
	}

	/* Check transport type */
	if (((xprt_type == XPRT_UDP) &&
	    ((op_ctx->export_perms.options & EXPORT_OPTION_UDP) == 0)) ||
	    ((xprt_type == XPRT_TCP) &&
	    ((op_ctx->export_perms.options & EXPORT_OPTION_TCP) == 0))) {
		LogInfoAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			"NFS4 over %s not allowed on Export_Id %d %s for client %s",
			xprt_type_to_str(xprt_type),
			op_ctx->ctx_export->export_id,
			CTX_PSEUDOPATH(op_ctx),
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");
		return NFS4ERR_ACCESS;
	}

	/* Check if client is using a privileged port. */
	if (((op_ctx->export_perms.options &
	      EXPORT_OPTION_PRIVILEGED_PORT) != 0)
	    && (port >= IPPORT_RESERVED)) {
		LogInfoAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			"Non-reserved Port %d is not allowed on Export_Id %d %s for client %s",
			port, op_ctx->ctx_export->export_id,
			CTX_PSEUDOPATH(op_ctx),
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");
		return NFS4ERR_ACCESS;
	}

	/* Test if export allows the authentication provided */
	if (export_check_security(req) == false) {
		LogInfoAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			"NFS4 auth not allowed on Export_Id %d %s for client %s",
			op_ctx->ctx_export->export_id,
			CTX_PSEUDOPATH(op_ctx),
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");
		return NFS4ERR_WRONGSEC;
	}

	/* Get creds */
	return nfs_req_creds(req);
}

/**
 * @brief Perform version independent ACCESS operation.
 *
 * This function wraps a call to fsal_access, determining the appropriate
 * access_mask to use to check all the requested access bits. It requests the
 * allowed and denied access so that it can respond for each requested access
 * with a single access call.
 *
 * @param[in]  obj Object handle to check access for
 * @param[in]  requested_access The ACCESS3 or ACCESS4 bits requested
 * @param[out] granted_access   The bits granted
 * @param[out] supported_access The bits supported for this inode
 *
 * @return FSAL status which may have in 'status.major':
 *	   - ERR_FSAL_NO_ERROR all access was granted
 *	   - ERR_FSAL_ACCESS one or more access bits were denied
 *	   - other values indicate a FSAL failure
 *
 */

fsal_status_t nfs_access_op(struct fsal_obj_handle *obj,
				   uint32_t requested_access,
				   uint32_t *granted_access,
				   uint32_t *supported_access)
{
	fsal_status_t fsal_status;
	fsal_accessflags_t access_mask;
	fsal_accessflags_t access_allowed;
	fsal_accessflags_t access_denied;
	uint32_t granted_mask = requested_access;

	access_mask = 0;
	*granted_access = 0;

	LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_V4_ACL,
		    "Requested ACCESS=%s,%s,%s,%s,%s,%s",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_READ) ? "READ" : "-",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_LOOKUP) ? "LOOKUP" : "-",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_MODIFY) ? "MODIFY" : "-",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_EXTEND) ? "EXTEND" : "-",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_DELETE) ? "DELETE" : "-",
		    FSAL_TEST_MASK(requested_access,
				   ACCESS3_EXECUTE) ? "EXECUTE" : "-");

	/* Set mode for read.
	 * NOTE: FSAL_ACE_PERM_LIST_DIR and FSAL_ACE_PERM_READ_DATA have
	 *       the same bit value so we don't bother looking at file type.
	 */
	if (requested_access & (ACCESS3_READ | ACCESS4_XAREAD | ACCESS4_XALIST))
		access_mask |= FSAL_R_OK | FSAL_ACE_PERM_READ_DATA;

	if (requested_access & ACCESS3_LOOKUP) {
		if (obj->type == DIRECTORY)
			access_mask |= FSAL_X_OK | FSAL_ACE_PERM_EXECUTE;
		else
			granted_mask &= ~ACCESS3_LOOKUP;
	}

	if (requested_access & (ACCESS3_MODIFY | ACCESS4_XAWRITE)) {
		if (obj->type == DIRECTORY)
			access_mask |= FSAL_W_OK | FSAL_ACE_PERM_DELETE_CHILD;
		else
			access_mask |= FSAL_W_OK | FSAL_ACE_PERM_WRITE_DATA;
	}

	if (requested_access & ACCESS3_EXTEND) {
		if (obj->type == DIRECTORY)
			access_mask |=
			    FSAL_W_OK | FSAL_ACE_PERM_ADD_FILE |
			    FSAL_ACE_PERM_ADD_SUBDIRECTORY;
		else
			access_mask |= FSAL_W_OK | FSAL_ACE_PERM_APPEND_DATA;
	}

	if (requested_access & ACCESS3_DELETE) {
		if (obj->type == DIRECTORY)
			access_mask |= FSAL_W_OK | FSAL_ACE_PERM_DELETE_CHILD;
		else
			granted_mask &= ~ACCESS3_DELETE;
	}

	if (requested_access & ACCESS3_EXECUTE) {
		if (obj->type != DIRECTORY)
			access_mask |= FSAL_X_OK | FSAL_ACE_PERM_EXECUTE;
		else
			granted_mask &= ~ACCESS3_EXECUTE;
	}

	if (access_mask != 0)
		access_mask |=
		    FSAL_MODE_MASK_FLAG | FSAL_ACE4_MASK_FLAG |
		    FSAL_ACE4_PERM_CONTINUE;

	LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_V4_ACL,
		    "access_mask = mode(%c%c%c) ACL(%s,%s,%s,%s,%s)",
		    FSAL_TEST_MASK(access_mask, FSAL_R_OK) ? 'r' : '-',
		    FSAL_TEST_MASK(access_mask, FSAL_W_OK) ? 'w' : '-',
		    FSAL_TEST_MASK(access_mask, FSAL_X_OK) ? 'x' : '-',
		    FSAL_TEST_MASK(access_mask, FSAL_ACE_PERM_READ_DATA) ?
			obj->type == DIRECTORY ?
			"list_dir" : "read_data" : "-",
		    FSAL_TEST_MASK(access_mask,
				   FSAL_ACE_PERM_WRITE_DATA) ?
			obj->type == DIRECTORY ?
			"add_file" : "write_data" : "-",
		    FSAL_TEST_MASK(access_mask, FSAL_ACE_PERM_EXECUTE) ?
			"execute" : "-",
		    FSAL_TEST_MASK(access_mask,
				   FSAL_ACE_PERM_ADD_SUBDIRECTORY) ?
			"add_subdirectory" : "-",
		    FSAL_TEST_MASK(access_mask, FSAL_ACE_PERM_DELETE_CHILD) ?
			"delete_child" : "-");

	fsal_status = obj->obj_ops->test_access(obj, access_mask,
					       &access_allowed,
					       &access_denied, false);
	if (fsal_status.major == ERR_FSAL_NO_ERROR ||
	    fsal_status.major == ERR_FSAL_ACCESS) {
		/* Define granted access based on granted mode bits. */
		if (access_allowed & FSAL_R_OK) {
			*granted_access |= ACCESS3_READ |
					   ACCESS4_XAREAD |
					   ACCESS4_XALIST;
		}

		if (access_allowed & FSAL_W_OK) {
			*granted_access |= ACCESS3_MODIFY |
					   ACCESS3_EXTEND |
					   ACCESS3_DELETE |
					   ACCESS4_XAWRITE;
		}

		if (access_allowed & FSAL_X_OK)
			*granted_access |= ACCESS3_LOOKUP | ACCESS3_EXECUTE;

		/* Define granted access based on granted ACL bits. */
		if (access_allowed & FSAL_ACE_PERM_READ_DATA)
			*granted_access |= ACCESS3_READ |
					   ACCESS4_XAREAD |
					   ACCESS4_XALIST;

		if (obj->type == DIRECTORY) {
			if (access_allowed & FSAL_ACE_PERM_DELETE_CHILD)
				*granted_access |= ACCESS3_MODIFY |
						   ACCESS3_DELETE |
						   ACCESS4_XAWRITE;

			if (access_allowed & FSAL_ACE_PERM_ADD_FILE)
				*granted_access |= ACCESS3_EXTEND |
						   ACCESS4_XAWRITE;

			if (access_allowed & FSAL_ACE_PERM_ADD_SUBDIRECTORY)
				*granted_access |= ACCESS3_EXTEND |
						   ACCESS4_XAWRITE;
		} else {
			if (access_allowed & FSAL_ACE_PERM_WRITE_DATA)
				*granted_access |= ACCESS3_MODIFY |
						   ACCESS4_XAWRITE;

			if (access_allowed & FSAL_ACE_PERM_APPEND_DATA)
				*granted_access |= ACCESS3_EXTEND |
						   ACCESS4_XAWRITE;
		}

		if (access_allowed & FSAL_ACE_PERM_EXECUTE)
			*granted_access |= ACCESS3_LOOKUP | ACCESS3_EXECUTE;

		/* Allow only read if client has read only access
		 * on this share.
		 */
		if (!(op_ctx->export_perms.options &
		    EXPORT_OPTION_WRITE_ACCESS))
			*granted_access &= ~(ACCESS3_EXTEND |
					     ACCESS3_MODIFY |
					     ACCESS3_DELETE |
					     ACCESS4_XAWRITE);

		/* Don't allow any bits that weren't set on request or
		 * allowed by the file type.
		 */
		*granted_access &= granted_mask;

		if (supported_access != NULL)
			*supported_access = granted_mask;

		LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_V4_ACL,
			    "Supported ACCESS=%s,%s,%s,%s,%s,%s",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_READ) ? "READ" : "-",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_LOOKUP) ? "LOOKUP" : "-",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_MODIFY) ? "MODIFY" : "-",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_EXTEND) ? "EXTEND" : "-",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_DELETE) ? "DELETE" : "-",
			    FSAL_TEST_MASK(granted_mask,
					   ACCESS3_EXECUTE) ? "EXECUTE" : "-");

		LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_V4_ACL,
			    "Granted ACCESS=%s,%s,%s,%s,%s,%s",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_READ) ? "READ" : "-",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_LOOKUP) ? "LOOKUP" : "-",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_MODIFY) ? "MODIFY" : "-",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_EXTEND) ? "EXTEND" : "-",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_DELETE) ? "DELETE" : "-",
			    FSAL_TEST_MASK(*granted_access,
					   ACCESS3_EXECUTE) ? "EXECUTE" : "-");
	}

	return fsal_status;
}
