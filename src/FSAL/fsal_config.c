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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file fsal_config.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Initialize configuration parameters
 */

#include "config.h"

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "FSAL/fsal_init.h"

#define STRCMP   strcasecmp

/** Behavior for init values */
typedef enum fsal_initflag__
{
  FSAL_INIT_FS_DEFAULT = 0,     /**< keep FS default value */
  FSAL_INIT_FORCE_VALUE,        /**< force a value */
  FSAL_INIT_MAX_LIMIT,          /**< force a value if default is greater */
  FSAL_INIT_MIN_LIMIT           /**< force a value if default is smaller */
      /* Note : for booleans, we considerate that true > false */
} fsal_initflag_t;

struct fsal_settable_bool {
        fsal_initflag_t how;
        bool val;
};

struct fsal_settable_int32 {
        fsal_initflag_t how;
        int32_t val;
};

struct fsal_settable_uint64 {
        fsal_initflag_t how;
        uint64_t val;
};

/*
 * Parameters which can be changed by parsing 'Filesystem' block
 */
struct fsal_fs_params {
        struct fsal_settable_bool symlink_support;
        struct fsal_settable_bool link_support;
        struct fsal_settable_bool lock_support;
        struct fsal_settable_bool lock_support_owner;
        struct fsal_settable_bool lock_support_async_block;
        struct fsal_settable_bool cansettime;
        struct fsal_settable_bool auth_exportpath_xdev;
        struct fsal_settable_uint64 maxread;
        struct fsal_settable_uint64 maxwrite;
        struct fsal_settable_int32 umask;
        struct fsal_settable_int32 xattr_access_rights;
};

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
 *                      false);
 *
 */

#define SET_INIT_INFO( _common_info_p_ , _field_name_ ,   \
                                    _field_behavior_ , _value_ ) do \
           {                                                        \
             _common_info_p_->_field_name_.how = _field_behavior_ ; \
             if ( _field_behavior_ != FSAL_INIT_FS_DEFAULT )        \
               _common_info_p_->_field_name_.val = _value_ ; \
           } while (0)

#define SET_INTEGER_PARAM( cfg, init_info, _field )          \
    switch( init_info._field.how ){                          \
    case FSAL_INIT_FORCE_VALUE :                             \
      /* force the value in any case */                      \
      cfg->_field = init_info._field.val;                    \
      break;                                                 \
    case FSAL_INIT_MAX_LIMIT :                               \
      /* check the higher limit */                           \
      if ( cfg->_field > init_info._field.val )              \
        cfg->_field = init_info._field.val ;                 \
      break;                                                 \
    case FSAL_INIT_MIN_LIMIT :                               \
      /* check the lower limit */                            \
      if ( cfg->_field < init_info._field.val )              \
        cfg->_field = init_info._field.val ;                 \
      break;                                                 \
    case FSAL_INIT_FS_DEFAULT:                               \
    default:                                                 \
    /* In the other cases, we keep the default value. */     \
        break;                                               \
    }

#define SET_BITMAP_PARAM( cfg, init_info, _field )           \
    switch( init_info._field.how ){                          \
    case FSAL_INIT_FORCE_VALUE :                             \
        /* force the value in any case */                    \
        cfg->_field = init_info._field.val;                  \
        break;                                               \
    case FSAL_INIT_MAX_LIMIT :                               \
      /* proceed a bit AND */                                \
      cfg->_field &= init_info._field.val ;                  \
      break;                                                 \
    case FSAL_INIT_MIN_LIMIT :                               \
      /* proceed a bit OR */                                 \
      cfg->_field |= init_info._field.val ;                  \
      break;                                                 \
    case FSAL_INIT_FS_DEFAULT:                               \
    default:                                                 \
    /* In the other cases, we keep the default value. */     \
        break;                                               \
    }

#define SET_BOOLEAN_PARAM( cfg, init_info, _field )          \
    switch( init_info._field.how ){                          \
    case FSAL_INIT_FORCE_VALUE :                             \
        /* force the value in any case */                    \
        cfg->_field = init_info._field.val;                  \
        break;                                               \
    case FSAL_INIT_MAX_LIMIT :                               \
      /* proceed a boolean AND */                            \
      cfg->_field = cfg->_field && init_info._field.val ;    \
      break;                                                 \
    case FSAL_INIT_MIN_LIMIT :                               \
      /* proceed a boolean OR */                             \
      cfg->_field = cfg->_field && init_info._field.val ;    \
      break;                                                 \
    case FSAL_INIT_FS_DEFAULT:                               \
    default:                                                 \
    /* In the other cases, we keep the default value. */     \
        break;                                               \
    }

static fsal_status_t
load_FSAL_parameters_from_conf(config_file_t in_config,
			       const char *fsal_name,
                               fsal_init_info_t *init_info,
                               fsal_extra_arg_parser_f extra)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t fsal_block, block = NULL;
  int i, fsal_cnt;

  fsal_block = config_FindItemByName(in_config, CONF_LABEL_FSAL);
  if(fsal_block == NULL) {
     LogFatal(COMPONENT_INIT,
	      "Cannot find item \"%s\" in configuration",
	      CONF_LABEL_FSAL);
     return fsalstat(ERR_FSAL_NOENT, 0);
  }
  if(config_ItemType(fsal_block) != CONFIG_ITEM_BLOCK) {
     LogFatal(COMPONENT_INIT,
	      "\"%s\" is not a block",
	      CONF_LABEL_FSAL);
     return fsalstat(ERR_FSAL_NOENT, 0);
  }
  fsal_cnt = config_GetNbItems(fsal_block);
  for(i = 0; i < fsal_cnt; i++)
  {
	  block = config_GetItemByIndex(fsal_block, i);
	  if(config_ItemType(block) == CONFIG_ITEM_BLOCK &&
	     strcasecmp(config_GetBlockName(block), fsal_name) == 0) {
		  break;
	  }
  }
  if(i == fsal_cnt) {
     LogFatal(COMPONENT_INIT,
	      "Cannot find the %s section of %s in the configuration file",
	      fsal_name, CONF_LABEL_FSAL);
     return fsalstat(ERR_FSAL_NOENT, 0);
  }
  if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              fsal_name);
      return fsalstat(ERR_FSAL_INVAL, 0);
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
          return fsalstat(ERR_FSAL_SERVERFAULT, err);
        }

      if(!STRCMP(key_name, "DebugLevel") ||
	 !STRCMP(key_name, "LogFile"))
        {
	  LogWarn(COMPONENT_CONFIG,
                  "Deprecated FSAL option %s=\'%s\'",
                  key_name, key_value);
	}
      else if(!STRCMP(key_name, "Max_FS_calls"))
        {

          int maxcalls = s_read_int(key_value);

          if(maxcalls < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

        }
      else if(strcasecmp(key_name, "FSAL_Shared_Library") == 0)
        {
	  continue; /* scanned at load time */
        }
      else if((extra == NULL) || ((*extra)(key_name, key_value,
                                           init_info, fsal_name)))
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_FSAL);
          return fsalstat(ERR_FSAL_INVAL, 0);
        }

    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t
load_FS_common_parameters_from_conf(config_file_t in_config,
                                    struct fsal_fs_params *common_info)
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
      return fsalstat(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_FS_COMMON);
      return fsalstat(ERR_FSAL_INVAL, 0);
    }

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
          return fsalstat(ERR_FSAL_SERVERFAULT, err);
        }

      /* does the variable exists ? */
      if(!STRCMP(key_name, "link_support"))
        {

          int val = StrToBoolean(key_value);

          if(val == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, link_support,
                             FSAL_INIT_MAX_LIMIT, val);

        }
      else if(!STRCMP(key_name, "symlink_support"))
        {
          int val = StrToBoolean(key_value);

          if(val == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, symlink_support,
                             FSAL_INIT_MAX_LIMIT, val);
        }
      else if(!STRCMP(key_name, "cansettime"))
        {
          int val = StrToBoolean(key_value);

          if(val == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          SET_INIT_INFO(common_info, cansettime,
                             FSAL_INIT_MAX_LIMIT, val);

        }
      else if(!STRCMP(key_name, "maxread"))
        {
          int size;

          size = s_read_int(key_value);

          SET_INIT_INFO(common_info, maxread,
                             FSAL_INIT_FORCE_VALUE, size);

        }
      else if(!STRCMP(key_name, "maxwrite"))
        {
          uint32_t size;

          size = s_read_int(key_value);

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
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, umask,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else if(!STRCMP(key_name, "auth_xdev_export"))
        {
          int val = StrToBoolean(key_value);

          if(val == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, auth_exportpath_xdev,
                             FSAL_INIT_FORCE_VALUE, val);
        }
      else if(!STRCMP(key_name, "xattr_access_rights"))
        {
          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                      key_name);
              return fsalstat(ERR_FSAL_INVAL, 0);
            }

          SET_INIT_INFO(common_info, xattr_access_rights,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                   key_name, CONF_LABEL_FS_COMMON);
          return fsalstat(ERR_FSAL_INVAL, 0);
        }

    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* filesystem info handlers
 * common functions for fsal info methods
 */

bool fsal_supports(struct fsal_staticfsinfo_t *info,
                   fsal_fsinfo_options_t option)
{
	switch(option) {
	case fso_no_trunc:
		return !!info->no_trunc;
	case fso_chown_restricted:
		return !!info->chown_restricted;
	case fso_case_insensitive:
		return !!info->case_insensitive;
	case fso_case_preserving:
		return !!info->case_preserving;
	case fso_link_support:
		return !!info->link_support;
	case fso_symlink_support:
		return !!info->symlink_support;
	case fso_lock_support:
		return !!info->lock_support;
	case fso_lock_support_owner:
		return !!info->lock_support_owner;
	case fso_lock_support_async_block:
		return !!info->lock_support_async_block;
	case fso_named_attr:
		return !!info->named_attr;
	case fso_unique_handles:
		return !!info->unique_handles;
	case fso_cansettime:
		return !!info->cansettime;
	case fso_homogenous:
		return !!info->homogenous;
	case fso_auth_exportpath_xdev:
		return !!info->auth_exportpath_xdev;
	case fso_dirs_have_sticky_bit:
		return !!info->dirs_have_sticky_bit;
	case fso_delegations:
		return !!info->delegations;
        case fso_pnfs_ds_supported:
		return !!info->pnfs_file;
	case fso_accesscheck_support:
		return !!info->accesscheck_support;
	case fso_share_support:
		return !!info->share_support;
	case fso_share_support_owner:
		return !!info->share_support_owner;
	default:
		return false; /* whatever I don't know about,
                               * you can't do
			       */
	}
}

uint64_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info)
{
        return info->maxfilesize;
}

uint32_t fsal_maxlink(struct fsal_staticfsinfo_t *info)
{
        return info->maxlink;
}

uint32_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info)
{
	return info->maxnamelen;
}

uint32_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info)
{
	return info->maxpathlen;
}

struct timespec fsal_lease_time(struct fsal_staticfsinfo_t *info)
{
	return info->lease_time;
}

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info)
{
	return info->acl_support;
}

attrmask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info)
{
	return info->supported_attrs;
}

uint32_t fsal_maxread(struct fsal_staticfsinfo_t *info)
{
	return info->maxread;
}

uint32_t fsal_maxwrite(struct fsal_staticfsinfo_t *info)
{
	return info->maxwrite;
}

uint32_t fsal_umask(struct fsal_staticfsinfo_t *info)
{
	return info->umask;
}

uint32_t fsal_xattr_access_rights(struct fsal_staticfsinfo_t *info)
{
	return info->xattr_access_rights;
}

fsal_status_t
fsal_load_config(const char *name,
                 config_file_t config_struct,
                 fsal_init_info_t *fsal_init,
                 struct fsal_staticfsinfo_t * fs_info,
                 fsal_extra_arg_parser_f extra)
{
	fsal_status_t st;
        struct fsal_fs_params common_info;

        st = load_FSAL_parameters_from_conf(config_struct, name,
                                            fsal_init, extra);
        if(FSAL_IS_ERROR(st))
                return st;

        /* Note - memset uses the fact that FSAL_FS_INIT_DEFAULT is 0 */
        memset(&common_info, 0, sizeof(common_info));
	st = load_FS_common_parameters_from_conf(config_struct, &common_info);
	if(FSAL_IS_ERROR(st))
		return st;

	SET_BOOLEAN_PARAM(fs_info, common_info, symlink_support);
	SET_BOOLEAN_PARAM(fs_info, common_info, link_support);
	SET_BOOLEAN_PARAM(fs_info, common_info, lock_support);
	SET_BOOLEAN_PARAM(fs_info, common_info, lock_support_owner);
	SET_BOOLEAN_PARAM(fs_info, common_info, lock_support_async_block);
	SET_BOOLEAN_PARAM(fs_info, common_info, cansettime);
	SET_INTEGER_PARAM(fs_info, common_info, maxread);
	SET_INTEGER_PARAM(fs_info, common_info, maxwrite);
	SET_BITMAP_PARAM(fs_info, common_info, umask);
	SET_BOOLEAN_PARAM(fs_info, common_info, auth_exportpath_xdev);
	SET_BITMAP_PARAM(fs_info, common_info, xattr_access_rights);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

void init_fsal_parameters(fsal_init_info_t *init_info)
{
        return;
}
/** @} */
