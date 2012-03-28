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

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include <string.h>

/* case unsensitivity */
#define STRCMP   strcasecmp

char *XFSFSAL_GetFSName()
{
  return "XFS";
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

int XFSFSAL_handlecmp(fsal_handle_t * hdl1, fsal_handle_t * hdl2,
                      fsal_status_t * status)
{
  xfsfsal_handle_t * handle1 = (xfsfsal_handle_t *)hdl1;
  xfsfsal_handle_t * handle2 = (xfsfsal_handle_t *)hdl2;
  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  if((handle1->data.inode != handle2->data.inode) ||
     (handle1->data.type != handle2->data.type) ||
     (handle1->data.handle_len != handle2->data.handle_len))
    return 1;

  return memcmp(handle1->data.handle_val, handle2->data.handle_val, handle2->data.handle_len);
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
unsigned int XFSFSAL_Handle_to_HashIndex(fsal_handle_t * handle,
                                         unsigned int cookie,
                                         unsigned int alphabet_len,
                                         unsigned int index_size)
{
  xfsfsal_handle_t * p_handle = (xfsfsal_handle_t *)handle;
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod = 0;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.handle_len % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < p_handle->data.handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.handle_val[cpt]), sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.handle_len - mod; cpt < p_handle->data.handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.handle_val[cpt];
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

unsigned int XFSFSAL_Handle_to_RBTIndex(fsal_handle_t * handle, unsigned int cookie)
{
  xfsfsal_handle_t * p_handle = (xfsfsal_handle_t *)handle;
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod = 0;

  h = cookie;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.handle_len % sizeof(unsigned int);

  for(cpt = 0; cpt < p_handle->data.handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.handle_val[cpt]), sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.handle_len - mod; cpt < p_handle->data.handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.handle_val[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

static ssize_t xfs_sizeof_handle(const xfsfsal_handle_t *hdl)
{
	/* data.handle_len is unsigned */
	if(hdl->data.handle_len >= FSAL_XFS_HANDLE_LEN)
	  {
		LogMajor(COMPONENT_FSAL, "Incorrect XFS handle length %d",
			 hdl->data.handle_len);
		return (size_t)-1;
	  }
	return offsetof(xfsfsal_handle_t, data.handle_val) + hdl->data.handle_len;
}

/**
 * FSAL_DigestHandle :
 *  Convert an xfsfsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 *
 * \param output_type (input):
 *        Indicates the type of digest to do.
 * \param in_fsal_handle (input):
 *        The handle to be converted to digest.
 * \param fh_desc (input/output):
 *        The buffer where the digest is to be stored.
 *        On input fh_desc->len is set to the size of the buffer,
 *        on return fh_desc->len is used to indicate how many bytes
 *        have been copied into the buffer at fh_desc->start.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t XFSFSAL_DigestHandle(fsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t output_type,      /* IN */
                                   fsal_handle_t * handle, /* IN */
                                   struct fsal_handle_desc * fh_desc  /* OUT */
    )
{
  const xfsfsal_handle_t * xfs_handle = (const xfsfsal_handle_t *)handle;
  const void *start;
  ssize_t sz;
  unsigned int ino32;

  /* sanity checks */
  if(!handle || !fh_desc || !fh_desc->start || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
      sz = xfs_sizeof_handle(xfs_handle);
      start = xfs_handle;
      break;

    case FSAL_DIGEST_FILEID2:
      ino32 = my_low32m(xfs_handle->data.inode);
      if (ino32 != xfs_handle->data.inode)
	  ReturnCode(ERR_FSAL_OVERFLOW, 0);
      sz = sizeof(ino32);
      start = &ino32;
      break;

    case FSAL_DIGEST_FILEID3:
    case FSAL_DIGEST_FILEID4:
      sz = sizeof(xfs_handle->data.inode);
      start = &xfs_handle->data.inode;  
      break;

    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

    if(fh_desc->len < sz)
      {
	LogMajor(COMPONENT_FSAL,
		 "buffer too small - need %zd, have %zd", sz, fh_desc->len);
	ReturnCode(ERR_FSAL_TOOSMALL, 0);
      }
    memcpy(fh_desc->start, start, sz);
    fh_desc->len = sz;
    ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_ExpandHandle :
 *  Verify handle - mostly used to check that the size matches.
 *
 * \param in_type (input):
 *        Indicates the type of digest to be expanded.
 * \param fh_desc (input/output):
 *        The handle built from digest.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t XFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t in_type,   /* IN */
                                   struct fsal_handle_desc *fh_desc /* IN/OUT */
    )
{
  ssize_t fh_size;
  const xfsfsal_handle_t *xh = (const xfsfsal_handle_t *)fh_desc->start;

  /* sanity checks */
  if( !fh_desc || !fh_desc->start)
    ReturnCode(ERR_FSAL_FAULT, 0);

  fh_size = xfs_sizeof_handle(xh);
  if(fh_size < 0)
    ReturnCode(ERR_FSAL_BADHANDLE, 0);

  switch(xh->data.type)
    {
    case DT_LNK:
    case DT_BLK:
    case DT_SOCK:
    case DT_CHR:
    case DT_FIFO:
    case DT_REG:
    case DT_DIR:
	break;
    default:
	LogMajor(COMPONENT_FSAL,
		 "Corrupted filehandle - unexpected file type %d",
		 xh->data.type);
	ReturnCode(ERR_FSAL_BADHANDLE, EINVAL);
    }

  switch(in_type)
    {
    case FSAL_DIGEST_NFSV2:
      if(fh_desc->len < fh_size)
        {
          LogMajor(COMPONENT_FSAL,
		   "buffer too small for handle.  should be %zd, got %zd",
		   fh_size, fh_desc->len);
	  ReturnCode(ERR_FSAL_SERVERFAULT, 0);
	}
      break;
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
      if(fh_desc->len != fh_size)
	{
	  LogMajor(COMPONENT_FSAL,
		   "size mismatch for handle.  should be %zd, got %zd",
		   fh_size, fh_desc->len);
	  ReturnCode(ERR_FSAL_BADHANDLE, 0);
	}
      break;
    case FSAL_DIGEST_SIZEOF:
      break;
    default: /* Catch FILEID2, FILEID3, FILEID4 */
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

fsal_status_t XFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t XFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter)
{

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
