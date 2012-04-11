/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box, Inc.
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Portions copyright CEA/DAM/DIF  (2008)
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

/**
 * \file    fsal_tools.c
 * \brief   miscelaneous FSAL tools that can be called from outside.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include <string.h>

/* case unsensitivity */
#define STRCMP   strcasecmp
#define low32m( a ) ( (unsigned int)a )

char *CEPHFSAL_GetFSName()
{
     return "CEPH";
}

/**
 * @brief Compare 2 handles
 *
 * This function compares two FSAL handles, returning 0 if they are to
 * be considered identical.
 *
 * @param handle1 [in] The first handle to be compared
 * @param handle2 [in] The second handle to be compared
 * @param status [out] Status of the compare operation
 *
 * @retval 0 if handles are the same
 * @retval Something else if they're not
 */

int
CEPHFSAL_handlecmp(fsal_handle_t *exthandle1,
                   fsal_handle_t *exthandle2,
                   fsal_status_t *status)
{
     cephfsal_handle_t *handle1 = (cephfsal_handle_t *)exthandle1;
     cephfsal_handle_t *handle2 = (cephfsal_handle_t *)exthandle2;
     *status = FSAL_STATUS_NO_ERROR;

     if (!handle1 || !handle2) {
          status->major = ERR_FSAL_FAULT;
          return -1;
     }

     if ((VINODE(handle1).ino.val == VINODE(handle2).ino.val) &&
         (VINODE(handle1).snapid.val == VINODE(handle2).snapid.val)) {
          return 0;
     } else {
          return 1;
     }
}

/**
 * @brief Generate an index in the handle table
 *
 * This function is used for hashing a FSAL handle in order to
 * distribute entries into the hash table array.
 *
 * @param exthandle [in] The handle to be hashed
 * @param cookie [in] Makes it possible to have different hash value for the
 *                    same handle, when cookie changes.
 * @param alphabet_len [in] Parameter for polynomial hashing algorithm
 * @param index_size [in] The range of hash value will be [0..index_size-1]
 *
 * @return The hash value
 */

unsigned int
CEPHFSAL_Handle_to_HashIndex(fsal_handle_t *exthandle,
                             unsigned int cookie,
                             unsigned int alphabet_len,
                             unsigned int index_size)
{
     cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;

     return (unsigned int)
          ((VINODE(handle).ino.val + VINODE(handle).snapid.val) %
           index_size);
}

/**
 * @brief Generate a hash to locate the handle within a tree
 *
 * This function is used for generating a RBT node ID in order to
 * identify entries into the RBT.
 *
 * @param exthandle [in] The handle to be hashed
 * @param cookie [in] Makes it possible to have different hash value
 *                    for the same handle, when cookie changes.
 *
 * @return The hash value
 */

unsigned int
CEPHFSAL_Handle_to_RBTIndex(fsal_handle_t *exthandle,
                            unsigned int cookie)
{
     cephfsal_handle_t* handle = (cephfsal_handle_t *)exthandle;
     return (unsigned int)(0xABCD1234 ^ VINODE(handle).ino.val ^
                           VINODE(handle).snapid.val ^ cookie);
}

/**
 * @brief Create a wire level representation of an FSAL handle
 *
 * Convert an fsal_handle_t to a buffer to be included into NFS
 * handles, or another digest.
 *
 * @param extexport [in] The export context
 * @param output_type [in] The type of digest requested
 * @param exthandle [in] The handle to be digested
 * @param fh_desc [out] Counted buffer to hold the handle
 *
 * @return Errors as appropriate
 */

fsal_status_t
CEPHFSAL_DigestHandle(fsal_export_context_t *extexport,
                      fsal_digesttype_t output_type,
                      fsal_handle_t *exthandle,
                      struct fsal_handle_desc *fh_desc)
{
     cephfsal_handle_t *handle = (cephfsal_handle_t *)exthandle;
     size_t fh_len = 0;
     void *fh_data = NULL;

     fh_len = sizeof(handle->data);
     fh_data = &handle->data;

     switch (output_type) {
          /* Digested Handles */
     case FSAL_DIGEST_NFSV2:
     case FSAL_DIGEST_NFSV3:
     case FSAL_DIGEST_NFSV4:
          if (fh_desc->len < fh_len) {
               LogMajor(COMPONENT_FSAL,
                        "Ceph DigestHandle: space too small for handle.  "
                        "Need %zu, have %zu", fh_len, fh_desc->len);
               ReturnCode(ERR_FSAL_TOOSMALL, 0);
          } else {
               memcpy(fh_desc->start, fh_data,
                      fh_len);
               fh_desc->len = fh_len;
          }
          break;

          /* Integer IDs */

     case FSAL_DIGEST_FILEID2:
          memcpy(fh_desc->start, &VINODE(handle), FSAL_DIGEST_SIZE_FILEID2);
          fh_desc->len = FSAL_DIGEST_SIZE_FILEID2;
          break;
     case FSAL_DIGEST_FILEID3:
          memcpy(fh_desc->start, &VINODE(handle).ino.val,
                 FSAL_DIGEST_SIZE_FILEID3);
          fh_desc->len = FSAL_DIGEST_SIZE_FILEID3;
          break;
     case FSAL_DIGEST_FILEID4:
          memcpy(fh_desc->start, &VINODE(handle).ino.val,
                 FSAL_DIGEST_SIZE_FILEID4);
          fh_desc->len = FSAL_DIGEST_SIZE_FILEID4;
          break;

     default:
          ReturnCode(ERR_FSAL_SERVERFAULT, 0);
     }

     ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* FSAL_DigestHandle */

/**
 * @brief Bring in a wire handle
 *
 * All we do here is adjust the descriptor length based on knowing the
 * internals of struct file_handle and let the upper level handle
 * memcpy, hash lookup and/or compare.  No copies anymore.
 *
 * @param extexport [in] The export handle
 * @param in_type [in] The type of digest to be expanded
 * @param fh_desc [in,out] Descriptor for buffer
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */

fsal_status_t
CEPHFSAL_ExpandHandle(fsal_export_context_t *extexport,
                      fsal_digesttype_t in_type,
                      struct fsal_handle_desc *fh_desc)
{
     cephfsal_handle_t* handle = (cephfsal_handle_t*) fh_desc->start;
     cephfsal_export_context_t* export = (cephfsal_export_context_t*)extexport;
     struct ceph_mount_info *cmount = export->cmount;
     int rc = 0;

     if (in_type != FSAL_DIGEST_SIZEOF) {
          if (fh_desc->len != sizeof(handle->data)) {
               LogMajor(COMPONENT_FSAL,
                        "VFS ExpandHandle: size mismatch. "
                        "should be %zu, got %zu",
                        sizeof(VINODE(handle)), fh_desc->len);
               ReturnCode(ERR_FSAL_SERVERFAULT, 0);
          }

#ifdef _PNFS
          if (handle->data.layout.fl_stripe_unit == 0) {
               rc = ceph_ll_connectable_m(cmount, &VINODE(handle),
                                          handle->data.parent_ino,
                                          handle->data.parent_hash);
               if (rc < 0) {
                    ReturnCode(posix2fsal_error(rc), 0);
               }
          }
#else /* !_PNFS */
          rc = ceph_ll_connectable_m(cmount, &VINODE(handle),
                                     handle->data.parent_ino,
                                     handle->data.parent_hash);
          if (rc < 0) {
               ReturnCode(posix2fsal_error(rc), 0);
          }
#endif /* !_PNFS */
     } else {
          fh_desc->len = sizeof(handle->data);
     }

     ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error),
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t CEPHFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t CEPHFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_common_info */

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxfilesize);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxlink);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxnamelen);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxpathlen);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, no_trunc);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, chown_restricted);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, case_insensitive);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, case_preserving);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, fh_expire_type);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, link_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, symlink_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, named_attr);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, unique_handles);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lease_time);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, acl_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, cansettime);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, homogenous);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, supported_attrs);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxread);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxwrite);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, umask);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, auth_exportpath_xdev);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, xattr_access_rights);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t CEPHFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  strcpy(&((cephfs_specific_initinfo_t)
           out_parameter->fs_specific_info).cephserver[0], "localhost");

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_load_FSAL_parameter_from_conf,
 * FSAL_load_FS_common_parameter_from_conf,
 * FSAL_load_FS_specific_parameter_from_conf:
 *
 * Those functions initialize the FSAL init parameter
 * structure from a configuration structure.
 *
 * \param in_config (input):
 *        Structure that represents the parsed configuration file.
 * \param out_parameter (ouput)
 *        FSAL initialization structure filled according
 *        to the configuration file given as parameter.
 *
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_NOENT (missing a mandatory stanza in config file),
 *         ERR_FSAL_INVAL (invalid parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 */

/* load FSAL init info */

fsal_status_t CEPHFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  int DebugLevel = -1;

  block = config_FindItemByName(in_config, CONF_LABEL_FSAL);

  /* cannot read item */

  if(block == NULL)
    ReturnCode(ERR_FSAL_NOENT, 0);
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    ReturnCode(ERR_FSAL_INVAL, 0);

  /* read variable for fsal init */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      if(!STRCMP(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if(DebugLevel == -1)
            {
              ReturnCode(ERR_FSAL_INVAL, -1);
            }

        }
      else if(!STRCMP(key_name, "Max_FS_calls"))
        {

          int maxcalls = s_read_int(key_value);

          if(maxcalls < 0)
            {
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fsal_info.max_fs_calls = (unsigned int)maxcalls;

        }
      else
        {
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }


  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t CEPHFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t * out_parameter)
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
     pnfs_supported
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

          int boolv = StrToBoolean(key_value);

          if(boolv == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, link_support,
                             FSAL_INIT_MAX_LIMIT, boolv);

        }
      else if(!STRCMP(key_name, "symlink_support"))
        {
          int boolv = StrToBoolean(key_value);

          if(boolv == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, symlink_support,
                             FSAL_INIT_MAX_LIMIT, boolv);
        }
      else if(!STRCMP(key_name, "cansettime"))
        {
          int boolv = StrToBoolean(key_value);

          if(boolv == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, cansettime,
                             FSAL_INIT_MAX_LIMIT, boolv);

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

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, maxread,
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

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, maxwrite,
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

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, umask,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else if(!STRCMP(key_name, "auth_xdev_export"))
        {
          int boolv = StrToBoolean(key_value);

          if(boolv == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, auth_exportpath_xdev,
                             FSAL_INIT_FORCE_VALUE, boolv);
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

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, xattr_access_rights,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
#ifdef _PNFS_MDS
      else if(!STRCMP(key_name, "pnfs_supported"))
        {
          int pnfs_supported = StrToBoolean(key_value);

          if(pnfs_supported < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info,
                             pnfs_supported,
                             FSAL_INIT_FORCE_VALUE, pnfs_supported);
        }
#endif /* !_PNFS_MDS */
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                   key_name, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }
    }
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_load_FS_common_parameter_from_conf */

/* load specific filesystem configuration options */

fsal_status_t CEPHFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_FS_SPECIFIC);

  /* cannot read item */
  if(block == NULL)
    {
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* makes an iteration on the (key, value) couplets */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      /* what parameter is it ? */

      if(!STRCMP(key_name, "cephserver"))
        {
          strncpy(&((cephfs_specific_initinfo_t)
                    out_parameter->fs_specific_info).cephserver[0],
                  key_value, FSAL_MAX_NAME_LEN);
        }
      else
        {
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
