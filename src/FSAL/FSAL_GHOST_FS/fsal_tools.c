/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:24 $
 * \version $Revision: 1.6 $
 * \brief   miscelaneous FSAL tools.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "common_utils.h"
#include <string.h>
#include "stuff_alloc.h"

#define STRCMP  strcasecmp

char *FSAL_GetFSName()
{
  return "GHOSTFS";
}

/** Compare 2 handles.
 *  \return - 0 if handle are the same
 *          - A non null value else.
 */
int FSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                   fsal_status_t * status)
{

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  /* These are memory adresses,
   * we can compare them as integers. */
  return memcmp(handle1, handle2, sizeof(fsal_handle_t));

}

#define ReturnCode( _code_, _minor_ ) do {                           \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;\
               (_struct_status_).major = (_code_) ;          \
               (_struct_status_).minor = (_minor_) ;         \
               return (_struct_status_);                     \
              } while(0)

/**
 * FSAL_Handle_to_HashIndex
 * This function is used for hashing a FSAL handle
 * in order to dispatch entries into the hash table array.
 *
 * \param p_handle      The handle to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 * \param alphabet_len  Parameter for polynomial hashing algorithm
 * \param index_size    The range of hash value will be [0..index_size-1]
 *
 * \return The hash value
 */

unsigned int FSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size)
{
  unsigned int h =
      ~(cookie * alphabet_len +
        ((unsigned int)p_handle->inode ^ (unsigned int)p_handle->magic));
  return h % index_size;
}

/*
 * FSAL_Handle_to_RBTIndex
 * This function is used for generating a RBT node ID
 * in order to identify entries into the RBT.
 *
 * \param p_handle      The handle to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 *
 * \return The hash value
 */

unsigned int FSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, unsigned int cookie)
{
  return (cookie + (unsigned int)p_handle->inode ^ (unsigned int)p_handle->magic);
}

/** FSAL_DigestHandle :
 *  convert an fsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 */
fsal_status_t FSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t output_type,  /* IN */
                                fsal_handle_t * in_fsal_handle, /* IN */
                                caddr_t out_buff        /* OUT */
    )
{

  /* sanity checks */
  if(!in_fsal_handle || !out_buff)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
      /* for the moment,
       * the handle must always be converted
       * to a 25 bytes handle.
       */
      memcpy(out_buff, in_fsal_handle, sizeof(fsal_handle_t));
      memset(out_buff + sizeof(fsal_handle_t), 0,
             FSAL_DIGEST_SIZE_HDLV2 - sizeof(fsal_handle_t));
      break;
    case FSAL_DIGEST_FILEID2:
      /* in ghostfs fileid = handle */
      /* in NFSV2, we truncate the fileid to a 32 bits entity,
         keeping low bits.
       */
      {
        int target32 = (int)(in_fsal_handle->inode);
        LogFullDebug(COMPONENT_FSAL, "Source=%p Target=%#X",
                     in_fsal_handle->inode, target32);
        memcpy(out_buff, &target32, sizeof(int));
      }

      break;
    case FSAL_DIGEST_FILEID3:
      /* sanity check */
      /* in ghostfs fileid = handle */
      if(sizeof(fsal_handle_t) > FSAL_DIGEST_SIZE_FILEID3)
        {
          ReturnCode(ERR_FSAL_SERVERFAULT, 0);
        }
      memcpy(out_buff, in_fsal_handle, sizeof(fsal_handle_t));
      /* complete with zeros */
      if(sizeof(fsal_handle_t) < FSAL_DIGEST_SIZE_FILEID3)
        memset(out_buff + sizeof(fsal_handle_t), 0,
               FSAL_DIGEST_SIZE_FILEID3 - sizeof(fsal_handle_t));
      break;
    case FSAL_DIGEST_FILEID4:
      /* sanity check */
      /* in ghostfs fileid = handle */
      if(sizeof(fsal_handle_t) > FSAL_DIGEST_SIZE_FILEID4)
        {
          ReturnCode(ERR_FSAL_SERVERFAULT, 0);
        }
      memcpy(out_buff, in_fsal_handle, sizeof(fsal_handle_t));
      /* complete with zeros */
      if(sizeof(fsal_handle_t) < FSAL_DIGEST_SIZE_FILEID4)
        memset(out_buff + sizeof(fsal_handle_t), 0,
               FSAL_DIGEST_SIZE_FILEID4 - sizeof(fsal_handle_t));
      break;
    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/** FSAL_ExpandHandle :
 *  convert a buffer extracted from NFS handles
 *  to an FSAL handle.
 */
fsal_status_t FSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t in_type,      /* IN */
                                caddr_t in_buff,        /* IN */
                                fsal_handle_t * out_fsal_handle /* OUT */
    )
{

  /* sanity checks */
  if(!out_fsal_handle || !in_buff)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
      /* for the moment,
       * the handle must always be converted
       * from a 25 bytes handle.
       */
      memcpy(out_fsal_handle, in_buff, sizeof(fsal_handle_t));
      break;
    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t FSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t FSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t FSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */

  out_parameter->fs_specific_info.root_mode = unix2fsal_mode(0755);
  out_parameter->fs_specific_info.root_owner = 0;
  out_parameter->fs_specific_info.root_group = 0;
  out_parameter->fs_specific_info.dot_dot_root_eq_root = TRUE;
  out_parameter->fs_specific_info.root_access = TRUE;

  out_parameter->fs_specific_info.dir_list = NULL;

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

fsal_status_t FSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                 fsal_parameter_t * out_parameter)
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
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
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
              LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: ERROR: Invalid debug level name: \"%s\".",
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

          out_parameter->fsal_info.max_fs_calls = (unsigned int)maxcalls;

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

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t FSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
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
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
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
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, link_support,
                             FSAL_INIT_MAX_LIMIT, bool);

        }
      else if(!STRCMP(key_name, "symlink_support"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, symlink_support,
                             FSAL_INIT_MAX_LIMIT, bool);
        }
      else if(!STRCMP(key_name, "cansettime"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, cansettime,
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
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, auth_exportpath_xdev,
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

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, xattr_access_rights,
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

}                               /* FSAL_load_FS_common_parameter_from_conf */

static int ParseGhostFsDirDef(char *string, ghostfs_dir_def_t * p_dir)
{
  char buff[2048];
  char *path, *modestr, *ownstr, *grpstr;

  int mode;
  int owner, group;

  strncpy(buff, string, 2048);

  path = strtok(buff, ":");
  if(path == NULL)
    return -1;

  modestr = strtok(NULL, ":");
  if(modestr == NULL)
    return -1;

  ownstr = strtok(NULL, ":");
  if(ownstr == NULL)
    return -1;

  grpstr = strtok(NULL, ":");
  if(grpstr == NULL)
    return -1;

  if(strtok(NULL, ":") != NULL)
    return -1;

  /* check and get path */

  if((path[0] != '/') || (path[1] == '\0'))
    return -1;

  strncpy(p_dir->path, path, FSAL_MAX_PATH_LEN);

  /* check and get mode */

  mode = s_read_octal(modestr);

  if(mode < 0)
    return -1;
  p_dir->dir_mode = unix2fsal_mode(mode);

  /* check and get owner */

  owner = s_read_int(ownstr);

  if(owner < 0)
    return -1;
  p_dir->dir_owner = owner;

  /* check and get group */

  group = s_read_int(grpstr);

  if(group < 0)
    return -1;
  p_dir->dir_group = group;

  return 0;

}

/* load specific filesystem configuration options */

fsal_status_t FSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
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
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FS_SPECIFIC);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
                 CONF_LABEL_FS_SPECIFIC);
      ReturnCode(ERR_FSAL_INVAL, 0);
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
               var_index, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      /* does the variable exists ? */
      if(!STRCMP(key_name, "fs_root_mode"))
        {

          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.root_mode = unix2fsal_mode(mode);

        }
      else if(!STRCMP(key_name, "fs_root_owner"))
        {

          int owner = s_read_int(key_value);

          if(owner < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.root_owner = owner;

        }
      else if(!STRCMP(key_name, "fs_root_group"))
        {

          int group = s_read_int(key_value);

          if(group < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.root_group = group;

        }
      else if(!STRCMP(key_name, "dot_dot_root"))
        {

          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.dot_dot_root_eq_root = bool;

        }
      else if(!STRCMP(key_name, "predefined_dir"))
        {
          ghostfs_dir_def_t *p_dir =
              (ghostfs_dir_def_t *) Mem_Alloc(sizeof(ghostfs_dir_def_t));
          ghostfs_dir_def_t *p_cur;

          if(p_dir == NULL)
            {
              LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: ERROR ALLOCATING MEMORY");
              ReturnCode(ERR_FSAL_SERVERFAULT, errno);
            }

          /* we must parse the definition string */
          if(ParseGhostFsDirDef(key_value, p_dir) != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 'path:mode:owner:group' expected",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* put the directory at the end of the list */
          p_dir->next = NULL;

          if(out_parameter->fs_specific_info.dir_list == NULL)
            out_parameter->fs_specific_info.dir_list = p_dir;
          else
            {
              for(p_cur = out_parameter->fs_specific_info.dir_list;
                  p_cur != NULL; p_cur = p_cur->next)
                {
                  if(p_cur->next == NULL)
                    {
                      p_cur->next = p_dir;
                      break;
                    }           /*if */
                }               /*for */
            }                   /*else */

        }
      /*predefined_dir */
      else
        {
          LogCrit(COMPONENT_CONFIG,
               "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
               key_name, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
