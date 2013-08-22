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
 *
 * ---------------------------------------
 */

/**
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file cache_inode_read_conf.c
 * @brief Read the configuration file for the Cache inode initialization.
 */
#include "config.h"
#include "log.h"
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

  int DebugLevel = -1;
  char *LogFile = NULL;

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

      else if(!strcasecmp(key_name, "NParts"))
        {
          param->nparts = atoi(key_value);
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
      else if(!strcasecmp(key_name, "Entries_HWMark"))
        {
          param->entries_hwmark = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "LRU_Run_Interval"))
        {
          param->lru_run_interval = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Cache_FDs"))
        {
          param->use_fd_cache = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "FD_Limit_Percent"))
        {
          param->fd_limit_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "FD_HWMark_Percent"))
        {
          param->fd_hwmark_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "FD_LWMark_Percent"))
        {
          param->fd_lwmark_percent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Reaper_Work"))
        {
          param->reaper_work = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Biggest_Window"))
        {
          param->biggest_window = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Required_Progress"))
        {
          param->required_progress = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Futility_Count"))
        {
          param->futility_count = atoi(key_value);
        }
     else if(!strcasecmp(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if(DebugLevel == -1)
            {
              LogDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_read_conf: ERROR: Invalid debug level name: \"%s\".",
                       key_value);
              return CACHE_INODE_INVALID_ARGUMENT;
            }
        }
      else if(!strcasecmp(key_name, "LogFile"))
        {

          LogFile = key_value;

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_CACHE_INODE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
    }

  /* init logging */
  if(LogFile)
    SetComponentLogFile(COMPONENT_CACHE_INODE, LogFile);

  if(DebugLevel > -1)
    SetComponentLogLevel(COMPONENT_CACHE_INODE, DebugLevel);

  return CACHE_INODE_SUCCESS;
}

/** @} */
