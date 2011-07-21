#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "stuff_alloc.h"
#include "fsal.h"
#include "HashTable.h"
#include "log_macros.h"
#include "RW_Lock.h"
#include "nfs4_acls.h"
#include <openssl/md5.h>

static unsigned int nb_pool_prealloc = 1024;

struct prealloc_pool fsal_acl_pool;
static pthread_mutex_t fsal_acl_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

struct prealloc_pool fsal_acl_key_pool;
static pthread_mutex_t fsal_acl_key_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int fsal_acl_hash_both(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef, uint32_t * phashval, uint32_t * prbtval);
static int compare_fsal_acl(hash_buffer_t * p_key1, hash_buffer_t * p_key2);
static int display_fsal_acl_key(hash_buffer_t * p_val, char *outbuff);
static int display_fsal_acl_val(hash_buffer_t * p_val, char *outbuff);

/* DEFAULT PARAMETERS for hash table */

static hash_parameter_t fsal_acl_hash_config = {
  .index_size = 67,
  .alphabet_length = 10,
  .nb_node_prealloc = 1024,
  .hash_func_key = NULL,
  .hash_func_rbt = NULL,
  .hash_func_both = fsal_acl_hash_both,
  .compare_key = compare_fsal_acl,
  .key_to_str = display_fsal_acl_key,
  .val_to_str = display_fsal_acl_val
};

static hash_table_t *fsal_acl_hash = NULL;

/* hash table functions */

static unsigned int fsal_acl_hash_both(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef, uint32_t * phashval, uint32_t * prbtval)
{
  char printbuf[2 * MD5_DIGEST_LENGTH];
  uint32_t h1 = 0 ;
  uint32_t h2 = 0 ;

  char *p_aclkey = (char *) (buffclef->pdata);

  Lookup3_hash_buff_dual((char *)(p_aclkey), MD5_DIGEST_LENGTH, &h1, &h2);

  h1 = h1 % p_hparam->index_size;

  *phashval = h1 ;
  *prbtval = h2 ;

  if(isDebug(COMPONENT_NFS_V4_ACL))
    {
      snprintmem(printbuf, 2 * MD5_DIGEST_LENGTH, p_aclkey, MD5_DIGEST_LENGTH);
      LogDebug(COMPONENT_NFS_V4_ACL, "p_aclkey=%s, hashvalue=%u, rbtvalue=%u", printbuf, h1, h2);
    }

  /* Success */
  return 1 ;
} /*  fsal_acl_hash_both */

static int compare_fsal_acl(hash_buffer_t * p_key1, hash_buffer_t * p_key2)
{
  return memcmp((char *)p_key1->pdata, (char *)p_key2->pdata, MD5_DIGEST_LENGTH);
}

static int display_fsal_acl_key(hash_buffer_t * p_val, char *outbuff)
{
  char printbuf[2 * MD5_DIGEST_LENGTH];
  char *p_aclkey = (char *) p_val->pdata;

  snprintmem(printbuf, 2 * MD5_DIGEST_LENGTH, p_aclkey, MD5_DIGEST_LENGTH);

  return sprintf(outbuff, "%s", printbuf);
}

static int display_fsal_acl_val(hash_buffer_t * p_val, char *outbuff)
{
  return sprintf(outbuff, "not implemented");
}

fsal_ace_t *nfs4_ace_alloc(int nace)
{
  fsal_ace_t *pace = NULL;

  pace = (fsal_ace_t *)Mem_Alloc(nace * sizeof(fsal_ace_t));

  return pace;
}

static fsal_acl_t *nfs4_acl_alloc()
{
  fsal_acl_t *pacl = NULL;

  P(fsal_acl_pool_mutex);
  GetFromPool(pacl, &fsal_acl_pool, fsal_acl_t);
  V(fsal_acl_pool_mutex);

  if(pacl == NULL)
  {
    LogCrit(COMPONENT_NFS_V4_ACL,
            "nfs4_acl_alloc: Can't allocate a new entry from fsal acl pool");
    return NULL;
  }

  return pacl;
}

void nfs4_ace_free(fsal_ace_t *pace)
{
  if(!pace)
    return;

  Mem_Free(pace);
}

static void nfs4_acl_free(fsal_acl_t *pacl)
{
  if(!pacl)
    return;

  P(fsal_acl_pool_mutex);
  ReleaseToPool(pacl, &fsal_acl_pool);
  V(fsal_acl_pool_mutex);
 }

static int nfs4_acldata_2_key(hash_buffer_t * pkey, fsal_acl_data_t *pacldata)
{
  MD5_CTX c;
  fsal_acl_key_t *pacl_key = NULL;

  P(fsal_acl_key_pool_mutex);
  GetFromPool(pacl_key, &fsal_acl_key_pool, fsal_acl_key_t);
  V(fsal_acl_key_pool_mutex);

  if(pacl_key == NULL)
  {
    LogCrit(COMPONENT_NFS_V4_ACL,
            "nfs4_acldata_2_key: Can't allocate a new entry from fsal acl key pool");
    return NFS_V4_ACL_INTERNAL_ERROR;
  }

  MD5_Init(&c);
  MD5_Update(&c, (char *)pacldata->aces, pacldata->naces * sizeof(fsal_ace_t));
  MD5_Final(pacl_key->digest, &c);

  pkey->pdata = (caddr_t) pacl_key;
  pkey->len = sizeof(fsal_acl_key_t);

  return NFS_V4_ACL_SUCCESS;
}

static void nfs4_release_acldata_key(hash_buffer_t *pkey)
{
  fsal_acl_key_t *pacl_key = NULL;

  if(!pkey)
    return;

  pacl_key = (fsal_acl_key_t *)pkey->pdata;

  if(!pacl_key)
    return;

  P(fsal_acl_key_pool_mutex);
  ReleaseToPool(pacl_key, &fsal_acl_key_pool);
  V(fsal_acl_key_pool_mutex);
 }

void nfs4_acl_entry_inc_ref(fsal_acl_t *pacl)
{
  if(!pacl)
    return;

  /* Increase ref counter */
  P_w(&pacl->lock);
  pacl->ref++;
  LogDebug(COMPONENT_NFS_V4_ACL, "nfs4_acl_entry_inc_ref: (acl, ref) = (%p, %u)", pacl, pacl->ref);
  V_w(&pacl->lock);
}

/* Should be called with lock held. */
static void nfs4_acl_entry_dec_ref(fsal_acl_t *pacl)
{
  if(!pacl)
    return;

  /* Decrease ref counter */
  pacl->ref--;
  LogDebug(COMPONENT_NFS_V4_ACL, "nfs4_acl_entry_dec_ref: (acl, ref) = (%p, %u)", pacl, pacl->ref);
}

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *pacldata, fsal_acl_status_t *pstatus)
{
  fsal_acl_t *pacl = NULL;
  hash_buffer_t buffkey;
  hash_buffer_t buffvalue;
  int rc;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *pstatus = NFS_V4_ACL_SUCCESS;

  LogDebug(COMPONENT_NFS_V4_ACL, "nfs4_acl_new_entry: acl hash table size = %u",
           HashTable_GetSize(fsal_acl_hash));

  /* Turn the input to a hash key */
  if(nfs4_acldata_2_key(&buffkey, pacldata))
    {
      *pstatus = NFS_V4_ACL_UNAPPROPRIATED_KEY;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      return NULL;
    }

  /* Check if the entry doesn't already exists */
  if(HashTable_Get(fsal_acl_hash, &buffkey, &buffvalue) == HASHTABLE_SUCCESS)
    {
      /* Entry is already in the cache, do not add it */
      pacl = (fsal_acl_t *) buffvalue.pdata;
      *pstatus = NFS_V4_ACL_EXISTS;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      return pacl;
    }

  /* Adding the entry in the cache */
  pacl = nfs4_acl_alloc();
  if(rw_lock_init(&(pacl->lock)) != 0)
    {
      nfs4_acl_free(pacl);
      LogCrit(COMPONENT_NFS_V4_ACL,
              "nfs4_acl_new_entry: rw_lock_init returned %d (%s)",
              errno, strerror(errno));
      *pstatus = NFS_V4_ACL_INIT_ENTRY_FAILED;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      return NULL;
    }

  pacl->naces = pacldata->naces;
  pacl->aces = pacldata->aces;
  pacl->ref = 0;

  /* Build the value */
  buffvalue.pdata = (caddr_t) pacl;
  buffvalue.len = sizeof(fsal_acl_t);

  if((rc =
      HashTable_Test_And_Set(fsal_acl_hash, &buffkey, &buffvalue,
                             HASHTABLE_SET_HOW_SET_NO_OVERWRITE)) != HASHTABLE_SUCCESS)
    {
      /* Put the entry back in its pool */
      nfs4_acl_free(pacl);
      LogWarn(COMPONENT_NFS_V4_ACL,
              "nfs4_acl_new_entry: entry could not be added to hash, rc=%d",
              rc);

      if( rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS )
       {
         *pstatus = NFS_V4_ACL_HASH_SET_ERROR;

         nfs4_release_acldata_key(&buffkey);

         return NULL;
       }
     else
      {
        LogDebug(COMPONENT_NFS_V4_ACL,
                 "nfs4_acl_new_entry: concurrency detected during acl insertion");

        /* This situation occurs when several threads try to init the same uncached entry
         * at the same time. The first creates the entry and the others got  HASHTABLE_ERROR_KEY_ALREADY_EXISTS
         * In this case, the already created entry (by the very first thread) is returned */
        if((rc = HashTable_Get(fsal_acl_hash, &buffkey, &buffvalue)) != HASHTABLE_SUCCESS)
         {
            *pstatus = NFS_V4_ACL_HASH_SET_ERROR;

            nfs4_release_acldata_key(&buffkey);

            return NULL;
         }

        pacl = (fsal_acl_t *) buffvalue.pdata;
        *pstatus = NFS_V4_ACL_SUCCESS;

        nfs4_release_acldata_key(&buffkey);

        return pacl;
      }
    }

  return pacl;
}

void nfs4_acl_release_entry(fsal_acl_t *pacl, fsal_acl_status_t *pstatus)
{
  fsal_acl_data_t acldata;
  hash_buffer_t key, old_key;
  hash_buffer_t old_value;
  int rc;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *pstatus = NFS_V4_ACL_SUCCESS;

  P_w(&pacl->lock);
  nfs4_acl_entry_dec_ref(pacl);
  if(pacl->ref)
    {
      V_w(&pacl->lock);
      return;
    }
  else
      LogDebug(COMPONENT_NFS_V4_ACL, "nfs4_acl_release_entry: free acl %p", pacl);

  /* Turn the input to a hash key */
  acldata.naces = pacl->naces;
  acldata.aces = pacl->aces;

  if(nfs4_acldata_2_key(&key, &acldata))
  {
    *pstatus = NFS_V4_ACL_UNAPPROPRIATED_KEY;

    nfs4_release_acldata_key(&key);

    V_w(&pacl->lock);

    return;
  }

  /* use the key to delete the entry */
  if((rc = HashTable_Del(fsal_acl_hash, &key, &old_key, &old_value)) != HASHTABLE_SUCCESS)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
              "nfs4_acl_release_entry: entry could not be deleted, status = %d",
              rc);

      nfs4_release_acldata_key(&key);

      *pstatus = NFS_V4_ACL_NOT_FOUND;

      V_w(&pacl->lock);

      return;
    }

  /* Release the hash key data */
  nfs4_release_acldata_key(&old_key);

  /* Sanity check: old_value.pdata is expected to be equal to pacl,
   * and is released later in this function */
  if((fsal_acl_t *) old_value.pdata != pacl)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
              "nfs4_acl_release_entry: unexpected pdata %p from hash table (pacl=%p)",
              old_value.pdata, pacl);
    }

  /* Release the current key */
  nfs4_release_acldata_key(&key);

  V_w(&pacl->lock);

  /* Release acl */
  nfs4_acl_free(pacl);
}

static void nfs4_acls_test()
{
  int i = 0;
  fsal_acl_data_t acldata;
  fsal_ace_t *pace = NULL;
  fsal_acl_t *pacl = NULL;
  fsal_acl_status_t status;

  acldata.naces = 3;
  acldata.aces = nfs4_ace_alloc(3);
  LogDebug(COMPONENT_NFS_V4_ACL, "&acldata.aces = %p", &acldata.aces);

  pace = acldata.aces;

  for(i = 0; i < 3; i++)
    {
      pace->type = i;
      pace->perm = i;
      pace->flag = i;
      pace->who.uid = i;
      pace++;
    }

  pacl = nfs4_acl_new_entry(&acldata, &status);
  nfs4_acl_entry_inc_ref(pacl);
  P_r(&pacl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "pacl = %p, ref = %u, status = %u", pacl, pacl->ref, status);
  V_r(&pacl->lock);

  pacl = nfs4_acl_new_entry(&acldata, &status);
  nfs4_acl_entry_inc_ref(pacl);
  P_r(&pacl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "re-access: pacl = %p, ref = %u, status = %u", pacl, pacl->ref, status);
  V_r(&pacl->lock);

  nfs4_acl_release_entry(pacl, &status);
  P_r(&pacl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "release: pacl = %p, ref = %u, status = %u", pacl, pacl->ref, status);
  V_r(&pacl->lock);

  nfs4_acl_release_entry(pacl, &status);
}

int nfs4_acls_init()
{
  LogDebug(COMPONENT_NFS_V4_ACL, "Initialize NFSv4 ACLs");
  LogDebug(COMPONENT_NFS_V4_ACL, "sizeof(fsal_ace_t)=%lu, sizeof(fsal_acl_t)=%lu", (long unsigned int)sizeof(fsal_ace_t), (long unsigned int)sizeof(fsal_acl_t));

  /* Initialize memory pool of ACLs. */
  MakePool(&fsal_acl_pool, nb_pool_prealloc, fsal_acl_t, NULL, NULL);

  /* Initialize memory pool of ACL keys. */
  MakePool(&fsal_acl_key_pool, nb_pool_prealloc, fsal_acl_key_t, NULL, NULL);

  /* Create hash table. */
  fsal_acl_hash = HashTable_Init(fsal_acl_hash_config);

  if(!fsal_acl_hash)
    {
      LogCrit(COMPONENT_NFS_V4_ACL, "ERROR creating hash table for NFSv4 ACLs");
      return NFS_V4_ACL_INTERNAL_ERROR;
    }

  nfs4_acls_test();

  return NFS_V4_ACL_SUCCESS;
}

