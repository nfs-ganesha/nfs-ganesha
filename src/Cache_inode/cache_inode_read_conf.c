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
 * \file    cache_inode_read_conf.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:21:47 $
 * \version $Revision: 1.11 $
 * \brief   Read the configuration file for the Cache inode initialization.
 *
 * cache_inode_read_conf.c : Read the configuration file for the Cache inode initialization.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "config_parsing.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

static const char *CONF_LABEL_CACHE_INODE_GCPOL = "CacheInode_GC_Policy";
static const char *CONF_LABEL_CACHE_INODE = "CacheInode";
static const char *CONF_LABEL_CACHE_INODE_HASH = "CacheInode_Hash";


/**
 *
 * @brief Read the configuration for the Cache_inode hash table
 *
 * This funcion reads the configuration for the hash table used by the
 * Cache_inode layer.
 *
 * @param[in]  config Configuration file handle
 * @param[out] param  Read parameters
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_NOT_FOUND if stanza is not present.
 * @retval CACHE_INODE_INVALID_ARGUMENT otherwise.
 *
 */
cache_inode_status_t
cache_inode_read_conf_hash_parameter(config_file_t config,
                                     cache_inode_parameter_t *param)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(config == NULL || param == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config,
                                    CONF_LABEL_CACHE_INODE_HASH)) == NULL)
    {
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_CACHE_INODE_HASH);
      return CACHE_INODE_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_CONFIG,
              "Item \"%s\" is expected to be a block",
              CONF_LABEL_CACHE_INODE_HASH);
      return CACHE_INODE_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_CACHE_INODE_HASH);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          param->hparam.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          param->hparam.alphabet_length = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_CACHE_INODE_HASH);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
    }

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_read_conf_hash_parameter */

int parse_cache_expire(cache_inode_expire_type_t *type, time_t *value, char *key_value)
{
  if(key_value[0] >= '0' && key_value[0] <= '9')
    {
      *value = atoi(key_value);
      /* special case backwards compatible meaning of 0 */
      if (*value == 0)
        *type = CACHE_INODE_EXPIRE_NEVER;
      else
        *type = CACHE_INODE_EXPIRE;
      return CACHE_INODE_SUCCESS;
    }

  /*
   * Note the CACHE_INODE_EXPIRE_IMMEDIATE is now a special value that works
   * fine with the value being set to 0, there will not need to be special
   * tests for CACHE_INODE_EXPIRE_IMMEDIATE.
   */
  *value = 0;

  if(strcasecmp(key_value, "Never") == 0)
    *type = CACHE_INODE_EXPIRE_NEVER;
  else if (strcasecmp(key_value, "Immediate") == 0)
    *type = CACHE_INODE_EXPIRE_IMMEDIATE;
  else
    return CACHE_INODE_INVALID_ARGUMENT;

  return CACHE_INODE_SUCCESS;
}

void cache_inode_expire_to_str(cache_inode_expire_type_t type, time_t value, char *out)
{
  switch(type)
    {
      case CACHE_INODE_EXPIRE:
        sprintf(out, "%u", (unsigned int) value);
        break;
      case CACHE_INODE_EXPIRE_NEVER:
        strcpy(out, "Never");
        break;
      case CACHE_INODE_EXPIRE_IMMEDIATE:
        strcpy(out, "Immediate");
        break;
    }
}

/**
 * @brief Read the configuration for the Cache inode layer
 *
 * @param[in]  config Configuration file handle
 * @param[out] param  Read parameters
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_NOT_FOUND if stanza not present
 * @retval CACHE_INODE_INVALID_ARGUMENT otherwise
 */
cache_inode_status_t
cache_inode_read_conf_parameter(config_file_t config,
                                cache_inode_parameter_t *param)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(config == NULL || param == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config,
                                    CONF_LABEL_CACHE_INODE)) == NULL)
    {
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_CACHE_INODE);
      return CACHE_INODE_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_CONFIG,
              "Item \"%s\" is expected to be a block",
              CONF_LABEL_CACHE_INODE);
      return CACHE_INODE_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_CACHE_INODE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      else if(!strcasecmp(key_name, "Attr_Expiration_Time"))
        {
          err = parse_cache_expire(&param->expire_type_attr,
                                   &param->grace_period_attr,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Symlink_Expiration_Time"))
        {
          err = parse_cache_expire(&param->expire_type_link,
                                   &param->grace_period_link,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Directory_Expiration_Time"))
        {
          err = parse_cache_expire(&param->expire_type_dirent,
                                   &param->grace_period_dirent,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Use_Getattr_Directory_Invalidation"))
        {
          param->getattr_dir_invalidation = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Use_Test_Access"))
        {
          if(atoi(key_value) != 1)
            LogWarn(COMPONENT_CACHE_INODE,
                    "Use of %s=%s is deprecated",
                    key_name, key_value);
        }
      else if(!strcasecmp( key_name, "Use_FSAL_Hash" ) )
        {
          param->use_fsal_hash = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "DebugLevel") ||
              !strcasecmp(key_name, "LogFile"))
        {
          LogWarn(COMPONENT_CONFIG,
                  "Deprecated %s option %s=\'%s\'",
                  CONF_LABEL_CACHE_INODE, key_name, key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_CACHE_INODE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
    }

  return CACHE_INODE_SUCCESS;
} /* cache_inode_read_conf_parameter */

/**
 *
 * @brief Read the garbage collection policy
 *
 * This function reads the garbage collection policy from the
 * configuration file.
 *
 * @param[in]  config Configuration file handle
 * @param[out] param  Read parameters
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_NOT_FOUND if stanza is not present.
 * @retval CACHE_INODE_INVALID_ARGUMENT otherwise.
 *
 */
cache_inode_status_t
cache_inode_read_conf_gc_policy(config_file_t config,
                                cache_inode_gc_policy_t *policy)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(config == NULL || policy == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config,
                                    CONF_LABEL_CACHE_INODE_GCPOL)) == NULL)
    {
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_CACHE_INODE_GCPOL);
      return CACHE_INODE_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_CONFIG,
              "Item \"%s\" is expected to be a block",
              CONF_LABEL_CACHE_INODE_GCPOL);
      return CACHE_INODE_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_CACHE_INODE_GCPOL);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
      else if(!strcasecmp(key_name, "Entries_HWMark"))
        {
          policy->entries_hwmark = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Entries_LWMark"))
        {
          policy->entries_lwmark = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Cache_FDs"))
        {
          policy->use_fd_cache = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "LRU_Run_Interval"))
        {
          policy->lru_run_interval = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "FD_Limit_Percent"))
        {
          policy->fd_limit_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "FD_HWMark_Percent"))
        {
          policy->fd_hwmark_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "FD_LWMark_Percent"))
        {
          policy->fd_lwmark_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Reaper_Work"))
        {
          policy->reaper_work = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Biggest_Window"))
        {
          policy->biggest_window = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Required_Progress"))
        {
          policy->required_progress = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Futility_Count"))
        {
          policy->futility_count = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_CACHE_INODE_GCPOL);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

    }

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_read_conf_gc_policy */

/**
 *
 * @brief Prints the garbage collection policy
 *
 * This function prints the garbage collection policy to the supplied
 * stream descriptor.
 *
 * @param[in] output A stream to which to print
 * @param[in] param  The structure to be printed
 *
 */
void cache_inode_print_conf_hash_parameter(FILE *output,
                                           cache_inode_parameter_t *param)
{
  fprintf(output, "CacheInode Hash: Index_Size              = %d\n",
          param->hparam.index_size);
  fprintf(output, "CacheInode Hash: Alphabet_Length         = %d\n",
          param->hparam.alphabet_length);
}                               /* cache_inode_print_conf_hash_parameter */

/**
 *
 * @brief Prints cache inode configuration
 *
 * Prints the cache inode configuration to the supplied stream.
 *
 * @param[in] output A stream to which to print the data
 * @param[in] param  structure to be printed
 */
void cache_inode_print_conf_parameter(FILE *output,
                                      cache_inode_parameter_t *param)
{
  fprintf(output, "CacheInode: Attr_Expiration_Time         = %jd\n",
          param->grace_period_attr);
  fprintf(output, "CacheInode: Symlink_Expiration_Time      = %jd\n",
          param->grace_period_link);
  fprintf(output, "CacheInode: Directory_Expiration_Time    = %jd\n",
          param->grace_period_dirent);
} /* cache_inode_print_conf_parameter */

/**
 *
 * @brief Prints the garbage collection policy.
 *
 * Prints the garbage collection policy.
 *
 * @param[in] output The stream to which to print the data
 * @param[in] param  Structure to be printed
 *
 */
void cache_inode_print_conf_gc_policy(FILE *output,
                                      cache_inode_gc_policy_t *gcpolicy)
{
     fprintf(output,
             "CacheInode_GC_Policy: HWMark_Entries = %d\n"
             "CacheInode_GC_Policy: LWMark_Entries = %d\n"
             "CacheInode_GC_Policy: Cache_FDs = %s\n"
             "CacheInode_GC_Policy: LRU_Run_Interval = %d\n"
             "CacheInode_GC_Policy: FD_Limit_Percent = %d\n"
             "CacheInode_GC_Policy: FD_HWMark_Percent = %d\n"
             "CacheInode_GC_Policy: FD_LWMark_Percent = %d\n"
             "CacheInode_GC_Policy: Reaper_Work = %d\n"
             "CacheInode_GC_Policy: Biggest_Window = %d\n"
             "CacheInode_GC_Policy: Required_Progress = %d\n"
             "CacheInode_GC_Policy: Futility_Count = %d\n",
             gcpolicy->entries_lwmark,
             gcpolicy->entries_hwmark,
             (gcpolicy->use_fd_cache ?
              "TRUE" :
              "FALSE"),
             gcpolicy->lru_run_interval,
             gcpolicy->fd_limit_percent,
             gcpolicy->fd_hwmark_percent,
             gcpolicy->fd_lwmark_percent,
             gcpolicy->reaper_work,
             gcpolicy->biggest_window,
             gcpolicy->required_progress,
             gcpolicy->futility_count);
} /* cache_inode_print_gc_policy */
