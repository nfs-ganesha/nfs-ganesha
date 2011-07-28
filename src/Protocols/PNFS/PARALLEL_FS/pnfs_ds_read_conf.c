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
#include "rpc.h"
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

int nfs_read_conf_pnfs_ds_conf(config_item_t subblock,
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
          LogCrit(COMPONENT_CONFIG, "No sub-block expected ");
          return -EINVAL;
        }

      /* recuperation du couple clef-valeur */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
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
                  LogCrit(COMPONENT_CONFIG,
                          "PNFS LOAD PARAMETER: ERROR: Unexpected value for %s",
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
                  "Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_PNFS);
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
  int unique;

  unsigned int ds_count = 0;

  /* Is the config tree initialized ? */
  if(in_config == NULL || pparam == NULL)
    return -1;

  /* Get the config BLOCK */
  if((block = config_FindItemByName_CheckUnique(in_config, CONF_LABEL_PNFS, &unique)) == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "Cannot read item \"%s\" from configuration file: %s",
              CONF_LABEL_PNFS, config_GetErrorMsg() );
      return 1;
    }
  else if (!unique)
  {
      LogCrit(COMPONENT_CONFIG,
              "Only a single \"%s\" block is expected in config file: %s",
              CONF_LABEL_PNFS, config_GetErrorMsg() );
      return -1;
  }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return -1;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;
      config_item_type item_type ;

      item = config_GetItemByIndex(block, var_index);
      item_type = config_ItemType(item) ;

      switch( item_type )
        {
        case CONFIG_ITEM_VAR:

          /* Get key's name */
          if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "Error reading key[%d] from section \"%s\" of configuration file.",
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
                      "Unknown or unsettable key: %s (item %s)", key_name,
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
                        "Unknown or unsettable key: %s (item %s)", key_name,
                        CONF_LABEL_PNFS);
                return -1;
              }

          ds_count += 1;
          break;

        default:
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_PNFS);
          return -1;
          break;
        }                       /* switch( config_ItemType(item) ) */

    }                           /* for */

  /* Sanity check : as much or less DS configured than stripe_size  */
  if(ds_count < pparam->layoutfile.stripe_width)
    {
      LogCrit(COMPONENT_CONFIG,
              "You must define more pNFS data server for strip_width=%u (only %u defined)",
              pparam->layoutfile.stripe_width, ds_count);
      return -1;
    }

  return 0;
}                               /* nfs_read_pnfs_conf */


