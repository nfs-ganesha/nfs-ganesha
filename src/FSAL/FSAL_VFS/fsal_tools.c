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
#include <stddef.h>

/* case unsensitivity */
#define STRCMP   strcasecmp

char *VFSFSAL_GetFSName()
{
  return "VFS";
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

int VFSFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                   fsal_status_t * status)
{
  vfsfsal_handle_t * handle1 = (vfsfsal_handle_t *)handle_1;
  vfsfsal_handle_t * handle2 = (vfsfsal_handle_t *)handle_2;

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  if(handle1->data.vfs_handle.handle_bytes != handle2->data.vfs_handle.handle_bytes)
    return -2;

  if(memcmp(&handle1->data.vfs_handle, &handle2->data.vfs_handle, sizeof( vfs_file_handle_t) ) )
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
unsigned int VFSFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size)
{
  vfsfsal_handle_t * p_handle = (vfsfsal_handle_t *)handle;
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.vfs_handle.handle_bytes % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < p_handle->data.vfs_handle.handle_bytes - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.vfs_handle.handle[cpt]), sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.vfs_handle.handle_bytes - mod; cpt < p_handle->data.vfs_handle.handle_bytes; cpt++ )
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.vfs_handle.handle[cpt];
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

unsigned int VFSFSAL_Handle_to_RBTIndex(fsal_handle_t *handle, unsigned int cookie)
{
  vfsfsal_handle_t * p_handle = (vfsfsal_handle_t *)handle;
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod;

  h = cookie;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.vfs_handle.handle_bytes % sizeof(unsigned int);

  for(cpt = 0; cpt < p_handle->data.vfs_handle.handle_bytes - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.vfs_handle.handle[cpt]), sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.vfs_handle.handle_bytes - mod; cpt < p_handle->data.vfs_handle.handle_bytes;
          cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.vfs_handle.handle[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

/**
 * FSAL_DigestHandle :
 *  Convert an vfsfsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 *
 * \param output_type (input):
 *        Indicates the type of digest to do.
 * \param in_fsal_handle (input):
 *        The handle to be converted to digest.
 * \param fh_desc (output):
 *        The descriptor for the buffer where the digest is to be stored.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t VFSFSAL_DigestHandle(fsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t output_type,       /* IN */
                                   fsal_handle_t *in_fsal_handle, /* IN */
                                   struct fsal_handle_desc *fh_desc     /* IN/OUT */
    )
{
  uint32_t ino32;
  uint64_t ino64;
  size_t fh_size;
  vfsfsal_handle_t * p_in_fsal_handle = (vfsfsal_handle_t *)in_fsal_handle;

  /* sanity checks */
  if(!p_in_fsal_handle || !fh_desc || !fh_desc->start || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFS handle digest */
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
      fh_size = vfs_sizeof_handle((struct file_handle *)in_fsal_handle);
      if(fh_desc->len < fh_size)
        {
	  LogMajor(COMPONENT_FSAL,
		   "VFS DigestHandle: space too small for handle.  need %lu, have %lu",
		   fh_size, fh_desc->len);
	  ReturnCode(ERR_FSAL_TOOSMALL, 0);
	}
      memcpy(fh_desc->start, (caddr_t)p_in_fsal_handle, fh_size);
      fh_desc->len = fh_size;
      break;
  
   case FSAL_DIGEST_FILEID2:
      memcpy(fh_desc->start, p_in_fsal_handle->data.vfs_handle.handle, FSAL_DIGEST_SIZE_FILEID2);
      fh_desc->len = FSAL_DIGEST_SIZE_FILEID2;
      break;

   case FSAL_DIGEST_FILEID3:
      /* Extracting FileId from VFS handle requires internal knowledge on the handle's structure 
       * which is given by 'struct fid' in kernel's sources. For most FS, it looks like this:
       * struct fid {
	union {
		struct {
			u32 ino;
			u32 gen;
			u32 parent_ino;
			u32 parent_gen;
		} i32;
 		struct {
 			u32 block;
 			u16 partref;
 			u16 parent_partref;
 			u32 generation;
 			u32 parent_block;
 			u32 parent_generation;
 		} udf;
		__u32 raw[0];
	};
      }; 
      This means that in most cases, fileid will be found in the first 32 bits of the structure. But there are exception
      BTRFS is one of them, with a struct fid like this 
      struct btrfs_fid {
	u64 objectid;
	u64 root_objectid;
	u32 gen;

	u64 parent_objectid;
	u32 parent_gen;

	u64 parent_root_objectid;
      } __attribute__ ((packed));*/
      memcpy(&ino32, p_in_fsal_handle->data.vfs_handle.handle, sizeof(ino32));
      ino64 = ino32;
      memcpy(fh_desc->start, &ino64, FSAL_DIGEST_SIZE_FILEID3);
      fh_desc->len = FSAL_DIGEST_SIZE_FILEID3;
      break;


   case FSAL_DIGEST_FILEID4:
      memcpy(&ino32, p_in_fsal_handle->data.vfs_handle.handle, sizeof(ino32));
      ino64 = ino32;
      memcpy(fh_desc->start, &ino64, FSAL_DIGEST_SIZE_FILEID4);
      fh_desc->len = FSAL_DIGEST_SIZE_FILEID4;
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
 * All we do here is adjust the descriptor length based on knowing the internals
 * of struct file_handle and let the upper level do handle memcpy, hash
 * lookup and/or compare.  No copies anymore.
 *
 * \param in_type (input):
 *        Indicates the type of digest to be expanded.
 * \param fh_desc (input/output):
 *        digest descriptor.  returns length to be copied
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t VFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,     /* IN not used */
                                   fsal_digesttype_t in_type,   /* IN */
                                   struct fsal_handle_desc *fh_desc  /* IN/OUT */ )
{
  struct file_handle *hdl;
  size_t fh_size;

  /* sanity checks */
  if( !fh_desc || !fh_desc->start)
    ReturnCode(ERR_FSAL_FAULT, 0);

  hdl = (struct file_handle *)fh_desc->start;
  fh_size = vfs_sizeof_handle(hdl);
  if(in_type == FSAL_DIGEST_NFSV2)
    {
      if(fh_desc->len < fh_size)
        {
          LogMajor(COMPONENT_FSAL,
		   "VFS ExpandHandle: V2 size too small for handle.  should be %lu, got %lu",
		   fh_size, fh_desc->len);
	  ReturnCode(ERR_FSAL_SERVERFAULT, 0);
	}
    }
  else if(in_type != FSAL_DIGEST_SIZEOF && fh_desc->len != fh_size)
    {
      LogMajor(COMPONENT_FSAL,
	       "VFS ExpandHandle: size mismatch for handle.  should be %lu, got %lu",
	       fh_size, fh_desc->len);
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }
  fh_desc->len = fh_size;  /* pass back the actual size */
  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */

fsal_status_t VFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */

#ifdef _USE_PGSQL

  /* pgsql db */
q  strcpy(out_parameter->fs_specific_info.dbparams.host, "localhost");
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

/* load FSAL init info */


/* load specific filesystem configuration options */
fsal_status_t VFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter)
{

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
