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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <assert.h>
#include "HashTable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "nfs4.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"
#include "abstract_atomic.h"

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
 * @brief Mutex to protect the counter
 */
pthread_mutex_t clientid_mutex = PTHREAD_MUTEX_INITIALIZER;

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
 * @brief Pool for client owner records
 */
pool_t *client_record_pool;

/**
 * @brief Return the nfs4 status for the client id error code
 *
 * @param[in] client id error code
 *
 * @return the corresponding nfs4 error code
 */
nfsstat4 clientid_error_to_nfsstat(clientid_status_t err)
{
	switch(err) {
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

	LogCrit(COMPONENT_CLIENTID,
		"Unexpected clientid error %d", err);

	return NFS4ERR_SERVERFAULT;
}

/**
 * @brief Return the nfs4 status string for the client id error code
 *
 * @param[in] client id error code
 *
 * @return the error string corresponding nfs4 error code
 */
const char * clientid_error_to_str(clientid_status_t err)
{
	switch(err) {
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

	LogCrit(COMPONENT_CLIENTID,
		"Unexpected clientid error %d", err);

	return "UNEXPECTED ERROR";
}


/**
 * @brief Return a string corresponding to the confirm state
 *
 * @param[in] confirmed Confirm state
 *
 * @return Corresponding string.
 */
const char *clientid_confirm_state_to_str(
	nfs_clientid_confirm_state_t confirmed)
{
	switch(confirmed) {
	case CONFIRMED_CLIENT_ID:
		return "CONFIRMED";
	case UNCONFIRMED_CLIENT_ID:
		return "UNCONFIRMED";
	case EXPIRED_CLIENT_ID:
		return "EXPIRED";
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

	tmpstr += sprintf(tmpstr,
			  "%p ClientID=%"PRIx64" %s Client={",
			  clientid,
			  clientid->cid_clientid,
			  clientid_confirm_state_to_str(
				  clientid->cid_confirmed));

	if (clientid->cid_client_record != NULL)
		tmpstr += display_client_record(clientid->cid_client_record,
						tmpstr);
	else
		tmpstr += sprintf(tmpstr, "<NULL>");

	if (clientid->cid_lease_reservations > 0)
		delta = 0;
	else
		delta = time(NULL) - clientid->cid_last_renew;

	if (clientid->cid_minorversion == 0) {
		tmpstr += sprintf(tmpstr,
				  "} cb_prog=%u r_addr=%s r_netid=%s "
				  "t_delta=%d reservations=%d "
				  "refcount=%"PRId32,
				  clientid->cid_cb.v40.cb_program,
				  clientid->cid_cb.v40.cb_client_r_addr,
				  (netid_nc_table[clientid->cid_cb.v40
						  .cb_addr.nc].netid),
				  delta,
				  clientid->cid_lease_reservations,
				  atomic_fetch_int32_t(&clientid->
						       cid_refcount));
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
		return DisplayOpaqueValue(clientid->cid_client_record
					  ->cr_client_val,
					  clientid->cid_client_record
					  ->cr_client_val_len,
					  str);
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

	(void) inc_client_id_ref(clientid);
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
			     "Increment refcount Clientid {%s} to %"PRId32,
			     str, cid_refcount);
	}

        return (cid_refcount);
}

/**
 * @brief nfsv41 has-sessions prediccate (returns true if sessions found)
 *
 * @param[in] clientid Client record
 */
bool client_id_has_nfs41_sessions(nfs_client_id_t *clientid)
{
    if (clientid->cid_minorversion > 0) {
        if (! glist_empty(&clientid->cid_cb.v41.cb_session_list)) {
            return (true);
        }
    }

    return (false);
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
		(void) dec_client_record_ref(clientid->cid_client_record);

	if (pthread_mutex_destroy(&clientid->cid_mutex) != 0)
		LogDebug(COMPONENT_CLIENTID,
			 "pthread_mutex_destroy returned errno %d (%s)",
			 errno, strerror(errno));

	/* For NFSv4.1 clientids, destroy all associated sessions */
	if (clientid->cid_minorversion > 0) {
		struct glist_head *glist = NULL;
		struct glist_head *glistn = NULL;

		glist_for_each_safe(glist,
				    glistn,
				    &clientid->cid_cb.v41.cb_session_list) {
			nfs41_session_t *session
				= glist_entry(glist,
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
		     "Decrement refcount Clientid {%s} refcount to %"PRId32,
		     str, cid_refcount);

	if (cid_refcount > 0)
		return (cid_refcount);

	/* We don't need a lock to look at cid_confirmed because when
	 * refcount has gone to 0, no other threads can have a pointer
	 * to the clientid record.
	 */
	if (clientid->cid_confirmed != EXPIRED_CLIENT_ID) {
		/* Is not in any hash table, so we can just delete it */
		LogFullDebug(COMPONENT_CLIENTID,
			     "Free Clientid {%s} refcount now=0",
			     str);

		free_client_id(clientid);
	} else {
		/* Clientid records should not be freed unless marked expired. */
		LogDebug(COMPONENT_CLIENTID,
			 "Should not be here, try to remove last ref {%s}",
			 str);

		assert(clientid->cid_confirmed == EXPIRED_CLIENT_ID);
	}

	return (cid_refcount);
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
 * @see HashTable_Init
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
 * @see HashTable_Init
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
 * @param[in] buff1 buffer to display
 * @param[in] buff2 output string
 *
 * @return Number of character written.
 *
 */
int display_client_id_key(struct gsh_buffdesc *buff, char *str)
{
	clientid4 clientid;

	clientid = *((clientid4 *) (buff->addr));

	return sprintf(str, "%"PRIx64, clientid);
}

/**
 * @brief Displays the client record from a hash table
 *
 * @param[in] buff1 buffer to display
 * @param[in] buff2 output string
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
				  uint32_t minorversion)
{
	nfs_client_id_t *client_rec = pool_alloc(client_id_pool, NULL);
	state_owner_t *owner;

	if (client_rec == NULL) {
		LogDebug(COMPONENT_CLIENTID,
			 "Unable to allocate memory for clientid %"PRIx64,
			 clientid);
		return NULL;
	}

	if (pthread_mutex_init(&client_rec->cid_mutex, NULL) == -1) {
		if (isDebug(COMPONENT_CLIENTID)) {
			char str_client[NFS4_OPAQUE_LIMIT * 2 + 1];

			display_clientid_name(client_rec,
					      str_client);

			LogDebug(COMPONENT_CLIENTID,
				 "Could not init mutex for clientid %"
				 PRIx64"->%s",
				 clientid, str_client);
		}

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

		/* Directly free the clientid record since we failed to initialize it */
		pool_free(client_id_pool, client_rec);

		return NULL;
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

	/* need to init the list_head */
	init_glist(&client_rec->cid_openowners);
	init_glist(&client_rec->cid_lockowners);

	/* set up the content of the clientid_owner */
	owner->so_type = STATE_CLIENTID_OWNER_NFSV4;
	owner->so_owner.so_nfs4_owner.so_clientid  = clientid;
	owner->so_owner.so_nfs4_owner.so_clientrec = client_rec;
	owner->so_owner.so_nfs4_owner.so_resp.resop = NFS4_OP_ILLEGAL;
	owner->so_owner.so_nfs4_owner.so_args.argop = NFS4_OP_ILLEGAL;
	owner->so_refcount = 1;

	/* Init the lists for the clientid_owner */
	init_glist(&owner->so_lock_list);
	init_glist(&owner->so_owner.so_nfs4_owner.so_state_list);

	/* Get a reference to the client record */
	(void) inc_client_record_ref(client_rec->cid_client_record);

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
	buffkey.len   = sizeof(clientid->cid_clientid);

	buffdata.addr = clientid;
	buffdata.len   = sizeof(nfs_client_id_t);

	rc = HashTable_Test_And_Set(ht_unconfirmed_client_id,
				    &buffkey,
				    &buffdata,
				    HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not insert unconfirmed clientid %"
			 PRIx64" error=%s",
			 clientid->cid_clientid,
			 hash_table_err_to_str(rc));

		/* Free the clientid record and return */
		free_client_id(clientid);

		return CLIENT_ID_INSERT_MALLOC_ERROR;
	}

	/* Take a reference to the unconfirmed clientid for the hash table. */
	(void) inc_client_id_ref(clientid);

	if (isFullDebug(COMPONENT_CLIENTID) &&
	    isFullDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID,
			     "-=-=-=-=-=-=-=-=-=-> ht_unconfirmed_client_id ");
		HashTable_Log(COMPONENT_CLIENTID, ht_unconfirmed_client_id);
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

	rc = HashTable_Del(ht_confirmed_client_id,
			   &buffkey,
			   &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not remove unconfirmed clientid %"
			 PRIx64" error=%s",
			 clientid->cid_clientid,
			 hash_table_err_to_str(rc));
		return rc;
	}

	clientid->cid_client_record->cr_confirmed_rec = NULL;

	/* Set this up so this client id record will be freed. */
	clientid->cid_confirmed = EXPIRED_CLIENT_ID;

	/* Release hash table reference to the unconfirmed record */
	(void) dec_client_id_ref(clientid);

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
	buffkey.len   = sizeof(clientid->cid_clientid);

	rc = HashTable_Del(ht_unconfirmed_client_id,
			   &buffkey,
			   &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not remove unconfirmed clientid %"
			 PRIx64" error=%s",
			 clientid->cid_clientid,
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
	(void) dec_client_id_ref(clientid);

	return rc;
}

/**
 * @brief Confirms a client id record.
 *
 * @param[in] clientid the client id record
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

	rc = HashTable_Del(ht_unconfirmed_client_id,
			   &buffkey,
			   &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(clientid, str);

			LogDebug(COMPONENT_CLIENTID,
				 "Unexpected problem %s, could not remove "
				 "{%s}",
				 hash_table_err_to_str(rc), str);
		}

		return CLIENT_ID_INVALID_ARGUMENT;
	}

	clientid->cid_confirmed  = CONFIRMED_CLIENT_ID;

	rc = HashTable_Test_And_Set(ht_confirmed_client_id,
				    &old_key,
				    &old_value,
				    HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	if (rc != HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_id_rec(clientid, str);

			LogDebug(COMPONENT_CLIENTID,
				 "Unexpected problem %s, could not "
				 "insert {%s}",
				 hash_table_err_to_str(rc), str);
		}

		/* Set this up so this client id record will be
		   freed. */
		clientid->cid_confirmed = EXPIRED_CLIENT_ID;

		/* Release hash table reference to the unconfirmed
		   record */
		(void) dec_client_id_ref(clientid);

		return CLIENT_ID_INSERT_MALLOC_ERROR;
	}

	/* Add the clientid as the confirmed entry for the client
	   record */
	clientid->cid_client_record->cr_confirmed_rec = clientid;

	return CLIENT_ID_SUCCESS;
}

/**
 * @brief Client expires, need to take care of owners
 *
 * This function assumes caller holds record->cr_mutex and holds a
 * reference to record also.
 *
 * @param[in] clientid The client id to expire
 * @param[in] req_ctx  Request context
 *
 * @return true if the clientid is successfully expired.
 */
bool nfs_client_id_expire(nfs_client_id_t *clientid,
			  struct req_op_context *req_ctx)
{
	struct glist_head *glist, *glistn;
	struct glist_head *glist2, *glistn2;
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_key;
	struct gsh_buffdesc old_value;
	hash_table_t *ht_expire;
	nfs_client_record_t *record;

	P(clientid->cid_mutex);
	if (clientid->cid_confirmed == EXPIRED_CLIENT_ID) {
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];
			display_client_id_rec(clientid, str);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Expired (skipped) {%s}", str);
		}

		V(clientid->cid_mutex);
		return false;
	}

	if (isDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];
		display_client_id_rec(clientid, str);
		LogDebug(COMPONENT_CLIENTID,
			 "Expiring {%s}", str);
	}

	if(clientid->cid_confirmed == CONFIRMED_CLIENT_ID)
		ht_expire = ht_confirmed_client_id;
	else
		ht_expire = ht_unconfirmed_client_id;

	clientid->cid_confirmed = EXPIRED_CLIENT_ID;

	/* Need to clean up the client record. */
	record = clientid->cid_client_record;

	V(clientid->cid_mutex);

	/* Detach the clientid record from the client record */
	if (record->cr_confirmed_rec == clientid)
		record->cr_confirmed_rec = NULL;

	if (record->cr_unconfirmed_rec == clientid)
		record->cr_unconfirmed_rec = NULL;

	buffkey.addr = &clientid->cid_clientid;
	buffkey.len = sizeof(clientid->cid_clientid);

	rc = HashTable_Del(ht_expire,
			   &buffkey,
			   &old_key,
			   &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_CLIENTID,
			 "Could not remove expired clientid %"PRIx64
			 " error=%s",
			 clientid->cid_clientid,
			 hash_table_err_to_str(rc));
		assert(rc == HASHTABLE_SUCCESS);
	}

	/* traverse the client's lock owners, and release all locks */
	glist_for_each_safe(glist, glistn,
			    &clientid->cid_lockowners) {
		state_owner_t *plock_owner
			= glist_entry(glist,
				      state_owner_t,
				      so_owner.so_nfs4_owner.so_perclient);

		glist_for_each_safe(
			glist2,
			glistn2,
			&plock_owner->so_owner.so_nfs4_owner.so_state_list) {
			state_t* plock_state = glist_entry(glist2,
							   state_t,
							   state_owner_list);

			state_owner_unlock_all(plock_owner,
					       req_ctx,
					       plock_state);
		}
	}

	/* traverse the client's lock owners, and release all locks
	   states and owners */
	glist_for_each_safe(glist, glistn, &clientid->cid_lockowners) {
		state_owner_t * plock_owner = glist_entry(
			glist,
			state_owner_t,
			so_owner.so_nfs4_owner.so_perclient);
		release_lockstate(plock_owner);

		if (isFullDebug(COMPONENT_CLIENTID)) {
			char owner[HASHTABLE_DISPLAY_STRLEN];

			DisplayOwner(plock_owner, owner);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Expired lock state for {%s}",
				     owner);
		}
	}

	/* release the corresponding open states , close files*/
	glist_for_each_safe(glist, glistn, &clientid->cid_openowners) {
		state_owner_t * popen_owner
			= glist_entry(glist,
				      state_owner_t,
				      so_owner.so_nfs4_owner.so_perclient);
		release_openstate(popen_owner);

		if (isFullDebug(COMPONENT_CLIENTID)) {
			char owner[HASHTABLE_DISPLAY_STRLEN];

			DisplayOwner(popen_owner, owner);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Expired open state for {%s}",
				     owner);
		}
	}

	if (clientid->cid_recov_dir != NULL) {
		nfs4_rm_clid(clientid->cid_recov_dir);
		gsh_free(clientid->cid_recov_dir);
		clientid->cid_recov_dir = NULL;
	}

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];
		display_client_id_rec(clientid, str);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Expired (done) {%s}", str);
	}

	if (isDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];
		display_client_id_rec(clientid, str);
		LogDebug(COMPONENT_CLIENTID,
			 "About to release last reference to {%s}", str);
	}

	/* Release the hash table reference to the clientid. */
	(void) dec_client_id_ref(clientid);

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
clientid_status_t nfs_client_id_get(hash_table_t *ht,
				    clientid4 clientid,
				    nfs_client_id_t **client_rec)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	clientid_status_t status;
	uint64_t      epoch_low = ServerEpoch & 0xFFFFFFFF;
	uint64_t      cid_epoch = (uint64_t) (clientid >>  (clientid4) 32);

	if (client_rec == NULL)
		return CLIENT_ID_INVALID_ARGUMENT;

	/* Don't even bother to look up clientid if epochs don't match */
	if(cid_epoch != epoch_low) {
		if(isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_CLIENTID,
				     "%s NOTFOUND (epoch doesn't match, assumed STALE)",
				     ht->parameter.ht_name);
		return CLIENT_ID_STALE;
	}

	buffkey.addr = &clientid;
	buffkey.len = sizeof(clientid4);

	if (isFullDebug(COMPONENT_CLIENTID) &&
	    isDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID,
			     "%s KEY {%"PRIx64"}",
			     ht->parameter.ht_name,
			     clientid);
	}

	if (isFullDebug(COMPONENT_CLIENTID) &&
	    isFullDebug(COMPONENT_HASHTABLE)) {
		LogFullDebug(COMPONENT_CLIENTID,
			     "-=-=-=-=-=-=-=-=-=-> %s", ht->parameter.ht_name);
		HashTable_Log(COMPONENT_CLIENTID, ht);
	}

	if (HashTable_GetRef(ht,
			     &buffkey,
			     &buffval,
			     Hash_inc_client_id_ref) == HASHTABLE_SUCCESS) {
		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_CLIENTID,
				     "%s FOUND", ht->parameter.ht_name);
		*client_rec = buffval.addr;
		status = CLIENT_ID_SUCCESS;
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
 * @brief Tries to get a pointer to an unconfirmed entry for client_id cache.
 *
 * @param[in] clientid     the client id
 * @param[out] p_clientid the found client id
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
	return nfs_client_id_get(ht_confirmed_client_id, clientid,
				 client_rec);
}

/**
 * @brief Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 *
 * @param[in] param Parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 */
int nfs_Init_client_id(nfs_client_id_parameter_t *param)
{
	if ((ht_confirmed_client_id
	     = HashTable_Init(&param->cid_confirmed_hash_param)) == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id cache");
		return -1;
	}

	if ((ht_unconfirmed_client_id
	     = HashTable_Init(&param->cid_unconfirmed_hash_param)) == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id cache");
		return -1;
	}

	if ((ht_client_record
	     = HashTable_Init(&param->cr_hash_param)) == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Record cache");
		return -1;
	}

	client_id_pool = pool_init("NFS4 Client ID Pool",
				   sizeof(nfs_client_id_t),
				   pool_basic_substrate,
				   NULL,
				   NULL,
				   NULL);

	if (client_id_pool == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Id Pool");
		return -1;
	}

	client_record_pool = pool_init("NFS4 Client Record Pool",
				       sizeof(nfs_client_record_t),
				       pool_basic_substrate,
				       NULL,
				       NULL,
				       NULL);

	if (client_record_pool == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS CLIENT_ID: Cannot init Client Record Pool");
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
	clientid4 newid;
	uint64_t  epoch_low = ServerEpoch & 0xFFFFFFFF;

	P(clientid_mutex);
	newid = ++clientid_counter + (epoch_low << (clientid4) 32);
	V(clientid_mutex);

	return newid;
}

/**
 * @brief Builds a new verifier4 value.
 *
 * @param[out] verf The verifier
 */
void new_clientifd_verifier(char *verf)
{
	P(clientid_mutex);
	++clientid_verifier;
	memcpy(verf, &clientid_verifier, NFS4_VERIFIER_SIZE);
	V(clientid_mutex);
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

	strtmp += DisplayOpaqueValue(record->cr_client_val,
				     record->cr_client_val_len,
				     strtmp);

	strtmp += sprintf(strtmp, " refcount=%"PRId32,
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

	if(isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(record, str);
		LogFullDebug(COMPONENT_CLIENTID,
			     "Increment refcount {%s}",
			     str);
	}

	return (rec_refcnt);
}

/**
 * @brief Deconstruct and free a client owner record
 *
 * @param[in] record The record to free
 */
void free_client_record(nfs_client_record_t *record)
{
	if (pthread_mutex_destroy(&record->cr_mutex) != 0)
		LogDebug(COMPONENT_CLIENTID,
			 "pthread_mutex_destroy returned errno %d(%s)",
			 errno, strerror(errno));
	pool_free(client_record_pool, record);
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
			     "Decrement refcount {%s} refcount now=%"PRId32,
			     str, refcount);

		return (refcount);
	}

	LogFullDebug(COMPONENT_CLIENTID,
		     "Try to remove {%s}",
		     str);

	buffkey.addr = record;
	buffkey.len = sizeof(*record);

	/* Get the hash table entry and hold latch */
	rc = HashTable_GetLatch(ht_client_record,
				&buffkey,
				&old_value,
				true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			HashTable_ReleaseLatched(ht_client_record, &latch);

		LogDebug(COMPONENT_CLIENTID,
			 "Error %s, could not find {%s}",
			 hash_table_err_to_str(rc), str);
	}

	refcount = atomic_fetch_int32_t(&record->cr_refcount);

	if (refcount > 0) {
		LogDebug(COMPONENT_CLIENTID,
			 "Did not release {%s} refcount now=%"PRId32,
			 str, refcount);

		HashTable_ReleaseLatched(ht_client_record, &latch);
		return (refcount);
	}

	/* use the key to delete the entry */
	rc = HashTable_DeleteLatched(ht_client_record,
				     &buffkey,
				     &latch,
				     &old_key,
				     &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			HashTable_ReleaseLatched(ht_client_record, &latch);

		LogDebug(COMPONENT_CLIENTID,
			 "Error %s, could not remove {%s}",
			 hash_table_err_to_str(rc), str);
	}

	LogFullDebug(COMPONENT_CLIENTID,
		     "Free {%s}",
		     str);

	free_client_record(old_value.addr);

	return (refcount);
}

/**
 * @brief Hash a client owner record key
 *
 * @todo ACE: Destroy this function and use a real hash.
 *
 * @param[in] key The client owner record
 *
 * @return The hash.
 */
uint64_t client_record_value_hash(nfs_client_record_t *key)
{
	unsigned int i;
	uint64_t res = 0;
	unsigned char *sum = (unsigned char *) &res;

	/* Compute the sum of all the characters across the uint64_t */
	for (i = 0; i < key->cr_client_val_len; i++)
		sum[i % sizeof(res)] += (unsigned char)key->cr_client_val[i];

	return res;
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
 * @see HashTable_Init
 *
 */
uint32_t client_record_value_hash_func(hash_parameter_t *hparam,
				       struct gsh_buffdesc *key)
{
	uint64_t res;

	res = client_record_value_hash(key->addr) %
		hparam->index_size;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_CLIENTID,
			     "value = %"PRIu64, res);

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
 * @see HashTable_Init
 *
 */
uint64_t client_record_rbt_hash_func(hash_parameter_t *hparam,
					  struct gsh_buffdesc *key)
{
	uint64_t res;

	res = client_record_value_hash(key->addr);

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_CLIENTID,
			     "value = %"PRIu64, res);

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
	nfs_client_record_t * pkey1 = buff1->addr;
	nfs_client_record_t * pkey2 = buff2->addr;

	if (pkey1->cr_client_val_len != pkey2->cr_client_val_len)
		return 1;

	return memcmp(pkey1->cr_client_val,
		      pkey2->cr_client_val,
		      pkey1->cr_client_val_len);
}

/**
 * @brief Displays the client_record stored in the buffer.
 *
 * @param[in] buff Buffer to display
 * @param[out str  output string
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
 * @param[in] buff Buffer to display
 * @param[out str  output string
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
nfs_client_record_t *get_client_record(char *value, int len)
{
	nfs_client_record_t *record;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	struct hash_latch latch;
	hash_error_t rc;

	record = pool_alloc(client_record_pool, NULL);

	if (record == NULL)
		return NULL;

	record->cr_refcount       = 1;
	record->cr_client_val_len = len;
	memcpy(record->cr_client_val, value, len);
	buffkey.addr = record;
	buffkey.len = sizeof(*record);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(record, str);

		LogFullDebug(COMPONENT_CLIENTID,
			     "Find Client Record KEY {%s}", str);
	}

	/* If we found it, return it, if we don't care, return NULL */
	rc = HashTable_GetLatch(ht_client_record,
				&buffkey,
				&buffval,
				true,
				&latch);

	if( rc == HASHTABLE_SUCCESS) {
		/* Discard the key we created and return the found
		 * Client Record.  Directly free since we didn't
		 * complete initialization.
		 */
		pool_free(client_record_pool, record);
		record = buffval.addr;
		inc_client_record_ref(record);
		HashTable_ReleaseLatched(ht_client_record, &latch);
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_record(record, str);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Found {%s}",
				     str);
		}

		return record;
	}

	/* Any other result other than no such key is an error */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		/* Discard the key we created and return.  Directly
		 * free since we didn't complete initialization.
		 */

		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_record(record, str);

			LogDebug(COMPONENT_CLIENTID,
				 "Error %s, failed to find {%s}",
				 hash_table_err_to_str(rc), str);
		}

		pool_free(client_record_pool, record);

		return NULL;
	}

	if (pthread_mutex_init(&record->cr_mutex, NULL) == -1) {
		/* Mutex initialization failed, directly free the
		 * record since we failed to initialize it. Also
		 * release hash latch since we failed to add record.
		 */
		HashTable_ReleaseLatched(ht_client_record, &latch);
		pool_free(client_record_pool, record);
		return NULL;
	}

	/* Use same record for record and key */
	buffval.addr = record;
	buffval.len = sizeof(*record);

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(record, str);
		LogFullDebug(COMPONENT_CLIENTID,
			     "New {%s}", str);
	}

	rc = HashTable_SetLatched(ht_client_record,
				  &buffkey,
				  &buffval,
				  &latch,
				  HASHTABLE_SET_HOW_SET_NO_OVERWRITE,
				  NULL,
				  NULL);

	if (rc == HASHTABLE_SUCCESS) {
		if (isFullDebug(COMPONENT_CLIENTID)) {
			char str[HASHTABLE_DISPLAY_STRLEN];

			display_client_record(record, str);
			LogFullDebug(COMPONENT_CLIENTID,
				     "Set Client Record {%s}",
				     str);
		}

		return record;
	}

	if (isFullDebug(COMPONENT_CLIENTID)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_client_record(record, str);

		LogDebug(COMPONENT_CLIENTID,
			 "Error %s Failed to add {%s}",
			 hash_table_err_to_str(rc), str);
	}

	free_client_record(record);

	return NULL;
}

/** @} */
