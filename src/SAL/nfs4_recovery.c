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
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"

/* The grace_mutex protects current_grace, clid_list, and clid_count */
static pthread_mutex_t grace_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t current_grace; /* current grace period timeout */
static int clid_count; /* number of active clients */
static struct glist_head clid_list = GLIST_HEAD_INIT(clid_list);  /* clients */

static struct nfs4_recovery_backend *recovery_backend;
int32_t reclaim_completes; /* atomic */

static void nfs4_recovery_load_clids(nfs_grace_start_t *gsp);
static void nfs_release_nlm_state(char *release_ip);
static void nfs_release_v4_client(char *ip);

clid_entry_t *nfs4_add_clid_entry(char *cl_name)
{
	clid_entry_t *new_ent = gsh_malloc(sizeof(clid_entry_t));

	glist_init(&new_ent->cl_rfh_list);
	strcpy(new_ent->cl_name, cl_name);
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

/**
 * Lift the grace period, if current_grace has not changed since we last
 * checked it. If something has changed in the interim, then don't do
 * anything. Either someone has set a new grace period, or someone else
 * beat us to lifting this one.
 */
static void
nfs_lift_grace_locked(time_t current)
{
	/*
	 * Caller must hold grace_mutex. Only the thread that actually sets
	 * the value to 0 gets to clean up the recovery db.
	 */
	if (atomic_fetch_time_t(&current_grace) == current) {
		nfs_end_grace();
		__sync_synchronize();
		atomic_store_time_t(&current_grace, (time_t)0);
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
 */
void nfs_start_grace(nfs_grace_start_t *gsp)
{
	bool was_grace;

	PTHREAD_MUTEX_lock(&grace_mutex);

	if (nfs_param.nfsv4_param.graceless) {
		nfs_lift_grace_locked(atomic_fetch_time_t(&current_grace));
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
	was_grace = (bool)atomic_fetch_time_t(&current_grace);
	atomic_store_time_t(&current_grace, time(NULL));
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
				nfs_release_v4_client(gsp->ipaddr);
				return;
			}
			else {
				nfs4_recovery_load_clids(gsp);
			}
		}
	}
out:
	PTHREAD_MUTEX_unlock(&grace_mutex);
}

/**
 * @brief Check if we are in the grace period
 *
 * @retval true if so.
 * @retval false if not.
 */
bool nfs_in_grace(void)
{
	return atomic_fetch_time_t(&current_grace);
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

void nfs_try_lift_grace(void)
{
	bool in_grace = true;
	int32_t rc_count = 0;
	time_t current = atomic_fetch_time_t(&current_grace);

	/* Already lifted? Just return */
	if (!current)
		return;

	/*
	 * If we know there are no NLM clients, then we can consider the grace
	 * period done when all previous clients have sent a RECLAIM_COMPLETE.
	 */
	rc_count = atomic_fetch_int32_t(&reclaim_completes);
	if (!nfs_param.core_param.enable_NLM)
		in_grace = (rc_count != clid_count);

	/* Otherwise, wait for the timeout */
	if (in_grace)
		in_grace = ((current + nfs_param.nfsv4_param.grace_period) >
					time(NULL));

	/*
	 * Can we lift the grace period now? Clustered backends may need
	 * extra checks before they can do so. If that is the case, then take
	 * the grace_mutex and try to do it. If the backend does not implement
	 * a try_lift_grace operation, then we assume it's always ok.
	 */
	if (!in_grace) {
		if (!recovery_backend->try_lift_grace ||
		     recovery_backend->try_lift_grace()) {
			PTHREAD_MUTEX_lock(&grace_mutex);
			nfs_lift_grace_locked(current);
			PTHREAD_MUTEX_unlock(&grace_mutex);
		}
	}
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

	PTHREAD_MUTEX_lock(&clientid->cid_mutex);

	LogDebug(COMPONENT_CLIENTID, "compare %s to %s",
		 clientid->cid_recov_tag, clid_ent->cl_name);

	if (clientid->cid_recov_tag &&
	    !strncmp(clientid->cid_recov_tag,
		     clid_ent->cl_name, PATH_MAX))
		ret = true;

	PTHREAD_MUTEX_unlock(&clientid->cid_mutex);
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
			return;
		}
	}
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

	/* A NULL gsp pointer indicates an initial startup grace period */
	if (gsp == NULL)
		nfs4_cleanup_clid_entries();
	recovery_backend->recovery_read_clids(gsp, nfs4_add_clid_entry,
						nfs4_add_rfh_entry);
}

static int load_backend(const char *name)
{
	if (!strcmp(name, "fs"))
		fs_backend_init(&recovery_backend);
#ifdef USE_RADOS_RECOV
	else if (!strcmp(name, "rados_kv"))
		rados_kv_backend_init(&recovery_backend);
	else if (!strcmp(name, "rados_ng"))
		rados_ng_backend_init(&recovery_backend);
	else if (!strcmp(name, "rados_cluster"))
		rados_cluster_backend_init(&recovery_backend);
#endif
	else if (!strcmp(name, "fs_ng"))
		fs_ng_backend_init(&recovery_backend);
	else
		return -1;
	return 0;
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
	if (load_backend(nfs_param.nfsv4_param.recovery_backend)) {
		LogCrit(COMPONENT_CLIENTID, "Unknown recovery backend");
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

	/* If we aren't in grace period, then reclaim is not possible */
	if (!nfs_in_grace())
		return false;

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

void extractv4(char *ipv6, char *ipv4)
{
	char *token, *saveptr;
	char *delim = ":";

	token = strtok_r(ipv6, delim, &saveptr);
	while (token != NULL) {
		/* IPv4 delimiter is '.' */
		if (strchr(token, '.') != NULL) {
			(void)strcpy(ipv4, token);
			return;
		}
		token = strtok_r(NULL, delim, &saveptr);
	}
	/* failed, copy a null string */
	(void)strcpy(ipv4, "");
}

bool ip_str_match(char *release_ip, char *server_ip)
{
	bool ripv6, sipv6;
	char ipv4[SOCK_NAME_MAX + 1];

	/* IPv6 delimiter is ':' */
	ripv6 = (strchr(release_ip, ':') != NULL);
	sipv6 = (strchr(server_ip, ':') != NULL);

	if (ripv6) {
		if (sipv6)
			return !strcmp(release_ip, server_ip);
		else {
			/* extract v4 addr from release_ip*/
			extractv4(release_ip, ipv4);
			return !strcmp(ipv4, server_ip);
		}
	} else {
		if (sipv6) {
			/* extract v4 addr from server_ip*/
			extractv4(server_ip, ipv4);
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
	char serverip[SOCK_NAME_MAX + 1];
	int i;

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
			sprint_sockip(&(nlm_cp->slc_server_addr),
					serverip,
					SOCK_NAME_MAX + 1);
			if (ip_str_match(release_ip, serverip)) {
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
	LogDebug(COMPONENT_STATE, "NFS Server V4 match ip %s with (%s)",
		 ip, cid->cid_client_record->cr_client_val);

	if (strlen(ip) == 0)	/* No IP all are matching */
		return 1;

	if (strstr(cid->cid_client_record->cr_client_val, ip) != NULL)
		return 1;

	return 0;		/* no match */
}

/*
 * try to find a V4 client that matches the IP we are releasing.
 * only search the confirmed clients, unconfirmed clients won't
 * have any state to release.
 */
static void nfs_release_v4_client(char *ip)
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

		PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
		head_rbt = &ht->partitions[i].rbt;

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
				return;

			} else {
				PTHREAD_MUTEX_unlock(&cp->cid_mutex);
			}
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
}

/** @} */
