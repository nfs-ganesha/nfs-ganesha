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
#include "ganesha_rpc.h"
#include "hashtable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief Hash table for stateids.
 */
hash_table_t *ht_state_id;

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
 * @param[in]  other The other
 * @param[out] str   Output buffer
 *
 * @return Length of output string.
 */
int display_stateid_other(char *other, char *str)
{
	uint32_t epoch = *((uint32_t *) other);
	uint64_t count = *((uint64_t *) (other + sizeof(uint32_t)));
	return sprintf(str, "epoch=0x%08x counter=0x%016llx",
		       (unsigned int)epoch, (unsigned long long)count);
}

/**
 * @brief Display a stateid other in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_state_id_key(struct gsh_buffdesc *buff, char *str)
{
	return display_stateid_other(buff->addr, str);
}

/**
 * @brief Display a state in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_state_id_val(struct gsh_buffdesc *buff, char *str)
{
	state_t *state = buff->addr;

	return sprintf(str,
		       "state %p is associated with entry=%p type=%u seqid=%u",
		       state, state->state_entry, state->state_type,
		       state->state_seqid);
}

/**
 * @brief Compare to stateids
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
		char str1[OTHERSIZE * 2 + 32], str2[OTHERSIZE * 2 + 32];

		display_stateid_other(buff1->addr, str1);
		display_stateid_other(buff2->addr, str2);

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
inline uint32_t compute_stateid_hash_value(uint32_t *stateid)
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
	.key_to_str = display_state_id_key,
	.val_to_str = display_state_id_val,
	.flags = HT_FLAG_CACHE,
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
 * @brief Set a state into the stateid hashtable.
 *
 * @param[in] other stateid4.other
 * @param[in] state The state to add
 *
 * @retval 1 if ok.
 * @retval 0 if not ok.
 */
int nfs4_State_Set(char other[OTHERSIZE], state_t *state)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;

	buffkey.addr = gsh_malloc(OTHERSIZE);

	if (buffkey.addr == NULL)
		return 0;

	LogFullDebug(COMPONENT_STATE, "Allocating stateid key %p",
		     buffkey.addr);

	memcpy(buffkey.addr, other, OTHERSIZE);
	buffkey.len = OTHERSIZE;

	buffval.addr = state;
	buffval.len = sizeof(state_t);

	if (hashtable_test_and_set
	    (ht_state_id, &buffkey, &buffval,
	     HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS) {
		LogCrit(COMPONENT_STATE,
			"hashtable_test_and_set failed for key %p",
			buffkey.addr);
		gsh_free(buffkey.addr);
		return 0;
	}

	return 1;
}

/**
 * @brief Get the state from the stateid
 *
 * @param[in]  other      stateid4.other
 * @param[out] state_data State found
 *
 * @retval 1 if ok.
 * @retval 0 if not ok.
 */
int nfs4_State_Get_Pointer(char other[OTHERSIZE], state_t **state_data)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	int rc;

	buffkey.addr = (caddr_t) other;
	buffkey.len = OTHERSIZE;

	rc = HashTable_Get(ht_state_id, &buffkey, &buffval);
	if (rc != HASHTABLE_SUCCESS) {
		LogDebug(COMPONENT_STATE, "HashTable_Get returned %d", rc);
		return 0;
	}

	*state_data = buffval.addr;

	return 1;
}

/**
 * @brief Remove a state from the stateid table
 *
 * @param[in] other stateid4.other
 *
 * This really can't fail.
 */
void nfs4_State_Del(char other[OTHERSIZE])
{
	struct gsh_buffdesc buffkey, old_key, old_value;
	hash_error_t err;

	buffkey.addr = other;
	buffkey.len = OTHERSIZE;

	err = HashTable_Del(ht_state_id, &buffkey, &old_key, &old_value);

	if (err == HASHTABLE_SUCCESS) {
		/* free the key that was stored in hash table */
		LogFullDebug(COMPONENT_STATE, "Freeing stateid key %p",
			     old_key.addr);
		gsh_free(old_key.addr);

		/* State is managed in stuff alloc, no free is needed for
		 * old_value.addr
		 */
	} else {
		LogCrit(COMPONENT_STATE,
			"Failure to delete state %s",
			hash_table_err_to_str(err));
	}
}

/**
 * @brief Check and look up the supplied stateid
 *
 * This function yields the state for the stateid if it is valid.
 *
 * @param[in]  stateid     Stateid to look up
 * @param[in]  entry       Associated file (if any)
 * @param[out] state       Found state
 * @param[in]  data        Compound data
 * @param[in]  flags       Flags governing special stateids
 * @param[in]  owner_seqid seqid on v4.0 owner
 * @param[in]  check_seqid Whether to validate owner_seqid
 * @param[in]  tag     Arbitrary string for logging/debugging
 *
 * @return NFSv4 status codes
 */
nfsstat4 nfs4_Check_Stateid(stateid4 *stateid, cache_entry_t *entry,
			    state_t **state, compound_data_t *data, int flags,
			    seqid4 owner_seqid, bool check_seqid,
			    const char *tag)
{
	uint32_t epoch = 0;
	uint64_t epoch_low = ServerEpoch & 0xFFFFFFFF;
	state_t *state2 = NULL;

	/* string str has to accomodate stateid->other(OTHERSIZE * 2 ),
	 * stateid->seqid(max 10 bytes),
	 * a colon (:) and a terminating null character.
	 */
	char str[OTHERSIZE * 2 + 10 + 2];
	int32_t diff;
	clientid4 clientid;
	nfs_client_id_t *pclientid;
	int rc;
	nfsstat4 status;

	if (isDebug(COMPONENT_STATE)) {
		sprint_mem(str, (char *)stateid->other, OTHERSIZE);
		sprintf(str + OTHERSIZE * 2, ":%u",
			(unsigned int)stateid->seqid);
	}

	LogFullDebug(COMPONENT_STATE, "Check %s stateid flags%s%s%s%s%s%s%s",
		     tag, flags & STATEID_SPECIAL_ALL_0 ? " ALL_0" : "",
		     flags & STATEID_SPECIAL_ALL_1 ? " ALL_1" : "",
		     flags & STATEID_SPECIAL_CURRENT ? " CURRENT" : "",
		     flags & STATEID_SPECIAL_CLOSE_40 ? " CLOSE_40" : "",
		     flags & STATEID_SPECIAL_CLOSE_41 ? " CLOSE_41" : "",
		     flags & STATEID_SPECIAL_FREE ? " FREE" : "",
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
		LogDebug(COMPONENT_STATE,
			 "Check %s stateid found stale stateid %s", tag, str);
		status = NFS4ERR_STALE_STATEID;
		goto failure;
	}

	/* Try to get the related state */
	if (!nfs4_State_Get_Pointer(stateid->other, &state2)) {
		/* We matched this server's epoch, but could not find the
		 * stateid. Chances are, the client was expired and the state
		 * has all been freed.
		 *
		 * We could use another check here for a BAD stateid
		 */
		LogDebug(COMPONENT_STATE,
			 "Check %s stateid could not find state %s", tag, str);

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
				pthread_mutex_lock(&data->preserved_clientid
						   ->cid_mutex);
				update_lease(data->preserved_clientid);
				pthread_mutex_unlock(&data->preserved_clientid
						     ->cid_mutex);
				data->preserved_clientid = NULL;
			}

			/* Check if lease is expired and reserve it */
			pthread_mutex_lock(&pclientid->cid_mutex);

			if (!reserve_lease(pclientid)) {
				LogDebug(COMPONENT_STATE,
					 "Returning NFS4ERR_EXPIRED");
				pthread_mutex_unlock(&pclientid->cid_mutex);
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
			pthread_mutex_unlock(&pclientid->cid_mutex);

			/* Replayed close, it's ok, but stateid doesn't exist */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid is a replayed close", tag);
			data->current_stateid_valid = false;
			goto success;
		}

		/* Release the clientid reference we just acquired. */
		dec_client_id_ref(pclientid);

		status = NFS4ERR_BAD_STATEID;
		goto failure;
	}

	/* Now, if this lease is not already reserved, reserve it */
	if (data->preserved_clientid !=
	    state2->state_owner->so_owner.so_nfs4_owner.so_clientrec) {
		if (data->preserved_clientid != NULL) {
			/* We don't expect this to happen, but, just in case...
			 * Update and release already reserved lease.
			 */
			pthread_mutex_lock(&data->preserved_clientid
					   ->cid_mutex);

			update_lease(data->preserved_clientid);

			pthread_mutex_unlock(&data->preserved_clientid
					     ->cid_mutex);

			data->preserved_clientid = NULL;
		}

		/* Check if lease is expired and reserve it */
		pthread_mutex_lock(&state2->state_owner->so_owner
				   .so_nfs4_owner.so_clientrec->cid_mutex);

		if (!reserve_lease
		    (state2->state_owner->so_owner.so_nfs4_owner.
		     so_clientrec)) {
			LogDebug(COMPONENT_STATE, "Returning NFS4ERR_EXPIRED");

			pthread_mutex_unlock(&state2->state_owner->so_owner
					     .so_nfs4_owner.so_clientrec
					     ->cid_mutex);

			status = NFS4ERR_EXPIRED;
			goto failure;
		}

		data->preserved_clientid =
		    state2->state_owner->so_owner.so_nfs4_owner.so_clientrec;

		pthread_mutex_unlock(&state2->state_owner->so_owner
				     .so_nfs4_owner.so_clientrec->cid_mutex);
	}

	/* Sanity check : Is this the right file ? */
	if ((entry != NULL) && (state2->state_entry != entry)) {
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
				state2->state_owner->so_owner.so_nfs4_owner.
				so_seqid)) {
				LogDebug(COMPONENT_STATE, "possible replay?");
				*state = state2;
				status = NFS4ERR_REPLAY;
				goto replay;
			}
			/* OLD_STATEID */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found OLD stateid %s, expected seqid %u",
				 tag, str, (unsigned int)state2->state_seqid);
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
			     state2->state_owner->so_owner.so_nfs4_owner.
			     so_seqid)) {
			LogDebug(COMPONENT_STATE, "possible replay?");
			*state = state2;
			status = NFS4ERR_REPLAY;
			goto replay;
		} else if (diff > 0) {
			/* BAD_STATEID */
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found BAD stateid %s, expected seqid %u",
				 tag, str, (unsigned int)state2->state_seqid);
			status = NFS4ERR_BAD_STATEID;
			goto failure;
		}
	}

	if ((flags & STATEID_SPECIAL_FREE) != 0) {
		switch (state2->state_type) {
		case STATE_TYPE_LOCK:
			PTHREAD_RWLOCK_rdlock(&state2->state_entry->state_lock);
			if (glist_empty
			    (&state2->state_data.lock.state_locklist)) {
				LogFullDebug(COMPONENT_STATE,
					     "Check %s stateid %s has no locks, ok to free",
					     tag, str);
				PTHREAD_RWLOCK_unlock(&state2->state_entry->
						      state_lock);
				break;
			}
			PTHREAD_RWLOCK_unlock(&state2->state_entry->state_lock);
			/* Fall through for failure */

		case STATE_TYPE_NONE:
		case STATE_TYPE_SHARE:
		case STATE_TYPE_DELEG:
		case STATE_TYPE_LAYOUT:
			LogDebug(COMPONENT_STATE,
				 "Check %s stateid found stateid %s with locks held",
				 tag, str);

			status = NFS4ERR_LOCKS_HELD;
			goto failure;
		}
	}

	data->current_stateid_valid = true;

	LogFullDebug(COMPONENT_STATE,
		     "Check %s stateid found valid stateid %s - %p", tag, str,
		     state2);

	/* Copy stateid into current for later use */
	data->current_stateid = *stateid;
	data->current_stateid.seqid = state2->state_seqid;

 success:

	*state = state2;
	return NFS4_OK;

 failure:

	*state = NULL;

 replay:

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
 * @param[in,out] state State to update
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
		data->current_stateid.seqid = state->state_seqid;
		memcpy(data->current_stateid.other, state->stateid_other,
		       OTHERSIZE);
		data->current_stateid_valid = true;
	}

	/* Copy stateid into response */
	resp->seqid = state->state_seqid;
	memcpy(resp->other, state->stateid_other, OTHERSIZE);

	if (isFullDebug(COMPONENT_STATE)) {
		char str[OTHERSIZE * 2 + 1 + 6];
		sprint_mem(str, (char *)state->stateid_other, OTHERSIZE);
		sprintf(str + OTHERSIZE * 2, ":%u",
			(unsigned int)state->state_seqid);
		LogDebug(COMPONENT_STATE,
			 "Update %s stateid to %s for response", tag, str);
	}
}

/**
 * @brief Check to see if any share conflicts with anonymous stateid
 *
 * @param[in] entry  File to check
 * @param[in] tag    Arbitrary debug/log string
 * @param[in] access Access sought
 *
 * @return NFS4 response codes
 */
nfsstat4 nfs4_check_special_stateid(cache_entry_t *entry, const char *tag,
				    int access)
{

	struct glist_head *glist;
	state_t *state_iterate;
	int rc = NFS4_OK;

	/* Acquire lock to enter critical section on this entry */
	PTHREAD_RWLOCK_rdlock(&entry->state_lock);

	/* Iterate through file's state to look for conflicts */
	glist_for_each(glist, &entry->state_list) {
		state_iterate = glist_entry(glist, state_t, state_list);

		switch (state_iterate->state_type) {
		case STATE_TYPE_SHARE:
			if ((access == FATTR4_ATTR_READ)
			    && (state_iterate->state_data.share.
				share_deny & OPEN4_SHARE_DENY_READ)) {
				/* Reading to this file is prohibited,
				 * file is read-denied
				 */
				rc = NFS4ERR_LOCKED;
				LogDebug(COMPONENT_NFS_V4_LOCK,
					 "%s is denied by state %p", tag,
					 state_iterate);
				goto ssid_out;
			}

			if ((access == FATTR4_ATTR_WRITE)
			    && (state_iterate->state_data.share.
				share_deny & OPEN4_SHARE_DENY_WRITE)) {
				/* Writing to this file is prohibited,
				 * file is write-denied
				 */
				rc = NFS4ERR_LOCKED;
				LogDebug(COMPONENT_NFS_V4_LOCK,
					 "%s is denied by state %p", tag,
					 state_iterate);
				goto ssid_out;
			}

			if ((access == FATTR4_ATTR_READ_WRITE)
			    && (state_iterate->state_data.share.
				share_deny & OPEN4_SHARE_DENY_BOTH)) {
				/* Reading and writing to this file is
				 * prohibited, file is rw-denied
				 */
				rc = NFS4ERR_LOCKED;
				LogDebug(COMPONENT_NFS_V4_LOCK,
					 "%s is denied by state %p", tag,
					 state_iterate);
				goto ssid_out;
			}

			break;

		case STATE_TYPE_LOCK:
			/* Skip, will check for conflicting locks later */
			break;

		case STATE_TYPE_DELEG:
			/** @todo FSF: should check for conflicting delegations,
			 *  may need to recall
			 */
			break;

		case STATE_TYPE_LAYOUT:
			/** @todo FSF: should check for conflicting layouts,
			 *  may need to recall
			 *  Need to look at this even for NFS v4 WRITE since
			 * there may be NFS v4.1 users of the file
			 */
			break;

		case STATE_TYPE_NONE:
			break;

		default:
			break;
		}
	}

	/** @todo FSF: need to check against existing locks */

 ssid_out:

	/* Use this exit point if the lock was already acquired. */

	PTHREAD_RWLOCK_unlock(&entry->state_lock);
	return rc;
}

/** @} */
