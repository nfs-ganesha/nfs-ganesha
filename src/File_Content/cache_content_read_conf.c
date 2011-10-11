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
 * \file    cache_content_read_conf.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.10 $
 * \brief   Management of the file content cache: configuration file parsing.
 *
 * cache_content_read_conf.c : Management of the file content cache: configuration file parsing.
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
#include "cache_content.h"
#include "config_parsing.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

char fcc_log_path[MAXPATHLEN];
int fcc_debug_level = -1;

/*
 *
 * cache_content_read_conf_client_parameter: read the configuration for a client to File Content layer.
 * 
 * Reads the configuration for a client to File Content layer (typically a worker thread).
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_CONTENT_SUCCESS if ok, CACHE_CONTENT_INVALID_ARGUMENT otherwise.
 *
 */
cache_content_status_t cache_content_read_conf_client_parameter(config_file_t in_config,
                                                                cache_content_client_parameter_t * pparam)
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
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_CONTENT_CLIENT)) == NULL)
    {
      /* fprintf(stderr, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CACHE_CONTENT_CLIENT ) ; */
      return CACHE_CONTENT_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return CACHE_CONTENT_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          fprintf(stderr,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_CACHE_CONTENT_CLIENT);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "LRU_Prealloc_PoolSize"))     /** @todo: BUGAZOMEU: to be removed */
        {
          //pparam->lru_param.nb_entry_prealloc = atoi( key_value ) ;
        }
      else if(!strcasecmp(key_name, "LRU_Nb_Call_Gc_invalid"))      /** @todo: BUGAZOMEU: to be removed */
        {
          //pparam->lru_param.nb_call_gc_invalid = atoi( key_value ) ;
        }
      else if(!strcasecmp(key_name, "Entry_Prealloc_PoolSize"))      /** @todo: BUGAZOMEU: to be removed */
        {
          pparam->nb_prealloc_entry = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Cache_Directory"))
        {
          strcpy(pparam->cache_dir, key_value);
        }
      else if(!strcasecmp(key_name, "Refresh_FSAL_Force"))
        {
          pparam->flush_force_fsal = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if(DebugLevel == -1)
            {
              LogCrit(COMPONENT_CACHE_CONTENT,
                  "cache_content_read_conf: ERROR: Invalid debug level name: \"%s\".",
                   key_value);
              return CACHE_CONTENT_INVALID_ARGUMENT;
            }
        }
      else if(!strcasecmp(key_name, "LogFile"))
        {
          LogFile = key_value;
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
      else
        {
          fprintf(stderr,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_CACHE_CONTENT_CLIENT);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }
    }

  fcc_debug_level = DebugLevel;
  if(LogFile) {
    LogEvent(COMPONENT_INIT, "Setting log file of emergency cache flush thread to %s",
	     LogFile);
    strncpy(fcc_log_path, LogFile, MAXPATHLEN);
  }
  else {
    LogDebug(COMPONENT_INIT, "No log file set for emergency cache flush thread in configuration. Setting to default.") ;
    strncpy(fcc_log_path, "/dev/null", MAXPATHLEN);
  }

  /* init logging */
  if(LogFile)
    SetComponentLogFile(COMPONENT_CACHE_CONTENT, LogFile);

  if(DebugLevel > -1)
    SetComponentLogLevel(COMPONENT_CACHE_CONTENT, DebugLevel);

  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_read_conf_client_parameter */

/**
 *
 * cache_content_print_conf_client_parameter: prints the client parameter.
 * 
 * Prints the client parameters.
 * 
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed. 
 *
 * @return nothing (void function).
 *
 */
void cache_content_print_conf_client_parameter(FILE * output,
                                               cache_content_client_parameter_t param)
{
  /* @todo BUGAZOMEU virer ces deux lignes */
  //fprintf( output, "FileContent Client: LRU_Prealloc_PoolSize   = %d\n", param.lru_param.nb_entry_prealloc ) ;
  //fprintf( output, "FileContent Client: LRU_Nb_Call_Gc_invalid  = %d\n", param.lru_param.nb_call_gc_invalid ) ;
  fprintf(output, "FileContent Client: Entry_Prealloc_PoolSize = %d\n",
          param.nb_prealloc_entry);
  fprintf(output, "FileContent Client: Cache Directory         = %s\n", param.cache_dir);
}                               /* cache_content_print_conf_client_parameter */

/**
 *
 * cache_content_read_conf_gc_policy: read the garbage collection policy in configuration file.
 *
 * Reads the garbage collection policy in configuration file.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_CONTENT_SUCCESS if ok, CACHE_CONTENT_NOT_FOUND is stanza is not there, CACHE_CONTENT_INVALID_ARGUMENT otherwise.
 *
 */
cache_content_status_t cache_content_read_conf_gc_policy(config_file_t in_config,
                                                         cache_content_gc_policy_t *
                                                         ppolicy)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || ppolicy == NULL)
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_CONTENT_GCPOL)) == NULL)
    {
      /* fprintf(stderr, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CACHE_CONTENT_GCPOL ) ; */
      return CACHE_CONTENT_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return CACHE_CONTENT_INVALID_ARGUMENT;
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
                  var_index, CONF_LABEL_CACHE_CONTENT_GCPOL);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "Lifetime"))
        {
          ppolicy->lifetime = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Runtime_Interval"))
        {
          ppolicy->run_interval = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_Call_Before_GC"))
        {
          ppolicy->nb_call_before_gc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Df_HighWater"))
        {
          ppolicy->hwmark_df = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Df_LowWater"))
        {
          ppolicy->lwmark_df = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Emergency_Grace_Delay"))
        {
          ppolicy->emergency_grace_delay = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_CACHE_CONTENT_GCPOL);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

    }
  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_read_conf_gc_policy */

/**
 *
 * cache_content_print_gc_pol: prints the garbage collection policy.
 *
 * Prints the garbage collection policy.
 *
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed.
 *
 * @return nothing (void function).
 *
 */
void cache_content_print_conf_gc_policy(FILE * output, cache_content_gc_policy_t gcpolicy)
{
  fprintf(output, "Garbage Policy: Lifetime              = %u\n",
          (unsigned int)gcpolicy.lifetime);
  fprintf(output, "Garbage Policy: Df_HighWater          = %u%%\n", gcpolicy.hwmark_df);
  fprintf(output, "Garbage Policy: Df_LowWater           = %u%%\n", gcpolicy.lwmark_df);
  fprintf(output, "Garbage Policy: Emergency Grace Delay = %u\n",
          (unsigned int)gcpolicy.emergency_grace_delay);
  fprintf(output, "Garbage Policy: Nb_Call_Before_GC     = %u\n",
          gcpolicy.nb_call_before_gc);
  fprintf(output, "Garbage Policy: Runtime_Interval      = %u\n", gcpolicy.run_interval);
}                               /* cache_content_print_gc_pol */
