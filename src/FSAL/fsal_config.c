/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * ------------- 
 */

/* Initialize configuration parameters
 */

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"

#define STRCMP   strcasecmp

/******************************************************
 *              Initialization tools.
 ******************************************************/

/** This macro initializes init info behaviors and values.
 *  Examples :
 *  SET_INIT_INFO( parameter.fs_common_info ,
 *                      maxfilesize              ,
 *                      FSAL_INIT_MAX_LIMIT      ,
 *                      0x00000000FFFFFFFFLL    );
 *
 *  SET_INIT_INFO( parameter.fs_common_info ,
 *                      linksupport              ,
 *                      FSAL_INIT_FORCEVALUE     ,
 *                      FALSE                   );
 *
 */

#define SET_INIT_INFO( _common_info_p_ , _field_name_ ,   \
                                    _field_behavior_ , _value_ ) do \
           {                                                        \
             _common_info_p_->behaviors._field_name_ = _field_behavior_ ;\
             if ( _field_behavior_ != FSAL_INIT_FS_DEFAULT )        \
               _common_info_p_->values._field_name_ = _value_ ; \
           } while (0)

/** This macro initializes the behavior for one parameter
 *  to default filesystem value.
 *  Examples :
 *  SET_INIT_DEFAULT( parameter.fs_common_info , case_insensitive );
 */
#define SET_INIT_DEFAULT( _common_info_p_ , _field_name_ ) \
        do {                                                         \
             _common_info_p_->behaviors._field_name_             \
                = FSAL_INIT_FS_DEFAULT ;                             \
           } while (0)



void init_fsal_parameters(fsal_init_info_t *init_info,
                              fs_common_initinfo_t *common_info)
{
  /* init max FS calls = unlimited */
  init_info->max_fs_calls = 0;
  /* set default values for all parameters of fs_common_info */

  SET_INIT_DEFAULT(common_info, maxfilesize);
  SET_INIT_DEFAULT(common_info, maxlink);
  SET_INIT_DEFAULT(common_info, maxnamelen);
  SET_INIT_DEFAULT(common_info, maxpathlen);
  SET_INIT_DEFAULT(common_info, no_trunc);
  SET_INIT_DEFAULT(common_info, chown_restricted);
  SET_INIT_DEFAULT(common_info, case_insensitive);
  SET_INIT_DEFAULT(common_info, case_preserving);
  SET_INIT_DEFAULT(common_info, fh_expire_type);
  SET_INIT_DEFAULT(common_info, link_support);
  SET_INIT_DEFAULT(common_info, symlink_support);
  SET_INIT_DEFAULT(common_info, lock_support);
  SET_INIT_DEFAULT(common_info, lock_support_owner);
  SET_INIT_DEFAULT(common_info, lock_support_async_block);
  SET_INIT_DEFAULT(common_info, named_attr);
  SET_INIT_DEFAULT(common_info, unique_handles);
  SET_INIT_DEFAULT(common_info, lease_time);
  SET_INIT_DEFAULT(common_info, acl_support);
  SET_INIT_DEFAULT(common_info, cansettime);
  SET_INIT_DEFAULT(common_info, homogenous);
  SET_INIT_DEFAULT(common_info, supported_attrs);
  SET_INIT_DEFAULT(common_info, maxread);
  SET_INIT_DEFAULT(common_info, maxwrite);
  SET_INIT_DEFAULT(common_info, umask);
  SET_INIT_DEFAULT(common_info, auth_exportpath_xdev);
  SET_INIT_DEFAULT(common_info, xattr_access_rights);
}

fsal_status_t load_FSAL_parameters_from_conf(config_file_t in_config,
                                             fsal_init_info_t *init_info)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  int DebugLevel = -1;
  char *LogFile = NULL;

  block = config_FindItemByName(in_config, CONF_LABEL_FSAL);

  /* cannot read item */

  if(block == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
              CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_INVAL, 0);
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
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      if(!STRCMP(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if(DebugLevel == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Invalid debug level name: \"%s\".",
                      key_value);
              ReturnCode(ERR_FSAL_INVAL, -1);
            }

        }
      else if(!STRCMP(key_name, "LogFile"))
        {

          LogFile = key_value;

        }
      else if(!STRCMP(key_name, "Max_FS_calls"))
        {

          int maxcalls = s_read_int(key_value);

          if(maxcalls < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          init_info->max_fs_calls = (unsigned int)maxcalls;

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  /* init logging */

  if(LogFile)
    SetComponentLogFile(COMPONENT_FSAL, LogFile);

  if(DebugLevel != -1)
    SetComponentLogLevel(COMPONENT_FSAL, DebugLevel);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t load_FS_common_parameters_from_conf(config_file_t in_config,
                                                  fs_common_initinfo_t *common_info)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_FS_COMMON);

  /* cannot read item */
  if(block == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
              CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /*
     configurable common info for filesystem are:
     link_support      # hardlink support
     symlink_support   # symlinks support
     cansettime        # Is it possible to change file times
     maxread           # Max read size from FS
     maxwrite          # Max write size to FS
     umask
     auth_exportpath_xdev
     xattr_access_rights

   */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      /* does the variable exists ? */
      if(!STRCMP(key_name, "link_support"))
        {

          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, link_support,
                             FSAL_INIT_MAX_LIMIT, bool);

        }
      else if(!STRCMP(key_name, "symlink_support"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, symlink_support,
                             FSAL_INIT_MAX_LIMIT, bool);
        }
      else if(!STRCMP(key_name, "cansettime"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, cansettime,
                             FSAL_INIT_MAX_LIMIT, bool);

        }
      else if(!STRCMP(key_name, "maxread"))
        {
          fsal_u64_t size;

          if(s_read_int64(key_value, &size))
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, maxread,
                             FSAL_INIT_FORCE_VALUE, size);

        }
      else if(!STRCMP(key_name, "maxwrite"))
        {
          fsal_u64_t size;

          if(s_read_int64(key_value, &size))
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, maxwrite,
                             FSAL_INIT_FORCE_VALUE, size);

        }
      else if(!STRCMP(key_name, "umask"))
        {
          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, umask,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else if(!STRCMP(key_name, "auth_xdev_export"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, auth_exportpath_xdev,
                             FSAL_INIT_FORCE_VALUE, bool);
        }
      else if(!STRCMP(key_name, "xattr_access_rights"))
        {
          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, xattr_access_rights,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                   key_name, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* filesystem info handlers
 * common functions for fsal info methods
 */

fsal_boolean_t fsal_supports(struct fsal_staticfsinfo_t *info,
			     fsal_fsinfo_options_t option)
{
	switch(option) {
	case no_trunc:
		return !!info->no_trunc;
	case chown_restricted:
		return !!info->chown_restricted;
	case case_insensitive:
		return !!info->case_insensitive;
	case case_preserving:
		return !!info->case_preserving;
	case link_support:
		return !!info->link_support;
	case symlink_support:
		return !!info->symlink_support;
	case lock_support:
		return !!info->lock_support;
	case lock_support_owner:
		return !!info->lock_support_owner;
	case lock_support_async_block:
		return !!info->lock_support_async_block;
	case named_attr:
		return !!info->named_attr;
	case unique_handles:
		return !!info->unique_handles;
	case cansettime:
		return !!info->cansettime;
	case homogenous:
		return !!info->homogenous;
	case auth_exportpath_xdev:
		return !!info->auth_exportpath_xdev;
	case dirs_have_sticky_bit:
		return !!info->dirs_have_sticky_bit;
#ifdef _USE_FSALMDS
	case pnfs_supported:
		return !!info->pnfs_supported;
#endif
	default:
		return FALSE; /* whatever I don't know about,
			       * you can't do
			       */
	}
}

fsal_size_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info)
{
	return info->maxfilesize;
}

fsal_count_t fsal_maxlink(struct fsal_staticfsinfo_t *info)
{
	return info->maxlink;
}

fsal_mdsize_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info)
{
	return info->maxnamelen;
}

fsal_mdsize_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info)
{
	return info->maxpathlen;
}

fsal_fhexptype_t fsal_fh_expire_type(struct fsal_staticfsinfo_t *info)
{
	return info->fh_expire_type;
}

fsal_time_t fsal_lease_time(struct fsal_staticfsinfo_t *info)
{
	return info->lease_time;
}

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info)
{
	return info->acl_support;
}

fsal_attrib_mask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info)
{
	return info->supported_attrs;
}

fsal_size_t fsal_maxread(struct fsal_staticfsinfo_t *info)
{
	return info->maxread;
}

fsal_size_t fsal_maxwrite(struct fsal_staticfsinfo_t *info)
{
	return info->maxwrite;
}

fsal_accessmode_t fsal_umask(struct fsal_staticfsinfo_t *info)
{
	return info->umask;
}

fsal_accessmode_t fsal_xattr_access_rights(struct fsal_staticfsinfo_t *info)
{
	return info->xattr_access_rights;
}

#ifdef _USE_FSALMDS
fattr4_fs_layout_types fsal_fs_layout_types(struct fsal_staticfsinfo_t *info)
{
	return info->fs_layout_types;
}

fsal_size_t fsal_layout_blksize(struct fsal_staticinfo_t *info)
{
	return info->layout_blksize;
}
#endif
