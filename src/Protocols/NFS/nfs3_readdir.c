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
				    const struct attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state);

static void free_entry3s(entry3 *entry3s);

/**
 * @brief Opaque bookkeeping structure for NFSv3 readdir
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIR response between calls to nfs3_readdir_callback.
 */

struct nfs3_readdir_cb_data {
	entry3 *entries;	/*< The array holding individual entries */
	size_t mem_left;	/*< The amount of memory remaining before we
				   hit maxcount */
	size_t count;		/*< The count of complete entries stored in the
				   buffer */
	size_t total_entries;	/*< The total number of entries in the
				   array */
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
	unsigned long count = 0;
	uint64_t cookie = 0;
	uint64_t fsal_cookie = 0;
	cookieverf3 cookie_verifier;
	unsigned int num_entries = 0;
	unsigned long estimated_num_entries = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	fsal_status_t fsal_status = {0, 0};
	fsal_status_t fsal_status_gethandle = {0, 0};
	int rc = NFS_REQ_OK;
	struct nfs3_readdir_cb_data tracker = { NULL };
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);
	READDIR3resfail *resfail = &res->res_readdir3.READDIR3res_u.resfail;

	if (isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &(arg->arg_readdir3.dir),
				 NULL,
				 str);

		LogDebugAlt(COMPONENT_NFSPROTO, COMPONENT_NFS_READDIR,
			    "REQUEST PROCESSING: Calling nfs_Readdir handle: %s",
			    str);
	}

	READDIR3resok * const RES_READDIR3_OK =
	    &res->res_readdir3.READDIR3res_u.resok;

	/* to avoid setting it on each error case */
	resfail->dir_attributes.attributes_follow = FALSE;

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

	count = arg->arg_readdir3.count;
	cookie = arg->arg_readdir3.cookie;
	estimated_num_entries =
	    MIN(count / (sizeof(entry3) - sizeof(char *)), 120);
	LogDebug(COMPONENT_NFS_READDIR,
		 "---> NFS3_READDIR: count=%lu  cookie=%" PRIu64
		 " estimated_num_entries=%lu",
		 count, cookie, estimated_num_entries);
	if (estimated_num_entries == 0) {
		res->res_readdir3.status = NFS3ERR_TOOSMALL;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* To make or check the cookie verifier */
	memset(cookie_verifier, 0, sizeof(cookieverf3));

	/* If cookie verifier is used, then a
	 * non-trivial value is returned to the
	 * client.
	 *
	 * This value is the ctime of the directory. If verifier is
	 * unused (as in many NFS Servers) then only a set of zeros
	 * is returned (trivial value).
	 */
	if (use_cookie_verifier) {
		struct attrlist attrs;

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

		memcpy(cookie_verifier,
		       &attrs.ctime.tv_sec,
		       sizeof(attrs.ctime.tv_sec));

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

	tracker.entries = gsh_calloc(estimated_num_entries, sizeof(entry3));
	tracker.total_entries = estimated_num_entries;
	tracker.mem_left = count - sizeof(READDIR3resok);
	tracker.count = 0;
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
			goto out;
		}
	}

	/* Fills ".." */
	if ((cookie <= 1) && (estimated_num_entries > 1)) {
		/* Get parent pentry */
		fsal_status_gethandle = fsal_lookupp(dir_obj,
						     &parent_dir_obj,
						     NULL);

		if (parent_dir_obj == NULL) {
			res->res_readdir3.status =
			    nfs3_Errno_status(fsal_status_gethandle);
			rc = NFS_REQ_OK;
			goto out;
		}

		res->res_readdir3.status = nfs_readdir_dot_entry(parent_dir_obj,
				"..", 2, nfs3_readdir_callback, &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out;
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
			goto out;
		}

		res->res_readdir3.status = nfs3_Errno_status(fsal_status);
		nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes, NULL);
		goto out;
	}

	if (tracker.error != NFS3_OK) {
		res->res_readdir3.status = tracker.error;
		nfs_SetPostOpAttr(dir_obj, &resfail->dir_attributes, NULL);
		goto out;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "-- Readdir -> Call to fsal_readdir(cookie=%"
		 PRIu64 ")",
		 fsal_cookie);

	if ((num_entries == 0) && (cookie > 1)) {
		RES_READDIR3_OK->reply.entries = NULL;
		RES_READDIR3_OK->reply.eof = TRUE;
	} else {
		RES_READDIR3_OK->reply.entries = tracker.entries;
		RES_READDIR3_OK->reply.eof = eod_met;
	}
	nfs_SetPostOpAttr(dir_obj, &RES_READDIR3_OK->dir_attributes, NULL);
	memcpy(RES_READDIR3_OK->cookieverf, cookie_verifier,
	       sizeof(cookieverf3));
	res->res_readdir3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (dir_obj)
		dir_obj->obj_ops->put_ref(dir_obj);

	if (parent_dir_obj)
		parent_dir_obj->obj_ops->put_ref(parent_dir_obj);

	/* Deallocate memory in the event of an error */
	if (((res->res_readdir3.status != NFS3_OK) || (rc != NFS_REQ_OK) ||
	    ((num_entries == 0) && (cookie > 1))) &&
	    (tracker.entries != NULL)) {
		free_entry3s(tracker.entries);
		RES_READDIR3_OK->reply.entries = NULL;
	}

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
	if (resp->res_readdir3.status == NFS3_OK) {
		free_entry3s(
			resp->res_readdir3.READDIR3res_u.resok.reply.entries);
	}
}

/**
 * @brief Populate entry3s when called from fsal_readdir
 *
 * This function is a callback passed to fsal_readdir.  It
 * fills in a pre-allocated array of entry3 structures and allocates
 * space for the name and attributes.  This space must be freed.
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
				    const struct attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state)
{
	/* Not-so-opaque pointer to callback data` */
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdir_cb_data *tracker = cb_parms->opaque;
	/* Length of the current filename */
	size_t namelen = strlen(cb_parms->name);
	entry3 *e3 = tracker->entries + tracker->count;
	size_t need = sizeof(entry3) + RNDUP(namelen) + BYTES_PER_XDR_UNIT
					- sizeof(char *) - sizeof(entry3 *);

	if (tracker->count == tracker->total_entries) {
		cb_parms->in_result = false;
		return ERR_FSAL_NO_ERROR;
	}
	if (tracker->mem_left < need) {
		if (tracker->count == 0)
			tracker->error = NFS3ERR_TOOSMALL;

		cb_parms->in_result = false;
		return ERR_FSAL_NO_ERROR;
	}

	e3->fileid = obj->fileid;
	e3->name = gsh_strdup(cb_parms->name);
	e3->cookie = cookie;

	if (tracker->count > 0)
		tracker->entries[tracker->count - 1].nextentry = e3;

	tracker->mem_left -= need;
	++(tracker->count);
	cb_parms->in_result = true;
	return ERR_FSAL_NO_ERROR;
}				/* */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIR
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entry3s [in] Pointer to first obj
 */

static void free_entry3s(entry3 *entry3s)
{
	entry3 *entry = NULL;

	for (entry = entry3s; entry != NULL; entry = entry->nextentry)
		gsh_free(entry->name);

	gsh_free(entry3s);
}
