/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
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

char *LUSTREFSAL_GetFSName()
{
  return "LUSTRE";
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

int LUSTREFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                         fsal_status_t * status)
{
  lustrefsal_handle_t * handle1 = (lustrefsal_handle_t *)handle_1;
  lustrefsal_handle_t * handle2 = (lustrefsal_handle_t *)handle_2;

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  return (handle1->data.fid.f_seq != handle2->data.fid.f_seq)
      || (handle1->data.fid.f_oid != handle2->data.fid.f_oid)
      || (handle1->data.fid.f_ver != handle2->data.fid.f_ver);

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

unsigned int LUSTREFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                            unsigned int cookie,
                                            unsigned int alphabet_len,
                                            unsigned int index_size)
{
  unsigned long long lval;
  lustrefsal_handle_t * p_handle = (lustrefsal_handle_t *)handle;

  /* polynom of prime numbers */
  lval = 3 * cookie * alphabet_len + 1873 * p_handle->data.fid.f_seq
      + 3511 * p_handle->data.fid.f_oid + 2999 * p_handle->data.fid.f_ver + 10267;

  return lval % index_size;
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

unsigned int LUSTREFSAL_Handle_to_RBTIndex(fsal_handle_t *handle,
                                           unsigned int cookie)
{
  unsigned long long lval;
  lustrefsal_handle_t * p_handle = (lustrefsal_handle_t *)handle;

  /* polynom of prime numbers */
  lval = 2239 * cookie + 3559 * p_handle->data.fid.f_seq + 5 * p_handle->data.fid.f_oid
      + 1409 * p_handle->data.fid.f_ver + 20011;

  return high32m(lval) ^ low32m(lval);
}

/**
 * FSAL_DigestHandle :
 *  Convert an lustrefsal_handle_t to a buffer
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
fsal_status_t LUSTREFSAL_DigestHandle(fsal_export_context_t *exp_context,       /* IN */
                                      fsal_digesttype_t output_type,    /* IN */
                                      fsal_handle_t *in_handle,   /* IN */
                                      caddr_t out_buff  /* OUT */
    )
{
  unsigned int ino32;
  lustrefsal_export_context_t * p_expcontext = (lustrefsal_export_context_t *)exp_context;
  lustrefsal_handle_t * p_in_fsal_handle = (lustrefsal_handle_t *)in_handle;

  /* sanity checks */
  if(!p_in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFS handle digest */
    case FSAL_DIGEST_NFSV2:

      if(sizeof(p_in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, p_in_fsal_handle, sizeof(lustrefsal_handle_t));
      break;

    case FSAL_DIGEST_NFSV3:

      if(sizeof(p_in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, p_in_fsal_handle, sizeof(p_in_fsal_handle->data));
      break;

    case FSAL_DIGEST_NFSV4:

      if(sizeof(p_in_fsal_handle->data) > FSAL_DIGEST_SIZE_HDLV4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(out_buff, p_in_fsal_handle, sizeof(p_in_fsal_handle->data));
      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:

      ino32 = low32m(p_in_fsal_handle->data.inode);

      /* sanity check about output size */
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID2);
      memcpy(out_buff, &ino32, sizeof(int));

      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:

      /* sanity check about output size */
      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID3);
      memcpy(out_buff, &(p_in_fsal_handle->data.inode), sizeof(fsal_u64_t));
      break;

      /* FileId digest for NFSv4 */
    case FSAL_DIGEST_FILEID4:

      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID4);
      memcpy(out_buff, &(p_in_fsal_handle->data.inode), sizeof(fsal_u64_t));
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
fsal_status_t LUSTREFSAL_ExpandHandle(fsal_export_context_t *exp_context,       /* IN */
                                      fsal_digesttype_t in_type,        /* IN */
                                      caddr_t in_buff,  /* IN */
                                      fsal_handle_t *out_handle   /* OUT */
    )
{
  lustrefsal_export_context_t * p_expcontext = (lustrefsal_export_context_t *)exp_context;
  lustrefsal_handle_t * p_out_fsal_handle = (lustrefsal_handle_t *)out_handle;

  /* sanity checks */
  if(!p_out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:
      memset(p_out_fsal_handle, 0, sizeof(lustrefsal_handle_t));
      memcpy(p_out_fsal_handle, in_buff, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:
      memset(p_out_fsal_handle, 0, sizeof(lustrefsal_handle_t));
      memcpy(p_out_fsal_handle, in_buff, sizeof(fsal_u64_t) + sizeof(int));
      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:
      memset(p_out_fsal_handle, 0, sizeof(lustrefsal_handle_t));
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

fsal_status_t LUSTREFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                          out_parameter)
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

fsal_status_t LUSTREFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                              fsal_parameter_t *
                                                              out_parameter)
{

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_specific_parameter_from_conf */
