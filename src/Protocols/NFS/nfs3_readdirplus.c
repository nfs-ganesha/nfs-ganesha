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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include <assert.h>

cache_inode_status_t nfs3_readdirplus_callback(void *opaque,
					       cache_entry_t *entry,
					       const struct attrlist *attr,
					       uint64_t mounted_on_fileid);

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
nfsstat3 nfs_readdir_dot_entry(cache_entry_t *entry, const char *name,
			       uint64_t cookie, cache_inode_getattr_cb_t cb,
			       struct nfs3_readdirplus_cb_data *tracker)
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
 * @brief The NFSPROC3_READDIRPLUS
 *
 * Implements the NFSPROC3_READDIRPLUS function
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_readdirplus(nfs_arg_t *arg,
		     nfs_worker_data_t *worker,
		     struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *dir_entry = NULL;
	uint64_t begin_cookie = 0;
	uint64_t cache_inode_cookie = 0;
	cookieverf3 cookie_verifier;
	unsigned int num_entries = 0;
	unsigned long estimated_num_entries = 0;
	object_file_type_t dir_filetype = 0;
	bool eod_met = false;
	cache_inode_status_t cache_status = 0;
	cache_inode_status_t cache_status_gethandle = 0;
	int rc = NFS_REQ_OK;
	struct nfs3_readdirplus_cb_data tracker = {
		.entries = NULL,
		.mem_left = 0,
		.count = 0,
		.error = NFS3_OK,
	};

	if (isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR)) {
		char str[LEN_FH_STR];
		log_components_t component;

		sprint_fhandle3(str, &(arg->arg_readdirplus3.dir));

		if (isDebug(COMPONENT_NFSPROTO))
			component = COMPONENT_NFSPROTO;
		else
			component = COMPONENT_NFS_READDIR;

		LogDebug(component,
			 "REQUEST PROCESSING: Calling nfs3_readdirplus handle: %s",
			 str);
	}

	/* to avoid setting it on each error case */
	res->res_readdir3.READDIR3res_u.resfail.dir_attributes.
	    attributes_follow = FALSE;

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

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "nfs3_readdirplus: dircount=%u " "begin_cookie=%" PRIu64
		     " " "estimated_num_entries=%lu, mem_left=%zd",
		     arg->arg_readdirplus3.dircount, begin_cookie,
		     estimated_num_entries, tracker.mem_left);

	/* Convert file handle into a vnode */
	dir_entry = nfs3_FhandleToCache(&(arg->arg_readdirplus3.dir),
					  &(res->res_readdirplus3.status),
					  &rc);

	if (dir_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Extract the filetype */
	dir_filetype = dir_entry->type;

	/* Sanity checks -- must be a directory */

	if (dir_filetype != DIRECTORY) {
		res->res_readdirplus3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	memset(cookie_verifier, 0, sizeof(cookieverf3));

	/* If cookie verifier is used, then an non-trivial value is
	   returned to the client This value is the mtime of the
	   directory. If verifier is unused (as in many NFS Servers) then
	   only a set of zeros is returned (trivial value) */

	if (op_ctx->export->options & EXPORT_OPTION_USE_COOKIE_VERIFIER)
		memcpy(cookie_verifier,
		       &(dir_entry->change_time),
		       sizeof(dir_entry->change_time));

	if (op_ctx->export->options & EXPORT_OPTION_USE_COOKIE_VERIFIER
	    && (begin_cookie != 0)) {
		/* Not the first call, so we have to check the cookie
		   verifier */
		if (memcmp(cookie_verifier,
			   arg->arg_readdirplus3.cookieverf,
			   NFS3_COOKIEVERFSIZE) != 0) {
			res->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
	res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

	/* Fudge cookie for "." and "..", if necessary */
	if (begin_cookie > 2)
		cache_inode_cookie = begin_cookie;
	else
		cache_inode_cookie = 0;

	/* Allocate space for entries */
	tracker.entries = gsh_calloc(estimated_num_entries, sizeof(entryplus3));

	if (tracker.entries == NULL) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	if (begin_cookie == 0) {
		/* Fill in "." */
		res->res_readdir3.status =
		    nfs_readdir_dot_entry(dir_entry,
					  ".",
					  1,
					  nfs3_readdirplus_callback,
					  &tracker);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* Fill in ".." */
	if (begin_cookie <= 1) {
		cache_entry_t *parent_dir_entry = NULL;
		cache_status_gethandle = cache_inode_lookupp(dir_entry,
							     &parent_dir_entry);

		if (parent_dir_entry == NULL) {
			res->res_readdirplus3.status =
			    nfs3_Errno(cache_status_gethandle);
			rc = NFS_REQ_OK;
			goto out;
		}

		res->res_readdir3.status =
		    nfs_readdir_dot_entry(parent_dir_entry,
					  "..",
					  2,
					  nfs3_readdirplus_callback,
					  &tracker);

		cache_inode_put(parent_dir_entry);

		if (res->res_readdir3.status != NFS3_OK) {
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* Call readdir */
	cache_status = cache_inode_readdir(dir_entry,
					   cache_inode_cookie,
					   &num_entries,
					   &eod_met,
					   ATTRS_NFS3,
					   nfs3_readdirplus_callback,
					   &tracker);

	if (cache_status != CACHE_INODE_SUCCESS) {
		/* Is this a retryable error */
		if (nfs_RetryableError(cache_status)) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		res->res_readdirplus3.status = nfs3_Errno(cache_status);
		nfs_SetPostOpAttr(dir_entry,
				  &res->res_readdirplus3.READDIRPLUS3res_u.
				  resfail.dir_attributes);

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
		     "Readdirplus3 -> Call to cache_inode_readdir( cookie=%"
		     PRIu64 ") -> num_entries = %u", cache_inode_cookie,
		     num_entries);

	if ((num_entries == 0) && (begin_cookie > 1)) {
		res->res_readdirplus3.status = NFS3_OK;
		res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
		    NULL;
		res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;

		nfs_SetPostOpAttr(dir_entry,
				  &res->res_readdirplus3.READDIRPLUS3res_u.
				  resok.dir_attributes);

		memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
		       cookie_verifier,
		       sizeof(cookieverf3));
	} else {
		res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
		    tracker.entries;
		res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof =
		    eod_met;
	}

	nfs_SetPostOpAttr(dir_entry,
			  &res->res_readdirplus3.READDIRPLUS3res_u.resok.
			  dir_attributes);

	memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
	       cookie_verifier,
	       sizeof(cookieverf3));

	res->res_readdirplus3.status = NFS3_OK;

	memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
	       cookie_verifier,
	       sizeof(cookieverf3));

	rc = NFS_REQ_OK;

 out:
	if (dir_entry)
		cache_inode_put(dir_entry);

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
 * @brief Populate entryplus3s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entryplys3 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdirplus_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

cache_inode_status_t nfs3_readdirplus_callback(void *opaque,
					       cache_entry_t *entry,
					       const struct attrlist *attr,
					       uint64_t mounted_on_fileid)
{
	/* Not-so-opaque pointer to callback data` */
	struct cache_inode_readdir_cb_parms *cb_parms = opaque;
	struct nfs3_readdirplus_cb_data *tracker = cb_parms->opaque;
	/* Length of the current filename */
	size_t namelen = strlen(cb_parms->name);
	entryplus3 *ep3 = tracker->entries + tracker->count;

	if (tracker->count == tracker->total_entries) {
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
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
		return CACHE_INODE_SUCCESS;
	}

	ep3->fileid = attr->fileid;
	ep3->name = gsh_strdup(cb_parms->name);
	if (ep3->name == NULL) {
		tracker->error = NFS3ERR_SERVERFAULT;
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}
	ep3->cookie = cb_parms->cookie;

	/* Account for file name + length + cookie */
	tracker->mem_left -= sizeof(ep3->cookie) + ((namelen + 3) & ~3) + 4;

	if (cb_parms->attr_allowed) {
		ep3->name_handle.handle_follows = TRUE;
		ep3->name_handle.post_op_fh3_u.handle.data.data_val =
		    gsh_malloc(NFS3_FHSIZE);
		if (ep3->name_handle.post_op_fh3_u.handle.data.data_val
		    == NULL) {
			LogEvent(COMPONENT_NFS_READDIR,
				 "FAILED to allocate FH");
			tracker->error = NFS3ERR_SERVERFAULT;
			gsh_free(ep3->name);
			cb_parms->in_result = false;
			return CACHE_INODE_SUCCESS;
		}

		if (!nfs3_FSALToFhandle(&ep3->name_handle.post_op_fh3_u.handle,
					entry->obj_handle,
					op_ctx->export)) {
			tracker->error = NFS3ERR_SERVERFAULT;
			gsh_free(ep3->name);
			gsh_free(ep3->name_handle.post_op_fh3_u.handle.data.
				 data_val);
			cb_parms->in_result = false;
			return CACHE_INODE_SUCCESS;
		}

		/* Account for filehande + length + follows + nextentry */
		tracker->mem_left -=
		    ep3->name_handle.post_op_fh3_u.handle.data.data_len + 12;

		ep3->name_attributes.attributes_follow =
		    nfs3_FSALattr_To_Fattr(op_ctx->export,
					   attr,
					   &(ep3->name_attributes.
					     post_op_attr_u.attributes));
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

	return CACHE_INODE_SUCCESS;
}				/* nfs3_readdirplus_callback */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIRPLUS
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entryplus3s [in] Pointer to first entry
 */

static void free_entryplus3s(entryplus3 *entryplus3s)
{
	entryplus3 *entry = NULL;

	for (entry = entryplus3s; entry != NULL; entry = entry->nextentry) {
		gsh_free(entry->name);
		gsh_free(entry->name_handle.post_op_fh3_u.handle.data.data_val);
	}

	gsh_free(entryplus3s);

	return;
}				/* free_entryplus3s */
