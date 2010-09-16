/*
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
 * \file    nfs_read_conf.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/07 14:28:00 $
 * \version $Revision: 1.10 $
 * \brief   This file that contain the routine required for parsing the NFS specific configuraion file.
 *
 * nfs_read_conf.c : This file that contain the routine required for parsing the NFS specific configuraion file.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_read_conf.c,v 1.10 2005/12/07 14:28:00 deniel Exp $
 *
 * $Log: nfs_read_conf.c,v $
 * Revision 1.10  2005/12/07 14:28:00  deniel
 * Support of stats via stats_thread was added
 *
 * Revision 1.9  2005/11/30 15:41:15  deniel
 * Added IP/stats conf
 *
 * Revision 1.8  2005/11/29 13:38:18  deniel
 * bottlenecked ip_stats
 *
 * Revision 1.6  2005/11/08 15:22:24  deniel
 * WildCard and Netgroup entry for exportlist are now supported
 *
 * Revision 1.5  2005/10/10 14:27:54  deniel
 * mnt_Mnt does not create root entries in Cache inode any more. This is done before the first request
 * once the export list is read the first time .
 *
 * Revision 1.4  2005/10/07 07:34:00  deniel
 * Added default parameters support to be able to manage 'simplified' config file
 *
 * Revision 1.3  2005/08/11 12:37:28  deniel
 * Added statistics management
 *
 * Revision 1.2  2005/08/03 13:14:00  deniel
 * memset to zero before building the filehandles
 *
 * Revision 1.1  2005/08/03 06:57:54  deniel
 * Added a libsupport for miscellaneous service functions
 *
 * Revision 1.1  2005/07/18 14:12:45  deniel
 * Fusion of the dirrent layers in progreess via the implementation of mount protocol
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_functions.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"

/**
 *
 * nfs_read_worker_conf: read the configuration ite; for the worker theads.
 * 
 * Reads the configuration ite; for the worker theads.
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 *
 */
int nfs_read_worker_conf(config_file_t in_config, nfs_worker_parameter_t * pparam)
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
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_WORKER)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_WORKER  ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_WORKER);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "Pending_Job_Prealloc"))
        {
          pparam->nb_pending_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_Before_GC"))
        {
          pparam->nb_before_gc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_DupReq_Prealloc"))
        {
          pparam->nb_dupreq_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_DupReq_Before_GC"))
        {
          pparam->nb_dupreq_before_gc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_Client_Id_Prealloc"))
        {
          pparam->nb_client_id_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_IP_Stats_Prealloc"))
        {
          pparam->nb_ip_stats_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "LRU_Pending_Job_Prealloc_PoolSize"))
        {
          pparam->lru_param.nb_entry_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "LRU_DupReq_Prealloc_PoolSize"))
        {
          pparam->lru_dupreq.nb_entry_prealloc = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_WORKER);
          return -1;
        }

    }

  return 0;
}                               /* nfs_read_worker_conf */

/**
 *
 * nfs_read_core_conf: read the configuration ite; for the worker theads.
 * 
 * Reads the configuration ite; for the worker theads.
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok, -1 if failed, 1 is stanza is not there.
 *
 */
int nfs_read_core_conf(config_file_t in_config, nfs_core_parameter_t * pparam)
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
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_CORE)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_CORE  ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_CORE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "Nb_Worker"))
        {
          pparam->nb_worker = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_MaxConcurrentGC"))
        {
          pparam->nb_max_concurrent_gc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DupReq_Expiration"))
        {
          pparam->expiration_dupreq = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Use_NFS_Commit"))
        {
          pparam->use_nfs_commit = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "NFS_Port"))
        {
          pparam->nfs_port = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Drop_IO_Errors"))
        {
          pparam->drop_io_errors = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Drop_Inval_Errors"))
        {
          pparam->drop_inval_errors = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "MNT_Port"))
        {
          pparam->mnt_port = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NFS_Program"))
        {
          pparam->nfs_program = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "MNT_Program"))
        {
          pparam->mnt_program = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NLM_Program"))
        {
          pparam->nlm_program = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Rquota_Program"))
        {
          pparam->rquota_program = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Rquota_Port"))
        {
          pparam->rquota_port = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Bind_Addr"))
        {
          int rc;
          memset(&pparam->bind_addr.sin_addr, 0, sizeof(pparam->bind_addr.sin_addr));
          rc = inet_pton(AF_INET, key_value, &pparam->bind_addr.sin_addr);
          if(rc <= 0)
            {
              /* Revert to INADDR_ANY in case of any error */
              pparam->bind_addr.sin_addr.s_addr = INADDR_ANY;   /* All the interfaces on the machine are used */
            }
        }
      else if(!strcasecmp(key_name, "Core_Dump_Size"))
        {
          pparam->core_dump_size = atol(key_value);
        }
      else if(!strcasecmp(key_name, "Nb_Max_Fd"))
        {
          pparam->nb_max_fd = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Stats_File_Path"))
        {
          strncpy(pparam->stats_file_path, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "Stats_Update_Delay"))
        {
          pparam->stats_update_delay = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Dump_Stats_Per_Client"))
        {
          pparam->dump_stats_per_client = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Stats_Per_Client_Directory"))
        {
          strncpy(pparam->stats_per_client_directory, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "FSAL_Shared_Library"))
        {
          strncpy(pparam->fsal_shared_library, key_value, MAXPATHLEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_CORE);
          return -1;
        }

    }

  return 0;
}                               /* nfs_read_core_conf */

/**
 *
 * nfs_read_dupreq_hash_conf: reads the configuration for the hash in Duplicate Request layer.
 * 
 * Reads the configuration for the hash in Duplicate Request layer
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_dupreq_hash_conf(config_file_t in_config,
                              nfs_rpc_dupreq_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_DUPREQ)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_DUPREQ ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_DUPREQ);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_DUPREQ);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_dupreq_hash_conf */

/**
 *
 * nfs_read_ip_name_conf: reads the configuration for the IP/name.
 * 
 * Reads the configuration for the IP/name.
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_ip_name_conf(config_file_t in_config, nfs_ip_name_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_IP_NAME)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_IP_NAME ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_IP_NAME);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Expiration_Time"))
        {
          pparam->expiration_time = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Map"))
        {
          strncpy(pparam->mapfile, key_value, MAXPATHLEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_IP_NAME);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_ip_name_conf */

/**
 *
 * nfs_read_ip_name_conf: reads the configuration for the Client/ID Cache
 *
 * Reads the configuration for the Client/ID Cache
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_client_id_conf(config_file_t in_config, nfs_client_id_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_CLIENT_ID)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CLIENT_ID ) ; */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_CLIENT_ID);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_CLIENT_ID);
          return -1;
        }
    }

  return 0;
}                               /* nfs_client_id_conf */

/**
 *
 * nfs_read_ip_name_conf: reads the configuration for the Client/ID Cache
 *
 * Reads the configuration for the Client/ID Cache
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_state_id_conf(config_file_t in_config, nfs_state_id_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_STATE_ID)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_STATE_ID ) ; */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_STATE_ID);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_STATE_ID);
          return -1;
        }
    }

  return 0;
}                               /* nfs_state_id_conf */

#ifdef _USE_NFS4_1
int nfs_read_session_id_conf(config_file_t in_config, nfs_session_id_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_SESSION_ID)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n",
              CONF_LABEL_STATE_ID); */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_SESSION_ID);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_SESSION_ID);
          return -1;
        }
    }

  return 0;
}                               /* nfs_session_id_conf */

#ifdef _USE_PNFS
static int nfs_read_conf_pnfs_ds_conf(config_item_t subblock,
                                      pnfs_ds_parameter_t * pds_conf)
{
  unsigned int nb_subitem = config_GetNbItems(subblock);
  unsigned int i;
  int err;
  int var_index;
  struct hostent *hp = NULL;

  for(i = 0; i < nb_subitem; i++)
    {
      char *key_name;
      char *key_value;
      config_item_t item;
      item = config_GetItemByIndex(subblock, i);

      /* Here, we are managing a configuration sub block, it has nothing but CONFIG_ITEM_VAR in it */
      if(config_ItemType(item) != CONFIG_ITEM_VAR)
        {
          LogCrit(COMPONENT_CONFIG, "No sub-block expected \n");
          return -EINVAL;
        }

      /* recuperation du couple clef-valeur */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_WORKER);
          return CACHE_INODE_INVALID_ARGUMENT;
        }
      else if(!strcasecmp(key_name, "DS_Addr"))
        {
          if(isdigit(key_value[0]))
            {
              /* Address begin with a digit, it is a address in the dotted form, translate it */
              pds_conf->ipaddr = inet_addr(key_value);

              /* Keep this address in the ascii format as well (for GETDEVICEINFO) */
              strncpy(pds_conf->ipaddr_ascii, key_value, MAXNAMLEN);
            }
          else
            {
              /* This is a serveur name that is to be resolved. Use gethostbyname */
              if((hp = gethostbyname(key_value)) == NULL)
                {
                  LogCrit(COMPONENT_CONFIG, "PNFS LOAD PARAMETER: ERROR: Unexpected value for %s",
                             key_name);
                  return -1;
                }
              memcpy(&pds_conf->ipaddr, (char *)hp->h_addr, hp->h_length);
              snprintf(pds_conf->ipaddr_ascii, MAXNAMLEN,
                       "%u.%u.%u.%u",
                       ((unsigned int)ntohl(pds_conf->ipaddr) &
                        0xFF000000) >> 24,
                       ((unsigned int)ntohl(pds_conf->ipaddr) &
                        0x00FF0000) >> 16,
                       ((unsigned int)ntohl(pds_conf->ipaddr) &
                        0x0000FF00) >> 8,
                       (unsigned int)ntohl(pds_conf->ipaddr) & 0x000000FF);
            }
        }
      else if(!strcasecmp(key_name, "DS_Ip_Port"))
        {
          pds_conf->ipport = htons((unsigned short)atoi(key_value));
        }
      else if(!strcasecmp(key_name, "DS_ProgNum"))
        {
          pds_conf->prognum = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DS_Root_Path"))
        {
          strncpy(pds_conf->rootpath, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "DS_Id"))
        {
          pds_conf->id = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DS_Is_Ganesha"))
        {
          pds_conf->is_ganesha = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n", key_name, CONF_LABEL_PNFS);
          return -1;
        }
    }                           /* for */

  return 0;
}                               /* nfs_read_conf_pnfs_ds_conf */

int nfs_read_pnfs_conf(config_file_t in_config, pnfs_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  char *block_name;
  config_item_t block;
  struct hostent *hp = NULL;

  unsigned int ds_count = 0;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_PNFS)) == NULL)
    {
      LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n",
              CONF_LABEL_PNFS);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      switch (config_ItemType(item))
        {
        case CONFIG_ITEM_VAR:

          /* Get key's name */
          if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "Error reading key[%d] from section \"%s\" of configuration file.\n",
                      var_index, CONF_LABEL_PNFS);
              return -1;
            }
          else if(!strcasecmp(key_name, "Stripe_Size"))
            {
              pparam->layoutfile.stripe_size = StrToBoolean(key_value);
            }
          else if(!strcasecmp(key_name, "Stripe_Width"))
            {
              pparam->layoutfile.stripe_width = atoi(key_value);
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "Unknown or unsettable key: %s (item %s)\n", key_name,
                      CONF_LABEL_PNFS);
              return -1;
            }

          break;

        case CONFIG_ITEM_BLOCK:
          block_name = config_GetBlockName(item);

          if(!strcasecmp(block_name, "DataServer"))
            if(nfs_read_conf_pnfs_ds_conf(item, &pparam->layoutfile.ds_param[ds_count]) !=
               0)
              {
                LogCrit(COMPONENT_CONFIG,
                        "Unknown or unsettable key: %s (item %s)\n", key_name,
                        CONF_LABEL_PNFS);
                return -1;
              }

          ds_count += 1;
          break;

        default:
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_PNFS);
          return -1;
          break;
        }                       /* switch( config_ItemType(item) ) */

    }                           /* for */

  /* Sanity check : as much or less DS configured than stripe_size  */
  if(ds_count < pparam->layoutfile.stripe_width)
    {
      LogCrit(COMPONENT_CONFIG,
              "You must define more pNFS data server for strip_width=%u (only %u defined)\n",
              pparam->layoutfile.stripe_width, ds_count);
      return -1;
    }

  return 0;
}                               /* nfs_read_pnfs_conf */
#endif                          /* _USE_PNFS */

#endif

/**
 *
 * nfs_read_uidmap_conf: reads the configuration for the UID_MAPPER Cache
 *
 * Reads the configuration for the UID_MAPPER Cache
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_uidmap_conf(config_file_t in_config, nfs_idmap_cache_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_UID_MAPPER)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CLIENT_ID ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_UID_MAPPER);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Map"))
        {
          strncpy(pparam->mapfile, key_value, MAXPATHLEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_UID_MAPPER);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_uidmap_conf */

/**
 *
 * nfs_read_gidmap_conf: reads the configuration for the GID_MAPPER Cache
 *
 * Reads the configuration for the GID_MAPPER Cache
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 *
 */
int nfs_read_gidmap_conf(config_file_t in_config, nfs_idmap_cache_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_GID_MAPPER)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CLIENT_ID ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_GID_MAPPER);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Alphabet_Length"))
        {
          pparam->hash_param.alphabet_length = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Prealloc_Node_Pool_Size"))
        {
          pparam->hash_param.nb_node_prealloc = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Map"))
        {
          strncpy(pparam->mapfile, key_value, MAXPATHLEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_GID_MAPPER);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_gidmap_conf */

/**
 *
 * nfs_read_krb5_conf: read the configuration for krb5 stuff
 *
 * Read the configuration for krb5 stuff.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 *
 */
int nfs_read_krb5_conf(config_file_t in_config, nfs_krb5_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_KRB5)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_KRB5 ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_KRB5);
          return -1;
        }

      if(!strcasecmp(key_name, "PrincipalName"))
        {
          strncpy(pparam->principal, key_value, MAXNAMLEN);
        }
      else if(!strcasecmp(key_name, "KeytabPath"))
        {
          strncpy(pparam->keytab, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "Active_krb5"))
        {
          pparam->active_krb5 = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_KRB5);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_krb5_conf */

/**
 *
 * nfs_read_version4_conf: read the configuration for NFSv4 stuff
 *
 * Read the configuration for NFSv4 stuff.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 *
 */
int nfs_read_version4_conf(config_file_t in_config, nfs_version4_parameter_t * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName(in_config, CONF_LABEL_NFS_VERSION4)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_VERSION4 ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_VERSION4);
          return -1;
        }

      if(!strcasecmp(key_name, "Lease_Lifetime"))
        {
          pparam->lease_lifetime = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DomainName"))
        {
          strncpy(pparam->domainname, key_value, MAXNAMLEN);
        }
      else if(!strcasecmp(key_name, "IdmapConf"))
        {
          strncpy(pparam->idmapconf, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "FH_Expire"))
        {
          pparam->fh_expire = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Returns_ERR_FH_EXPIRED"))
        {
          pparam->returns_err_fh_expired = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Use_OPEN_CONFIRM"))
        {
          pparam->use_open_confirm = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Return_Bad_Stateid"))
        {
          pparam->return_bad_stateid = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_NFS_VERSION4);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_version4_conf */

/**
 *
 * Print_param_in_log : prints the nfs worker parameter structure into the logfile
 *
 * prints the nfs worker parameter structure into the logfile
 *
 * @param pparam Pointer to the nfs worker parameter
 * 
 * @return none (void function)
 *
 */
void Print_param_worker_in_log(nfs_worker_parameter_t * pparam)
{
  LogEvent(COMPONENT_INIT, "NFS PARAM : worker_param.lru_param.nb_entry_prealloc = %d",
             pparam->lru_param.nb_entry_prealloc);
  LogEvent(COMPONENT_INIT, "NFS PARAM : worker_param.nb_pending_prealloc = %d",
             pparam->nb_pending_prealloc);
  LogEvent(COMPONENT_INIT, "NFS PARAM : worker_param.nb_before_gc = %d", pparam->nb_before_gc);
}                               /* Print_param_worker_in_log */

/**
 *
 * Print_param_in_log : prints the nfs parameter structure into the logfile
 *
 * prints the nfs parameter structure into the logfile
 *
 * @param pparam Pointer to the nfs parameter
 * 
 * @return none (void function)
 *
 */
void Print_param_in_log(nfs_parameter_t * pparam)
{
  LogEvent(COMPONENT_INIT, "NFS PARAM : core_param.nb_worker = %d", pparam->core_param.nb_worker);
  Print_param_worker_in_log(&(pparam->worker_param));
}                               /* Print_param_in_log */

int nfs_get_fsalpathlib_conf(char *configPath, char *PathLib)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;
  unsigned int found = FALSE;
  config_file_t config_struct;

  /* Is the config tree initialized ? */
  if(configPath == NULL || PathLib == NULL)
    return 1;

  config_struct = config_ParseFile(configPath);

  if(!config_struct)
    {
      LogCrit(COMPONENT_CONFIG, "NFS STARTUP: Error while parsing %s: %s", configPath,
                 config_GetErrorMsg());
      exit(1);
    }

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config_struct, CONF_LABEL_NFS_CORE)) == NULL)
    {
      /* LogCrit(COMPONENT_CONFIG, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_NFS_CORE  ) ; */
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return 1;
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
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_NFS_CORE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "FSAL_Shared_Library"))
        {
          strncpy(PathLib, key_value, MAXPATHLEN);
          found = TRUE;
        }

    }

  if(!found)
    return 1;

  return 0;
}                               /* nfs_get_fsalpathlib_conf */
