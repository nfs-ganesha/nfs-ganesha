// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright 2020 Google LLC
 * Author: Solomon Boulos <boulos@google.com>
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
 * -------------
 */

#include "proxyv3_fsal_methods.h"

#include "nlm4.h"
#include "nlm_util.h"

/* Our hostname for the NLM "client" (this host, since we're a proxy). */
static char nlmMachineName[MAXHOSTNAMELEN + 1] = { 0 };
static pid_t nlmSvid;

/**
 * @brief Setup our NLM "stack" for PROXY_V3.
 *
 * @return - True.
 */

bool proxyv3_nlm_init(void)
{
	/* Initialise only once. */
	static bool nlm_initialised;

	if (nlm_initialised)
		return true;

	/* Cache our hostname for auth later. */
	if (gethostname(nlmMachineName, sizeof(nlmMachineName)) != 0) {
		const char *kClientName = "127.0.0.1";

		LogCrit(COMPONENT_FSAL,
			"gethostname() failed. Errno %d (%s). Hardcoding a client IP instead.",
			errno, strerror(errno));
		memcpy(nlmMachineName, kClientName,
		       strlen(kClientName) + 1 /* For NULL */);
	}

	nlmSvid = (int32_t) getpid();
	nlm_initialised = true;
	return true;
}

/**
 * @brief Determine if this is a lock op we can handle.
 *
 * @param obj_hdl The fsal_obj_handle (currently unused).
 * @param state The object lock state (currently unused).
 * @param owner The object owner (must be non-NULL for valid ops).
 * @param lock_op The lock op itself. We currently don't expect FSAL_OP_LOCKB.
 * @param request_lock The input lock info.
 * @param conflicting_lock Optional output lock. Required for FSAL_OP_LOCKT.
 *
 * @return - True, if we can handle this op.
 *         - False, otherwise.
 */

static bool
proxyv3_is_valid_lockop(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			struct state_owner_t *owner,
			fsal_lock_op_t lock_op,
			fsal_lock_param_t *request_lock,
			fsal_lock_param_t *conflicting_lock)
{
	if (lock_op == FSAL_OP_LOCKB) {
		LogCrit(COMPONENT_FSAL,
			"Asked to perform an async lock request. We told Ganesha we can't handle those...");
		return false;
	}

	if (request_lock->lock_sle_type != FSAL_POSIX_LOCK) {
		LogCrit(COMPONENT_FSAL,
			"Asked to do an NFSv4 Delegation/Lease (%d)",
			request_lock->lock_sle_type);
		return false;
	}

	if (owner == NULL) {
		/*
		 * We need the owner info to fill in the various alock fields in
		 * the requests.
		 */
		LogCrit(COMPONENT_FSAL,
			"Didn't receive an owner. Unexpected.");
		return false;
	}

	if (lock_op == FSAL_OP_LOCKT && conflicting_lock == NULL) {
		LogCrit(COMPONENT_FSAL,
			"ERROR: Ganesha asked for NLM4_TEST, but output is NULL");
		return false;
	}

	if (proxyv3_nlm_port() == 0) {
		LogCrit(COMPONENT_FSAL,
			"Got a lock op request, but we don't have a lockmanagerd port!");
		return false;
	}

	return true;
}

/**
 * @brief Map from fsal_lock_op_t to a const char* string.
 * @param status Input lock op as a fsal_lock_op_t.
 *
 * @return - The status enum as a string (e.g., "LOCK_ASYNC").
 *         - "INVALID" otherwise.
 */

static const char *lock_op_to_cstr(fsal_lock_op_t op)
{
	switch (op) {
	case FSAL_OP_LOCKT: return "TEST";
	case FSAL_OP_LOCK:  return "LOCK_IMMEDIATE";
	case FSAL_OP_LOCKB: return "LOCK_ASYNC";
	case FSAL_OP_UNLOCK: return "UNLOCK";
	case FSAL_OP_CANCEL: return "CANCEL";
	}
	return "INVALID";
}

/**
 * @brief Map from nlm4_stats error codes a const char* string.
 * @param status Input status as an nlm4_stats.
 *
 * @return - The status enum as a string (e.g., "NLM4_GRANTED").
 *         - "INVALID" otherwise.
 */

static const char *nlm4stat_to_cstr(nlm4_stats status)
{
	switch (status) {
	case NLM4_GRANTED: return "NLM4_GRANTED";
	case NLM4_DENIED: return "NLM4_DENIED";
	case NLM4_DENIED_NOLOCKS: return "NLM4_DENIED_NOLOCKS";
	case NLM4_BLOCKED: return "NLM4_BLOCKED";
	case NLM4_DENIED_GRACE_PERIOD: return "NLM4_DENIED_GRACE_PERIOD";
	case NLM4_DEADLCK: return "NLM4_DEADLCK";
	case NLM4_ROFS: return "NLM4_ROFS";
	case NLM4_STALE_FH: return "NLM4_STALE_FH";
	case NLM4_FBIG: return "NLM4_FBIG";
	case NLM4_FAILED: return "NLM4_FAILED";
	}
	return "INVALID";
}

/**
 * @brief Fill in the common NLM arguments (cookie and lock).
 *
 * @param obj The object handle for the object (as proxyv3_obj_handle).
 * @param state The current object lock state.
 * @param state_owner The object owner info.
 * @param request_lock The input lock info.
 * @param cookie The output NLM cookie argument.
 * @param lock The output nlm4_lock argument.
 */

static void
proxyv3_nlm_fill_common_args(struct proxyv3_obj_handle *obj,
			     struct state_t *state,
			     struct state_owner_t *state_owner,
			     fsal_lock_param_t *request_lock,
			     struct netobj *cookie,
			     struct nlm4_lock *lock)
{
	/*
	 * Fill in the cookie.
	 *
	 * NFS Illustrated claims that the client (that's us!) get to fill this
	 * in with whatever we want (I think it's an extra double check on top
	 * of the XID in the RPC). My first plan was to use obj->fh3, but those
	 * are often >32 bytes which Linux's NFSD doesn't like at the least:
	 *
	 *   lockd: bad cookie size 36 (only cookies under 32 bytes are
	 *   supported.)
	 *
	 * So just trim the length to the first 32.
	 */

	cookie->n_bytes = obj->fh3.data.data_val;
	if (obj->fh3.data.data_len > 32) {
		cookie->n_len = 32;
	} else {
		cookie->n_len = obj->fh3.data.data_len;
	}


	/*
	 * @todo: if we (the proxy) crash, the backend will try to reach out to
	 * us, but Ganesha won't know what it's talking about (that might be
	 * fine! lock recovery is cooperative). We will be in grace though, and
	 * all *our* clients *should* reach out to us to reclaim their locks
	 * with reclaim=true.
	 */

	/* We use *our* hostname to tell the backend that we are its client. */
	lock->caller_name = nlmMachineName;
	lock->svid = nlmSvid;

	lock->fh.n_bytes = obj->fh3.data.data_val;
	lock->fh.n_len = obj->fh3.data.data_len;

	lock->oh.n_bytes = state_owner->so_owner_val;
	lock->oh.n_len = state_owner->so_owner_len;

	lock->l_offset = request_lock->lock_start;
	lock->l_len = request_lock->lock_length;
}


/**
 * @brief A little helper to perform an NLM RPC via proxyv3_nlm_call.
 */

static fsal_status_t
proxyv3_nlm_commonrpc(rpcproc_t nlmProc, const char *procName,
		      xdrproc_t encFunc, void *args,
		      xdrproc_t decFunc, void *result,
		      nlm4_stats *status,
		      struct netobj *cookie,
		      struct nlm4_lock *lock)
{
	LogDebug(COMPONENT_FSAL,
		 "Issuing an %s. Lock info: offset %" PRIu64 ", len %" PRIu64,
		 procName, lock->l_offset, lock->l_len);

	if (!proxyv3_nlm_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nlm_port(),
			      &op_ctx->creds,
			      nlmProc,
			      encFunc, args,
			      decFunc, result)) {
		LogCrit(COMPONENT_FSAL,
			"PROXY_V3: NLM op %s failed.",
			procName);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* For now, always log the results. */
	LogDebug(COMPONENT_FSAL,
		 "PROXY_V3: NLM op %s returned %s",
		 procName, nlm4stat_to_cstr(*status));

	return nlm4stat_to_fsalstat(*status);
}

/**
 * @brief Handle NLM_TEST.
 */

static fsal_status_t
proxyv3_nlm_test(struct proxyv3_obj_handle *obj,
		 struct state_t *state,
		 struct state_owner_t *state_owner,
		 bool exclusive_lock,
		 fsal_lock_param_t *request_lock,
		 fsal_lock_param_t *conflicting_lock)
{
	nlm4_testargs args;
	nlm4_testres result;
	nlm4_stats *status;
	nlm4_holder *holder;
	fsal_status_t rc;

	memset(&result, 0, sizeof(result));

	args.exclusive = exclusive_lock;
	proxyv3_nlm_fill_common_args(obj, state, state_owner,
				     request_lock,
				     &args.cookie,
				     &args.alock);

	status = &result.test_stat.stat;
	rc =
		proxyv3_nlm_commonrpc(NLMPROC4_TEST, "NLM_TEST",
				      (xdrproc_t) xdr_nlm4_testargs, &args,
				      (xdrproc_t) xdr_nlm4_testres, &result,
				      status, &args.cookie, &args.alock);

	/* If we don't get an explicit DENIED response, return the result. */
	if (*status != NLM4_DENIED) {
		return rc;
	}

	/* Otherwise, we need to fill in the conflict info. */
	holder = &result.test_stat.nlm4_testrply_u.holder;

	/*
	 * @todo The holder also has the other owner information, but it's not
	 * clear if you're supposed to fill in state_owner with that info...
	 */

	conflicting_lock->lock_type =
		(holder->exclusive) ? FSAL_LOCK_W : FSAL_LOCK_R;
	conflicting_lock->lock_start = holder->l_offset;
	conflicting_lock->lock_length = holder->l_len;

	return rc;
}



/**
 * @brief Handle NLM_LOCK.
 */

static fsal_status_t
proxyv3_nlm_lock(struct proxyv3_obj_handle *obj,
		 struct state_t *state,
		 struct state_owner_t *state_owner,
		 bool exclusive_lock,
		 fsal_lock_param_t *request_lock)
{
	nlm4_lockargs args;
	nlm4_res result;

	memset(&result, 0, sizeof(result));

	args.block = false;
	args.exclusive = exclusive_lock;
	args.reclaim = request_lock->lock_reclaim;
	/*
	 * While sal_data.h says this is the NFSv4 Sequence ID, nlm4_Lock pushes
	 * arg->state from v3 to eventually get_nlm_state as "nsm_state" which
	 * goes into the state_seqid field.
	 */
	args.state = state->state_seqid;

	proxyv3_nlm_fill_common_args(obj, state, state_owner,
				     request_lock,
				     &args.cookie,
				     &args.alock);

	return proxyv3_nlm_commonrpc(NLMPROC4_LOCK, "NLM_LOCK",
				     (xdrproc_t) xdr_nlm4_lockargs, &args,
				     (xdrproc_t) xdr_nlm4_res, &result,
				     &result.stat.stat,
				     &args.cookie,
				     &args.alock);
}

/*
 * NOTE(boulos): We should never currently up end calling CANCEL, because we
 * tell Ganesha we aren't ready to deal with blocking locks (yet).
 */

/**
 * @brief Handle NLM_CANCEL.
 */

static fsal_status_t
proxyv3_nlm_cancel(struct proxyv3_obj_handle *obj,
		   struct state_t *state,
		   struct state_owner_t *state_owner,
		   bool exclusive_lock,
		   fsal_lock_param_t *request_lock)
{
	nlm4_cancargs args;
	nlm4_res result;

	memset(&result, 0, sizeof(result));

	args.block = false;
	args.exclusive = exclusive_lock;

	proxyv3_nlm_fill_common_args(obj, state, state_owner,
				     request_lock,
				     &args.cookie,
				     &args.alock);

	return proxyv3_nlm_commonrpc(NLMPROC4_CANCEL, "NLM_CANCEL",
				     (xdrproc_t) xdr_nlm4_cancargs, &args,
				     (xdrproc_t) xdr_nlm4_res, &result,
				     &result.stat.stat,
				     &args.cookie,
				     &args.alock);
}


/**
 * @brief Handle NLM_UNLOCK.
 */

static fsal_status_t
proxyv3_nlm_unlock(struct proxyv3_obj_handle *obj,
		   struct state_t *state,
		   struct state_owner_t *state_owner,
		   bool exclusive_lock,
		   fsal_lock_param_t *request_lock)
{
	nlm4_unlockargs args;
	nlm4_res result;

	memset(&result, 0, sizeof(result));

	proxyv3_nlm_fill_common_args(obj, state, state_owner,
				     request_lock,
				     &args.cookie,
				     &args.alock);

	return proxyv3_nlm_commonrpc(NLMPROC4_UNLOCK, "NLM4_UNLOCK",
				     (xdrproc_t) xdr_nlm4_unlockargs, &args,
				     (xdrproc_t) xdr_nlm4_res, &result,
				     &result.stat.stat,
				     &args.cookie,
				     &args.alock);
}

/**
 * @brief Clear the conflicting_lock parameter for lock operations.
 *
 * @param lock_op The type of lock op (should be FSAL_OP_LOCKT).
 * @param conflicting_lock The output fsal_lock_param_t to clear.
 *
 */

static void
proxyv3_clear_conflicting_lock(fsal_lock_op_t lock_op,
			       fsal_lock_param_t *conflicting_lock)
{
	if (lock_op != FSAL_OP_LOCKT) {
		/*
		 * @todo Alternatively, we can do a TEST afterwards to
		 * fill in who the conflict was likely to be. But that can also
		 * fail if the conflict gives up in between our LOCK. The CEPH
		 * FSAL chooses to do this though, and it probably makes Ganesha
		 * more able to handle immediate responses for lock requests
		 * (i.e., if it knows that only a certain range is locked, it
		 * might allow in a read lock to a non-overlapping range). But
		 * the SAL do_lock_op always just fills in *holder with
		 * &unknown_holder anyway... so it doesn't seem like we should
		 * waste our time.
		 */
		LogDebug(COMPONENT_FSAL,
			 "Lock op is %s, but Ganesha wants to know about the conflict. Report the whole file as locked like nlm_process_conflict.",
			 lock_op_to_cstr(lock_op));
	}

	conflicting_lock->lock_sle_type = FSAL_POSIX_LOCK;
	conflicting_lock->lock_type = FSAL_LOCK_W; /* Write lock / exclusive */
	conflicting_lock->lock_start = 0;
	conflicting_lock->lock_length = 0; /* Whole file */
	conflicting_lock->lock_reclaim = false;
}


/**
 * @brief Handle all basic NLM lock operations (LOCK, UNLOCK, TEST, CANCEL).
 *
 * @param obj_hdl The fsal_obj_handle for the object.
 * @param state The current object lock state.
 * @param owner The object owner info.
 * @param lock_op The lock op itself.
 * @param request_lock The input lock info.
 * @param conflicting_lock Optional output lock. Required for FSAL_OP_LOCKT.
 *
 * @return - fsal_status_t for the result of the operation.
 */

fsal_status_t
proxyv3_lock_op2(struct fsal_obj_handle *obj_hdl,
		 struct state_t *state,
		 void *void_owner,
		 fsal_lock_op_t lock_op,
		 fsal_lock_param_t *request_lock,
		 fsal_lock_param_t *conflicting_lock)
{
	LogDebug(COMPONENT_FSAL,
		 "Got lock_op2 for obj %p. Op is %s",
		 obj_hdl, lock_op_to_cstr(lock_op));

	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	/*
	 * NOTE(boulos): I'm super confused as to whether state->state_owner is
	 * supposed to be used here vs casting owner to state_owner_t...
	 */
	struct state_owner_t *owner = (struct state_owner_t *) void_owner;

	/*
	 * A write lock is an exclusive request, while reads are not. See
	 * nlm_process_parameters for reference.
	 */
	bool exclusive = request_lock->lock_type == FSAL_LOCK_W;

	/*
	 * Before we fail or not, clear the output conflicting_lock if
	 * appropriate.  NOTE(boulos): Ganesha seems to (incorrectly?) fill in
	 * the response for non-TEST calls with the conflict holder (e.g., in
	 * nlm4_Lock) even though these RPCs are all supposed to return only a
	 * nlm4_res which has no holder information.
	 */
	if (conflicting_lock != NULL) {
		proxyv3_clear_conflicting_lock(lock_op, conflicting_lock);
	}

	/* Make sure we can handle the request and that it's well formed. */
	if (!proxyv3_is_valid_lockop(obj_hdl,
				     state,
				     owner,
				     lock_op,
				     request_lock,
				     conflicting_lock)) {
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	switch (lock_op) {
	case FSAL_OP_LOCKT:
		return proxyv3_nlm_test(obj, state, owner, exclusive,
					request_lock, conflicting_lock);
	case FSAL_OP_LOCK:
		return proxyv3_nlm_lock(obj, state, owner,
					exclusive, request_lock);
	case FSAL_OP_UNLOCK:
		return proxyv3_nlm_unlock(obj, state, owner,
					  exclusive, request_lock);
	case FSAL_OP_CANCEL:
		return proxyv3_nlm_cancel(obj, state, owner,
					  exclusive, request_lock);
	default:
		/* UNREACHABLE. (Tested in is_valid_lockop). */
		LogCrit(COMPONENT_FSAL,
			"Unexpected lock op %d",
			lock_op);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
}
