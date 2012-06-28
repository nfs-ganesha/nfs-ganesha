#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "abstract_mem.h"
#include "fsal.h"
#include "HashTable.h"
#include "log.h"
#include "RW_Lock.h"
#include "nfs4_acls.h"
#include <openssl/md5.h>

pool_t *fsal_acl_pool;
static pthread_mutex_t fsal_acl_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

pool_t *fsal_acl_key_pool;
static pthread_mutex_t fsal_acl_key_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static int fsal_acl_hash_both(hash_parameter_t * p_hparam,
                              hash_buffer_t * buffclef,
                              uint32_t * phashval,
                              uint64_t * prbtval);
static int compare_fsal_acl(hash_buffer_t * p_key1, hash_buffer_t * p_key2);
static int display_fsal_acl_key(hash_buffer_t * p_val, char *outbuff);
static int display_fsal_acl_val(hash_buffer_t * p_val, char *outbuff);

/* DEFAULT PARAMETERS for hash table */

static hash_parameter_t fsal_acl_hash_config = {
  .index_size = 67,
  .alphabet_length = 10,
  .hash_func_key = NULL,
  .hash_func_rbt = NULL,
  .hash_func_both = fsal_acl_hash_both,
  .compare_key = compare_fsal_acl,
  .key_to_str = display_fsal_acl_key,
  .val_to_str = display_fsal_acl_val,
  .ht_name = "ACL Table",
  .flags = HT_FLAG_CACHE,
  .ht_log_component = COMPONENT_NFS_V4_ACL
};

static hash_table_t *fsal_acl_hash = NULL;

/* hash table functions */

static int fsal_acl_hash_both(hash_parameter_t *p_hparam,
                              hash_buffer_t *buffclef,
                              uint32_t *phashval,
                              uint64_t *prbtval)
{
  char printbuf[2 * MD5_DIGEST_LENGTH];
  uint32_t h1 = 0 ;
  uint32_t h2 = 0 ;

  char *p_aclkey = (buffclef->pdata);

  Lookup3_hash_buff_dual(p_aclkey, MD5_DIGEST_LENGTH, &h1, &h2);

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

  pace = gsh_calloc(nace, sizeof(fsal_ace_t));
  return pace;
}

static fsal_acl_t *nfs4_acl_alloc()
{
  fsal_acl_t *pacl = NULL;

  pacl = pool_alloc(fsal_acl_pool, NULL);

  if(pacl == NULL)
  {
    LogCrit(COMPONENT_NFS_V4_ACL,
            "Can't allocate a new entry from fsal ACL pool");
    return NULL;
  }

  return pacl;
}

void nfs4_ace_free(fsal_ace_t *pace)
{
  if(!pace)
    return;

  LogDebug(COMPONENT_NFS_V4_ACL,
           "free ace %p", pace);

  gsh_free(pace);
}

static void nfs4_acl_free(fsal_acl_t *pacl)
{
  if(!pacl)
    return;

  if(pacl->aces)
    nfs4_ace_free(pacl->aces);

  P(fsal_acl_pool_mutex);
  pool_free(fsal_acl_pool, pacl);
  V(fsal_acl_pool_mutex);
 }

static int nfs4_acldata_2_key(hash_buffer_t * pkey, fsal_acl_data_t *pacldata)
{
  MD5_CTX c;
  fsal_acl_key_t *pacl_key = NULL;

  pacl_key = pool_alloc(fsal_acl_key_pool, NULL);

  if(pacl_key == NULL)
  {
    LogCrit(COMPONENT_NFS_V4_ACL,
            "Can't allocate a new entry from fsal ACL key pool");
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
  pool_free(fsal_acl_key_pool, pacl_key);
  V(fsal_acl_key_pool_mutex);
 }

void nfs4_acl_entry_inc_ref(fsal_acl_t *pacl)
{
  /* Increase ref counter */
  P_w(&pacl->lock);
  pacl->ref++;
  LogDebug(COMPONENT_NFS_V4_ACL,
           "(acl, ref) = (%p, %u)",
           pacl, pacl->ref);
  V_w(&pacl->lock);
}

/* Should be called with lock held. */
static void nfs4_acl_entry_dec_ref(fsal_acl_t *pacl)
{
  /* Decrease ref counter */
  pacl->ref--;
  LogDebug(COMPONENT_NFS_V4_ACL,
           "(acl, ref) = (%p, %u)",
           pacl, pacl->ref);
}

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *pacldata, fsal_acl_status_t *pstatus)
{
  fsal_acl_t        * pacl = NULL;
  hash_buffer_t       buffkey;
  hash_buffer_t       buffvalue;
  int                 rc;
  struct hash_latch   latch;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *pstatus = NFS_V4_ACL_SUCCESS;

  LogDebug(COMPONENT_NFS_V4_ACL,
           "ACL hash table size=%zu",
           HashTable_GetSize(fsal_acl_hash));

  /* Turn the input to a hash key */
  if(nfs4_acldata_2_key(&buffkey, pacldata))
    {
      *pstatus = NFS_V4_ACL_UNAPPROPRIATED_KEY;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      return NULL;
    }

  /* Check if the entry already exists */
  rc =  HashTable_GetLatch(fsal_acl_hash,
                           &buffkey,
                           &buffvalue,
                           TRUE,
                           &latch);
  if(rc == HASHTABLE_SUCCESS)
    {
      /* Entry is already in the cache, do not add it */
      pacl = (fsal_acl_t *) buffvalue.pdata;
      *pstatus = NFS_V4_ACL_EXISTS;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      nfs4_acl_entry_inc_ref(pacl);

      HashTable_ReleaseLatched(fsal_acl_hash, &latch);

      return pacl;
    }

  /* Any other result other than no such key is an error */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      *pstatus = NFS_V4_ACL_INIT_ENTRY_FAILED;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      return NULL;
    }

  /* Adding the entry in the cache */
  pacl = nfs4_acl_alloc();
  if(rw_lock_init(&(pacl->lock)) != 0)
    {
      nfs4_acl_free(pacl);
      LogCrit(COMPONENT_NFS_V4_ACL,
              "New ACL rw_lock_init returned %d (%s)",
              errno, strerror(errno));
      *pstatus = NFS_V4_ACL_INIT_ENTRY_FAILED;

      nfs4_release_acldata_key(&buffkey);

      nfs4_ace_free(pacldata->aces);

      HashTable_ReleaseLatched(fsal_acl_hash, &latch);

      return NULL;
    }

  pacl->naces = pacldata->naces;
  pacl->aces  = pacldata->aces;
  pacl->ref   = 1;               /* We give out one reference */

  /* Build the value */
  buffvalue.pdata = (caddr_t) pacl;
  buffvalue.len = sizeof(fsal_acl_t);

  rc = HashTable_SetLatched(fsal_acl_hash,
                            &buffkey,
                            &buffvalue,
                            &latch,
                            HASHTABLE_SET_HOW_SET_NO_OVERWRITE,
                            NULL,
                            NULL);

  if(rc != HASHTABLE_SUCCESS)
    {
      /* Put the entry back in its pool */
      nfs4_acl_free(pacl);
      LogWarn(COMPONENT_NFS_V4_ACL,
              "New ACL entry could not be added to hash, rc=%s",
              hash_table_err_to_str(rc));

      *pstatus = NFS_V4_ACL_HASH_SET_ERROR;

      nfs4_release_acldata_key(&buffkey);

      return NULL;
    }

  return pacl;
}

void nfs4_acl_release_entry(fsal_acl_t *pacl, fsal_acl_status_t *pstatus)
{
  fsal_acl_data_t   acldata;
  hash_buffer_t     key, old_key;
  hash_buffer_t     old_value;
  int               rc;
  struct hash_latch latch;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *pstatus = NFS_V4_ACL_SUCCESS;

  if (pacl == NULL)
    return;

  P_w(&pacl->lock);
  if(pacl->ref > 1)
    {
      nfs4_acl_entry_dec_ref(pacl);
      V_w(&pacl->lock);
      return;
    }
  else
      LogDebug(COMPONENT_NFS_V4_ACL, "Free ACL %p", pacl);

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

  V_w(&pacl->lock);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(fsal_acl_hash,
                          &key,
                          &old_value,
                          TRUE,
                          &latch);

  switch(rc)
    {
      case HASHTABLE_ERROR_NO_SUCH_KEY:
        HashTable_ReleaseLatched(fsal_acl_hash, &latch);
        return;

      case HASHTABLE_SUCCESS:
        P_w(&pacl->lock);
        nfs4_acl_entry_dec_ref(pacl);
        if(pacl->ref != 0)
          {
            /* Did not actually release last reference */
            HashTable_ReleaseLatched(fsal_acl_hash, &latch);
            V_w(&pacl->lock);
            return;
          }

        /* use the key to delete the entry */
        rc = HashTable_DeleteLatched(fsal_acl_hash,
                                     &key,
                                     &latch,
                                     &old_key,
                                     &old_value);
        if(rc == HASHTABLE_SUCCESS)
           break;

         /* Fall through to default case */

      default:
          LogCrit(COMPONENT_NFS_V4_ACL,
                  "ACL entry could not be deleted, status=%s",
                  hash_table_err_to_str(rc));
          return;
    }

  /* Release the hash key data */
  nfs4_release_acldata_key(&old_key);

  /* Sanity check: old_value.pdata is expected to be equal to pacl,
   * and is released later in this function */
  if((fsal_acl_t *) old_value.pdata != pacl)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
              "Unexpected ACL %p from hash table (pacl=%p)",
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
  fsal_acl_data_t acldata, acldata2;
  fsal_ace_t *pace = NULL;
  fsal_acl_t *pacl = NULL;
  fsal_acl_status_t status;

  acldata.naces = 3;
  acldata.aces = nfs4_ace_alloc(3);
  LogDebug(COMPONENT_NFS_V4_ACL, "acldata.aces = %p", acldata.aces);

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
  P_r(&pacl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "pacl = %p, ref = %u, status = %u", pacl, pacl->ref, status);
  V_r(&pacl->lock);

  acldata2.naces = 3;
  acldata2.aces = nfs4_ace_alloc(3);

  LogDebug(COMPONENT_NFS_V4_ACL, "acldata2.aces = %p", acldata2.aces);

  pace = acldata2.aces;

  for(i = 0; i < 3; i++)
    {
      pace->type = i;
      pace->perm = i;
      pace->flag = i;
      pace->who.uid = i;
      pace++;
    }

  pacl = nfs4_acl_new_entry(&acldata2, &status);
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
  LogDebug(COMPONENT_NFS_V4_ACL,
           "sizeof(fsal_ace_t)=%zu, sizeof(fsal_acl_t)=%zu",
           sizeof(fsal_ace_t), sizeof(fsal_acl_t));

  /* Initialize memory pool of ACLs. */
  fsal_acl_pool = pool_init(NULL, sizeof(fsal_acl_t),
                            pool_basic_substrate,
                            NULL, NULL, NULL);

  /* Initialize memory pool of ACL keys. */
  fsal_acl_key_pool = pool_init(NULL, sizeof(fsal_acl_key_t),
                                pool_basic_substrate,
                                NULL, NULL, NULL);

  /* Create hash table. */
  fsal_acl_hash = HashTable_Init(&fsal_acl_hash_config);

  if(!fsal_acl_hash)
    {
      LogCrit(COMPONENT_NFS_V4_ACL, "ERROR creating hash table for NFSv4 ACLs");
      return NFS_V4_ACL_INTERNAL_ERROR;
    }

  nfs4_acls_test();

  return NFS_V4_ACL_SUCCESS;
}

