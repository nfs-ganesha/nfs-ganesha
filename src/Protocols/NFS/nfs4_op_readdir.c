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
 * --------------------------------------- */
#include "config.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 * @brief Opaque bookkeeping structure for NFSv4 readdir
 *
 * This structure keeps track of the process of writing out an NFSv4
 * READDIR response between calls to nfs4_readdir_callback.
 */

struct nfs4_readdir_cb_data {
	entry4 *entries;	/*< The array holding individual entries */
	size_t mem_left;	/*< The amount of memory remaining before we
				   hit maxcount */
	size_t count;		/*< The count of complete entries stored in the
				   buffer */
	size_t total_entries;	/*< The total number of entries available
				   in the array */
	nfsstat4 error;		/*< Set to a value other than NFS4_OK if the
				   callback function finds a fatal error. */
	struct bitmap4 *req_attr;	/*< The requested attributes */
	compound_data_t *data;	/*< The compound data, so we can produce
				   nfs_fh4s. */
};

/**
 * @brief Populate entry4s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entry4 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param[in,out] opaque A struct nfs4_readdir_cb_data that stores the
 *                       location of the array and other bookeeping
 *                       information
 * @param[in]     name   The filename for the current entry
 * @param[in]     handle The current entry's filehandle
 * @param[in]     attrs  The current entry's attributes
 * @param[in]     cookie The readdir cookie for the current entry
 */

cache_inode_status_t nfs4_readdir_callback(void *opaque,
					   const struct attrlist *attr,
					   uint64_t mounted_on_fileid)
{
	struct cache_inode_readdir_cb_parms *cb_parms = opaque;
	struct nfs4_readdir_cb_data *tracker = cb_parms->opaque;
	size_t namelen = 0;
	char val_fh[NFS4_FHSIZE];
	nfs_fh4 entryFH = {
		.nfs_fh4_len = 0,
		.nfs_fh4_val = val_fh
	};
	struct xdr_attrs_args args;
	compound_data_t *data = tracker->data;
	nfsstat4 rdattr_error = NFS4_OK;
	entry4 *tracker_entry = tracker->entries + tracker->count;
	cache_inode_status_t attr_status;
	fsal_accessflags_t access_mask_attr = 0;
	cache_entry_t *entry = cb_parms->entry;

	if (tracker->total_entries == tracker->count)
		goto failure;

	memset(val_fh, 0, NFS4_FHSIZE);

	/* Bits that don't require allocation */
	if (tracker->mem_left < sizeof(entry4)) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= sizeof(entry4);
	tracker_entry->cookie = cb_parms->cookie;
	tracker_entry->nextentry = NULL;

	/* The filename.  We don't use str2utf8 because that has an
	 * additional copy into a buffer before copying into the
	 * destination.
	 */
	namelen = strlen(cb_parms->name);

	if (tracker->mem_left < (namelen + 1)) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= (namelen);
	tracker_entry->name.utf8string_len = namelen;
	tracker_entry->name.utf8string_val = gsh_malloc(namelen);

	if (tracker_entry->name.utf8string_val == NULL) {
		/* Could not allocate name */
		goto server_fault;
	}

	memcpy(tracker_entry->name.utf8string_val,
	       cb_parms->name,
	       namelen);

	/* If we carried an error from above, now that we have
	 * the name set up, go ahead and try and put error in
	 * results.
	 */
	if (rdattr_error != NFS4_OK)
		goto skip;

	if (cb_parms->attr_allowed
	    && attribute_is_set(tracker->req_attr, FATTR4_FILEHANDLE)) {
		if (!nfs4_FSALToFhandle(&entryFH, entry->obj_handle))
			goto server_fault;
	}

	if (!cb_parms->attr_allowed) {
		/* cache_inode_readdir is signaling us that client didn't have
		 * search permission in this directory, so we can't return any
		 * attributes, but must indicate NFS4ERR_ACCESS.
		 */
		rdattr_error = NFS4ERR_ACCESS;
		goto skip;
	}

	/* Adjust access mask if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if (attribute_is_set(tracker->req_attr, FATTR4_ACL))
		access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);

	/* cache_inode_readdir holds attr_lock while making callback,
	 * so we need to do access check with no mutex.
	 */
	attr_status =
	    cache_inode_access_no_mutex(entry, access_mask_attr, data->req_ctx);

	if (attr_status != CACHE_INODE_SUCCESS) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "permission check for attributes status=%s",
			     cache_inode_err_str(attr_status));

		rdattr_error = nfs4_Errno(attr_status);
		goto skip;
	}

	memset(&args, 0, sizeof(args));
	args.attrs = (struct attrlist *)attr;
	args.data = data;
	args.hdl4 = &entryFH;
	args.mounted_on_fileid = mounted_on_fileid;

	if (nfs4_FSALattr_To_Fattr(&args,
				   tracker->req_attr,
				   &tracker_entry->attrs) != 0) {
		LogCrit(COMPONENT_NFS_V4,
			"nfs4_FSALattr_To_Fattr failed to convert attr");
		goto server_fault;
	}

 skip:

	if (rdattr_error != NFS4_OK) {
		if (!attribute_is_set(tracker->req_attr, FATTR4_RDATTR_ERROR)) {
			tracker->error = rdattr_error;
			goto failure;
		}

		if (nfs4_Fattr_Fill_Error(&tracker_entry->attrs,
					  rdattr_error) == -1)
			goto server_fault;
	}

	if (tracker->mem_left <
	    ((tracker_entry->attrs.attrmask.bitmap4_len * sizeof(uint32_t))
	     + (tracker_entry->attrs.attr_vals.attrlist4_len))) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= tracker_entry->attrs.attrmask.bitmap4_len *
			     sizeof(uint32_t);

	tracker->mem_left -= tracker_entry->attrs.attr_vals.attrlist4_len;

	if (tracker->count != 0)
		tracker->entries[tracker->count - 1].nextentry = tracker_entry;

	++(tracker->count);
	cb_parms->in_result = true;
	goto out;

 server_fault:

	tracker->error = NFS4ERR_SERVERFAULT;

 failure:

	if (tracker_entry->attrs.attr_vals.attrlist4_val != NULL) {
		gsh_free(tracker_entry->attrs.attr_vals.attrlist4_val);
		tracker_entry->attrs.attr_vals.attrlist4_val = NULL;
	}

	if (tracker_entry->name.utf8string_val != NULL) {
		gsh_free(tracker_entry->name.utf8string_val);
		tracker_entry->name.utf8string_val = NULL;
	}

	cb_parms->in_result = false;

 out:

	return CACHE_INODE_SUCCESS;
}

/**
 * @brief Free a list of entry4s
 *
 * This function frees a list of entry4s and all dependent strctures.
 *
 * @param[in,out] entries The entries to be freed
 */

static void free_entries(entry4 *entries)
{
	entry4 *entry = NULL;

	for (entry = entries; entry != NULL; entry = entry->nextentry) {
		if (entry->attrs.attr_vals.attrlist4_val != NULL)
			gsh_free(entry->attrs.attr_vals.attrlist4_val);

		if (entry->name.utf8string_val != NULL)
			gsh_free(entry->name.utf8string_val);
	}
	gsh_free(entries);

	return;
}				/* free_entries */

/**
 * @brief NFS4_OP_READDIR
 *
 * Implements the NFS4_OP_READDIR opeartion. If fh is a pseudo FH,
 * then call is routed to routine nfs4_op_readdir_pseudo
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 371-2
 *
 */
int nfs4_op_readdir(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	READDIR4args * const arg_READDIR4 = &op->nfs_argop4_u.opreaddir;
	READDIR4res * const res_READDIR4 = &resp->nfs_resop4_u.opreaddir;
	cache_entry_t *dir_entry = NULL;
	bool eod_met = false;
	unsigned long dircount = 0;
	unsigned long maxcount = 0;
	entry4 *entries = NULL;
	verifier4 cookie_verifier;
	uint64_t cookie = 0;
	unsigned int estimated_num_entries = 0;
	unsigned int num_entries = 0;
	struct nfs4_readdir_cb_data tracker;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	attrmask_t attrmask;

	resp->resop = NFS4_OP_READDIR;
	res_READDIR4->status = NFS4_OK;

	res_READDIR4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_READDIR4->status != NFS4_OK)
		goto out;

	/* Pseudo Fs management */
	if (nfs4_Is_Fh_Pseudo(&(data->currentFH))) {
		res_READDIR4->status = nfs4_op_readdir_pseudo(op, data, resp);
		goto out;
	}

	memset(&tracker, sizeof(tracker), 0);

	dir_entry = data->current_entry;

	/* get the characteristic value for readdir operation */
	dircount = arg_READDIR4->dircount;
	maxcount = (arg_READDIR4->maxcount * 9) / 10;
	cookie = arg_READDIR4->cookie;

	/* Dircount is considered meaningless by many nfsv4 client (like the
	 * CITI one).  we use maxcount instead.
	 *
	 * The Linux 3.0, 3.1.0 clients vs. TCP Ganesha comes out 10x slower
	 * with 500 max entries
	 */
	estimated_num_entries = 50;
	tracker.total_entries = estimated_num_entries;

	LogFullDebug(COMPONENT_NFS_V4,
		     "--- nfs4_op_readdir ---> dircount=%lu maxcount=%lu "
		     "cookie=%" PRIu64 " estimated_num_entries=%u",
		     dircount, maxcount, cookie, estimated_num_entries);

	/* Since we never send a cookie of 1 or 2, we shouldn't ever get
	 * them back.
	 */
	if (cookie == 1 || cookie == 2) {
		res_READDIR4->status = NFS4ERR_BAD_COOKIE;
		goto out;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access_Bitmap
	    (&arg_READDIR4->attr_request, FATTR4_ATTR_READ)) {
		res_READDIR4->status = NFS4ERR_INVAL;
		goto out;
	}

	/* If maxcount is too short (14 should be enough for an empty
	 * directory) return NFS4ERR_TOOSMALL
	 */
	if (maxcount < 14 || estimated_num_entries == 0) {
		res_READDIR4->status = NFS4ERR_TOOSMALL;
		goto out;
	}

	/* If a cookie verifier is used, then a non-trivial value is
	 * returned to the client.  This value is the mtime of the
	 * directory.  If verifier is unused (as in many NFS Servers)
	 * then only a set of zeros is returned (trivial value)
	 */
	memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);

	/* Cookie delivered by the server and used by the client SHOULD
	 * not be 0, 1 or 2 because these values are reserved (see RFC
	 * 3530, p. 192/RFC 5661, p468).
	 *
	 * 0 - cookie for first READDIR
	 * 1 - reserved for . on client
	 * 2 - reserved for .. on client
	 *
	 * '.' and '..' are not returned, so all cookies will be offset by 2
	 */

	if ((cookie != 0) && (data->export->UseCookieVerifier == 1)) {
		if (memcmp(cookie_verifier,
			   arg_READDIR4->cookieverf,
			   NFS4_VERIFIER_SIZE) != 0) {
			res_READDIR4->status = NFS4ERR_BAD_COOKIE;
			goto out;
		}
	}

	/* Prepare to read the entries */

	entries = gsh_calloc(estimated_num_entries, sizeof(entry4));
	tracker.entries = entries;
	tracker.mem_left = maxcount - sizeof(READDIR4resok);
	tracker.count = 0;
	tracker.error = NFS4_OK;
	tracker.req_attr = &arg_READDIR4->attr_request;
	tracker.data = data;

	/* Assume we need at least the NFS v3 attr.
	 * Any attr is sufficient for permission checking.
	 */
	attrmask = ATTRS_NFS3;

	/* If ACL is requested, we need to add that for permission checking. */
	if (attribute_is_set(tracker.req_attr, FATTR4_ACL))
		attrmask |= ATTR_ACL;

	/* Perform the readdir operation */
	cache_status = cache_inode_readdir(dir_entry,
					   cookie,
					   &num_entries,
					   &eod_met,
					   data->req_ctx,
					   attrmask,
					   nfs4_readdir_callback,
					   &tracker);

	if (cache_status != CACHE_INODE_SUCCESS) {
		res_READDIR4->status = nfs4_Errno(cache_status);
		goto out;
	}

	res_READDIR4->status = tracker.error;

	if (res_READDIR4->status != NFS4_OK)
		goto out;

	if (tracker.count != 0) {
		/* Put the entry's list in the READDIR reply if
		 * there were any.
		 */
		res_READDIR4->READDIR4res_u.resok4.reply.entries = entries;
	} else {
		gsh_free(entries);
		entries = NULL;
		res_READDIR4->READDIR4res_u.resok4.reply.entries = NULL;
	}

	/* This slight bit of oddness is caused by most booleans
	 * throughout Ganesha being of C99's bool type (taking the values
	 * true and false), but fields in XDR being of the older bool_t
	 * type i(taking the values TRUE and FALSE)
	 */
	if (eod_met)
		res_READDIR4->READDIR4res_u.resok4.reply.eof = TRUE;
	else
		res_READDIR4->READDIR4res_u.resok4.reply.eof = FALSE;

	/* Do not forget to set the verifier */
	memcpy(res_READDIR4->READDIR4res_u.resok4.cookieverf,
	       cookie_verifier,
	       NFS4_VERIFIER_SIZE);

	res_READDIR4->status = NFS4_OK;

 out:
	if ((res_READDIR4->status != NFS4_OK) && (entries != NULL))
		free_entries(entries);

	return res_READDIR4->status;
}				/* nfs4_op_readdir */

/**
 * @brief Free memory allocated for READDIR result
 *
 * This function frees any memory allocated for the results of the
 * NFS4_OP_READDIR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_readdir_Free(nfs_resop4 *res)
{
	READDIR4res *resp = &res->nfs_resop4_u.opreaddir;

	free_entries(resp->READDIR4res_u.resok4.reply.entries);
}				/* nfs4_op_readdir_Free */
