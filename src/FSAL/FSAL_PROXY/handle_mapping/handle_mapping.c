/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file handle_mapping.c
 *
 * \brief  This module is used for managing a persistent
 *         map between PROXY FSAL handles (including NFSv4 handles from server)
 *         and nfsv2 and v3 handles digests (sent to client).
 */
#include "config.h"
#include "handle_mapping.h"
#include "handle_mapping_db.h"
#include "handle_mapping_internal.h"
#include "../fsal_internal.h"
#include "stuff_alloc.h"

/* hashe table definitions */

static unsigned long hash_digest_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static unsigned long hash_digest_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static int cmp_digest(hash_buffer_t * p_key1, hash_buffer_t * p_key2);

static int print_digest(hash_buffer_t * p_val, char *outbuff);
static int print_handle(hash_buffer_t * p_val, char *outbuff);

/* DEFAULT PARAMETERS for hash table */

static hash_parameter_t handle_hash_config = {
  .index_size = 67,
  .alphabet_length = 10,
  .nb_node_prealloc = 1024,
  .hash_func_key = hash_digest_idx,
  .hash_func_rbt = hash_digest_rbt,
  .compare_key = cmp_digest,
  .key_to_str = print_digest,
  .val_to_str = print_handle
};

static hash_table_t *handle_map_hash = NULL;

/* memory pool definitions */

typedef struct digest_pool_entry__
{
  nfs23_map_handle_t nfs23_digest;
  struct digest_pool_entry__ *p_next;
} digest_pool_entry_t;

typedef struct handle_pool_entry__
{
  fsal_handle_t handle;
  struct handle_pool_entry__ *p_next;
} handle_pool_entry_t;

static unsigned int nb_pool_prealloc = 1024;

struct prealloc_pool digest_pool;
static pthread_mutex_t digest_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

struct prealloc_pool handle_pool;
static pthread_mutex_t handle_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

/* helpers for pool allocation */

static digest_pool_entry_t *digest_alloc()
{
  digest_pool_entry_t *p_new;

  P(digest_pool_mutex);
  GetFromPool(p_new, &digest_pool, digest_pool_entry_t);
  V(digest_pool_mutex);

  memset(p_new, 0, sizeof(digest_pool_entry_t));

  return p_new;
}

static void digest_free(digest_pool_entry_t * p_digest)
{
  memset(p_digest, 0, sizeof(digest_pool_entry_t));

  P(digest_pool_mutex);
  ReleaseToPool(p_digest, &digest_pool);
  V(digest_pool_mutex);
}

static handle_pool_entry_t *handle_alloc()
{
  handle_pool_entry_t *p_new;

  P(handle_pool_mutex);
  GetFromPool(p_new, &handle_pool, handle_pool_entry_t);
  V(handle_pool_mutex);

  memset(p_new, 0, sizeof(handle_pool_entry_t));

  return p_new;
}

static void handle_free(handle_pool_entry_t * p_handle)
{
  memset(p_handle, 0, sizeof(handle_pool_entry_t));

  P(handle_pool_mutex);
  ReleaseToPool(p_handle, &handle_pool);
  V(handle_pool_mutex);
}

/* hash table functions */

static unsigned long hash_digest_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  unsigned long hash;
  digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_key->pdata;

  hash =
      (p_conf->alphabet_length +
       ((unsigned long)p_digest->nfs23_digest.object_id ^ (unsigned int)p_digest->
        nfs23_digest.handle_hash));
  hash = (743 * hash + 1999) % p_conf->index_size;

  return hash;

}

static unsigned long hash_digest_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  unsigned long hash;
  digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_key->pdata;

  hash = (257 * p_digest->nfs23_digest.object_id + 541);

  return hash;
}

static int cmp_digest(hash_buffer_t * p_key1, hash_buffer_t * p_key2)
{
  digest_pool_entry_t *p_digest1 = (digest_pool_entry_t *) p_key1->pdata;
  digest_pool_entry_t *p_digest2 = (digest_pool_entry_t *) p_key2->pdata;

  /* compare object_id and handle_hash */

  if(p_digest1->nfs23_digest.object_id != p_digest2->nfs23_digest.object_id)
    return (int)(p_digest1->nfs23_digest.object_id - p_digest2->nfs23_digest.object_id);
  else if(p_digest1->nfs23_digest.handle_hash != p_digest2->nfs23_digest.handle_hash)
    return (int)p_digest1->nfs23_digest.handle_hash -
        (int)p_digest2->nfs23_digest.handle_hash;
  else                          /* same */
    return 0;
}

static int print_digest(hash_buffer_t * p_val, char *outbuff)
{
  digest_pool_entry_t *p_digest = (digest_pool_entry_t *) p_val->pdata;

  return sprintf(outbuff, "%llu, %u", (unsigned long long)p_digest->nfs23_digest.object_id,
                 p_digest->nfs23_digest.handle_hash);
}

static int print_handle(hash_buffer_t * p_val, char *outbuff)
{
  handle_pool_entry_t *p_handle = (handle_pool_entry_t *) p_val->pdata;

  return snprintHandle(outbuff, HASHTABLE_DISPLAY_STRLEN, &p_handle->handle);
}

int handle_mapping_hash_add(hash_table_t * p_hash,
                            uint64_t object_id,
                            unsigned int handle_hash, fsal_handle_t * p_handle)
{
  int rc;
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  digest_pool_entry_t *digest;
  handle_pool_entry_t *handle;

  digest = digest_alloc();
  handle = handle_alloc();

  if(!digest || !handle)
    return HANDLEMAP_SYSTEM_ERROR;

  digest->nfs23_digest.object_id = object_id;
  digest->nfs23_digest.handle_hash = handle_hash;
  handle->handle = *p_handle;

  buffkey.pdata = (caddr_t) digest;
  buffkey.len = sizeof(digest_pool_entry_t);

  buffval.pdata = (caddr_t) handle;
  buffval.len = sizeof(handle_pool_entry_t);

  rc = HashTable_Test_And_Set(handle_map_hash, &buffkey, &buffval,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS)
    {
      digest_free(digest);
      handle_free(handle);

      if(rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
        {
          LogCrit(COMPONENT_FSAL,
                  "ERROR %d inserting entry to handle mapping hash table", rc);
          return HANDLEMAP_HASHTABLE_ERROR;
        }
      else
        {
          return HANDLEMAP_EXISTS;
        }
    }

  return HANDLEMAP_SUCCESS;
}

/**
 * Init handle mapping module.
 * Reloads the content of the mapping files it they exist,
 * else it creates them.
 * \return 0 if OK, a posix error code else.
 */
int HandleMap_Init(const handle_map_param_t * p_param)
{
  int rc;

  nb_pool_prealloc = p_param->nb_handles_prealloc;

  /* first check database count */

  rc = handlemap_db_count(p_param->databases_directory);

  if((rc > 0) && (rc != p_param->database_count))
    {
      LogCrit(COMPONENT_FSAL,
              "ERROR: The number of existing databases (%u) does not match the requested DB thread count (%u)",
              rc, p_param->database_count);

      return HANDLEMAP_INVALID_PARAM;
    }
  else if(rc < 0)
    return -rc;

  /* init database module */

  rc = handlemap_db_init(p_param->databases_directory,
                         p_param->temp_directory,
                         p_param->database_count,
                         p_param->nb_db_op_prealloc, p_param->synchronous_insert);

  if(rc)
    {
      LogCrit(COMPONENT_FSAL, "ERROR %d initializing database access", rc);
      return rc;
    }

  /* initialize memory pool of digests and handles */

  MakePool(&digest_pool, nb_pool_prealloc, digest_pool_entry_t, NULL, NULL);

  MakePool(&handle_pool, nb_pool_prealloc, handle_pool_entry_t, NULL, NULL);

  /* create hash table */

  handle_hash_config.index_size = p_param->hashtable_size;
  handle_hash_config.nb_node_prealloc = p_param->nb_handles_prealloc;

  handle_map_hash = HashTable_Init(handle_hash_config);

  if(!handle_map_hash)
    {
      LogCrit(COMPONENT_FSAL, "ERROR creating hash table for handle mapping");
      return HANDLEMAP_INTERNAL_ERROR;
    }

  /* reload previous data */

  rc = handlemap_db_reaload_all(handle_map_hash);

  if(rc)
    {
      LogCrit(COMPONENT_FSAL, "ERROR %d reloading handle mapping from database", rc);
      return rc;
    }

  return HANDLEMAP_SUCCESS;
}

/**
 * Retrieves a full fsal_handle from a NFS2/3 digest.
 *
 * \param  p_nfs23_digest   [in] the NFS2/3 handle digest
 * \param  p_out_fsal_handle [out] the fsal handle to be retrieved
 *
 * \return HANDLEMAP_SUCCESS if the handle is available,
 *         HANDLEMAP_STALE if the disgest is unknown or the handle has been deleted
 */
int HandleMap_GetFH(nfs23_map_handle_t * p_in_nfs23_digest,
                    fsal_handle_t * p_out_fsal_handle)
{

  int rc;
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  digest_pool_entry_t digest;
  fsal_handle_t *p_handle;

  digest.nfs23_digest = *p_in_nfs23_digest;

  buffkey.pdata = (caddr_t) & digest;
  buffkey.len = sizeof(digest_pool_entry_t);

  rc = HashTable_Get(handle_map_hash, &buffkey, &buffval);

  if(rc == HASHTABLE_SUCCESS)
    {
      p_handle = (fsal_handle_t *) buffval.pdata;
      *p_out_fsal_handle = *p_handle;

      return HANDLEMAP_SUCCESS;
    }
  else
    return HANDLEMAP_STALE;

}                               /* HandleMap_GetFH */

/**
 * Save the handle association if it was unknown.
 */
int HandleMap_SetFH(nfs23_map_handle_t * p_in_nfs23_digest, fsal_handle_t * p_in_handle)
{
  int rc;

  /* first, try to insert it to the hash table */

  rc = handle_mapping_hash_add(handle_map_hash, p_in_nfs23_digest->object_id,
                               p_in_nfs23_digest->handle_hash, p_in_handle);

  if((rc != 0) && (rc != HANDLEMAP_EXISTS))
    /* error */
    return rc;
  else if(rc == HANDLEMAP_EXISTS)
    /* already in database */
    return HANDLEMAP_EXISTS;
  else
    {
      /* insert it to DB */
      return handlemap_db_insert(p_in_nfs23_digest, p_in_handle);
    }
}

/**
 * Remove a handle from the map
 * when it was removed from the filesystem
 * or when it is stale.
 */
int HandleMap_DelFH(nfs23_map_handle_t * p_in_nfs23_digest)
{
  int rc;
  hash_buffer_t buffkey, stored_buffkey;
  hash_buffer_t stored_buffval;

  digest_pool_entry_t digest;

  digest_pool_entry_t *p_stored_digest;
  handle_pool_entry_t *p_stored_handle;

  /* first, delete it from hash table */

  digest.nfs23_digest = *p_in_nfs23_digest;

  buffkey.pdata = (caddr_t) & digest;
  buffkey.len = sizeof(digest_pool_entry_t);

  rc = HashTable_Del(handle_map_hash, &buffkey, &stored_buffkey, &stored_buffval);

  if(rc != HASHTABLE_SUCCESS)
    {
      return HANDLEMAP_STALE;
    }

  p_stored_digest = (digest_pool_entry_t *) stored_buffkey.pdata;
  p_stored_handle = (handle_pool_entry_t *) stored_buffval.pdata;

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
