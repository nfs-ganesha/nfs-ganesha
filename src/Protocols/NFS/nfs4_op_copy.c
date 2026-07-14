// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Deeraj Patil <deeraj.patil@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/>
 *
 * ---------------------------------------
 */

/**
 * @file nfs4_op_copy.c
 * @brief NFS4_OP_COPY - sync + async intra-server copy implementation.
 *
 * Implements the NFSv4.2 COPY operation (RFC 7862 Sec.15.2) supporting both
 * synchronous inline copying and asynchronous offload via CB_OFFLOAD.
 *
 * Per RFC 7862 15.2:
 *  - SAVED_FH  : source file
 *  - CURRENT_FH: destination file
 *  - ca_source_server_len == 0  -> intra-server (this file)
 *  - ca_source_server_len  > 0  -> inter-server (between the servers NOTSUPP)
 *
 *   ca_synchronous == TRUE  (client requires inline copy):
 *     -> SYNC path always, but will use Xcopy threads to achieve the same.
 *
 *   ca_synchronous == FALSE  (client requested for async copy):
 *     -> ASYNC path always.

 *   Copy_Offload = true  AND  ca_synchronous == FALSE
 *     -> ASYNC path :
 *       1. Server allocates a copy stateid (STATE_TYPE_COPY_OFFLOAD),
 *          registers it in the SAL, returns COPY response IMMEDIATELY
 *          with wr_ids=1 / cr_synchronous=FALSE and the copy stateid.
 *       2. An xcopy fridge worker copies data in copy_chunk_size chunks.
 *       3. When done the worker fires CB_OFFLOAD over the back channel.
 *       4. Client may poll via OFFLOAD_STATUS(copy_stateid) at any time
 *          between steps 1 and 3.
 *
 */

#include "config.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "common_utils.h"
#include "fsal.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "sal_data.h"
#include "nfs_convert.h"
#include "fridgethr.h"
#include "nfs_rpc_callback.h"
#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs4.h"
#endif

/**
 * Maximum number of CB_OFFLOAD retransmits after a transient client error.
 *
 * RFC 7862 15.9.3: "If the client returns NFS4ERR_DELAY (or the server
 * receives no reply), the server SHOULD retransmit CB_OFFLOAD."
 * NFS4ERR_RESOURCE is treated the same way — it signals a transient
 * condition on the client, not a permanent rejection.
 *
 * After NFS4_COPY_CB_MAX_RETRIES additional attempts the server gives up
 * and destroys the copy state, making subsequent OFFLOAD_STATUS calls
 * return NFS4ERR_BAD_STATEID.
 */
#define NFS4_COPY_CB_MAX_RETRIES 2

/**
 * Dedicated fridge thread pool for NFSv4.2 COPY offload operations.
 *
 * Threads are named xcopy0, xcopy1, … in logs so copy workers are
 * distinguishable from the shared Gen_Fridge threads.  Pool size is
 * set at startup from nfs_param.nfsv4_param.max_copy_workers.
 */
static struct fridgethr *copy_fridge;

/**
 * Monotonically increasing counter for per-thread xcopyN naming.
 */
static uint32_t copy_thr_next_id;

/**
 * @brief Shared execution context for a copy worker (sync or async).
 *
 * Stack-allocated in nfs4_op_copy(), populated by copy_validate(), then
 * struct-copied into copy_job::job_ctx by copy_submit_job() for both modes.
 * The worker reads from job->job_ctx regardless of sync or async path.
 *
 * On successful submit, src_state/dst_state in the local ctx are NULLed
 * (ownership transferred to the worker).  On failure the out: label releases
 * any refs not yet transferred.
 *
 * Ownership of all pointer fields is documented in copy_submit_job().
 */
struct copy_ctx {
	struct fsal_obj_handle *src; /* source file handle */
	struct fsal_obj_handle *dst; /* destination file handle */
	state_t *src_state; /* source open/lock stateid */
	state_t *dst_state; /* destination open/lock stateid */
	uint64_t src_off; /* starting source offset */
	uint64_t dst_off; /* starting destination offset */
	uint64_t to_copy; /* bytes to copy (0 = until EOF) */
	struct gsh_export *ctx_export; /* get_ref'd at dispatch time */
	struct fsal_export *fsal_export; /* borrowed from ctx_export */
};

/** Result of a copy job dispatch attempt (shared by sync and async paths). */
enum copy_dispatch_result {
	COPY_DISPATCHED,
	COPY_NO_RESOURCES,
};

/**
 * @brief Result bundle returned by copy_do_work() to both workers.
 */
struct copy_work_result {
	fsal_status_t st; /* final FSAL status (ERR_FSAL_NO_ERROR on OK) */
	uint64_t bytes_copied; /* total bytes successfully transferred */
	uint64_t elapsed_ms; /* wall-clock duration in milliseconds */
	uint64_t rate_kbps; /* average throughput in KB/s */
	uint64_t chunk_num; /* number of chunks executed */
};

/** Selects which completion path copy_worker() takes. */
enum copy_mode {
	COPY_MODE_SYNC, /* svc_resume -> nfs4_op_copy_resume -> inline reply */
	COPY_MODE_ASYNC, /* CB_OFFLOAD sent from within the worker */
};

/**
 * @brief per-job context for every xcopy worker invocation.
 *
 * Allocated before fridgethr_submit() for both the sync and async paths.
 * The worker frees it (or, for ASYNC, the copy_offload_state machinery
 * frees cos; the job itself is freed by the worker before svc_resume /
 * CB_OFFLOAD dispatch).
 *
 * Mode-specific fields
 *
 * COPY_MODE_SYNC:
 *   job_res   — pointer into the compound response (filled on resume)
 *   job_data  — compound_data_t* needed to call svc_resume(req)
 *   job_status / job_copied — result stored by worker, read by resume
 *
 * COPY_MODE_ASYNC:
 *   job_cos   — the SAL copy_offload_state (owns its own lifetime via
 *               refcounting); all its cos_* execution fields are already
 *               populated by copy_submit_job() before submit.
 *
 * Shared execution context
 *
 * job_ctx holds the validated src/dst/offset/export fields for both modes.
 * copy_submit_job() struct-copies the local copy_ctx into job_ctx for both
 * SYNC and ASYNC.  The ASYNC worker reads from job->job_ctx exactly like SYNC.
 *
 * copy_offload_state (job_cos) is separate because it must outlive the job:
 * it persists after the worker calls gsh_free(job), so that OFFLOAD_STATUS
 * can query it and CB_OFFLOAD retries can re-read cached completion fields.
 */
struct copy_job {
	enum copy_mode job_mode;

	COPY4res *job_res; /**< response to fill on resume */
	compound_data_t *job_cdata; /**< compound data for svc_resume */
	nfsstat4 job_status; /**< result set by worker */
	uint64_t job_copied; /**< bytes copied, set by worker */

	/**< SAL state; persists past job lifetime */
	struct copy_offload_state *job_cos;
	struct copy_ctx job_ctx;
};

/**
 * Shutdown-in-progress flag.
 *
 * Written once by nfs4_copy_fridge_shutdown() via atomic_store_uint8_t
 * BEFORE issuing fridgethr_comm_stop. copy_worker checks it every chunk
 * via atomic_fetch_uint8_t so in-flight copies abort promptly.
 */
static uint8_t copy_fridge_stopping;

/**
 * @brief Assign a stable xcopyN thread name on first use.
 *
 * Registered as frp.thread_initialize so fridgethr calls it exactly once
 * per worker thread, before the job runs.
 */
static void copy_thread_initializer(struct fridgethr_context *ctx)
{
	char thr_name[32];
	uint32_t my_id = atomic_postadd_uint32_t(&copy_thr_next_id, 1);

	snprintf(thr_name, sizeof(thr_name), "xcopy%" PRIu32, my_id);
	SetNameFunction(thr_name);
}

int nfs4_copy_fridge_init(void)
{
	struct fridgethr_params frp;
	int rc;

	memset(&frp, 0, sizeof(frp));
	frp.thr_max = nfs_param.nfsv4_param.max_copy_workers;
	frp.thr_min = 0;
	frp.flavor = fridgethr_flavor_worker;
	frp.thread_initialize = copy_thread_initializer;
	/*
	 * fridgethr_defer_fail: when all max_copy_workers threads are busy,
	 * fridgethr_submit() returns EWOULDBLOCK immediately.
	 * nfs4_op_copy() maps EWOULDBLOCK -> NFS4ERR_OFFLOAD_DENIED;
	 *
	 * Using fridgethr_defer_queue would create an unbounded work queue:
	 * parked RPC compounds pile up indefinitely and can exhaust memory.
	 */
	frp.deferment = fridgethr_defer_fail;

	rc = fridgethr_init(&copy_fridge, "xcopy", &frp);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize copy fridge (xcopy), error %d",
			 rc);
		return rc;
	}
	LogEvent(COMPONENT_THREAD,
		 "COPY fridge (xcopy) started, max_workers=%" PRIu32,
		 nfs_param.nfsv4_param.max_copy_workers);
	return 0;
}

int nfs4_copy_fridge_shutdown(void)
{
	int rc;

	if (copy_fridge == NULL)
		return 0;

	/* Signal all copy_worker threads to stop at the next chunk. */
	atomic_store_uint8_t(&copy_fridge_stopping, 1);
	LogEvent(COMPONENT_THREAD,
		 "COPY fridge: signalled in-flight workers to stop.");

	rc = fridgethr_sync_command(copy_fridge, fridgethr_comm_stop, 30);
	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_THREAD,
			 "COPY fridge shutdown timedout, force-cancelling");
		fridgethr_cancel(copy_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_THREAD, "COPY fridge shutdown error: %d",
			 rc);
	} else {
		LogEvent(COMPONENT_THREAD, "COPY fridge shut down.");
	}
	return rc;
}

/**
 * @brief Validate a stateid for COPY source or destination.
 *
 * Checks that the stateid is valid and that the open/lock/delegation
 * state allows read (source) or write (destination) access.
 *
 * @param[in]  sid        Stateid from the client
 * @param[in]  obj        File handle being accessed
 * @param[in]  data       Compound request data
 * @param[in]  need_write TRUE for destination (write check)
 * @param[out] state_out  Resolved state_t (caller must dec_state_t_ref)
 *
 * @return NFS4_OK on success, error code otherwise
 */
static nfsstat4 check_copy_stateid(stateid4 *sid, struct fsal_obj_handle *obj,
				   compound_data_t *data, bool need_write,
				   state_t **state_out)
{
	state_t *state = NULL;
	state_t *state_open = NULL;
	nfsstat4 status;

	status = nfs4_Check_Stateid(sid, obj, &state, data, STATEID_SPECIAL_ANY,
				    0, false,
				    need_write ? "COPY_DST" : "COPY_SRC");
	if (status != NFS4_OK)
		return status;

	if (state != NULL) {
		switch (state->state_type) {
		case STATE_TYPE_SHARE:
			if (need_write &&
			    !(state->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_WRITE)) {
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			if (!need_write &&
			    !(state->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_READ)) {
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			break;
		case STATE_TYPE_LOCK:
			state_open = nfs4_State_Get_Pointer(
				state->state_data.lock.openstate_key);
			if (state_open == NULL) {
				dec_state_t_ref(state);
				return NFS4ERR_BAD_STATEID;
			}
			if (need_write &&
			    !(state_open->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_WRITE)) {
				dec_state_t_ref(state_open);
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			if (!need_write &&
			    !(state_open->state_data.share.share_access &
			      OPEN4_SHARE_ACCESS_READ)) {
				dec_state_t_ref(state_open);
				dec_state_t_ref(state);
				return NFS4ERR_OPENMODE;
			}
			dec_state_t_ref(state_open);
			break;
		case STATE_TYPE_DELEG:
			if (need_write) {
				struct state_deleg *sd =
					&state->state_data.deleg;
				if (sd->sd_type != OPEN_DELEGATE_WRITE &&
				    sd->sd_type !=
					    OPEN_DELEGATE_WRITE_ATTRS_DELEG) {
					return NFS4ERR_BAD_STATEID;
				}
			}
			break;
		default:
			dec_state_t_ref(state);
			return NFS4ERR_BAD_STATEID;
		}
	} else if (state_deleg_conflict(obj, need_write)) {
		return NFS4ERR_DELAY;
	}

	*state_out = state;
	return NFS4_OK;
}

/**
 * @brief state_free callback invoked by dec_state_t_ref when refcount
 *        reaches zero.
 *
 * This is the ONLY place where the copy_offload_state memory is freed.
 * It is invoked automatically through the state_t refcount machinery,
 * ensuring that OFFLOAD_STATUS callers that hold a transient reference
 * (via nfs4_Check_Stateid) cannot race with the final cleanup.
 */
static void copy_offload_state_free(struct state_t *state)
{
	struct copy_offload_state *cos =
		container_of(state, struct copy_offload_state, cos_state);

	/* Free the destination FH buffer stored for CB_OFFLOAD coa_fh */
	gsh_free(cos->cos_dst_fh.nfs_fh4_val, MEM_COMP_XCOPY);
	cos->cos_dst_fh.nfs_fh4_val = NULL;

	gsh_free(cos, MEM_COMP_XCOPY);
}

/**
 * @brief Allocate and register a copy offload state.
 *
 * Sets state_obj and state_owner so nfs4_Check_Stateid() can validate:
 *   state_obj   = dst  — for CURRENT_FH comparison
 *   state_owner = cid_owner — for lease check via so_clientrec
 *
 * IMPORTANT: we set state_obj and state_owner for field access by
 * nfs4_Check_Stateid() only.  We deliberately do NOT add this state to
 * obj->state_hdl->file.list_of_states or cid_owner->so_state_list.
 * Those lists are walked by _state_del_locked() which unconditionally
 * calls obj->obj_ops->close2() (nfs4_state.c:577).  close2 requires a
 * valid op_ctx->fsal_export, but destroy_copy_offload_state() can be
 * called from the RPC callback thread (cb_offload_completion) where
 * op_ctx has no valid fsal_export -> crash in mdc_cur_export().
 *
 * Cleanup is handled manually in destroy_copy_offload_state() using
 * nfs4_State_Del() (hash-only removal) without going through state_del().
 *
 * Refcount starts at 1 (owner's reference).
 *
 * @param data Compound request's data
 * @param dst  Destination fsal_obj_handle (CURRENT_FH at COPY time).
 * @param count Amount of data needs to be copied.
 *
 * @return copy_offload_state structure
 */
static struct copy_offload_state *create_copy_offload_state(
	compound_data_t *data, struct fsal_obj_handle *dst, uint64_t tcount)
{
	struct copy_offload_state *cos;
	nfs_client_id_t *clientid = data->preserved_clientid;
	struct timespec now;

	cos = gsh_calloc(1, sizeof(struct copy_offload_state), MEM_COMP_XCOPY);
	cos->cos_state.state_type = STATE_TYPE_COPY_OFFLOAD;
	/*
	 * Start seqid at 0 — same as _state_add_impl().
	 * update_stateid() in nfs4_op_copy() will increment it to 1 and
	 * stamp the correct value into the COPY response stateid.
	 */
	cos->cos_state.state_seqid = 0;
	cos->cos_state.state_refcount = 1; /* owner's reference */
	cos->cos_state.state_free = copy_offload_state_free;
	cos->cos_state.state_export = op_ctx->ctx_export;
	if (cos->cos_state.state_export)
		get_gsh_export_ref(cos->cos_state.state_export);

	/*
	 * state_obj = destination file (with a ref).
	 * Needed by get_state_obj_ref_locked() inside nfs4_Check_Stateid()
	 * to return the obj for CURRENT_FH comparison. Released manually in
	 * destroy_copy_offload_state() under state_mutex, NOT via state_del()
	 */
	cos->cos_state.state_obj = dst;
	dst->obj_ops->get_ref(dst);

	/*
	 * state_owner = cid_owner (STATE_CLIENTID_OWNER_NFSV4).
	 * The copy stateid belongs to the clientid.
	 * nfs4_Check_Stateid() reads owner2->so_nfs4_owner.so_clientrec to
	 * check data->preserved_clientid == clientid.
	 * inc_state_owner_ref is the permanent hold on cid_owner that matches
	 * dec_state_owner_ref in destroy_copy_offload_state().
	 * We do NOT add to cid_owner->so_state_list — see comment above.
	 */
	cos->cos_state.state_owner = &clientid->cid_owner;
	inc_state_owner_ref(&clientid->cid_owner);

	/*
	 * state_mutex is required by dec_state_t_ref: it calls
	 * PTHREAD_MUTEX_destroy(&state->state_mutex) when refcount hits 0.
	 */
	PTHREAD_MUTEX_init(&cos->cos_state.state_mutex, NULL);

	/*
	 * glist_head fields must be self-referential (empty-list sentinel).
	 */
	glist_init(&cos->cos_state.state_list);
	glist_init(&cos->cos_state.state_owner_list);
	glist_init(&cos->cos_state.state_export_list);

	cos->cos_clientid = clientid;
	inc_client_id_ref(clientid);
	cos->cos_total_count = tcount;
	cos->cos_status = NFS4_OK;

	clock_gettime(CLOCK_REALTIME, &now);
	cos->cos_start_time.seconds = now.tv_sec;
	cos->cos_start_time.nseconds = now.tv_nsec;

	nfs4_BuildStateId_Other(clientid, cos->cos_state.stateid_other);

	if (nfs4_State_Set(&cos->cos_state) != STATE_SUCCESS) {
		if (cos->cos_state.state_export)
			put_gsh_export(cos->cos_state.state_export);
		dst->obj_ops->put_ref(dst);
		cos->cos_state.state_obj = NULL;
		dec_state_owner_ref(&clientid->cid_owner);
		cos->cos_state.state_owner = NULL;
		dec_client_id_ref(clientid);
		PTHREAD_MUTEX_destroy(&cos->cos_state.state_mutex);
		gsh_free(cos, MEM_COMP_XCOPY);
		return NULL;
	}

	/*
	 * Capture the destination file handle for CB_OFFLOAD coa_fh.
	 *
	 * Requires coa_fh as the FIRST field of CB_OFFLOAD4args.
	 * At this point data->currentFH is the NFS4 file handle of the
	 * destination file (CURRENT_FH == dst for COPY).
	 * copy it here so it is available after the compound returns
	 *
	 * copy_offload_state_free() frees it on final refcount drop.
	 */
	cos->cos_dst_fh.nfs_fh4_len = data->currentFH.nfs_fh4_len;
	cos->cos_dst_fh.nfs_fh4_val = gsh_malloc(data->currentFH.nfs_fh4_len,
						 MEM_COMP_XCOPY);
	memcpy(cos->cos_dst_fh.nfs_fh4_val, data->currentFH.nfs_fh4_val,
	       data->currentFH.nfs_fh4_len);

	LogDebug(COMPONENT_NFS_V4, "COPY async: created offload state seqid=%u",
		 cos->cos_state.state_seqid);
	return cos;
}

/**
 * @brief Release the owner's reference on a copy offload state.
 *
 * Uses nfs4_State_Del() (hash-only removal) — NOT state_del().
 *
 * WHY NOT state_del():
 *   state_del() -> _state_del_locked() unconditionally calls
 *   obj->obj_ops->close2() (nfs4_state.c:577).  mdcache_close2() calls
 *   mdc_cur_export() which dereferences op_ctx->fsal_export.  This
 *   function is called from cb_offload_completion() which runs in the
 *   RPC callback thread where op_ctx has no valid fsal_export -> crash.
 *   Additionally, _state_del_locked() calls dec_state_t_ref() as the
 *   "sentinel" release (nfs4_state.c:589).  Our refcount starts at 1
 *   (not 2 as _state_add_impl sets), so that drop would immediately
 *   trigger gsh_free(cos) before we finish this function -> use-after-free.
 *
 * Manual cleanup mirrors _state_del_locked() without the close2 call:
 *   1. nfs4_State_Del       — removes from ht_state_id (no list changes)
 *   2. state_mutex          — clear state_obj + put_ref(dst)
 *   3. dec_state_owner_ref  — drops the permanent hold on cid_owner
 *   4. put_gsh_export       — drops the export ref
 *   5. dec_client_id_ref    — drops the clientid hold taken at create
 *   6. dec_state_t_ref      — drops owner's ref; gsh_free(cos) when last
 */
static void destroy_copy_offload_state(struct copy_offload_state *cos)
{
	struct fsal_obj_handle *dst;
	state_owner_t *owner;
	struct gsh_export *export;

	if (cos == NULL)
		return;

	/* Remove from stateid hash table only. */
	nfs4_State_Del(&cos->cos_state);

	/*
	 * Release state_obj ref under state_mutex so that
	 * get_state_obj_ref_locked() in a concurrent nfs4_Check_Stateid()
	 * cannot race with the NULL assignment.
	 */
	PTHREAD_MUTEX_lock(&cos->cos_state.state_mutex);
	dst = cos->cos_state.state_obj;
	cos->cos_state.state_obj = NULL;
	owner = cos->cos_state.state_owner;
	cos->cos_state.state_owner = NULL;
	export = cos->cos_state.state_export;
	cos->cos_state.state_export = NULL;
	PTHREAD_MUTEX_unlock(&cos->cos_state.state_mutex);

	if (dst)
		dst->obj_ops->put_ref(dst);

	/* Drop the permanent hold on cid_owner taken during create. */
	if (owner)
		dec_state_owner_ref(owner);

	/* Drop the export ref taken during create. */
	if (export)
		put_gsh_export(export);

	/* cos-specific fields not tracked by state_t. */
	if (cos->cos_clientid) {
		dec_client_id_ref(cos->cos_clientid);
		cos->cos_clientid = NULL;
	}

	/*
	 * Drop the owner's reference on state_t.
	 * When refcount reaches zero dec_state_t_ref() calls:
	 *   PTHREAD_MUTEX_destroy(&cos->cos_state.state_mutex)
	 *   copy_offload_state_free() -> gsh_free(cos)
	 * Any concurrent nfs4_Check_Stateid() that bumped the refcount
	 * defers this until its own dec_state_t_ref() fires last.
	 */
	dec_state_t_ref(&cos->cos_state);
}

/**
 * Forward declaration — nfs4_copy_send_cb_offload is defined below but
 * called from cb_offload_completion (retry path). */
static void nfs4_copy_send_cb_offload(struct copy_offload_state *cos,
				      uint64_t bytes_copied, fsal_status_t st,
				      const verifier4 write_verifier);

/**
 * @brief Completion hook called by the RPC machinery after CB_OFFLOAD
 *        is acknowledged (or aborted).
 *
 * If the client returns a transient error  (NFS4ERR_RESOURCE, NFS4ERR_DELAY)
 * the server SHOULD retransmit CB_OFFLOAD.
 * We implement this by retrying up to NFS4_COPY_CB_MAX_RETRIES times using the
 * completion parameters cached in cos->cb_*.
 * Each retry calls nfs4_copy_send_cb_offload(), which registers a new
 * cb_offload_completion hook; on success cos
 * ownership transfers to that new hook, on failure it is destroyed.
 *
 * On NFS_CB_CALL_ABORTED (back-channel down) or after exhausting all
 * retries we destroy the state unconditionally: any subsequent
 * OFFLOAD_STATUS from the client will get NFS4ERR_BAD_STATEID, which
 * is how the client learns the copy is done (one way or another).
 *
 * For NFSv4.1 the back-channel slot must be released via
 * nfs41_release_single() BEFORE we retry or destroy, since
 * nfs41_release_single() touches call->chan->source.session which is
 * only valid while 'call' is alive.
 *
 * @param[in] call  The completed (or aborted) RPC call.
 *                  call->call_arg carries the copy_offload_state pointer.
 */
static void cb_offload_completion(rpc_call_t *call)
{
	struct copy_offload_state *cos = call->call_arg;
	nfsstat4 compound_st;
	bool do_retry;

	/*
	 * For NFSv4.1, release the back-channel slot BEFORE any retry or
	 * destroy.  nfs41_release_single() touches call->chan->source.session
	 * which must still be valid here.
	 */
	if (cos != NULL && cos->cos_clientid != NULL &&
	    cos->cos_clientid->cid_minorversion != 0)
		nfs41_release_single(call);

	if (call->states & NFS_CB_CALL_ABORTED) {
		LogWarn(COMPONENT_NFS_V4,
			"CB_OFFLOAD: call aborted (back-channel down) seqid=%u",
			cos ? cos->cos_state.state_seqid : 0);
		goto out;
	}

	compound_st = call->cbt.v_u.v4.res.status;
	do_retry = (cos != NULL && compound_st != NFS4_OK &&
		    cos->cos_cb_retry_count < NFS4_COPY_CB_MAX_RETRIES);

	if (compound_st == NFS4_OK) {
		LogDebug(COMPONENT_NFS_V4, "CB_OFFLOAD: acknowledged seqid=%u",
			 cos ? cos->cos_state.state_seqid : 0);
	} else if (do_retry) {
		cos->cos_cb_retry_count++;
		LogWarn(COMPONENT_NFS_V4,
			"CB_OFFLOAD: ret stat %d retry %d/%d seqid=%u",
			compound_st, cos->cos_cb_retry_count,
			NFS4_COPY_CB_MAX_RETRIES, cos->cos_state.state_seqid);
	} else {
		LogWarn(COMPONENT_NFS_V4,
			"CB_OFFLOAD: ret stat %d giving up seqid=%u",
			compound_st, cos ? cos->cos_state.state_seqid : 0);
	}

	if (do_retry) {
		nfs4_copy_send_cb_offload(cos, cos->cos_cb_bytes_copied,
					  cos->cos_cb_fsal_status,
					  cos->cos_cb_write_verifier);
		return;
	}

out:
	destroy_copy_offload_state(cos);
}

/**
 * @brief Build and dispatch a CB_OFFLOAD callback for a completed copy.
 *
 * On success the copy_offload_state ownership is transferred to
 * cb_offload_completion (which calls destroy_copy_offload_state).
 * On send failure we call destroy_copy_offload_state directly so the
 * state is always cleaned up.
 *
 * @param[in] cos            Copy offload state (caller gives up ownership)
 * @param[in] bytes_copied   Bytes successfully transferred
 * @param[in] st             Final FSAL status (ERR_FSAL_NO_ERROR on success)
 * @param[in] write_verifier Server write verifier captured while op_ctx
 *                           was still valid (only meaningful when !error)
 */
static void nfs4_copy_send_cb_offload(struct copy_offload_state *cos,
				      uint64_t bytes_copied, fsal_status_t st,
				      const verifier4 write_verifier)
{
	nfs_cb_argop4 op;
	CB_OFFLOAD4args *args;
	int rc;

	if (cos == NULL)
		return;

	if (cos->cos_clientid == NULL) {
		LogWarn(COMPONENT_NFS_V4,
			"CB_OFFLOAD: no clientid, skipping callback");
		destroy_copy_offload_state(cos);
		return;
	}

	memset(&op, 0, sizeof(op));
	op.argop = NFS4_OP_CB_OFFLOAD;
	args = &op.nfs_cb_argop4_u.opcboffload;

	args->coa_fh = cos->cos_dst_fh;
	COPY_STATEID(&args->coa_stateid, &cos->cos_state);

	if (FSAL_IS_ERROR(st)) {
		/*
		 * On error:
		 * coa_status  : carries the error
		 * coa_bytes_copied : carries how many bytes we
		 * managed to copy before the failure/cancellation.
		 */
		args->coa_status = nfs4_Errno_status(st);
		args->coa_payload.coa_bytes_copied = (length4)bytes_copied;
		LogDebug(COMPONENT_NFS_V4,
			 "CB_OFFLOAD: error status=%d bytes_copied=%" PRIu64
			 " seqid=%u",
			 args->coa_status, bytes_copied,
			 cos->cos_state.state_seqid);
	} else {
		/* success */
		args->coa_status = NFS4_OK;
		args->coa_payload.coa_resok4.wr_ids = 0;
		args->coa_payload.coa_resok4.wr_count = (length4)bytes_copied;
		args->coa_payload.coa_resok4.wr_committed = FILE_SYNC4;
		memcpy(args->coa_payload.coa_resok4.wr_writeverf,
		       write_verifier, sizeof(verifier4));

		LogDebug(COMPONENT_NFS_V4,
			 "CB_OFFLOAD: sending OK bytes=%" PRIu64 " seqid=%u",
			 bytes_copied, cos->cos_state.state_seqid);
	}

	/*
	 * Dispatch the callback.  On success, ownership of cos transfers
	 * to cb_offload_completion.  On failure, we clean up here.
	 */
	rc = nfs_rpc_cb_single(cos->cos_clientid, &op, NULL,
			       cb_offload_completion, cos);
	if (rc != 0) {
		LogWarn(COMPONENT_NFS_V4,
			"CB_OFFLOAD: failed (%d) destroying state directly",
			rc);
		destroy_copy_offload_state(cos);
	}
}

/** Return the total bytes to copy for the job. */
static inline uint64_t copy_job_total(struct copy_job *job)
{
	return (job->job_mode == COPY_MODE_ASYNC)
		       ? job->job_cos->cos_total_count
		       : job->job_ctx.to_copy;
}

/**
 * @brief cancellation check applicable as below.
 * sync : Server shutdown.
 * Async: Server shutdown, Lease expiry, offload_cancel.
 *
 * return : bool, should continue or not.
 */
static inline bool copy_should_continue(bool is_async,
					struct copy_offload_state *cos)
{
	if (is_async && atomic_fetch_uint8_t((uint8_t *)&cos->cos_cancelled)) {
		LogDebug(COMPONENT_NFS_V4,
			 "COPY worker stopping OFFLOAD_CANCEL received");
		return false;
	}

	if (atomic_fetch_uint8_t(&copy_fridge_stopping)) {
		LogEvent(COMPONENT_THREAD,
			 "COPY worker stopping: server shutdown in progress");
		return false;
	}

	if (is_async && cos->cos_clientid != NULL &&
	    cos->cos_clientid->cid_confirmed == EXPIRED_CLIENT_ID) {
		LogDebug(COMPONENT_NFS_V4,
			 "COPY worker stopping: client %" PRIu64
			 " lease expired",
			 cos->cos_clientid->cid_clientid);
		return false;
	}

	return true;
}

static inline void copy_async_update_progress(uint64_t bytes_copied,
					      struct copy_offload_state *cos)
{
	atomic_store_uint64_t(&cos->cos_bytes_copied, bytes_copied);
}

/**
 * @brief Shared copy loop executed by every xcopy worker.
 *
 * @param[in]  job     Unified copy job (read-only execution fields)
 * @param[out] result  Filled with timing and copy outcome
 *
 */
static void copy_do_work(struct copy_job *job, struct copy_work_result *result)
{
	const struct copy_ctx *cctx = &job->job_ctx;
	bool is_async = (job->job_mode == COPY_MODE_ASYNC);
	struct copy_offload_state *cos = is_async ? job->job_cos : NULL;
	const char *label = is_async ? "ASYNC" : "SYNC";
	uint64_t copy_chunk_size =
		nfs_param.nfsv4_param.copy_offload_chunk_size;
	/* Mutable offset trackers — advance as each chunk completes. */
	uint64_t total = copy_job_total(job);
	uint64_t src_off = cctx->src_off;
	uint64_t dst_off = cctx->dst_off;

	uint64_t remaining = total;
	uint64_t bytes_copied = 0;
	uint64_t chunk_num = 0;
	uint64_t chunk = 0;
	uint64_t chunk_copied = 0;
	fsal_status_t st = { ERR_FSAL_NO_ERROR, 0 };
	struct timespec t_start, t_end;
	nsecs_elapsed_t elapsed_ns;

	now_mono(&t_start);
	LogDebug(COMPONENT_NFS_V4,
		 "COPY %s worker START: total=%" PRIu64
		 " bytes src_off=%" PRIu64 " dst_off=%" PRIu64
		 " chunk_size=%" PRIu64,
		 label, total, src_off, dst_off, (uint64_t)copy_chunk_size);

	while (remaining > 0) {
		chunk = MIN(remaining, copy_chunk_size);
		chunk_copied = 0;

		if (!copy_should_continue(is_async, cos)) {
			/*
			 * ERR_FSAL_INTERRUPT is the sentinel used by async
			 * post-loop code to distinguish cancel/shutdown from
			 * a real FSAL error.
			 */
			st = fsalstat(ERR_FSAL_INTERRUPT, 0);
			break;
		}

		LogFullDebug(COMPONENT_NFS_V4,
			     "COPY %s chunk #%" PRIu64 ": src_off=%" PRIu64
			     " len=%" PRIu64 " progress=%" PRIu64 "/%" PRIu64,
			     label, chunk_num, src_off, chunk, bytes_copied,
			     total);

		st = cctx->src->obj_ops->copy_file_range(
			cctx->src, cctx->src_state, cctx->dst, cctx->dst_state,
			src_off, dst_off, chunk, &chunk_copied);
		if (FSAL_IS_ERROR(st)) {
			LogWarn(COMPONENT_NFS_V4,
				"COPY %s FSAL ERR chunk #%" PRIu64
				" offset=%" PRIu64
				": major=%u minor=%u (copied so far: %" PRIu64
				"/%" PRIu64 ")",
				label, chunk_num, src_off, st.major, st.minor,
				bytes_copied, total);
			break;
		}

		bytes_copied += chunk_copied;
		src_off += chunk_copied;
		dst_off += chunk_copied;
		remaining -= chunk_copied;
		chunk_num++;

		/* Update bytes_copied for OFFLOAD_STATUS progress*/
		if (is_async)
			copy_async_update_progress(bytes_copied, cos);

		if (nfs_param.nfsv4_param.short_copy)
			break;

		if (chunk_copied < chunk) {
			LogDebug(COMPONENT_NFS_V4,
				 "COPY %s short chunk #%" PRIu64
				 ": req=%" PRIu64 " got=%" PRIu64
				 " (EOF) — stopping",
				 label, chunk_num - 1, chunk, chunk_copied);
			break;
		}
	}

	now_mono(&t_end);
	elapsed_ns = timespec_diff(&t_start, &t_end);
	result->elapsed_ms = (uint64_t)(elapsed_ns / 1000000ULL);
	result->rate_kbps = result->elapsed_ms > 0
				    ? (bytes_copied * 1000ULL /
				       result->elapsed_ms / 1024ULL)
				    : 0;
	result->bytes_copied = bytes_copied;
	result->chunk_num = chunk_num;
	result->st = st;
}

/**
 * @brief Release all FSAL/state/export refs held by a copy job and
 *        tear down the per-worker op_ctx.
 *
 * Called once by every worker path (SYNC and ASYNC) before signalling
 * the compound or dispatching CB_OFFLOAD.  Always leaves the ctx fields
 * NULL so any later defensive cleanup path sees a consistent zeroed state.
 */
static void copy_worker_teardown(struct copy_job *job)
{
	struct copy_ctx *ctx = &job->job_ctx;

	if (ctx->src_state) {
		dec_state_t_ref(ctx->src_state);
		ctx->src_state = NULL;
	}
	if (ctx->dst_state) {
		dec_state_t_ref(ctx->dst_state);
		ctx->dst_state = NULL;
	}
	if (ctx->src) {
		ctx->src->obj_ops->put_ref(ctx->src);
		ctx->src = NULL;
	}
	if (ctx->dst) {
		ctx->dst->obj_ops->put_ref(ctx->dst);
		ctx->dst = NULL;
	}

	release_op_context();
	ctx->ctx_export = NULL;
	ctx->fsal_export = NULL;
}

/**
 * @brief Complete a synchronous copy job.
 *
 * Stores the result in the job, tears down worker refs, decrements the
 * active-copy counter, and wakes the compound via svc_resume().
 * The job is freed by nfs4_op_copy_resume() after it reads the result.
 */
static void copy_complete_sync(struct copy_job *job,
			       const struct copy_work_result *wr)
{
	job->job_status = FSAL_IS_ERROR(wr->st) ? nfs4_Errno_status(wr->st)
						: NFS4_OK;
	job->job_copied = wr->bytes_copied;
	copy_worker_teardown(job);
	svc_resume(job->job_cdata->req);
}

/**
 * @brief Update cos state fields and cache CB_OFFLOAD retry parameters.
 *
 * Captures the write verifier while op_ctx is still valid (must be called
 * before copy_worker_teardown).
 */
static void copy_async_finalize(struct copy_offload_state *cos,
				const struct copy_work_result *wr,
				bool client_cancelled, verifier4 write_verifier)
{
	if (!FSAL_IS_ERROR(wr->st)) {
		struct gsh_buffdesc verf_desc = {
			.addr = write_verifier,
			.len = sizeof(verifier4),
		};

		op_ctx->fsal_export->exp_ops.get_write_verifier(
			op_ctx->fsal_export, &verf_desc);
	}

	cos->cos_status = (FSAL_IS_ERROR(wr->st) && !client_cancelled)
				  ? nfs4_Errno_status(wr->st)
				  : NFS4_OK;
	atomic_store_uint64_t(&cos->cos_bytes_copied, wr->bytes_copied);
	atomic_store_uint8_t(&cos->cos_complete, 1);

	cos->cos_cb_bytes_copied = wr->bytes_copied;
	cos->cos_cb_fsal_status = wr->st;
	memcpy(cos->cos_cb_write_verifier, write_verifier, sizeof(verifier4));
}

/**
 * @brief Complete an asynchronous copy job.
 *
 * Classifies the stop reason, finalises the cos state, tears down worker
 * refs, frees the job, then either destroys the state silently (client-
 * cancelled) or fires CB_OFFLOAD.
 */
static void copy_complete_async(struct copy_job *job,
				struct copy_work_result *wr)
{
	struct copy_offload_state *cos = job->job_cos;
	verifier4 write_verifier;
	bool client_cancelled = atomic_fetch_uint8_t(&cos->cos_cancelled);

	memset(write_verifier, 0, sizeof(write_verifier));

	copy_async_finalize(cos, wr, client_cancelled, write_verifier);

	copy_worker_teardown(job);
	gsh_free(job, MEM_COMP_XCOPY);
	/* on client initiated cancel, no need of callback to client */
	if (client_cancelled) {
		destroy_copy_offload_state(cos);
		return;
	}

	nfs4_copy_send_cb_offload(cos, wr->bytes_copied, wr->st,
				  write_verifier);
}

/*
 * Unified xcopy worker entry point
 *
 * Thin dispatcher: claim thread name, establish op_ctx, run the copy
 * loop, then hand off to the mode-specific completion function.
 **/
static void copy_worker(struct fridgethr_context *fridge_ctx)
{
	struct copy_job *job = fridge_ctx->arg;
	struct copy_work_result wr;
	struct req_op_context op_context;

	init_op_context_simple(&op_context, job->job_ctx.ctx_export,
			       job->job_ctx.fsal_export);

	copy_do_work(job, &wr);

	if (job->job_mode == COPY_MODE_SYNC)
		copy_complete_sync(job, &wr);
	else
		copy_complete_async(job, &wr);
}

static void copy_log_request(const COPY4args *args)
{
	/*
	 * ca_synchronous == TRUE  -> client demands inline copy; server MUST
	 *                           honour it or return NFS4ERR_OFFLOAD_NO_REQS
	 *                           with cr_synchronous=FALSE.
	 * ca_synchronous == FALSE -> client allows async; server MAY choose
	 *                           async (returns wr_ids=1,
	 *                           cr_synchronous=FALSE + copy stateid)
	 *                           OR
	 *                           fall back to sync.
	 */
	LogDebug(COMPONENT_NFS_V4,
		 "COPY request: ca_src_off=%" PRIu64 " ca_dst_off=%" PRIu64
		 " ca_count=%" PRIu64 " ca_con=%d ca_sync=%d ca_src_ser_len=%u",
		 args->ca_src_offset, args->ca_dst_offset, args->ca_count,
		 (int)args->ca_consecutive, (int)args->ca_synchronous,
		 args->ca_source_server_len);
}

/**
 * @brief Validate handles, stateids, access, and compute copy length.
 *
 * Writes into the already-allocated @a ctx (zeroed by the caller).
 *
 * On success the caller owns ctx->src_state / ctx->dst_state and MUST
 * release them via dec_state_t_ref().
 *
 * On error this function releases any state refs it has already acquired
 * before returning, so the caller never needs to inspect ctx on failure.
 */
static nfsstat4 copy_validate(COPY4args *args, compound_data_t *data,
			      struct copy_ctx *ctx)
{
	struct fsal_attrlist attrs;
	struct saved_export_context saved_src_ctx = { NULL };
	fsal_status_t fsal_st;
	uint64_t src_size = 0;
	uint64_t to_copy = args->ca_count;
	uint64_t MaxOffsetWrite;
	nfsstat4 status;

	ctx->src_off = args->ca_src_offset;
	ctx->dst_off = args->ca_dst_offset;

	if (args->ca_source_server_len > 0)
		return NFS4ERR_NOTSUPP;

	status = nfs4_sanity_check_FH(data, REGULAR_FILE, false);
	if (status != NFS4_OK)
		return status;

	status = nfs4_sanity_check_saved_FH(data, REGULAR_FILE, false);
	if (status != NFS4_OK)
		return status;

	ctx->src = data->saved_obj;
	ctx->dst = data->current_obj;

	if (ctx->src == ctx->dst)
		return NFS4ERR_INVAL;

	status = check_copy_stateid(&args->ca_src_stateid, ctx->src, data,
				    false, &ctx->src_state);
	if (status != NFS4_OK)
		return status;

	status = check_copy_stateid(&args->ca_dst_stateid, ctx->dst, data, true,
				    &ctx->dst_state);
	if (status != NFS4_OK)
		goto err_release;

	fsal_st = ctx->src->obj_ops->test_access(ctx->src, FSAL_READ_ACCESS,
						 NULL, NULL, true);
	if (FSAL_IS_ERROR(fsal_st)) {
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}

	fsal_st = ctx->dst->obj_ops->test_access(ctx->dst, FSAL_WRITE_ACCESS,
						 NULL, NULL, true);
	if (FSAL_IS_ERROR(fsal_st)) {
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}

	/*
	 * Cross-FSAL guard: op_ctx->fsal_export currently points at the dst
	 * export (set by the preceding PUTFH for the copy destination).
	 * Temporarily switch op_ctx to the saved (src) export for this call.
	 */
	get_gsh_export_ref(data->saved_export);
	save_op_context_export_and_set_export(&saved_src_ctx,
					      data->saved_export);
	op_ctx->export_perms = data->saved_export_perms;
	fsal_prepare_attrs(&attrs, ATTR_SIZE);
	fsal_st = ctx->src->obj_ops->getattrs(ctx->src, &attrs);
	restore_op_context_export(&saved_src_ctx);

	if (FSAL_IS_ERROR(fsal_st)) {
		fsal_release_attrs(&attrs);
		status = nfs4_Errno_status(fsal_st);
		goto err_release;
	}
	if (FSAL_TEST_MASK(attrs.valid_mask, ATTR_SIZE))
		src_size = attrs.filesize;
	fsal_release_attrs(&attrs);

	if (args->ca_count == 0) {
		to_copy = (ctx->src_off < src_size) ? (src_size - ctx->src_off)
						    : 0;
	} else if (ctx->src_off >= src_size ||
		   ctx->src_off + to_copy > src_size) {
		/*
		 * If the source offset or the source offset plus count
		 * is greater than the size of the source file, the
		 * operation MUST fail with NFS4ERR_INVAL.
		 * Silently clipping the range is NOT permitted.
		 */
		status = NFS4ERR_INVAL;
		goto err_release;
	}

	ctx->to_copy = to_copy;
	if (to_copy == 0)
		return NFS4_OK;

	MaxOffsetWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetWrite);
	if (MaxOffsetWrite < UINT64_MAX &&
	    ctx->dst_off + to_copy > MaxOffsetWrite) {
		status = NFS4ERR_FBIG;
		goto err_release;
	}

	return NFS4_OK;

err_release:
	if (ctx->src_state != NULL) {
		dec_state_t_ref(ctx->src_state);
		ctx->src_state = NULL;
	}
	if (ctx->dst_state != NULL) {
		dec_state_t_ref(ctx->dst_state);
		ctx->dst_state = NULL;
	}
	return status;
}

/**
 * @brief Decide whether to use the async offload path.
 *
 * Async is selected when ALL of the following hold:
 *   1. ca_synchronous == FALSE
 *   2. NFSv4.0 back-channel-down case:
 *   The server MAY choose sync when the back-channel is confirmed down
 *   and the client set ca_synchronous=FALSE (allowed async, not demanded it).
 *   This is the ONLY case where the server may choose sync over async.
 */
static bool copy_want_async(const COPY4args *args, compound_data_t *data,
			    uint64_t to_copy)
{
	bool do_async;
	nfs_client_id_t *clid = data->preserved_clientid;
	uint32_t minorver;

	do_async = !args->ca_synchronous;

	if (!do_async || clid == NULL)
		return do_async;

	minorver = clid->cid_minorversion;

	if (minorver == 0 && get_cb_chan_down(clid)) {
		LogWarn(COMPONENT_NFS_V4,
			"COPY: NFSv4.0 client %" PRIx64 " BC down, doing sync ",
			clid->cid_clientid);
		return false;
	}

	LogDebug(COMPONENT_NFS_V4,
		 "COPY back-channel check: minor_ver=%" PRIu32
		 "%s - back-channel %s for CB_OFFLOAD",
		 minorver,
		 minorver == 0	 ? " (NFSv4.0)"
		 : minorver == 1 ? " (NFSv4.1/session)"
				 : " (NFSv4.2/session)",
		 (minorver == 0 && get_cb_chan_down(clid)) ? "DOWN (will abort)"
							   : "UP");

	return do_async;
}

static void copy_log_dispatch(const COPY4args *args, uint64_t to_copy,
			      bool do_async)
{
	LogDebug(COMPONENT_NFS_V4,
		 "COPY dispatch: to_copy=%" PRIu64
		 " ca_consecutive=%d ca_synchronous=%d  min_size=%" PRIu64
		 " -> %s[RESPONSE will have: wr_ids=%d cr_synchronous=%d]",
		 to_copy, (int)args->ca_consecutive, (int)args->ca_synchronous,
		 nfs_param.nfsv4_param.copy_offload_chunk_size,
		 do_async ? "ASYNC (CB_OFFLOAD)" : "SYNC (inline)",
		 do_async ? 1 : 0, do_async ? 0 : 1);
}

/**
 * @brief Fill COPY4resok — the single place that stamps the RPC reply.
 *
 * Sync (RFC 7862 15.2.3):
 *   wr_ids=0, wr_count=<bytes>, FILE_SYNC4, cr_synchronous=TRUE
 *
 * Async (RFC 7862 15.2.3):
 *   wr_ids=1, wr_callback_id=<copy stateid>, wr_count=0, UNSTABLE4,
 *   cr_synchronous=FALSE; CB_OFFLOAD follows when the worker finishes.
 */
static void fill_copy_response(COPY4res *res, compound_data_t *data, bool async,
			       struct copy_offload_state *cos,
			       uint64_t sync_bytes_copied)
{
	COPY4resok *resok = &res->COPY4res_u.cr_resok4;

	res->cr_status = NFS4_OK;
	resok->cr_consecutive = TRUE;

	if (async) {
		resok->cr_response.wr_ids = 1;

		/*
		 * update_stateid() increments cos->cos_state.state_seqid from
		 * 0 -> 1 and stamps both the response stateid and
		 * data->current_stateid, exactly as nfs4_op_open() does.
		 * This is the standard SAL convention for seqid initialisation
		 * — do NOT use COPY_STATEID() here.
		 */
		update_stateid_locked(&cos->cos_state,
				      &resok->cr_response.wr_callback_id, data,
				      "COPY-OFFLOAD");

		resok->cr_response.wr_count = 0;
		resok->cr_response.wr_committed = UNSTABLE4;
		memset(resok->cr_response.wr_writeverf, 0, sizeof(verifier4));
		resok->cr_synchronous = FALSE;

		LogDebug(COMPONENT_NFS_V4,
			 "COPY async RESPONSE: wr_ids=1 to_copy=%" PRIu64
			 "cr_sync=FALSE cr_cons=TRUE copy_stateid_seqid=%u",
			 cos->cos_total_count, cos->cos_state.state_seqid);
		return;
	} else {
		struct gsh_buffdesc verf_desc;

		resok->cr_response.wr_ids = 0;
		resok->cr_response.wr_count = (length4)sync_bytes_copied;
		resok->cr_response.wr_committed = FILE_SYNC4;

		verf_desc.addr = resok->cr_response.wr_writeverf;
		verf_desc.len = sizeof(verifier4);
		op_ctx->fsal_export->exp_ops.get_write_verifier(
			op_ctx->fsal_export, &verf_desc);

		resok->cr_synchronous = TRUE;

		LogDebug(COMPONENT_NFS_V4,
			 "COPY sync response: wr_count=%" PRIu64
			 " wr_ids=0 cr_synchronous=TRUE",
			 sync_bytes_copied);
	}
}

/**
 * @brief Complete a synchronous COPY — fill the inline COPY4res.
 *
 * This is the copy equivalent of nfs4_complete_write().  By the time this
 * is called by nfs4_op_copy_resume(), copy_worker() (SYNC path) has already
 * released all execution refs (state, obj, export).
 * This function is therefore a pure response-fill: no ref management here.
 *
 * It does NOT free data->op_data — that is the caller's (nfs4_op_copy_resume)
 * responsibility
 *
 * @return NFS_REQ_OK / NFS_REQ_ERROR.  Never NFS_REQ_ASYNC_WAIT.
 */
static enum nfs_req_result nfs4_copy_complete(struct copy_job *job,
					      compound_data_t *data)
{
	COPY4res *res = job->job_res;
	enum nfs_req_result rc;

	LogDebug(COMPONENT_NFS_V4, "job_status:%d copied :%" PRIu64,
		 job->job_status, job->job_copied);
	if (job->job_status == NFS4_OK)
		fill_copy_response(res, data, false, NULL, job->job_copied);
	else
		res->cr_status = job->job_status;

	rc = nfsstat4_to_nfs_req_result(res->cr_status);

	GSH_AUTO_TRACEPOINT(nfs4, op_copy_end, TRACE_INFO,
			    "COPY status={} bytes={}", res->cr_status,
			    res->cr_status,
			    res->cr_status == NFS4_OK
				    ? (uint64_t)res->COPY4res_u.cr_resok4
					      .cr_response.wr_count
				    : 0ULL);

	return rc;
}

/**
 * @brief Resume handler for the synchronous COPY path.
 *
 * Called by the compound machinery after copy_worker() (SYNC mode) fires
 * svc_resume().  Delegates all response-filling to nfs4_copy_complete(),
 * then frees op_data.
 */
enum nfs_req_result nfs4_op_copy_resume(struct nfs_argop4 *op,
					compound_data_t *data,
					struct nfs_resop4 *resp)
{
	struct copy_job *job = data->op_data;
	enum nfs_req_result rc;

	LogDebug(COMPONENT_NFS_V4, "job:%p", job);
	if (job == NULL) {
		/* Should not happen — defensive. */
		LogWarn(COMPONENT_NFS_V4, "op_data is NULL");
		return NFS_REQ_ERROR;
	}

	rc = nfs4_copy_complete(job, data);

	/* NOTE: we do not expect rc == NFS_REQ_ASYNC_WAIT */
	assert(rc != NFS_REQ_ASYNC_WAIT);

	if (rc != NFS_REQ_ASYNC_WAIT) {
		/* We are completely done with the copy.  Free op_data exactly
		 * as nfs4_op_write_resume() frees write_data after
		 * nfs4_complete_write() returns.
		 */
		gsh_free(data->op_data, MEM_COMP_XCOPY);
		data->op_data = NULL;
	}

	return rc;
}

/**
 * @brief Allocate a copy_job, take worker refs, and submit to the xcopy fridge.
 *
 * Shared helper for both the ASYNC and SYNC paths.  The two modes differ only
 * in which job fields are populated and what the worker does on completion;
 * the ref-taking, fridge submission, and ref-rollback logic is identical.
 *
 * On COPY_DISPATCHED:
 *   - The worker owns all execution refs (src/dst get_ref, export ref,
 *     src_state/dst_state).  ctx->src_state/dst_state are NULLed here.
 *   - For ASYNC: *cos_out is set; caller passes it to fill_copy_response().
 *   - For SYNC:  data->op_data is set to the job; caller returns
 *                NFS_REQ_ASYNC_WAIT and waits for svc_resume().
 *
 * On COPY_NO_RESOURCES:
 *   - All refs are undone here; ctx is unchanged (out: label releases states).
 *   - Caller MUST return NFS4ERR_OFFLOAD_DENIED to the client.
 */
static enum copy_dispatch_result copy_submit_job(
	compound_data_t *data, struct copy_ctx *ctx, enum copy_mode mode,
	COPY4res *sync_res, struct copy_offload_state **cos_out)
{
	struct copy_job *job;
	struct copy_offload_state *cos = NULL;
	struct copy_ctx *jctx;
	int submit_rc;

	if (cos_out)
		*cos_out = NULL;

	if (mode == COPY_MODE_ASYNC) {
		cos = create_copy_offload_state(data, ctx->dst, ctx->to_copy);
		if (cos == NULL) {
			/*
			 * State allocation failure (OOM or hash collision).
			 * Return NO_RESOURCES so the caller replies
			 * NFS4ERR_OFFLOAD_DENIED; client try normal copy.
			 */
			LogWarn(COMPONENT_NFS_V4,
				"COPY: create_copy_offload_state failed ");
			return COPY_NO_RESOURCES;
		}
	}

	/*
	 * Extra FSAL refs for src and dst: the compound returns immediately
	 * after a successful dispatch, releasing data->saved_obj /
	 * data->current_obj.
	 * The worker keeps independent refs via job_ctx.src/dst.
	 */
	ctx->src->obj_ops->get_ref(ctx->src);
	ctx->dst->obj_ops->get_ref(ctx->dst);

	job = gsh_malloc(sizeof(*job), MEM_COMP_XCOPY);
	jctx = &job->job_ctx;

	job->job_mode = mode;
	job->job_cos = cos; /* NULL for SYNC; cos pointer for ASYNC */

	/* Populate the worker execution context identical for both modes. */
	*jctx = *ctx;
	jctx->ctx_export = op_ctx->ctx_export;
	jctx->fsal_export = op_ctx->fsal_export;
	if (jctx->ctx_export)
		get_gsh_export_ref(jctx->ctx_export);

	/* SYNC-only bookkeeping, ignored by the worker on the ASYNC path. */
	if (mode == COPY_MODE_SYNC) {
		job->job_res = sync_res;
		job->job_cdata = data;
		data->op_data = job;
	}

	submit_rc = fridgethr_submit(copy_fridge, copy_worker, job);
	if (submit_rc == 0) {
		/*
		 * Transfer complete — worker owns all execution refs.
		 * Null src_state/dst_state in ctx so nfs4_op_copy's out:
		 * label skips the dec_state_t_ref calls.
		 */
		ctx->src_state = NULL;
		ctx->dst_state = NULL;
		if (cos_out)
			*cos_out = cos;
		return COPY_DISPATCHED;
	}

	/*
	 * fridgethr_submit() failed — roll back all refs and free the job.
	 * The only mode-specific steps after ref teardown are:
	 *   ASYNC: destroy_copy_offload_state(cos)
	 *   SYNC:  data->op_data = NULL  (was set optimistically above)
	 */
	jctx->src->obj_ops->put_ref(jctx->src);
	jctx->dst->obj_ops->put_ref(jctx->dst);
	jctx->src = NULL;
	jctx->dst = NULL;
	jctx->src_state = NULL;
	jctx->dst_state = NULL;
	if (jctx->ctx_export)
		put_gsh_export(jctx->ctx_export);
	jctx->ctx_export = NULL;
	jctx->fsal_export = NULL;

	if (mode == COPY_MODE_ASYNC)
		destroy_copy_offload_state(cos);
	else
		data->op_data = NULL;

	gsh_free(job, MEM_COMP_XCOPY);

	LogDebug(COMPONENT_NFS_V4,
		 "COPY: returning NFS4ERR_OFFLOAD_NO_REQ :%d %" PRIu32,
		 submit_rc, nfs_param.nfsv4_param.max_copy_workers);
	return COPY_NO_RESOURCES;
}

/**
 * @brief NFS4_OP_COPY — validate, dispatch, and complete the RPC reply.
 *
 * fill_copy_response() is called here for every outcome so that a reader
 * sees all response-field assignments in one function:
 *
 *   to_copy == 0        fill_copy_response(sync, 0 bytes)  [no counter]
 *   limit reached       res->cr_status = NFS4ERR_OFFLOAD_DENIED
 *   DISPATCHED (async)  fill_copy_response(async, cos) -> svc replies
 *   DISPATCHED (sync)   -> NFS_REQ_ASYNC_WAIT (resume fills response)
 *   NO_RESOURCES        res->cr_status = NFS4ERR_OFFLOAD_DENIED
 *   error               res->cr_status = <error code>
 *
 * Both sync and async copies are dispatched through the single xcopy fridge
 * via copy_submit_job().  There is no in-line blocking copy on the svc thread.
 * If the fridge is full (EWOULDBLOCK) for either mode the server returns
 * NFS4ERR_OFFLOAD_DENIED — the limit and response are identical regardless
 * of whether the client requested ca_synchronous=TRUE or FALSE.
 *
 */
enum nfs_req_result nfs4_op_copy(struct nfs_argop4 *op, compound_data_t *data,
				 struct nfs_resop4 *resp)
{
	COPY4args *const args = &op->nfs_argop4_u.opcopy;
	COPY4res *const res = &resp->nfs_resop4_u.opcopy;
	struct copy_ctx ctx = { 0 };
	struct copy_offload_state *cos = NULL;
	nfsstat4 status;
	enum copy_mode mode;
	bool do_async;

	resp->resop = NFS4_OP_COPY;
	res->cr_status = NFS4_OK;

	/* Master enable/disable gate — both sync and async require this. */
	if (!copy_fridge || !nfs_param.nfsv4_param.allow_copy_offload) {
		res->cr_status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	copy_log_request(args);

	status = copy_validate(args, data, &ctx);
	if (status != NFS4_OK) {
		res->cr_status = status;
		goto out;
	}

	/* Zero-byte copy — no real work, skip resource accounting. */
	if (ctx.to_copy == 0) {
		fill_copy_response(res, data, false, NULL, 0);
		goto out;
	}

	do_async = copy_want_async(args, data, ctx.to_copy);
	copy_log_dispatch(args, ctx.to_copy, do_async);

	mode = do_async ? COPY_MODE_ASYNC : COPY_MODE_SYNC;
	if (copy_submit_job(data, &ctx, mode, do_async ? NULL : res, &cos) ==
	    COPY_DISPATCHED) {
		if (do_async) {
			fill_copy_response(res, data, true, cos, 0);
			goto out;
		}
		goto out_async_wait;
	}

	/* Intimation for client to do normal read/write for cp */
	res->cr_status = NFS4ERR_OFFLOAD_DENIED;
out:
	/*
	 * Release stateid refs still in the local ctx.
	 * Both async-dispatched and sync-dispatched paths null these before
	 * reaching here; these null-guards are no-ops for those paths.
	 */
	if (ctx.src_state != NULL)
		dec_state_t_ref(ctx.src_state);
	if (ctx.dst_state != NULL)
		dec_state_t_ref(ctx.dst_state);

	GSH_AUTO_TRACEPOINT(nfs4, op_copy_end, TRACE_INFO,
			    "COPY status={} bytes={}", res->cr_status,
			    res->cr_status,
			    res->cr_status == NFS4_OK
				    ? (uint64_t)res->COPY4res_u.cr_resok4
					      .cr_response.wr_count
				    : 0ULL);
	return nfsstat4_to_nfs_req_result(res->cr_status);

out_async_wait:
	return NFS_REQ_ASYNC_WAIT;
}

void nfs4_op_copy_Free(nfs_resop4 *resp)
{
	/* Nothing to do */
}
