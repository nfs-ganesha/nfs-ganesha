/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * @file nfs41_session_id.c
 * @brief The management of the session id cache.
 */

#include "config.h"
#include "sal_functions.h"

/**
 * @brief Pool for allocating session data
 */
pool_t *nfs41_session_pool = NULL;

/**
 * @param Session ID hash
 */

hash_table_t *ht_session_id;

/**
 * @param counter for creating session IDs.
 */

uint64_t global_sequence = 0;

/**
 * @brief Display a session ID
 *
 * @param[in]  session_id The session ID
 * @param[out] str        Output buffer
 *
 * @return Length of output string.
 */

int display_session_id(char *session_id, char *str)
{
	return DisplayOpaqueValue(session_id, NFS4_SESSIONID_SIZE, str);
}

/**
 * @brief Display a key in the session ID table
 *
 * @param[in]  buff The key to display
 * @param[out] str  Displayed key
 *
 * @return Length of output string.
 */

int display_session_id_key(struct gsh_buffdesc *buff, char *str)
{
	char *strtmp = str;

	strtmp += sprintf(strtmp, "sessionid=");
	strtmp += display_session_id(buff->addr, strtmp);
	return strtmp - str;
}

/**
 * @brief Display a session object
 *
 * @param[in]  session The session to display
 * @param[out] str     Output buffer
 *
 * @return Length of displayed string.
 */

int display_session(nfs41_session_t *session, char *str)
{
	char *strtmp = str;

	strtmp += sprintf(strtmp, "sessionid=");
	strtmp += display_session_id(session->session_id, strtmp);

	return strtmp - str;
}

/**
 * @brief Display a value in the session ID table
 *
 * @param[in]  buff The value to display
 * @param[out] str  Displayed value
 *
 * @return Length of output string.
 */

int display_session_id_val(struct gsh_buffdesc *buff, char *str)
{
	return display_session(buff->addr, str);
}

/**
 * @brief Compare two session IDs in the hash table
 *
 * @retval 0 if they are equal.
 * @retval 1 if they are not.
 */

int compare_session_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	return memcmp(buff1->addr, buff2->addr, NFS4_SESSIONID_SIZE);
}

/**
 * @brief Hash index of a sessionid
 *
 * @param[in] hparam Hash table parameters
 * @param[in] key    The session key
 *
 * @return The hash index of the key.
 */

uint32_t session_id_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key)
{
	/* Only need to take the mod of the global counter portion
	   since it is unique */
	uint64_t *counter = key->addr + sizeof(clientid4);

	return *counter % hparam->index_size;
}

/**
 * @brief RBT hash of a sessionid
 *
 * @param[in] hparam Hash table parameters
 * @param[in] key    The session key
 *
 * @return The RBT hash of the key.
 */

uint64_t session_id_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	/* Only need to return the global counter portion since it is unique */
	uint64_t *counter = key->addr + sizeof(clientid4);

	return *counter;
}

static hash_parameter_t session_id_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = session_id_value_hash_func,
	.hash_func_rbt = session_id_rbt_hash_func,
	.compare_key = compare_session_id,
	.key_to_str = display_session_id_key,
	.val_to_str = display_session_id_val,
	.flags = HT_FLAG_CACHE,
};

/**
 * @brief Init the hashtable for Session Id cache.
 *
 * @retval 0 if successful.
 * @retval -1 otherwise
 *
 */

int nfs41_Init_session_id(void)
{
	ht_session_id = hashtable_init(&session_id_param);

	if (ht_session_id == NULL) {
		LogCrit(COMPONENT_SESSIONS,
			"NFS SESSION_ID: Cannot init Session Id cache");
		return -1;
	}

	return 0;
}

/**
 * @brief Build a sessionid from a clientid
 *
 * @param[in]  clientid  Pointer to the related clientid
 * @param[out] sessionid The sessionid
 */

void nfs41_Build_sessionid(clientid4 *clientid, char *sessionid)
{
	uint64_t seq;

	seq = atomic_inc_uint64_t(&global_sequence);

	memset(sessionid, 0, NFS4_SESSIONID_SIZE);
	memcpy(sessionid, clientid, sizeof(clientid4));
	memcpy(sessionid + sizeof(clientid4), &seq, sizeof(seq));
}

int32_t inc_session_ref(nfs41_session_t *session)
{
	int32_t refcnt = atomic_inc_int32_t(&session->refcount);
	return refcnt;
}

int32_t dec_session_ref(nfs41_session_t *session)
{
	int32_t refcnt = atomic_dec_int32_t(&session->refcount);
	if (refcnt == 0) {

		/* Unlink the session from the client's list of
		   sessions */
		pthread_mutex_lock(&session->clientid_record->cid_mutex);
		glist_del(&session->session_link);
		pthread_mutex_unlock(&session->clientid_record->cid_mutex);

		/* Decrement our reference to the clientid record */
		dec_client_id_ref(session->clientid_record);

		/* Destroy the session's back channel (if any) */
		if (session->flags & session_bc_up)
			nfs_rpc_destroy_chan(&session->cb_chan);

		/* Free the memory for the session */
		pool_free(nfs41_session_pool, session);
	}

	return refcnt;
}

/**
 * @brief Set a session into the session hashtable.
 *
 * @param[in] sessionid    Sessionid to add
 * @param[in] session_data Session data to add
 *
 * @retval 1 if successful.
 * @retval 0 otherwise.
 *
 */

int nfs41_Session_Set(nfs41_session_t *session_data)
{
	struct gsh_buffdesc key;
	struct gsh_buffdesc val;
	struct hash_latch latch;
	hash_error_t code;
	int rc = 0;

	key.addr = session_data->session_id;
	key.len = NFS4_SESSIONID_SIZE;

	val.addr = session_data;
	val.len = sizeof(nfs41_session_t);

	/* The latch idiom isn't strictly necessary here */
	code = hashtable_getlatch(ht_session_id, &key, &val, true, &latch);
	if (code == HASHTABLE_SUCCESS) {
		hashtable_releaselatched(ht_session_id, &latch);
		goto out;
	}
	if (code == HASHTABLE_ERROR_NO_SUCH_KEY) {
		/* nfs4_op_create_session ensures refcount == 2 for new
		 * session records */
		code =
		    hashtable_setlatched(ht_session_id, &key, &val, &latch,
					 FALSE, NULL, NULL);
		if (code == HASHTABLE_SUCCESS)
			rc = 1;
	}

 out:
	return rc;
}

/**
 * @brief Get a pointer to a session from the session hashtable
 *
 * @param[in]  sessionid    The sessionid to look up
 * @param[out] session_data The associated session data
 *
 * @retval 1 if successful.
 * @retval 0 otherwise.
 */

int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
			      nfs41_session_t **session_data)
{
	struct gsh_buffdesc key;
	struct gsh_buffdesc val;
	struct hash_latch latch;
	char str[HASHTABLE_DISPLAY_STRLEN];
	hash_error_t code;

	if (isFullDebug(COMPONENT_SESSIONS)) {
		display_session_id(sessionid, str);
		LogFullDebug(COMPONENT_SESSIONS, "Get Session %s", str);
	}

	key.addr = sessionid;
	key.len = NFS4_SESSIONID_SIZE;

	code = hashtable_getlatch(ht_session_id, &key, &val, false, &latch);
	if (code != HASHTABLE_SUCCESS) {
		hashtable_releaselatched(ht_session_id, &latch);
		LogFullDebug(COMPONENT_SESSIONS, "Session %s Not Found", str);
		return 0;
	}

	*session_data = val.addr;
	inc_session_ref(*session_data);	/* XXX more locks? */

	hashtable_releaselatched(ht_session_id, &latch);

	LogFullDebug(COMPONENT_SESSIONS, "Session %s Found", str);

	return 1;
}

/**
 * @brief Remove a session from the session hashtable.
 *
 * This also shuts down any back channel and frees the session data.
 *
 * @param[in] sessionid The sessionid to remove
 *
 * @return 1 if successful.
 * @retval 0 otherwise.
 */

int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE])
{
	struct gsh_buffdesc key, old_key, old_value;

	key.addr = sessionid;
	key.len = NFS4_SESSIONID_SIZE;

	if (HashTable_Del(ht_session_id, &key, &old_key, &old_value) ==
	    HASHTABLE_SUCCESS) {
		nfs41_session_t *session = old_value.addr;

		/* unref session */
		dec_session_ref(session);

		return 1;
	} else {
		return 0;
	}
}

/**
 * @brief Display the content of the session hashtable
 */

void nfs41_Session_PrintAll(void)
{
	hashtable_log(COMPONENT_SESSIONS, ht_session_id);
}

/** @} */
