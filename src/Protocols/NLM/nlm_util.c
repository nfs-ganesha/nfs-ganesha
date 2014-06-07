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
#include "ganesha_rpc.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "nlm_util.h"
#include "nsm.h"
#include "nlm_async.h"
#include "nfs_core.h"
#include "export_mgr.h"

/* nlm grace time tracking */
static struct timeval nlm_grace_tv;
static const int NLM4_GRACE_PERIOD = 10;

/*
 * Time after which we should retry the granted
 * message request again
 */
static const int NLM4_CLIENT_GRACE_PERIOD = 3;

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
	pthread_mutex_lock(&granted_mutex);
	granted_cookie.gc_cookie++;
	*cookie = granted_cookie;
	pthread_mutex_unlock(&granted_mutex);
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

bool fill_netobj(netobj *dst, char *data, int len)
{
	dst->n_len = 0;
	dst->n_bytes = NULL;
	if (len != 0) {
		dst->n_bytes = gsh_malloc(len);
		if (dst->n_bytes != NULL) {
			dst->n_len = len;
			memcpy(dst->n_bytes, data, len);
		} else
			return false;
	}
	return true;
}

netobj *copy_netobj(netobj *dst, netobj *src)
{
	if (dst == NULL)
		return NULL;
	dst->n_len = 0;
	if (src->n_len != 0) {
		dst->n_bytes = gsh_malloc(src->n_len);
		if (!dst->n_bytes)
			return NULL;
		memcpy(dst->n_bytes, src->n_bytes, src->n_len);
	} else
		dst->n_bytes = NULL;

	dst->n_len = src->n_len;
	return dst;
}

void netobj_free(netobj *obj)
{
	if (obj->n_bytes)
		gsh_free(obj->n_bytes);
}

void netobj_to_string(netobj *obj, char *buffer, int maxlen)
{
	int len = obj->n_len;
	if ((len * 2) + 10 > maxlen)
		len = (maxlen - 10) / 2;

	DisplayOpaqueValue(obj->n_bytes, len, buffer);
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
	if (nlm_arg->nlm_async_args.nlm_async_grant.alock.caller_name != NULL)
		gsh_free(nlm_arg->nlm_async_args.nlm_async_grant.alock.
			 caller_name);
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
	char buffer[1024];
	state_status_t state_status = STATE_SUCCESS;
	state_cookie_entry_t *cookie_entry;
	state_nlm_async_data_t *nlm_arg =
	    &arg->state_async_data.state_nlm_async_data;
	struct root_op_context root_op_context;
	struct gsh_export *export;

	if (isDebug(COMPONENT_NLM)) {
		netobj_to_string(&nlm_arg->nlm_async_args.nlm_async_grant.
				 cookie, buffer, sizeof(buffer));

		LogDebug(COMPONENT_NLM,
			 "Sending GRANTED for arg=%p svid=%d start=%llx len=%llx cookie=%s",
			 arg,
			 nlm_arg->nlm_async_args.nlm_async_grant.alock.svid,
			 (unsigned long long)nlm_arg->nlm_async_args.
			 nlm_async_grant.alock.l_offset,
			 (unsigned long long)nlm_arg->nlm_async_args.
			 nlm_async_grant.alock.l_len, buffer);
	}

	retval = nlm_send_async(NLMPROC4_GRANTED_MSG,
				nlm_arg->nlm_async_host,
				&nlm_arg->nlm_async_args.nlm_async_grant,
				nlm_arg->nlm_async_key);

	dec_nlm_client_ref(nlm_arg->nlm_async_host);

	/* If success, we are done. */
	if (retval == RPC_SUCCESS)
		goto out;

	/*
	 * We are not able call granted callback. Some client may retry
	 * the lock again. So remove the existing blocked nlm entry
	 */
	LogMajor(COMPONENT_NLM,
		 "GRANTED_MSG RPC call failed with return code %d. Removing the blocking lock",
		 retval);

	state_status = state_find_grant(
			nlm_arg->nlm_async_args.nlm_async_grant.cookie.n_bytes,
			nlm_arg->nlm_async_args.nlm_async_grant.cookie.n_len,
			&cookie_entry);

	if (state_status != STATE_SUCCESS) {
		/* This must be an old NLM_GRANTED_RES */
		LogFullDebug(COMPONENT_NLM,
			     "Could not find cookie=%s status=%s", buffer,
			     state_err_str(state_status));
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&cookie_entry->sce_entry->state_lock);

	if (cookie_entry->sce_lock_entry->sle_block_data == NULL) {
		/* Wow, we're not doing well... */
		PTHREAD_RWLOCK_unlock(&cookie_entry->sce_entry->state_lock);
		LogFullDebug(COMPONENT_NLM,
			     "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
			     buffer);
		goto out;
	}

	PTHREAD_RWLOCK_unlock(&cookie_entry->sce_entry->state_lock);

	/* Initialize a context */
	export = cookie_entry->sce_lock_entry->sle_export;
	get_gsh_export_ref(export);

	init_root_op_context(&root_op_context,
			     export, export->fsal_export,
			     NFS_V3, 0, NFS_REQUEST);

	state_status = state_release_grant(cookie_entry);

	release_root_op_context();
	put_gsh_export(export);

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
			   cache_entry_t **ppentry,
			   care_t care, state_nsm_client_t **ppnsm_client,
			   state_nlm_client_t **ppnlm_client,
			   state_owner_t **ppowner,
			   state_block_data_t **block_data)
{
	nfsstat3 nfsstat3;
	SVCXPRT *ptr_svc = req->rq_xprt;
	int rc;

	*ppnsm_client = NULL;
	*ppnlm_client = NULL;
	*ppowner = NULL;

	/* Convert file handle into a cache entry */
	*ppentry = nfs3_FhandleToCache((struct nfs_fh3 *)&alock->fh,
				       &nfsstat3,
				       &rc);

	if (*ppentry == NULL) {
		/* handle is not valid */
		return NLM4_STALE_FH;
	}

	*ppnsm_client = get_nsm_client(care, ptr_svc, alock->caller_name);

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
		dec_nsm_client_ref(*ppnsm_client);

		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppowner = get_nlm_owner(care, *ppnlm_client, &alock->oh, alock->svid);

	if (*ppowner == NULL) {
		LogDebug(COMPONENT_NLM, "Could not get NLM Owner");
		dec_nsm_client_ref(*ppnsm_client);
		dec_nlm_client_ref(*ppnlm_client);
		*ppnlm_client = NULL;

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

	if (block_data != NULL) {
		state_block_data_t *bdat = gsh_malloc(sizeof(*bdat));
		*block_data = bdat;

		/* Fill in the block data, if we don't get one, we will just
		 * proceed without (which will mean the lock doesn't block.
		 */
		if (bdat != NULL) {
			memset(bdat, 0, sizeof(*bdat));
			bdat->sbd_granted_callback = nlm_granted_callback;
			bdat->sbd_prot.sbd_nlm.sbd_nlm_fh.n_bytes =
				bdat->sbd_prot.sbd_nlm.sbd_nlm_fh_buf;
			bdat->sbd_prot.sbd_nlm.sbd_nlm_fh.n_len =
				alock->fh.n_len;
			memcpy(bdat->sbd_prot.sbd_nlm.sbd_nlm_fh_buf,
			       alock->fh.n_bytes,
			       alock->fh.n_len);
		}
	}
	/* Fill in plock */
	plock->lock_type = exclusive ? FSAL_LOCK_W : FSAL_LOCK_R;
	plock->lock_start = alock->l_offset;
	plock->lock_length = alock->l_len;

	LogFullDebug(COMPONENT_NLM, "Parameters Processed");

	return -1;

 out_put:

	cache_inode_put(*ppentry);
	*ppentry = NULL;
	return rc;
}

int nlm_process_share_parms(struct svc_req *req, nlm4_share *share,
			    struct fsal_export *exp_hdl,
			    cache_entry_t **ppentry, care_t care,
			    state_nsm_client_t **ppnsm_client,
			    state_nlm_client_t **ppnlm_client,
			    state_owner_t **ppowner)
{
	nfsstat3 nfsstat3;
	SVCXPRT *ptr_svc = req->rq_xprt;
	int rc;

	*ppnsm_client = NULL;
	*ppnlm_client = NULL;
	*ppowner = NULL;

	/* Convert file handle into a cache entry */
	*ppentry = nfs3_FhandleToCache((struct nfs_fh3 *)&share->fh,
				       &nfsstat3,
				       &rc);

	if (*ppentry == NULL) {
		/* handle is not valid */
		return NLM4_STALE_FH;
	}

	*ppnsm_client = get_nsm_client(care, ptr_svc, share->caller_name);

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
		dec_nsm_client_ref(*ppnsm_client);

		if (care != CARE_NOT)
			rc = NLM4_DENIED_NOLOCKS;
		else
			rc = NLM4_GRANTED;

		goto out_put;
	}

	*ppowner = get_nlm_owner(care, *ppnlm_client, &share->oh, 0);

	if (*ppowner == NULL) {
		LogDebug(COMPONENT_NLM, "Could not get NLM Owner");
		dec_nsm_client_ref(*ppnsm_client);
		dec_nlm_client_ref(*ppnlm_client);
		*ppnlm_client = NULL;

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

	LogFullDebug(COMPONENT_NLM, "Parameters Processed");

	return -1;

 out_put:

	cache_inode_put(*ppentry);
	*ppentry = NULL;
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
	case STATE_STATE_CONFLICT:
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
	case STATE_FSAL_ESTALE:
		return NLM4_STALE_FH;
	case STATE_FILE_BIG:
		return NLM4_FBIG;
	default:
		return NLM4_FAILED;
	}
}

state_status_t nlm_granted_callback(cache_entry_t *pentry,
				    state_lock_entry_t *lock_entry)
{
	state_block_data_t *block_data = lock_entry->sle_block_data;
	state_nlm_block_data_t *nlm_block_data = &block_data->sbd_prot.sbd_nlm;
	state_cookie_entry_t *cookie_entry = NULL;
	state_async_queue_t *arg;
	nlm4_testargs *inarg;
	state_nlm_owner_t *nlm_grant_owner =
	    &lock_entry->sle_owner->so_owner.so_nlm_owner;
	state_nlm_client_t *nlm_grant_client = nlm_grant_owner->so_client;
	struct granted_cookie nlm_grant_cookie;
	state_status_t state_status;
	state_status_t state_status_int;

	arg = gsh_malloc(sizeof(*arg));
	if (arg == NULL) {
		/* If we fail allocation the best is to delete the block entry
		 * so that client can try again and get the lock. May be
		 * by then we are able to allocate objects
		 */
		state_status = STATE_MALLOC_ERROR;
		return state_status;
	}

	memset(arg, 0, sizeof(*arg));

	/* Get a cookie to use for this grant */
	next_granted_cookie(&nlm_grant_cookie);

	/* Add a cookie to the blocked lock pending grant.
	 * It will also request lock from FSAL.
	 * Could return STATE_LOCK_BLOCKED because FSAL would have had to block.
	 */
	state_status = state_add_grant_cookie(pentry,
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
	arg->state_async_data.state_nlm_async_data.nlm_async_host =
	    nlm_grant_client;
	arg->state_async_data.state_nlm_async_data.nlm_async_key = cookie_entry;
	inarg = &arg->state_async_data.state_nlm_async_data.nlm_async_args.
		nlm_async_grant;

	if (!copy_netobj(&inarg->alock.fh, &nlm_block_data->sbd_nlm_fh))
		goto grant_fail_malloc;

	if (!fill_netobj(&inarg->alock.oh,
			 lock_entry->sle_owner->so_owner_val,
			 lock_entry->sle_owner->so_owner_len))
		goto grant_fail_malloc;

	if (!fill_netobj(&inarg->cookie,
			 (char *)&nlm_grant_cookie,
			 sizeof(nlm_grant_cookie)))
		goto grant_fail_malloc;

	inarg->alock.caller_name =
	    gsh_strdup(nlm_grant_client->slc_nlm_caller_name);

	if (!inarg->alock.caller_name)
		goto grant_fail_malloc;

	inarg->exclusive = lock_entry->sle_lock.lock_type == FSAL_LOCK_W;
	inarg->alock.svid = nlm_grant_owner->so_nlm_svid;
	inarg->alock.l_offset = lock_entry->sle_lock.lock_start;
	inarg->alock.l_len = lock_entry->sle_lock.lock_length;

	if (isDebug(COMPONENT_NLM)) {
		char buffer[1024];

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

 grant_fail_malloc:
	state_status = STATE_MALLOC_ERROR;

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
