/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file nfs4_recovery.c
 * @brief NFSv4 recovery
 */

#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include <dlfcn.h>
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"

/* The grace_mutex protects current_grace, clid_list, and clid_count */
static pthread_mutex_t grace_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec current_grace; /* current grace period timeout */
static int clid_count; /* number of active clients */
static struct glist_head clid_list = GLIST_HEAD_INIT(clid_list);  /* clients */

/*
 * Low two bits of grace_status word are flags. One for whether we're currently
 * in a grace period and one if a change was requested.
 */
#define GRACE_STATUS_ACTIVE_SHIFT	0
#define GRACE_STATUS_CHANGE_REQ_SHIFT	1

/* The remaining bits are for the refcount */
#define GRACE_STATUS_COUNTER_SHIFT	2

#define GRACE_STATUS_ACTIVE		(1U << GRACE_STATUS_ACTIVE_SHIFT)
#define GRACE_STATUS_CHANGE_REQ		(1U << GRACE_STATUS_CHANGE_REQ_SHIFT)
#define GRACE_STATUS_REF_INCREMENT	(1U << GRACE_STATUS_COUNTER_SHIFT)
#define GRACE_STATUS_COUNT_MASK		((~0U) << GRACE_STATUS_COUNTER_SHIFT)

static uint32_t	grace_status;

static int default_recovery_init(void)
{
	return 0;
}

static void default_end_grace(void)
{
}

static void default_recovery_read_clids(nfs_grace_start_t *gsp,
					add_clid_entry_hook add_clid_entry,
					add_rfh_entry_hook add_rfs_entry)
{
}

static void default_add_clid(nfs_client_id_t *clientid)
{
}

static void default_rm_clid(nfs_client_id_t *clientid)
{
}

static void default_add_revoke_fh(nfs_client_id_t *dlr_clid,
				  nfs_fh4 *dlr_handle)
{
}

static struct nfs4_recovery_backend default_recovery_backend = {
	.recovery_init = default_recovery_init,
	.end_grace = default_end_grace,
	.recovery_read_clids = default_recovery_read_clids,
	.add_clid = default_add_clid,
	.rm_clid = default_rm_clid,
	.add_revoke_fh = default_add_revoke_fh,
};


static struct nfs4_recovery_backend *recovery_backend =
					&default_recovery_backend;
int32_t reclaim_completes; /* atomic */

static void nfs4_recovery_load_clids(nfs_grace_start_t *gsp);
static void nfs_release_nlm_state(char *release_ip);
static void nfs_release_v4_clients(char *ip);




clid_entry_t *nfs4_add_clid_entry(char *cl_name)
{
	clid_entry_t *new_ent = gsh_malloc(sizeof(clid_entry_t));

	glist_init(&new_ent->cl_rfh_list);
	(void) strlcpy(new_ent->cl_name, cl_name, sizeof(new_ent->cl_name));
	glist_add(&clid_list, &new_ent->cl_list);
	++clid_count;
	return new_ent;
}

rdel_fh_t *nfs4_add_rfh_entry(clid_entry_t *clid_ent, char *rfh_name)
{
	rdel_fh_t *new_ent = gsh_malloc(sizeof(rdel_fh_t));

	new_ent->rdfh_handle_str = gsh_strdup(rfh_name);
	glist_add(&clid_ent->cl_rfh_list, &new_ent->rdfh_list);
	return new_ent;
}

void nfs4_cleanup_clid_entries(void)
{
	struct clid_entry *clid_entry;
	/* when not doing a takeover, start with an empty list */
	while ((clid_entry = glist_first_entry(&clid_list,
					       struct clid_entry,
					       cl_list)) != NULL) {
		glist_del(&clid_entry->cl_list);
		gsh_free(clid_entry);
		--clid_count;
	}
	assert(clid_count == 0);
	atomic_store_int32_t(&reclaim_completes, 0);
}

/*
 * Check the current status of the grace period against what the caller needs.
 * If it's different then return false without taking a reference. If a change
 * has been requested, then we also don't want to give out a reference.
 */
bool nfs_get_grace_status(bool want_grace)
{
	uint32_t cur, pro, old;

	old = atomic_fetch_uint32_t(&grace_status);
	do {
		cur = old;

		/* If it's not the state we want, then no reference */
		if (want_grace != (bool)(cur & GRACE_STATUS_ACTIVE))
			return false;

		/* If a change was requested, no reference */
		if (cur & GRACE_STATUS_CHANGE_REQ)
			return false;

		/* Bump the counter */
		pro = cur + GRACE_STATUS_REF_INCREMENT;
		old = __sync_val_compare_and_swap(&grace_status, cur, pro);
	} while (old != cur);
	return true;
}

/*
 * Put grace status. If the refcount goes to zero, and a change was requested,
 * then wake the reaper thread to do its thing.
 */
void nfs_put_grace_status(void)
{
	uint32_t cur;

	cur = __sync_sub_and_fetch(&grace_status, GRACE_STATUS_REF_INCREMENT);
	if (cur & GRACE_STATUS_CHANGE_REQ &&
	    !(cur >> GRACE_STATUS_COUNTER_SHIFT)) {
		nfs_notify_grace_norefs_waiters();
		reaper_wake();
	}
}

/**
 * Lift the grace period if it's still active.
 */
static void
nfs_lift_grace_locked(void)
{
	uint32_t cur;

	/*
	 * Caller must hold grace_mutex. Only the thread that actually sets
	 * the value to 0 gets to clean up the recovery db.
	 */
	if (nfs_in_grace()) {
		nfs_end_grace();
		__sync_synchronize();
		/* Now change the actual status */
		cur = __sync_and_and_fetch(&grace_status,
			~(GRACE_STATUS_ACTIVE|GRACE_STATUS_CHANGE_REQ));
		assert(!(cur & GRACE_STATUS_COUNT_MASK));
		LogEvent(COMPONENT_STATE, "NFS Server Now NOT IN GRACE");
	}
}

/*
 * Report our new state to the cluster
 */
static void nfs4_set_enforcing(void)
{
	if (recovery_backend->set_enforcing)
		recovery_backend->set_enforcing();
}

/**
 * @brief Start grace period
 *
 * This routine can be called due to server start/restart or from
 * failover code.  If this node is taking over for a node, that nodeid
 * will be passed to this routine inside of the grace start structure.
 *
 * @param[in] gsp Grace period start information
 * Returns 0 on success, -EAGAIN on failure to enforce grace.
 */
int nfs_start_grace(nfs_grace_start_t *gsp)
{
	int ret = 0;
	bool was_grace;
	uint32_t cur, old, pro;

	PTHREAD_MUTEX_lock(&grace_mutex);

	if (nfs_param.nfsv4_param.graceless) {
		nfs_lift_grace_locked();
		LogEvent(COMPONENT_STATE,
			 "NFS Server skipping GRACE (Graceless is true)");
		goto out;
	}

	/* grace should always be greater than or equal to lease time,
	 * some clients are known to have problems with grace greater than 60
	 * seconds Lease_Lifetime should be set to a smaller value for those
	 * setups.
	 *
	 * Checks against the grace period are lockless, so we want to ensure
	 * that the callers see the
	 * Full barrier to ensure enforcement begins ASAP.
	 */

	/*
	 * Ensure there are no outstanding references to the current state of
	 * grace. If there are, set flag indicating that a change has been
	 * requested and that no more references will be handed out until it
	 * takes effect.
	 */
	ret = clock_gettime(CLOCK_MONOTONIC, &current_grace);
	if (ret != 0) {
		LogCrit(COMPONENT_MAIN, "Failed to get timestamp");
		assert(0);	/* if this is broken, we are toast so die */
	}

	cur = atomic_fetch_uint32_t(&grace_status);
	do {
		old = cur;
		was_grace = cur & GRACE_STATUS_ACTIVE;

		/* If we're already in a grace period then we're done */
		if (was_grace)
			break;

		/*
		 * Are there outstanding refs? If so, then set the change req
		 * flag and nothing else. If not, then clear the change req
		 * flag and flip the active bit.
		 */
		if (old & GRACE_STATUS_COUNT_MASK) {
			pro = old | GRACE_STATUS_CHANGE_REQ;
		} else {
			pro = old | GRACE_STATUS_ACTIVE;
			pro &= ~GRACE_STATUS_CHANGE_REQ;
		}

		/* If there are no changes, then we don't need to update */
		if (pro == old)
			break;
		cur = __sync_val_compare_and_swap(&grace_status, old, pro);
	} while (cur != old);

	/*
	 * If we were not in a grace period before and there were still
	 * references outstanding, then we can't do anything else.
	 * Fail with -EAGAIN so that caller can retry if needed.
	 */
	if (!was_grace && (old & GRACE_STATUS_COUNT_MASK)) {
		LogEvent(COMPONENT_STATE,
			 "Unable to start grace, grace status 0x%x",
			 grace_status);
		ret = -EAGAIN;
		goto out;
	}

	__sync_synchronize();

	if ((int)nfs_param.nfsv4_param.grace_period <
		(int)nfs_param.nfsv4_param.lease_lifetime) {
		LogWarn(COMPONENT_STATE,
		 "NFS Server GRACE duration should at least match LEASE period. Current configured values are GRACE(%d), LEASE(%d)",
		 (int)nfs_param.nfsv4_param.grace_period,
		 (int)nfs_param.nfsv4_param.lease_lifetime);
	}

	LogEvent(COMPONENT_STATE, "NFS Server Now IN GRACE, duration %d",
		 (int)nfs_param.nfsv4_param.grace_period);

	/* Set enforcing flag here */
	if (!was_grace)
		nfs4_set_enforcing();

	/*
	 * If we're just starting the grace period, then load the
	 * clid database. Don't load it however if we're extending the
	 * existing grace period.
	 */
	if (!gsp && !was_grace) {
		nfs4_cleanup_clid_entries();
		nfs4_recovery_load_clids(NULL);
	} else if (gsp && gsp->event != EVENT_JUST_GRACE) {
		/*
		 * if called from failover code and given a nodeid, then this
		 * node is doing a take over.  read in the client ids from the
		 * failing node.
		 */
		LogEvent(COMPONENT_STATE,
			 "NFS Server recovery event %d nodeid %d ip %s",
			 gsp->event, gsp->nodeid, gsp->ipaddr);

		if (gsp->event == EVENT_CLEAR_BLOCKED)
			cancel_all_nlm_blocked();
		else {
			nfs_release_nlm_state(gsp->ipaddr);
			if (gsp->event == EVENT_RELEASE_IP) {
				PTHREAD_MUTEX_unlock(&grace_mutex);
				nfs_release_v4_clients(gsp->ipaddr);
				return ret;
			}
			else {
				/*
				 * If we're already in a grace period,
				 * it can not break the existing count,
				 * other case, which should not be affected
				 * by the last count, should be cleanup.
				 */
				if (!was_grace)
					nfs4_cleanup_clid_entries();
				nfs4_recovery_load_clids(gsp);
			}
		}
	}
out:
	PTHREAD_MUTEX_unlock(&grace_mutex);
	return ret;
}

/**
 * @brief Check if we are in the grace period
 *
 * @retval true if so.
 * @retval false if not.
 */
bool nfs_in_grace(void)
{
	return atomic_fetch_uint32_t(&grace_status) & GRACE_STATUS_ACTIVE;
}

/**
 * @brief Enter the grace period if another node in the cluster needs it
 *
 * Singleton servers generally won't use this operation. Clustered servers
 * call this function to check whether another node might need a grace period.
 */
void nfs_maybe_start_grace(void)
{
	if (!nfs_in_grace() && recovery_backend->maybe_start_grace)
		recovery_backend->maybe_start_grace();
}

/**
 * @brief Are all hosts in cluster enforcing the grace period?
 *
 * Singleton servers always return true here since the only grace period that
 * matters is the local one. Clustered backends should check to make sure that
 * the whole cluster is in grace.
 */
bool nfs_grace_enforcing(void)
{
	if (recovery_backend->grace_enforcing)
		return recovery_backend->grace_enforcing();
	return true;
}

/**
 * @brief Is this host still a member of the cluster?
 *
 * Singleton servers are always considered to be cluster members. This call
 * is mainly for clustered servers, which may need to handle things differently
 * on a clean shutdown depending on whether they are still a member of the
 * cluster.
 */
bool nfs_grace_is_member(void)
{
	if (recovery_backend->is_member)
		return recovery_backend->is_member();
	return true;
}

/**
 * @brief Return nodeid for the server
 *
 * If the recovery backend specifies a nodeid, return it. If it does not
 * specify one, default to using the server's hostname.
 *
 * Returns 0 on success and fills out pnodeid. Caller must free the returned
 * value with gsh_free. Returns negative POSIX error code on error.
 */
int nfs_recovery_get_nodeid(char **pnodeid)
{
	int rc;
	long maxlen;
	char *nodeid = NULL;

	if (recovery_backend->get_nodeid) {
		rc = recovery_backend->get_nodeid(&nodeid);

		/* Return error if we got one */
		if (rc)
			return rc;

		/* If we got a nodeid, then we're done */
		if (nodeid) {
			*pnodeid = nodeid;
			return 0;
		}
	}

	/*
	 * Either the backend doesn't support get_nodeid or it handed back a
	 * NULL pointer. Just use hostname.
	 */
	maxlen = sysconf(_SC_HOST_NAME_MAX);
	nodeid = gsh_malloc(maxlen);
	rc = gsh_gethostname(nodeid, maxlen,
			nfs_param.core_param.enable_AUTHSTATS);
	if (rc != 0) {
		LogEvent(COMPONENT_CLIENTID, "gethostname failed: %d", errno);
		rc = -errno;
		gsh_free(nodeid);
	} else {
		*pnodeid = nodeid;
	}
	return rc;
}

void nfs_try_lift_grace(void)
{
	bool in_grace = true;
	int32_t rc_count = 0;
	uint32_t cur, old, pro;

	/* Already lifted? Just return */
	if (!(atomic_fetch_uint32_t(&grace_status) & GRACE_STATUS_ACTIVE))
		return;

	/*
	 * If we know there are no NLM clients, then we can consider the grace
	 * period done when all previous clients have sent a RECLAIM_COMPLETE.
	 */
	PTHREAD_MUTEX_lock(&grace_mutex);
	rc_count = atomic_fetch_int32_t(&reclaim_completes);
#ifdef _USE_NLM
	if (!nfs_param.core_param.enable_NLM)
#endif
		in_grace = (rc_count != clid_count);

	/* Otherwise, wait for the timeout */
	if (in_grace) {
		struct timespec timeout, now;
		int ret = clock_gettime(CLOCK_MONOTONIC, &now);

		if (ret != 0) {
			LogCrit(COMPONENT_MAIN, "Failed to get timestamp");
			assert(0);
		}

		timeout = current_grace;
		timeout.tv_sec += nfs_param.nfsv4_param.grace_period;
		in_grace = gsh_time_cmp(&timeout, &now) > 0;
	}

	/*
	 * Ok, we're basically ready to lift. Ensure there are no outstanding
	 * references to the current status of the grace period. If there are,
	 * then set the flag saying that there is an upcoming change.
	 */

	/*
	 * Can we lift the grace period now? If there are any outstanding refs,
	 * then just set the grace_change_req flag to indicate that we don't
	 * want to hand any more refs out. Otherwise, we try to lift.
	 *
	 * Clustered backends may need extra checks before they can do so. If
	 * the backend does not implement a try_lift_grace operation, then we
	 * assume there are no external conditions and that it's always ok.
	 */
	if (!in_grace) {
		cur = atomic_fetch_uint32_t(&grace_status);
		do {
			old = cur;

			/* Are we already done? Exit if so */
			if (!(cur & GRACE_STATUS_ACTIVE)) {
				PTHREAD_MUTEX_unlock(&grace_mutex);
				return;
			}

			/* Record that a change has now been requested */
			pro = old | GRACE_STATUS_CHANGE_REQ;
			if (pro == old)
				break;
			cur = __sync_val_compare_and_swap(&grace_status,
							  old, pro);
		} while (cur != old);

		/* Otherwise, go ahead and lift if we can */
		if (!(old & GRACE_STATUS_COUNT_MASK) &&
		    (!recovery_backend->try_lift_grace ||
		     recovery_backend->try_lift_grace()))
			nfs_lift_grace_locked();
	}
	PTHREAD_MUTEX_unlock(&grace_mutex);
}

static pthread_cond_t enforcing_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t enforcing_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Poll every 5s, just in case we miss the wakeup for some reason */
void nfs_wait_for_grace_enforcement(void)
{
	nfs_grace_start_t gsp = { .event = EVENT_JUST_GRACE };

	pthread_mutex_lock(&enforcing_mutex);
	nfs_try_lift_grace();
	while (nfs_in_grace() && !nfs_grace_enforcing()) {
		struct timespec	timeo = { .tv_sec = time(NULL) + 5,
					  .tv_nsec = 0 };

		pthread_cond_timedwait(&enforcing_cond, &enforcing_mutex,
						&timeo);

		pthread_mutex_unlock(&enforcing_mutex);
		nfs_start_grace(&gsp);
		nfs_try_lift_grace();
		pthread_mutex_lock(&enforcing_mutex);
	}
	pthread_mutex_unlock(&enforcing_mutex);
}

void nfs_notify_grace_waiters(void)
{
	pthread_mutex_lock(&enforcing_mutex);
	pthread_cond_broadcast(&enforcing_cond);
	pthread_mutex_unlock(&enforcing_mutex);
}

static pthread_cond_t norefs_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t norefs_mutex = PTHREAD_MUTEX_INITIALIZER;

void nfs_wait_for_grace_norefs(void)
{
	pthread_mutex_lock(&norefs_mutex);
	struct timespec	timeo = { .tv_sec = time(NULL) + 5,
				  .tv_nsec = 0 };
	pthread_cond_timedwait(&norefs_cond, &norefs_mutex,
			       &timeo);
	pthread_mutex_unlock(&norefs_mutex);
}

void nfs_notify_grace_norefs_waiters(void)
{
	pthread_mutex_lock(&norefs_mutex);
	pthread_cond_broadcast(&norefs_cond);
	pthread_mutex_unlock(&norefs_mutex);
}

/**
 * @brief Create an entry in the recovery directory
 *
 * This entry allows the client to reclaim state after a server
 * reboot/restart.
 *
 * @param[in] clientid Client record
 */
void nfs4_add_clid(nfs_client_id_t *clientid)
{
	PTHREAD_MUTEX_lock(&clientid->cid_mutex);
	recovery_backend->add_clid(clientid);
	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
}

/**
 * @brief Remove a client entry from the recovery directory
 *
 * This function would be called when a client expires.
 *
 */
void nfs4_rm_clid(nfs_client_id_t *clientid)
{
	PTHREAD_MUTEX_lock(&clientid->cid_mutex);
	recovery_backend->rm_clid(clientid);
	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
}

static bool check_clid(nfs_client_id_t *clientid, clid_entry_t *clid_ent)
{
	bool ret = false;


	LogDebug(COMPONENT_CLIENTID, "compare %s to %s",
		 clientid->cid_recov_tag, clid_ent->cl_name);

	if (clientid->cid_recov_tag &&
	    !strncmp(clientid->cid_recov_tag,
		     clid_ent->cl_name, PATH_MAX))
		ret = true;

	return ret;
}

/**
 * @brief Determine whether or not this client may reclaim state
 *
 * If the server is not in grace period, then no reclaim can happen.
 *
 * @param[in] clientid Client record
 */
void  nfs4_chk_clid_impl(nfs_client_id_t *clientid, clid_entry_t **clid_ent_arg)
{
	struct glist_head *node;
	clid_entry_t *clid_ent;
	*clid_ent_arg = NULL;

	LogDebug(COMPONENT_CLIENTID, "chk for %lu",
		 clientid->cid_clientid);

	/* If there were no clients at time of restart, we're done */
	if (clid_count == 0)
		return;

	/*
	 * loop through the list and try to find this client. If we
	 * find it, mark it to allow reclaims.
	 */
	PTHREAD_MUTEX_lock(&clientid->cid_mutex);
	glist_for_each(node, &clid_list) {
		clid_ent = glist_entry(node, clid_entry_t, cl_list);
		if (check_clid(clientid, clid_ent)) {
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};

				display_client_id_rec(&dspbuf, clientid);

				LogFullDebug(COMPONENT_CLIENTID,
					     "Allowed to reclaim ClientId %s",
					     str);
			}
			clientid->cid_allow_reclaim = true;
			*clid_ent_arg = clid_ent;
			break;
		}
	}
	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
}

void  nfs4_chk_clid(nfs_client_id_t *clientid)
{
	clid_entry_t *dummy_clid_ent;

	PTHREAD_MUTEX_lock(&grace_mutex);
	nfs4_chk_clid_impl(clientid, &dummy_clid_ent);
	PTHREAD_MUTEX_unlock(&grace_mutex);
}

/**
 * @brief Load clients for recovery
 *
 * @param[in] nodeid Node, on takeover
 *
 * Caller must hold grace_mutex.
 */
static void nfs4_recovery_load_clids(nfs_grace_start_t *gsp)
{
	LogDebug(COMPONENT_STATE, "Load recovery cli %p", gsp);

	recovery_backend->recovery_read_clids(gsp, nfs4_add_clid_entry,
						nfs4_add_rfh_entry);
}

#ifdef USE_RADOS_RECOV
static struct {
	void *dl;
	void (*kv_init)(struct nfs4_recovery_backend **);
	void (*ng_init)(struct nfs4_recovery_backend **);
	void (*cluster_init)(struct nfs4_recovery_backend **);
	int (*load_config_from_parse)(config_file_t,
				      struct config_error_type *);
} rados = { NULL,};

static int load_rados_recov(void)
{
	rados.dl = dlopen("libganesha_rados_recov.so",
#if defined(LINUX) && !defined(SANITIZE_ADDRESS)
			  RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#elif defined(FREEBSD) || defined(SANITIZE_ADDRESS)
			  RTLD_NOW | RTLD_LOCAL);
#endif

	if (rados.dl) {
		rados.kv_init = dlsym(rados.dl, "rados_kv_backend_init");
		rados.ng_init = dlsym(rados.dl, "rados_ng_backend_init");
		rados.cluster_init = dlsym(rados.dl,
					   "rados_cluster_backend_init");
		rados.load_config_from_parse = dlsym(rados.dl,
					   "rados_load_config_from_parse");

		if (!rados.kv_init || !rados.ng_init || !rados.cluster_init ||
		    !rados.load_config_from_parse) {
			dlclose(rados.dl);
			rados.dl = NULL;
			return -1;
		}
	} else {
		return -1;
	}
	return 0;
}
#endif

const char *recovery_backend_str(enum recovery_backend recovery_backend)
{
	switch (recovery_backend) {
	case RECOVERY_BACKEND_FS:
		return "fs";
	case RECOVERY_BACKEND_FS_NG:
		return "fs_ng";
	case RECOVERY_BACKEND_RADOS_KV:
		return "rados_kv";
	case RECOVERY_BACKEND_RADOS_NG:
		return "rados_ng";
	case RECOVERY_BACKEND_RADOS_CLUSTER:
		return "rados_cluster";
	}

	return "Unknown recovery backend";
}

/**
 * @brief Create the recovery directory
 *
 * The recovery directory may not exist yet, so create it.  This
 * should only need to be done once (if at all).  Also, the location
 * of the directory could be configurable.
 */
int nfs4_recovery_init(void)
{
	LogInfo(COMPONENT_CLIENTID, "Recovery Backend Init for %s",
		recovery_backend_str(nfs_param.nfsv4_param.recovery_backend));

	switch (nfs_param.nfsv4_param.recovery_backend) {
	case RECOVERY_BACKEND_FS:
		fs_backend_init(&recovery_backend);
		break;
	case RECOVERY_BACKEND_FS_NG:
		fs_ng_backend_init(&recovery_backend);
		break;
#ifdef USE_RADOS_RECOV
	case RECOVERY_BACKEND_RADOS_KV:
		rados.kv_init(&recovery_backend);
		break;
	case RECOVERY_BACKEND_RADOS_NG:
		rados.ng_init(&recovery_backend);
		break;
	case RECOVERY_BACKEND_RADOS_CLUSTER:
		rados.cluster_init(&recovery_backend);
		break;
#else
	case RECOVERY_BACKEND_RADOS_KV:
	case RECOVERY_BACKEND_RADOS_NG:
	case RECOVERY_BACKEND_RADOS_CLUSTER:
#endif
	default:
		LogCrit(COMPONENT_CLIENTID, "Unsupported Backend %s",
			recovery_backend_str(
				nfs_param.nfsv4_param.recovery_backend));
		return -ENOENT;
	}

	return recovery_backend->recovery_init();
}

/**
 * @brief Shut down the recovery backend
 *
 * Shut down the recovery backend, cleaning up any clients or tracking
 * structures in preparation for server shutdown.
 */
void nfs4_recovery_shutdown(void)
{
	if (recovery_backend->recovery_shutdown)
		recovery_backend->recovery_shutdown();
#ifdef USE_RADOS_RECOV
	if (rados.dl)
		(void) dlclose(rados.dl);
	rados.dl = NULL;
#endif
}

/**
 * @brief Clean up recovery directory
 */
void nfs_end_grace(void)
{
	recovery_backend->end_grace();
}

/**
 * @brief Record revoked filehandle under the client.
 *
 * @param[in] clientid Client record
 * @param[in] filehandle of the revoked file.
 */
void nfs4_record_revoke(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	/* A client's lease is reserved while recalling or revoking a
	 * delegation which means the client will not expire until we
	 * complete this revoke operation. The only exception is when
	 * the reaper thread revokes delegations of an already expired
	 * client!
	 */
	PTHREAD_MUTEX_lock(&delr_clid->cid_mutex);
	if (delr_clid->cid_confirmed == EXPIRED_CLIENT_ID) {
		/* Called from reaper thread, no need to record
		 * revoked file handles for an expired client.
		 */
		PTHREAD_MUTEX_unlock(&delr_clid->cid_mutex);
		return;
	}
	recovery_backend->add_revoke_fh(delr_clid, delr_handle);
	PTHREAD_MUTEX_unlock(&delr_clid->cid_mutex);
}

/**
 * @brief Decides if it is allowed to reclaim a given delegation
 *
 * @param[in] clientid Client record
 * @param[in] filehandle of the revoked file.
 * @retval true if allowed and false if not.
 *
 */
bool nfs4_check_deleg_reclaim(nfs_client_id_t *clid, nfs_fh4 *fhandle)
{
	char rhdlstr[NAME_MAX];
	struct glist_head *node;
	rdel_fh_t *rfh_entry;
	clid_entry_t *clid_ent;
	int b64ret;
	bool retval = true;

	/* Convert nfs_fh4_val into base64 encoded string */
	b64ret = base64url_encode(fhandle->nfs_fh4_val, fhandle->nfs_fh4_len,
				  rhdlstr, sizeof(rhdlstr));
	assert(b64ret != -1);

	PTHREAD_MUTEX_lock(&grace_mutex);
	nfs4_chk_clid_impl(clid, &clid_ent);
	if (clid_ent) {
		glist_for_each(node, &clid_ent->cl_rfh_list) {
			rfh_entry = glist_entry(node, rdel_fh_t, rdfh_list);
			assert(rfh_entry != NULL);
			if (!strcmp(rhdlstr, rfh_entry->rdfh_handle_str)) {
				LogFullDebug(COMPONENT_CLIENTID,
					"Can't reclaim revoked fh:%s",
					rfh_entry->rdfh_handle_str);
				retval = false;
				break;
			}
		}
	}
	PTHREAD_MUTEX_unlock(&grace_mutex);
	LogFullDebug(COMPONENT_CLIENTID, "Returning %s",
		     retval ? "TRUE" : "FALSE");
	return retval;
}

#ifdef _USE_NLM
/**
 * @brief Release NLM state
 */
static void nlm_releasecall(struct fridgethr_context *ctx)
{
	state_nsm_client_t *nsm_cp;
	state_status_t err;

	nsm_cp = ctx->arg;
	err = state_nlm_notify(nsm_cp, false, 0);
	if (err != STATE_SUCCESS)
		LogDebug(COMPONENT_STATE,
			"state_nlm_notify failed with %d",
			err);
	dec_nsm_client_ref(nsm_cp);
}
#endif /* _USE_NLM */

void extractv4(char *ipv6, char *ipv4, size_t size)
{
	char *token, *saveptr;
	char *delim = ":";

	token = strtok_r(ipv6, delim, &saveptr);
	while (token != NULL) {
		/* IPv4 delimiter is '.' */
		if (strchr(token, '.') != NULL) {
			(void) strlcpy(ipv4, token, size);
			return;
		}
		token = strtok_r(NULL, delim, &saveptr);
	}
	/* failed, copy a null string */
	ipv4[0] = '\0';
}

bool ip_str_match(char *release_ip, char *server_ip)
{
	bool ripv6, sipv6;
	char ipv4[SOCK_NAME_MAX];

	/* IPv6 delimiter is ':' */
	ripv6 = (strchr(release_ip, ':') != NULL);
	sipv6 = (strchr(server_ip, ':') != NULL);

	if (ripv6) {
		if (sipv6)
			return !strcmp(release_ip, server_ip);
		else {
			/* extract v4 addr from release_ip*/
			extractv4(release_ip, ipv4, sizeof(ipv4));
			return !strcmp(ipv4, server_ip);
		}
	} else {
		if (sipv6) {
			/* extract v4 addr from server_ip*/
			extractv4(server_ip, ipv4, sizeof(ipv4));
			return !strcmp(ipv4, release_ip);
		}
	}
	/* Both are ipv4 addresses */
	return !strcmp(release_ip, server_ip);
}

/**
 * @brief Release all NLM state
 */
static void nfs_release_nlm_state(char *release_ip)
{
#ifdef _USE_NLM
	hash_table_t *ht = ht_nlm_client;
	state_nlm_client_t *nlm_cp;
	state_nsm_client_t *nsm_cp;
	struct rbt_head *head_rbt;
	struct rbt_node *pn;
	struct hash_data *pdata;
	state_status_t state_status;
	char serverip[SOCK_NAME_MAX];
	int i;

	if (!nfs_param.core_param.enable_NLM)
		return;

	LogDebug(COMPONENT_STATE, "Release all NLM locks");

	cancel_all_nlm_blocked();

	/* walk the client list and call state_nlm_notify */
	for (i = 0; i < ht->parameter.index_size; i++) {
		PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
		head_rbt = &ht->partitions[i].rbt;
		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);
			nlm_cp = (state_nlm_client_t *) pdata->val.addr;

			if (sprint_sockip(&nlm_cp->slc_server_addr,
					  serverip, sizeof(serverip)) &&
			    ip_str_match(release_ip, serverip)) {
				nsm_cp = nlm_cp->slc_nsm_client;
				inc_nsm_client_ref(nsm_cp);
				state_status = fridgethr_submit(
						state_async_fridge,
						nlm_releasecall,
						nsm_cp);
				if (state_status != STATE_SUCCESS) {
					dec_nsm_client_ref(nsm_cp);
					LogCrit(COMPONENT_STATE,
						"failed to submit nlm release thread ");
				}
			}
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
#endif /* _USE_NLM */
}

static int ip_match(char *ip, nfs_client_id_t *cid)
{
	char *haystack;
	char *value = cid->cid_client_record->cr_client_val;
	int len = cid->cid_client_record->cr_client_val_len;

	LogDebug(COMPONENT_STATE, "NFS Server V4 match ip %s with (%.*s)",
		 ip, len, value);

	if (strlen(ip) == 0)	/* No IP all are matching */
		return 1;

	haystack = alloca(len + 1);
	memcpy(haystack, value, len);
	haystack[len] = '\0';
	if (strstr(haystack, ip) != NULL)
		return 1;

	return 0;		/* no match */
}

/*
 * try to find a V4 clients which match the IP we are releasing.
 * only search the confirmed clients, unconfirmed clients won't
 * have any state to release.
 */
static void nfs_release_v4_clients(char *ip)
{
	hash_table_t *ht = ht_confirmed_client_id;
	struct rbt_head *head_rbt;
	struct rbt_node *pn;
	struct hash_data *pdata;
	nfs_client_id_t *cp;
	nfs_client_record_t *recp;
	int i;

	LogEvent(COMPONENT_STATE, "NFS Server V4 recovery release ip %s", ip);

	/* go through the confirmed clients looking for a match */
	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &ht->partitions[i].rbt;

restart:
		PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);

			cp = (nfs_client_id_t *) pdata->val.addr;
			PTHREAD_MUTEX_lock(&cp->cid_mutex);
			if ((cp->cid_confirmed == CONFIRMED_CLIENT_ID)
			     && ip_match(ip, cp)) {
				inc_client_id_ref(cp);

				/* Take a reference to the client record
				 * before we drop cid_mutex. client record
				 * may be decoupled, so check if it is still
				 * coupled!
				 */
				recp = cp->cid_client_record;
				if (recp)
					inc_client_record_ref(recp);

				PTHREAD_MUTEX_unlock(&cp->cid_mutex);

				PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);

				/* nfs_client_id_expire requires cr_mutex
				 * if not decoupled alread
				 */
				if (recp)
					PTHREAD_MUTEX_lock(&recp->cr_mutex);

				nfs_client_id_expire(cp, true);

				if (recp) {
					PTHREAD_MUTEX_unlock(&recp->cr_mutex);
					dec_client_record_ref(recp);
				}

				dec_client_id_ref(cp);
				goto restart;

			} else {
				PTHREAD_MUTEX_unlock(&cp->cid_mutex);
			}
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
}

int load_recovery_param_from_conf(config_file_t parse_tree,
				  struct config_error_type *err_type)
{
	switch (nfs_param.nfsv4_param.recovery_backend) {
	case RECOVERY_BACKEND_FS:
	case RECOVERY_BACKEND_FS_NG:
		return 0;

	case RECOVERY_BACKEND_RADOS_KV:
	case RECOVERY_BACKEND_RADOS_NG:
	case RECOVERY_BACKEND_RADOS_CLUSTER:
#ifdef USE_RADOS_RECOV
		/*
		 * see if we actually need the rados_recovery shlib loaded
		 *
		 * we are here because the config (explicitly) calls
		 * for this recovery class. If we can't do it because
		 * the (package with the) libganesha_rados_recovery
		 * library wasn't installed, then we should return
		 * an error and eventually die.
		 */
		if (!rados.dl && load_rados_recov() < 0) {
			LogCrit(COMPONENT_CLIENTID,
				"Failed to load Backend %s. Please install the appropriate package",
				recovery_backend_str(
				       nfs_param.nfsv4_param.recovery_backend));
			return -1;
		}

		return rados.load_config_from_parse(parse_tree, err_type);
#endif
	default:
		LogCrit(COMPONENT_CLIENTID, "Unsupported Backend %s",
			recovery_backend_str(
				nfs_param.nfsv4_param.recovery_backend));
	}

	return -1;
}

/** @} */
