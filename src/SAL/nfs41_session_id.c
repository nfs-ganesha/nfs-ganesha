/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @brief The management of the session id cache.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "sal_functions.h"

pool_t *nfs41_session_pool = NULL;

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_session_id;
uint64_t global_sequence = 0;
pthread_mutex_t mutex_sequence = PTHREAD_MUTEX_INITIALIZER;

int display_session_id(char *session_id, char *str)
{
	return DisplayOpaqueValue(session_id,
				  NFS4_SESSIONID_SIZE,
				  str);
}

int display_session_id_key(struct gsh_buffdesc *buff, char *str)
{
	char *strtmp = str;

	strtmp += sprintf(strtmp, "sessionid=");
	strtmp += display_session_id(buff->addr, strtmp);
	return strtmp - str;
}

int display_session(nfs41_session_t *session, char *str)
{
	char *strtmp = str;

	strtmp += sprintf(strtmp, "sessionid=");
	strtmp += display_session_id(session->session_id, strtmp);

	return strtmp - str;
}

int display_session_id_val(struct gsh_buffdesc *buff, char *str)
{
	return display_session(buff->addr, str);
}

int compare_session_id(struct gsh_buffdesc *buff1,
		       struct gsh_buffdesc *buff2)
{
	return memcmp(buff1->addr, buff2->addr, NFS4_SESSIONID_SIZE);
}

uint32_t session_id_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key)
{
	/* Only need to hash the global counter portion since it is unique */
	uint64_t sum;

	memcpy(&sum, key->addr + sizeof(clientid4),
	       sizeof(sum));

	if (isFullDebug(COMPONENT_SESSIONS) && isDebug(COMPONENT_HASHTABLE)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_session_id_key(key , str);
		LogFullDebug(COMPONENT_SESSIONS,
			     "value hash: %s=%"PRIu32,
			     str,
			     (uint32_t)(sum % hparam->index_size));
	}

	return (sum % hparam->index_size);
}

uint64_t session_id_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	/* Only need to hash the global counter portion since it is unique */
	uint64_t i1 = 0;

	memcpy(&i1, key->addr + sizeof(clientid4), sizeof(i1));

	if (isFullDebug(COMPONENT_SESSIONS) && isDebug(COMPONENT_HASHTABLE)) {
		char str[HASHTABLE_DISPLAY_STRLEN];

		display_session_id_key(key, str);
		LogFullDebug(COMPONENT_SESSIONS,
			     "rbt hash: %s=%"PRIu64,
			     str,
			     i1);
	}

	return i1;
}

/**
 * @brief Init the hashtable for Session Id cache.
 *
 * @param[in] param Parameter used to init the session id cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */

int nfs41_Init_session_id(nfs_session_id_parameter_t param)
{
	if ((ht_session_id = HashTable_Init(&param.hash_param)) == NULL) {
		LogCrit(COMPONENT_SESSIONS,
			"NFS SESSION_ID: Cannot init Session Id cache");
		return -1;
	}

	return 0;
}

/**
 * @brief Build a sessionid from a clientid
 *
 * @param[in] clientid  Pointer to the related clientid
 * @param[out sessionid The sessionid
 */

void nfs41_Build_sessionid(clientid4 *clientid, char *sessionid)
{
	uint64_t seq;

	P(mutex_sequence);
	seq = ++global_sequence;
	V(mutex_sequence);

	memset(sessionid, 0, NFS4_SESSIONID_SIZE);
	memcpy(sessionid, clientid, sizeof(clientid4));
	memcpy(sessionid + sizeof(clientid4), &seq, sizeof(seq));
}

/**
 * @brief Set a session into the session hashtable.
 *
 * @param[in] session      Sessionid to add
 * @param[in] session_data Session data to add
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Set(char sessionid[NFS4_SESSIONID_SIZE],
		      nfs41_session_t *session_data)
{
	struct gsh_buffdesc key;
	struct gsh_buffdesc val;
	char str[HASHTABLE_DISPLAY_STRLEN];

	if (isFullDebug(COMPONENT_SESSIONS)) {
		display_session_id(sessionid, str);
		LogFullDebug(COMPONENT_SESSIONS,
			     "Set SSession %s", str);
	}

	if ((key.addr = gsh_malloc(NFS4_SESSIONID_SIZE)) == NULL) {
		return 0;
	}
	memcpy(key.addr, sessionid, NFS4_SESSIONID_SIZE);
	key.len = NFS4_SESSIONID_SIZE;

	val.addr = session_data;
	val.len = sizeof(nfs41_session_t);

	if (HashTable_Test_And_Set(
		    ht_session_id, &key, &val,
		    HASHTABLE_SET_HOW_SET_NO_OVERWRITE) !=
	    HASHTABLE_SUCCESS) {
		return 0;
	}

	return 1;
}

/**
 * @brief Get a pointer to a session from the session hashtable
 *
 * @param[in]  sessionid    The sessionid to look up
 * @param[out] session_data The associated session data
 *
 * @return 1 if ok, 0 otherwise.
 *
 */

int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
			      nfs41_session_t **session_data)
{
	struct gsh_buffdesc key;
	struct gsh_buffdesc val;
	char str[HASHTABLE_DISPLAY_STRLEN];

	if (isFullDebug(COMPONENT_SESSIONS)) {
		display_session_id(sessionid, str);
		LogFullDebug(COMPONENT_SESSIONS,
			     "Get Session %s", str);
	}

	key.addr = sessionid;
	key.len = NFS4_SESSIONID_SIZE;

	if (HashTable_Get(ht_session_id, &key, &val)
	    != HASHTABLE_SUCCESS) {
		LogFullDebug(COMPONENT_SESSIONS,
			     "Session %s Not Found", str);
		return 0;
	}

	*session_data = val.addr;

	LogFullDebug(COMPONENT_SESSIONS,
		     "Session %s Found", str);

	return 1;
}

/**
 * @brief Remove a session from the session hashtable.
 *
 * This also shuts down any back channel and frees the session data.
 *
 * @param[in] sessionid The sessionid to remove
 *
 * @return 1 if ok, 0 otherwise.
 */

int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE])
{
	struct gsh_buffdesc key, old_key, old_value;
	char str[HASHTABLE_DISPLAY_STRLEN];

	if (isFullDebug(COMPONENT_SESSIONS)) {
		display_session_id(sessionid, str);
		LogFullDebug(COMPONENT_SESSIONS,
			     "Delete Session %s", str);
	}

	key.addr = sessionid;
	key.len = NFS4_SESSIONID_SIZE;

	if (HashTable_Del(ht_session_id, &key, &old_key,
			  &old_value) == HASHTABLE_SUCCESS) {
		nfs41_session_t *session = old_value.addr;

		/* free the key that was stored in hash table */
		gsh_free(old_key.addr);
		/* Decrement our reference to the clientid record */
		dec_client_id_ref(session->pclientid_record);
		/* Unlink the session from the client's list of
		   sessions */
		glist_del(&session->session_link);
		/* Destroy the session's back channel (if any) */
		if (session->cb_chan_up) {
			nfs_rpc_destroy_chan(&session->cb_chan);
		}
		/* Free the memory for the session */
		pool_free(nfs41_session_pool, session);
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
	HashTable_Log(COMPONENT_SESSIONS,
		      ht_session_id);
}
