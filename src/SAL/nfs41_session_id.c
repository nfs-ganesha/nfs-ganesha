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
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"
#ifdef USE_LTTNG
#include "gsh_lttng/nfs4.h"
#endif

/**
 * @brief Pool for allocating session data
 */
pool_t *nfs41_session_pool;

/**
 * @param Session ID hash
 */

hash_table_t *ht_session_id;

/**
 * @param counter for creating session IDs.
 */

uint64_t global_sequence;

/**
 * @brief Display a session ID
 *
 * @param[in/out] dspbuf     display_buffer describing output string
 * @param[in]     session_id The session ID
 *
 * @return the bytes remaining in the buffer.
 */

int display_session_id(struct display_buffer *dspbuf, char *session_id)
{
	int b_left = display_cat(dspbuf, "sessionid=");

	if (b_left > 0)
		b_left = display_opaque_value(dspbuf,
					      session_id,
					      NFS4_SESSIONID_SIZE);

	return b_left;
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
	struct display_buffer dspbuf = {HASHTABLE_DISPLAY_STRLEN, str, str};

	display_session_id(&dspbuf, buff->addr);
	return display_buffer_len(&dspbuf);
}

/**
 * @brief Display a session object
 *
 * @param[in]  buff The key to display
 * @param[in]  session The session to display
 *
 * @return Length of output string.
 */

int display_session(struct display_buffer *dspbuf, nfs41_session_t *session)
{
	int b_left = display_printf(dspbuf, "session %p {", session);

	if (b_left > 0)
		b_left = display_session_id(dspbuf, session->session_id);

	if (b_left > 0)
		b_left = display_cat(dspbuf, "}");

	return b_left;
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
	struct display_buffer dspbuf = {HASHTABLE_DISPLAY_STRLEN, str, str};

	display_session(&dspbuf, buff->addr);
	return display_buffer_len(&dspbuf);
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
	.ht_log_component = COMPONENT_SESSIONS,
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

int32_t _inc_session_ref(nfs41_session_t *session, const char *func, int line)
{
	int32_t refcnt = atomic_inc_int32_t(&session->refcount);
#ifdef USE_LTTNG
	tracepoint(nfs4, session_ref, func, line, session, refcnt);
#endif
	return refcnt;
}

int32_t _dec_session_ref(nfs41_session_t *session, const char *func, int line)
{
	int i;
	int32_t refcnt = atomic_dec_int32_t(&session->refcount);
#ifdef USE_LTTNG
	tracepoint(nfs4, session_unref, func, line, session, refcnt);
#endif

	if (refcnt == 0) {

		/* Unlink the session from the client's list of
		   sessions */
		PTHREAD_MUTEX_lock(&session->clientid_record->cid_mutex);
		glist_del(&session->session_link);
		PTHREAD_MUTEX_unlock(&session->clientid_record->cid_mutex);

		/* Decrement our reference to the clientid record */
		dec_client_id_ref(session->clientid_record);
		/* Destroy this session's mutexes and condition variable */

		for (i = 0; i < session->nb_slots; i++) {
			nfs41_session_slot_t *slot;

			slot = &session->fc_slots[i];
			PTHREAD_MUTEX_destroy(&slot->lock);
			release_slot(slot);
		}

		PTHREAD_COND_destroy(&session->cb_cond);
		PTHREAD_MUTEX_destroy(&session->cb_mutex);

		/* Destroy the session's back channel (if any) */
		if (session->flags & session_bc_up)
			nfs_rpc_destroy_chan(&session->cb_chan);

		/* Free the slot tables */
		gsh_free(session->fc_slots);
		gsh_free(session->bc_slots);

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
					 false, NULL, NULL);
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
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	hash_error_t code;

	if (isFullDebug(COMPONENT_SESSIONS)) {
		display_session_id(&dspbuf, sessionid);
		LogFullDebug(COMPONENT_SESSIONS, "Get Session %s", str);
		str_valid = true;
	}

	key.addr = sessionid;
	key.len = NFS4_SESSIONID_SIZE;

	code = hashtable_getlatch(ht_session_id, &key, &val, false, &latch);
	if (code != HASHTABLE_SUCCESS) {
		hashtable_releaselatched(ht_session_id, &latch);
		if (str_valid)
			LogFullDebug(COMPONENT_SESSIONS,
				     "Session %s Not Found", str);
		return 0;
	}

	*session_data = val.addr;
	inc_session_ref(*session_data);	/* XXX more locks? */

	hashtable_releaselatched(ht_session_id, &latch);

	if (str_valid)
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

		return true;
	} else {
		return false;
	}
}

/**
 * @brief Display the content of the session hashtable
 */

void nfs41_Session_PrintAll(void)
{
	hashtable_log(COMPONENT_SESSIONS, ht_session_id);
}

bool check_session_conn(nfs41_session_t *session,
			compound_data_t *data,
			bool can_associate)
{
	int i, num;
	sockaddr_t addr;
	bool associate = false;

	/* Copy the address coming over the wire. */
	copy_xprt_addr(&addr, data->req->rq_xprt);

	PTHREAD_RWLOCK_rdlock(&session->conn_lock);

retry:

	/* Save number of connections for use outside the lock */
	num = session->num_conn;

	for (i = 0; i < session->num_conn; i++) {
		if (isFullDebug(COMPONENT_SESSIONS)) {
			char str1[LOG_BUFF_LEN / 2] = "\0";
			char str2[LOG_BUFF_LEN / 2] = "\0";
			struct display_buffer db1 = {sizeof(str1), str1, str1};
			struct display_buffer db2 = {sizeof(str2), str2, str2};

			display_sockaddr(&db1, &addr);
			display_sockaddr(&db2, &session->connections[i]);
			LogFullDebug(COMPONENT_SESSIONS,
				     "Comparing addr %s for %s to Session bound addr %s",
				     str1, data->opname, str2);
		}

		if (cmp_sockaddr(&addr, &session->connections[i], false)) {
			/* We found a match */
			PTHREAD_RWLOCK_unlock(&session->conn_lock);
			return true;
		}
	}

	if (!can_associate || num == NFS41_MAX_CONNECTIONS) {
		/* We either aren't allowd to associate a new address or there
		 * is no room.
		 */
		PTHREAD_RWLOCK_unlock(&session->conn_lock);

		if (isDebug(COMPONENT_SESSIONS)) {
			char str1[LOG_BUFF_LEN / 2] = "\0";
			struct display_buffer db1 = {sizeof(str1), str1, str1};

			display_sockaddr(&db1, &addr);
			LogDebug(COMPONENT_SESSIONS,
				     "Found no match for addr %s for %s",
				     str1, data->opname);
		}

		return false;
	}

	if (!associate) {
		/* First pass was with read lock, now acquire write lock and
		 * try again.
		 */
		associate = true;
		PTHREAD_RWLOCK_unlock(&session->conn_lock);
		PTHREAD_RWLOCK_wrlock(&session->conn_lock);
		goto retry;
	}

	/* Add the new connection. */
	memcpy(&session->connections[session->num_conn++], &addr, sizeof(addr));

	PTHREAD_RWLOCK_unlock(&session->conn_lock);

	return true;
}

/** @} */
