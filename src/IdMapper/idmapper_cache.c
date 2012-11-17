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
#include "log.h"
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
 * @brief Overload mapping of uid/gid to buffdata values
 *
 * To save allocating space, uids and gids are overlayed into the value pointer
 * (.pdata) of the hashbuffer_t.  This union accomplishes that mapping.
 */

union idmap_val {
	caddr_t id_as_pointer;
	uint32_t real_id;
};

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
uint32_t idmapper_value_hash_func(hash_parameter_t * p_hparam,
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


uint32_t namemapper_value_hash_func(hash_parameter_t * p_hparam,
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
uint64_t idmapper_rbt_hash_func(hash_parameter_t * p_hparam,
                                hash_buffer_t * buffclef)
{
  unsigned int result;

  if(idmap_compute_hash_value((char *)buffclef->pdata,
                              (uint32_t *) & result) != ID_MAPPER_SUCCESS)
    return 0;

  return (unsigned long)result;
}                               /* ip_name_rbt_hash_func */

uint64_t namemapper_rbt_hash_func(hash_parameter_t * p_hparam,
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
  return strcmp((char *)(buff1->pdata), (char *)(buff2->pdata));
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
  if((ht_pwnam = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_UID cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int uidgidmap_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_uidgid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS UID/GID MAPPER: Cannot init UIDGID_MAP cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int idmap_uname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_pwuid = HashTable_Init(&param.hash_param)) == NULL)
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
  if((ht_grnam = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_GID cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}                               /* idmap_uid_init */

int idmap_gname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_grgid = HashTable_Init(&param.hash_param)) == NULL)
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
int idmap_add(hash_table_t * ht, char *key, uint32_t val)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc;
  union idmap_val local_val = {0};

  if(ht == NULL || key == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffkey.pdata = gsh_strdup(key)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the key */
  buffkey.len = strlen(key);

  /* Build the value */
  local_val.real_id = val;
  buffdata.pdata = local_val.id_as_pointer;
  buffdata.len = sizeof(union idmap_val);
  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following principal->uid mapping: %s->%lu",
	       (char *)buffkey.pdata, (unsigned long int)buffdata.pdata);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}                               /* idmap_add */

int namemap_add(hash_table_t * ht, uint32_t key, char *val)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0;
  union idmap_val local_key = {0};

  if(ht == NULL || val == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffdata.pdata = gsh_strdup(val)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the data */
  buffdata.len = strlen(val);

  /* Build the key */
  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following uid->principal mapping: %lu->%s",
	       (unsigned long int)buffkey.pdata, (char *)buffdata.pdata);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}                               /* idmap_add */

int uidgidmap_add(uid_t key, gid_t value)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  int rc = 0;
  union idmap_val local_key = {0};
  union idmap_val local_val = {0};

  /* Build keys and data, no storage is used there, caddr_t pointers are just charged */
  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  local_val.real_id = value;
  buffdata.pdata = local_val.id_as_pointer;
  buffdata.len = sizeof(union idmap_val);

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
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing uid->principal mapping: %p->%s",
                 key.pdata, (char *)val.pdata);

  /* key is just an integer caste to charptr */
  if (val.pdata != NULL)
    gsh_free(val.pdata);
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
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing principal->uid mapping: %s->%p",
                 (char *)key.pdata, val.pdata);

  /* val is just an integer caste to charptr */
  if (key.pdata != NULL)
    gsh_free(key.pdata);
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


int uidmap_add(char *key, uid_t val, int propagate)
{
  int rc1 = ID_MAPPER_SUCCESS;
  int rc2 = ID_MAPPER_SUCCESS;

  rc1 = idmap_add(ht_pwnam, key, val);
  if(propagate)
    rc2 = namemap_add(ht_pwuid, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* uidmap_add */

int unamemap_add(uid_t key, char *val, int propagate)
{
  int rc1 = ID_MAPPER_SUCCESS;
  int rc2 = ID_MAPPER_SUCCESS;

  rc1 = namemap_add(ht_pwuid, key, val);
  if(propagate)
    rc2 = idmap_add(ht_pwnam, val, key);

  if(rc1 != ID_MAPPER_SUCCESS)
    return rc1;
  else if(rc2 != ID_MAPPER_SUCCESS)
    return rc2;

  return ID_MAPPER_SUCCESS;
}                               /* unamemap_add */

int gidmap_add(char *key, gid_t val)
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

int gnamemap_add(gid_t key, char *val)
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
 * @param pval     [OUT] the uid/gid.  Always uint32_t
 *
 * @return ID_MAPPER_SUCCESS or ID_MAPPER_NOT_FOUND
 *
 */
int idmap_get(hash_table_t * ht, char *key, uint32_t *pval)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;

  if(ht == NULL || key == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) key;
  buffkey.len = strlen(key);

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      union idmap_val id;

      id.id_as_pointer = buffval.pdata;
      *pval = id.real_id;
      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_get */

int namemap_get(hash_table_t * ht, uint32_t key, char *pval, size_t size)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  union idmap_val local_key = {0};

  if(ht == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      strmaxcpy(pval, (char *)buffval.pdata, size);

      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_get */

int uidgidmap_get(uid_t key, gid_t *pval)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  union idmap_val local_key = {0};

  if(pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Get(ht_uidgid, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      union idmap_val id;

      id.id_as_pointer = buffval.pdata;
      *pval = id.real_id;
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

int uidmap_get(char *key, uid_t *pval)
{
  return idmap_get(ht_pwnam, key, pval);
}

int unamemap_get(uid_t key, char *val, size_t size)
{
  return namemap_get(ht_pwuid, key, val, size);
}

int gidmap_get(char *key, gid_t *pval)
{
  return idmap_get(ht_grnam, key, pval);
}

int gnamemap_get(gid_t key, char *val, size_t size)
{
  return namemap_get(ht_grgid, key, val, size);
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

  buffkey.pdata = key;
  buffkey.len = strlen(key);

  if(HashTable_Del(ht, &buffkey, &old_key, NULL) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      gsh_free(old_key.pdata);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_remove */

int namemap_remove(hash_table_t * ht, uint32_t key)
{
  hash_buffer_t buffkey, old_data;
  int status;
  union idmap_val local_key = {0};

  if(ht == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Del(ht, &buffkey, NULL, &old_data) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      gsh_free(old_data.pdata);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}                               /* idmap_remove */

int uidgidmap_remove(uid_t key)
{
  hash_buffer_t buffkey, old_data;
  int status;
  union idmap_val local_key = {0};

  local_key.real_id = key;
  buffkey.pdata = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

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

int unamemap_remove(uid_t key)
{
  return namemap_remove(ht_pwuid, key);
}

int gidmap_remove(char *key)
{
  return idmap_remove(ht_grnam, key);
}

int gnamemap_remove(gid_t key)
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
  const char *label;
  hash_table_t *ht = NULL;
  hash_table_t *ht_reverse = NULL;
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
      label = CONF_LABEL_UID_MAPPER_TABLE;
      ht = ht_pwnam;
      ht_reverse = ht_pwuid;
      break;

    case GIDMAP_TYPE:
      label = CONF_LABEL_GID_MAPPER_TABLE;
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
      uint64_t value = 0;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, label);
          return ID_MAPPER_INVALID_ARGUMENT;
        }
      errno = 0;
      value = strtoul(key_value, NULL, 10);
      if(errno != 0 || value > UINT_MAX)
          return ID_MAPPER_INVALID_ARGUMENT;

      if((rc = idmap_add(ht, key_name, (uint32_t)value)) != ID_MAPPER_SUCCESS)
        return rc;

      if((rc = namemap_add(ht_reverse, (uint32_t)value, key_name)) != ID_MAPPER_SUCCESS)
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
