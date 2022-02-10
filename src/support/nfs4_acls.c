// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
 *
 * --------------------------
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
 *
 */

#include "config.h"
#include "abstract_mem.h"
#include "fsal_types.h"
#include "hashtable.h"
#include "log.h"
#include "nfs4_acls.h"
#include "city.h"
#include "common_utils.h"

pool_t *fsal_acl_pool;

static int fsal_acl_hash_both(hash_parameter_t *, struct gsh_buffdesc *,
			      uint32_t *, uint64_t *);
static int compare_fsal_acl(struct gsh_buffdesc *, struct gsh_buffdesc *);

/* DEFAULT PARAMETERS for hash table */

static hash_parameter_t fsal_acl_hash_config = {
	.index_size = 67,
	.hash_func_key = NULL,
	.hash_func_rbt = NULL,
	.hash_func_both = fsal_acl_hash_both,
	.compare_key = compare_fsal_acl,
	.ht_name = "ACL Table",
	.flags = HT_FLAG_CACHE,
	.ht_log_component = COMPONENT_NFS_V4_ACL
};

static hash_table_t *fsal_acl_hash;

/* hash table functions */

static int fsal_acl_hash_both(hash_parameter_t *hparam,
			      struct gsh_buffdesc *key, uint32_t *index,
			      uint64_t *rbthash)
{
	*rbthash = CityHash64(key->addr, key->len);
	*index = *rbthash % hparam->index_size;

	return 1;
}

static int compare_fsal_acl(struct gsh_buffdesc *key1,
			    struct gsh_buffdesc *keya)
{
	if (key1->len != keya->len)
		return -1;

	return memcmp(key1->addr, keya->addr, key1->len);
}

fsal_ace_t *nfs4_ace_alloc(int nace)
{
	fsal_ace_t *ace = NULL;

	ace = gsh_calloc(nace, sizeof(fsal_ace_t));
	return ace;
}

fsal_acl_t *nfs4_acl_alloc(void)
{
	fsal_acl_t *acl = pool_alloc(fsal_acl_pool);

	PTHREAD_RWLOCK_init(&acl->lock, NULL);

	return acl;
}

void nfs4_ace_free(fsal_ace_t *ace)
{
	if (!ace)
		return;

	LogDebug(COMPONENT_NFS_V4_ACL, "free ace %p", ace);

	gsh_free(ace);
}

void nfs4_acl_free(fsal_acl_t *acl)
{
	if (!acl)
		return;

	if (acl->aces)
		nfs4_ace_free(acl->aces);

	PTHREAD_RWLOCK_destroy(&acl->lock);

	pool_free(fsal_acl_pool, acl);
}

void nfs4_acl_entry_inc_ref(fsal_acl_t *acl)
{
	/* Increase ref counter */
	PTHREAD_RWLOCK_wrlock(&acl->lock);
	acl->ref++;
	LogDebug(COMPONENT_NFS_V4_ACL, "(acl, ref) = (%p, %u)", acl, acl->ref);
	PTHREAD_RWLOCK_unlock(&acl->lock);
}

/* Should be called with lock held. */
static void nfs4_acl_entry_dec_ref(fsal_acl_t *acl)
{
	/* Decrease ref counter */
	acl->ref--;
	LogDebug(COMPONENT_NFS_V4_ACL, "(acl, ref) = (%p, %u)", acl, acl->ref);
}

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *acldata,
			       fsal_acl_status_t *status)
{
	fsal_acl_t *acl = NULL;
	struct gsh_buffdesc key;
	struct gsh_buffdesc value;
	int rc;
	struct hash_latch latch;

	/* Set the return default to NFS_V4_ACL_SUCCESS */
	*status = NFS_V4_ACL_SUCCESS;

	key.addr = acldata->aces;
	key.len = acldata->naces * sizeof(fsal_ace_t);

	/* Check if the entry already exists */
	rc = hashtable_getlatch(fsal_acl_hash, &key, &value, true, &latch);
	if (rc == HASHTABLE_SUCCESS) {
		/* Entry is already in the cache, do not add it */
		acl = value.addr;
		*status = NFS_V4_ACL_EXISTS;

		nfs4_ace_free(acldata->aces);
		nfs4_acl_entry_inc_ref(acl);
		hashtable_releaselatched(fsal_acl_hash, &latch);

		return acl;
	}

	/* Any other result other than no such key is an error */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		*status = NFS_V4_ACL_INIT_ENTRY_FAILED;
		nfs4_ace_free(acldata->aces);
		return NULL;
	}

	/* Adding the entry in the cache */
	acl = nfs4_acl_alloc();
	acl->naces = acldata->naces;
	acl->aces = acldata->aces;
	acl->ref = 1;		/* We give out one reference */

	/* Build the value */
	value.addr = acl;
	value.len = sizeof(fsal_acl_t);

	rc = hashtable_setlatched(fsal_acl_hash, &key, &value, &latch,
				  false, NULL, NULL);

	if (rc != HASHTABLE_SUCCESS) {
		/* Put the entry back in its pool */
		nfs4_acl_free(acl);
		LogWarn(COMPONENT_NFS_V4_ACL,
			"New ACL entry could not be added to hash, rc=%s",
			hash_table_err_to_str(rc));

		*status = NFS_V4_ACL_HASH_SET_ERROR;
		return NULL;
	}

	return acl;
}

fsal_acl_status_t nfs4_acl_release_entry(fsal_acl_t *acl)
{
	struct gsh_buffdesc key, old_key;
	struct gsh_buffdesc old_value;
	int rc;
	struct hash_latch latch;
	fsal_acl_status_t status = NFS_V4_ACL_SUCCESS;

	if (!acl)
		return status;

	PTHREAD_RWLOCK_wrlock(&acl->lock);
	if (acl->ref > 1) {
		nfs4_acl_entry_dec_ref(acl);
		PTHREAD_RWLOCK_unlock(&acl->lock);
		return status;
	} else
		LogDebug(COMPONENT_NFS_V4_ACL, "Free ACL %p", acl);

	key.addr = acl->aces;
	key.len = acl->naces * sizeof(fsal_ace_t);

	PTHREAD_RWLOCK_unlock(&acl->lock);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(fsal_acl_hash, &key, &old_value, true, &latch);

	switch (rc) {
	case HASHTABLE_ERROR_NO_SUCH_KEY:
		hashtable_releaselatched(fsal_acl_hash, &latch);
		return status;

	case HASHTABLE_SUCCESS:
		PTHREAD_RWLOCK_wrlock(&acl->lock);
		nfs4_acl_entry_dec_ref(acl);
		if (acl->ref != 0) {
			/* Did not actually release last reference */
			hashtable_releaselatched(fsal_acl_hash, &latch);
			PTHREAD_RWLOCK_unlock(&acl->lock);
			return status;
		}

		/* use the key to delete the entry */
		hashtable_deletelatched(fsal_acl_hash, &key, &latch,
					&old_key, &old_value);

		/* Release the latch */
		hashtable_releaselatched(fsal_acl_hash, &latch);
		break;

	default:
		LogCrit(COMPONENT_NFS_V4_ACL,
			"ACL entry could not be deleted, status=%s",
			hash_table_err_to_str(rc));
		return NFS_V4_ACL_ERROR;
	}

	/* Sanity check: old_value.addr is expected to be equal to acl,
	 * and is released later in this function */
	assert(old_value.addr == acl);

	PTHREAD_RWLOCK_unlock(&acl->lock);

	/* Release acl */
	nfs4_acl_free(acl);
	return status;
}

int nfs4_acls_init(void)
{
	LogDebug(COMPONENT_NFS_V4_ACL, "Initialize NFSv4 ACLs");
	LogDebug(COMPONENT_NFS_V4_ACL,
		 "sizeof(fsal_ace_t)=%zu, sizeof(fsal_acl_t)=%zu",
		 sizeof(fsal_ace_t), sizeof(fsal_acl_t));

	/* Initialize memory pool of ACLs. */
	fsal_acl_pool = pool_basic_init("acl_pool", sizeof(fsal_acl_t));

	/* Create hash table. */
	fsal_acl_hash = hashtable_init(&fsal_acl_hash_config);

	if (!fsal_acl_hash) {
		LogCrit(COMPONENT_NFS_V4_ACL,
			"ERROR creating hash table for NFSv4 ACLs");
		return NFS_V4_ACL_INTERNAL_ERROR;
	}

	return NFS_V4_ACL_SUCCESS;
}
