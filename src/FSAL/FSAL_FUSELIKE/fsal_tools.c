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

char *FUSEFSAL_GetFSName()
{
  return "FUSE";
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

int FUSEFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                       fsal_status_t * status)
{
  fusefsal_handle_t * handle1 = (fusefsal_handle_t *)handle_1;
  fusefsal_handle_t * handle2 = (fusefsal_handle_t *)handle_2;

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  if((handle1->data.inode > handle2->data.inode) || (handle1->data.device > handle2->data.device))
    return 1;
  else if((handle1->data.inode < handle2->data.inode) || (handle1->data.device < handle2->data.device))
    return -1;
  else
    return 0;

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

unsigned int FUSEFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size)
{
  fusefsal_handle_t * p_handle = (fusefsal_handle_t *)handle;

  /* >> here must be your implementation of your fusefsal_handle_t hashing */
  return (3 * (unsigned int)p_handle->data.inode + 5 * (unsigned int)p_handle->data.device + 1999 +
          cookie) % index_size;

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

unsigned int FUSEFSAL_Handle_to_RBTIndex(fsal_handle_t *handle,
                                         unsigned int cookie)
{
  fusefsal_handle_t * p_handle = (fusefsal_handle_t *)handle;

  /* >> here must be your implementation of your fusefsal_handle_t hashing << */
  return (unsigned int)(0xABCD1234 ^ p_handle->data.inode ^ cookie ^ p_handle->data.device);

}

/**
 * FSAL_DigestHandle :
 *  Convert an fusefsal_handle_t to a buffer
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

fsal_status_t FUSEFSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    fsal_handle_t * in_handle, /* IN */
                                    caddr_t out_buff    /* OUT */
    )
{
  fusefsal_handle_t * in_fsal_handle = (fusefsal_handle_t *)in_handle;

  /* sanity checks */
  if(!in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, in_fsal_handle, sizeof(fusefsal_handle_t));

      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, in_fsal_handle, sizeof(fusefsal_handle_t));

      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(out_buff, in_fsal_handle, sizeof(fusefsal_handle_t));

      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:
      {
        int cast2 = (int)in_fsal_handle->data.inode;

#ifndef _NO_CHECKS

        /* sanity check about output size */

        if(sizeof(cast2) > FSAL_DIGEST_SIZE_FILEID2)
          ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

        memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID2);
        memcpy(out_buff, &cast2, sizeof(cast2));
      }
      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:
      {
        fsal_u64_t cast3 = (fsal_u64_t) in_fsal_handle->data.inode;

#ifndef _NO_CHECKS

        /* sanity check about output size */

        if(sizeof(cast3) > FSAL_DIGEST_SIZE_FILEID3)
          ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

        memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID3);
        memcpy(out_buff, &cast3, sizeof(cast3));
      }
      break;

      /* FileId digest for NFSv4 */

    case FSAL_DIGEST_FILEID4:
      {
        fsal_u64_t cast4 = (fsal_u64_t) in_fsal_handle->data.inode;

#ifndef _NO_CHECKS

        /* sanity check about output size */

        if(sizeof(cast4) > FSAL_DIGEST_SIZE_FILEID4)
          ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

        memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID4);
        memcpy(out_buff, &cast4, sizeof(cast4));
      }
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
fsal_status_t FUSEFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    fsal_handle_t * out_handle /* OUT */
    )
{
  fusefsal_handle_t * out_fsal_handle = (fusefsal_handle_t *)out_handle;

  /* sanity checks */
  if(!out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:

      memcpy(out_fsal_handle, in_buff, sizeof(fusefsal_handle_t));

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

fsal_status_t FUSEFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t FUSEFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter)
{
  int err;
  int blk_index;
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

  /* makes an iteration on the (key, value) couplets */

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

      /* what parameter is it ? */

      if(!STRCMP(key_name, "my_parameter_name1"))
        {
          /* >> interpret the parameter string and fill the fs_specific_info structure << */
        }
      else if(!STRCMP(key_name, "my_parameter_name2"))
        {
          /* >> interpret the parameter string and fill the fs_specific_info structure << */
        }
      /* etc... */
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
