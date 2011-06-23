/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    idmapper_cache.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   Id mapping functions
 *
 * idmapper_cache.c : Id mapping functions, passwd and groups cache management.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pwd.h>
#include <grp.h>

#ifdef _APPLE
#define strnlen( s, l ) strlen( s )
#else
size_t strnlen(const char *s, size_t maxlen);
#endif

/* Hashtable used to cache the hostname, accessed by their IP addess */
hash_table_t *ht_pwnam;
hash_table_t *ht_grnam;
hash_table_t *ht_pwuid;
hash_table_t *ht_grgid;
hash_table_t *ht_uidgid;

/**
 *
 * idmapper_rbt_hash_func: computes the hash value for the entry in id mapper stuff
 *
 * Computes the hash value for the entry in id mapper stuff. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long idmapper_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for(i = 0, c = ((char *)buffclef->pdata)[0]; ((char *)buffclef->pdata)[i] != '\0';
      c = ((char *)buffclef->pdata)[++i], sum += c) ;

  return (unsigned long)(sum % p_hparam->index_size);
}                               /*  ip_name_value_hash_func */


unsigned long namemapper_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
  return ((unsigned long)(buffclef->pdata) % p_hparam->index_size);
}

/**
 *
 * idmapper_rbt_hash_func: computes the rbt value for the entry in the id mapper stuff.
 *
 * Computes the rbt value for the entry in the id mapper stuff.
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long idmapper_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef)
{
  unsigned int result;

  if(idmap_compute_hash_value((char *)buffclef->pdata,
                              (uint32_t *) & result) != ID_MAPPER_SUCCESS)
    return 0;

  return (unsigned long)result;
}                               /* ip_name_rbt_hash_func */

unsigned long namemapper_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  return (unsigned long)(buffclef->pdata);
}

/**
 *
 * compare_idmapper: compares the values stored in the key buffers.
 *
 * Compares the values stored in the key buffers. This function is to be used as 'compare_key' field in
 * the hashtable storing the nfs duplicated requests.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_idmapper(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return strncmp((char *)(buff1->pdata), (char *)(buff2->pdata), PWENT_MAX_LEN);
}                               /* compare_xid */

int compare_namemapper(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  unsigned long xid1 = (unsigned long)(buff1->pdata);
  unsigned long xid2 = (unsigned long)(buff2->pdata);

  return (xid1 == xid2) ? 0 : 1;
}                               /* compare_xid */

/**
 *
 * display_idmapper_key: displays the entry key stored in the buffer.
 *
 * Displays the entry key stored in the buffer. This function is to be used as 'key_to_str' field in
 * the hashtable storing the id mapper stuff
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_idmapper_key(hash_buffer_t * pbuff, char *str)
{
  return sprintf(str, "%s", (char *)(pbuff->pdata));
}                               /* display_idmapper */

/**
 *
 * display_idmapper_val: displays the entry key stored in the buffer.
 *
 * Displays the entry key stored in the buffer. This function is to be used as 'val_to_str' field in
 * the hashtable storing the id mapper stuff
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_idmapper_val(hash_buffer_t * pbuff, char *str)
{
  return sprintf(str, "%lu", (unsigned long)(pbuff->pdata));
}                               /* display_idmapper_val */

/**
 *
 * idmap_uid_init: Inits the hashtable for UID mapping.
 *
 * Inits the hashtable for UID mapping.
 *
 * @param param [IN] parameter used to init the uid map cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int idmap_uid_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_pwnam = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_UID cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int uidgidmap_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_uidgid = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS UID/GID MAPPER: Cannot init UIDGID_MAP cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int idmap_uname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_pwuid = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_UNAME cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

/**
 *
 * idmap_gid_init: Inits the hashtable for GID mapping.
 *
 * Inits the hashtable for GID mapping.
 *
 * @param param [IN] parameter used to init the gid map cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int idmap_gid_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_grnam = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_GID cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int idmap_gname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_grgid = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_GNAME cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

/**
 *
 * idmap_compute_hash_value: computes the hash value, based on the string.
 *
 * Computes the computes the hash value, based on the string.
 *
 */

int idmap_compute_hash_value(char *name, uint32_t * phashval)
{
   uint32_t res ;

   res = Lookup3_hash_buff( name, strlen( name ) ) ;

    return (int)res ;
}

int ___idmap_compute_hash_value(char *name, uint32_t * phashval)
{
  char padded_name[PWENT_MAX_LEN];
  uint64_t computed_value = 0;
  unsigned int i = 0;
  unsigned int offset = 0;
  uint64_t extract = 0;
  uint64_t sum = 0;
  uint64_t i1;
  uint64_t i2;
  uint64_t i3;
  uint64_t i4;
  uint64_t i5;
  uint64_t i6;
  uint64_t i7;
  uint64_t i8;
  uint64_t i9;
  uint64_t l;

  if(name == NULL || phashval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  memset(padded_name, 0, PWENT_MAX_LEN);

  /* Copy the string to the padded one */
  for(i = 0; i < strnlen(name, PWENT_MAX_LEN); padded_name[i] = name[i], i++) ;

#ifdef WITH_PRINTF_DEBUG_PWHASH_COMPUTE
  printf("#%s# :", padded_name);
#endif

  /* For each 9 character pack:
   *   - keep the 7 first bit (the 8th is often 0: ascii string)
   *   - pack 7x9 bit to 63 bits using xor
   *   - xor the last 8th bit to a single 0 , or-ed with the rest
   * Proceeding with the next 9 bytes pack will produce a new value that is xored with the
   * one of the previous iteration */

  for(offset = 0; offset < PWENT_MAX_LEN; offset += 9)
    {
      /* input name is ascii string, remove 8th bit on each byte, not significant */
      i1 = padded_name[offset + 0] & 0x7F;
      i2 = (uint64_t) (padded_name[offset + 1] & 0x7F) << 7;
      i3 = (uint64_t) (padded_name[offset + 2] & 0x7F) << 14;
      i4 = (uint64_t) (padded_name[offset + 3] & 0x7F) << 21;
      i5 = (uint64_t) (padded_name[offset + 4] & 0x7F) << 28;
      i6 = (uint64_t) (padded_name[offset + 5] & 0x7F) << 35;
      i7 = (uint64_t) (padded_name[offset + 6] & 0x7F) << 42;
      i8 = (uint64_t) (padded_name[offset + 7] & 0x7F) << 49;
      i9 = (uint64_t) (padded_name[offset + 8] & 0x7F) << 56;

      sum = (uint64_t) padded_name[offset + 0] +
          (uint64_t) padded_name[offset + 1] +
          (uint64_t) padded_name[offset + 2] +
          (uint64_t) padded_name[offset + 3] +
          (uint64_t) padded_name[offset + 4] +
          (uint64_t) padded_name[offset + 5] +
          (uint64_t) padded_name[offset + 6] +
          (uint64_t) padded_name[offset + 7] + (uint64_t) padded_name[offset + 8];

#ifdef WITH_PRINTF_DEBUG_PWHASH_COMPUTE
      printf("|%llx |%llx |%llx |%llx |%llx |%llx |%llx |%llx |%llx | = ",
             i1, i2, i3, i4, i5, i6, i7, i8, i9);
#endif

      /* Get xor combibation of all the 8h bit */
      l = (padded_name[offset + 0] & 0x80) ^
          (padded_name[offset + 1] & 0x80) ^
          (padded_name[offset + 2] & 0x80) ^
          (padded_name[offset + 3] & 0x80) ^
          (padded_name[offset + 4] & 0x80) ^
          (padded_name[offset + 5] & 0x80) ^
          (padded_name[offset + 6] & 0x80) ^
          (padded_name[offset + 7] & 0x80) ^ (padded_name[offset + 8] & 0x80);

      extract = (i1 ^ i2 ^ i3 ^ i4 ^ i5 ^ i6 ^ i7 ^ i8 ^ i9) | l;

#ifdef WITH_PRINTF_DEBUG_PWHASH_COMPUTE
      printf("%llx ", extract);
#endif

      computed_value ^= extract;
      computed_value ^= sum;
    }
#ifdef WITH_PRINTF_DEBUG_PWHASH_COMPUTE
  printf("\n");
#endif

  computed_value = (computed_value >> 32) + (computed_value & 0x00000000FFFFFFFFLL);

  *phashval = (uint32_t) computed_value;

  return ID_MAPPER_SUCCESS;
}                               /* idmap_compute_hash_value */

/**
 *
 * idmap_add: Adds a value by key
 *
 * Adss a value by key.
 *
 * @param ht       [INOUT] the hash table to be used
 * @param key      [IN]  the ip address requested
 * @param val      [OUT] the value
 *
 * @return ID_MAPPER_SUCCESS, ID_MAPPER_INSERT_MALLOC_ERROR, ID_MAPPER_INVALID_ARGUMENT
 *
 */
int idmap_add(hash_table_t * ht, char *key, unsigned int val)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc;
  unsigned long local_val = (unsigned long)val;

  if(ht == NULL || key == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffkey.pdata = (caddr_t) Mem_Alloc(PWENT_MAX_LEN)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the key */
  strncpy((char *)(buffkey.pdata), key, PWENT_MAX_LEN);
  buffkey.len = PWENT_MAX_LEN;

  /* Build the value */
  buffdata.pdata = (caddr_t) local_val;
  buffdata.len = sizeof(unsigned long);
  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following principal->uid mapping: %s->%lu",
	       (char *)buffkey.pdata, (unsigned long int)buffdata.pdata);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}                               /* idmap_add */

int namemap_add(hash_table_t * ht, unsigned int key, char *val)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0;
  unsigned long local_key = (unsigned long)key;

  if(ht == NULL || val == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffdata.pdata = (caddr_t) Mem_Alloc(PWENT_MAX_LEN)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the data */
  strncpy((char *)(buffdata.pdata), val, PWENT_MAX_LEN);
  buffdata.len = PWENT_MAX_LEN;

  /* Build the value */
  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned int);

  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following uid->principal mapping: %lu->%s",
	       (unsigned long int)buffkey.pdata, (char *)buffdata.pdata);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}                               /* idmap_add */

int uidgidmap_add(unsigned int key, unsigned int value)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0;
  unsigned long local_key = (unsigned long)key;
  unsigned long local_val = (unsigned long)value;

  /* Build keys and data, no storage is used there, caddr_t pointers are just charged */
  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned int);

  buffdata.pdata = (caddr_t) local_val;
  buffdata.len = sizeof(unsigned int);

  rc = HashTable_Test_And_Set(ht_uidgid, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;

}                               /* uidgidmap_add */

static int uidgidmap_free(hash_buffer_t key, hash_buffer_t val)
{
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing uid->gid mapping: %lu->%lu",
		 (unsigned long)key.pdata, (unsigned long)val.pdata);
  return 1;
}

int uidgidmap_clear()
{
  int rc;
  LogInfo(COMPONENT_IDMAPPER, "Clearing all uid->gid map entries.");
  rc = HashTable_Delall(ht_uidgid, uidgidmap_free);
  if (rc != HASHTABLE_SUCCESS)
    return ID_MAPPER_FAIL;
  return ID_MAPPER_SUCCESS;
}

static int idmap_free(hash_buffer_t key, hash_buffer_t val)
{
  if (val.pdata != NULL)
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing uid->principal mapping: %lu->%s",
		 (unsigned long)key.pdata, (char *)val.pdata);

  /* key is just an integer caste to charptr */
  if (val.pdata != NULL)
    Mem_Free(val.pdata);
  return 1;
}

int idmap_clear()
{
  int rc;
  LogInfo(COMPONENT_IDMAPPER, "Clearing all principal->uid map entries.");
  rc = HashTable_Delall(ht_pwuid, idmap_free);
  if (rc != HASHTABLE_SUCCESS)
    return ID_MAPPER_FAIL;
  return ID_MAPPER_SUCCESS;
}

static int namemap_free(hash_buffer_t key, hash_buffer_t val)
{
  if (key.pdata != NULL)
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing principal->uid mapping: %s->%lu",
		 (char *)key.pdata, (unsigned long)val.pdata);

  /* val is just an integer caste to charptr */
  if (key.pdata != NULL)
    Mem_Free(key.pdata);  
  return 1;
}

int namemap_clear()
{
  int rc;
  LogInfo(COMPONENT_IDMAPPER, "Clearing all uid->principal map entries.");
  rc = HashTable_Delall(ht_pwnam, namemap_free);
  if (rc != HASHTABLE_SUCCESS)
    return ID_MAPPER_FAIL;
  return ID_MAPPER_SUCCESS;
}


int uidmap_add(char *key, unsigned int val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = idmap_add(ht_pwnam, key, val);
  rc2 = namemap_add(ht_pwuid, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* uidmap_add */

int unamemap_add(unsigned int key, char *val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = namemap_add(ht_pwuid, key, val);
  rc2 = idmap_add(ht_pwnam, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* unamemap_add */

int gidmap_add(char *key, unsigned int val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = idmap_add(ht_grnam, key, val);
  rc2 = namemap_add(ht_grgid, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* gidmap_add */

int gnamemap_add(unsigned int key, char *val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = namemap_add(ht_grgid, key, val);
  rc2 = idmap_add(ht_grnam, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* gnamemap_add */

/**
 *
 * idmap_get: gets a value by key
 *
 * Gets a value by key.
 *
 * @param ht       [INOUT] the hash table to be used
 * @param key      [IN]  the ip address requested
 * @param hostname [OUT] the hostname
 *
 * @return ID_MAPPER_SUCCESS or ID_MAPPER_NOT_FOUND
 *
 */
int idmap_get(hash_table_t * ht, char *key, unsigned long *pval)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;

  if(ht == NULL || key == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) key;
  buffkey.len = PWENT_MAX_LEN;

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      *pval = (unsigned long)buffval.pdata;

      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_get */

int namemap_get(hash_table_t * ht, unsigned int key, char *pval)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  long local_key = (long)key;

  if(ht == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned long);

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      strncpy(pval, (char *)buffval.pdata, PWENT_MAX_LEN);

      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_get */

int uidgidmap_get(unsigned int key, unsigned int *pval)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  long local_key = (long)key;

  if(pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned long);

  if(HashTable_Get(ht_uidgid, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      memcpy((char *)pval, &(buffval.pdata), sizeof(unsigned int));
      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      /* WIth RPCSEC_GSS, it may be possible that 0 is not mapped to root */
      if(key == 0)
        {
          *pval = 0;
          status = ID_MAPPER_SUCCESS;
        }
      else
        status = ID_MAPPER_NOT_FOUND;
    }

  return status;

}                               /* uidgidmap_get */

int uidmap_get(char *key, unsigned long *pval)
{
  return idmap_get(ht_pwnam, key, pval);
}

int unamemap_get(unsigned int key, char *val)
{
  return namemap_get(ht_pwuid, key, val);
}

int gidmap_get(char *key, unsigned long *pval)
{
  return idmap_get(ht_grnam, key, pval);
}

int gnamemap_get(unsigned int key, char *val)
{
  return namemap_get(ht_grgid, key, val);
}

/**
 *
 * idmap_remove: Tries to remove an entry for ID_MAPPER
 *
 * Tries to remove an entry for ID_MAPPER
 *
 * @param ht            [INOUT] the hash table to be used
 * @param key           [IN]    the key uncached.
 *
 * @return the delete status
 *
 */
int idmap_remove(hash_table_t * ht, char *key)
{
  hash_buffer_t buffkey, old_key;
  int status;

  if(ht == NULL || key == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) key;
  buffkey.len = PWENT_MAX_LEN;

  if(HashTable_Del(ht, &buffkey, &old_key, NULL) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      Mem_Free(old_key.pdata);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_remove */

int namemap_remove(hash_table_t * ht, unsigned int key)
{
  hash_buffer_t buffkey, old_data;
  int status;
  unsigned long local_key = (unsigned long)key;

  if(ht == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned long);

  if(HashTable_Del(ht, &buffkey, NULL, &old_data) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      Mem_Free(old_data.pdata);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_remove */

int uidgidmap_remove(unsigned int key)
{
  hash_buffer_t buffkey, old_data;
  int status;
  unsigned long local_key = (unsigned long)key;

  buffkey.pdata = (caddr_t) local_key;
  buffkey.len = sizeof(unsigned long);

  if(HashTable_Del(ht_uidgid, &buffkey, NULL, &old_data) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* uidgidmap_remove */

int uidmap_remove(char *key)
{
  return idmap_remove(ht_pwnam, key);
}

int unamemap_remove(unsigned int key)
{
  return namemap_remove(ht_pwuid, key);
}

int gidmap_remove(char *key)
{
  return idmap_remove(ht_grnam, key);
}

int gnamemap_remove(unsigned int key)
{
  return namemap_remove(ht_grgid, key);
}

/**
 *
 * idmap_populate_by_conf: Use the configuration file to populate the ID_MAPPER.
 *
 * Use the configuration file to populate the ID_MAPPER.
 *
 *
 */
int idmap_populate(char *path, idmap_type_t maptype)
{
  config_file_t config_file;
  config_item_t block;
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  char label[MAXNAMLEN];
  hash_table_t *ht = NULL;
  hash_table_t *ht_reverse = NULL;
  unsigned int value = 0;
  int rc = 0;

  config_file = config_ParseFile(path);

  if(!config_file)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "Can't open file %s", path);

      return ID_MAPPER_INVALID_ARGUMENT;
    }

  switch (maptype)
    {
    case UIDMAP_TYPE:
      strncpy(label, CONF_LABEL_UID_MAPPER_TABLE, MAXNAMLEN);
      ht = ht_pwnam;
      ht_reverse = ht_pwuid;
      break;

    case GIDMAP_TYPE:
      strncpy(label, CONF_LABEL_GID_MAPPER_TABLE, MAXNAMLEN);
      ht = ht_grnam;
      ht_reverse = ht_grgid;
      break;

    default:
      /* Using incoherent value */
      return ID_MAPPER_INVALID_ARGUMENT;
      break;
    }

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config_file, label)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "Can't get label %s in file %s", label, path);
      return ID_MAPPER_INVALID_ARGUMENT;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_IDMAPPER,
              "Label %s in file %s is expected to be a block", label, path);
      return ID_MAPPER_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, label);
          return ID_MAPPER_INVALID_ARGUMENT;
        }

      value = atoi(key_value);

      if((rc = idmap_add(ht, key_name, value)) != ID_MAPPER_SUCCESS)
        return rc;

      if((rc = namemap_add(ht_reverse, value, key_name)) != ID_MAPPER_SUCCESS)
        return rc;

    }

  /* HashTable_Log( ht ) ; */
  /* HashTable_Log( ht_reverse ) ; */

  return ID_MAPPER_SUCCESS;
}                               /* idmap_populate_by_conf */

/**
 *
 * idmap_get_stats: gets the hash table statistics for the idmap et the reverse id map
 *
 * Gets the hash table statistics for the idmap et the reverse idmap.
 *
 * @param maptype [IN] type of the mapping to be queried (should be UIDMAP_TYPE or GIDMAP_TYPE)
 * @param phstat [OUT] pointer to the resulting stats for direct map.
 * @param phstat [OUT] pointer to the resulting stats for reverse map.
 *
 * @return nothing (void function)
 *
 * @see HashTable_GetStats
 *
 */
void idmap_get_stats(idmap_type_t maptype, hash_stat_t * phstat,
                     hash_stat_t * phstat_reverse)
{
  hash_table_t *ht = NULL;
  hash_table_t *ht_reverse = NULL;

  switch (maptype)
    {
    case UIDMAP_TYPE:
      ht = ht_pwnam;
      ht_reverse = ht_pwuid;
      break;

    case GIDMAP_TYPE:
      ht = ht_grnam;
      ht_reverse = ht_grgid;
      break;

    default:
      /* Using incoherent value */
      return;
      break;
    }

  HashTable_GetStats(ht, phstat);
  HashTable_GetStats(ht_reverse, phstat_reverse);

}                               /* idmap_get_stats */
