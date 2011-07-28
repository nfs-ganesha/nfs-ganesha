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
 * \file    BuddyConfig.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 13:43:20 $
 * \version $Revision: 1.1 $
 * \brief   Configuration parsing for BuddyMallocator.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "BuddyMalloc.h"
#include "log_macros.h"
#include "common_utils.h"
#include <strings.h>
#include <string.h>

#define STRCMP strcasecmp

extern buddy_parameter_t default_buddy_parameter;

/* there but be exactly 1 bit set */
static int check_2power(unsigned long long tested_size)
{
  int i;

  for(i = 0; i < 64; i++)
    {
      /* if the first detected bit equals tested_size,
       * it's OK.
       */
      if((tested_size & ((unsigned long long)1 << i)) == tested_size)
        return TRUE;
    }
  return FALSE;

}

int Buddy_set_default_parameter(buddy_parameter_t * out_parameter)
{
  if(out_parameter == NULL)
    return BUDDY_ERR_EFAULT;

  *out_parameter = default_buddy_parameter;

  return BUDDY_SUCCESS;
}

int Buddy_load_parameter_from_conf(config_file_t in_config,
                                   buddy_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_BUDDY);

  /* cannot read item */

  if(block == NULL)
    {
      LogCrit(COMPONENT_MEMALLOC,
              "BUDDY LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
              CONF_LABEL_BUDDY);
      return BUDDY_ERR_ENOENT;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_MEMALLOC,
              "BUDDY LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_BUDDY);
      return BUDDY_ERR_EINVAL;
    }

  /* read variable for fsal init */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          LogCrit(COMPONENT_MEMALLOC,
                  "BUDDY LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_BUDDY);
          return BUDDY_ERR_EFAULT;
        }

      if(!STRCMP(key_name, "Page_Size"))
        {
          size_t page_size;

          if(s_read_size(key_value, &page_size) || !check_2power(page_size))
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: must be a 2^n value.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->memory_area_size = page_size;

        }
      else if(!STRCMP(key_name, "Enable_OnDemand_Alloc"))
        {
          int bool;

          bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->on_demand_alloc = bool;

        }
      else if(!STRCMP(key_name, "Enable_Extra_Alloc"))
        {
          int bool;

          bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->extra_alloc = bool;

        }
      else if(!STRCMP(key_name, "Enable_GC"))
        {
          int bool;

          bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->free_areas = bool;

        }
      else if(!STRCMP(key_name, "GC_Keep_Factor"))
        {

          int keep_factor = s_read_int(key_value);

          if(keep_factor < 1)
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->keep_factor = keep_factor;

        }
      else if(!STRCMP(key_name, "GC_Keep_Min"))
        {

          int keep_min = s_read_int(key_value);

          if(keep_min < 0)
            {
              LogCrit(COMPONENT_MEMALLOC,
                      "BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              return BUDDY_ERR_EINVAL;
            }

          out_parameter->keep_minimum = keep_min;

        }
      else if(!STRCMP(key_name, "LogFile"))
        {
          SetComponentLogFile(COMPONENT_MEMALLOC, key_value);
          SetComponentLogFile(COMPONENT_MEMLEAKS, key_value);
        }
      else
        {
          LogCrit(COMPONENT_MEMALLOC,
                  "BUDDY LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_BUDDY);
          return BUDDY_ERR_EINVAL;
        }

    }

  return BUDDY_SUCCESS;

}                               /* Buddy_load_parameter_from_conf */
