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
 * @addtogroup idmapper
 * @{
 */

/**
 * @file    idmapper_cache.c
 * @brief   Id mapping cache functions
 */
#include "config.h"
#include "HashTable.h"
#include "lookup3.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pwd.h>
#include <grp.h>
#include "idmapper.h"
#include "common_utils.h"
#include "city.h"


/* Hashtable used to cache the hostname, accessed by their IP addess */
hash_table_t *ht_pwnam;
hash_table_t *ht_grnam;
hash_table_t *ht_pwuid;
hash_table_t *ht_grgid;
hash_table_t *ht_uidgid;

/**
 * @brief Computes the hash value for the entry in id mapper stuff
 *
 * @param[in]  hparam  Hash table parameter
 * @param[in]  key     Hash key buffer
 * @param[out] index   Partition index
 * @param[out] rbthash Key in the red-black tree
 *
 * @return Constantly 1.
 */
int idmapper_hash_func(hash_parameter_t *hparam,
		       struct gsh_buffdesc *key,
		       uint32_t *index,
		       uint64_t *rbthash)
{
  *rbthash = CityHash64(key->addr, key->len);
  *index = *rbthash % hparam->index_size;

  return 1;
}


uint32_t namemapper_value_hash_func(hash_parameter_t * p_hparam,
				    struct gsh_buffdesc * buffclef)
{
  return ((unsigned long)(buffclef->addr) % p_hparam->index_size);
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
 * @return 0 if keys are identical, -1 or 1 if not.
 */
int compare_idmapper(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
  return memcmp(buff1->addr, buff2->addr, MAX(buff1->len, buff2->len));
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
    return sprintf(str, "%"PRIdPTR, (uintptr_t)(pbuff->addr));
  else
    return sprintf(str, "%.*s",
		   (int)pbuff->len,
		   (char *)(pbuff->addr));
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
  return sprintf(str, "%"PRIuPTR, (uintptr_t)pbuff->addr);
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

  return 0;
}

int uidgidmap_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_uidgid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS UID/GID MAPPER: Cannot init UIDGID_MAP cache");
      return -1;
    }

  return 0;
}

int idmap_uname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_pwuid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_UNAME cache");
      return -1;
    }

  return 0;
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

  return 0;
}

int idmap_gname_init(nfs_idmap_cache_parameter_t param)
{
  if((ht_grgid = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "NFS ID MAPPER: Cannot init IDMAP_GNAME cache");
      return -1;
    }

  return 0;
}

/**
 * @brief Adds a value by key
 *
 * @param[in,out] ht   The hash table to be used
 * @param[in]     name The name to add
 * @param[out]    val  The value
 *
 * @return 0, or -1
 */
int idmap_add(hash_table_t *ht, const struct gsh_buffdesc *name,
	      uint32_t val)
{
  struct gsh_buffdesc key;
  struct gsh_buffdesc data;
  int rc;
  union idmap_val local_val = {0};

  key.len = name->len;
  key.addr = gsh_malloc(name->len);

  if (key.addr == NULL)
    return -1;

  /* Build the key */
  memcpy(key.addr, name->addr, name->len);

  /* Build the value */
  local_val.real_id = val;
  data.addr = local_val.id_as_pointer;
  data.len = sizeof(union idmap_val);
  rc = HashTable_Test_And_Set(ht, &key, &data,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      gsh_free(key.addr);
      return -1;
    }

  if (rc == HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      gsh_free(key.addr);
    }

  return 0;
}

int namemap_add(hash_table_t *ht, uint32_t id,
		const struct gsh_buffdesc *val)
{
  struct gsh_buffdesc data;
  int rc = 0;
  union idmap_val local_key =
    {
      .real_id = id
    };
  struct gsh_buffdesc key =
    {
      .addr = local_key.id_as_pointer,
      .len = sizeof(union idmap_val)
    };

  data.addr = gsh_malloc(val->len);
  if (data.addr == NULL)
    return -1;

  /* Build the data */
  memcpy(data.addr, val->addr, val->len);
  data.len = val->len;

  rc = HashTable_Test_And_Set(ht, &key, &data,
			      HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      gsh_free(data.addr);
      return -1;
    }

  if (rc == HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      gsh_free(data.addr);
    }

  return 0;
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
    return -1;

  return 0;
}

static int uidgidmap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
  return 1;
}

int uidgidmap_clear()
{
  int rc;
  LogInfo(COMPONENT_IDMAPPER, "Clearing all uid->gid map entries.");
  rc = HashTable_Delall(ht_uidgid, uidgidmap_free);

  if (rc != HASHTABLE_SUCCESS)
    return -1;

  return 0;
}

static int idmap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
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
    return -1;

  return 0;
}

static int namemap_free(struct gsh_buffdesc key, struct gsh_buffdesc val)
{
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
    return -1;

  return 0;
}

int uidmap_add(const struct gsh_buffdesc *name, uid_t val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = idmap_add(ht_pwnam, name, val);
  rc2 = namemap_add(ht_pwuid, val, name);

  if(rc1 != 0)
    return rc1;
  else if(rc2 != 0)
    return rc2;

  return 0;
}

int unamemap_add(uid_t key, const struct gsh_buffdesc *val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = namemap_add(ht_pwuid, key, val);
  rc2 = idmap_add(ht_pwnam, val, key);

  if(rc1 != 0)
    return rc1;
  else if(rc2 != 0)
    return rc2;

  return 0;
}

int gidmap_add(const struct gsh_buffdesc *key, gid_t val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = idmap_add(ht_grnam, key, val);
  rc2 = namemap_add(ht_grgid, val, key);

  if(rc1 != 0)
    return rc1;
  else if(rc2 != 0)
    return rc2;

  return 0;
}

int gnamemap_add(gid_t key, const struct gsh_buffdesc *val)
{
  int rc1 = 0;
  int rc2 = 0;

  rc1 = namemap_add(ht_grgid, key, val);
  rc2 = idmap_add(ht_grnam, val, key);

  if(rc1 != 0)
    return rc1;
  else if(rc2 != 0)
    return rc2;

  return 0;
}

/**
 * @brief Gets a value by key
 *
 * @param[in]  ht   The hash table to be used
 * @param[in]  key  The IP address requested
 * @param[out] val The uid/gid.  Always uint32_t
 *
 * @return 0 on success or -1 on failure.
 */

int idmap_get(hash_table_t *ht, const struct gsh_buffdesc *key,
	      uint32_t *val)
{
  struct gsh_buffdesc data;
  union idmap_val id;

  if(HashTable_Get(ht, key, &data) != HASHTABLE_SUCCESS)
    {
      return -1;
    }

  id.id_as_pointer = data.addr;
  *val = id.real_id;
  return 0;
}

int uidgidmap_get(uid_t uid, gid_t *gid)
{
  struct gsh_buffdesc key;
  struct gsh_buffdesc val;
  union idmap_val local_key = {0};
  union idmap_val id;

  local_key.real_id = uid;
  key.addr = local_key.id_as_pointer;
  key.len = sizeof(union idmap_val);

  if(HashTable_Get(ht_uidgid, &key, &val) != HASHTABLE_SUCCESS)
    {
      /* WIth RPCSEC_GSS, it may be possible that 0 is not mapped to root */
      if(uid == 0)
        {
          *gid = 0;
          return 0;
        }
      else
        return -1;
    }

  id.id_as_pointer = val.addr;
  *gid = id.real_id;

  return 0;
}

int uidmap_get(const struct gsh_buffdesc *key, uid_t *val)
{
  return idmap_get(ht_pwnam, key, val);
}

int gidmap_get(const struct gsh_buffdesc *key, gid_t *val)
{
  return idmap_get(ht_grnam, key, val);
}

/**
 * @brief Tries to remove an entry for ID_MAPPER
 *
 * @param[in,out] ht   The hash table to be used
 * @param[out]    name The name uncached
 *
 * @return 0 on success, -1 on error
 */

int idmap_remove(hash_table_t *ht,
		 const struct gsh_buffdesc *key)
{
  struct gsh_buffdesc old_key;

  if(HashTable_Del(ht, key, &old_key, NULL) != HASHTABLE_SUCCESS)
    return -1;

  gsh_free(old_key.addr);
  return 0;
}

int namemap_remove(hash_table_t *ht, uint32_t id)
{
  struct gsh_buffdesc key, old_data;
  union idmap_val local_key = {0};

  local_key.real_id = id;
  key.addr = local_key.id_as_pointer;
  key.len = sizeof(union idmap_val);

  if(HashTable_Del(ht, &key, NULL, &old_data) != HASHTABLE_SUCCESS)
    {
      return -1;
    }

  gsh_free(old_data.addr);
  return 0;
}

int uidgidmap_remove(uid_t uid)
{
  union idmap_val local_key =
    {
      .real_id = uid
    };
  struct gsh_buffdesc key =
    {
      .addr = local_key.id_as_pointer,
      .len = sizeof(union idmap_val)
    };
  struct gsh_buffdesc old_data;

  if(HashTable_Del(ht_uidgid, &key, NULL, &old_data) != HASHTABLE_SUCCESS)
    return -1;

  return 0;
}

int uidmap_remove(const struct gsh_buffdesc *key)
{
  return idmap_remove(ht_pwnam, key);
}

int unamemap_remove(uid_t key)
{
  return namemap_remove(ht_pwuid, key);
}

int gidmap_remove(const struct gsh_buffdesc *key)
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

      return -1;
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
      return -1;
      break;
    }

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config_file, label)) == NULL)
    {
      LogCrit(COMPONENT_IDMAPPER,
              "Can't get label %s in file %s", label, path);
      return -1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_IDMAPPER,
              "Label %s in file %s is expected to be a block", label, path);
      return -1;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;
      uint64_t value = 0;
      struct gsh_buffdesc key_buff;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_IDMAPPER,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, label);
          return -1;
        }
      key_buff.addr = key_name;
      key_buff.len = strlen(key_name);
      errno = 0;
      value = strtoul(key_value, NULL, 10);
      if(errno != 0 || value > UINT_MAX)
          return -1;

      if((rc = idmap_add(ht, &key_buff, (uint32_t)value)) != 0)
        return rc;

      if((rc = namemap_add(ht_reverse, (uint32_t)value, &key_buff)) != 0)
        return rc;

    }

  /* HashTable_Log( ht ) ; */
  /* HashTable_Log( ht_reverse ) ; */

  return 0;
}

/**
 * @brief Gets the hash table statistics for the idmap and reverse id map
 *
 * @param[in]  maptype        Type of the mapping to be queried (should be UIDMAP_TYPE or GIDMAP_TYPE)
 * @param[out] phstat         Resulting stats for direct map.
 * @param[out] phstat_reverse Resulting stats for reverse map
 *
 * @todo Delendum est
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
/** @} */
