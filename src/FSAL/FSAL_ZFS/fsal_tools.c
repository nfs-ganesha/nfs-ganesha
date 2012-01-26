/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/09 12:16:07 $
 * \version $Revision: 1.28 $
 * \brief   miscelaneous FSAL tools that can be called from outside.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "fsal_common.h"
#include <string.h>

/* case unsensitivity */
#define STRCMP   strcasecmp

char *ZFSFSAL_GetFSName()
{
  return "ZFS";
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

int ZFSFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                   fsal_status_t * status)
{

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  return memcmp(handle1, handle2, sizeof(zfsfsal_handle_t));
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

unsigned int ZFSFSAL_Handle_to_HashIndex(fsal_handle_t * handle,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size)
{
  zfsfsal_handle_t * p_handle = (zfsfsal_handle_t *)handle;

  /* >> here must be your implementation of your zfsfsal_handle_t hashing */
  return (3 * (unsigned int)(p_handle->data.zfs_handle.inode*p_handle->data.zfs_handle.generation*(p_handle->data.i_snap+1)) + 1999 + cookie) % index_size;

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

unsigned int ZFSFSAL_Handle_to_RBTIndex(fsal_handle_t * handle, unsigned int cookie)
{
  zfsfsal_handle_t * p_handle = (zfsfsal_handle_t *)handle;

  /* >> here must be your implementation of your zfsfsal_handle_t hashing << */
  return (unsigned int)(0xABCD1234 ^ (p_handle->data.zfs_handle.inode*p_handle->data.zfs_handle.generation*(p_handle->data.i_snap+1)) ^ cookie);

}

/**
 * FSAL_DigestHandle :
 *  Convert an zfsfsal_handle_t to a buffer
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

fsal_status_t ZFSFSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t output_type,  /* IN */
                                fsal_handle_t * in_handle, /* IN */
                                caddr_t out_buff        /* OUT */
    )
{
  uint32_t ino32;
  zfsfsal_handle_t * in_fsal_handle = (zfsfsal_handle_t *)in_handle;

  /* sanity checks */
  if(!in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:
      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, in_fsal_handle, sizeof(in_fsal_handle->data) );
      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:
      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, in_fsal_handle, sizeof(in_fsal_handle->data));
      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:
      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(out_buff, in_fsal_handle, sizeof(in_fsal_handle->data));
      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:
      ino32 = (uint32_t)(in_fsal_handle->data.zfs_handle.inode);
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID2);
      memcpy(out_buff, &ino32, sizeof(uint32_t));
      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID3);
      memcpy(out_buff, &(in_fsal_handle->data.zfs_handle.inode), sizeof(fsal_u64_t));
      break;

      /* FileId digest for NFSv4 */

    case FSAL_DIGEST_FILEID4:
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID4);
      memcpy(out_buff, &(in_fsal_handle->data.zfs_handle.inode), sizeof(fsal_u64_t));
      break;

    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_DigestHandle */

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
fsal_status_t ZFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t in_type,      /* IN */
                                caddr_t in_buff,        /* IN */
                                fsal_handle_t * out_handle /* OUT */
    )
{
  zfsfsal_handle_t * out_fsal_handle = (zfsfsal_handle_t *)out_handle;

  /* sanity checks */
  if(!out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

    case FSAL_DIGEST_NFSV2:
      memset(out_fsal_handle, 0, sizeof(zfsfsal_handle_t));
      memcpy(out_fsal_handle, in_buff, sizeof(out_fsal_handle->data));
      break;

    case FSAL_DIGEST_NFSV3:
      memset(out_fsal_handle, 0, sizeof(zfsfsal_handle_t));
      memcpy(out_fsal_handle, in_buff, sizeof(out_fsal_handle->data));
      break;

    case FSAL_DIGEST_NFSV4:
      memset(out_fsal_handle, 0, sizeof(zfsfsal_handle_t));
      memcpy(out_fsal_handle, in_buff, sizeof(out_fsal_handle->data));
      break;

    default:
      /* Invalid input digest type. */
      ReturnCode(ERR_FSAL_INVAL, 0);
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

fsal_status_t ZFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* >> set your default FS configuration into the
     out_parameter->fs_specific_info structure << */

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

/* load specific filesystem configuration options */

fsal_status_t ZFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                        fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;
  zfsfs_specific_initinfo_t *specific_info =
	  (zfsfs_specific_initinfo_t *) &out_parameter->fs_specific_info;

  block = config_FindItemByName(in_config, CONF_LABEL_FS_SPECIFIC);

  /* cannot read item */
  if(block == NULL)
    {
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FS_SPECIFIC);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
                 CONF_LABEL_FS_SPECIFIC);
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
          LogCrit(COMPONENT_FSAL,
              "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
               var_index, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      /* what parameter is it ? */

      if(!STRCMP(key_name, "zpool"))
          strncpy(specific_info->psz_zpool, key_value, 256);
      else if(!STRCMP(key_name, "auto_snapshots"))
          specific_info->auto_snapshots = !STRCMP(key_value, "TRUE");
      else if(!STRCMP(key_name, "snap_hourly_prefix"))
          strncpy(specific_info->psz_snap_hourly_prefix, key_value, 256);
      else if(!STRCMP(key_name, "snap_hourly_time"))
          specific_info->snap_hourly_time = atoi(key_value);
      else if(!STRCMP(key_name, "snap_hourly_number"))
          specific_info->snap_hourly_number = atoi(key_value);
      else if(!STRCMP(key_name, "snap_daily_prefix"))
          strncpy(specific_info->psz_snap_daily_prefix, key_value, 256);
      else if(!STRCMP(key_name, "snap_daily_time"))
          specific_info->snap_daily_time = atoi(key_value);
      else if(!STRCMP(key_name, "snap_daily_number"))
          specific_info->snap_daily_number = atoi(key_value);
      else
        {
          LogCrit(COMPONENT_FSAL,
              "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
               key_name, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
