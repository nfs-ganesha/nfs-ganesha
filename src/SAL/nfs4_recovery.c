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

time_t current_grace;
pthread_mutex_t grace_mutex = PTHREAD_MUTEX_INITIALIZER;        /*< Mutex */
struct glist_head clid_list = GLIST_HEAD_INIT(clid_list);  /*< Clients */
struct nfs4_recovery_backend *recovery_backend;

static void nfs4_recovery_load_clids_nolock(nfs_grace_start_t *gsp);
static void nfs_release_nlm_state(char *release_ip);
static void nfs_release_v4_client(char *ip);

clid_entry_t *nfs4_add_clid_entry(char *cl_name)
{
	clid_entry_t *new_ent = gsh_malloc(sizeof(clid_entry_t));

	strcpy(new_ent->cl_name, cl_name);
	glist_add(&clid_list, &new_ent->cl_list);
	return new_ent;
}

rdel_fh_t *nfs4_add_rfh_entry(clid_entry_t *clid_ent, char *rfh_name)
{
	rdel_fh_t *new_ent = gsh_malloc(sizeof(rdel_fh_t));

	new_ent->rdfh_handle_str = gsh_strdup(rfh_name);
	glist_add(&clid_ent->cl_rfh_list, &new_ent->rdfh_list);
	return new_ent;
}

void nfs4_cleanup_clid_entrys(void)
{
	struct clid_entry *clid_entry;
	/* when not doing a takeover, start with an empty list */
	while ((clid_entry = glist_first_entry(&clid_list,
					       struct clid_entry,
					       cl_list)) != NULL) {
		glist_del(&clid_entry->cl_list);
		gsh_free(clid_entry);
	}
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
void nfs4_start_grace(nfs_grace_start_t *gsp)
{
	if (nfs_param.nfsv4_param.graceless) {
		LogEvent(COMPONENT_STATE,
			 "NFS Server skipping GRACE (Graceless is true)");
		return;
	}

	PTHREAD_MUTEX_lock(&grace_mutex);

	/* grace should always be greater than or equal to lease time,
	 * some clients are known to have problems with grace greater than 60
	 * seconds Lease_Lifetime should be set to a smaller value for those
	 * setups.
	 */
	atomic_store_time_t(&current_grace, time(NULL));

	if ((int)nfs_param.nfsv4_param.grace_period <
		(int)nfs_param.nfsv4_param.lease_lifetime) {
		LogWarn(COMPONENT_STATE,
		 "NFS Server GRACE duration should at least match LEASE period. Current configured values are GRACE(%d), LEASE(%d)",
		 (int)nfs_param.nfsv4_param.grace_period,
		 (int)nfs_param.nfsv4_param.lease_lifetime);
	}

	LogEvent(COMPONENT_STATE, "NFS Server Now IN GRACE, duration %d",
		 (int)nfs_param.nfsv4_param.grace_period);
	/*
	 * if called from failover code and given a nodeid, then this node
	 * is doing a take over.  read in the client ids from the failing node
	 */
	if (gsp && gsp->event != EVENT_JUST_GRACE) {
		LogEvent(COMPONENT_STATE,
			 "NFS Server recovery event %d nodeid %d ip %s",
			 gsp->event, gsp->nodeid, gsp->ipaddr);

		if (gsp->event == EVENT_CLEAR_BLOCKED)
			cancel_all_nlm_blocked();
		else {
			nfs_release_nlm_state(gsp->ipaddr);
			if (gsp->event == EVENT_RELEASE_IP)
				nfs_release_v4_client(gsp->ipaddr);
			else
				nfs4_recovery_load_clids_nolock(gsp);
		}
	}
	PTHREAD_MUTEX_unlock(&grace_mutex);
}

/**
 * @brief Check if we are in the grace period
 *
 * @retval true if so.
 * @retval false if not.
 */
int nfs_in_grace(void)
{
	int in_grace;
	static int last_grace  = -1;

	if (nfs_param.nfsv4_param.graceless)
		return 0;

	in_grace = ((atomic_fetch_time_t(&current_grace) +
		     nfs_param.nfsv4_param.grace_period) > time(NULL));

	if (in_grace != last_grace) {
		LogEvent(COMPONENT_STATE, "NFS Server Now %s",
			 in_grace ? "IN GRACE" : "NOT IN GRACE");
		last_grace = in_grace;
	} else if (in_grace) {
		LogDebug(COMPONENT_STATE, "NFS Server IN GRACE");
	}

	return in_grace;
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
	recovery_backend->add_clid(clientid);
}

/**
 * @brief Remove a client entry from the recovery directory
 *
 * This function would be called when a client expires.
 *
 */
void nfs4_rm_clid(nfs_client_id_t *clientid)
{
	recovery_backend->rm_clid(clientid);
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
	if (glist_empty(&clid_list))
		return;

	/*
	 * loop through the list and try to find this client.  if we
	 * find it, mark it to allow reclaims.  perhaps the client should
	 * be removed from the list at this point to make the list shorter?
	 */
	glist_for_each(node, &clid_list) {
		clid_ent = glist_entry(node, clid_entry_t, cl_list);
		if (recovery_backend->check_clid(clientid, clid_ent)) {
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(str), str, str};

				display_client_id_rec(&dspbuf, clientid);

				LogFullDebug(COMPONENT_CLIENTID,
					     "Allowed to reclaim ClientId %s",
					     str);
			}
			clientid->cid_allow_reclaim = 1;
			*clid_ent_arg = clid_ent;
			return;
		}
	}
}

void  nfs4_chk_clid(nfs_client_id_t *clientid)
{
	clid_entry_t *dummy_clid_ent;

	/* If we aren't in grace period, then reclaim is not possible */
	if (!nfs_in_grace())
		return;
	PTHREAD_MUTEX_lock(&grace_mutex);
	nfs4_chk_clid_impl(clientid, &dummy_clid_ent);
	PTHREAD_MUTEX_unlock(&grace_mutex);
}

static void nfs4_recovery_read_clids_recover(void)
{
	recovery_backend->recovery_read_clids_recover(nfs4_add_clid_entry,
						      nfs4_add_rfh_entry);
}

static void nfs4_recovery_read_clids_takeover(nfs_grace_start_t *gsp)
{
	recovery_backend->recovery_read_clids_takeover(gsp,
						       nfs4_add_clid_entry,
						       nfs4_add_rfh_entry);
}

static void nfs4_recovery_load_clids_nolock(nfs_grace_start_t *gsp)
{
	LogDebug(COMPONENT_STATE, "Load recovery cli %p", gsp);

	if (gsp == NULL) {
		nfs4_cleanup_clid_entrys();
		nfs4_recovery_read_clids_recover();
	} else {
		nfs4_recovery_read_clids_takeover(gsp);
	}
}

/**
 * @brief Load clients for recovery
 *
 * @param[in] nodeid Node, on takeover
 */
void nfs4_recovery_load_clids(nfs_grace_start_t *gsp)
{
	PTHREAD_MUTEX_lock(&grace_mutex);

	nfs4_recovery_load_clids_nolock(gsp);

	PTHREAD_MUTEX_unlock(&grace_mutex);
}

/**
 * @brief Create the recovery directory
 *
 * The recovery directory may not exist yet, so create it.  This
 * should only need to be done once (if at all).  Also, the location
 * of the directory could be configurable.
 */
void nfs4_recovery_init(void)
{
	fs_backend_init(&recovery_backend);
	recovery_backend->recovery_init();
}

/**
 * @brief Clean up recovery directory
 */
void nfs4_recovery_cleanup(void)
{
	recovery_backend->recovery_cleanup();
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
	PTHREAD_MUTEX_unlock(&delr_clid->cid_mutex);

	recovery_backend->add_revoke_fh(delr_clid, delr_handle);
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
	int retval;

	/* If we aren't in grace period, then reclaim is not possible */
	if (!nfs_in_grace())
		return false;

	/* Convert nfs_fh4_val into base64 encoded string */
	retval = base64url_encode(fhandle->nfs_fh4_val, fhandle->nfs_fh4_len,
				  rhdlstr, sizeof(rhdlstr));
	assert(retval != -1);

	PTHREAD_MUTEX_lock(&grace_mutex);
	nfs4_chk_clid_impl(clid, &clid_ent);
	if (clid_ent == NULL || glist_empty(&clid_ent->cl_rfh_list)) {
		PTHREAD_MUTEX_unlock(&grace_mutex);
		return true;
	}

	glist_for_each(node, &clid_ent->cl_rfh_list) {
		rfh_entry = glist_entry(node, rdel_fh_t, rdfh_list);
		assert(rfh_entry != NULL);
		if (!strcmp(rhdlstr, rfh_entry->rdfh_handle_str)) {
			PTHREAD_MUTEX_unlock(&grace_mutex);
			LogFullDebug(COMPONENT_CLIENTID,
				"Can't reclaim revoked fh:%s",
				rfh_entry->rdfh_handle_str);
			return false;
		}
	}

	PTHREAD_MUTEX_unlock(&grace_mutex);
	LogFullDebug(COMPONENT_CLIENTID, "Returning TRUE");
	return true;
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
