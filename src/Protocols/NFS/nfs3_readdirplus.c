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
					       const struct attrlist *attr,
					       uint64_t mounted_on_fileid,
					       uint64_t cookie,
					       enum cb_state cb_state);

static void free_entryplus3s(entryplus3 *entryplus3s);

/**
 * @brief Opaque bookkeeping structure for NFSPROC3_READDIRPLUS
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIRPLUS response between calls to nfs3_readdirplus_callback.
 */

struct nfs3_readdirplus_cb_data {
	entryplus3 *entries;	/*< The array holding individual entries */
	size_t mem_left;	/*< The amount of memory remaining before we
				   hit maxcount */
	size_t count;		/*< The count of complete entries stored in the
				   buffer */
	size_t total_entries;	/*< The number of entires we allocated for
				   the array. */
	nfsstat3 error;		/*< Set to a value other than NFS_OK if the
				   callback function finds a fatal error. */
};

static
nfsstat3 nfs_readdir_dot_entry(struct fsal_obj_handle *obj, const char *name,
			       uint64_t cookie, helper_readdir_cb cb,
			       struct nfs3_readdirplus_cb_data *tracker,
			       struct attrlist *attrs)
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
	cookieverf3 cookie_verifier;
	unsigned int num_entries = 0;
	unsigned long estimated_num_entries = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t fsal_status_gethandle = {0, 0};
	int rc = NFS_REQ_OK;
	struct nfs3_readdirplus_cb_data tracker = {
		.entries = NULL,
		.mem_left = 0,
		.count = 0,
		.error = NFS3_OK,
	};
	struct attrlist attrs_dir, attrs_parent;
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

	if (isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR)) {
		char str[LEN_FH_STR];

		sprint_fhandle3(str, &(arg->arg_readdirplus3.dir));

		LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_READDIR,
			    "REQUEST PROCESSING: Calling NFS3_READDIRPLUS handle: %s",
			    str);
	}

	/* to avoid setting it on each error case */
	res->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow
		= FALSE;

	if (op_ctx_export_has_option(EXPORT_OPTION_NO_READDIR_PLUS)) {
		res->res_readdirplus3.status = NFS3ERR_NOTSUPP;
		LogDebug(COMPONENT_NFS_READDIR, "Request not supported");
		goto out;
	}

	tracker.mem_left = (arg->arg_readdirplus3.maxcount * 9) / 10;
	begin_cookie = arg->arg_readdirplus3.cookie;

	tracker.mem_left -= sizeof(READDIRPLUS3resok);

	/* Estimate assuming that we're going to send no names and no handles.
	 * Don't count space for pointers for nextentry or
	 * name_handle.data.data_val in entryplus3
	 */
	estimated_num_entries = MIN((tracker.mem_left + sizeof(entryplus3 *))
				    / (sizeof(entryplus3) - sizeof(char *) * 2),
				    50);

	tracker.total_entries = estimated_num_entries;

	LogDebug(COMPONENT_NFS_READDIR,
		 "NFS3_READDIRPLUS: dircount=%u begin_cookie=%" PRIu64
		 " estimated_num_entries=%lu, mem_left=%zd",
		 arg->arg_readdirplus3.dircount, begin_cookie,
		 estimated_num_entries, tracker.mem_left);

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
			res->res_readdir3.status = NFS3ERR_SERVERFAULT;
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
	tracker.entries = gsh_calloc(estimated_num_entries, sizeof(entryplus3));

	if (begin_cookie == 0) {
		/* Fill in "." */
		res->res_readdir3.status =
			nfs_readdir_dot_entry(dir_obj, ".", 1,
					      nfs3_readdirplus_callback,
					      &tracker, &attrs_dir);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_fail;
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
			goto out_fail;
		}

		res->res_readdir3.status =
		    nfs_readdir_dot_entry(parent_dir_obj,
					  "..",
					  2,
					  nfs3_readdirplus_callback,
					  &tracker,
					  &attrs_parent);

		parent_dir_obj->obj_ops->put_ref(parent_dir_obj);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out_fail;
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
			goto out_fail;
		}

		res->res_readdirplus3.status = nfs3_Errno_status(fsal_status);
		goto out_fail;
	}

	if (tracker.error != NFS3_OK) {
		res->res_readdir3.status = tracker.error;
		goto out_fail;
	}

	if ((num_entries == 0) && (begin_cookie > 1)) {
		res->res_readdirplus3.status = NFS3_OK;
		resok->reply.entries = NULL;
		resok->reply.eof = TRUE;
	} else {
		resok->reply.entries = tracker.entries;
		resok->reply.eof = eod_met;
	}

	nfs_SetPostOpAttr(dir_obj, &resok->dir_attributes, &attrs_dir);

	memcpy(resok->cookieverf, cookie_verifier, sizeof(cookieverf3));

	res->res_readdirplus3.status = NFS3_OK;

	rc = NFS_REQ_OK;

	goto out;

 out_fail:

	/* Set the post op attributes as we have found them. */
	nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes, &attrs_dir);

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs_dir);
	fsal_release_attrs(&attrs_parent);

	if (dir_obj)
		dir_obj->obj_ops->put_ref(dir_obj);

	if (((res->res_readdir3.status != NFS3_OK) || (rc != NFS_REQ_OK))
	    && (tracker.entries != NULL))
		free_entryplus3s(tracker.entries);

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
#define RESREADDIRPLUSREPLY resp->res_readdirplus3.READDIRPLUS3res_u.resok.reply
	if ((resp->res_readdirplus3.status == NFS3_OK)
	    && (RESREADDIRPLUSREPLY.entries != NULL))
		free_entryplus3s(RESREADDIRPLUSREPLY.entries);
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
					const struct attrlist *attr,
					uint64_t mounted_on_fileid,
					uint64_t cookie,
					enum cb_state cb_state)
{
	/* Not-so-opaque pointer to callback data` */
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdirplus_cb_data *tracker = cb_parms->opaque;
	/* Length of the current filename */
	size_t namelen = strlen(cb_parms->name);
	entryplus3 *ep3 = tracker->entries + tracker->count;

	if (tracker->count == tracker->total_entries) {
		cb_parms->in_result = false;
		return ERR_FSAL_NO_ERROR;
	}

	/* This is a pessimistic check, which assumes that we're going
	 * to send attributes and full size handle - if it fails then
	 * we're close enough to the buffer size limit and t's time to
	 * stop anyway */
	if (tracker->mem_left
	    < (sizeof(entryplus3) + namelen + NFS3_FHSIZE)) {
		if (tracker->count == 0)
			tracker->error = NFS3ERR_TOOSMALL;

		cb_parms->in_result = false;
		return ERR_FSAL_NO_ERROR;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		"Callback for %s cookie %"PRIu64,
		cb_parms->name, cookie);

	ep3->fileid = obj->fileid;
	ep3->name = gsh_strdup(cb_parms->name);
	ep3->cookie = cookie;

	/* Account for file name + length + cookie */
	tracker->mem_left -= sizeof(ep3->cookie) + RNDUP(namelen) +
							BYTES_PER_XDR_UNIT;

	if (cb_parms->attr_allowed) {
		ep3->name_handle.handle_follows = TRUE;

		if (!nfs3_FSALToFhandle(true,
					&ep3->name_handle.post_op_fh3_u.handle,
					obj,
					op_ctx->ctx_export)) {
			tracker->error = NFS3ERR_SERVERFAULT;
			gsh_free(ep3->name);
			cb_parms->in_result = false;
			return ERR_FSAL_NO_ERROR;
		}

		/* Account for filehande + length + follows + nextentry */
		tracker->mem_left -=
		    ep3->name_handle.post_op_fh3_u.handle.data.data_len + 12;

		/* Check if attributes follow and then place the attributes
		 * that follow
		 */
		ep3->name_attributes.attributes_follow = nfs3_FSALattr_To_Fattr(
			obj, attr,
			&ep3->name_attributes.post_op_attr_u.attributes);
	} else {
		ep3->name_handle.handle_follows = false;
		ep3->name_attributes.attributes_follow = false;
		tracker->mem_left -= sizeof(ep3->name_handle.handle_follows);
	}

	if (ep3->name_attributes.attributes_follow)
		tracker->mem_left -= sizeof(ep3->name_attributes);
	else
		tracker->mem_left -=
		    sizeof(ep3->name_attributes.attributes_follow);

	if (tracker->count > 0)
		tracker->entries[tracker->count - 1].nextentry = ep3;

	++(tracker->count);
	cb_parms->in_result = true;

	return ERR_FSAL_NO_ERROR;
}				/* nfs3_readdirplus_callback */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIRPLUS
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entryplus3s [in] Pointer to first obj
 */

static void free_entryplus3s(entryplus3 *entryplus3s)
{
	entryplus3 *entry = NULL;

	for (entry = entryplus3s; entry != NULL; entry = entry->nextentry) {
		gsh_free(entry->name);
		gsh_free(entry->name_handle.post_op_fh3_u.handle.data.data_val);
	}

	gsh_free(entryplus3s);
}
