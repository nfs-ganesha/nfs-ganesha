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

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#include "config_parsing.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

/**
 *
 * cache_inode_read_conf_hash_parameter: read the configuration for the hash in Cache_inode layer.
 *
 * Reads the configuration for the hash in Cache_inode layer.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_INODE_SUCCESS if ok, CACHE_INODE_NOT_FOUND is stanza is not there, CACHE_INODE_INVALID_ARGUMENT otherwise.
 *
 */
cache_inode_status_t cache_inode_read_conf_hash_parameter(config_file_t in_config,
                                                          cache_inode_parameter_t *
                                                          pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_INODE_HASH)) == NULL)
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
          pparam->hparam.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hparam.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hparam.nb_node_prealloc = atoi(key_value);
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
 *
 * cache_inode_read_conf_client_parameter: read the configuration for a client to Cache inode layer.
 *
 * Reads the configuration for a client to Cache inode layer (typically a worker thread).
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_INODE_SUCCESS if ok, CACHE_INODE_NOT_FOUND is stanza is not there, CACHE_INODE_INVALID_ARGUMENT otherwise.
 *
 */
cache_inode_status_t cache_inode_read_conf_client_parameter(config_file_t in_config,
                                                            cache_inode_client_parameter_t
                                                            * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  int DebugLevel = -1;
  char *LogFile = NULL;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_INODE_CLIENT)) == NULL)
    {
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_CACHE_INODE_CLIENT);
      return CACHE_INODE_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogCrit(COMPONENT_CONFIG,
              "Item \"%s\" is expected to be a block",
              CONF_LABEL_CACHE_INODE_CLIENT);
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
                  var_index, CONF_LABEL_CACHE_INODE_CLIENT);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "LRU_Prealloc_PoolSize"))
        {
          pparam->lru_param.nb_entry_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "LRU_Nb_Call_Gc_invalid"))
        {
          pparam->lru_param.nb_call_gc_invalid = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Entry_Prealloc_PoolSize"))
        {
          pparam->nb_prealloc_entry = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "ParentData_Prealloc_PoolSize"))
        {
          pparam->nb_pre_parent = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "State_v4_Prealloc_PoolSize"))
        {
          pparam->nb_pre_state_v4 = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Attr_Expiration_Time"))
        {
          err = parse_cache_expire(&pparam->expire_type_attr,
                                   &pparam->grace_period_attr,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Symlink_Expiration_Time"))
        {
          err = parse_cache_expire(&pparam->expire_type_link,
                                   &pparam->grace_period_link,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Directory_Expiration_Time"))
        {
          err = parse_cache_expire(&pparam->expire_type_dirent,
                                   &pparam->grace_period_dirent,
                                   key_value);
          if(err != CACHE_INODE_SUCCESS)
            return err;
        }
      else if(!strcasecmp(key_name, "Use_Getattr_Directory_Invalidation"))
        {
          pparam->getattr_dir_invalidation = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Use_Test_Access"))
        {
          pparam->use_test_access = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Max_Fd"))
        {
          pparam->max_fd_per_thread = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "OpenFile_Retention"))
        {
          pparam->retention = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Use_OpenClose_cache"))
        {
          pparam->use_cache = StrToBoolean(key_value);
        }
      else if(!strcasecmp( key_name, "Use_FSAL_Hash" ) )
        {
          pparam->use_fsal_hash = StrToBoolean(key_value);
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
                  key_name, CONF_LABEL_CACHE_INODE_CLIENT);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
    }

  /* init logging */
  if(LogFile)
    SetComponentLogFile(COMPONENT_CACHE_INODE, LogFile);

  if(DebugLevel > -1)
    SetComponentLogLevel(COMPONENT_CACHE_INODE, DebugLevel);

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_read_conf_client_parameter */

/**
 *
 * cache_inode_read_conf_gc_policy: read the garbage collection policy in configuration file.
 *
 * Reads the garbage collection policy in configuration file.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_INODE_SUCCESS if ok, CACHE_INODE_NOT_FOUND is stanza is not there, CACHE_INODE_INVALID_ARGUMENT otherwise.
 *
 */
cache_inode_status_t cache_inode_read_conf_gc_policy(config_file_t in_config,
                                                     cache_inode_gc_policy_t * ppolicy)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || ppolicy == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_INODE_GCPOL)) == NULL)
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

      if(!strcasecmp(key_name, "File_Lifetime"))
        {
          ppolicy->file_expiration_delay = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Directory_Lifetime"))
        {
          ppolicy->directory_expiration_delay = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NbEntries_HighWater"))
        {
          ppolicy->hwmark_nb_entries = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NbEntries_LowWater"))
        {
          ppolicy->lwmark_nb_entries = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Runtime_Interval"))
        {
          ppolicy->run_interval = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_Call_Before_GC"))
        {
          ppolicy->nb_call_before_gc = atoi(key_value);
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
 * cache_inode_print_conf_gc_policy: prints the garbage collection policy.
 *
 * Prints the garbage collection policy in configuration file.
 *
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_print_conf_hash_parameter(FILE * output, cache_inode_parameter_t param)
{
  fprintf(output, "CacheInode Hash: Index_Size              = %d\n",
          param.hparam.index_size);
  fprintf(output, "CacheInode Hash: Alphabet_Length         = %d\n",
          param.hparam.alphabet_length);
  fprintf(output, "CacheInode Hash: Prealloc_Node_Pool_Size = %d\n",
          param.hparam.nb_node_prealloc);
}                               /* cache_inode_print_conf_hash_parameter */

/**
 *
 * cache_inode_print_conf_client_parameter: prints the client parameter.
 *
 * Prints the client parameters.
 *
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_print_conf_client_parameter(FILE * output,
                                             cache_inode_client_parameter_t param)
{
  fprintf(output, "CacheInode Client: LRU_Prealloc_PoolSize        = %d\n",
          param.lru_param.nb_entry_prealloc);
  fprintf(output, "CacheInode Client: LRU_Nb_Call_Gc_invalid       = %d\n",
          param.lru_param.nb_call_gc_invalid);
  fprintf(output, "CacheInode Client: Entry_Prealloc_PoolSize      = %d\n",
          param.nb_prealloc_entry);
  fprintf(output, "CacheInode Client: ParentData_Prealloc_PoolSize = %d\n",
          param.nb_pre_parent);
  fprintf(output, "CacheInode Client: Attr_Expiration_Time         = %d\n",
          (int)param.grace_period_attr);
  fprintf(output, "CacheInode Client: Symlink_Expiration_Time      = %d\n",
          (int)param.grace_period_link);
  fprintf(output, "CacheInode Client: Directory_Expiration_Time    = %d\n",
          (int)param.grace_period_dirent);
  fprintf(output, "CacheInode Client: Use_Test_Access              = %d\n",
          param.use_test_access);
}                               /* cache_inode_print_conf_client_parameter */

/**
 *
 * cache_inode_print_gc_pol: prints the garbage collection policy.
 *
 * Prints the garbage collection policy.
 *
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_print_conf_gc_policy(FILE * output, cache_inode_gc_policy_t gcpolicy)
{
  fprintf(output, "Garbage Policy: File_Lifetime       = %d\n",
          gcpolicy.file_expiration_delay);
  fprintf(output, "Garbage Policy: Directory_Lifetime  = %d\n",
          gcpolicy.directory_expiration_delay);
  fprintf(output, "Garbage Policy: NbEntries_HighWater = %d\n",
          gcpolicy.hwmark_nb_entries);
  fprintf(output, "Garbage Policy: NbEntries_LowWater  = %d\n",
          gcpolicy.lwmark_nb_entries);
  fprintf(output, "Garbage Policy: Nb_Call_Before_GC   = %d\n",
          gcpolicy.nb_call_before_gc);
  fprintf(output, "Garbage Policy: Runtime_Interval    = %d\n", gcpolicy.run_interval);
}                               /* cache_inode_print_gc_pol */
