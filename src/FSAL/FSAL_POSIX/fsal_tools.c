/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.26 $
 * \brief   miscelaneous FSAL tools.
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

char *POSIXFSAL_GetFSName()
{
  return "POSIX";
}

/** 
 * FSAL_handlecmp:
 * Compare 2 handles.
 *
 * \param handle1 (input):
 *        The first handle to be compared.
 * \param handle2 (input):
 *        The second handle to be compared.
 * \param status (output):
 *        The status of the compare operation.
 *
 * \return - 0 if handles are the same.
 *         - A non null value else.
 *         - Segfault if status is a NULL pointer.
 */

int POSIXFSAL_handlecmp(fsal_handle_t * hdl1, fsal_handle_t * hdl2,
                        fsal_status_t * status)
{
  posixfsal_handle_t * handle1 = (posixfsal_handle_t *) hdl1;
  posixfsal_handle_t * handle2 = (posixfsal_handle_t *) hdl2;
  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  return (handle1->data.id != handle2->data.id) || (handle1->data.ts != handle2->data.ts);

}

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

unsigned int POSIXFSAL_Handle_to_HashIndex(fsal_handle_t * handle,
                                           unsigned int cookie,
                                           unsigned int alphabet_len,
                                           unsigned int index_size)
{
  posixfsal_handle_t * p_handle = (posixfsal_handle_t *) handle;
  unsigned int h;
  h = (cookie * alphabet_len + ((unsigned int)p_handle->data.id ^ (unsigned int)p_handle->data.ts));
  return (3 * h + 1999) % index_size;
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

unsigned int POSIXFSAL_Handle_to_RBTIndex(fsal_handle_t * handle,
                                          unsigned int cookie)
{
#define MAGIC   0xABCD1234
  posixfsal_handle_t * p_handle = (posixfsal_handle_t *) handle;
  unsigned int h;

  h = (cookie ^ (unsigned int)p_handle->data.id ^ (unsigned int)p_handle->data.ts ^ MAGIC);
  return h;
}

/**
 * FSAL_DigestHandle :
 *  Convert an posixfsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 *
 * \param output_type (input):
 *        Indicates the type of digest to do.
 * \param in_fsal_handle (input):
 *        The handle to be converted to digest.
 * \param out_buff (output):
 *        The buffer where the digest is to be stored.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t POSIXFSAL_DigestHandle(fsal_export_context_t * expcontext, /* IN */
                                     fsal_digesttype_t output_type,     /* IN */
                                     fsal_handle_t * in_fsal_handle,     /* IN */
                                     caddr_t out_buff   /* OUT */
    )
{
  posixfsal_export_context_t * p_expcontext
    = (posixfsal_export_context_t *) expcontext;
  posixfsal_handle_t * p_in_fsal_handle = (posixfsal_handle_t *) in_fsal_handle;
  /* sanity checks */
  if(!p_in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(fsal_u64_t) + sizeof(int) > FSAL_DIGEST_SIZE_HDLV2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, p_in_fsal_handle, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(fsal_u64_t) + sizeof(int) > FSAL_DIGEST_SIZE_HDLV3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, p_in_fsal_handle, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(fsal_u64_t) + sizeof(int) > FSAL_DIGEST_SIZE_HDLV4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(out_buff, p_in_fsal_handle, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(ino_t) > FSAL_DIGEST_SIZE_FILEID2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID2);
      memcpy(out_buff, &(p_in_fsal_handle->data.info.inode), sizeof(ino_t));

      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(ino_t) > FSAL_DIGEST_SIZE_FILEID3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID3);
      memcpy(out_buff, &(p_in_fsal_handle->data.info.inode), sizeof(ino_t));
      break;

      /* FileId digest for NFSv4 */

    case FSAL_DIGEST_FILEID4:

#ifndef _NO_CHECKS
      /* sanity check about output size */

      if(sizeof(ino_t) > FSAL_DIGEST_SIZE_FILEID4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID4);
      memcpy(out_buff, &(p_in_fsal_handle->data.info.inode), sizeof(ino_t));
      break;

      /* Nodetype digest. */
    case FSAL_DIGEST_NODETYPE:

#ifndef _NO_CHECKS

      if(sizeof(fsal_nodetype_t) > FSAL_DIGEST_SIZE_NODETYPE)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
#endif
      memset(out_buff, 0, FSAL_DIGEST_SIZE_NODETYPE);
      memcpy(out_buff, &(p_in_fsal_handle->data.info.ftype), sizeof(fsal_nodetype_t));
      break;
    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_ExpandHandle :
 *  Convert a buffer extracted from NFS handles
 *  to an FSAL handle.
 *
 * \param in_type (input):
 *        Indicates the type of digest to be expanded.
 * \param in_buff (input):
 *        Pointer to the digest to be expanded.
 * \param out_fsal_handle (output):
 *        The handle built from digest.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t POSIXFSAL_ExpandHandle(fsal_export_context_t * expcontext, /* IN */
                                     fsal_digesttype_t in_type, /* IN */
                                     caddr_t in_buff,   /* IN */
                                     fsal_handle_t * out_fsal_handle     /* OUT */
    )
{
  posixfsal_export_context_t * p_expcontext
    = (posixfsal_export_context_t *) expcontext;
  posixfsal_handle_t * p_out_fsal_handle = (posixfsal_handle_t *) out_fsal_handle;

  /* sanity checks */
  if(!p_out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:
      memset(p_out_fsal_handle, 0, sizeof(posixfsal_handle_t));
      memcpy(p_out_fsal_handle, in_buff, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:
      memset(p_out_fsal_handle, 0, sizeof(posixfsal_handle_t));
      memcpy(p_out_fsal_handle, in_buff, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:
      memset(p_out_fsal_handle, 0, sizeof(posixfsal_handle_t));
      memcpy(p_out_fsal_handle, in_buff, sizeof(fsal_u64_t) + sizeof(int));
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
fsal_status_t POSIXFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t POSIXFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
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
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support_owner);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support_async_block);
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

fsal_status_t POSIXFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  posixfs_specific_initinfo_t * p_init_info;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */
  p_init_info = (posixfs_specific_initinfo_t *)&out_parameter->fs_specific_info;

#ifdef _USE_PGSQL

  /* pgsql db */
  strcpy(p_init_info->dbparams.host, "localhost");
  strcpy(p_init_info->dbparams.port, "5432");
  p_init_info->dbparams.dbname[0] = '\0';
  p_init_info->dbparams.login[0] = '\0';
  p_init_info->dbparams.passwdfile[0] = '\0';

#elif defined(_USE_MYSQL)

  strcpy(p_init_info->dbparams.host, "localhost");
  strcpy(p_init_info->dbparams.port, "");
  p_init_info->dbparams.dbname[0] = '\0';
  p_init_info->dbparams.login[0] = '\0';
  p_init_info->dbparams.passwdfile[0] = '\0';

#endif

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

fsal_status_t POSIXFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
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

  /* Default : NIV_CRIT */

  if(DebugLevel != -1)
    SetComponentLogLevel(COMPONENT_FSAL, DebugLevel);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t POSIXFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter)
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
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
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
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
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
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
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

/* load specific filesystem configuration options */

fsal_status_t POSIXFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                             fsal_parameter_t *
                                                             out_parameter)
{
  posixfs_specific_initinfo_t * p_init_info;
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */
  p_init_info = (posixfs_specific_initinfo_t *)&out_parameter->fs_specific_info;

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
      if(!STRCMP(key_name, "DB_Host"))
        {
          strncpy(p_init_info->dbparams.host,
                  key_value, FSAL_MAX_DBHOST_NAME_LEN);
        }
      else if(!STRCMP(key_name, "DB_Port"))
        {
          int port;
          port = atoi(key_value);       /* XXX: replace atoi by my_atoi ?! */
          if(port <= 0 || port > USHRT_MAX)
            {
              LogCrit(COMPONENT_CONFIG,
                   "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer (< %i) expected.",
                   key_name, USHRT_MAX);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          strncpy(p_init_info->dbparams.port,
                  key_value, FSAL_MAX_DBPORT_STR_LEN);
        }
      else if(!STRCMP(key_name, "DB_Name"))
        {
          strncpy(p_init_info->dbparams.dbname,
                  key_value, FSAL_MAX_DB_NAME_LEN);
        }
      else if(!STRCMP(key_name, "DB_Login"))
        {
          strncpy(p_init_info->dbparams.login,
                  key_value, FSAL_MAX_DB_LOGIN_LEN);
        }
      else if(!STRCMP(key_name, "DB_keytab"))
        {
          strncpy(p_init_info->dbparams.passwdfile,
                  key_value, FSAL_MAX_PATH_LEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
               "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
               key_name, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }
    }

  if(p_init_info->dbparams.host[0] == '\0'
     || p_init_info->dbparams.dbname[0] == '\0')
    {
      LogCrit(COMPONENT_CONFIG,
           "FSAL LOAD PARAMETER: DB_Host and DB_Name MUST be specified in the configuration file");
      ReturnCode(ERR_FSAL_NOENT, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
