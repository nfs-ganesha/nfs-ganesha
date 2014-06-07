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
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include <assert.h>

cache_inode_status_t nfs3_readdir_callback(void *opaque,
					   cache_entry_t *entry,
					   const struct attrlist *attr,
					   uint64_t mounted_on_fileid);

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
nfsstat3 nfs_readdir_dot_entry(cache_entry_t *entry, const char *name,
			       uint64_t cookie, cache_inode_getattr_cb_t cb,
			       struct nfs3_readdir_cb_data *tracker)
{
	struct cache_inode_readdir_cb_parms cb_parms;
	cache_inode_status_t cache_status;

	cb_parms.opaque = tracker;
	cb_parms.name = name;
	cb_parms.attr_allowed = true;
	cb_parms.cookie = cookie;
	cb_parms.in_result = true;
	cache_status = cb(&cb_parms, entry, &entry->obj_handle->attributes, 0);

	if (cache_status != CACHE_INODE_SUCCESS)
		return nfs3_Errno(cache_status);
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
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_readdir(nfs_arg_t *arg,
		 nfs_worker_data_t *worker,
		 struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *dir_entry = NULL;
	unsigned long count = 0;
	uint64_t cookie = 0;
	uint64_t cache_inode_cookie = 0;
	cookieverf3 cookie_verifier;
	unsigned int num_entries;
	unsigned long estimated_num_entries = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	cache_inode_status_t cache_status_gethandle = CACHE_INODE_SUCCESS;
	int rc = NFS_REQ_OK;
	struct nfs3_readdir_cb_data tracker = { NULL };
	cache_entry_t *parent_dir_entry = NULL;

	if (isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR)) {
		char str[LEN_FH_STR];
		log_components_t component;

		nfs_FhandleToStr(req->rq_vers,
				 &(arg->arg_readdir3.dir),
				 NULL,
				 str);

		if (isDebug(COMPONENT_NFSPROTO))
			component = COMPONENT_NFSPROTO;
		else
			component = COMPONENT_NFS_READDIR;

		LogDebug(component,
			 "REQUEST PROCESSING: Calling nfs_Readdir handle: %s",
			 str);
	}

	READDIR3resok * const RES_READDIR3_OK =
	    &res->res_readdir3.READDIR3res_u.resok;

	/* to avoid setting it on each error case */
	res->res_readdir3.READDIR3res_u.resfail.dir_attributes.
	    attributes_follow = FALSE;

	/* Look up cache entry for filehandle */
	dir_entry = nfs3_FhandleToCache(&(arg->arg_readdir3.dir),
					&(res->res_readdir3.status),
					&rc);
	if (dir_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Extract the filetype */
	dir_filetype = dir_entry->type;
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
	LogFullDebug(COMPONENT_NFS_READDIR,
		     "---> nfs3_readdir: count=%lu  cookie=%" PRIu64 "  "
		     "estimated_num_entries=%lu", count, cookie,
		     estimated_num_entries);
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
	 * This value is the mtime of the directory. If verifier is
	 * unused (as in many NFS Servers) then only a set of zeros
	 * is returned (trivial value).
	 */
	if (op_ctx->export->options & EXPORT_OPTION_USE_COOKIE_VERIFIER)
		memcpy(cookie_verifier,
		       &dir_entry->change_time,
		       sizeof(dir_entry->change_time));

	/* Nothing to do if != 0 because the area is already full of
	 * zero
	 */
	if ((cookie != 0) &&
	    (op_ctx->export->options & EXPORT_OPTION_USE_COOKIE_VERIFIER)) {
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

	if (tracker.entries == NULL) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	tracker.total_entries = estimated_num_entries;
	tracker.mem_left = count - sizeof(READDIR3resok);
	tracker.count = 0;
	tracker.error = NFS3_OK;

	/* Adjust the cookie we supply to cache_inode */
	if (cookie > 2) {	/* it is not the cookie for "." nor ".." */
		cache_inode_cookie = cookie;
	} else {
		cache_inode_cookie = 0;
	}

	/* Fills "."  */
	if (cookie == 0) {
		res->res_readdir3.status =
		    nfs_readdir_dot_entry(dir_entry,
					  ".",
					  1,
					  nfs3_readdir_callback,
					  &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* Fills ".." */
	if ((cookie <= 1) && (estimated_num_entries > 1)) {
		/* Get parent pentry */
		cache_status_gethandle = cache_inode_lookupp(dir_entry,
							     &parent_dir_entry);

		if (parent_dir_entry == NULL) {
			res->res_readdir3.status =
			    nfs3_Errno(cache_status_gethandle);
			rc = NFS_REQ_OK;
			goto out;
		}

		res->res_readdir3.status =
		    nfs_readdir_dot_entry(parent_dir_entry,
					  "..",
					  2,
					  nfs3_readdir_callback,
					  &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out;
		}

		cache_inode_put(parent_dir_entry);
		parent_dir_entry = NULL;
	}

	/* Call readdir */
	cache_status = cache_inode_readdir(dir_entry,
					   cache_inode_cookie,
					   &num_entries,
					   &eod_met,
					   0,	/* no attr */
					   nfs3_readdir_callback,
					   &tracker);

	if (cache_status != CACHE_INODE_SUCCESS) {
		if (nfs_RetryableError(cache_status)) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		res->res_readdir3.status = nfs3_Errno(cache_status);
		nfs_SetPostOpAttr(dir_entry,
				  &res->res_readdir3.READDIR3res_u.resfail.
				  dir_attributes);
		goto out;
	}

	if (tracker.error != NFS3_OK) {
		res->res_readdir3.status = tracker.error;
		nfs_SetPostOpAttr(dir_entry,
				  &res->res_readdir3.READDIR3res_u.resfail.
				  dir_attributes);
		goto out;
	}

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "-- Readdir -> Call to " "cache_inode_readdir(cookie=%"
		     PRIu64 " -> num_entries = %u", cache_inode_cookie,
		     num_entries);

	RES_READDIR3_OK->reply.entries = tracker.entries;
	RES_READDIR3_OK->reply.eof = eod_met;
	nfs_SetPostOpAttr(dir_entry, &RES_READDIR3_OK->dir_attributes);
	memcpy(RES_READDIR3_OK->cookieverf, cookie_verifier,
	       sizeof(cookieverf3));
	res->res_readdir3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (dir_entry)
		cache_inode_put(dir_entry);

	if (parent_dir_entry)
		cache_inode_put(parent_dir_entry);

	/* Deallocate memory in the event of an error */
	if (((res->res_readdir3.status != NFS3_OK) || (rc != NFS_REQ_OK))
	    && (tracker.entries != NULL)) {
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
	if ((resp->res_readdir3.status == NFS3_OK)
	    && (resp->res_readdir3.READDIR3res_u.resok.reply.entries != NULL)) {
		free_entry3s(resp->res_readdir3.READDIR3res_u.resok.reply.
			     entries);
	}
}

/**
 * @brief Populate entry3s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entry3 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdir_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

cache_inode_status_t nfs3_readdir_callback(void *opaque,
					   cache_entry_t *entry,
					   const struct attrlist *attr,
					   uint64_t mounted_on_fileid)
{
	/* Not-so-opaque pointer to callback data` */
	struct cache_inode_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdir_cb_data *tracker = cb_parms->opaque;
	/* Length of the current filename */
	size_t namelen = strlen(cb_parms->name);
	entry3 *e3 = tracker->entries + tracker->count;
	size_t need =
	    sizeof(entry3) + ((namelen + 3) & ~3) + 4 - sizeof(char *) -
	    sizeof(entry3 *);

	if (tracker->count == tracker->total_entries) {
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}
	if ((tracker->mem_left < need)) {
		if (tracker->count == 0)
			tracker->error = NFS3ERR_TOOSMALL;

		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}

	e3->fileid = attr->fileid;
	e3->name = gsh_strdup(cb_parms->name);
	if (e3->name == NULL) {
		tracker->error = NFS3ERR_SERVERFAULT;
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}
	e3->cookie = cb_parms->cookie;

	if (tracker->count > 0)
		tracker->entries[tracker->count - 1].nextentry = e3;

	tracker->mem_left -= need;
	++(tracker->count);
	cb_parms->in_result = true;
	return CACHE_INODE_SUCCESS;
}				/* */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIR
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entry3s [in] Pointer to first entry
 */

static void free_entry3s(entry3 *entry3s)
{
	entry3 *entry = NULL;

	for (entry = entry3s; entry != NULL; entry = entry->nextentry)
		gsh_free(entry->name);

	gsh_free(entry3s);

	return;
}				/* free_entry3s */
