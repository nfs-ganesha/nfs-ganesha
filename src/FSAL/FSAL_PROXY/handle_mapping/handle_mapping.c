/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/*
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file handle_mapping.c
 *
 * \brief  This module is used for managing a persistent
 *         map between PROXY FSAL handles (including NFSv4 handles from server)
 *         and nfsv3 handles digests (sent to client).
 */
#include "config.h"
#include "fsal.h"
#include "nfs4.h"
#include "handle_mapping.h"
#include "handle_mapping_db.h"
#include "handle_mapping_internal.h"

static hash_table_t *handle_map_hash;

/* memory pool definitions */

typedef struct digest_pool_entry__ {
	nfs23_map_handle_t nfs23_digest;
} digest_pool_entry_t;

typedef struct handle_pool_entry__ {
	uint32_t fh_len;
	char fh_data[NFS4_FHSIZE];
} handle_pool_entry_t;

pool_t *digest_pool;
static pthread_mutex_t digest_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

pool_t *handle_pool;
static pthread_mutex_t handle_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

/* helpers for pool allocation */

static digest_pool_entry_t *digest_alloc()
{
	digest_pool_entry_t *p_new;

	pthread_mutex_lock(&digest_pool_mutex);
	p_new = pool_alloc(digest_pool, NULL);
	pthread_mutex_unlock(&digest_pool_mutex);

	memset(p_new, 0, sizeof(digest_pool_entry_t));

	return p_new;
}

static void digest_free(digest_pool_entry_t *p_digest)
{
	memset(p_digest, 0, sizeof(digest_pool_entry_t));

	pthread_mutex_lock(&digest_pool_mutex);
	pool_free(digest_pool, p_digest);
	pthread_mutex_unlock(&digest_pool_mutex);
}

static handle_pool_entry_t *handle_alloc()
{
	handle_pool_entry_t *p_new;

	pthread_mutex_lock(&handle_pool_mutex);
	p_new = pool_alloc(handle_pool, NULL);
	pthread_mutex_unlock(&handle_pool_mutex);

	memset(p_new, 0, sizeof(handle_pool_entry_t));

	return p_new;
}

static void handle_free(handle_pool_entry_t *p_handle)
{
	memset(p_handle, 0, sizeof(handle_pool_entry_t));

	pthread_mutex_lock(&handle_pool_mutex);
	pool_free(handle_pool, p_handle);
	pthread_mutex_unlock(&handle_pool_mutex);
}

/* hash table functions */

static uint32_t hash_digest_idx(hash_parameter_t *p_conf,
				struct gsh_buffdesc *p_key)
{
	uint32_t hash;
	digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_key->addr;

	hash = ((unsigned long)p_digest->nfs23_digest.object_id ^
		(unsigned int)p_digest->nfs23_digest.handle_hash);
	hash = (743 * hash + 1999) % p_conf->index_size;

	return hash;

}

static unsigned long hash_digest_rbt(hash_parameter_t *p_conf,
				     struct gsh_buffdesc *p_key)
{
	unsigned long hash;
	digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_key->addr;

	hash = (257 * p_digest->nfs23_digest.object_id + 541);

	return hash;
}

static int cmp_digest(struct gsh_buffdesc *p_key1, struct gsh_buffdesc *p_key2)
{
	digest_pool_entry_t *p_digest1 = (digest_pool_entry_t *) p_key1->addr;
	digest_pool_entry_t *p_digest2 = (digest_pool_entry_t *) p_key2->addr;

	/* compare object_id and handle_hash */

	if (p_digest1->nfs23_digest.object_id !=
	    p_digest2->nfs23_digest.object_id)
		return (int)(p_digest1->nfs23_digest.object_id -
			     p_digest2->nfs23_digest.object_id);
	else if (p_digest1->nfs23_digest.handle_hash !=
		 p_digest2->nfs23_digest.handle_hash)
		return (int)p_digest1->nfs23_digest.handle_hash -
		    (int)p_digest2->nfs23_digest.handle_hash;
	else			/* same */
		return 0;
}

static int print_digest(struct gsh_buffdesc *p_val, char *outbuff)
{
	digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_val->addr;

	return sprintf(outbuff, "%llu, %u",
		       (unsigned long long)p_digest->nfs23_digest.object_id,
		       p_digest->nfs23_digest.handle_hash);
}

static int print_handle(struct gsh_buffdesc *p_val, char *outbuff)
{
	handle_pool_entry_t *p_handle = (handle_pool_entry_t *) p_val->addr;

	return snprintmem(outbuff, HASHTABLE_DISPLAY_STRLEN, p_handle->fh_data,
			  p_handle->fh_len);
}

int handle_mapping_hash_add(hash_table_t *p_hash, uint64_t object_id,
			    unsigned int handle_hash, const void *data,
			    uint32_t datalen)
{
	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	digest_pool_entry_t *digest;
	handle_pool_entry_t *handle;

	if (datalen >= sizeof(handle->fh_data))
		return HANDLEMAP_INVALID_PARAM;

	digest = digest_alloc();

	if (!digest)
		return HANDLEMAP_SYSTEM_ERROR;

	handle = handle_alloc();

	if (!handle) {
		digest_free(digest);
		return HANDLEMAP_SYSTEM_ERROR;
	}

	digest->nfs23_digest.object_id = object_id;
	digest->nfs23_digest.handle_hash = handle_hash;
	memset(handle->fh_data, 0, sizeof(handle->fh_data));
	memcpy(handle->fh_data, data, datalen);
	handle->fh_len = datalen;

	buffkey.addr = (caddr_t) digest;
	buffkey.len = sizeof(digest_pool_entry_t);

	buffval.addr = (caddr_t) handle;
	buffval.len = sizeof(handle_pool_entry_t);

	rc = hashtable_test_and_set(handle_map_hash, &buffkey, &buffval,
				    HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

	if (rc != HASHTABLE_SUCCESS) {
		digest_free(digest);
		handle_free(handle);

		if (rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS) {
			LogCrit(COMPONENT_FSAL,
				"ERROR %d inserting entry to handle mapping hash table",
				rc);
			return HANDLEMAP_HASHTABLE_ERROR;
		} else {
			return HANDLEMAP_EXISTS;
		}
	}

	return HANDLEMAP_SUCCESS;
}

/* DEFAULT PARAMETERS for hash table */
static hash_parameter_t handle_hash_config = {
	.index_size = 67,
	.hash_func_key = hash_digest_idx,
	.hash_func_rbt = hash_digest_rbt,
	.compare_key = cmp_digest,
	.key_to_str = print_digest,
	.val_to_str = print_handle
};

/**
 * Init handle mapping module.
 * Reloads the content of the mapping files it they exist,
 * else it creates them.
 * \return 0 if OK, a posix error code else.
 */
int HandleMap_Init(const handle_map_param_t *p_param)
{
	int rc;

	/* first check database count */

	rc = handlemap_db_count(p_param->databases_directory);

	if ((rc > 0) && (rc != p_param->database_count)) {
		LogCrit(COMPONENT_FSAL,
			"ERROR: The number of existing databases (%u) does not match the requested DB thread count (%u)",
			rc, p_param->database_count);

		return HANDLEMAP_INVALID_PARAM;
	} else if (rc < 0)
		return -rc;

	/* init database module */

	rc = handlemap_db_init(p_param->databases_directory,
			       p_param->temp_directory, p_param->database_count,
			       p_param->synchronous_insert);

	if (rc) {
		LogCrit(COMPONENT_FSAL, "ERROR %d initializing database access",
			rc);
		return rc;
	}

	/* initialize memory pool of digests and handles */

	digest_pool =
	    pool_init(NULL, sizeof(digest_pool_entry_t), pool_basic_substrate,
		      NULL, NULL, NULL);

	handle_pool =
	    pool_init(NULL, sizeof(handle_pool_entry_t), pool_basic_substrate,
		      NULL, NULL, NULL);

	/* create hash table */

	handle_hash_config.index_size = p_param->hashtable_size;

	handle_map_hash = hashtable_init(&handle_hash_config);

	if (!handle_map_hash) {
		LogCrit(COMPONENT_FSAL,
			"ERROR creating hash table for handle mapping");
		return HANDLEMAP_INTERNAL_ERROR;
	}

	/* reload previous data */

	rc = handlemap_db_reaload_all(handle_map_hash);

	if (rc) {
		LogCrit(COMPONENT_FSAL,
			"ERROR %d reloading handle mapping from database", rc);
		return rc;
	}

	return HANDLEMAP_SUCCESS;
}

/**
 * @brief Retrieves a full fsal_handle from a NFS3 digest.
 *
 * @param[in]  nfs23_digest The NFS3 handle digest
 * @param[out] fsal_handle  The fsal handle to be retrieved
 *
 * @note The caller must provide storage for nfs_fh4_val.
 *
 * @retval HANDLEMAP_SUCCESS if the handle is available
 * @retval HANDLEMAP_STALE if the disgest is unknown or the handle has been deleted
 */
int HandleMap_GetFH(const nfs23_map_handle_t *nfs23_digest,
		    struct gsh_buffdesc *fsal_handle)
{

	int rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	digest_pool_entry_t digest;
	struct hash_latch hl;

	digest.nfs23_digest = *nfs23_digest;

	buffkey.addr = (caddr_t) &digest;
	buffkey.len = sizeof(digest_pool_entry_t);

	rc = hashtable_getlatch(handle_map_hash, &buffkey, &buffval, 0, &hl);

	if (rc == HASHTABLE_SUCCESS) {
		handle_pool_entry_t *h = (handle_pool_entry_t *) buffval.addr;
		if (h->fh_len < fsal_handle->len) {
			fsal_handle->len = h->fh_len;
			memcpy(fsal_handle->addr, h->fh_data, h->fh_len);
			rc = HANDLEMAP_SUCCESS;
		} else {
			rc = HANDLEMAP_INTERNAL_ERROR;
		}
		hashtable_releaselatched(handle_map_hash, &hl);
		return rc;
	}

	if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
		hashtable_releaselatched(handle_map_hash, &hl);
	return HANDLEMAP_STALE;
}				/* HandleMap_GetFH */

/**
 * Save the handle association if it was unknown.
 */
int HandleMap_SetFH(nfs23_map_handle_t *p_in_nfs23_digest, const void *data,
		    uint32_t len)
{
	int rc;

	/* first, try to insert it to the hash table */

	rc = handle_mapping_hash_add(handle_map_hash,
				     p_in_nfs23_digest->object_id,
				     p_in_nfs23_digest->handle_hash, data, len);

	if ((rc != 0) && (rc != HANDLEMAP_EXISTS))
		/* error */
		return rc;
	else if (rc == HANDLEMAP_EXISTS)
		/* already in database */
		return HANDLEMAP_EXISTS;
	else {
		/* insert it to DB */
		return handlemap_db_insert(p_in_nfs23_digest, data, len);
	}
}

/**
 * Remove a handle from the map
 * when it was removed from the filesystem
 * or when it is stale.
 */
int HandleMap_DelFH(nfs23_map_handle_t *p_in_nfs23_digest)
{
	int rc;
	struct gsh_buffdesc buffkey, stored_buffkey;
	struct gsh_buffdesc stored_buffval;

	digest_pool_entry_t digest;

	digest_pool_entry_t *p_stored_digest;
	handle_pool_entry_t *p_stored_handle;

	/* first, delete it from hash table */

	digest.nfs23_digest = *p_in_nfs23_digest;

	buffkey.addr = (caddr_t) &digest;
	buffkey.len = sizeof(digest_pool_entry_t);

	rc = HashTable_Del(handle_map_hash, &buffkey, &stored_buffkey,
			   &stored_buffval);

	if (rc != HASHTABLE_SUCCESS)
		return HANDLEMAP_STALE;

	p_stored_digest = (digest_pool_entry_t *) stored_buffkey.addr;
	p_stored_handle = (handle_pool_entry_t *) stored_buffval.addr;

	digest_free(p_stored_digest);
	handle_free(p_stored_handle);

	/* then, submit the request to the database */

	return handlemap_db_delete(p_in_nfs23_digest);

}

/**
 * Flush pending database operations (before stopping the server).
 */
int HandleMap_Flush()
{
	return handlemap_db_flush();
}
