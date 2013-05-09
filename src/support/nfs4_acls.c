#include "config.h"
#include "abstract_mem.h"
#include "fsal.h"
#include "HashTable.h"
#include "log.h"
#include "nfs4_acls.h"
#include <openssl/md5.h>
#include <pthread.h>
#include "lookup3.h"

pool_t *fsal_acl_pool;
pool_t *fsal_acl_key_pool;

static int fsal_acl_hash_both(hash_parameter_t *,
                              struct gsh_buffdesc *,
                              uint32_t *,
                              uint64_t *);
static int compare_fsal_acl(struct gsh_buffdesc *, struct gsh_buffdesc *);
static int display_fsal_acl_key(struct gsh_buffdesc *, char *);
static int display_fsal_acl_val(struct gsh_buffdesc *, char *);

/* DEFAULT PARAMETERS for hash table */

static hash_parameter_t fsal_acl_hash_config = {
  .index_size = 67,
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

static int fsal_acl_hash_both(hash_parameter_t *hparam,
                              struct gsh_buffdesc *key,
                              uint32_t *index,
                              uint64_t *rbthash)
{
  char printbuf[2 * MD5_DIGEST_LENGTH];
  uint32_t h1 = 0;
  uint32_t h2 = 0;

  char *aclkey = (key->addr);

  Lookup3_hash_buff_dual(aclkey, MD5_DIGEST_LENGTH, &h1, &h2);

  h1 = h1 % hparam->index_size;

  *index = h1;
  *rbthash = h2;

  if(isDebug(COMPONENT_NFS_V4_ACL))
    {
      snprintmem(printbuf, 2 * MD5_DIGEST_LENGTH, aclkey, MD5_DIGEST_LENGTH);
      LogDebug(COMPONENT_NFS_V4_ACL, "aclkey=%s, hashvalue=%u, rbtvalue=%u", printbuf, h1, h2);
    }

  /* Success */
  return 1;
} /*  fsal_acl_hash_both */

static int compare_fsal_acl(struct gsh_buffdesc *key1, struct gsh_buffdesc *keya)
{
  return memcmp(key1->addr, keya->addr, MD5_DIGEST_LENGTH);
}

static int display_fsal_acl_key(struct gsh_buffdesc *val, char *outbuff)
{
  char printbuf[2 * MD5_DIGEST_LENGTH];
  char *aclkey = (char *) val->addr;

  snprintmem(printbuf, 2 * MD5_DIGEST_LENGTH, aclkey, MD5_DIGEST_LENGTH);

  return sprintf(outbuff, "%s", printbuf);
}

static int display_fsal_acl_val(struct gsh_buffdesc *val, char *outbuff)
{
  return sprintf(outbuff, "not implemented");
}

fsal_ace_t *nfs4_ace_alloc(int nace)
{
  fsal_ace_t *ace = NULL;

  ace = gsh_calloc(nace, sizeof(fsal_ace_t));
  return ace;
}

static fsal_acl_t *nfs4_acl_alloc()
{
  fsal_acl_t *acl = NULL;

  acl = pool_alloc(fsal_acl_pool, NULL);

  if(acl == NULL)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
	      "Can't allocate a new entry from fsal ACL pool");
      return NULL;
    }

  return acl;
}

void nfs4_ace_free(fsal_ace_t *ace)
{
  if(!ace)
    return;

  LogDebug(COMPONENT_NFS_V4_ACL,
           "free ace %p", ace);

  gsh_free(ace);
}

static void nfs4_acl_free(fsal_acl_t *acl)
{
  if(!acl)
    return;

  if(acl->aces)
    nfs4_ace_free(acl->aces);

  pool_free(fsal_acl_pool, acl);
 }

static int nfs4_acldata_2_key(struct gsh_buffdesc *key, fsal_acl_data_t *acldata)
{
  MD5_CTX c;
  fsal_acl_key_t *acl_key = NULL;

  acl_key = pool_alloc(fsal_acl_key_pool, NULL);

  if(acl_key == NULL)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
	      "Can't allocate a new entry from fsal ACL key pool");
      return NFS_V4_ACL_INTERNAL_ERROR;
    }

  MD5_Init(&c);
  MD5_Update(&c, (char *)acldata->aces, acldata->naces * sizeof(fsal_ace_t));
  MD5_Final(acl_key->digest, &c);

  key->addr = acl_key;
  key->len = sizeof(fsal_acl_key_t);

  return NFS_V4_ACL_SUCCESS;
}

static void nfs4_release_acldata_key(struct gsh_buffdesc *key)
{
  fsal_acl_key_t *acl_key = NULL;

  if(!key)
    return;

  acl_key = key->addr;

  if(!acl_key)
    return;

  pool_free(fsal_acl_key_pool, acl_key);
 }

void nfs4_acl_entry_inc_ref(fsal_acl_t *acl)
{
  /* Increase ref counter */
  PTHREAD_RWLOCK_wrlock(&acl->lock);
  acl->ref++;
  LogDebug(COMPONENT_NFS_V4_ACL,
           "(acl, ref) = (%p, %u)",
           acl, acl->ref);
  PTHREAD_RWLOCK_unlock(&acl->lock);
}

/* Should be called with lock held. */
static void nfs4_acl_entry_dec_ref(fsal_acl_t *acl)
{
  /* Decrease ref counter */
  acl->ref--;
  LogDebug(COMPONENT_NFS_V4_ACL,
           "(acl, ref) = (%p, %u)",
           acl, acl->ref);
}

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *acldata, fsal_acl_status_t *status)
{
  fsal_acl_t        * acl = NULL;
  struct gsh_buffdesc key;
  struct gsh_buffdesc value;
  int                 rc;
  struct hash_latch   latch;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *status = NFS_V4_ACL_SUCCESS;

  /* Turn the input to a hash key */
  if(nfs4_acldata_2_key(&key, acldata))
    {
      *status = NFS_V4_ACL_UNAPPROPRIATED_KEY;

      nfs4_release_acldata_key(&key);

      nfs4_ace_free(acldata->aces);

      return NULL;
    }

  /* Check if the entry already exists */
  rc =  HashTable_GetLatch(fsal_acl_hash,
                           &key,
                           &value,
                           true,
                           &latch);
  if(rc == HASHTABLE_SUCCESS)
    {
      /* Entry is already in the cache, do not add it */
      acl = (fsal_acl_t *) value.addr;
      *status = NFS_V4_ACL_EXISTS;

      nfs4_release_acldata_key(&key);

      nfs4_ace_free(acldata->aces);

      nfs4_acl_entry_inc_ref(acl);

      HashTable_ReleaseLatched(fsal_acl_hash, &latch);

      return acl;
    }

  /* Any other result other than no such key is an error */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      *status = NFS_V4_ACL_INIT_ENTRY_FAILED;

      nfs4_release_acldata_key(&key);

      nfs4_ace_free(acldata->aces);

      return NULL;
    }

  /* Adding the entry in the cache */
  acl = nfs4_acl_alloc();
  if(pthread_rwlock_init(&(acl->lock), NULL) != 0)
    {
      nfs4_acl_free(acl);
      LogCrit(COMPONENT_NFS_V4_ACL,
              "New ACL rw_lock_init returned %d (%s)",
              errno, strerror(errno));
      *status = NFS_V4_ACL_INIT_ENTRY_FAILED;

      nfs4_release_acldata_key(&key);

      nfs4_ace_free(acldata->aces);

      HashTable_ReleaseLatched(fsal_acl_hash, &latch);

      return NULL;
    }

  acl->naces = acldata->naces;
  acl->aces  = acldata->aces;
  acl->ref   = 1;               /* We give out one reference */

  /* Build the value */
  value.addr = acl;
  value.len = sizeof(fsal_acl_t);

  rc = HashTable_SetLatched(fsal_acl_hash,
                            &key,
                            &value,
                            &latch,
                            HASHTABLE_SET_HOW_SET_NO_OVERWRITE,
                            NULL,
                            NULL);

  if(rc != HASHTABLE_SUCCESS)
    {
      /* Put the entry back in its pool */
      nfs4_acl_free(acl);
      LogWarn(COMPONENT_NFS_V4_ACL,
              "New ACL entry could not be added to hash, rc=%s",
              hash_table_err_to_str(rc));

      *status = NFS_V4_ACL_HASH_SET_ERROR;

      nfs4_release_acldata_key(&key);

      return NULL;
    }

  return acl;
}

void nfs4_acl_release_entry(fsal_acl_t *acl, fsal_acl_status_t *status)
{
  fsal_acl_data_t     acldata;
  struct gsh_buffdesc key, old_key;
  struct gsh_buffdesc old_value;
  int                 rc;
  struct hash_latch   latch;

  /* Set the return default to NFS_V4_ACL_SUCCESS */
  *status = NFS_V4_ACL_SUCCESS;

  if (acl == NULL)
    return;

  PTHREAD_RWLOCK_wrlock(&acl->lock);
  if(acl->ref > 1)
    {
      nfs4_acl_entry_dec_ref(acl);
      PTHREAD_RWLOCK_unlock(&acl->lock);
      return;
    }
  else
      LogDebug(COMPONENT_NFS_V4_ACL, "Free ACL %p", acl);

  /* Turn the input to a hash key */
  acldata.naces = acl->naces;
  acldata.aces = acl->aces;

  if(nfs4_acldata_2_key(&key, &acldata))
    {
      *status = NFS_V4_ACL_UNAPPROPRIATED_KEY;

      nfs4_release_acldata_key(&key);

      PTHREAD_RWLOCK_unlock(&acl->lock);

      return;
    }

  PTHREAD_RWLOCK_unlock(&acl->lock);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(fsal_acl_hash,
                          &key,
                          &old_value,
                          true,
                          &latch);

  switch(rc)
    {
      case HASHTABLE_ERROR_NO_SUCH_KEY:
        HashTable_ReleaseLatched(fsal_acl_hash, &latch);
        return;

      case HASHTABLE_SUCCESS:
        PTHREAD_RWLOCK_wrlock(&acl->lock);
        nfs4_acl_entry_dec_ref(acl);
        if(acl->ref != 0)
          {
            /* Did not actually release last reference */
            HashTable_ReleaseLatched(fsal_acl_hash, &latch);
            PTHREAD_RWLOCK_unlock(&acl->lock);
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

  /* Sanity check: old_value.addr is expected to be equal to acl,
   * and is released later in this function */
  if((fsal_acl_t *) old_value.addr != acl)
    {
      LogCrit(COMPONENT_NFS_V4_ACL,
              "Unexpected ACL %p from hash table (acl=%p)",
              old_value.addr, acl);
    }

  /* Release the current key */
  nfs4_release_acldata_key(&key);

  PTHREAD_RWLOCK_unlock(&acl->lock);

  /* Release acl */
  nfs4_acl_free(acl);
}

static void nfs4_acls_test()
{
  int i = 0;
  fsal_acl_data_t acldata, acldata2;
  fsal_ace_t *ace = NULL;
  fsal_acl_t *acl = NULL;
  fsal_acl_status_t status;

  acldata.naces = 3;
  acldata.aces = nfs4_ace_alloc(3);
  LogDebug(COMPONENT_NFS_V4_ACL, "acldata.aces = %p", acldata.aces);

  ace = acldata.aces;

  for(i = 0; i < 3; i++)
    {
      ace->type = i;
      ace->perm = i;
      ace->flag = i;
      ace->who.uid = i;
      ace++;
    }

  acl = nfs4_acl_new_entry(&acldata, &status);
  PTHREAD_RWLOCK_rdlock(&acl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "acl = %p, ref = %u, status = %u", acl, acl->ref, status);
  PTHREAD_RWLOCK_unlock(&acl->lock);

  acldata2.naces = 3;
  acldata2.aces = nfs4_ace_alloc(3);

  LogDebug(COMPONENT_NFS_V4_ACL, "acldata2.aces = %p", acldata2.aces);

  ace = acldata2.aces;

  for(i = 0; i < 3; i++)
    {
      ace->type = i;
      ace->perm = i;
      ace->flag = i;
      ace->who.uid = i;
      ace++;
    }

  acl = nfs4_acl_new_entry(&acldata2, &status);
  PTHREAD_RWLOCK_rdlock(&acl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "re-access: acl = %p, ref = %u, status = %u", acl, acl->ref, status);
  PTHREAD_RWLOCK_unlock(&acl->lock);

  nfs4_acl_release_entry(acl, &status);
  PTHREAD_RWLOCK_rdlock(&acl->lock);
  LogDebug(COMPONENT_NFS_V4_ACL, "release: acl = %p, ref = %u, status = %u", acl, acl->ref, status);
  PTHREAD_RWLOCK_unlock(&acl->lock);

  nfs4_acl_release_entry(acl, &status);
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

