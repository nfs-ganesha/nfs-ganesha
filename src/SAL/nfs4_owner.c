/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file nfs4_owner.c
 * @brief The management of the NFS4 Owner cache.
 */

#include "config.h"
#include <pthread.h>
#include <ctype.h>
#include "log.h"
#include "hashtable.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

hash_table_t *ht_nfs4_owner;

/**
 * @brief Display an NFSv4 owner key
 *
 * @param[in]  buff Key to display
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nfs4_owner_key(struct gsh_buffdesc *buff, char *str)
{
	return display_nfs4_owner((state_owner_t *) (buff->addr), str);
}

/**
 * @brief Display NFSv4 owner
 *
 * @param[in]  owner The state owner
 * @param[out] str   Output string
 *
 * @return The length of the output string.
 */
int display_nfs4_owner(state_owner_t *owner, char *str)
{
	char *strtmp = str;

	strtmp +=
	    sprintf(strtmp, "%s %p:", state_owner_type_to_str(owner->so_type),
		    owner);

	strtmp += sprintf(strtmp, " clientid={");
	strtmp +=
	    display_client_id_rec(owner->so_owner.so_nfs4_owner.so_clientrec,
				  strtmp);
	strtmp += sprintf(strtmp, "} owner=");

	strtmp +=
	    DisplayOpaqueValue(owner->so_owner_val, owner->so_owner_len,
			       strtmp);

	strtmp +=
	    sprintf(strtmp, " confirmed=%u seqid=%u",
		    owner->so_owner.so_nfs4_owner.so_confirmed,
		    owner->so_owner.so_nfs4_owner.so_seqid);

	if (owner->so_owner.so_nfs4_owner.so_related_owner != NULL) {
		strtmp += sprintf(strtmp, " related_owner={");
		strtmp +=
		    display_nfs4_owner(owner->so_owner.so_nfs4_owner.
				       so_related_owner, strtmp);
		strtmp += sprintf(strtmp, "}");
	}

	strtmp +=
	    sprintf(strtmp, " refcount=%d",
		    atomic_fetch_int32_t(&owner->so_refcount));

	return strtmp - str;
}

/**
 * @brief Display owner from hash table
 *
 * @param[in]  buff Buffer
 * @param[out] str  Output buffer
 *
 * @return Length of the output string.
 */
int display_nfs4_owner_val(struct gsh_buffdesc *buff, char *str)
{
	return display_nfs4_owner(buff->addr, str);
}

/**
 * @brief Compare two NFSv4 owners
 *
 * @param[in] owner1 One owner
 * @param[in] owner2 Another owner
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nfs4_owner(state_owner_t *owner1, state_owner_t *owner2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_nfs4_owner(owner1, str1);
		display_nfs4_owner(owner2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (owner1 == NULL || owner2 == NULL)
		return 1;

	if (owner1 == owner2)
		return 0;

	if (owner1->so_type == STATE_LOCK_OWNER_NFSV4
	    && owner2->so_type == STATE_OPEN_OWNER_NFSV4)
		return compare_nfs4_owner(owner1->so_owner.so_nfs4_owner.
					  so_related_owner, owner2);

	if (owner2->so_type == STATE_LOCK_OWNER_NFSV4
	    && owner1->so_type == STATE_OPEN_OWNER_NFSV4)
		return compare_nfs4_owner(owner2->so_owner.so_nfs4_owner.
					  so_related_owner, owner1);

	if (owner1->so_type != owner2->so_type)
		return 1;

	if (owner1->so_owner.so_nfs4_owner.so_clientid !=
	    owner2->so_owner.so_nfs4_owner.so_clientid)
		return 1;

	if (owner1->so_owner_len != owner2->so_owner_len)
		return 1;

	return memcmp(owner1->so_owner_val, owner2->so_owner_val,
		      owner1->so_owner_len);
}

/**
 * @brief Compare two NFSv4 owners in the hash table
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another owner
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nfs4_owner_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2)
{
	state_owner_t *pkey1 = buff1->addr;
	state_owner_t *pkey2 = buff2->addr;

	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		DisplayOwner(pkey1, str1);
		DisplayOwner(pkey2, str2);

		if (isDebug(COMPONENT_HASHTABLE))
			LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1,
				     str2);
	}

	if (pkey1 == NULL || pkey2 == NULL)
		return 1;

	if (pkey1->so_type != pkey2->so_type)
		return 1;

	return compare_nfs4_owner(pkey1, pkey2);
}

/**
 * @brief Compute the hash index for an NFSv4 owner
 *
 * @todo Destroy this function and replace it with a real hash.
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    The key
 *
 * @return The hash index.
 */
uint32_t nfs4_owner_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i = 0;
	unsigned char c = 0;
	uint32_t res = 0;

	state_owner_t *pkey = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++) {
		c = ((char *)pkey->so_owner_val)[i];
		sum += c;
	}

	res =
	    ((uint32_t) pkey->so_owner.so_nfs4_owner.so_clientid +
	     (uint32_t) sum + pkey->so_owner_len +
	     (uint32_t) pkey->so_type) % (uint32_t) hparam->index_size;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %" PRIu32, res);

	return res;
}

/**
 * @brief Compute the RBT hash for an NFSv4 owner
 *
 * @todo Destroy this function and replace it with a real hash.
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    The key
 *
 * @return The RBT hash.
 */
uint64_t nfs4_owner_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	state_owner_t *pkey = key->addr;

	unsigned int sum = 0;
	unsigned int i = 0;
	unsigned char c = 0;
	uint64_t res = 0;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++) {
		c = ((char *)pkey->so_owner_val)[i];
		sum += c;
	}

	res =
	    (uint64_t) pkey->so_owner.so_nfs4_owner.so_clientid +
	    (uint64_t) sum + pkey->so_owner_len + (uint64_t) pkey->so_type;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %" PRIu64, res);

	return res;
}

/**
 * @brief Free an NFS4 owner object
 *
 * @param[in] owner Owner to remove
 */

void free_nfs4_owner(state_owner_t *owner)
{
	if (owner->so_owner.so_nfs4_owner.so_related_owner != NULL)
		dec_state_owner_ref(owner->so_owner.so_nfs4_owner.
				    so_related_owner);

	/* Release the saved response. */
	nfs4_Compound_FreeOne(&owner->so_owner.so_nfs4_owner.so_resp);

	/* Remove the owner from the owners per clientid list. */
	pthread_mutex_lock(&owner->so_owner.so_nfs4_owner.so_clientrec
			   ->cid_mutex);

	glist_del(&owner->so_owner.so_nfs4_owner.so_perclient);

	pthread_mutex_unlock(&owner->so_owner.so_nfs4_owner.so_clientrec
			     ->cid_mutex);

	dec_client_id_ref(owner->so_owner.so_nfs4_owner.so_clientrec);
}

static hash_parameter_t nfs4_owner_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nfs4_owner_value_hash_func,
	.hash_func_rbt = nfs4_owner_rbt_hash_func,
	.compare_key = compare_nfs4_owner_key,
	.key_to_str = display_nfs4_owner_key,
	.val_to_str = display_nfs4_owner_val,
	.flags = HT_FLAG_CACHE,
};

/**
 * @brief Init the hashtable for NFSv4 owner cache
 *
 * @retval 0 if successful.
 * @retval -1 if we failed.
 */
int Init_nfs4_owner(void)
{
	ht_nfs4_owner = hashtable_init(&nfs4_owner_param);

	if (ht_nfs4_owner == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NFS Open Owner cache");
		return -1;
	}

	return 0;
}				/* nfs4_Init_nfs4_owner */

/**
 * @brief Initialize an NFS4 open owner object
 *
 * @param[in] owner The owner record
 *
 */
static void init_nfs4_owner(state_owner_t *owner)
{
	glist_init(&owner->so_owner.so_nfs4_owner.so_state_list);

	/* Increment refcount on related owner */
	if (owner->so_owner.so_nfs4_owner.so_related_owner != NULL)
		inc_state_owner_ref(owner->so_owner.so_nfs4_owner.
				    so_related_owner);

	/* Increment reference count for clientid record */
	inc_client_id_ref(owner->so_owner.so_nfs4_owner.so_clientrec);

	pthread_mutex_lock(&owner->so_owner.so_nfs4_owner.so_clientrec
			   ->cid_mutex);

	if (owner->so_type == STATE_OPEN_OWNER_NFSV4) {
		/* If open owner, add to clientid lock owner list */
		glist_add_tail(&owner->so_owner.so_nfs4_owner.so_clientrec->
			       cid_openowners,
			       &owner->so_owner.so_nfs4_owner.so_perclient);
	} else if (owner->so_type == STATE_LOCK_OWNER_NFSV4) {
		/* If lock owner, add to clientid open owner list */
		glist_add_tail(&owner->so_owner.so_nfs4_owner.so_clientrec->
			       cid_lockowners,
			       &owner->so_owner.so_nfs4_owner.so_perclient);
	}

	pthread_mutex_unlock(&owner->so_owner.so_nfs4_owner.so_clientrec
			     ->cid_mutex);
}

/**
 * @brief Display the NFSv4 owner table
 */
void nfs4_owner_PrintAll(void)
{
	hashtable_log(COMPONENT_STATE, ht_nfs4_owner);
}

/**
 * @brief Create an NFSv4 state owner
 *
 * @param[in]  name          Owner name
 * @param[in]  clientid      Client record
 * @param[in]  type          Owner type
 * @param[in]  related_owner For lock owners, the related open owner
 * @param[in]  init_seqid    The starting seqid (for NFSv4.0)
 * @param[out] pisnew        Whether the owner actually is new
 * @param[in]  care          Care flag (to unify v3/v4 owners?)
 *
 * @return A new state owner or NULL.
 */
state_owner_t *create_nfs4_owner(state_nfs4_owner_name_t *name,
				 nfs_client_id_t *clientid,
				 state_owner_type_t type,
				 state_owner_t *related_owner,
				 unsigned int init_seqid, bool_t *pisnew,
				 care_t care)
{
	state_owner_t key;
	state_owner_t *owner;
	bool_t isnew;

	/* set up the content of the open_owner */
	memset(&key, 0, sizeof(key));

	key.so_type = type;
	key.so_owner.so_nfs4_owner.so_seqid = init_seqid;
	key.so_owner.so_nfs4_owner.so_related_owner = related_owner;
	key.so_owner.so_nfs4_owner.so_clientid = clientid->cid_clientid;
	key.so_owner.so_nfs4_owner.so_clientrec = clientid;
	key.so_owner_len = name->son_owner_len;
	key.so_owner_val = name->son_owner_val;
	key.so_owner.so_nfs4_owner.so_resp.resop = NFS4_OP_ILLEGAL;
	key.so_owner.so_nfs4_owner.so_args.argop = NFS4_OP_ILLEGAL;
	key.so_refcount = 1;
#if 0
	/* WAITING FOR COMMUNITY FIX */
	/* setting lock owner confirmed */
	if (type == STATE_LOCK_OWNER_NFSV4)
		key.so_owner.so_nfs4_owner.so_confirmed = 1;
#endif

	owner = get_state_owner(care, &key, init_nfs4_owner, &isnew);

	if (owner != NULL && related_owner != NULL) {
		pthread_mutex_lock(&owner->so_mutex);
		/* Related owner already exists. */
		if (owner->so_owner.so_nfs4_owner.so_related_owner == NULL) {
			/* Attach related owner to owner now that we know it. */
			inc_state_owner_ref(related_owner);
			owner->so_owner.so_nfs4_owner.so_related_owner =
			    related_owner;
		} else if (owner->so_owner.so_nfs4_owner.so_related_owner !=
			   related_owner) {
			char str1[HASHTABLE_DISPLAY_STRLEN];
			char str2[HASHTABLE_DISPLAY_STRLEN];

			DisplayOwner(related_owner, str1);
			DisplayOwner(owner, str1);

			LogCrit(COMPONENT_NFS_V4_LOCK,
				"Related {%s} doesn't match for {%s}", str1,
				str2);
			pthread_mutex_unlock(&owner->so_mutex);

			/* Release the reference to the owner. */
			dec_state_owner_ref(owner);

			return NULL;
		}
		pthread_mutex_unlock(&owner->so_mutex);
	}

	if (!isnew && owner != NULL && pisnew != NULL) {
		if (isDebug(COMPONENT_STATE)) {
			char str[HASHTABLE_DISPLAY_STRLEN];
			DisplayOwner(owner, str);
			LogDebug(COMPONENT_STATE,
				 "Previously known owner {%s} is being reused",
				 str);
		}
	}

	if (pisnew != NULL)
		*pisnew = isnew;

	return owner;
}

/**
 * @brief Fill out an NFSv4 lock conflict
 *
 * @param[out] denied   NFSv4 LOCK4denied structure
 * @param[in]  holder   Holder of the conflicting lock
 * @param[in]  conflict The conflicting lock
 */

void Process_nfs4_conflict(LOCK4denied *denied, state_owner_t *holder,
			   fsal_lock_param_t *conflict)
{
	/* A  conflicting lock from a different lock_owner,
	 * returns NFS4ERR_DENIED
	 */
	denied->offset = conflict->lock_start;
	denied->length = conflict->lock_length;

	if (conflict->lock_type == FSAL_LOCK_R)
		denied->locktype = READ_LT;
	else
		denied->locktype = WRITE_LT;

	if (holder != NULL && holder->so_owner_len != 0)
		denied->owner.owner.owner_val =
		    gsh_malloc(holder->so_owner_len);
	else
		denied->owner.owner.owner_val = NULL;

	LogFullDebug(COMPONENT_STATE, "denied->owner.owner.owner_val = %p",
		     denied->owner.owner.owner_val);

	if (denied->owner.owner.owner_val != NULL) {
		denied->owner.owner.owner_len = holder->so_owner_len;

		memcpy(denied->owner.owner.owner_val, holder->so_owner_val,
		       holder->so_owner_len);
	} else {
		denied->owner.owner.owner_len = unknown_owner.so_owner_len;
		denied->owner.owner.owner_val = unknown_owner.so_owner_val;
	}

	if (holder != NULL && holder->so_type == STATE_LOCK_OWNER_NFSV4)
		denied->owner.clientid =
		    holder->so_owner.so_nfs4_owner.so_clientid;
	else
		denied->owner.clientid = 0;

	/* Release any lock owner reference passed back from SAL */
	if (holder != NULL)
		dec_state_owner_ref(holder);
}

/**
 * @brief Release data allocated for LOCK4denied
 *
 * @param[in] denied Structure to release
 */
void Release_nfs4_denied(LOCK4denied *denied)
{
	if (denied->owner.owner.owner_val != unknown_owner.so_owner_val
	    && denied->owner.owner.owner_val != NULL)
		gsh_free(denied->owner.owner.owner_val);
}

/**
 * @brief Deep copy a LOCK4denied
 *
 * @param[out] denied_dst Target
 * @param[in]  denied_src Source
 */
void Copy_nfs4_denied(LOCK4denied *denied_dst, LOCK4denied *denied_src)
{
	memcpy(denied_dst, denied_src, sizeof(*denied_dst));

	if (denied_src->owner.owner.owner_val != unknown_owner.so_owner_val
	    && denied_src->owner.owner.owner_val != NULL) {
		denied_dst->owner.owner.owner_val =
		    gsh_malloc(denied_src->owner.owner.owner_len);
		LogFullDebug(COMPONENT_STATE,
			     "denied_dst->owner.owner.owner_val = %p",
			     denied_dst->owner.owner.owner_val);
		if (denied_dst->owner.owner.owner_val)
			memcpy(denied_dst->owner.owner.owner_val,
			       denied_src->owner.owner.owner_val,
			       denied_src->owner.owner.owner_len);
	}

	if (denied_dst->owner.owner.owner_val == NULL) {
		denied_dst->owner.owner.owner_len = unknown_owner.so_owner_len;
		denied_dst->owner.owner.owner_val = unknown_owner.so_owner_val;
	}
}

/**
 * @brief Copy a operation into a state owner
 *
 * This is only used for NFSv4.0 and only for a specific subset of
 * operations for which it guarantees At-Most Once Semantics.
 *
 * @param[in,out] owner The owner to hold the operation
 * @param[in]     seqid Seqid of this operation
 * @param[in]     args  Arguments of operation to copy
 * @param[in]     data  Compound data
 * @param[in]     resp  Response to copy
 * @param[in]     tag   Arbitrary string for logging/debugging
 */
void Copy_nfs4_state_req(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
			 cache_entry_t *entry, nfs_resop4 *resp,
			 const char *tag)
{
	/* Simplify use of this function when we may not be keeping any data
	 * for the state owner
	 */
	if (owner == NULL)
		return;

	LogFullDebug(COMPONENT_STATE,
		     "%s: saving response %p so_seqid %u new seqid %u", tag,
		     owner, owner->so_owner.so_nfs4_owner.so_seqid, seqid);

	/* Free previous response */
	nfs4_Compound_FreeOne(&owner->so_owner.so_nfs4_owner.so_resp);

	/* Copy new response */
	nfs4_Compound_CopyResOne(&owner->so_owner.so_nfs4_owner.so_resp, resp);

	/** @todo Deep copy OPEN args?
	 * if (owner->so_owner.so_nfs4_owner.so_args.argop == NFS4_OP_OPEN)
	 */

	/* Copy bnew args */
	memcpy(&owner->so_owner.so_nfs4_owner.so_args, args,
	       sizeof(owner->so_owner.so_nfs4_owner.so_args));

	/* Copy new file, note we don't take any reference, so this entry
	 * might not remain valid, but the pointer value suffices here.
	 */
	owner->so_owner.so_nfs4_owner.so_last_entry = entry;

	/** @todo Deep copy OPEN args?
	 * if (args->argop == NFS4_OP_OPEN)
	 */

	/* Store new seqid */
	owner->so_owner.so_nfs4_owner.so_seqid = seqid;
}

/**
 * @brief Check NFS4 request for valid seqid for replay, next request, or BAD_SEQID.
 *
 * Returns true if the request is the next seqid.  If the request is a
 * replay, copies the saved response and returns false.  Otherwise,
 * sets status to NFS4ERR_BAD_SEQID and returns false.
 *
 * In either case, on a false return, the caller should send the
 * resulting response back to the client.
 *
 * @param[in]  owner Owner to check
 * @param[in]  seqid Seqid to check
 * @param[in]  args  Arguments of operation
 * @param[in]  data  Compound data
 * @param[out] resp  Cached request, if replay
 * @param[in]  tag   Arbitrary string for logging/debugging
 *
 * @retval true if the caller should process the operation.
 * @retval false if the caller should immediately return the provides response.
 */
bool Check_nfs4_seqid(state_owner_t *owner, seqid4 seqid, nfs_argop4 *args,
		      cache_entry_t *entry, nfs_resop4 *resp, const char *tag)
{
	seqid4 next;
	char str[HASHTABLE_DISPLAY_STRLEN];

	/* Check if any owner to verify seqid against */
	if (owner == NULL) {
		LogFullDebug(COMPONENT_STATE,
			     "%s: Unknown owner doesn't have saved seqid, req seqid %u",
			     tag, seqid);
		return true;
	}

	if (isDebug(COMPONENT_STATE))
		DisplayOwner(owner, str);

	/* If this is a new state owner, client may start with any seqid */
	if (owner->so_owner.so_nfs4_owner.so_last_entry == NULL) {
		LogFullDebug(COMPONENT_STATE,
			     "%s: New {%s} doesn't have saved seqid, req seqid %u",
			     tag, str, seqid);
		return true;
	}

	/* Check for valid next seqid */
	next = owner->so_owner.so_nfs4_owner.so_seqid + 1;

	LogFullDebug(COMPONENT_STATE, "%s: Check {%s} next %u req seqid %u",
		     tag, str, next, seqid);

	if (seqid == next)
		return true;

	/* All NFS4 responses have the status in the same place, so use any to
	 * set NFS4ERR_BAD_SEQID
	 */
	resp->nfs_resop4_u.oplock.status = NFS4ERR_BAD_SEQID;

	/* Now check for valid replay */
	if (owner->so_owner.so_nfs4_owner.so_seqid != seqid) {
		LogDebug(COMPONENT_STATE,
			 "%s: Invalid seqid %u in request (not replay), expected seqid for {%s}, returning NFS4ERR_BAD_SEQID",
			 tag, seqid, str);
		return false;
	}

	if (args->argop != owner->so_owner.so_nfs4_owner.so_args.argop) {
		LogDebug(COMPONENT_STATE,
			 "%s: Invalid seqid %u in request (not replay - not same op), expected seqid for {%s}, returning NFS4ERR_BAD_SEQID",
			 tag, seqid, str);
		return false;
	}

	if (owner->so_owner.so_nfs4_owner.so_last_entry != entry) {
		LogDebug(COMPONENT_STATE,
			 "%s: Invalid seqid %u in request (not replay - wrong file), expected seqid for {%s}, returning NFS4ERR_BAD_SEQID",
			 tag, seqid, str);
		return false;
	}

	/** @todo FSF: add more checks here... */

	LogDebug(COMPONENT_STATE,
		 "%s: Copying saved response for seqid %u into {%s}", tag,
		 seqid, str);

	/* Copy the saved response and tell caller to use it */
	nfs4_Compound_CopyResOne(resp, &owner->so_owner.so_nfs4_owner.so_resp);

	return false;
}

/** @} */
