/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include <ctype.h>
#include "log.h"
#include "HashTable.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_nfs4_owner;

uint32_t nfs4_owner_counter = 0;
pthread_mutex_t nfs4_owner_counter_lock = PTHREAD_MUTEX_INITIALIZER;

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
  char *type;
  char *strtmp = str;
  state_nfs4_owner_name_t *pname = buff->addr;

  if(pname->son_islock)
    type = "lock";
  else
    type = "open";

  strtmp += sprintf(strtmp, "clientid=%"PRIx64" %s owner=",
                    pname->son_clientid,
                    type);

  strtmp += DisplayOpaqueValue(pname->son_owner_val,
                               pname->son_owner_len,
                               strtmp);

  return strtmp - str;
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

  strtmp += sprintf(strtmp, "%s %p:",
                    state_owner_type_to_str(owner->so_type),
                    owner);

  strtmp += sprintf(strtmp, " clientid={%"PRIx64"} owner=",
                    owner->so_owner.so_nfs4_owner.so_clientid);

  strtmp += DisplayOpaqueValue(owner->so_owner_val,
                               owner->so_owner_len,
                               strtmp);

  strtmp += sprintf(strtmp, " confirmed=%u counter=%u seqid=%u refcount=%d",
                    owner->so_owner.so_nfs4_owner.so_confirmed,
                    owner->so_owner.so_nfs4_owner.so_counter,
                    owner->so_owner.so_nfs4_owner.so_seqid,
                    owner->so_refcount);

  if(owner->so_owner.so_nfs4_owner.so_related_owner != NULL)
    {
      strtmp += sprintf(strtmp, " related_owner={");
      strtmp += display_nfs4_owner(owner->so_owner.so_nfs4_owner.so_related_owner, strtmp);
      strtmp += sprintf(strtmp, "}");
    }

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
int compare_nfs4_owner(state_owner_t *owner1,
                       state_owner_t *owner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner(owner1, str1);
      display_nfs4_owner(owner2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(owner1 == NULL || owner2 == NULL)
    return 1;

  if(owner1 == owner2)
    return 0;

  if(owner1->so_type == STATE_LOCK_OWNER_NFSV4 &&
     owner2->so_type == STATE_OPEN_OWNER_NFSV4)
    return compare_nfs4_owner(owner1->so_owner.so_nfs4_owner.so_related_owner,
                              owner2);

  if(owner2->so_type == STATE_LOCK_OWNER_NFSV4 &&
     owner1->so_type == STATE_OPEN_OWNER_NFSV4)
    return compare_nfs4_owner(owner2->so_owner.so_nfs4_owner.so_related_owner,
                              owner1);

  if(owner1->so_type != owner2->so_type)
    return 1;

  if(owner1->so_owner.so_nfs4_owner.so_clientid !=
     owner2->so_owner.so_nfs4_owner.so_clientid)
    return 1;

  if(owner1->so_owner_len !=
     owner2->so_owner_len)
    return 1;

  return memcmp(owner1->so_owner_val,
                owner2->so_owner_val,
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
int compare_nfs4_owner_key(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner_key(buff1, str1);
      display_nfs4_owner_key(buff2, str2);
      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_STATE,
                     "{%s} vs {%s}", str1, str2);
    }

  state_nfs4_owner_name_t *pname1 = buff1->addr;
  state_nfs4_owner_name_t *pname2 = buff2->addr;

  if(pname1 == NULL || pname2 == NULL)
    return 1;

  if(pname1->son_islock != pname2->son_islock)
    return 1;

  if(pname1->son_clientid != pname2->son_clientid)
    return 1;

  if(pname1->son_owner_len != pname2->son_owner_len)
    return 1;

  return memcmp(pname1->son_owner_val, pname2->son_owner_val, pname1->son_owner_len);
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
  unsigned long res = 0;

  state_nfs4_owner_name_t *pname = key->addr;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->son_owner_len; i++)
    {
      c = ((char *)pname->son_owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->son_clientid) + (unsigned long)sum + pname->son_owner_len + pname->son_islock;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % hparam->index_size);

  return (unsigned long)(res % hparam->index_size);

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
  state_nfs4_owner_name_t *pname = key->addr;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->son_owner_len; i++)
    {
      c = ((char *)pname->son_owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->son_clientid) + (unsigned long)sum + pname->son_owner_len + pname->son_islock;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}

/**
 * @brief Remove an owner from the hash table
 *
 * @param[in] owner Owner to remove
 * @param[in] str   Arbitrary string for debugging/logging
 */

void remove_nfs4_owner(state_owner_t *owner,
                       const char *str)
{
  struct gsh_buffdesc           buffkey, old_key, old_value;
  state_nfs4_owner_name_t oname;
  int                     rc;

  memset(&oname, 0, sizeof(oname));

  oname.son_clientid  = owner->so_owner.so_nfs4_owner.so_clientid;
  oname.son_owner_len = owner->so_owner_len;
  oname.son_islock    = owner->so_type == STATE_LOCK_OWNER_NFSV4;
  memcpy(oname.son_owner_val, owner->so_owner_val, owner->so_owner_len);

  buffkey.addr = &oname;
  buffkey.len   = sizeof(*owner);

  rc = HashTable_DelRef(ht_nfs4_owner,
                        &buffkey,
                        &old_key,
                        &old_value,
                        Hash_dec_state_owner_ref);

  switch(rc)
    {
      case HASHTABLE_SUCCESS:
        if(owner->so_type == STATE_LOCK_OWNER_NFSV4 &&
           owner->so_owner.so_nfs4_owner.so_related_owner != NULL)
          dec_state_owner_ref(owner->so_owner.so_nfs4_owner.so_related_owner);

        /* Release the owner_name (key) and owner (data) back to appropriate pools */
        LogFullDebug(COMPONENT_STATE, "Free %s", str);

        nfs4_Compound_FreeOne(&owner->so_owner.so_nfs4_owner.so_resp);

        P(owner->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);

        glist_del(&owner->so_owner.so_nfs4_owner.so_perclient);

        V(owner->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);

        dec_client_id_ref(owner->so_owner.so_nfs4_owner.so_clientrec);

        pool_free(state_owner_pool, old_value.addr);
        pool_free(state_nfs4_owner_name_pool, old_key.addr);
        break;

      case HASHTABLE_NOT_DELETED:
        /* ref count didn't end up at 0, don't free. */
        LogDebug(COMPONENT_STATE,
                 "HashTable_DelRef didn't reduce refcount to 0 for %s",
                  str);
        break;

      default:
        /* some problem occurred */
        LogDebug(COMPONENT_STATE,
                 "HashTable_DelRef failed (%s) for %s",
                  hash_table_err_to_str(rc), str);
        break;
    }
}

/**
 * @brief Init the hashtable for NFSv4 owner cache
 *
 * @param[in] param Parameter to init the owner table
 *
 * @retval 0 if successful.
 * @retval -1 if we failed.
 */
int Init_nfs4_owner(nfs4_owner_parameter_t param)
{
  if((ht_nfs4_owner = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NFS Open Owner cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_nfs4_owner */

/**
 * @brief Set an open owner into the hash table
 *
 * @param[in] name  The owner name
 * @param[in] owner The owner record
 *
 * @retval 1 if okay.
 * @retval 0 f not okay.
 *
 */
int nfs4_owner_Set(state_nfs4_owner_name_t *name,
                   state_owner_t *owner)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int rc;

  buffkey.addr = name;
  buffkey.len = sizeof(state_nfs4_owner_name_t);

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  buffval.addr = owner;
  buffval.len = sizeof(state_owner_t);

  P(nfs4_owner_counter_lock);
  nfs4_owner_counter += 1;
  owner->so_owner.so_nfs4_owner.so_counter = nfs4_owner_counter;
  V(nfs4_owner_counter_lock);

  rc = HashTable_Test_And_Set
     (ht_nfs4_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
  if(rc != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "Failed to insert nfs4 owner into hash table rc = %d", rc);
      return 0;
    }

  return 1;
}

/**
 * @brief Get a an NFSv4 open owner
 *
 * @param[in]  name  The owner name
 * @param[out] owner The found owner
 *
 * @retval 1 if okay.
 * @retval 0 if not okay.
 *
 */
int nfs4_owner_Get_Pointer(state_nfs4_owner_name_t *name,
                           state_owner_t **owner)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;

  buffkey.addr = name;
  buffkey.len = sizeof(state_nfs4_owner_name_t);

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_nfs4_owner,
                      &buffkey,
                      &buffval,
                      Hash_inc_state_owner_ref) != HASHTABLE_SUCCESS)
    {
      *owner = NULL;
      LogFullDebug(COMPONENT_STATE,
                   "NOTFOUND");
      return 0;
    }

  *owner = buffval.addr;

  LogFullDebug(COMPONENT_STATE,
               "FOUND");

  return 1;
}

/**
 * @brief Display the NFSv4 owner table
 */
void nfs4_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_nfs4_owner);
}

/**
 * @brief Convert an open_owner to an owner name
 *
 * @param[in]  nfsowner   Open owner as specified in NFS
 * @param[out] name_owner Name used as key in owner table
 * @param[in]  clientid   Request clientid (NFSv4.1)
 */
void convert_nfs4_open_owner(open_owner4 *nfsowner,
                             state_nfs4_owner_name_t *name_owner,
                             clientid4 clientid)
{
  memset(name_owner, 0, sizeof(*name_owner));

  if (clientid)
    {
      name_owner->son_clientid = clientid;
    }
  else
    {
      name_owner->son_clientid  = nfsowner->clientid;
    }
  name_owner->son_owner_len = nfsowner->owner.owner_len;
  name_owner->son_islock = false;
  memcpy(name_owner->son_owner_val,
         nfsowner->owner.owner_val,
         nfsowner->owner.owner_len);
}

/**
 * @brief Convert a lock_owner to an owner name
 *
 * @param[in]  nfsowner   Open owner as specified in NFS
 * @param[out] name_owner Name used as key in owner table
 * @param[in]  clientid   Request clientid (NFSv4.1)
 */
void convert_nfs4_lock_owner(lock_owner4 *nfsowner,
                             state_nfs4_owner_name_t *name_owner,
                             clientid4 clientid)
{
  memset(name_owner, 0, sizeof(*name_owner));

  if (clientid)
    {
      name_owner->son_clientid = clientid;
    }
  else
    {
      name_owner->son_clientid = nfsowner->clientid;
    }
  name_owner->son_owner_len = nfsowner->owner.owner_len;
  name_owner->son_islock = true;
  memcpy(name_owner->son_owner_val,
         nfsowner->owner.owner_val,
         nfsowner->owner.owner_len);
}

/**
 * @brief Create an NFSv4 state owner
 *
 * @param[in] name          Owner name
 * @param[in] clientid      Client record
 * @param[in] type          Owner type
 * @param[in] related_owner For lock owners, the related open owner
 * @param[in] init_seqid    The starting seqid (for NFSv4.0)
 *
 * @return A new state owner or NULL.
 */
state_owner_t *create_nfs4_owner(state_nfs4_owner_name_t *name,
                                 nfs_client_id_t *clientid,
                                 state_owner_type_t type,
                                 state_owner_t *related_owner,
                                 unsigned int init_seqid)
{
  state_owner_t           * owner;
  state_nfs4_owner_name_t * owner_name;

  /* This lock owner is not known yet, allocated and set up a new one */
  owner = pool_alloc(state_owner_pool, NULL);

  if(owner == NULL)
    return NULL;

  owner_name = pool_alloc(state_nfs4_owner_name_pool, NULL);

  if(owner_name == NULL)
    {
      pool_free(state_owner_pool, owner);
      return NULL;
    }

  *owner_name = *name;

  /* set up the content of the open_owner */
  owner->so_type                                 = type;
  owner->so_owner.so_nfs4_owner.so_seqid         = init_seqid;
  owner->so_owner.so_nfs4_owner.so_related_owner = related_owner;
  owner->so_owner.so_nfs4_owner.so_clientid      = name->son_clientid;
  owner->so_owner.so_nfs4_owner.so_clientrec     = clientid;
  owner->so_owner_len                            = name->son_owner_len;
  owner->so_owner.so_nfs4_owner.so_resp.resop    = NFS4_OP_ILLEGAL;
  owner->so_owner.so_nfs4_owner.so_args.argop    = NFS4_OP_ILLEGAL;
  owner->so_refcount                             = 1;
#if 0
  /* WAITING FOR COMMUNITY FIX */
  /* setting lock owner confirmed */
  if (type == STATE_LOCK_OWNER_NFSV4)
    owner->so_owner.so_nfs4_owner.so_confirmed   = 1;
#endif
  init_glist(&owner->so_lock_list);
  init_glist(&owner->so_owner.so_nfs4_owner.so_state_list);

  memcpy(owner->so_owner_val,
         name->son_owner_val,
         name->son_owner_len);

  owner->so_owner_val[owner->so_owner_len] = '\0';

  if(pthread_mutex_init(&owner->so_mutex, NULL) == -1)
    {
      pool_free(state_owner_pool, owner);
      pool_free(state_nfs4_owner_name_pool, owner_name);
      return NULL;
    }

  if(!nfs4_owner_Set(owner_name, owner))
    {
      pool_free(state_owner_pool, owner);
      pool_free(state_nfs4_owner_name_pool, owner_name);
      return NULL;
    }

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(owner, str);
      LogFullDebug(COMPONENT_STATE,
                   "New Owner %s", str);
    }

  /* Increment refcount on related owner */
  if(related_owner != NULL)
    inc_state_owner_ref(related_owner);

  P(clientid->cid_mutex);

  if (type == STATE_OPEN_OWNER_NFSV4)
    {
      /* If open owner, add to clientid lock owner list */
      owner->so_refcount++;
      glist_add_tail(&clientid->cid_openowners,
                     &owner->so_owner.so_nfs4_owner.so_perclient);
    }
  else if(type == STATE_LOCK_OWNER_NFSV4)
    {
      /* If lock owner, add to clientid open owner list */
      owner->so_refcount++;
      glist_add_tail(&clientid->cid_lockowners,
                     &owner->so_owner.so_nfs4_owner.so_perclient);
    }

  /* Increment reference count for clientid record */
  inc_client_id_ref(clientid);

  /* NFSv4.1 does not haver owner confirmation */
  if (clientid->cid_minorversion > 0)
    {
      owner->so_owner.so_nfs4_owner.so_confirmed = 1;
    }

  V(clientid->cid_mutex);

  return owner;
}

/**
 * @brief Fill out an NFSv4 lock conflict
 *
 * @param[out] denied   NFSv4 LOCK4denied structure
 * @param[in]  holder   Holder of the conflicting lock
 * @param[in]  conflict The conflicting lock
 */

void Process_nfs4_conflict(LOCK4denied *denied,
                           state_owner_t *holder,
                           fsal_lock_param_t *conflict)
{
  /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
  denied->offset = conflict->lock_start;
  denied->length = conflict->lock_length;

  if(conflict->lock_type == FSAL_LOCK_R)
    denied->locktype = READ_LT;
  else
    denied->locktype = WRITE_LT;

  if(holder != NULL && holder->so_owner_len != 0)
    denied->owner.owner.owner_val = gsh_malloc(holder->so_owner_len);
  else
    denied->owner.owner.owner_val = NULL;

  LogFullDebug(COMPONENT_STATE,
               "denied->owner.owner.owner_val = %p",
               denied->owner.owner.owner_val);

  if(denied->owner.owner.owner_val != NULL)
    {
      denied->owner.owner.owner_len = holder->so_owner_len;

      memcpy(denied->owner.owner.owner_val,
             holder->so_owner_val,
             holder->so_owner_len);
    }
  else
    {
      denied->owner.owner.owner_len = unknown_owner.so_owner_len;
      denied->owner.owner.owner_val = unknown_owner.so_owner_val;
    }

  if(holder != NULL && holder->so_type == STATE_LOCK_OWNER_NFSV4)
    denied->owner.clientid = holder->so_owner.so_nfs4_owner.so_clientid;
  else
    denied->owner.clientid = 0;

  /* Release any lock owner reference passed back from SAL */
  if(holder != NULL)
    dec_state_owner_ref(holder);
}

/**
 * @brief Release data allocated for LOCK4denied
 *
 * @param[in] denied Structure to release
 */
void Release_nfs4_denied(LOCK4denied *denied)
{
  if(denied->owner.owner.owner_val != unknown_owner.so_owner_val &&
     denied->owner.owner.owner_val != NULL)
    gsh_free(denied->owner.owner.owner_val);
}

/**
 * @brief Deep copy a LOCK4denied
 *
 * @param[out] denied_dst Target
 * @param[in]  denied_src Source
 */
void Copy_nfs4_denied(LOCK4denied *denied_dst,
		      LOCK4denied *denied_src)
{
  memcpy(denied_dst, denied_src, sizeof(*denied_dst));

  if(denied_src->owner.owner.owner_val != unknown_owner.so_owner_val &&
     denied_src->owner.owner.owner_val != NULL)
    {
      denied_dst->owner.owner.owner_val
        = gsh_malloc(denied_src->owner.owner.owner_len);
      LogFullDebug(COMPONENT_STATE,
                   "denied_dst->owner.owner.owner_val = %p",
                   denied_dst->owner.owner.owner_val);
    }

  if(denied_dst->owner.owner.owner_val == NULL)
    {
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
void Copy_nfs4_state_req(state_owner_t *owner,
                         seqid4 seqid,
                         nfs_argop4 *args,
                         compound_data_t *data,
                         nfs_resop4 *resp,
                         const char *tag)
{
  /* Simplify use of this function when we may not be keeping any data for the
   * state owner
   */
  if(owner == NULL)
    return;

  LogFullDebug(COMPONENT_STATE,
               "%s: saving response %p so_seqid %u new seqid %u",
               tag, owner, owner->so_owner.so_nfs4_owner.so_seqid, seqid);

  /* Free previous response */
  nfs4_Compound_FreeOne(&owner->so_owner.so_nfs4_owner.so_resp);

  /* Copy new response */
  nfs4_Compound_CopyResOne(&owner->so_owner.so_nfs4_owner.so_resp, resp);

  /* Deep copy OPEN args? */
  if(owner->so_owner.so_nfs4_owner.so_args.argop == NFS4_OP_OPEN)
    {
    }

  /* Copy bnew args */
  memcpy(&owner->so_owner.so_nfs4_owner.so_args,
         args,
         sizeof(owner->so_owner.so_nfs4_owner.so_args));

  /* Copy new file */
  owner->so_owner.so_nfs4_owner.so_last_entry = data->current_entry;

  /* Deep copy OPEN args? */
  if(args->argop == NFS4_OP_OPEN)
    {
    }

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
bool Check_nfs4_seqid(state_owner_t *owner,
                      seqid4 seqid,
                      nfs_argop4 *args,
                      compound_data_t *data,
                      nfs_resop4 *resp,
                      const char *tag)
{
  seqid4 next;

  /* Check if any owner to verify seqid against */
  if(owner == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
               "%s: Unknown owner doesn't have saved seqid, req seqid %u",
               tag, seqid);
      return true;
    }

  /* If this is a new state owner, client may start with any seqid */
  if(owner->so_owner.so_nfs4_owner.so_last_entry == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
               "%s: New owner %p doesn't have saved seqid, req seqid %u",
               tag, owner, seqid);
      return true;
    }

  /* Check for valid next seqid */
  next = owner->so_owner.so_nfs4_owner.so_seqid + 1;

  LogFullDebug(COMPONENT_STATE,
               "%s: Check owner %p so_seqid %u next %u req seqid %u",
               tag, owner, owner->so_owner.so_nfs4_owner.so_seqid, next, seqid);

  if(seqid == next)
    return true;

  /* All NFS4 responses have the status in the same place, so use any to set NFS4ERR_BAD_SEQID */
  resp->nfs_resop4_u.oplock.status = NFS4ERR_BAD_SEQID;

  /* Now check for valid replay */
  if(owner->so_owner.so_nfs4_owner.so_seqid != seqid)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid %u in request (not replay), expected seqid %u, returning NFS4ERR_BAD_SEQID",
               tag, seqid, owner->so_owner.so_nfs4_owner.so_seqid);
      return false;
    }

  if(args->argop != owner->so_owner.so_nfs4_owner.so_args.argop)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid in request %u (not replay - not same op), returning NFS4ERR_BAD_SEQID",
               tag, seqid);
      return false;
    }

  if(owner->so_owner.so_nfs4_owner.so_last_entry != data->current_entry)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid in request %u (not replay - wrong file), returning NFS4ERR_BAD_SEQID",
               tag, seqid);
      return false;
    }

  // TODO FSF: add more checks here...

  LogDebug(COMPONENT_STATE,
           "%s: Copying saved response for seqid %u",
           tag, seqid);

  /* Copy the saved response and tell caller to use it */
  nfs4_Compound_CopyResOne(resp, &owner->so_owner.so_nfs4_owner.so_resp);

  return false;
}

/**
 * @brief Get the per-clientid state owner
 *
 * @todo FSF: I'm not really sure about the refcounting here...
 *
 * @param[in]  clientid       The clientid to look up
 * @param[out] clientid_owner Matching state owner
 *
 * @return State status.
 */
state_status_t get_clientid_owner(clientid4 clientid,
                                  state_owner_t **clientid_owner)
{
  /* Pointer to client record, retrieved by ID and in which the
     client state owner is stored */
  nfs_client_id_t *pclientid = NULL;

  if ((nfs_client_id_get_confirmed(clientid, &pclientid))
      != CLIENT_ID_SUCCESS)
    {
      return STATE_NOT_FOUND;
    }
  else
    {
      *clientid_owner = &pclientid->cid_owner;
      dec_client_id_ref(pclientid);
      return STATE_SUCCESS;
    }
}
/** @} */
