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
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "idmapper.h"
#include "export_mgr.h"
#include "uid2grp.h"

void squash_setattr(export_perms_t *export_perms,
		    struct req_op_context *req_ctx,
		    struct attrlist *attr)
{
	if (attr->mask & ATTR_OWNER) {
		if (export_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)
			attr->owner = export_perms->anonymous_uid;
		else if (!(export_perms->options & EXPORT_OPTION_ROOT)
			 && (attr->owner == 0)
			 && ((req_ctx->cred_flags & UID_SQUASHED) != 0))
			attr->owner = export_perms->anonymous_uid;
	}

	if (attr->mask & ATTR_GROUP) {
		/* If all squashed, then always squash the owner_group.
		 *
		 * If root squashed, then squash owner_group if
		 * caller_gid has been squashed or one of the caller's
		 * alternate groups has been squashed.
		 */
		if (export_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)
			attr->group = export_perms->anonymous_gid;
		else if (!(export_perms->options & EXPORT_OPTION_ROOT)
			 && (attr->group == 0)
			 && ((req_ctx->cred_flags & (GID_SQUASHED |
						     GARRAY_SQUASHED)) != 0))
			attr->group = export_perms->anonymous_gid;
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

	pcred->flavor = req->rq_cred.oa_flavor;
	pcred->length = req->rq_cred.oa_length;

	switch (req->rq_cred.oa_flavor) {
	case AUTH_NONE:
		/* Do nothing... */
		break;

	case AUTH_UNIX:
		aup = (struct authunix_parms *)(req->rq_clntcred);

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
		pcred->auth_union.auth_gss.gss_context_id = gd->ctx;
		break;
#endif

	default:
		/* Unsupported authentication flavour */
		return -1;
		break;
	}

	return 1;
}

/**
 * @brief Get numeric credentials from request
 *
 * @todo This MUST be refactored to not use TI-RPC private structures.
 * Instead, export appropriate functions from lib(n)tirpc.
 *
 * @param[in]  req              Incoming request.
 * @param[out] user_credentials Filled in structure with UID and GIDs
 *
 * @return true if successful, false otherwise
 *
 */
bool get_req_creds(struct svc_req *req,
		   struct req_op_context *req_ctx,
		   export_perms_t *export_perms)
{
	unsigned int i;
	const char *auth_label = "UNKNOWN";
	gid_t **garray_copy = &req_ctx->caller_garray_copy;
#ifdef _HAVE_GSSAPI
	struct svc_rpc_gss_data *gd = NULL;
	char principal[MAXNAMLEN + 1];
#endif

	/* Make sure we clear out all the cred_flags except CREDS_LOADED and
	 * CREDS_ANON.
	 */
	req_ctx->cred_flags &= CREDS_LOADED | CREDS_ANON;

	switch (req->rq_cred.oa_flavor) {
	case AUTH_NONE:
		/* Nothing to be done here... */
		req_ctx->cred_flags |= CREDS_LOADED | CREDS_ANON;
		auth_label = "AUTH_NONE";
		break;

	case AUTH_SYS:
		if ((req_ctx->cred_flags & CREDS_LOADED) == 0) {
			struct authunix_parms *creds = NULL;

			/* We map the rq_cred to Authunix_parms */
			creds = (struct authunix_parms *) req->rq_clntcred;
			req_ctx->original_creds.caller_uid = creds->aup_uid;
			req_ctx->original_creds.caller_gid = creds->aup_gid;
			req_ctx->original_creds.caller_glen = creds->aup_len;
			req_ctx->original_creds.caller_garray = creds->aup_gids;
			req_ctx->cred_flags |= CREDS_LOADED;
		}

		/* Copy original_creds creds */
		*req_ctx->creds = req_ctx->original_creds;

		/* Do we trust AUTH_SYS creds for groups or not ? */
		if ((export_perms->options & EXPORT_OPTION_MANAGE_GIDS)
		    != 0) {
			req_ctx->cred_flags |= MANAGED_GIDS;
			garray_copy = &req_ctx->managed_garray_copy;
		}

		auth_label = "AUTH_SYS";
		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		if ((req_ctx->cred_flags & CREDS_LOADED) == 0) {
			/* Get the gss data to process them */
			gd = SVCAUTH_PRIVATE(req->rq_auth);

			memcpy(principal, gd->cname.value, gd->cname.length);
			principal[gd->cname.length] = 0;

			LogMidDebug(COMPONENT_RPCSEC_GSS,
				     "Mapping RPCSEC_GSS principal %s to uid/gid",
				     principal);

			/* Convert to uid */
#if _MSPAC_SUPPORT
			if (!principal2uid(principal,
					   &req_ctx->original_creds.caller_uid,
					   &req_ctx->original_creds.caller_gid,
					   gd)) {
#else
			if (!principal2uid(principal,
					   &req_ctx->original_creds.caller_uid,
					   &req_ctx->original_creds.caller_gid)) {
#endif
				LogWarn(COMPONENT_IDMAPPER,
					"Could not map principal %s to uid",
					principal);
				/* For compatibility with Linux knfsd, we set
				 * the uid/gid to anonymous when a name->uid
				 * mapping can't be found.
				 */
				req_ctx->cred_flags |= CREDS_ANON |
						       CREDS_LOADED;
				auth_label = "RPCSEC_GSS (no mapping)";
				break;
			}

			req_ctx->cred_flags |= CREDS_LOADED;
		}

		auth_label = "RPCSEC_GSS";
		req_ctx->cred_flags |= MANAGED_GIDS;
		garray_copy = &req_ctx->managed_garray_copy;

		break;
#endif				/* _USE_GSSRPC */

	default:
		LogMidDebug(COMPONENT_DISPATCH,
			     "FAILURE: Request xid=%u, has unsupported authentication %d",
			     req->rq_xid, req->rq_cred.oa_flavor);
		/* Reject the request for weak authentication and
		 * return to worker
		 */
		return false;

		break;
	}

	/****************************************************************/
	/* Mow check for anon creds or id squashing			*/
	/****************************************************************/
	if ((req_ctx->cred_flags & CREDS_ANON) != 0 ||
	    ((export_perms->options & EXPORT_OPTION_ALL_ANONYMOUS) != 0) ||
	    ((export_perms->options & EXPORT_OPTION_ROOT) == 0 &&
	      req_ctx->original_creds.caller_uid == 0)) {
		req_ctx->creds->caller_uid = export_perms->anonymous_uid;
		req_ctx->creds->caller_gid = export_perms->anonymous_gid;
		req_ctx->creds->caller_glen = 0;
		LogMidDebug(COMPONENT_DISPATCH,
			    "%s creds squashed to uid=%u, gid=%u",
			    auth_label,
			    req_ctx->creds->caller_uid,
			    req_ctx->creds->caller_gid);
		req_ctx->cred_flags |= UID_SQUASHED | GID_SQUASHED;
		return true;
	}

	/* Now we will use the original_creds uid from original credential */
	req_ctx->creds->caller_uid = req_ctx->original_creds.caller_uid;

	/****************************************************************/
	/* Now sqush group or use original_creds gid			*/
	/****************************************************************/
	if ((export_perms->options & EXPORT_OPTION_ROOT) == 0 &&
	    req_ctx->original_creds.caller_gid == 0) {
		/* Squash gid */
		req_ctx->creds->caller_gid = export_perms->anonymous_gid;
		req_ctx->cred_flags |= GID_SQUASHED;
	} else {
		/* Use original_creds gid */
		req_ctx->creds->caller_gid = req_ctx->original_creds.caller_gid;
	}

	/****************************************************************/
	/* Check if we have manage_gids.				*/
	/****************************************************************/
	if ((req_ctx->cred_flags & MANAGED_GIDS) != 0) {
		/* Fetch the group data if required */
		if (req_ctx->caller_gdata == NULL &&
		    !uid2grp(req_ctx->original_creds.caller_uid,
			     &req_ctx->caller_gdata)) {
			/** @todo: do we really want to bail here? */
			LogCrit(COMPONENT_DISPATCH,
				"Attempt to fetch managed_gids failed");
			return false;
		}

		req_ctx->creds->caller_glen = req_ctx->caller_gdata->nbgroups;
		req_ctx->creds->caller_garray = req_ctx->caller_gdata->groups;
	} else {
		/* Use the original_creds group list */
		req_ctx->creds->caller_glen   =
					req_ctx->original_creds.caller_glen;
		req_ctx->creds->caller_garray =
					req_ctx->original_creds.caller_garray;
	}

	/****************************************************************/
	/* Check the garray for gid 0 to squash				*/
	/****************************************************************/

	/* If no root squashing in caller_garray, return now */
	if ((export_perms->options & EXPORT_OPTION_ROOT) != 0 ||
	    req_ctx->creds->caller_glen == 0)
		goto out;

	for (i = 0; i < req_ctx->creds->caller_glen; i++) {
		if (req_ctx->creds->caller_garray[i] == 0) {
			/* Meed to make a copy, or use the old copy */
			if ((*garray_copy) == NULL) {
				/* Make a copy of the active garray */
				(*garray_copy) =
					gsh_malloc(req_ctx->creds->caller_glen *
						   sizeof(gid_t));

				if ((*garray_copy) == NULL) {
					LogCrit(COMPONENT_DISPATCH,
						"Attempt to sqaush caller_garray failed - no memory");
					return false;
				}

				memcpy((*garray_copy),
				       req_ctx->creds->caller_garray,
				       req_ctx->creds->caller_glen *
				       sizeof(gid_t));
			}

			/* Now squash the root id. Since the original copy is
			 * always the same, any root ids in it were still in
			 * the same place, so even if using a copy that had a
			 * different anonymous_gid, we're fine.
			 */
			(*garray_copy)[i] = export_perms->anonymous_gid;

			/* Indicate we squashed the caller_garray */
			req_ctx->cred_flags |= GARRAY_SQUASHED;
		}
	}

	/* If we squashed the caller_garray, use the squashed copy */
	if ((req_ctx->cred_flags & GARRAY_SQUASHED) != 0)
		req_ctx->creds->caller_garray = *garray_copy;

out:

	LogMidDebug(COMPONENT_DISPATCH,
		    "%s creds mapped to uid=%u, gid=%u%s, glen=%d%s",
		    auth_label,
		    req_ctx->creds->caller_uid,
		    req_ctx->creds->caller_gid,
		    (req_ctx->cred_flags & GID_SQUASHED) != 0
		    	? " (squashed)"
		    	: "",
		    req_ctx->creds->caller_glen,
		    (req_ctx->cred_flags & MANAGED_GIDS) != 0
			? ((req_ctx->cred_flags & GARRAY_SQUASHED) != 0
				? " (managed and squashed)"
				: " (managed)")
			: ((req_ctx->cred_flags & GARRAY_SQUASHED) != 0
				? " (squashed)"
				: ""));

	return true;
}

/**
 * @brief Initialize request context and credentials.
 *
 * @param[in] req_ctx The request context to initialize.
 */
void init_credentials(struct req_op_context *req_ctx)
{
	memset(req_ctx->creds, 0, sizeof(*req_ctx->creds));
	memset(&req_ctx->original_creds, 0, sizeof(req_ctx->original_creds));
	req_ctx->creds->caller_uid = (uid_t) ANON_UID;
	req_ctx->creds->caller_gid = (gid_t) ANON_GID;
	req_ctx->caller_gdata = NULL;
	req_ctx->caller_garray_copy = NULL;
	req_ctx->managed_garray_copy = NULL;
	req_ctx->cred_flags = 0;
}

/**
 * @brief Release temporary credential resources.
 *
 * @param[in] req_ctx The request context to clean up.
 */
void clean_credentials(struct req_op_context *req_ctx)
{
	/* If Manage_gids is used, unref the group list. */
	if (req_ctx->caller_gdata != NULL)
		uid2grp_unref(req_ctx->caller_gdata);

	/* Have we made a local copy of the managed_gids garray? */
	if (req_ctx->managed_garray_copy != NULL)
	    	gsh_free(req_ctx->caller_garray_copy);

	/* Have we made a local copy of the AUTH_SYS garray? */
	if (req_ctx->caller_garray_copy != NULL)
	    	gsh_free(req_ctx->caller_garray_copy);

	/* Prepare the request context and creds for re-use */
	init_credentials(req_ctx);
}
