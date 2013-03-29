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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file  nfs_read_conf.c
 * @brief This file that contain the routine required for parsing the NFS specific configuraion file.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h> /* for having FNDELAY */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"

/**
 * @brief Read the core configuration
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed, 1 is stanza is not there.
 */
int nfs_read_core_conf(config_file_t in_config,
		       nfs_core_parameter_t *pparam)
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
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_NFS_CORE);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogDebug(COMPONENT_CONFIG,
               "Item \"%s\" is expected to be a block",
               CONF_LABEL_NFS_CORE);
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
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_NFS_CORE);
          return CACHE_INODE_INVALID_ARGUMENT;
        }

      if(!strcasecmp(key_name, "NFS_Port"))
        {
          pparam->port[P_NFS] = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "MNT_Port"))
        {
          pparam->port[P_MNT] = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NLM_Port"))
        {
          pparam->port[P_NLM] = (unsigned short)atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Rquota_Port"))
        {
#ifdef _USE_RQUOTA
          pparam->port[P_RQUOTA] = (unsigned short)atoi(key_value);
#endif
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
      else if(!strcasecmp(key_name, "NFS_Program"))
        {
          pparam->program[P_NFS] = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "MNT_Program"))
        {
          pparam->program[P_MNT] = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NLM_Program"))
        {
          pparam->program[P_NLM] = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Rquota_Program"))
        {
#ifdef _USE_RQUOTA
          pparam->program[P_RQUOTA] = atoi(key_value);
#endif
        }
      else if(!strcasecmp(key_name, "Nb_Worker"))
        {
          pparam->nb_worker = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Core_Dump_Size"))
        {
          pparam->core_dump_size = atol(key_value);
        }
      else if(!strcasecmp(key_name, "Drop_IO_Errors"))
        {
          pparam->drop_io_errors = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Drop_Inval_Errors"))
        {
          pparam->drop_inval_errors = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Drop_Delay_Errors"))
        {
          pparam->drop_delay_errors = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Dispatch_Max_Reqs"))
        {
          pparam->dispatch_max_reqs = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Dispatch_Max_Reqs_Xprt"))
        {
          pparam->dispatch_max_reqs_xprt = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_Disabled"))
        {
            pparam->drc.disabled = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Npart"))
        {
          pparam->drc.tcp.npart = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Size"))
        {
          pparam->drc.tcp.size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Cachesz"))
        {
          pparam->drc.tcp.cachesz = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Hiwat"))
        {
          pparam->drc.tcp.hiwat = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Recycle_Npart"))
        {
          pparam->drc.tcp.recycle_npart = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Recycle_Expire_S"))
        {
          pparam->drc.tcp.recycle_expire_s = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_TCP_Checksum"))
        {
          pparam->drc.tcp.checksum = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_UDP_Npart"))
        {
          pparam->drc.udp.npart = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_UDP_Size"))
        {
          pparam->drc.udp.size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_UDP_Cachesz"))
        {
          pparam->drc.udp.cachesz = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_UDP_Hiwat"))
        {
          pparam->drc.udp.hiwat = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "DRC_UDP_Checksum"))
        {
          pparam->drc.udp.checksum = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "RPC_Debug_Flags"))
        {
          pparam->rpc.debug_flags = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "RPC_Max_Connections"))
        {
          pparam->rpc.max_connections = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "RPC_Idle_Timeout_S"))
        {
          pparam->rpc.idle_timeout_s = atoi(key_value);
        }
      else if(!strcasecmp( key_name, "MaxRPCSendBufferSize" ) )
        {
          pparam->rpc.max_send_buffer_size = atoi(key_value);
        }
      else if(!strcasecmp( key_name, "MaxRPCRecvBufferSize" ) )
        {
          pparam->rpc.max_recv_buffer_size = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Long_Processing_Threshold"))
        {
          pparam->long_processing_threshold = atoi(key_value);
        }
      else if(!strcasecmp( key_name, "Decoder_Fridge_Expiration_Delay" ) )
        {
          pparam->decoder_fridge_expiration_delay = atoi(key_value);
        }
      else if(!strcasecmp( key_name, "Decoder_Fridge_Block_Timeout" ) )
        {
          pparam->decoder_fridge_block_timeout = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "NFS_Protocols"))
        {

#     define MAX_NFSPROTO      10       /* large enough !!! */
#     define MAX_NFSPROTO_LEN  256      /* so is it !!! */

          char *nfsvers_list[MAX_NFSPROTO];
          int idx, count;

          /* reset nfs versions flags (clean defaults) */
          pparam->core_options &= ~(CORE_OPTION_ALL_VERS);

          /* allocate nfs vers strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            nfsvers_list[idx] = gsh_malloc(MAX_NFSPROTO_LEN);

          /*
           * Search for coma-separated list of nfsprotos
           */
          count = nfs_ParseConfLine(nfsvers_list, MAX_NFSPROTO,
                                    key_value, find_comma, find_endLine);

          if(count < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS_Protocols list too long (>%d)",
                      MAX_NFSPROTO);

              /* free sec strings */
              for(idx = 0; idx < MAX_NFSPROTO; idx++)
                gsh_free(nfsvers_list[idx]);

              return -1;
            }

          /* add each Nfs protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!strcmp(nfsvers_list[idx], "4"))
                {
                  pparam->core_options |= CORE_OPTION_NFSV4;
                }
              else if(!strcmp(nfsvers_list[idx], "3"))
                {
                  pparam->core_options |= CORE_OPTION_NFSV3;
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "Invalid NFS Protocol \"%s\". Values can be: 3, 4.",
                          nfsvers_list[idx]);
                  return -1;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            gsh_free(nfsvers_list[idx]);

          /* check that at least one nfs protocol has been specified */
          if((pparam->core_options & (CORE_OPTION_ALL_VERS)) == 0)
            {
              LogCrit(COMPONENT_CONFIG, "Empty NFS_Protocols list");
              return -1;
            }
        }
      else if(!strcasecmp( key_name, "NSM_Use_Caller_Name" ) )
        {
          pparam->nsm_use_caller_name = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Clustered"))
        {
          pparam->clustered = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Enable_NLM"))
        {
          pparam->enable_NLM = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Enable_RQUOTA"))
        {
          pparam->enable_RQUOTA = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_NFS_CORE);
          return -1;
        }

    }

  return 0;
}

/**
 * @brief Reads the configuration for the IP/name.
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok,  -1 if not, 1 is stanza is not there.
 */
int nfs_read_ip_name_conf(config_file_t in_config,
			  nfs_ip_name_parameter_t *pparam)
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
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file", CONF_LABEL_NFS_IP_NAME);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogDebug(COMPONENT_CONFIG,
               "Item \"%s\" is expected to be a block", CONF_LABEL_NFS_IP_NAME);
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
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_NFS_IP_NAME);
          return -1;
        }

      if(!strcasecmp(key_name, "Index_Size"))
        {
          pparam->hash_param.index_size = atoi(key_value);
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
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_NFS_IP_NAME);
          return -1;
        }
    }

  return 0;
}

#ifdef _HAVE_GSSAPI
/**
 *
 * @brief Read the configuration for krb5 stuff
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 */
int nfs_read_krb5_conf(config_file_t in_config,
		       nfs_krb5_parameter_t *pparam)
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
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_NFS_KRB5);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogDebug(COMPONENT_CONFIG,
               "Item \"%s\" is expected to be a block",
               CONF_LABEL_NFS_KRB5);
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
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_NFS_KRB5);
          return -1;
        }

      if(!strcasecmp(key_name, "PrincipalName"))
        {
          strlcpy(pparam->svc.principal, key_value,
                  sizeof(pparam->svc.principal));
        }
      else if(!strcasecmp(key_name, "KeytabPath"))
        {
          strlcpy(pparam->keytab, key_value, sizeof(pparam->keytab));
        }
      else if(!strcasecmp(key_name, "Active_krb5"))
        {
          pparam->active_krb5 = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_NFS_KRB5);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_krb5_conf */
#endif

/**
 * @brief Read the configuration for NFSv4 stuff
 *
 * @param[in]  in_config Configuration file handle
 * @param[out] pparam    Read parameters
 *
 * @return 0 if ok, -1 if failed,1 is stanza is not there
 */
int nfs_read_version4_conf(config_file_t in_config,
			   nfs_version4_parameter_t *pparam)
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
      LogDebug(COMPONENT_CONFIG,
               "Cannot read item \"%s\" from configuration file",
               CONF_LABEL_NFS_VERSION4);
      return 1;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      LogDebug(COMPONENT_CONFIG,
               "Item \"%s\" is expected to be a block",
               CONF_LABEL_NFS_VERSION4);
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
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_NFS_VERSION4);
          return -1;
        }

      if(!strcasecmp(key_name, "Graceless"))
        {
          pparam->graceless = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Lease_Lifetime"))
        {
          pparam->lease_lifetime = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Grace_Period"))
        {
          pparam->grace_period = atoi(key_value);
        }
      else if(!strcasecmp(key_name, "Return_Bad_Stateid"))
        {
          pparam->return_bad_stateid = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "DomainName"))
        {
          strncpy(pparam->domainname, key_value, MAXNAMLEN);
        }
      else if(!strcasecmp(key_name, "IdmapConf"))
        {
          strncpy(pparam->idmapconf, key_value, MAXPATHLEN);
        }
      else if(!strcasecmp(key_name, "UseGetpwnam"))
        {
          pparam->use_getpwnam = StrToBoolean(key_value);
        }
      else if(!strcasecmp(key_name, "Allow_Numeric_Owners"))
        {
          pparam->allow_numeric_owners = StrToBoolean(key_value);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_NFS_VERSION4);
          return -1;
        }
    }

  return 0;
}                               /* nfs_read_version4_conf */

