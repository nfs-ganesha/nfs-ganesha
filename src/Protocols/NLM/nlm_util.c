// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 * * --------------------------
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
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "nlm_util.h"
#include "nsm.h"
#include "nlm_async.h"
#include "nfs_core.h"
#include "export_mgr.h"

/* nlm grace time tracking */
static struct timeval nlm_grace_tv;

/* We manage our own cookie for GRANTED call backs
 * Cookie
 */
struct granted_cookie {
	unsigned long gc_seconds;
	unsigned long gc_microseconds;
	unsigned long gc_cookie;
};

struct granted_cookie granted_cookie;
pthread_mutex_t granted_mutex = PTHREAD_MUTEX_INITIALIZER;

void next_granted_cookie(struct granted_cookie *cookie)
{
	PTHREAD_MUTEX_lock(&granted_mutex);
	granted_cookie.gc_cookie++;
	*cookie = granted_cookie;
	PTHREAD_MUTEX_unlock(&granted_mutex);
}

const char *lock_result_str(int rc)
{
	switch (rc) {
	case NLM4_GRANTED:
		return "NLM4_GRANTED";
	case NLM4_DENIED:
		return "NLM4_DENIED";
	case NLM4_DENIED_NOLOCKS:
		return "NLM4_DENIED_NOLOCKS";
	case NLM4_BLOCKED:
		return "NLM4_BLOCKED";
	case NLM4_DENIED_GRACE_PERIOD:
		return "NLM4_DENIED_GRACE_PERIOD";
	case NLM4_DEADLCK:
		return "NLM4_DEADLCK";
	case NLM4_ROFS:
		return "NLM4_ROFS";
	case NLM4_STALE_FH:
		return "NLM4_STALE_FH";
	case NLM4_FBIG:
		return "NLM4_FBIG";
	case NLM4_FAILED:
		return "NLM4_FAILED";
	default:
		return "Unknown";
	}
}

inline uint64_t lock_end(uint64_t start, uint64_t len)
{
	if (len == 0)
		return UINT64_MAX;
	else
		return start + len - 1;
}

void fill_netobj(netobj *dst, char *data, int len)
{
	dst->n_len = 0;
	dst->n_bytes = NULL;
	if (len != 0) {
		dst->n_bytes = gsh_malloc(len);
		dst->n_len = len;
		memcpy(dst->n_bytes, data, len);
	}
}

void copy_netobj(netobj *dst, netobj *src)
{
	if (src->n_len != 0) {
		dst->n_bytes = gsh_malloc(src->n_len);
		memcpy(dst->n_bytes, src->n_bytes, src->n_len);
	} else
		dst->n_bytes = NULL;

	dst->n_len = src->n_len;
}

void netobj_free(netobj *obj)
{
	gsh_free(obj->n_bytes);
}

void netobj_to_string(netobj *obj, char *buffer, int maxlen)
{
	int len = obj->n_len;
	struct display_buffer dspbuf = {maxlen, buffer, buffer};

	display_opaque_value(&dspbuf, obj->n_bytes, len);
}

void nlm_init(void)
{
	/* start NLM grace period */
	gettimeofday(&nlm_grace_tv, NULL);

	/* also use this time to initialize granted_cookie */
	granted_cookie.gc_seconds = (unsigned long)nlm_grace_tv.tv_sec;
	granted_cookie.gc_microseconds = (unsigned long)nlm_grace_tv.tv_usec;
	granted_cookie.gc_cookie = 0;
}

void free_grant_arg(state_async_queue_t *arg)
{
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;
	netobj_free(&nlm_arg->nlm_async_args.nlm_async_grant.cookie);
	netobj_free(&nlm_arg->nlm_async_args.nlm_async_grant.alock.oh);
	netobj_free(&nlm_arg->nlm_async_args.nlm_async_grant.alock.fh);
	gsh_free(nlm_arg->nlm_async_args.nlm_async_grant.alock.caller_name);
	gsh_free(arg);
}

/**
 *
 * nlm4_send_grant_msg: Send NLMPROC4_GRANTED_MSG
 *
 * This runs in the nlm_asyn_thread context.
 */
static void nlm4_send_grant_msg(state_async_queue_t *arg)
{
	int retval;
	char buffer[1024] = "\0";
	state_status_t state_status = STATE_SUCCESS;
	state_cookie_entry_t *cookie_entry;
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;
	struct req_op_context op_context;
	struct gsh_export *export;
	nlm4_testargs *nlm_async_grant;

	nlm_async_grant = &nlm_arg->nlm_async_args.nlm_async_grant;

	if (isDebug(COMPONENT_NLM)) {
		netobj_to_string(&nlm_async_grant->cookie,
				 buffer, sizeof(buffer));

		LogDebug(COMPONENT_NLM,
			 "Sending GRANTED for arg=%p svid=%d start=%" PRIx64
			 " len=%" PRIx64 " cookie=%s",
			 arg,
			 nlm_async_grant->alock.svid,
			 nlm_async_grant->alock.l_offset,
			 nlm_async_grant->alock.l_len, buffer);
	}

	retval = nlm_send_async(NLMPROC4_GRANTED_MSG,
				nlm_arg->nlm_async_host,
				nlm_async_grant,
				nlm_arg->nlm_async_key);

	dec_nlm_client_ref(nlm_arg->nlm_async_host);

	/* If success, we are done. */
	if (retval == RPC_SUCCESS)
		goto out;

	/*
	 * We are not able call granted callback. Some client may retry
	 * the lock again. So remove the existing blocked nlm entry
	 */
	LogEvent(COMPONENT_NLM,
		 "GRANTED_MSG RPC call failed with return code %d. Removing the blocking lock",
		 retval);

	state_status = state_find_grant(
			nlm_async_grant->cookie.n_bytes,
			nlm_async_grant->cookie.n_len,
			&cookie_entry);

	if (state_status != STATE_SUCCESS) {
		/* This must be an old NLM_GRANTED_RES */
		LogFullDebug(COMPONENT_NLM,
			     "Could not find cookie=%s status=%s", buffer,
			     state_err_str(state_status));
		goto out;
	}

	if (cookie_entry->sce_lock_entry->sle_block_data == NULL) {
		/* Wow, we're not doing well... */
		LogFullDebug(COMPONENT_NLM,
			     "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
			     buffer);
		goto out;
	}

	/* Initialize a context, it is ok if the export is stale because
	 * we must clean up the cookie_entry.
	 */
	export = cookie_entry->sce_lock_entry->sle_export;
	get_gsh_export_ref(export);

	init_op_context(&op_context, export, export->fsal_export, NULL,
			NFS_V3, 0, NFS_REQUEST);

	state_status = state_release_grant(cookie_entry);

	release_op_context();

	if (state_status != STATE_SUCCESS) {
		/* Huh? */
		LogFullDebug(COMPONENT_NLM,
			     "Could not release cookie=%s status=%s",
			     buffer,
			     state_err_str(state_status));
	}
 out:
	free_grant_arg(arg);
}

int nlm_process_parameters(struct svc_req *req, bool exclusive,
			   nlm4_lock *alock, fsal_lock_param_t *plock,
			   struct fsal_obj_handle **ppobj,
			   care_t care, state_nsm_client_t **ppnsm_client,
			   state_nlm_client_t **ppnlm_client,
			   state_owner_t **ppowner,
			   state_block_data_t **block_data,
			   int32_t nsm_state,
			   state_t **state)
{
	nfsstat3 nfsstat3;
	SVCXPRT *ptr_svc = req->rq_xprt;
	int rc;
	uint64_t maxfilesize =
	    op_ctx->fsal_export->exp_ops.fs_maxfilesize(op_ctx->fsal_export);

	*ppnsm_client = NULL;
	*ppnlm_client = NULL;
	*ppowner = NULL;

	if (state != NULL)
		*state = NULL;

	if (alock->l_offset > maxfilesize) {
		/* Offset larger than max file size */
		return NLM4_FBIG;
	}

	/* Convert file handle into a fsal object */
	*ppobj = nfs3_FhandleToCache((struct nfs_fh3 *)&alock->fh,
				       &nfsstat3,
				       &rc);

	if (*ppobj == NULL) {
		/* handle is not valid */
		return NLM4_STALE_FH;
	}

	if ((*ppobj)->type != REGULAR_FILE) {
		LogWarn(COMPONENT_NLM,
			"NLM operation on non-REGULAR_FILE");
		return NLM4_FAILED;
	}

	*ppnsm_client = get_nsm_client(care, alock->caller_name);

	if (*ppnsm_client == NULL) {
		/* If NSM Client is not found, and we don't care (such as
		 * unlock), just return GRANTED (the unlock must succeed,
		 * there can't be any locks).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppnlm_client =
	    get_nlm_client(care, ptr_svc, *ppnsm_client, alock->caller_name);

	if (*ppnlm_client == NULL) {
		/* If NLM Client is not found, and we don't care (such as
		 * unlock), just return GRANTED (the unlock must succeed,
		 * there can't be any locks).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppowner = get_nlm_owner(care, *ppnlm_client, &alock->oh, alock->svid);

	if (*ppowner == NULL) {
		LogDebug(COMPONENT_NLM, "Could not get NLM Owner");

		/* If owner is not found, and we don't care (such as unlock),
		 * just return GRANTED (the unlock must succeed, there can't be
		 * any locks).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	if (state != NULL) {
		rc = get_nlm_state(STATE_TYPE_NLM_LOCK,
				   *ppobj,
				   *ppowner,
				   care,
				   nsm_state,
				   state);

		if (rc > 0) {
			LogDebug(COMPONENT_NLM, "Could not get NLM State");
			goto out_put;
		}
	}

	if (block_data != NULL) {
		state_block_data_t *bdat = gsh_calloc(1, sizeof(*bdat));
		*block_data = bdat;

		/* Fill in the block data. */
		bdat->sbd_granted_callback = nlm_granted_callback;
		bdat->sbd_prot.sbd_nlm.sbd_nlm_fh.n_bytes =
			bdat->sbd_prot.sbd_nlm.sbd_nlm_fh_buf;
		bdat->sbd_prot.sbd_nlm.sbd_nlm_fh.n_len =
			alock->fh.n_len;
		memcpy(bdat->sbd_prot.sbd_nlm.sbd_nlm_fh_buf,
		       alock->fh.n_bytes,
		       alock->fh.n_len);
	}

	/* Fill in plock (caller will reset reclaim if appropriate) */
	plock->lock_sle_type = FSAL_POSIX_LOCK;
	plock->lock_reclaim = false;
	plock->lock_type = exclusive ? FSAL_LOCK_W : FSAL_LOCK_R;
	plock->lock_start = alock->l_offset;
	plock->lock_length = alock->l_len;

	/* Check for range overflow past maxfilesize.  Comparing beyond 2^64 is
	 * not possible in 64 bits precision, but off+len > maxfilesize is
	 * equivalent to len > maxfilesize - off
	 */
	if (alock->l_len > (maxfilesize - alock->l_offset)) {
		/* Fix up lock length to 0 - end of file */
		LogFullDebug(COMPONENT_NLM,
			     "Converting lock length %"PRIx64" to 0",
			     alock->l_len);
		plock->lock_length = 0;
	}

	LogFullDebug(COMPONENT_NLM, "Parameters Processed");

	return -1;

 out_put:

	(*ppobj)->obj_ops->put_ref((*ppobj));

	if (*ppnsm_client != NULL) {
		dec_nsm_client_ref(*ppnsm_client);
		*ppnsm_client = NULL;
	}

	if (*ppnlm_client != NULL) {
		dec_nlm_client_ref(*ppnlm_client);
		*ppnlm_client = NULL;
	}

	if (*ppowner != NULL) {
		dec_state_owner_ref(*ppowner);
		*ppowner = NULL;
	}

	*ppobj = NULL;
	return rc;
}

int nlm_process_share_parms(struct svc_req *req, nlm4_share *share,
			    struct fsal_export *exp_hdl,
			    struct fsal_obj_handle **ppobj, care_t care,
			    state_nsm_client_t **ppnsm_client,
			    state_nlm_client_t **ppnlm_client,
			    state_owner_t **ppowner,
			    state_t **state)
{
	nfsstat3 nfsstat3;
	SVCXPRT *ptr_svc = req->rq_xprt;
	int rc;

	*ppnsm_client = NULL;
	*ppnlm_client = NULL;
	*ppowner = NULL;

	/* Convert file handle into a fsal object */
	*ppobj = nfs3_FhandleToCache((struct nfs_fh3 *)&share->fh,
				       &nfsstat3,
				       &rc);

	if (*ppobj == NULL) {
		/* handle is not valid */
		return NLM4_STALE_FH;
	}

	if ((*ppobj)->type != REGULAR_FILE) {
		LogWarn(COMPONENT_NLM,
			"NLM operation on non-REGULAR_FILE");
		return NLM4_FAILED;
	}

	*ppnsm_client = get_nsm_client(care, share->caller_name);

	if (*ppnsm_client == NULL) {
		/* If NSM Client is not found, and we don't care (for unshare),
		 * just return GRANTED (the unshare must succeed, there can't be
		 * any shares).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppnlm_client =
	    get_nlm_client(care, ptr_svc, *ppnsm_client, share->caller_name);

	if (*ppnlm_client == NULL) {
		/* If NLM Client is not found, and we don't care (such as
		 * unlock), just return GRANTED (the unlock must succeed, there
		 * can't be any locks).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppowner = get_nlm_owner(care, *ppnlm_client, &share->oh, 0);

	if (*ppowner == NULL) {
		LogDebug(COMPONENT_NLM, "Could not get NLM Owner");

		/* If owner is not found, and we don't care (such as unlock),
		 * just return GRANTED (the unlock must succeed, there can't be
		 * any locks).
		 */
		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	if (state != NULL) {
		rc = get_nlm_state(STATE_TYPE_NLM_SHARE,
				   *ppobj,
				   *ppowner,
				   care,
				   0,
				   state);

		if (rc > 0 || !(*state)) {
			LogDebug(COMPONENT_NLM, "Could not get NLM State");
			goto out_put;
		}
	}

	LogFullDebug(COMPONENT_NLM, "Parameters Processed");

	/* Return non NLM error code '-1' on success. */
	return -1;

 out_put:

	if (*ppnsm_client != NULL) {
		dec_nsm_client_ref(*ppnsm_client);
		*ppnsm_client = NULL;
	}

	if (*ppnlm_client != NULL) {
		dec_nlm_client_ref(*ppnlm_client);
		*ppnlm_client = NULL;
	}

	if (*ppowner != NULL) {
		dec_state_owner_ref(*ppowner);
		*ppowner = NULL;
	}

	(*ppobj)->obj_ops->put_ref((*ppobj));
	*ppobj = NULL;
	return rc;
}

void nlm_process_conflict(nlm4_holder *nlm_holder, state_owner_t *holder,
			  fsal_lock_param_t *conflict)
{
	if (conflict != NULL) {
		nlm_holder->exclusive = conflict->lock_type == FSAL_LOCK_W;
		nlm_holder->l_offset = conflict->lock_start;
		nlm_holder->l_len = conflict->lock_length;
	} else {
		/* For some reason, don't have an actual conflict,
		 * just make it exclusive over the whole file
		 * (which would conflict with any lock requested).
		 */
		nlm_holder->exclusive = true;
		nlm_holder->l_offset = 0;
		nlm_holder->l_len = 0;
	}

	if (holder != NULL) {
		if (holder->so_type == STATE_LOCK_OWNER_NLM)
			nlm_holder->svid =
			    holder->so_owner.so_nlm_owner.so_nlm_svid;
		else
			nlm_holder->svid = 0;
		fill_netobj(&nlm_holder->oh, holder->so_owner_val,
			    holder->so_owner_len);
	} else {
		/* If we don't have an NLM owner, not much we can do. */
		nlm_holder->svid = 0;
		fill_netobj(&nlm_holder->oh, unknown_owner.so_owner_val,
			    unknown_owner.so_owner_len);
	}

	/* Release any lock owner reference passed back from SAL */
	if (holder != NULL)
		dec_state_owner_ref(holder);
}

nlm4_stats nlm_convert_state_error(state_status_t status)
{
	switch (status) {
	case STATE_SUCCESS:
		return NLM4_GRANTED;
	case STATE_LOCK_CONFLICT:
		return NLM4_DENIED;
	case STATE_SHARE_DENIED:
		return NLM4_DENIED;
	case STATE_MALLOC_ERROR:
		return NLM4_DENIED_NOLOCKS;
	case STATE_LOCK_BLOCKED:
		return NLM4_BLOCKED;
	case STATE_GRACE_PERIOD:
		return NLM4_DENIED_GRACE_PERIOD;
	case STATE_LOCK_DEADLOCK:
		return NLM4_DEADLCK;
	case STATE_READ_ONLY_FS:
		return NLM4_ROFS;
	case STATE_NOT_FOUND:
		return NLM4_STALE_FH;
	case STATE_ESTALE:
		return NLM4_STALE_FH;
	case STATE_FILE_BIG:
	case STATE_BAD_RANGE:
		return NLM4_FBIG;
	default:
		return NLM4_FAILED;
	}
}

state_status_t nlm_granted_callback(struct fsal_obj_handle *obj,
				    state_lock_entry_t *lock_entry)
{
	state_block_data_t *block_data = lock_entry->sle_block_data;
	state_nlm_block_data_t *nlm_block_data = &block_data->sbd_prot.sbd_nlm;
	state_cookie_entry_t *cookie_entry = NULL;
	state_async_queue_t *arg;
	nlm4_testargs *inarg;
	state_nlm_async_data_t *nlm_async_data;
	state_nlm_owner_t *nlm_grant_owner =
	    &lock_entry->sle_owner->so_owner.so_nlm_owner;
	state_nlm_client_t *nlm_grant_client = nlm_grant_owner->so_client;
	struct granted_cookie nlm_grant_cookie;
	state_status_t state_status;
	state_status_t state_status_int;

	arg = gsh_calloc(1, sizeof(*arg));
	nlm_async_data = &arg->state_async_data.state_nlm_async_data;

	/* Get a cookie to use for this grant */
	next_granted_cookie(&nlm_grant_cookie);

	/* Add a cookie to the blocked lock pending grant.
	 * It will also request lock from FSAL.
	 * Could return STATE_LOCK_BLOCKED because FSAL would have had to block.
	 */
	state_status = state_add_grant_cookie(obj,
					      &nlm_grant_cookie,
					      sizeof(nlm_grant_cookie),
					      lock_entry,
					      &cookie_entry);

	if (state_status != STATE_SUCCESS) {
		free_grant_arg(arg);
		return state_status;
	}

	/* Fill in the arguments for the NLMPROC4_GRANTED_MSG call */
	inc_nlm_client_ref(nlm_grant_client);
	arg->state_async_func = nlm4_send_grant_msg;
	nlm_async_data->nlm_async_host = nlm_grant_client;
	nlm_async_data->nlm_async_key = cookie_entry;
	inarg = &nlm_async_data->nlm_async_args.nlm_async_grant;

	copy_netobj(&inarg->alock.fh, &nlm_block_data->sbd_nlm_fh);

	fill_netobj(&inarg->alock.oh,
		    lock_entry->sle_owner->so_owner_val,
		    lock_entry->sle_owner->so_owner_len);

	fill_netobj(&inarg->cookie,
		    (char *)&nlm_grant_cookie,
		    sizeof(nlm_grant_cookie));

	inarg->alock.caller_name =
	    gsh_strdup(nlm_grant_client->slc_nlm_caller_name);

	inarg->exclusive = lock_entry->sle_lock.lock_type == FSAL_LOCK_W;
	inarg->alock.svid = nlm_grant_owner->so_nlm_svid;
	inarg->alock.l_offset = lock_entry->sle_lock.lock_start;
	inarg->alock.l_len = lock_entry->sle_lock.lock_length;

	if (isDebug(COMPONENT_NLM)) {
		char buffer[1024] = "\0";

		netobj_to_string(&inarg->cookie, buffer, sizeof(buffer));

		LogDebug(COMPONENT_NLM,
			 "Sending GRANTED for arg=%p svid=%d start=%llx len=%llx cookie=%s",
			 arg, inarg->alock.svid,
			 (unsigned long long)inarg->alock.l_offset,
			 (unsigned long long)inarg->alock.l_len, buffer);
	}

	/* Now try to schedule NLMPROC4_GRANTED_MSG call */
	state_status = state_async_schedule(arg);

	if (state_status != STATE_SUCCESS)
		goto grant_fail;

	return state_status;

 grant_fail:

	/* Something went wrong after we added a grant cookie,
	 * need to clean up
	 */
	dec_nlm_client_ref(nlm_grant_client);

	/* Clean up NLMPROC4_GRANTED_MSG arguments */
	free_grant_arg(arg);

	/* Cancel the pending grant to release the cookie */
	state_status_int = state_cancel_grant(cookie_entry);

	if (state_status_int != STATE_SUCCESS) {
		/* Not much we can do other than log that something
		 * bad happened.
		 */
		LogCrit(COMPONENT_NLM,
			"Unable to clean up GRANTED lock after error");
	}

	return state_status;
}
