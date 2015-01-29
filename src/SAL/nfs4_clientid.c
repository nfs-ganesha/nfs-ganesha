/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software F oundation; either version 3 of
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
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file nfs4_clientid.c
 * @brief The management of the client id cache.
 *
 */

#include "config.h"
#include <assert.h>
#include "hashtable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "nfs4.h"
#include "fsal.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"
#include "abstract_atomic.h"
#include "city.h"

/**
 * @brief Hashtable used to cache NFSv4 clientids
 */
hash_table_t *ht_client_record;

/**
 * @brief Hash table to store confirmed client IDs
 */
hash_table_t *ht_confirmed_client_id;

/**
 * @brief Hash table to store unconfirmed client IDs
 */
hash_table_t *ht_unconfirmed_client_id;

/**
 * @brief Counter to create clientids
 */
uint32_t clientid_counter;

/**
 * @brief Verifier to construct clientids
 */
uint64_t clientid_verifier;

/**
 * @brief Pool for client data structures
 */
pool_t *client_id_pool;

/**
 * @brief Return the NFSv4 status for the client id error code
 *
 * @param[in] err Client id error code
 *
 * @return the corresponding nfs4 error code
 */
nfsstat4 clientid_error_to_nfsstat(clientid_status_t err)
{
	switch (err) {
	case CLIENT_ID_SUCCESS:
		return NFS4_OK;
	case CLIENT_ID_INSERT_MALLOC_ERROR:
		return NFS4ERR_RESOURCE;
	case CLIENT_ID_INVALID_ARGUMENT:
		return NFS4ERR_SERVERFAULT;
	case CLIENT_ID_EXPIRED:
		return NFS4ERR_EXPIRED;
	case CLIENT_ID_STALE:
		return NFS4ERR_STALE_CLIENTID;
	}

	LogCrit(COMPONENT_CLIENTID, "Unexpected clientid error %d", err);

	return NFS4ERR_SERVERFAULT;
}

/**
 * @brief Return the NFSv4 status string for the client id error code
 *
 * @param[in] err client id error code
 *
 * @return the error string corresponding nfs4 error code
 */
const char *clientid_error_to_str(clientid_status_t err)
{
	switch (err) {
	case CLIENT_ID_SUCCESS:
		return "CLIENT_ID_SUCCESS";
	case CLIENT_ID_INSERT_MALLOC_ERROR:
		return "CLIENT_ID_INSERT_MALLOC_ERROR";
	case CLIENT_ID_INVALID_ARGUMENT:
		return "CLIENT_ID_INVALID_ARGUMENT";
	case CLIENT_ID_EXPIRED:
		return "CLIENT_ID_EXPIRED";
	case CLIENT_ID_STALE:
		return "CLIENT_ID_STALE";
	}

	LogCrit(COMPONENT_CLIENTID, "Unexpected clientid error %d", err);

	return "UNEXPECTED ERROR";
}

/**
 * @brief Return a string corresponding to the confirm state
 *
 * @param[in] confirmed Confirm state
 *
 * @return Corresponding string.
 */

const char *clientid_confirm_state_to_str(nfs_clientid_confirm_state_t
					  confirmed)
{
	switch (confirmed) {
	case CONFIRMED_CLIENT_ID:
		return "CONFIRMED";
	case UNCONFIRMED_CLIENT_ID:
		return "UNCONFIRMED";
	case EXPIRED_CLIENT_ID:
		return "EXPIRED";
	case STALE_CLIENT_ID:
		return "STALE";
	}
	return "UNKNOWN STATE";
}

/**
 * @brief Display a client record
 *
 * @param[in]  clientid Client record
 * @param[out] str      Output buffer
 *
 * @return Length of display string.
 */
int display_client_id_rec(nfs_client_id_t *clientid, char *str)
{
	int delta;
	char *tmpstr = str;

	tmpstr +=
	    sprintf(tmpstr, "%p ClientID=%" PRIx64 " %s Client={", clientid,
		    clientid->cid_clientid,
		    clientid_confirm_state_to_str(clientid->cid_confirmed));

	if (clientid->cid_client_record != NULL)
		tmpstr +=
		    display_client_record(clientid->cid_client_record, tmpstr);
	else
		tmpstr += sprintf(tmpstr, "<NULL>");

	if (clientid->cid_lease_reservations > 0)
		delta = 0;
	else
		delta = time(NULL) - clientid->cid_last_renew;

	if (clientid->cid_minorversion == 0) {
		tmpstr +=
		    sprintf(tmpstr,
			    "} cb_prog=%u r_addr=%s r_netid=%s "
			    "t_delta=%d reservations=%d " "refcount=%" PRId32,
			    clientid->cid_cb.v40.cb_program,
			    clientid->cid_cb.v40.cb_client_r_addr,
			    (netid_nc_table[clientid->cid_cb.v40.cb_addr.nc].
			     netid), delta, clientid->cid_lease_reservations,
			    atomic_fetch_int32_t(&clientid->cid_refcount));
	}

	return tmpstr - str;
}

/**
 * @brief Display a client owner
 *
 * @param[in]  clientid The client record
 * @param[out] str      Output buffer
 *
 * @return Length of display string.
 */
int display_clientid_name(nfs_client_id_t *clientid, char *str)
{
	if (clientid->cid_client_record != NULL)
		return DisplayOpaqueValue(clientid->cid_client_record->
					  cr_client_val,
					  clientid->cid_client_record->
					  cr_client_val_len, str);
	else
		return sprintf(str, "<NULL>");
}

/**
 * @brief Increment the clientid refcount in the hash table
 *
 * @param[in] val Buffer pointing to client record
 */
static void Hash_inc_client_id_ref(struct gsh_buffdesc *val)
{
	nfs_client_id_t *clientid = val->addr;

	inc_client_id_ref(clientid);
}

/**
 * @brief Increment the clientid refcount
 *
 * @param[in] clientid Client record
 */
int32_t inc_client_id_ref(nfs_client_id_t *clientid)
{
	int32_t cid_refcount = atomic_inc_int32_t(&clientid->cid_refcount);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_id_rec(clientid, str);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Increment refcount Clientid {%s} to %" PRId32,
			     str, cid_refcount);
	}

	return cid_refcount;
}

/**
 * @brief nfsv41 has-sessions prediccate (returns true if sessions found)
 *
 * @param[in] clientid Client record
 *
 * @return true if there are sessions associated with the client,
 * false otherwise.
 */

bool client_id_has_nfs41_sessions(nfs_client_id_t *clientid)
{
	if (clientid->cid_minorversion > 0) {
		if (!glist_empty(&clientid->cid_cb.v41.cb_session_list))
			return true;
	}

	return false;
}

/**
 * @brief Tests whether state exists on a client id
 *
 * We assume that open owners are predictive of open or lock state,
 * since they're collected when the last state is removed.
 *
 * @note The clientid mutex must be held when calling this function.
 *
 * @param[in] clientid Client record
 *
 * @retval true if there is state.
 * @retval false if there isn't.
 */

bool client_id_has_state(nfs_client_id_t *clientid)
{
	bool result;

	if (glist_empty(&clientid->cid_openowners))
		return false;

	pthread_mutex_lock(&clientid->cid_owner.so_mutex);

	result = !glist_empty(
		&clientid->cid_owner.so_owner.so_nfs4_owner.so_state_list);

	pthread_mutex_unlock(&clientid->cid_owner.so_mutex);

	return result;
}

/**
 * @brief Deconstruct and free a client record
 *
 * @param[in] clientid The client record to free
 */

void free_client_id(nfs_client_id_t *clientid)
{
	assert(atomic_fetch_int32_t(&clientid->cid_refcount) == 0);

	if (clientid->cid_client_record != NULL)
		dec_client_record_ref(clientid->cid_client_record);

	if (pthread_mutex_destroy(&clientid->cid_mutex) != 0)
		LogDebug(COMPONENT_CLIENTID,
			 "pthread_mutex_destroy returned errno %d (%s)", errno,
			 strerror(errno));

	/* For NFSv4.1 clientids, destroy all associated sessions */
	if (clientid->cid_minorversion > 0) {
		struct glist_head *glist = NULL;
		struct glist_head *glistn = NULL;

		glist_for_each_safe(glist, glistn,
				    &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(glist,
							       nfs41_session_t,
							       session_link);
			nfs41_Session_Del(session->session_id);
		}
	}
	pool_free(client_id_pool, clientid);
}

/**
 * @brief Decrement the clientid refcount
 *
 * @param[in] clientid Client record
 */
int32_t dec_client_id_ref(nfs_client_id_t *clientid)
{
	char str[HASHTABLE_DISPLAY_STRLEN];
	int32_t cid_refcount;

	if (isFullDebug(COMPONENT_CLIENTID))
		display_client_id_rec(clientid, str);

	cid_refcount = atomic_dec_int32_t(&clientid->cid_refcount);

	LogFullDebug(COMPONENT_CLIENTID,
		     "Decrement refcount Clientid {%s} refcount to %" PRId32,
		     str, cid_refcount);

	if (cid_refcount > 0)
		return cid_refcount;

	/* We don't need a lock to look at cid_confirmed because when
	 * refcount has gone to 0, no other threads can have a pointer
	 * to the clientid record.
	 */
	if (clientid->cid_confirmed == EXPIRED_CLIENT_ID) {
		/* Is not in any hash table, so we can just delete it */
		LogFullDebug(COMPONENT_CLIENTID,
			     "Free Clientid refcount now=0 {%s}", str);

		free_client_id(clientid);
	} else {
		/* Clientid records should not be freed unless marked expired */
		display_client_id_rec(clientid, str);
		LogCrit(COMPONENT_CLIENTID,
			"Should not be here, try to remove last ref {%s}", str);

		assert(clientid->cid_confirmed == EXPIRED_CLIENT_ID);
	}

	return cid_refcount;
}

/**
 * @brief Computes the hash value for the entry in Client Id cache.
 *
 * This function computes the hash value for the entry in Client Id
 * cache. In fact, it just uses the clientid as the value (identity
 * function) modulo the size of the hash.  This function is called
 * internal in the HasTable_* function
 *
 * @param[in] hparam Hash table parameter
 * @param[in] key    Pointer to the hash key buffer
 *
 * @return The computed hash value.
 *
 * @see hashtable_init
 *
 */
uint32_t client_id_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	clientid4 clientid;

	memcpy(&clientid, key->addr, sizeof(clientid));

	return (uint32_t) clientid % hparam->index_size;
}

/**
 * @brief Computes the RBT hash for the entry in Client Id cache
 *
 * Computes the rbt value for the entry in Client Id cache. In fact,
 * it just use the address value itself (which is an unsigned integer)
 * as the rbt value.  This function is called internal in the
 * HasTable_* function
 *
 * @param[in] hparam Hash table parameter.
 * @param[in] key    Hash key buffer
 *
 * @return The computed RBT value.
 *
 * @see hashtable_init
 *
 */
uint64_t client_id_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key)
{
	clientid4 clientid;

	memcpy(&clientid, key->addr, sizeof(clientid));

	return clientid;
}

/**
 * @brief Compares clientids stored in the key buffers.
 *
 * This function compares the clientid stored in the key buffers. This
 * function is to be used as 'compare_key' field in the hashtable
 * storing the client ids.
 *
 * @param[in] buff1 first key
 * @param[in] buff2 second key
 *
 * @retval 0 if keys are identifical.
 * @retval 1 if the keys are different.
 *
 */
int compare_client_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	clientid4 cl1 = *((clientid4 *) (buff1->addr));
	clientid4 cl2 = *((clientid4 *) (buff2->addr));
	return (cl1 == cl2) ? 0 : 1;
}

/**
 * @brief Displays the client_id stored from the hash table
 *
 * @param[in]  buff Buffer to display
 * @param[out] str  Output string
 *
 * @return Number of character written.
 *
 */
int display_client_id_key(struct gsh_buffdesc *buff, char *str)
{
	clientid4 clientid;

	clientid = *((clientid4 *) (buff->addr));

	return sprintf(str, "%" PRIx64, clientid);
}

/**
 * @brief Displays the client record from a hash table
 *
 * @param[in]  buff Buffer to display
 * @param[out] str  Output string
 *
 * @return Number of character written.
 *
 */
int display_client_id_val(struct gsh_buffdesc *buff, char *str)
{
	return display_client_id_rec(buff->addr, str);
}

/**
 * @brief Create a new client record
 *
 * @param[in] clientid      The associated clientid
 * @param[in] client_record The client owner record
 * @param[in] client_addr   Client socket address
 * @param[in] credential    Client credential
 * @param[in] minorversion  Minor version client uses
 *
 * @return New client record or NULL.
 */
nfs_client_id_t *create_client_id(clientid4 clientid,
				  nfs_client_record_t *client_record,
				  sockaddr_t *client_addr,
				  nfs_client_cred_t *credential,
				  struct gsh_client *gsh_client,
				  uint32_t minorversion)
{
	nfs_client_id_t *client_rec = pool_alloc(client_id_pool, NULL);
	state_owner_t *owner;

	if (client_rec == NULL) {
		LogCrit(COMPONENT_CLIENTID,
			"Unable to allocate memory for clientid %" PRIx64,
			clientid);
		return NULL;
	}

	if (pthread_mutex_init(&client_rec->cid_mutex, NULL) == -1) {
		char str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
		display_clientid_name(client_rec, str_client);

		LogCrit(COMPONENT_CLIENTID,
			"Could not init mutex for clientid %" PRIx64 "->%s",
			clientid, str_client);

		/* Directly free the clientid record since we failed
		   to initialize it */
		pool_free(client_id_pool, client_rec);

		return NULL;
	}

	owner = &client_rec->cid_owner;

	if (pthread_mutex_init(&owner->so_mutex, NULL) == -1) {
		LogDebug(COMPONENT_CLIENTID,
			 "Unable to create clientid owner for clientid %"
			 PRIx64, clientid);

		/* Free the clientid record since we failed to initialize it */
		pool_free(client_id_pool, client_rec);

		return NULL;
	}

	/* initialize the chan mutex for v4 */
	if (minorversion == 0) {
		if (pthread_mutex_init(&client_rec->cid_cb.v40.cb_chan.mtx,
				       NULL) == -1) {
			LogDebug(COMPONENT_CLIENTID,
				 "Unable to init chan mutex for clientid %"
				 PRIx64, clientid);
			pool_free(client_id_pool, client_rec);
			return NULL;
		}
		client_rec->cid_cb.v40.cb_chan_down = true;
		client_rec->first_path_down_resp_time = 0;
	}

	if (clientid == 0)
		clientid = new_clientid();

	client_rec->cid_confirmed = UNCONFIRMED_CLIENT_ID;
	client_rec->cid_clientid = clientid;
	client_rec->cid_last_renew = time(NULL);
	client_rec->cid_client_record = client_record;
	client_rec->cid_client_addr = *client_addr;
	client_rec->cid_credential = *credential;
	client_rec->cid_minorversion = minorversion;
	client_rec->gsh_client = gsh_client;

	/* need to init the list_head */
	glist_init(&client_rec->cid_openowners);
	glist_init(&client_rec->cid_lockowners);

	/* set up the content of the clientid_owner */
	owner->so_type = STATE_CLIENTID_OWNER_NFSV4;
	owner->so_owner.so_nfs4_owner.so_clientid = clientid;
	owner->so_owner.so_nfs4_owner.so_clientrec = client_rec;
	owner->so_owner.so_nfs4_owner.so_resp.resop = NFS4_OP_ILLEGAL;
	owner->so_owner.so_nfs4_owner.so_args.argop = NFS4_OP_ILLEGAL;
	owner->so_refcount = 1;

	/* Init the lists for the clientid_owner */
	glist_init(&owner->so_lock_list);
	glist_init(&owner->so_owner.so_nfs4_owner.so_state_list);

	/* Get a reference to the client record */
	(void)inc_client_record_ref(client_rec->cid_client_record);

	return client_rec;
}

/**
 * @brief Inserts an entry describing a clientid4 into the cache.
 *
 * @param[in] clientid the client id record
 *
 * @retval CLIENT_ID_SUCCESS if successfull.
 * @retval CLIENT_ID_INSERT_MALLOC_ERROR if an error occured during
 *         the insertion process
 * @retval CLIENT_ID_NETDB_ERROR if an error occured during the netdb
 *         query (via gethostbyaddr).
 */

clientid_status_t nfs_client_id_insert(nfs_client_id_t *clientid)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffdata;
	int rc;

	/* Create key from cid_clientid */
	buffkey.addr = &clientid->cid_clientid;
	buffkey.len = sizeof(clientid->cid_clientid);

	buffdata.addr = clientid;
	buffdata.len = sizeof(nfs_client_id_t);

	rc = hashtable_test_and_set(ht_unconfirmed_client_id, &buffkey,
				    &buffdata,
				    HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not insert unconfirmed clientid %" PRIx64
			 " error=%s", clientid->cid_clientid,
			 hash_table_err_to_str(rc));

		/* Free the clientid record and return */
		free_client_id(clientid);

		return CLIENT_ID_INSERT_MALLOC_ERROR;
	}

	/* Take a reference to the unconfirmed clientid for the hash table. */
	(void)inc_client_id_ref(clientid);

	if (isFullDebug(COMPONENT_CLIENTID) &&
	    isFullDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID,
			     "-=-=-=-=-=-=-=-=-=-> ht_unconfirmed_client_id ");
		hashtable_log(COMPONENT_CLIENTID, ht_unconfirmed_client_id);
	}

	/* Attach new clientid to client record's cr_punconfirmed_id. */
	clientid->cid_client_record->cr_unconfirmed_rec = clientid;

	return CLIENT_ID_SUCCESS;
}

/**
 * @brief Removes a confirmed client id record.
 *
 * @param[in] clientid The client id record
 *
 * @return hash table error code
 */
int remove_confirmed_client_id(nfs_client_id_t *clientid)
{
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_key;
	struct gsh_buffdesc old_value;

	buffkey.addr = &clientid->cid_clientid;
	buffkey.len = sizeof(clientid->cid_clientid);

	rc = HashTable_Del(ht_confirmed_client_id, &buffkey, &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not remove unconfirmed clientid %" PRIx64
			 " error=%s", clientid->cid_clientid,
			 hash_table_err_to_str(rc));
		return rc;
	}

	clientid->cid_client_record->cr_confirmed_rec = NULL;

	/* Set this up so this client id record will be freed. */
	clientid->cid_confirmed = EXPIRED_CLIENT_ID;

	/* Release hash table reference to the unconfirmed record */
	(void)dec_client_id_ref(clientid);

	return rc;
}

/**
 * @brief Removes an unconfirmed client id record.
 *
 * @param[in] clientid The client id record
 *
 * @return hash table error code
 */

int remove_unconfirmed_client_id(nfs_client_id_t *clientid)
{
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_key;
	struct gsh_buffdesc old_value;

	buffkey.addr = &clientid->cid_clientid;
	buffkey.len = sizeof(clientid->cid_clientid);

	rc = HashTable_Del(ht_unconfirmed_client_id, &buffkey, &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		LogCrit(COMPONENT_CLIENTID,
			"Could not remove unconfirmed clientid %" PRIx64
			" error=%s", clientid->cid_clientid,
			hash_table_err_to_str(rc));
		return rc;
	}

	/* XXX prevents calling remove_confirmed before removed_confirmed,
	 * if we failed to maintain the invariant that the cases are
	 * disjoint */
	clientid->cid_client_record->cr_unconfirmed_rec = NULL;

	/* Set this up so this client id record will be freed. */
	clientid->cid_confirmed = EXPIRED_CLIENT_ID;

	/* Release hash table reference to the unconfirmed record */
	(void)dec_client_id_ref(clientid);

	return rc;
}

/**
 * @brief Confirm a client id record.
 *
 * @param[in] clientid  The client id record
 * @param[in] component Component ID for logging
 *
 * @retval CLIENT_ID_SUCCESS if successfull.
 * @retval CLIENT_ID_INVALID_ARGUMENT if unable to find record in
 *         unconfirmed table
 * @retval CLIENT_ID_INSERT_MALLOC_ERROR if unable to insert record
 *         into confirmed table
 * @retval CLIENT_ID_NETDB_ERROR if an error occured during the netdb
 *         query (via gethostbyaddr).
 */
clientid_status_t nfs_client_id_confirm(nfs_client_id_t *clientid,
					log_components_t component)
{
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_key;
	struct gsh_buffdesc old_value;

	buffkey.addr = &clientid->cid_clientid;
	buffkey.len = sizeof(clientid->cid_clientid);

	/* Remove the clientid as the unconfirmed entry for the client
	   record */
	clientid->cid_client_record->cr_unconfirmed_rec = NULL;

	rc = HashTable_Del(ht_unconfirmed_client_id, &buffkey, &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(clientid, str);

			LogCrit(COMPONENT_CLIENTID,
				"Unexpected problem %s, could not remove "
				"{%s}", hash_table_err_to_str(rc), str);
		}

		return CLIENT_ID_INVALID_ARGUMENT;
	}

	clientid->cid_confirmed = CONFIRMED_CLIENT_ID;

	rc = hashtable_test_and_set(ht_confirmed_client_id, &old_key,
				    &old_value,
				    HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	if (rc != HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(clientid, str);

			LogCrit(COMPONENT_CLIENTID,
				"Unexpected problem %s, could not "
				"insert {%s}", hash_table_err_to_str(rc), str);
		}

		/* Set this up so this client id record will be
		   freed. */
		clientid->cid_confirmed = EXPIRED_CLIENT_ID;

		/* Release hash table reference to the unconfirmed
		   record */
		(void)dec_client_id_ref(clientid);

		return CLIENT_ID_INSERT_MALLOC_ERROR;
	}

	/* Add the clientid as the confirmed entry for the client
	   record */
	clientid->cid_client_record->cr_confirmed_rec = clientid;

	nfs4_add_clid(clientid);

	return CLIENT_ID_SUCCESS;
}

/**
 * @brief Client expires, need to take care of owners
 *
 * This function assumes caller holds record->cr_mutex and holds a
 * reference to record also.
 *
 * @param[in] clientid The client id to expire
 * @param[in] make_stale  Set if client id expire is due to ip move.
 *
 * @return true if the clientid is successfully expired.
 */
bool nfs_client_id_expire(nfs_client_id_t *clientid, bool make_stale)
{
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_key;
	struct gsh_buffdesc old_value;
	hash_table_t *ht_expire;
	nfs_client_record_t *record;
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct root_op_context root_op_context;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL,
			     0, 0, UNKNOWN_REQUEST);

	pthread_mutex_lock(&clientid->cid_mutex);
	if (clientid->cid_confirmed == EXPIRED_CLIENT_ID) {
		if (isFullDebug(COMPONENT_CLIENTID)) {
			display_client_id_rec(clientid, str);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Expired (skipped) {%s}", str);
		}

		pthread_mutex_unlock(&clientid->cid_mutex);
		release_root_op_context();
		return false;
	}

	if (isDebug(COMPONENT_CLIENTID)) {
		display_client_id_rec(clientid, str);
		LogDebug(COMPONENT_CLIENTID, "Expiring {%s}", str);
	}

	if ((clientid->cid_confirmed == CONFIRMED_CLIENT_ID) ||
	    (clientid->cid_confirmed == STALE_CLIENT_ID))
		ht_expire = ht_confirmed_client_id;
	else
		ht_expire = ht_unconfirmed_client_id;

	if (make_stale) {
		clientid->cid_confirmed = STALE_CLIENT_ID;
		pthread_mutex_unlock(&clientid->cid_mutex);
	} else {
		clientid->cid_confirmed = EXPIRED_CLIENT_ID;
		/* Need to clean up the client record. */
		record = clientid->cid_client_record;

		pthread_mutex_unlock(&clientid->cid_mutex);

		/* Detach the clientid record from the client record */
		if (record->cr_confirmed_rec == clientid)
			record->cr_confirmed_rec = NULL;

		if (record->cr_unconfirmed_rec == clientid)
			record->cr_unconfirmed_rec = NULL;

		buffkey.addr = &clientid->cid_clientid;
		buffkey.len = sizeof(clientid->cid_clientid);

		rc = HashTable_Del(ht_expire, &buffkey, &old_key, &old_value);
		if ((rc != HASHTABLE_SUCCESS) &&
		    (clientid->cid_confirmed == STALE_CLIENT_ID)) {
			/* Try in the unconfirmed hash table */
			rc = HashTable_Del(ht_unconfirmed_client_id,
					   &buffkey,
					   &old_key,
					   &old_value);
		}

		if (rc != HASHTABLE_SUCCESS) {
			LogFatal(COMPONENT_CLIENTID,
				"Could not remove expired clientid %" PRIx64
				" error=%s", clientid->cid_clientid,
				hash_table_err_to_str(rc));
		}
	}

	/* Traverse the client's lock owners, and release all
	 * locks and owners.
	 *
	 * Note: If there is an owner refcount bug, this COULD infinite loop,
	 * and it will spam the log with warnings... Such a refcount bug will
	 * be quickly fixed :-).
	 */
	while (true) {
		state_owner_t *owner;

		pthread_mutex_lock(&clientid->cid_mutex);

		owner = glist_first_entry(&clientid->cid_lockowners,
					  state_owner_t,
					  so_owner.so_nfs4_owner.so_perclient);

		if (owner == NULL) {
			pthread_mutex_unlock(&clientid->cid_mutex);
			break;
		}

		/* Move owner to end of list in case it doesn't get
		 * freed when we decrement the refcount.
		 */
		glist_del(&owner->so_owner.so_nfs4_owner.so_perclient);
		glist_add_tail(&clientid->cid_lockowners,
			       &owner->so_owner.so_nfs4_owner.so_perclient);

		/* Hold a reference to the owner while we drop the cid_mutex. */
		inc_state_owner_ref(owner);

		pthread_mutex_unlock(&clientid->cid_mutex);

		state_nfs4_owner_unlock_all(owner);

		if (isFullDebug(COMPONENT_CLIENTID)) {
			int32_t refcount =
			    atomic_fetch_int32_t(&owner->so_refcount);

			DisplayOwner(owner, str);

			if (refcount > 1)
				LogWarn(COMPONENT_CLIENTID,
					"Expired State, Possibly extra references to {%s}",
					str);
			else
				LogFullDebug(COMPONENT_CLIENTID,
					     "Expired State for {%s}", str);
		}

		dec_state_owner_ref(owner);
	}

	/* revoke layouts for this client*/
	revoke_owner_layouts(&clientid->cid_owner);

	/* release the corresponding open states , close files.
	 *
	 * Note: If there is an owner refcount bug, this COULD infinite loop,
	 * and it will spam the log with warnings... Such a refcount bug will
	 * be quickly fixed :-).
	 */
	while (true) {
		state_owner_t *owner;

		pthread_mutex_lock(&clientid->cid_mutex);

		owner = glist_first_entry(&clientid->cid_openowners,
					  state_owner_t,
					  so_owner.so_nfs4_owner.so_perclient);

		if (owner == NULL) {
			pthread_mutex_unlock(&clientid->cid_mutex);
			break;
		}

		/* Move owner to end of list in case it doesn't get
		 * freed when we decrement the refcount.
		 */
		glist_del(&owner->so_owner.so_nfs4_owner.so_perclient);
		glist_add_tail(&clientid->cid_openowners,
			       &owner->so_owner.so_nfs4_owner.so_perclient);

		/* Hold a reference to the owner while we drop the cid_mutex. */
		inc_state_owner_ref(owner);

		pthread_mutex_unlock(&clientid->cid_mutex);

		release_openstate(owner);

		if (isFullDebug(COMPONENT_CLIENTID)) {
			int32_t refcount =
			    atomic_fetch_int32_t(&owner->so_refcount);

			DisplayOwner(owner, str);

			if (refcount > 1)
				LogWarn(COMPONENT_CLIENTID,
					"Expired State, Possibly extra references to {%s}",
					str);
			else
				LogFullDebug(COMPONENT_CLIENTID,
					     "Expired State for {%s}", str);
		}

		dec_state_owner_ref(owner);
	}

	/* revoke delegations for this client*/
	revoke_owner_delegs(&clientid->cid_owner);

	/* Destroy v4 callback channel */
	if (clientid->cid_minorversion == 0 &&
	    clientid->cid_cb.v40.cb_chan.clnt)
		nfs_rpc_destroy_chan(&clientid->cid_cb.v40.cb_chan);

	/* For NFSv4.1 clientids, destroy all associated sessions */
	if (clientid->cid_minorversion > 0) {
		struct glist_head *glist = NULL;
		struct glist_head *glistn = NULL;

		glist_for_each_safe(glist, glistn,
				    &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session = glist_entry(glist,
							       nfs41_session_t,
							       session_link);
			struct gsh_buffdesc key = {
				.addr = session->session_id,
				.len = NFS4_SESSIONID_SIZE
			};
			struct gsh_buffdesc old_key, old_value;

			if (HashTable_Del(ht_session_id,
					  &key,
					  &old_key,
					  &old_value) == HASHTABLE_SUCCESS) {
				nfs41_session_t *session = old_value.addr;

				/* unref session */
				int32_t refcnt =
				    atomic_dec_int32_t(&session->refcount);
				if (refcnt == 0) {
					/* Unlink the session from the client's
					 * list of sessions
					 */
					LogFullDebug(COMPONENT_SESSIONS,
						     "Destroying session %p",
						     session);
					glist_del(&session->session_link);

					/* Decrement our reference to the
					 * clientid record
					 */
					dec_client_id_ref(session->
							  clientid_record);

					/* Destroy the session's back channel
					 * (if any)
					 */
					if (session->flags & session_bc_up) {
						nfs_rpc_destroy_chan(&session->
								     cb_chan);
					}

					/* Free the memory for the session */
					pool_free(nfs41_session_pool, session);
				}

			} else {
				LogCrit(COMPONENT_SESSIONS,
					"Expire session failed");
			}
		}
	}

	if (clientid->cid_recov_dir != NULL) {
		nfs4_rm_clid(clientid->cid_recov_dir, v4_recov_dir, 0);
		gsh_free(clientid->cid_recov_dir);
		clientid->cid_recov_dir = NULL;
	}

	if (isFullDebug(COMPONENT_CLIENTID)) {
		display_client_id_rec(clientid, str);
		LogFullDebug(COMPONENT_CLIENTID, "Expired (done) {%s}", str);
	}

	if (isDebug(COMPONENT_CLIENTID)) {
		display_client_id_rec(clientid, str);
		LogDebug(COMPONENT_CLIENTID,
			 "About to release last reference to {%s}", str);
	}

	/* Release the hash table reference to the clientid. */
	if (!make_stale)
		(void)dec_client_id_ref(clientid);

	release_root_op_context();
	return true;
}

/**
 * @brief Get a clientid from a hash table
 *
 * @param[in]  ht         Table from which to fetch
 * @param[in]  clientid   Clientid to fetch
 * @param[out] client_rec Fetched client record
 *
 * @return Clientid status.
 */
clientid_status_t nfs_client_id_get(hash_table_t *ht, clientid4 clientid,
				    nfs_client_id_t **client_rec)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	clientid_status_t status;
	uint64_t epoch_low = ServerEpoch & 0xFFFFFFFF;
	uint64_t cid_epoch = (uint64_t) (clientid >> (clientid4) 32);
	nfs_client_id_t *pclientid;
	int rc;


	/* Don't even bother to look up clientid if epochs don't match */
	if (cid_epoch != epoch_low) {
		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_CLIENTID,
				     "%s NOTFOUND (epoch doesn't match, assumed STALE)",
				     ht->parameter.ht_name);
		*client_rec = NULL;
		return CLIENT_ID_STALE;
	}

	buffkey.addr = &clientid;
	buffkey.len = sizeof(clientid4);

	if (isFullDebug(COMPONENT_CLIENTID) && isDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID, "%s KEY {%" PRIx64 "}",
			     ht->parameter.ht_name, clientid);
	}

	if (isFullDebug(COMPONENT_CLIENTID) &&
	    isFullDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID, "-=-=-=-=-=-=-=-=-=-> %s",
			     ht->parameter.ht_name);
		hashtable_log(COMPONENT_CLIENTID, ht);
	}

	if (hashtable_getref(ht, &buffkey, &buffval, Hash_inc_client_id_ref) ==
	    HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_CLIENTID, "%s FOUND",
				     ht->parameter.ht_name);
		pclientid = buffval.addr;

		if (pclientid->cid_confirmed == STALE_CLIENT_ID) {
			/* Stale client becuse of ip detach and attach to
			 * same node */
			status = CLIENT_ID_STALE;
			dec_client_id_ref(pclientid);
			pclientid->cid_confirmed = EXPIRED_CLIENT_ID;
			rc = HashTable_Del(ht, &buffkey, NULL, NULL);
			if (rc != HASHTABLE_SUCCESS) {
				LogWarn(COMPONENT_CLIENTID,
				"Could not remove unconfirmed clientid %" PRIx64
				" error=%s", pclientid->cid_clientid,
				hash_table_err_to_str(rc));
			}

			dec_client_id_ref(pclientid);
			*client_rec = NULL;
		} else {
			*client_rec = pclientid;
			status = CLIENT_ID_SUCCESS;
		}

	} else {
		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_CLIENTID,
				     "%s NOTFOUND (assumed EXPIRED)",
				     ht->parameter.ht_name);
		*client_rec = NULL;
		status = CLIENT_ID_EXPIRED;
	}

	return status;
}

/**
 * @brief Triy to get a pointer to an unconfirmed entry for client_id cache.
 *
 * @param[in]  clientid   The client id
 * @param[out] client_rec The found client id structure
 *
 * @return Same as nfs_client_id_get
 */
clientid_status_t nfs_client_id_get_unconfirmed(clientid4 clientid,
						nfs_client_id_t **client_rec)
{
	return nfs_client_id_get(ht_unconfirmed_client_id, clientid,
				 client_rec);
}

/**
 * @brief Tries to get a pointer to an confirmed entry for client_id cache.
 *
 * @param[in]  clientid   The client id
 * @param[out] client_rec The found client id
 *
 * @return the result previously set if the return code CLIENT_ID_SUCCESS
 *
 */
clientid_status_t nfs_client_id_get_confirmed(clientid4 clientid,
					      nfs_client_id_t **client_rec)
{
	return nfs_client_id_get(ht_confirmed_client_id, clientid, client_rec);
}

static hash_parameter_t cid_confirmed_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = client_id_value_hash_func,
	.hash_func_rbt = client_id_rbt_hash_func,
	.hash_func_both = NULL,
	.compare_key = compare_client_id,
	.key_to_str = display_client_id_key,
	.val_to_str = display_client_id_val,
	.ht_name = "Confirmed Client ID",
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_CLIENTID,
};

static hash_parameter_t cid_unconfirmed_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = client_id_value_hash_func,
	.hash_func_rbt = client_id_rbt_hash_func,
	.hash_func_both = NULL,
	.compare_key = compare_client_id,
	.key_to_str = display_client_id_key,
	.val_to_str = display_client_id_val,
	.ht_name = "Unconfirmed Client ID",
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_CLIENTID,
};

static hash_parameter_t cr_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = client_record_value_hash_func,
	.hash_func_rbt = client_record_rbt_hash_func,
	.hash_func_both = NULL,
	.compare_key = compare_client_record,
	.key_to_str = display_client_record_key,
	.val_to_str = display_client_record_val,
	.ht_name = "Client Record",
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_CLIENTID,
};

/**
 * @brief Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 *
 * @return 0 if successful, -1 otherwise
 */
int nfs_Init_client_id(void)
{
	ht_confirmed_client_id =
		hashtable_init(&cid_confirmed_hash_param);

	if (ht_confirmed_client_id == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id cache");
		return -1;
	}

	ht_unconfirmed_client_id =
		hashtable_init(&cid_unconfirmed_hash_param);

	if (ht_unconfirmed_client_id == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id cache");
		return -1;
	}

	ht_client_record = hashtable_init(&cr_hash_param);

	if (ht_client_record == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Record cache");
		return -1;
	}

	client_id_pool =
	    pool_init("NFS4 Client ID Pool", sizeof(nfs_client_id_t),
		      pool_basic_substrate, NULL, NULL, NULL);

	if (client_id_pool == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id Pool");
		return -1;
	}

	return CLIENT_ID_SUCCESS;
}

/**
 * @brief Builds a new clientid4 value
 *
 * We use the clientid counter and the server epoch, the latter
 * ensures that clientids from old instances of Ganesha are marked as
 * invalid.
 *
 * @return The new clientid.
 */

clientid4 new_clientid(void)
{
	clientid4 newid = atomic_inc_uint32_t(&clientid_counter);
	uint64_t epoch_low = ServerEpoch & 0xFFFFFFFF;

	return newid + (epoch_low << (clientid4) 32);
}

/**
 * @brief Builds a new verifier4 value.
 *
 * @param[out] verf The verifier
 */
void new_clientid_verifier(char *verf)
{
	uint64_t my_verifier = atomic_inc_uint64_t(&clientid_verifier);

	memcpy(verf, &my_verifier, NFS4_VERIFIER_SIZE);
}

/******************************************************************************
 *
 * Functions to handle lookup of clientid by nfs_client_id4 received from
 * client.
 *
 *****************************************************************************/

/**
 * @brief Display a client owner record
 *
 * @param[in]  record The record to display
 * @param[out] str    Output buffer
 *
 * @return Length of output string.
 */

int display_client_record(nfs_client_record_t *record, char *str)
{
	char *strtmp = str;

	strtmp += sprintf(strtmp, "%p name=", record);

	strtmp +=
	    DisplayOpaqueValue(record->cr_client_val, record->cr_client_val_len,
			       strtmp);

	strtmp +=
	    sprintf(strtmp, " refcount=%" PRId32,
		    atomic_fetch_int32_t(&record->cr_refcount));

	return strtmp - str;
}

/**
 * @brief Increment the refcount on a client owner record
 *
 * @param[in] record Record on which to take a reference
 */

int32_t inc_client_record_ref(nfs_client_record_t *record)
{
	int32_t rec_refcnt = atomic_inc_int32_t(&record->cr_refcount);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(record, str);
		LogFullDebug(COMPONENT_CLIENTID, "Increment refcount {%s}",
			     str);
	}

	return rec_refcnt;
}

/**
 * @brief Deconstruct and free a client owner record
 *
 * @param[in] record The record to free
 */
void free_client_record(nfs_client_record_t *record)
{
	if (pthread_mutex_destroy(&record->cr_mutex) != 0)
		LogCrit(COMPONENT_CLIENTID,
			"pthread_mutex_destroy returned errno %d(%s)", errno,
			strerror(errno));
	gsh_free(record);
}

/**
 * @brief Decrement the refcount on a client owner record
 *
 * @param[in] record Record on which to release a reference
 */

int32_t dec_client_record_ref(nfs_client_record_t *record)
{
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;
	int32_t refcount;

	if (isDebug(COMPONENT_CLIENTID))
		display_client_record(record, str);

	refcount = atomic_dec_int32_t(&record->cr_refcount);

	if (refcount > 0) {
		LogFullDebug(COMPONENT_CLIENTID,
			     "Decrement refcount refcount now=%" PRId32 " {%s}",
			     refcount, str);

		return refcount;
	}

	LogFullDebug(COMPONENT_CLIENTID, "Try to remove {%s}", str);

	buffkey.addr = record;
	buffkey.len = sizeof(*record);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(ht_client_record, &buffkey, &old_value, true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_client_record, &latch);

		display_client_record(record, str);

		LogCrit(COMPONENT_CLIENTID, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return refcount;
	}

	refcount = atomic_fetch_int32_t(&record->cr_refcount);

	if (refcount > 0) {
		LogDebug(COMPONENT_CLIENTID,
			 "Did not release refcount now=%" PRId32 " {%s}",
			 refcount, str);

		hashtable_releaselatched(ht_client_record, &latch);
		return refcount;
	}

	/* use the key to delete the entry */
	rc = hashtable_deletelatched(ht_client_record, &buffkey, &latch,
				     &old_key, &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_client_record, &latch);

		display_client_record(record, str);

		LogCrit(COMPONENT_CLIENTID, "Error %s, could not remove {%s}",
			hash_table_err_to_str(rc), str);

		return refcount;
	}

	LogFullDebug(COMPONENT_CLIENTID, "Free {%s}", str);

	free_client_record(old_value.addr);

	return refcount;
}

/**
 * @brief Hash a client owner record key
 *
 * @param[in] key The client owner record
 *
 * @return The hash.
 */

uint64_t client_record_value_hash(nfs_client_record_t *key)
{
	uint64_t other;

	other = key->cr_pnfs_flags;
	other = (other << 32) | key->cr_server_addr;
	return CityHash64WithSeed(key->cr_client_val, key->cr_client_val_len,
				  other);
}

/**
 *
 * @brief Computes the hash value for the entry in Client Record cache.
 *
 * @param[in] hparam Hash table parameter.
 * @param[in] key    The hash key buffer
 *
 * @return the computed hash value.
 *
 * @see hashtable_init
 *
 */
uint32_t client_record_value_hash_func(hash_parameter_t *hparam,
				       struct gsh_buffdesc *key)
{
	uint64_t res;

	res = client_record_value_hash(key->addr) % hparam->index_size;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_CLIENTID, "value = %" PRIu64, res);

	return (uint32_t) res;
}

/**
 * @brief Computes the RBT hash for the entry in Client Id cache.
 *
 * @param[in] hparam Hash table parameter
 * @param[in] key    The hash key buffer
 *
 * @return The computed rbt value.
 *
 * @see hashtable_init
 *
 */
uint64_t client_record_rbt_hash_func(hash_parameter_t *hparam,
				     struct gsh_buffdesc *key)
{
	uint64_t res;

	res = client_record_value_hash(key->addr);

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_CLIENTID, "value = %" PRIu64, res);

	return res;
}

/**
 * @brief Compares the cr_client_val the key buffers.
 *
 * @param[in] buff1 first key
 * @param[in] buff2 second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_client_record(struct gsh_buffdesc *buff1,
			  struct gsh_buffdesc *buff2)
{
	nfs_client_record_t *pkey1 = buff1->addr;
	nfs_client_record_t *pkey2 = buff2->addr;

	if (pkey1->cr_client_val_len != pkey2->cr_client_val_len)
		return 1;
	if (pkey1->cr_pnfs_flags != pkey2->cr_pnfs_flags)
		return 1;

	return memcmp(pkey1->cr_client_val, pkey2->cr_client_val,
		      pkey1->cr_client_val_len);
}

/**
 * @brief Displays the client_record stored in the buffer.
 *
 * @param[in]  buff Buffer to display
 * @param[out] str  output string
 *
 * @return The number of character written.
 */
int display_client_record_key(struct gsh_buffdesc *buff, char *str)
{
	return display_client_record(buff->addr, str);
}

/**
 * @brief Displays the client_record stored in the buffer.
 *
 * @param[in]  buff Buffer to display
 * @param[out] str  output string
 *
 * @return The number of character written.
 */
int display_client_record_val(struct gsh_buffdesc *buff, char *str)
{
	return display_client_record(buff->addr, str);
}

/**
 * @brief Get a client record from the table
 *
 * @param[in] value Client owner name
 * @param[in] len   Length of owner name
 *
 * @return The client record or NULL.
 */
nfs_client_record_t *get_client_record(const char *const value,
				       const size_t len,
				       const uint32_t pnfs_flags,
				       const uint32_t server_addr)
{
	nfs_client_record_t *record;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	struct hash_latch latch;
	hash_error_t rc;

	record = gsh_malloc(sizeof(nfs_client_record_t) + len);

	if (record == NULL)
		return NULL;

	record->cr_refcount = 1;
	record->cr_client_val_len = len;
	record->cr_confirmed_rec = NULL;
	record->cr_unconfirmed_rec = NULL;
	memcpy(record->cr_client_val, value, len);
	record->cr_pnfs_flags = pnfs_flags;
	record->cr_server_addr = server_addr;
	buffkey.addr = record;
	buffkey.len = sizeof(*record);

	/* If we found it, return it, if we don't care, return NULL */
	rc = hashtable_getlatch(ht_client_record, &buffkey, &buffval, true,
				&latch);

	if (rc == HASHTABLE_SUCCESS) {
		/* Discard the key we created and return the found
		 * Client Record.  Directly free since we didn't
		 * complete initialization.
		 */
		gsh_free(record);
		record = buffval.addr;
		inc_client_record_ref(record);
		hashtable_releaselatched(ht_client_record, &latch);
		return record;
	}

	/* Any other result other than no such key is an error */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		/* Discard the key we created and return.  Directly
		 * free since we didn't complete initialization.
		 */

		LogFatal(COMPONENT_CLIENTID,
			 "Client record hash table corrupt.");
		gsh_free(record);
		return NULL;
	}

	if (pthread_mutex_init(&record->cr_mutex, NULL) == -1) {
		/* Mutex initialization failed, directly free the
		 * record since we failed to initialize it. Also
		 * release hash latch since we failed to add record.
		 */
		hashtable_releaselatched(ht_client_record, &latch);
		LogFatal(COMPONENT_CLIENTID,
			 "Unable to initialize mutex in client record.");
		gsh_free(record);
		return NULL;
	}

	/* Use same record for record and key */
	buffval.addr = record;
	buffval.len = sizeof(nfs_client_record_t) + len;

	rc = hashtable_setlatched(ht_client_record, &buffkey, &buffval, &latch,
				  HASHTABLE_SET_HOW_SET_NO_OVERWRITE, NULL,
				  NULL);

	if (rc != HASHTABLE_SUCCESS) {
		LogFatal(COMPONENT_CLIENTID,
			 "Client record hash table corrupt.");
		free_client_record(record);
		return NULL;
	}

	return record;
}

struct client_callback_arg {
	void *state;
	nfs_client_id_t *pclientid;
	bool (*cb)(nfs_client_id_t *, void *);
};

/**
 * @brief client callback
 */
static void client_cb(struct fridgethr_context *ctx)
{
	struct client_callback_arg *cb_arg;

	cb_arg = ctx->arg;
	cb_arg->cb(cb_arg->pclientid, cb_arg->state);
	dec_client_id_ref(cb_arg->pclientid);
	if (cb_arg->state)
		gsh_free(cb_arg->state);
	gsh_free(cb_arg);
}

/**
 * @brief Walk the client tree and do the callback on each 4.1 nodes
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

void
nfs41_foreach_client_callback(bool(*cb) (nfs_client_id_t *cl, void *state),
			      void *state)
{
	uint32_t i;
	hash_table_t *ht = ht_confirmed_client_id;
	struct rbt_head *head_rbt;
	struct hash_data *pdata = NULL;
	struct rbt_node *pn;
	nfs_client_id_t *pclientid;
	struct client_callback_arg *cb_arg;
	int rc;

	/* For each bucket of the hashtable */
	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &(ht->partitions[i].rbt);

		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&(ht->partitions[i].lock));

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);
			pclientid = pdata->val.addr;
			RBT_INCREMENT(pn);

			if (pclientid->cid_minorversion > 0) {
				cb_arg = gsh_malloc(
						sizeof(struct
							client_callback_arg));
				if (cb_arg == NULL) {
					LogCrit(COMPONENT_CLIENTID,
						"malloc failed for %p",
						pclientid);
					continue;
				}
				cb_arg->cb = cb;
				cb_arg->state = state;
				cb_arg->pclientid = pclientid;
				inc_client_id_ref(pclientid);
				rc = fridgethr_submit(state_async_fridge,
						 client_cb,
						 cb_arg);
				if (rc != 0) {
					LogCrit(COMPONENT_CLIENTID,
						"unable to start client cb thread %d",
						rc);
					gsh_free(cb_arg);
					dec_client_id_ref(pclientid);
				}
			}
		}
		PTHREAD_RWLOCK_unlock(&(ht->partitions[i].lock));
	}
	return;
}

/** @} */
