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
 * @file nfs4_state_id.c
 * @brief NFSv4 state ids
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>		/* for having isalnum */
#include <stdlib.h>		/* for having atoi */
#include <dirent.h>		/* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include "log.h"
#include "gsh_rpc.h"
#include "hashtable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"
#include "city.h"

/**
 * @brief Hash table for stateids.
 */
hash_table_t *ht_state_id;
hash_table_t *ht_state_obj;

/**
 * @brief All-zeroes stateid4.other
 */
char all_zero[OTHERSIZE];

/**
 * @brief All-zeroes stateid4.other
 */
char all_ones[OTHERSIZE];
#define seqid_all_one 0xFFFFFFFF

/**
 * @brief Display a stateid other
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     other  The other component of the stateid
 *
 * @return the bytes remaining in the buffer.
 */
int display_stateid_other(struct display_buffer *dspbuf, char *other)
{
	uint64_t clientid = *((uint64_t *) other);
	uint32_t count    = *((uint32_t *) (other + sizeof(uint64_t)));
	int b_left = display_cat(dspbuf, "OTHER=");

	if (b_left <= 0)
		return b_left;

	b_left = display_opaque_bytes(dspbuf, other, OTHERSIZE);

	if (b_left <= 0)
		return b_left;

	b_left = display_cat(dspbuf, " {{CLIENTID ");

	if (b_left <= 0)
		return b_left;

	b_left = display_clientid(dspbuf, clientid);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, "} StateIdCounter=0x%08"PRIx32"}", count);
}

/**
 * @brief Display a stateid other in the hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff The key
 */
int display_state_id_key(struct display_buffer *dspbuf,
			 struct gsh_buffdesc *buff)
{
	return display_stateid_other(dspbuf, buff->addr);
}

/**
 * @brief Display a stateid4 from the wire
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     state  The stateid
 *
 * @return the bytes remaining in the buffer.
 */
int display_stateid4(struct display_buffer *dspbuf, stateid4 *stateid)
{
	int b_left = display_stateid_other(dspbuf, stateid->other);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, " seqid=%"PRIu32, stateid->seqid);
}

const char *str_state_type(state_t *state)
{
	switch (state->state_type) {
	case STATE_TYPE_NONE:
		return "NONE";
	case STATE_TYPE_SHARE:
		return "SHARE";
	case STATE_TYPE_DELEG:
		return "DELEGATION";
	case STATE_TYPE_LOCK:
		return "LOCK";
	case STATE_TYPE_LAYOUT:
		return "LAYOUT";
	case STATE_TYPE_NLM_LOCK:
		return "NLM_LOCK";
	case STATE_TYPE_NLM_SHARE:
		return "NLM_SHARE";
	case STATE_TYPE_9P_FID:
		return "9P_FID";
	}

	return "UNKNOWN";
}

/**
 * @brief Display a stateid
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     state  The stateid
 *
 * @return the bytes remaining in the buffer.
 */
int display_stateid(struct display_buffer *dspbuf, state_t *state)
{
	int b_left;

	if (state == NULL)
		return display_cat(dspbuf, "STATE <NULL>");


	b_left = display_printf(dspbuf,
				"STATE %p ",
				state);

	if (b_left <= 0)
		return b_left;

	b_left = display_stateid_other(dspbuf, state->stateid_other);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf,
				" obj=%p type=%s seqid=%"PRIu32" owner={",
				&state->state_obj,
				str_state_type(state),
				state->state_seqid);

	if (b_left <= 0)
		return b_left;

	b_left = display_nfs4_owner(dspbuf, state->state_owner);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf,
			      "} refccount=%"PRId32,
			      atomic_fetch_int32_t(&state->state_refcount));
}

/**
 * @brief Display a state in the hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff The value
 * @return Length of output string.
 */
int display_state_id_val(struct display_buffer *dspbuf,
			 struct gsh_buffdesc *buff)
{
	return display_stateid(dspbuf, buff->addr);
}

/**
 * @brief Compare two stateids
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another key
 *
 * @retval 0 if equal.
 * @retval 1 if not equal.
 */
int compare_state_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	if (isFullDebug(COMPONENT_STATE)) {
		char str1[DISPLAY_STATEID_OTHER_SIZE] = "\0";
		char str2[DISPLAY_STATEID_OTHER_SIZE] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_stateid_other(&dspbuf1, buff1->addr);
		display_stateid_other(&dspbuf2, buff2->addr);

		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1,
				     str2);
	}

	return memcmp(buff1->addr, buff2->addr, OTHERSIZE);
}				/* compare_state_id */

/**
 * @brief Hash a stateid
 *
 * @param[in] stateid Array aliased to stateid
 */
static inline uint32_t compute_stateid_hash_value(uint32_t *stateid)
{
	return stateid[1] ^ stateid[2];
}

/**
 * @brief Hash index for a stateid
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t state_id_value_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	uint32_t val =
	    compute_stateid_hash_value(key->addr) % hparam->index_size;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "val = %" PRIu32, val);

	return val;
}

/**
 * @brief RBT hash for a stateid
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t state_id_rbt_hash_func(hash_parameter_t *hparam,
				struct gsh_buffdesc *key)
{
	uint64_t val = compute_stateid_hash_value(key->addr);

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %" PRIu64, val);

	return val;
}

static hash_parameter_t state_id_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = state_id_value_hash_func,
	.hash_func_rbt = state_id_rbt_hash_func,
	.compare_key = compare_state_id,
	.display_key = display_state_id_key,
	.display_val = display_state_id_val,
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_STATE,
	.ht_name = "State ID Table"
};

/**
 * @brief Compare two stateids by entry/owner
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another key
 *
 * @retval 0 if equal.
 * @retval 1 if not equal.
 */
int compare_state_obj(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	state_t *state1 = buff1->addr;
	state_t *state2 = buff2->addr;

	if (state1 == NULL || state2 == NULL)
		return 1;

	if (state1->state_obj != state2->state_obj)
		return 1;

	return compare_nfs4_owner(state1->state_owner, state2->state_owner);
}

/**
 * @brief Hash index for a stateid by entry/owner
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t state_obj_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i = 0;
	unsigned char c = 0;
	uint32_t res = 0;

	struct gsh_buffdesc fh_desc;
	state_t *pkey = key->addr;

	pkey->state_obj->obj_ops->handle_to_key(pkey->state_obj, &fh_desc);

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->state_owner->so_owner_len; i++) {
		c = ((char *)pkey->state_owner->so_owner_val)[i];
		sum += c;
	}

	res = ((uint32_t) pkey->state_owner->so_owner.so_nfs4_owner.so_clientid
	      + (uint32_t) sum + pkey->state_owner->so_owner_len
	      + (uint32_t) pkey->state_owner->so_type
	      + (uint64_t) CityHash64WithSeed(fh_desc.addr, fh_desc.len, 557))
	      % (uint32_t) hparam->index_size;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %" PRIu32, res);

	return res;
}

/**
 * @brief RBT hash for a stateid by entry/owner
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t state_obj_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key)
{
	state_t *pkey = key->addr;
	struct gsh_buffdesc fh_desc;

	unsigned int sum = 0;
	unsigned int i = 0;
	unsigned char c = 0;
	uint64_t res = 0;

	pkey->state_obj->obj_ops->handle_to_key(pkey->state_obj, &fh_desc);

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->state_owner->so_owner_len; i++) {
		c = ((char *)pkey->state_owner->so_owner_val)[i];
		sum += c;
	}

	res = (uint64_t) pkey->state_owner->so_owner.so_nfs4_owner.so_clientid
	      + (uint64_t) sum + pkey->state_owner->so_owner_len
	      + (uint64_t) pkey->state_owner->so_type
	      + (uint64_t) CityHash64WithSeed(fh_desc.addr, fh_desc.len, 557);

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %" PRIu64, res);

	return res;
}

static hash_parameter_t state_obj_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = state_obj_value_hash_func,
	.hash_func_rbt = state_obj_rbt_hash_func,
	.compare_key = compare_state_obj,
	.display_key = display_state_id_val,
	.display_val = display_state_id_val,
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_STATE,
	.ht_name = "State Obj Table"
};

/**
 * @brief Init the hashtable for stateids
 *
 * @retval 0 if successful.
 * @retval -1 on failure.
 */
int nfs4_Init_state_id(void)
{
	/* Init  all_one */
	memset(all_zero, 0, OTHERSIZE);
	memset(all_ones, 0xFF, OTHERSIZE);

	ht_state_id = hashtable_init(&state_id_param);

	if (ht_state_id == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init State Id cache");
		return -1;
	}

	ht_state_obj = hashtable_init(&state_obj_param);

	if (ht_state_obj == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init State Entry cache");
		return -1;
	}

	return 0;
}

/**
 * @brief Build the 12 byte "other" portion of a stateid
 *
 * It is built from the ServerEpoch and a 64 bit global counter.
 *
 * @param[in] other stateid.other object (a char[OTHERSIZE] string)
 */
void nfs4_BuildStateId_Other(nfs_client_id_t *clientid, char *other)
{
	uint32_t my_stateid =
	    atomic_inc_uint32_t(&clientid->cid_stateid_counter);

	/* The first part of the other is the 64 bit clientid, which
	 * consists of the epoch in the high order 32 bits followed by
	 * the clientid counter in the low order 32 bits.
	 */
	memcpy(other, &clientid->cid_clientid, sizeof(clientid->cid_clientid));

	memcpy(other + sizeof(clientid->cid_clientid), &my_stateid,
	       sizeof(my_stateid));
}

/**
 * @brief Relinquish a reference on a state_t
 *
 * @param[in] state The state_t to release
 */
void dec_nfs4_state_ref(struct state_t *state)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	int32_t refcount;

	if (isFullDebug(COMPONENT_STATE)) {
		display_stateid(&dspbuf, state);
		str_valid = true;
	}

	refcount = atomic_dec_int32_t(&state->state_refcount);

	if (refcount > 0) {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "Decrement refcount now=%" PRId32 " {%s}",
				     refcount, str);

		return;
	}

	PTHREAD_MUTEX_destroy(&state->state_mutex);

	state->state_exp->exp_ops.free_state(state->state_exp, state);

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Deleted %s", str);
}

/**
 * @brief Set a state into the stateid hashtable.
 *
 * @param[in] other stateid4.other
 * @param[in] state The state to add
 *
 * @retval STATE_SUCCESS if able to insert the new state.
 * @retval STATE_ENTRY_EXISTS if state is already there.
 */
state_status_t nfs4_State_Set(state_t *state)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	hash_error_t err;

	buffkey.addr = state->stateid_other;
	buffkey.len = OTHERSIZE;

	buffval.addr = state;
	buffval.len = sizeof(state_t);

	err = hashtable_test_and_set(ht_state_id,
				     &buffkey,
				     &buffval,
				     HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	switch (err) {
	case HASHTABLE_SUCCESS:
		break;
	default:
		LogCrit(COMPONENT_STATE,
			"ht_state_id hashtable_test_and_set failed %s for key %p",
			hash_table_err_to_str(err), buffkey.addr);
		return STATE_ENTRY_EXISTS; /* likely reason */
	}

	/* If stateid is a LOCK or SHARE state, we also index by entry/owner */
	if (state->state_type != STATE_TYPE_LOCK &&
	    state->state_type != STATE_TYPE_SHARE)
		return STATE_SUCCESS;

	buffkey.addr = state;
	buffkey.len = sizeof(state_t);

	buffval.addr = state;
	buffval.len = sizeof(state_t);

	err = hashtable_test_and_set(ht_state_obj,
				     &buffkey,
				     &buffval,
				     HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	switch (err) {
	case HASHTABLE_SUCCESS:
		return STATE_SUCCESS;

	case HASHTABLE_ERROR_KEY_ALREADY_EXISTS: /* buggy client? */
	default: /* error case */
		LogCrit(COMPONENT_STATE,
			"ht_state_obj hashtable_test_and_set failed %s for key %p",
			hash_table_err_to_str(err), buffkey.addr);

		if (isFullDebug(COMPONENT_STATE)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = {sizeof(str), str, str};
			state_t *state2;

			display_stateid(&dspbuf, state);
			LogCrit(COMPONENT_STATE, "State %s", str);
			state2 = nfs4_State_Get_Obj(state->state_obj,
						    state->state_owner);
			if (state2 != NULL) {
				display_reset_buffer(&dspbuf);
				display_stateid(&dspbuf, state2);

				LogCrit(COMPONENT_STATE,
					"Duplicate State %s",
					str);
			}
		}

		buffkey.addr = state->stateid_other;
		buffkey.len = OTHERSIZE;
		err = HashTable_Del(ht_state_id, &buffkey, NULL, NULL);

		if (err != HASHTABLE_SUCCESS) {
			LogCrit(COMPONENT_STATE,
				 "Failure to delete stateid %s",
				 hash_table_err_to_str(err));
		}
		return STATE_ENTRY_EXISTS; /* likely reason */
	}
}

/**
 * @brief Get the state from the stateid
 *
 * @param[in]  other      stateid4.other
 *
 * @returns The found state_t or NULL if not found.
 */
struct state_t *nfs4_State_Get_Pointer(char *other)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	hash_error_t rc;
	struct hash_latch latch;
	struct state_t *state;

	buffkey.addr = other;
	buffkey.len = OTHERSIZE;

	rc = hashtable_getlatch(ht_state_id, &buffkey, &buffval, true, &latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_state_id, &latch);
		LogDebug(COMPONENT_STATE, "HashTable_Get returned %d", rc);
		return NULL;
	}

	state = buffval.addr;

	/* Take a reference under latch */
	inc_state_t_ref(state);

	/* Release latch */
	hashtable_releaselatched(ht_state_id, &latch);

	return state;
}

/**
 * @brief Get the state from the stateid by entry/owner
 *
 * @param[in]  obj	Object containing state
 * @param[in]  owner	Owner for state
 *
 * @returns The found state_t or NULL if not found.
 */
struct state_t *nfs4_State_Get_Obj(struct fsal_obj_handle *obj,
				   state_owner_t *owner)
{
	state_t state_key;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	hash_error_t rc;
	struct hash_latch latch;
	struct state_t *state;

	memset(&state_key, 0, sizeof(state_key));

	buffkey.addr = &state_key;
	buffkey.len = sizeof(state_key);

	state_key.state_owner = owner;
	state_key.state_obj = obj;

	rc = hashtable_getlatch(ht_state_obj,
				&buffkey,
				&buffval,
				true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_state_obj, &latch);
		LogDebug(COMPONENT_STATE, "HashTable_Get returned %d", rc);
		return NULL;
	}

	state = buffval.addr;

	/* Take a reference under latch */
	inc_state_t_ref(state);

	/* Release latch */
	hashtable_releaselatched(ht_state_obj, &latch);

	return state;
}

/**
 * @brief Remove a state from the stateid table
 *
 * @param[in] other stateid4.other
 *
 * @retval true if success
 * @retval false if failure
 */
bool nfs4_State_Del(state_t *state)
{
	struct gsh_buffdesc buffkey, old_key, old_value;
	struct hash_latch latch;
	hash_error_t err;

	buffkey.addr = state->stateid_other;
	buffkey.len = OTHERSIZE;

	err = HashTable_Del(ht_state_id, &buffkey, &old_key, &old_value);

	if (err == HASHTABLE_ERROR_NO_SUCH_KEY) {
		/* Already gone */
		return false;
	} else if (err != HASHTABLE_SUCCESS) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_stateid(&dspbuf, state);

		LogDebug(COMPONENT_STATE,
			 "Failure to delete stateid %s %s",
			 str,
			 hash_table_err_to_str(err));
		return false;
	}

	assert(state == old_value.addr);

	/* If stateid is a LOCK or SHARE state, we had also indexed by
	 * entry/owner
	 */
	if (state->state_type != STATE_TYPE_LOCK &&
	    state->state_type != STATE_TYPE_SHARE)
		return true;

	/* Delete the stateid hashed by entry/owner.
	 * Use the old_value from above as the key.
	 */
	buffkey.addr = old_value.addr;
	buffkey.len = old_value.len;

	/* Get latch: we need to check we're deleting the right state */
	err = hashtable_getlatch(ht_state_obj, &buffkey, &old_value, true,
				 &latch);
	if (err != HASHTABLE_SUCCESS) {
		if (err == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_state_obj, &latch);

		LogCrit(COMPONENT_STATE, "hashtable get latch failed: %d",
			err);
		return false;
	}

	if (old_value.addr != state) {
		/* state obj had already been swapped out */
		hashtable_releaselatched(ht_state_obj, &latch);
		return false;
	}

	hashtable_deletelatched(ht_state_obj, &buffkey, &latch, NULL, NULL);
	hashtable_releaselatched(ht_state_obj, &latch);
	return true;
}

/**
 * @brief Check and look up the supplied stateid
 *
 * This function yields the state for the stateid if it is valid.
 *
 * @param[in]  stateid     Stateid to look up
 * @param[in]  fsal_obj    Associated file (if any)
 * @param[out] state       Found state
 * @param[in]  data        Compound data
 * @param[in]  flags       Flags governing special stateids
 * @param[in]  owner_seqid seqid on v4.0 owner
 * @param[in]  check_seqid Whether to validate owner_seqid
 * @param[in]  tag     Arbitrary string for logging/debugging
 *
 * @return NFSv4 status codes
 */
nfsstat4 nfs4_Check_Stateid(stateid4 *stateid, struct fsal_obj_handle *fsal_obj,
			    state_t **state, compound_data_t *data, int flags,
			    seqid4 owner_seqid, bool check_seqid,
			    const char *tag)
{
	uint32_t epoch = 0;
	uint64_t epoch_low = nfs_ServerEpoch & 0xFFFFFFFF;
	state_t *state2 = NULL;
	struct fsal_obj_handle *obj2 = NULL;
	state_owner_t *owner2 = NULL;
	char str[DISPLAY_STATEID4_SIZE] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	int32_t diff;
	clientid4 clientid;
	nfs_client_id_t *pclientid;
	int rc;
	nfsstat4 status;

	if (isDebug(COMPONENT_STATE)) {
		display_stateid4(&dspbuf, stateid);
		str_valid = true;
	}

	LogFullDebug(COMPONENT_STATE, "Check %s stateid flags%s%s%s%s%s%s",
		     tag, flags & STATEID_SPECIAL_ALL_0 ? " ALL_0" : "",
		     flags & STATEID_SPECIAL_ALL_1 ? " ALL_1" : "",
		     flags & STATEID_SPECIAL_CURRENT ? " CURRENT" : "",
		     flags & STATEID_SPECIAL_CLOSE_40 ? " CLOSE_40" : "",
		     flags & STATEID_SPECIAL_CLOSE_41 ? " CLOSE_41" : "",
		     flags == 0 ? " NONE" : "");

	/* Test for OTHER is all zeros */
	if (memcmp(stateid->other, all_zero, OTHERSIZE) == 0) {
		if (stateid->seqid == 0 &&
		    (flags & STATEID_SPECIAL_ALL_0) != 0) {
			/* All 0 stateid */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found special all 0 stateid",
				 tag);

			/** @todo FSF: eventually this may want to return an
			 * actual state for use in temporary locks for I/O.
			 */
			data->current_stateid_valid = false;
			goto success;
		}
		if (stateid->seqid == 1
		    && (flags & STATEID_SPECIAL_CURRENT) != 0) {
			/* Special current stateid */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found special 'current' stateid",
				 tag);
			if (!data->current_stateid_valid) {
				LogDebug(COMPONENT_STATE,
					 "Check %s stateid STATEID_SPECIAL_CURRENT - current stateid is bad",
					 tag);
				status = NFS4ERR_BAD_STATEID;
				goto failure;
			}

			/* Copy current stateid in and proceed to checks */
			*stateid = data->current_stateid;
			goto check_it;
		}

		LogDebug(COMPONENT_STATE,
			 "Check %s stateid with OTHER all zeros, seqid %u unexpected",
			 tag, (unsigned int)stateid->seqid);
		status = NFS4ERR_BAD_STATEID;
		goto failure;
	}

	/* Test for OTHER is all ones */
	if (memcmp(stateid->other, all_ones, OTHERSIZE) == 0) {
		/* Test for special all ones stateid */
		if (stateid->seqid == seqid_all_one &&
		    (flags & STATEID_SPECIAL_ALL_1) != 0) {
			/* All 1 stateid */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found special all 1 stateid",
				 tag);

			/** @todo FSF: eventually this may want to return an
			 * actual state for use in temporary locks for I/O.
			 */
			data->current_stateid_valid = false;
			goto success;
		}

		LogDebug(COMPONENT_STATE,
			 "Check %s stateid with OTHER all ones, seqid %u unexpected",
			 tag, (unsigned int)stateid->seqid);
		status = NFS4ERR_BAD_STATEID;
		goto failure;
	}

 check_it:

	/* Extract the clientid from the stateid other field */
	memcpy(&clientid, stateid->other, sizeof(clientid));

	/* Extract the epoch from the clientid */
	epoch = clientid >> (clientid4) 32;

	/* Check if stateid was made from this server instance */
	if (epoch != epoch_low) {
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found stale stateid %s",
				 tag, str);
		status = NFS4ERR_STALE_STATEID;
		goto failure;
	}

	/* Try to get the related state */
	state2 = nfs4_State_Get_Pointer(stateid->other);

	/* We also need a reference to the state_obj and state_owner.
	 * If we can't get them, we will check below for lease invalidity.
	 * Note that calling get_state_obj_export_owner_refs with a NULL
	 * state2 returns false.
	 */
	if (!get_state_obj_export_owner_refs(state2,
					       &obj2,
					       NULL,
					       &owner2)) {
		/* We matched this server's epoch, but could not find the
		 * stateid. Chances are, the client was expired and the state
		 * has all been freed.
		 *
		 * We could use another check here for a BAD stateid
		 */
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid could not find %s",
				 tag, str);

		/* Try and find the clientid */
		rc = nfs_client_id_get_confirmed(clientid, &pclientid);

		if (rc != CLIENT_ID_SUCCESS) {
			/* Unknown client id (or other problem),
			 * return that result.
			 */
			status = clientid_error_to_nfsstat(rc);
			goto failure;
		}

		if ((flags & (STATEID_SPECIAL_CLOSE_40 |
			      STATEID_SPECIAL_CLOSE_41)) != 0) {
			/* This is a close with a valid clientid, but invalid
			 * stateid counter. We will assume this is a replayed
			 * close.
			 */
			if (data->preserved_clientid != NULL) {
				/* We don't expect this, but, just in case...
				 * Update and release already reserved lease.
				 */
				PTHREAD_MUTEX_lock(&data->preserved_clientid
						   ->cid_mutex);
				update_lease(data->preserved_clientid);
				PTHREAD_MUTEX_unlock(&data->preserved_clientid
						     ->cid_mutex);
				data->preserved_clientid = NULL;
			}

			/* Check if lease is expired and reserve it */
			PTHREAD_MUTEX_lock(&pclientid->cid_mutex);

			if (!reserve_lease(pclientid)) {
				LogDebug(COMPONENT_STATE,
					 "Returning NFS4ERR_EXPIRED");
				PTHREAD_MUTEX_unlock(&pclientid->cid_mutex);
				status = NFS4ERR_EXPIRED;
				goto failure;
			}

			if ((flags & STATEID_SPECIAL_CLOSE_40) != 0) {
				/* Just update the lease and leave the reserved
				 * clientid NULL.
				 */
				update_lease(pclientid);
			} else {
				/* Remember the reserved clientid for the rest
				 * of the compound.
				 */
				data->preserved_clientid = pclientid;
			}
			PTHREAD_MUTEX_unlock(&pclientid->cid_mutex);

			/* Replayed close, it's ok, but stateid doesn't exist */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid is a replayed close", tag);
			data->current_stateid_valid = false;
			goto success;
		}

		if (state2 == NULL)
			status = NFS4ERR_BAD_STATEID;
		else {
			/* We had a valid stateid, but the entry was stale.
			 * Check if lease is expired and reserve it so we
			 * can distinguish betwen the state_t being in the
			 * midst of tear down due to expired lease or if
			 * in fact the entry is actually stale.
			 */
			PTHREAD_MUTEX_lock(&pclientid->cid_mutex);

			if (!reserve_lease(pclientid)) {
				LogDebug(COMPONENT_STATE,
					 "Returning NFS4ERR_EXPIRED");
				PTHREAD_MUTEX_unlock(&pclientid->cid_mutex);

				/* Release the clientid reference we just
				 * acquired.
				 */
				dec_client_id_ref(pclientid);
				status = NFS4ERR_EXPIRED;
				goto failure;
			}

			/* Just update the lease and leave the reserved
			 * clientid NULL.
			 */
			update_lease(pclientid);
			PTHREAD_MUTEX_unlock(&pclientid->cid_mutex);

			/* The lease was valid, so this must be a stale
			 * entry.
			 */
			status = NFS4ERR_STALE;
		}

		/* Release the clientid reference we just acquired. */
		dec_client_id_ref(pclientid);
		goto failure;
	}

	/* Now, if this lease is not already reserved, reserve it */
	if (data->preserved_clientid !=
	    owner2->so_owner.so_nfs4_owner.so_clientrec) {
		if (data->preserved_clientid != NULL) {
			/* We don't expect this to happen, but, just in case...
			 * Update and release already reserved lease.
			 */
			PTHREAD_MUTEX_lock(&data->preserved_clientid
					   ->cid_mutex);

			update_lease(data->preserved_clientid);

			PTHREAD_MUTEX_unlock(&data->preserved_clientid
					     ->cid_mutex);

			data->preserved_clientid = NULL;
		}

		/* Check if lease is expired and reserve it */
		PTHREAD_MUTEX_lock(
		    &owner2->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);

		if (!reserve_lease(
				owner2->so_owner.so_nfs4_owner.so_clientrec)) {
			LogDebug(COMPONENT_STATE, "Returning NFS4ERR_EXPIRED");

			PTHREAD_MUTEX_unlock(&owner2->so_owner.so_nfs4_owner
					     .so_clientrec->cid_mutex);

			status = NFS4ERR_EXPIRED;
			goto failure;
		}

		data->preserved_clientid =
		    owner2->so_owner.so_nfs4_owner.so_clientrec;

		PTHREAD_MUTEX_unlock(
		    &owner2->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);
	}

	/* Sanity check : Is this the right file ? */
	if (fsal_obj && !fsal_obj->obj_ops->handle_cmp(fsal_obj, obj2)) {
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found stateid %s has wrong file",
				 tag, str);
		status = NFS4ERR_BAD_STATEID;
		goto failure;
	}

	/* Whether stateid.seqid may be zero depends on the state type
	   exclusively, See RFC 5661 pp. 161,287-288. */
	if ((state2->state_type == STATE_TYPE_LAYOUT) ||
	    (stateid->seqid != 0)) {
		/* Check seqid in stateid */

		/**
		 * @todo fsf: maybe change to simple comparison:
		 *            stateid->seqid < state2->state_seqid
		 *            as good enough and maybe makes pynfs happy.
		 */
		diff = stateid->seqid - state2->state_seqid;
		if (diff < 0) {
			/* if this is NFSv4.0 and stateid's seqid is one less
			 * than current AND if owner_seqid is current
			 * pass state back to allow replay check
			 */
			if ((check_seqid)
			    && ((diff == -1)
				|| ((state2->state_seqid == 1)
				    && (stateid->seqid == seqid_all_one)))
			    && (owner_seqid ==
				owner2->so_owner.so_nfs4_owner.so_seqid)) {
				LogDebug(COMPONENT_STATE, "possible replay?");
				*state = state2;
				status = NFS4ERR_REPLAY;
				goto replay;
			}
			/* OLD_STATEID */
			if (str_valid)
				LogDebug(COMPONENT_STATE,
					 "Check %s stateid found OLD stateid %s, expected seqid %"
					 PRIu32,
					 tag, str,
					 state2->state_seqid);
			status = NFS4ERR_OLD_STATEID;
			goto failure;
		}

		/* stateid seqid is current and owner seqid is previous,
		 * replay (should be an error condition that did not change
		 * the stateid, no real need to check since the operation
		 * must be the same)
		 */
		else if ((diff == 0) && (check_seqid)
			 && (owner_seqid ==
			     owner2->so_owner.so_nfs4_owner.so_seqid)) {
			LogDebug(COMPONENT_STATE, "possible replay?");
			*state = state2;
			status = NFS4ERR_REPLAY;
			goto replay;
		} else if (diff > 0) {
			/* BAD_STATEID */
			if (str_valid)
				LogDebug(COMPONENT_STATE,
					 "Check %s stateid found BAD stateid %s, expected seqid %"
					 PRIu32,
					 tag, str,
					 state2->state_seqid);
			status = NFS4ERR_BAD_STATEID;
			goto failure;
		}
	}

	data->current_stateid_valid = true;

	if (str_valid)
		LogFullDebug(COMPONENT_STATE,
			     "Check %s stateid found valid stateid %s - %p",
			     tag, str, state2);

	/* Copy stateid into current for later use */
	data->current_stateid = *stateid;
	data->current_stateid.seqid = state2->state_seqid;

 success:

	if (obj2 != NULL) {
		obj2->obj_ops->put_ref(obj2);
		dec_state_owner_ref(owner2);
	}

	*state = state2;
	return NFS4_OK;

 failure:

	if (state2 != NULL)
		dec_state_t_ref(state2);

	*state = NULL;

 replay:

	if (obj2 != NULL) {
		obj2->obj_ops->put_ref(obj2);
		dec_state_owner_ref(owner2);
	}

	data->current_stateid_valid = false;
	return status;
}

/**
 * @brief Display the stateid table
 */

void nfs_State_PrintAll(void)
{
	if (isFullDebug(COMPONENT_STATE))
		hashtable_log(COMPONENT_STATE, ht_state_id);
}

/**
 * @brief Update stateid and set current
 *
 * We increment the seqid, handling wraparound, and copy the id into
 * the response.
 *
 * @param[in,out] state The state to update
 * @param[out]    resp  Stateid in response
 * @param[in,out] data  Compound data to upddate with current stateid
 *                      (may be NULL)
 * @param[in]     tag   Arbitrary text for debug/log
 */
void update_stateid(state_t *state, stateid4 *resp, compound_data_t *data,
		    const char *tag)
{
	/* Increment state_seqid, handling wraparound */
	state->state_seqid += 1;
	if (state->state_seqid == 0)
		state->state_seqid = 1;

	/* Copy stateid into current for later use */
	if (data) {
		COPY_STATEID(&data->current_stateid, state);
		data->current_stateid_valid = true;
	}

	/* Copy stateid into response */
	COPY_STATEID(resp, state);

	if (isFullDebug(COMPONENT_STATE)) {
		char str[DISPLAY_STATEID4_SIZE] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_stateid4(&dspbuf, resp);

		LogDebug(COMPONENT_STATE,
			 "Update %s stateid to %s for response", tag, str);
	}
}

/** @} */
