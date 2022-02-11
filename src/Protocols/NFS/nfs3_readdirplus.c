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
 *
 * ---------------------------------------
 */

/**
 * @file nfs3_readdirplus.c
 * @brief Everything you need for the NFSv3 readdirplus operation
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include <assert.h>

fsal_errors_t nfs3_readdirplus_callback(void *opaque,
					       struct fsal_obj_handle *obj,
					       const struct fsal_attrlist *attr,
					       uint64_t mounted_on_fileid,
					       uint64_t cookie,
					       enum cb_state cb_state);

/**
 * @brief Opaque bookkeeping structure for NFSPROC3_READDIRPLUS
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIRPLUS response between calls to nfs3_readdirplus_callback.
 */

struct nfs3_readdirplus_cb_data {
	XDR xdr;	/*< The xdrmem to serialize the entries */
	char *entries;	/*< The array holding serialized entries */
	size_t mem_avail;	/*< The amount of memory available before we
				   hit maxcount */
	nfsstat3 error;		/*< Set to a value other than NFS_OK if the
				   callback function finds a fatal error. */
};

static
nfsstat3 nfs_readdir_dot_entry(struct fsal_obj_handle *obj, const char *name,
			       uint64_t cookie, helper_readdir_cb cb,
			       struct nfs3_readdirplus_cb_data *tracker,
			       struct fsal_attrlist *attrs)
{
	struct fsal_readdir_cb_parms cb_parms;
	fsal_status_t fsal_status;

	cb_parms.opaque = tracker;
	cb_parms.name = name;
	cb_parms.attr_allowed = true;
	cb_parms.in_result = true;
	fsal_status.major = cb(&cb_parms, obj, attrs, 0, cookie, CB_ORIGINAL);

	if (FSAL_IS_ERROR(fsal_status))
		return nfs3_Errno_status(fsal_status);
	else
		return tracker->error;
}

/**
 * @brief The NFSPROC3_READDIRPLUS
 *
 * Implements the NFSPROC3_READDIRPLUS function
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_readdirplus(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *dir_obj = NULL;
	uint64_t begin_cookie = 0;
	uint64_t fsal_cookie = 0;
	uint32_t maxcount;
	uint32_t cfg_readdir_size = nfs_param.core_param.readdir_res_size;
	cookieverf3 cookie_verifier;
	unsigned int num_entries = 0;
	uint64_t mem_avail = 0;
	uint64_t max_mem = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t fsal_status_gethandle = {0, 0};
	int rc = NFS_REQ_OK;
	struct nfs3_readdirplus_cb_data tracker;
	struct fsal_attrlist attrs_dir, attrs_parent;
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);
	READDIRPLUS3resfail *resfail =
			&res->res_readdirplus3.READDIRPLUS3res_u.resfail;
	READDIRPLUS3resok *resok =
			&res->res_readdirplus3.READDIRPLUS3res_u.resok;

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs_dir, ATTRS_NFS3 | ATTR_RDATTR_ERR);
	fsal_prepare_attrs(&attrs_parent, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_readdirplus3.dir,
			  "");

	/* to avoid setting it on each error case */
	resfail->dir_attributes.attributes_follow = FALSE;

	memset(&tracker, 0, sizeof(tracker));

	if (op_ctx_export_has_option(EXPORT_OPTION_NO_READDIR_PLUS)) {
		res->res_readdirplus3.status = NFS3ERR_NOTSUPP;
		LogDebug(COMPONENT_NFS_READDIR, "Request not supported");
		goto out;
	}

	/* Figure out how much memory we have available for the dirlist3.
	 * Subtract:
	 *	nfsstat3 status (BYTES_PER_XDR_UNIT)
	 *	post_op_attr dir_attributes
	 *		bool_t attributes_follow
	 *		struct fattr3_wire attributes
	 *	cookieverf3 cookieverf (8 or sizeof(cookieverf3))
	 */
	if (cfg_readdir_size != 0 &&
	    cfg_readdir_size < arg->arg_readdirplus3.maxcount) {
		maxcount = cfg_readdir_size;
	} else {
		maxcount = arg->arg_readdirplus3.maxcount;
	}
	mem_avail = maxcount
				- BYTES_PER_XDR_UNIT
				- BYTES_PER_XDR_UNIT
				- sizeof(struct fattr3_wire)
				- sizeof(cookieverf3);
	max_mem = atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxRead);
	tracker.mem_avail = MIN(mem_avail, max_mem);

	begin_cookie = arg->arg_readdirplus3.cookie;

	LogDebug(COMPONENT_NFS_READDIR,
		 "NFS3_READDIRPLUS: dircount=%u begin_cookie=%" PRIu64
		 " mem_avail=%zd",
		 arg->arg_readdirplus3.dircount, begin_cookie,
		 tracker.mem_avail);

	/* Convert file handle into a vnode */
	dir_obj = nfs3_FhandleToCache(&(arg->arg_readdirplus3.dir),
					  &(res->res_readdirplus3.status),
					  &rc);

	if (dir_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Extract the filetype */
	dir_filetype = dir_obj->type;

	/* Sanity checks -- must be a directory */

	if (dir_filetype != DIRECTORY) {
		res->res_readdirplus3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Fetch the attributes for use later. */
	fsal_status = dir_obj->obj_ops->getattrs(dir_obj, &attrs_dir);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_readdirplus3.status = nfs3_Errno_status(fsal_status);
		LogDebug(COMPONENT_NFS_READDIR,
			 "Error %s fetching attributes",
			 fsal_err_txt(fsal_status));
		goto out;
	}

	/* To make or check the cookie verifier */
	memset(cookie_verifier, 0, sizeof(cookie_verifier));

	/* If cookie verifier is used, then an non-trivial value is
	 * returned to the client This value is the change attribute of the
	 * directory. If verifier is unused (as in many NFS Servers) then
	 * only a set of zeros is returned (trivial value)
	 */
	if (use_cookie_verifier) {
		if (attrs_dir.valid_mask == ATTR_RDATTR_ERR) {
			res->res_readdirplus3.status = NFS3ERR_SERVERFAULT;
			LogDebug(COMPONENT_NFS_READDIR,
				 "Could not fetch ctime");
			goto out_fail;
		}

		memcpy(cookie_verifier, &attrs_dir.change,
		       MIN(sizeof(cookie_verifier), sizeof(attrs_dir.change)));
	}

	if (use_cookie_verifier && begin_cookie != 0) {
		/* Not the first call, so we have to check the cookie
		   verifier */
		if (memcmp(cookie_verifier,
			   arg->arg_readdirplus3.cookieverf,
			   NFS3_COOKIEVERFSIZE) != 0) {
			res->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;
			rc = NFS_REQ_OK;
			goto out_fail;
		}
	}

	resok->reply.entries = NULL;
	resok->reply.eof = FALSE;

	/* Fudge cookie for "." and "..", if necessary */
	if (begin_cookie > 2)
		fsal_cookie = begin_cookie;
	else
		fsal_cookie = 0;

	/* Allocate space for entries */
	tracker.entries = gsh_malloc(tracker.mem_avail);
	xdrmem_create(&tracker.xdr, tracker.entries, tracker.mem_avail,
		      XDR_ENCODE);

	if (begin_cookie == 0) {
		/* Fill in "." */
		res->res_readdirplus3.status =
			nfs_readdir_dot_entry(dir_obj, ".", 1,
					      nfs3_readdirplus_callback,
					      &tracker, &attrs_dir);

		if (res->res_readdirplus3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_destroy;
		}
	}

	/* Fill in ".." */
	if (begin_cookie <= 1) {
		struct fsal_obj_handle *parent_dir_obj = NULL;

		fsal_status_gethandle = fsal_lookupp(dir_obj,
						     &parent_dir_obj,
						     &attrs_parent);

		if (parent_dir_obj == NULL) {
			res->res_readdirplus3.status =
			    nfs3_Errno_status(fsal_status_gethandle);
			rc = NFS_REQ_OK;
			goto out_destroy;
		}

		res->res_readdirplus3.status =
		    nfs_readdir_dot_entry(parent_dir_obj,
					  "..",
					  2,
					  nfs3_readdirplus_callback,
					  &tracker,
					  &attrs_parent);

		parent_dir_obj->obj_ops->put_ref(parent_dir_obj);

		if (res->res_readdirplus3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_destroy;
		}
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "Readdirplus3 -> Call to fsal_readdir, cookie=%"
		 PRIu64, fsal_cookie);

	/* Call readdir */
	fsal_status = fsal_readdir(dir_obj, fsal_cookie, &num_entries, &eod_met,
				   ATTRS_NFS3, nfs3_readdirplus_callback,
				   &tracker);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Is this a retryable error */
		if (nfs_RetryableError(fsal_status.major)) {
			rc = NFS_REQ_DROP;
			goto out_destroy;
		}

		res->res_readdirplus3.status = nfs3_Errno_status(fsal_status);
		goto out_destroy;
	}

	if (tracker.error != NFS3_OK) {
		res->res_readdirplus3.status = tracker.error;
		goto out_destroy;
	}

	if ((num_entries == 0) && (begin_cookie > 1)) {
		res->res_readdirplus3.status = NFS3_OK;
		resok->reply.entries = NULL;
		resok->reply.eof = TRUE;
	} else {
		struct xdr_uio *uio;
		u_int pos_end;
		bool_t tmp;

		if (eod_met) {
			/* If we hit end of directory, then the dirlist3 we have
			 * encoded so far has a complete entry (we MUST have
			 * consumed the last entry and encoded it). Now we need
			 * to encode a FALSE to indicate no next entry.
			 */
			tmp = FALSE;
			if (!xdr_bool(&tracker.xdr, &tmp)) {
				/* Oops... */
				LogCrit(COMPONENT_NFS_READDIR,
					"Encode of no next entry failed.");
				res->res_readdirplus3.status
					= NFS3ERR_SERVERFAULT;
				goto out_destroy;
			}
		}

		/* Serialize eod_met into the entries buffer */
		tmp = eod_met;
		if (!xdr_bool(&tracker.xdr, &tmp)) {
			/* Oops... */
			LogCrit(COMPONENT_NFS_READDIR,
				"Encode of EOD failed.");
			res->res_readdirplus3.status = NFS3ERR_SERVERFAULT;
			goto out_destroy;
		}

		pos_end = xdr_getpos(&tracker.xdr);

		/* Get an xdr_uio and fill it in */
		uio = gsh_calloc(
			1, sizeof(struct xdr_uio) + sizeof(struct xdr_uio));
		uio->uio_release = xdr_dirlistplus3_uio_release;
		uio->uio_count = 1;
		uio->uio_vio[0].vio_base = tracker.entries;
		uio->uio_vio[0].vio_head = tracker.entries;
		uio->uio_vio[0].vio_tail = tracker.entries + pos_end;
		uio->uio_vio[0].vio_wrap = tracker.entries + pos_end;
		uio->uio_vio[0].vio_length = pos_end;
		uio->uio_vio[0].vio_type = VIO_DATA;

		/* Take over entries buffer */
		tracker.entries = NULL;
		resok->reply.uio = uio;
		resok->reply.entries = NULL;
	}

	nfs_SetPostOpAttr(dir_obj, &resok->dir_attributes, &attrs_dir);

	memcpy(resok->cookieverf, cookie_verifier, sizeof(cookieverf3));

	res->res_readdirplus3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out_destroy:

	xdr_destroy(&tracker.xdr);

 out_fail:

	if (res->res_readdirplus3.status != NFS3_OK) {
		/* Set the post op attributes as we have found them. */
		nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes,
				  &attrs_dir);
	}

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs_dir);
	fsal_release_attrs(&attrs_parent);

	if (dir_obj)
		dir_obj->obj_ops->put_ref(dir_obj);

	/* If we allocated but didn't consume entries, free it now. */
	gsh_free(tracker.entries);

	return rc;
}				/* nfs3_readdirplus */

/**
 * @brief Frees the result structure allocated for nfs3_readdirplus.
 *
 * Frees the result structure allocated for nfs3_readdirplus.
 *
 * @param resp [in,out] Pointer to the result structure
 *
 */
void nfs3_readdirplus_free(nfs_res_t *resp)
{
	/* Nothing to do, entries has been handed off or already freed. */
}

/**
 * @brief Populate entryplus3s when called from fsal_readdir
 *
 * This function is a callback passed to fsal_readdir.  It
 * fills in a pre-allocated array of entryplys3 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdirplus_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current obj
 * @param handle [in] The current obj's filehandle
 * @param attrs [in] The current obj's attributes
 * @param cookie [in] The readdir cookie for the current obj
 */

fsal_errors_t nfs3_readdirplus_callback(void *opaque,
					struct fsal_obj_handle *obj,
					const struct fsal_attrlist *attr,
					uint64_t mounted_on_fileid,
					uint64_t cookie,
					enum cb_state cb_state)
{
	/* Not-so-opaque pointer to callback data` */
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdirplus_cb_data *tracker = cb_parms->opaque;
	entryplus3 ep3;
	u_int pos_start = xdr_getpos(&tracker->xdr);

	LogDebug(COMPONENT_NFS_READDIR,
		"Callback for %s cookie %"PRIu64,
		cb_parms->name, cookie);

	memset(&ep3, 0, sizeof(ep3));
	ep3.fileid = obj->fileid;
	ep3.name = (char *) cb_parms->name;
	ep3.cookie = cookie;

	if (cb_parms->attr_allowed) {
		ep3.name_handle.handle_follows = TRUE;

		if (!nfs3_FSALToFhandle(true,
					&ep3.name_handle.post_op_fh3_u.handle,
					obj,
					op_ctx->ctx_export)) {
			tracker->error = NFS3ERR_SERVERFAULT;
			cb_parms->in_result = false;
			return ERR_FSAL_NO_ERROR;
		}

		/* Check if attributes follow */
		ep3.name_attributes.attributes_follow =
						nfs3_Fixup_FSALattr(obj, attr);
	} else {
		ep3.name_handle.handle_follows = false;
		ep3.name_attributes.attributes_follow = false;
	}

	/* Encode the entry into the xdrmem buffer and then assure there is
	 * space for at least two booleans (one to be false to terminate the
	 * entry ist, the other to encode EOD or not). Note we use the special
	 * xdr_encode_entryplus3 that uses a passed in struct fsal_attrlist
	 * rather than name_attributes from entryplus3, though we will use the
	 * boolean attributes_follow from the entryplus3.
	 */
	if (!xdr_encode_entryplus3(&tracker->xdr, &ep3, attr) ||
	    (xdr_getpos(&tracker->xdr) + BYTES_PER_XDR_UNIT)
	    >= tracker->mem_avail) {
		bool_t res_false = false;

		/* XDR serialization of the entry failed or we ran out of room
		 * to serialize at least a boolean after the result. */
		cb_parms->in_result = false;

		/* Reset to where we started this entry and encode a boolean
		 * false instead (entry_follows is false).
		 */
		if (!xdr_setpos(&tracker->xdr, pos_start) ||
		    !xdr_bool(&tracker->xdr, &res_false)) {
			/* Oops, what broke... */
			LogCrit(COMPONENT_NFS_READDIR,
				"Unexpected XDR failure processing readdir result");
			tracker->error = NFS3ERR_SERVERFAULT;
		}
	} else {
		/* The entry fit, let the caller know it fit */
		cb_parms->in_result = true;
	}

	/* Now we are done with anything allocated */
	gsh_free(ep3.name_handle.post_op_fh3_u.handle.data.data_val);

	return ERR_FSAL_NO_ERROR;
}				/* nfs3_readdirplus_callback */
