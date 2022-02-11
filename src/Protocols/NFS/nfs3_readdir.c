// SPDX-License-Identifier: LGPL-3.0-or-later
 /*
  * vimf:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file  nfs3_readdir.c
 * @brief Everything you need for a simple READDIR
 *
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
#include "nfs_proto_tools.h"
#include <assert.h>

fsal_errors_t nfs3_readdir_callback(void *opaque,
				    struct fsal_obj_handle *obj,
				    const struct fsal_attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state);

/**
 * @brief Opaque bookkeeping structure for NFSv3 readdir
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIR response between calls to nfs3_readdir_callback.
 */

struct nfs3_readdir_cb_data {
	XDR xdr;                /*< The xdrmem to serialize the entries */
	char *entries;          /*< The array holding serialized entries */
	size_t mem_avail;       /*< The amount of memory available before we
				   hit maxcount */
	nfsstat3 error;		/*< Set to a value other than NFS_OK if the
				   callback function finds a fatal error. */
};

static
nfsstat3 nfs_readdir_dot_entry(struct fsal_obj_handle *obj, const char *name,
			       uint64_t cookie, helper_readdir_cb cb,
			       struct nfs3_readdir_cb_data *tracker)
{
	struct fsal_readdir_cb_parms cb_parms;
	fsal_status_t fsal_status;

	cb_parms.opaque = tracker;
	cb_parms.name = name;
	cb_parms.attr_allowed = true;
	cb_parms.in_result = true;

	/* NFS v3 READDIR does not use attributes, so pass NULL */
	fsal_status.major = cb(&cb_parms, obj, NULL, 0, cookie, CB_ORIGINAL);

	if (FSAL_IS_ERROR(fsal_status))
		return nfs3_Errno_status(fsal_status);
	else
		return tracker->error;
}

/**
 *
 * @brief The NFSPROC3_READDIR
 *
 * Implements the NFSPROC3_READDIR function.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_readdir(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *dir_obj = NULL;
	struct fsal_obj_handle *parent_dir_obj = NULL;
	uint64_t cookie = 0;
	uint64_t fsal_cookie = 0;
	cookieverf3 cookie_verifier;
	unsigned int num_entries = 0;
	uint64_t mem_avail = 0;
	uint64_t max_mem = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t fsal_status_gethandle = {0, 0};
	int rc = NFS_REQ_OK;
	struct nfs3_readdir_cb_data tracker;
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);
	READDIR3resfail *resfail = &res->res_readdir3.READDIR3res_u.resfail;

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_readdir3.dir,
			  "");

	READDIR3resok * const RES_READDIR3_OK =
	    &res->res_readdir3.READDIR3res_u.resok;

	/* to avoid setting it on each error case */
	resfail->dir_attributes.attributes_follow = FALSE;

	memset(&tracker, 0, sizeof(tracker));

	/* Look up object for filehandle */
	dir_obj = nfs3_FhandleToCache(&(arg->arg_readdir3.dir),
					&(res->res_readdir3.status),
					&rc);
	if (dir_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Extract the filetype */
	dir_filetype = dir_obj->type;
	/* Sanity checks -- must be a directory */
	if (dir_filetype != DIRECTORY) {
		res->res_readdir3.status = NFS3ERR_NOTDIR;

		rc = NFS_REQ_OK;
		goto out;
	}

	/* Parse out request arguments and decide how many entries we
	 * want.  For NFSv3, deal with the cookie verifier.
	 */

	cookie = arg->arg_readdir3.cookie;

	/* Figure out how much memory we have available for the dirlist3.
	 * Subtract:
	 *	nfsstat3 status (BYTES_PER_XDR_UNIT)
	 *	post_op_attr dir_attributes
	 *		bool_t attributes_follow
	 *		struct fattr3_wire attributes
	 *	cookieverf3 cookieverf (8 or sizeof(cookieverf3))
	 */
	mem_avail = arg->arg_readdir3.count
			- BYTES_PER_XDR_UNIT
			- BYTES_PER_XDR_UNIT
			- sizeof(struct fattr3_wire)
			- sizeof(cookieverf3);
	max_mem = atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxRead);
	tracker.mem_avail = MIN(mem_avail, max_mem);

	LogDebug(COMPONENT_NFS_READDIR,
		 "---> NFS3_READDIR: count=%u  cookie=%" PRIu64
		 " mem_avail=%zd",
		 arg->arg_readdir3.count, cookie, tracker.mem_avail);

	/* To make or check the cookie verifier */
	memset(cookie_verifier, 0, sizeof(cookie_verifier));

	/* If cookie verifier is used, then an non-trivial value is
	 * returned to the client This value is the change attribute of the
	 * directory. If verifier is unused (as in many NFS Servers) then
	 * only a set of zeros is returned (trivial value)
	 */
	if (use_cookie_verifier) {
		struct fsal_attrlist attrs;

		fsal_prepare_attrs(&attrs, ATTR_CTIME);

		fsal_status = dir_obj->obj_ops->getattrs(dir_obj, &attrs);

		if (FSAL_IS_ERROR(fsal_status)) {
			res->res_readdir3.status =
						nfs3_Errno_status(fsal_status);
			LogDebug(COMPONENT_NFS_READDIR,
				 "getattrs returned %s",
				 msg_fsal_err(fsal_status.major));
			goto out;
		}

		memcpy(cookie_verifier, &attrs.change,
		       MIN(sizeof(cookie_verifier), sizeof(attrs.change)));

		/* Done with the attrs */
		fsal_release_attrs(&attrs);
	}

	if (cookie != 0 && use_cookie_verifier) {
		/* Not the first call, so we have to check the cookie
		 * verifier
		 */
		if (memcmp(cookie_verifier,
			   arg->arg_readdir3.cookieverf,
			   NFS3_COOKIEVERFSIZE) != 0) {
			res->res_readdir3.status = NFS3ERR_BAD_COOKIE;
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	tracker.entries = gsh_malloc(tracker.mem_avail);
	xdrmem_create(&tracker.xdr, tracker.entries, tracker.mem_avail,
		      XDR_ENCODE);
	tracker.error = NFS3_OK;

	/* Adjust the cookie we supply to fsal */
	if (cookie > 2) {	/* it is not the cookie for "." nor ".." */
		fsal_cookie = cookie;
	} else {
		fsal_cookie = 0;
	}

	/* Fills "."  */
	if (cookie == 0) {
		res->res_readdir3.status = nfs_readdir_dot_entry(dir_obj, ".",
					 1, nfs3_readdir_callback, &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_destroy;
		}
	}

	/* Fills ".." */
	if ((cookie <= 1)) {
		/* Get parent pentry */
		fsal_status_gethandle = fsal_lookupp(dir_obj,
						     &parent_dir_obj,
						     NULL);

		if (parent_dir_obj == NULL) {
			res->res_readdir3.status =
			    nfs3_Errno_status(fsal_status_gethandle);
			rc = NFS_REQ_OK;
			goto out_destroy;
		}

		res->res_readdir3.status = nfs_readdir_dot_entry(parent_dir_obj,
				"..", 2, nfs3_readdir_callback, &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_destroy;
		}

		parent_dir_obj->obj_ops->put_ref(parent_dir_obj);
		parent_dir_obj = NULL;
	}

	/* Call readdir */
	fsal_status = fsal_readdir(dir_obj, fsal_cookie, &num_entries, &eod_met,
				   0,	/* no attr */
				   nfs3_readdir_callback, &tracker);

	if (FSAL_IS_ERROR(fsal_status)) {
		if (nfs_RetryableError(fsal_status.major)) {
			rc = NFS_REQ_DROP;
			goto out_destroy;
		}

		res->res_readdir3.status = nfs3_Errno_status(fsal_status);
		nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes, NULL);
		goto out_destroy;
	}

	if (tracker.error != NFS3_OK) {
		res->res_readdir3.status = tracker.error;
		nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes, NULL);
		goto out_destroy;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "-- Readdir -> Call to fsal_readdir(cookie=%"
		 PRIu64 ")",
		 fsal_cookie);

	if ((num_entries == 0) && (cookie > 1)) {
		RES_READDIR3_OK->reply.entries = NULL;
		RES_READDIR3_OK->reply.eof = TRUE;
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
				res->res_readdir3.status = NFS3ERR_SERVERFAULT;
				goto out_destroy;
			}
		}

		/* Serialize eod_met into the entries buffer */
		tmp = eod_met;
		if (!xdr_bool(&tracker.xdr, &tmp)) {
			/* Oops... */
			LogCrit(COMPONENT_NFS_READDIR,
				"Encode of EOD failed.");
			res->res_readdir3.status = NFS3ERR_SERVERFAULT;
			goto out_destroy;
		}

		pos_end = xdr_getpos(&tracker.xdr);

		/* Get an xdr_uio and fill it in */
		uio = gsh_calloc(
			1, sizeof(struct xdr_uio) + sizeof(struct xdr_uio));
		uio->uio_release = xdr_dirlist3_uio_release;
		uio->uio_count = 1;
		uio->uio_vio[0].vio_base = tracker.entries;
		uio->uio_vio[0].vio_head = tracker.entries;
		uio->uio_vio[0].vio_tail = tracker.entries + pos_end;
		uio->uio_vio[0].vio_wrap = tracker.entries + pos_end;
		uio->uio_vio[0].vio_length = pos_end;
		uio->uio_vio[0].vio_type = VIO_DATA;

		/* Take over entries buffer */
		tracker.entries = NULL;
		RES_READDIR3_OK->reply.uio = uio;
		RES_READDIR3_OK->reply.entries = NULL;
	}
	nfs_SetPostOpAttr(dir_obj, &RES_READDIR3_OK->dir_attributes, NULL);
	memcpy(RES_READDIR3_OK->cookieverf, cookie_verifier,
	       sizeof(cookieverf3));
	res->res_readdir3.status = NFS3_OK;

	rc = NFS_REQ_OK;

out_destroy:

	xdr_destroy(&tracker.xdr);

 out:
	/* return references */
	if (dir_obj)
		dir_obj->obj_ops->put_ref(dir_obj);

	if (parent_dir_obj)
		parent_dir_obj->obj_ops->put_ref(parent_dir_obj);

	return rc;
}				/* nfs3_readdir */

/**
 * @brief Free the result structure allocated for nfs3_readdir
 *
 * @param[in,out] resp Result structure
 *
 */
void nfs3_readdir_free(nfs_res_t *resp)
{
	/* Nothing to do, entries has been handed off or already freed. */
}

/**
 * @brief Populate entry3s when called from fsal_readdir
 *
 * This function is a callback passed to fsal_readdir.  It
 * fills in a pre-allocated XDR buffer.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdir_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current obj
 * @param handle [in] The current obj's filehandle
 * @param attrs [in] The current obj's attributes
 * @param cookie [in] The readdir cookie for the current obj
 */

fsal_errors_t nfs3_readdir_callback(void *opaque,
				    struct fsal_obj_handle *obj,
				    const struct fsal_attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state)
{
	/* Not-so-opaque pointer to callback data` */
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdir_cb_data *tracker = cb_parms->opaque;
	entry3 e3;
	u_int pos_start = xdr_getpos(&tracker->xdr);

	memset(&e3, 0, sizeof(e3));
	e3.fileid = obj->fileid;
	e3.name = (char *) cb_parms->name;
	e3.cookie = cookie;

	/* Encode the entry into the xdrmem buffer and then assure there is
	 * space for at least two booleans (one to be false to terminate the
	 * entry ist, the other to encode EOD or not).
	 */
	if (!xdr_encode_entry3(&tracker->xdr, &e3) ||
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

	return ERR_FSAL_NO_ERROR;
}				/* nfs3_readdir_callback */
