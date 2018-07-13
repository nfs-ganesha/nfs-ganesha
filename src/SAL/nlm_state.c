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

#include "city.h"
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
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
	       state1->state_export != state2->state_export ||
	       state1->state_obj != state2->state_obj;
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
	uint64_t hk;
	char *addr = (char *)&pkey->state_owner;

	/* We hash based on the owner pointer, and the object key.  This depends
	 * on them being sequential in memory. */
	hk = CityHash64WithSeed(addr, sizeof(pkey->state_owner)
				+ sizeof(pkey->state_obj), 557);

	if (pkey->state_type == STATE_TYPE_NLM_SHARE)
		hk = ~hk;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %"PRIx32,
			     (uint32_t)(hk % hparam->index_size));

	return hk % hparam->index_size;
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
	uint64_t hk;
	char *addr = (char *)&pkey->state_owner;

	/* We hash based on the owner pointer, and the object key.  This depends
	 * on them being sequential in memory. */
	hk = CityHash64WithSeed(addr, sizeof(pkey->state_owner) +
				sizeof(pkey->state_obj), 557);

	if (pkey->state_type == STATE_TYPE_NLM_SHARE)
		hk = ~hk;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %"PRIx64,
			     hk % hparam->index_size);

	return hk % hparam->index_size;
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
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	int32_t refcount;
	struct fsal_obj_handle *obj;

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

	/* Another thread that needs this entry might have deleted this
	 * nlm state to insert its own nlm state. So expect not to find
	 * this nlm state or find someone else's nlm state!
	 */
	switch (rc) {
	case HASHTABLE_SUCCESS:
		if (old_value.addr == state) { /* our own state */
			hashtable_deletelatched(ht_nlm_states, &buffkey,
						&latch, NULL, NULL);
		}
		break;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	default:
		if (!str_valid)
			display_nlm_state(&dspbuf, state);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	/* Release the latch */
	hashtable_releaselatched(ht_nlm_states, &latch);

	LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	dec_state_owner_ref(state->state_owner);

	put_gsh_export(state->state_export);

	obj = get_state_obj_ref(state);

	if (obj == NULL) {
		LogDebug(COMPONENT_STATE,
			 "Entry for state is stale");
		return;
	}

	/* We need to close the state before freeing the state. */
	(void) obj->obj_ops->close2(obj, state);

	state->state_exp->exp_ops.free_state(state->state_exp, state);

	/* Release 2 refs: our sentinal one, plus the one from
	 * get_state_obj_ref() */
	obj->obj_ops->put_ref(obj);
	obj->obj_ops->put_ref(obj);
}

/**
 * @brief Get an NLM State
 *
 * @param[in] state_type        type of state (LOCK or SHARE)
 * @param[in] state_obj         FSAL obj state applies to
 * @param[in] state_owner       NLM owner of the state
 * @param[in] care              Indicates to what degree the caller cares about
 *                              actually getting a state.
 * @param[in] nsm_state         NSM state value for locks, only valid when
 *                              care == CARE_MONITOR
 * @param[out] pstate           Pointer to return the found state in
 *
 * @return NLM Status code or 0 if no special return
 */
int get_nlm_state(enum state_type state_type,
		  struct fsal_obj_handle *state_obj,
		  state_owner_t *state_owner,
		  care_t care,
		  uint32_t nsm_state,
		  state_t **pstate)
{
	state_t key;
	state_t *state;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;

	*pstate = NULL;
	memset(&key, 0, sizeof(key));

	key.state_type = state_type;
	key.state_owner = state_owner;
	key.state_export = op_ctx->ctx_export;
	key.state_seqid = nsm_state;
	key.state_obj = state_obj;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_state(&dspbuf, &key);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nlm_states, &buffkey, &buffval, true,
				&latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		state = buffval.addr;
		if (care == CARE_MONITOR && state->state_seqid != nsm_state) {
			/* We are getting new locks before the old ones
			 * are gone. We need to unhash this state_t and
			 * create a new one.
			 *
			 * Keep the latch after the delete to proceed with
			 * the new insert.
			 */
			hashtable_deletelatched(ht_nlm_states, &buffkey,
						&latch, NULL, NULL);
			break;
		}

		if (atomic_inc_int32_t(&state->state_refcount) == 1) {
			/* The state is in the process of getting
			 * deleted. Delete from the hashtable and
			 * pretend as though we didn't find it.
			 */
			(void)atomic_dec_int32_t(&state->state_refcount);
			hashtable_deletelatched(ht_nlm_states, &buffkey,
						&latch, NULL, NULL);
			break;
		}

		/* Return the found NLM State */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_state(&dspbuf, state);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		hashtable_releaselatched(ht_nlm_states, &latch);

		*pstate = state;
		return 0;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	default: /* An error occurred, return NULL */
		display_nlm_state(&dspbuf, &key);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		*pstate = NULL;

		return NLM4_DENIED_NOLOCKS;
	}

	/* If we don't care at all, or only care about owner, we don't want to
	 * create a new state.
	 */
	if (care == CARE_NOT || care == CARE_OWNER) {
		hashtable_releaselatched(ht_nlm_states, &latch);
		return 0;
	}

	state = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							 state_type,
							 NULL);

	/* Copy everything over */
	state->state_obj = state_obj;
	state->state_owner = state_owner;
	state->state_export = op_ctx->ctx_export;
	state->state_seqid = nsm_state;

	PTHREAD_MUTEX_init(&state->state_mutex, NULL);

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

	state_obj->obj_ops->get_ref(state_obj);

	rc = hashtable_setlatched(ht_nlm_states, &buffval, &buffval, &latch,
				  false, NULL, NULL);

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_SUCCESS) {
		display_nlm_state(&dspbuf, state);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		PTHREAD_MUTEX_destroy(&state->state_mutex);

		/* Free the ref taken above and the state.
		 * No need to close here, the state was never opened.
		 */
		state->state_exp->exp_ops.free_state(state->state_exp, state);
		state_obj->obj_ops->put_ref(state_obj);

		*pstate = NULL;

		return NLM4_DENIED_NOLOCKS;
	}

	get_gsh_export_ref(state->state_export);

	inc_state_owner_ref(state->state_owner);

	*pstate = state;

	return 0;
}
