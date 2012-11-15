/*
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
 *
 * ---------------------------------------
 */

/**
 * @file    idmapper_cache.c
 * @brief   Id mapping cache functions
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

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

#include "common_utils.h"


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
 * (.addr) of the hashbuffer_t.  This union accomplishes that mapping.
 * When used, the length (.len) is expected to be zero: This is not a pointer.
 */

union idmap_val {
	caddr_t id_as_pointer;
	uint32_t real_id;
};

/**
 * @brief Computes the hash value for the entry in id mapper stuff
 *
 * @param[in] hparam Hash table parameter
 * @param[in] key    Hash key buffer
 *
 * @return the computed hash value.
 */
uint32_t idmapper_value_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for(i = 0, c = ((char *)key->addr)[0]; ((char *)key->addr)[i] != '\0';
      c = ((char *)key->addr)[++i], sum += c) ;

  return (unsigned long)(sum % hparam->index_size);
}


uint32_t namemapper_value_hash_func(hash_parameter_t * p_hparam,
				    struct gsh_buffdesc * buffclef)
{
  return ((unsigned long)(buffclef->addr) % p_hparam->index_size);
}

/**
 * @brief Computes the RBT value for the entry in the id mapper stuff
 *
 * @param[in] hparam Hash table parameter
 * @param[in] key    Hash key buffer
 *
 * @return the computed rbt value.
 */
uint64_t idmapper_rbt_hash_func(hash_parameter_t *hparam,
				struct gsh_buffdesc *key)
{
  return idmap_compute_hash_value(key->addr);
}

uint64_t namemapper_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
  return (uint64_t) key->addr;
}

/**
 * @brief Compares the values stored in the key buffers
 *
 * @param[in] buff1 First key
 * @param[in] buff2 Second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 */
int compare_idmapper(struct gsh_buffdesc * buff1, struct gsh_buffdesc * buff2)
{
  return strncmp(buff1->addr, buff2->addr, PWENT_MAX_LEN);
}

int compare_namemapper(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
  unsigned long xid1 = (unsigned long)(buff1->addr);
  unsigned long xid2 = (unsigned long)(buff2->addr);

  return (xid1 == xid2) ? 0 : 1;
}

/**
 * @brief Displays the entry key stored in the buffer
 *
 * @param[in]  pbuff Buffer to display
 * @param[out] str   Output string
 *
 * @return number of character written.
 *
 */
int display_idmapper_key(struct gsh_buffdesc *pbuff, char *str)
{
  if (pbuff->len == 0)
    return sprintf(str, "%"PRIxPTR, (uintptr_t)(pbuff->addr));
  else
    return sprintf(str, "%s", (char *)(pbuff->addr));
}

/**
 * @brief Displays the entry key stored in the buffer.
 *
 * @param[in]  pbuff Buffer to display
 * @param[out] str   Output string
 *
 * @return number of character written.
 *
 */
int display_idmapper_val(struct gsh_buffdesc *pbuff, char *str)
{
  return sprintf(str, "%lu", (unsigned long)(pbuff->addr));
}

/**
 * @brief Inits the hashtable for UID mapping.
 *
 * @param[in] param Parameter used to init the uid map cache
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
}

int uidgidmap_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_uidgid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS UID/GID MAPPER: Cannot init UIDGID_MAP cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}

int idmap_uname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_pwuid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_UNAME cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}

/**
 * @brief Inits the hashtable for GID mapping
 *
 * @param[in] param Parameter used to init the GID map cache
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
}

int idmap_gname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_grgid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_GNAME cache");
      return -1;
    }

  return ID_MAPPER_SUCCESS;
}

/**
 * @brief Computes the hash value, based on the string.
 *
 * @param[in] name String to hash
 *
 * @return The hash value
 */
uint32_t idmap_compute_hash_value(char *name)
{
   uint32_t res ;

   res = Lookup3_hash_buff(name, strlen(name));

   return res;
}

/**
 * @brief Adds a value by key
 *
 * @param[in,out] ht  The hash table to be used
 * @param[in]     key The ip address requested
 * @param[out]    val The value
 *
 * @return ID_MAPPER_SUCCESS, ID_MAPPER_INSERT_MALLOC_ERROR, ID_MAPPER_INVALID_ARGUMENT
 */
int idmap_add(hash_table_t * ht, char *key, uint32_t val)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffdata;
  int rc;
  union idmap_val local_val = {0};

  if(ht == NULL || key == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffkey.addr = gsh_malloc(PWENT_MAX_LEN)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the key */
  strncpy((buffkey.addr), key, PWENT_MAX_LEN);
  buffkey.len = PWENT_MAX_LEN;

  /* Build the value */
  local_val.real_id = val;
  buffdata.addr = local_val.id_as_pointer;
  buffdata.len = sizeof(union idmap_val);
  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following principal->uid mapping: %s->%lu",
	       (char *)buffkey.addr, (unsigned long int)buffdata.addr);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}

int namemap_add(hash_table_t * ht, uint32_t key, char *val)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffdata;
  int rc = 0;
  union idmap_val local_key = {0};

  if(ht == NULL || val == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  if((buffdata.addr = gsh_malloc(PWENT_MAX_LEN)) == NULL)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  /* Build the data */
  strncpy((buffdata.addr), val, PWENT_MAX_LEN);
  buffdata.len = PWENT_MAX_LEN;

  /* Build the key */
  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  LogFullDebug(COMPONENT_IDMAPPER, "Adding the following uid->principal mapping: %lu->%s",
	       (unsigned long int)buffkey.addr, (char *)buffdata.addr);
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;
}

int uidgidmap_add(uid_t key, gid_t value)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffdata;
  int rc = 0;
  union idmap_val local_key = {0};
  union idmap_val local_val = {0};

  /* Build keys and data, no storage is used there, caddr_t pointers are just charged */
  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  local_val.real_id = value;
  buffdata.addr = local_val.id_as_pointer;
  buffdata.len = sizeof(union idmap_val);

  rc = HashTable_Test_And_Set(ht_uidgid, &buffkey, &buffdata,
                              HASHTABLE_SET_HOW_SET_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    return ID_MAPPER_INSERT_MALLOC_ERROR;

  return ID_MAPPER_SUCCESS;

}

static int uidgidmap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing uid->gid mapping: %lu->%lu",
		 (unsigned long)key.addr, (unsigned long)val.addr);
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

static int idmap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
  if (val.addr != NULL)
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing uid->principal mapping: %p->%s",
                 key.addr, (char *)val.addr);

  /* key is just an integer caste to charptr */
  if (val.addr != NULL)
    gsh_free(val.addr);
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

static int namemap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
  if (key.addr != NULL)
    LogFullDebug(COMPONENT_IDMAPPER, "Freeing principal->uid mapping: %s->%p",
                 (char *)key.addr, val.addr);

  /* val is just an integer caste to charptr */
  if (key.addr != NULL)
    gsh_free(key.addr);
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


int uidmap_add(char *key, uid_t val)
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
}

int unamemap_add(uid_t key, char *val)
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
}

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
}

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
}

/**
 * @brief Gets a value by key
 *
 * @param[in]  ht   The hash table to be used
 * @param[in]  key  The IP address requested
 * @param[out] pval The uid/gid.  Always uint32_t
 *
 * @return ID_MAPPER_SUCCESS or ID_MAPPER_NOT_FOUND
 */
int idmap_get(hash_table_t * ht, char *key, uint32_t *pval)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int status;

  if(ht == NULL || key == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.addr = (caddr_t) key;
  buffkey.len = PWENT_MAX_LEN;

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      union idmap_val id;

      id.id_as_pointer = buffval.addr;
      *pval = id.real_id;
      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}

int namemap_get(hash_table_t * ht, uint32_t key, char *pval)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int status;
  union idmap_val local_key = {0};

  if(ht == NULL || pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Get(ht, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      strncpy(pval, (char *)buffval.addr, PWENT_MAX_LEN);

      status = ID_MAPPER_SUCCESS;
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}

int uidgidmap_get(uid_t key, gid_t *pval)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int status;
  union idmap_val local_key = {0};

  if(pval == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Get(ht_uidgid, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      union idmap_val id;

      id.id_as_pointer = buffval.addr;
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

}

int uidmap_get(char *key, uid_t *pval)
{
  return idmap_get(ht_pwnam, key, pval);
}

int unamemap_get(uid_t key, char *val)
{
  return namemap_get(ht_pwuid, key, val);
}

int gidmap_get(char *key, gid_t *pval)
{
  return idmap_get(ht_grnam, key, pval);
}

int gnamemap_get(gid_t key, char *val)
{
  return namemap_get(ht_grgid, key, val);
}

/**
 * @brief Tries to remove an entry for ID_MAPPER
 *
 * @param[in,out] ht  The hash table to be used
 * @param[out]    key The key uncached
 *
 * @return the delete status
 */
int idmap_remove(hash_table_t *ht, char *key)
{
  struct gsh_buffdesc buffkey, old_key;
  int status;

  if(ht == NULL || key == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  buffkey.addr = key;
  buffkey.len = PWENT_MAX_LEN;

  if(HashTable_Del(ht, &buffkey, &old_key, NULL) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      gsh_free(old_key.addr);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}

int namemap_remove(hash_table_t * ht, uint32_t key)
{
  struct gsh_buffdesc buffkey, old_data;
  int status;
  union idmap_val local_key = {0};

  if(ht == NULL)
    return ID_MAPPER_INVALID_ARGUMENT;

  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
  buffkey.len = sizeof(union idmap_val);

  if(HashTable_Del(ht, &buffkey, NULL, &old_data) == HASHTABLE_SUCCESS)
    {
      status = ID_MAPPER_SUCCESS;
      gsh_free(old_data.addr);
    }
  else
    {
      status = ID_MAPPER_NOT_FOUND;
    }

  return status;
}

int uidgidmap_remove(uid_t key)
{
  struct gsh_buffdesc buffkey, old_data;
  int status;
  union idmap_val local_key = {0};

  local_key.real_id = key;
  buffkey.addr = local_key.id_as_pointer;
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
}

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
 * @brief Use the configuration file to populate the ID_MAPPER.
 *
 * @param[in] path    Path for configuration file
 * @param[in] maptype Map to populate
 *
 * Use the configuration file to populate the ID_MAPPER.
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
}

/**
 * @brief Gets the hash table statistics for the idmap and reverse id map
 *
 * @param[in]  maptype        Type of the mapping to be queried (should be UIDMAP_TYPE or GIDMAP_TYPE)
 * @param[out] phstat         Resulting stats for direct map.
 * @param[out] phstat_reverse Resulting stats for reverse map
 *
 * @return nothing (void function)
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

}
