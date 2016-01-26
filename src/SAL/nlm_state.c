/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright Panasas Inc  (2015)
 * contributor: Frank S Filz	ffilzlnx@mindspring.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
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
  * @file nlm_state.c
  * @brief The management of the NLM state caches.
  */

#include "config.h"
#include <string.h>
#include <ctype.h>
#include <netdb.h>

#include "sal_functions.h"
#include "nsm.h"
#include "log.h"
#include "fsal.h"

/**
 * @brief NLM States
 */
hash_table_t *ht_nlm_states;

/*******************************************************************************
 *
 * NLM State Routines
 *
 ******************************************************************************/

/**
 * @brief Display an NLM State
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     key    The NLM State
 *
 * @return the bytes remaining in the buffer.
 */
int display_nlm_state(struct display_buffer *dspbuf, state_t *key)
{
	int b_left;

	if (key == NULL)
		return display_printf(dspbuf, "NLM State <NULL>");

	b_left = display_printf(dspbuf, "NLM State %p: ", key);

	if (b_left <= 0)
		return b_left;

	return b_left;
}

/**
 * @brief Display an NLM State in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_state_key(struct gsh_buffdesc *buff, char *str)
{
	struct display_buffer dspbuf = {HASHTABLE_DISPLAY_STRLEN, str, str};

	display_nlm_state(&dspbuf, buff->addr);
	return display_buffer_len(&dspbuf);
}

/**
 * @brief Display an NLM State in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_state_val(struct gsh_buffdesc *buff, char *str)
{
	struct display_buffer dspbuf = {HASHTABLE_DISPLAY_STRLEN, str, str};

	display_nlm_state(&dspbuf, buff->addr);
	return display_buffer_len(&dspbuf);
}

/**
 * @brief Compare NLM States
 *
 * @param[in] state1 An NLM State
 * @param[in] state2 Another NLM State
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_state(state_t *state1, state_t *state2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[LOG_BUFF_LEN / 2];
		char str2[LOG_BUFF_LEN / 2];
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_nlm_state(&dspbuf1, state1);
		display_nlm_state(&dspbuf2, state2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (state1 == NULL || state2 == NULL)
		return 1;

	if (state1 == state2)
		return 0;

	return state1->state_type != state2->state_type ||
	       state1->state_owner != state2->state_owner ||
	       state1->state_entry != state2->state_entry ||
	       state1->state_export != state2->state_export;
}

/**
 * @brief Compare NLM States in the hash table
 *
 * @param[in] buff1 A key
 * @param[in] buff2 Another key
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_state_key(struct gsh_buffdesc *buff1,
			  struct gsh_buffdesc *buff2)
{
	return compare_nlm_state(buff1->addr, buff2->addr);

}

/**
 * @brief Calculate hash index for an NSM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t nlm_state_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	state_t *pkey = key->addr;
	uint32_t res = (long int) pkey->state_entry ^
		       (long int) pkey->state_owner;

	if (pkey->state_type == STATE_TYPE_NLM_SHARE)
		res = ~res;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %"PRIx32,
			     res % hparam->index_size);

	return res % hparam->index_size;
}

/**
 * @brief Calculate RBT hash for an NSM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t nlm_state_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key)
{
	state_t *pkey = key->addr;
	uint64_t res = (long int) pkey->state_entry ^
		       (long int) pkey->state_owner;

	if (pkey->state_type == STATE_TYPE_NLM_SHARE)
		res = ~res;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %"PRIx64,
			     res);

	return res;
}

static hash_parameter_t nlm_state_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nlm_state_value_hash_func,
	.hash_func_rbt = nlm_state_rbt_hash_func,
	.compare_key = compare_nlm_state_key,
	.key_to_str = display_nlm_state_key,
	.val_to_str = display_nlm_state_val,
	.flags = HT_FLAG_NONE,
};

/**
 * @brief Init the hashtables for NLM state support
 *
 * @return 0 if successful, -1 otherwise
 */
int Init_nlm_state_hash(void)
{
	ht_nlm_states = hashtable_init(&nlm_state_hash_param);

	if (ht_nlm_states == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NLM States cache");
		return -1;
	}

	return 0;
}

/**
 * @brief Relinquish a reference on an NLM State
 *
 * @param[in] state The NLM State to release
 */
void dec_nlm_state_ref(state_t *state)
{
	char str[LOG_BUFF_LEN];
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;
	int32_t refcount;

	if (isDebug(COMPONENT_STATE)) {
		display_nlm_state(&dspbuf, state);
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

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Try to remove {%s}", str);

	buffkey.addr = state;
	buffkey.len = sizeof(*state);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(ht_nlm_states, &buffkey, &old_value, true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_nlm_states, &latch);

		if (!str_valid)
			display_nlm_state(&dspbuf, state);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	refcount = atomic_fetch_int32_t(&state->state_refcount);

	if (refcount > 0) {
		if (str_valid)
			LogDebug(COMPONENT_STATE,
				 "Did not release refcount now=%"PRId32" {%s}",
				 refcount, str);

		hashtable_releaselatched(ht_nlm_states, &latch);

		return;
	}

	/* use the key to delete the entry */
	hashtable_deletelatched(ht_nlm_states, &buffkey, &latch, &old_key,
				&old_value);

	/* Release the latch */
	hashtable_releaselatched(ht_nlm_states, &latch);

	LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	dec_state_owner_ref(state->state_owner);

	put_gsh_export(state->state_export);

	cache_inode_lru_unref(state->state_entry, LRU_FLAG_NONE);

	gsh_free(state);
}

/**
 * @brief Get an NLM State
 *
 * @param[in] state_type        type of state (LOCK or SHARE)
 * @param[in] state_entry       cache inode state applies to
 * @param[in] state_owner       NLM owner of the state
 * @param[in] nsm_state_applies True if nsm_state is available
 * @param[in] nsm_state         NSM state value for locks
 * @param[out] pstate           Pointer to return the found state in
 *
 * @return NLM Status code or 0 if no special return
 */
int get_nlm_state(enum state_type state_type,
		  cache_entry_t *state_entry,
		  state_owner_t *state_owner,
		  bool nsm_state_applies,
		  uint32_t nsm_state,
		  state_t **pstate)
{
	state_t key;
	state_t *state;
	char str[LOG_BUFF_LEN];
	struct display_buffer dspbuf = {sizeof(str), str, str};
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey, old_key;
	struct gsh_buffdesc buffval, old_value;

	memset(&key, 0, sizeof(key));

	key.state_type = state_type;
	key.state_entry = state_entry;
	key.state_owner = state_owner;
	key.state_export = op_ctx->export;
	key.state_seqid = nsm_state;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_state(&dspbuf, &key);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nlm_states, &buffkey, &buffval, true,
				&latch);

	/* If we found it, return it */
	if (rc == HASHTABLE_SUCCESS) {
		state = buffval.addr;

		if (nsm_state_applies && state->state_seqid != nsm_state) {
			/* We are getting new locks before the old ones are
			 * gone. We need to unhash this state_t and create a
			 * new one.
			 *
			 * Keep the latch after the delete to proceed with
			 * the new insert.
			 */

			/* use the key to delete the entry */
			hashtable_deletelatched(ht_nlm_states,
						&buffkey,
						&latch,
						&old_key,
						&old_value);
			goto new_state;
		}

		/* Return the found NLM State */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_state(&dspbuf, state);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		/* Increment refcount under hash latch.
		 * This prevents dec ref from removing this entry from hash
		 * if a race occurs.
		 */
		inc_state_t_ref(state);

		hashtable_releaselatched(ht_nlm_states, &latch);

		*pstate = state;
		return 0;
	}

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		display_nlm_state(&dspbuf, &key);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		*pstate = NULL;

		return NLM4_DENIED_NOLOCKS;
	}

 new_state:

	/* If the nsm state doesn't apply, we don't want to create a new
	 * state_t if one didn't exist already.
	 */
	if (!nsm_state_applies) {
		hashtable_releaselatched(ht_nlm_states, &latch);
		return 0;
	}

	state = gsh_malloc(sizeof(*state));

	if (state == NULL) {
		display_nlm_state(&dspbuf, &key);
		LogCrit(COMPONENT_STATE, "No memory for {%s}", str);

		*pstate = NULL;

		hashtable_releaselatched(ht_nlm_states, &latch);

		return NLM4_DENIED_NOLOCKS;
	}

	/* Copy everything over */
	memcpy(state, &key, sizeof(key));

	assert(pthread_mutex_init(&state->state_mutex, NULL) == 0);

	if (state_type == STATE_TYPE_NLM_LOCK)
		glist_init(&state->state_data.lock.state_locklist);

	state->state_refcount = 1;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_state(&dspbuf, state);
		LogFullDebug(COMPONENT_STATE, "New {%s}", str);
	}

	buffkey.addr = state;
	buffkey.len = sizeof(*state);
	buffval.addr = state;
	buffval.len = sizeof(*state);

	if (cache_inode_lru_ref(state->state_entry, LRU_FLAG_NONE) !=
	    CACHE_INODE_SUCCESS) {
		hashtable_releaselatched(ht_nlm_states, &latch);
		*pstate = NULL;
		gsh_free(state);
		return NLM4_STALE_FH;
	}

	rc = hashtable_setlatched(ht_nlm_states, &buffval, &buffval, &latch,
				  false, NULL, NULL);

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_SUCCESS) {
		display_nlm_state(&dspbuf, state);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		assert(pthread_mutex_destroy(&state->state_mutex) == 0);

		/* Free the LRU ref taken above and the state. */
		cache_inode_lru_unref(state->state_entry, LRU_FLAG_NONE);

		gsh_free(state);

		*pstate = NULL;

		return NLM4_DENIED_NOLOCKS;
	}

	get_gsh_export_ref(state->state_export);

	inc_state_owner_ref(state->state_owner);

	*pstate = state;

	return 0;
}
