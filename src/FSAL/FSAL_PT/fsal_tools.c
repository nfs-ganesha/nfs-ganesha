// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_tools.c
// Description: FSAL tools operations implmentation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------

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
 * -------------
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
#include "pt_ganesha.h"


/* case unsensitivity */
#define STRCMP   strcasecmp

char *PTFSAL_GetFSName()
{
  return "PTFS";
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

int PTFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                   fsal_status_t * status)
{
  ptfsal_handle_t *handle1 = (ptfsal_handle_t *)handle_1;
  ptfsal_handle_t *handle2 = (ptfsal_handle_t *)handle_2;

  // FSI_TRACE(FSI_DEBUG, "FSI - handlecmp\n");

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  if(memcmp
     (handle1->data.handle.f_handle, handle2->data.handle.f_handle, 
      OPENHANDLE_KEY_LEN))
    return -3;

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
unsigned int 
PTFSAL_Handle_to_HashIndex(fsal_handle_t * handle,
                           unsigned int cookie,
                           unsigned int alphabet_len, 
                           unsigned int index_size)
{
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod;
  ptfsal_handle_t *p_handle = (ptfsal_handle_t *)handle;

  // so we can see something we understand... set all hashes to 0
  return 0;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get 
   * uninitialized chars after the end of the handle. We must avoid this 
   * by skipping the last loop and doing a special processing for the 
   * last bytes */

  mod = p_handle->data.handle.handle_key_size % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < p_handle->data.handle.handle_key_size - mod; cpt += 
      sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.handle.f_handle[cpt]), 
             sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.handle.handle_key_size - mod; 
          cpt < p_handle->data.handle.handle_key_size;
          cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.handle.f_handle[cpt];
        }
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  return sum;
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

unsigned int PTFSAL_Handle_to_RBTIndex(fsal_handle_t * handle, 
                                       unsigned int cookie)
{
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod;
  ptfsal_handle_t * p_handle = (ptfsal_handle_t *)handle;

  h = cookie;
  h = *((uint64_t *)&p_handle->data.handle.f_handle) % 32767;

  return h;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get 
   * uninitialized chars after the end of the handle. We must avoid this 
   * by skipping the last loop and doing a special processing for the last 
   * bytes */

  mod = p_handle->data.handle.handle_key_size % sizeof(unsigned int);

  for(cpt = 0; cpt < p_handle->data.handle.handle_key_size - mod; cpt += 
      sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.handle.f_handle[cpt]), 
             sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.handle.handle_key_size - mod; cpt < 
          p_handle->data.handle.handle_key_size;
          cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.handle.f_handle[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

/**
 *  PTFSAL_DigestHandle :
 *  Convert an fsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 *
 * \param output_type (input):
 *        Indicates the type of digest to do.
 * \param in_fsal_handle (input):
 *        The handle to be converted to digest.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t PTFSAL_DigestHandle(fsal_export_context_t *exp_context, /* IN */
                                fsal_digesttype_t output_type,        /* IN */
                                fsal_handle_t * in_fsal_handle,       /* IN */
                                struct fsal_handle_desc *fh_desc  /* IN/OUT */
	)
{
  ptfsal_export_context_t * p_expcontext = 
    (ptfsal_export_context_t *)exp_context;
  ptfsal_handle_t * p_in_fsal_handle = (ptfsal_handle_t *)in_fsal_handle;
  size_t fh_size;


  /* sanity checks */
  if(!p_in_fsal_handle || !fh_desc || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFS handle digest */
    case FSAL_DIGEST_NFSV2:

      /* PTFS FSAL can no longer support NFS v2 */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);

    case FSAL_DIGEST_NFSV3:
      fh_size = pt_sizeof_handle((struct file_handle *)in_fsal_handle);
      if(fh_desc->len < fh_size)
        {
          LogMajor(COMPONENT_FSAL,
                   "GPFSFSAL_DigestHandle: space too small for handle.Need " 
                   "%lu, have %lu",
                   (unsigned long)fh_size, (unsigned long)fh_desc->len);
          ReturnCode(ERR_FSAL_TOOSMALL, 0);
        }
      FSI_TRACE(FSI_DEBUG, "Digest Handle");
      memcpy(fh_desc->start, (caddr_t)p_in_fsal_handle, fh_size);
      fh_desc->len = fh_size;
      break;
    case FSAL_DIGEST_NFSV4:
      fh_size = pt_sizeof_handle((struct file_handle *)in_fsal_handle);
      if(fh_desc->len < fh_size)
        {
          LogMajor(COMPONENT_FSAL,
                   "GPFSFSAL_DigestHandle: space too small for handle.Need " 
                   "%lu, have %lu",
                   (unsigned long)fh_size, (unsigned long)fh_desc->len);
          ReturnCode(ERR_FSAL_TOOSMALL, 0);
        }
      memcpy(fh_desc->start, (caddr_t)p_in_fsal_handle, fh_size);
      fh_desc->len = fh_size;
      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:

      /* PTFS FSAL can no longer support NFS v2 */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:
      FSI_TRACE (FSI_DEBUG,"DIGEST_FILEID3 memcpy");
      /* sanity check about output size */
      /* If the handle_size is the full OPENHANDLE_HANDLE_LEN then we assume 
       * it's a new style PTFS handle */
      if(p_in_fsal_handle->data.handle.handle_size < OPENHANDLE_HANDLE_LEN)
        memcpy(fh_desc->start, p_in_fsal_handle->data.handle.f_handle, 
               sizeof(uint32_t)); 
      else
        memcpy(fh_desc->start, p_in_fsal_handle->data.handle.f_handle + 
               OPENHANDLE_OFFSET_OF_FILEID, sizeof(uint64_t)); 
      break;

      /* FileId digest for NFSv4 */
    case FSAL_DIGEST_FILEID4:
      FSI_TRACE (FSI_DEBUG,"DIGEST_FILEID4 memcpy");
      if(p_in_fsal_handle->data.handle.handle_size < OPENHANDLE_HANDLE_LEN)
        memcpy(fh_desc->start, p_in_fsal_handle->data.handle.f_handle, 
               sizeof(uint32_t)); 
      else
        memcpy(fh_desc->start, p_in_fsal_handle->data.handle.f_handle + 
               OPENHANDLE_OFFSET_OF_FILEID, sizeof(uint64_t));
      break;

    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);

    }
  ptfsal_print_handle(p_in_fsal_handle);
  ptfsal_print_handle (fh_desc->start);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_ExpandHandle :
 *  Convert a buffer extracted from NFS handles
 *  to an FSAL handle.
 *
 * \param in_type (input):
 *        Indicates the type of digest to be expanded.
 * \param fh_desc (input/output):
 *        digest descriptor.  returns length to be copied 
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t PTFSAL_ExpandHandle(fsal_export_context_t *exp_context,  /* IN */
                                fsal_digesttype_t in_type,             /* IN */
                                struct fsal_handle_desc *fh_desc     /* OUT */)
{
  struct file_handle *hdl;
  size_t fh_size;

  /* sanity checks */
  if(!fh_desc || !fh_desc->start)
    ReturnCode(ERR_FSAL_FAULT, 0);

  hdl = (struct file_handle *)fh_desc->start;
  fh_size = pt_sizeof_handle(hdl);
 
  if(in_type == FSAL_DIGEST_NFSV2)
    {
      /* PTFS FSAL can no longer support NFS v2 */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);
    }
  else if(in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size)
    {
      LogMajor(COMPONENT_FSAL,
               "GPFSFSAL_ExpandHandle: size mismatch for handle. " 
               "should be %lu, got %lu",
               fh_size, fh_desc->len);
     ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

  fh_desc->len = fh_size;  /* pass back the actual size */
  FSI_TRACE(FSI_DEBUG, "expand handle %d",fh_desc->len);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */

fsal_status_t 
PTFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */

#ifdef _USE_PGSQL

  /* pgsql db */
  strcpy(out_parameter->fs_specific_info.dbparams.host, "localhost");
  strcpy(out_parameter->fs_specific_info.dbparams.port, "5432");
  out_parameter->fs_specific_info.dbparams.dbname[0] = '\0';
  out_parameter->fs_specific_info.dbparams.login[0] = '\0';
  out_parameter->fs_specific_info.dbparams.passwdfile[0] = '\0';

#elif defined(_USE_MYSQL)

  strcpy(out_parameter->fs_specific_info.dbparams.host, "localhost");
  strcpy(out_parameter->fs_specific_info.dbparams.port, "");
  out_parameter->fs_specific_info.dbparams.dbname[0] = '\0';
  out_parameter->fs_specific_info.dbparams.login[0] = '\0';
  out_parameter->fs_specific_info.dbparams.passwdfile[0] = '\0';

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

/* load specific filesystem configuration options */

fsal_status_t 
PTFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                            fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;
  ptfs_specific_initinfo_t *initinfo
	  = (ptfs_specific_initinfo_t *) &out_parameter->fs_specific_info;

  block = config_FindItemByName(in_config, CONF_LABEL_FS_SPECIFIC);

  /* cannot read item */
  if(block == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Cannot read item \"%s\" " 
              "from configuration file",
              CONF_LABEL_FS_SPECIFIC);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
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
                  "FSAL LOAD PARAMETER: ERROR reading key[%d] " 
                  "from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }
      /* does the variable exists ? */
      if(!STRCMP(key_name, "OpenByHandleDeviceFile"))
        {
          strncpy(initinfo->open_by_handle_dev_file, key_value,
                  MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "Use_Kernel_Module_Interface"))
        {
          int bool = StrToBoolean(key_value);
          if (bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value " 
                      "for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          initinfo->use_kernel_module_interface = bool;
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: " 
                  "%s (item %s)",
                  key_name, CONF_LABEL_FS_SPECIFIC);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }
    }

  if(initinfo->use_kernel_module_interface && 
     initinfo->open_by_handle_dev_file[0] == '\0')
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: OpenByHandleDeviceFile MUST be specified " 
              "in the configuration file (item %s)",
              CONF_LABEL_FS_SPECIFIC);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
