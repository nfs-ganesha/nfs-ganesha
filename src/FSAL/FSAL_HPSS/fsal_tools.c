/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/09 12:16:07 $
 * \version $Revision: 1.28 $
 * \brief   miscelaneous FSAL tools.
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

#if HPSS_VERSION_MAJOR == 5
#define TYPE_UUIDT  uuid_t
#else
#define TYPE_UUIDT  hpss_uuid_t
#endif

#define INTMACRO_TO_STR(_x) _DEREF(_x)
#define _DEREF(_x) #_x

char *HPSSFSAL_GetFSName()
{
  return "HPSS " INTMACRO_TO_STR(HPSS_MAJOR_VERSION) "."
      INTMACRO_TO_STR(HPSS_MINOR_VERSION) "." INTMACRO_TO_STR(HPSS_PATCH_LEVEL);
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

int HPSSFSAL_handlecmp(hpssfsal_handle_t * handle1, hpssfsal_handle_t * handle2,
                       fsal_status_t * status)
{

  fsal_u64_t fileid1, fileid2;

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  /* hpss_HandleCompare returns wrong results for hardlinks :
   * it says that the two objects are different.
   * so we use our own comparation method,
   * by comparing fileids.
   */
  fileid1 = hpss_GetObjId(&handle1->data.ns_handle);
  fileid2 = hpss_GetObjId(&handle2->data.ns_handle);

  if(fileid1 > fileid2)
    return 1;
  else if(fileid2 == fileid1)
    return 0;
  else
    return -1;

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

unsigned int HPSSFSAL_Handle_to_HashIndex(hpssfsal_handle_t * p_handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size)
{
#define SMALL_PRIME_MULT        3
#define SMALL_PRIME_ADD         1999
#define HASH_INCR( _h_, _i_ )        ( _h_ = (_h_ * SMALL_PRIME_MULT + SMALL_PRIME_ADD) % _i_ )

  unsigned int h = cookie;
  unsigned int i;
  unsigned32 objid = hpss_GetObjId(&p_handle->data.ns_handle);

  /* In HPSS, the hardlink is a separated and independant object 
   * because of this, the ns_handle.Generation may be different between 
   * the file and its hadlink, leading to different FSAL handle. This would
   * mess up the cache a lot. Because of this, the next two lines are commented */

  //h ^= p_handle->data.ns_handle.Generation;
  //HASH_INCR(h, index_size);
  h ^= objid;
  HASH_INCR(h, index_size);
  h ^= p_handle->data.ns_handle.CoreServerUUID.time_low;
  HASH_INCR(h, index_size);
  h ^= p_handle->data.ns_handle.CoreServerUUID.time_mid;
  HASH_INCR(h, index_size);
  h ^= p_handle->data.ns_handle.CoreServerUUID.time_hi_and_version;
  HASH_INCR(h, index_size);
  h ^= p_handle->data.ns_handle.CoreServerUUID.clock_seq_hi_and_reserved;
  HASH_INCR(h, index_size);
  h ^= p_handle->data.ns_handle.CoreServerUUID.clock_seq_low;
  HASH_INCR(h, index_size);

  for(i = 0; i < 6; i++)
    {
      h ^= p_handle->data.ns_handle.CoreServerUUID.node[i];
      HASH_INCR(h, index_size);
    }

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

unsigned int HPSSFSAL_Handle_to_RBTIndex(hpssfsal_handle_t * p_handle,
                                         unsigned int cookie)
{
  unsigned int h;
  unsigned int i;
  unsigned32 objid = hpss_GetObjId(&p_handle->data.ns_handle);

  h = cookie;
  // h ^= p_handle->data.ns_handle.Generation << 1; /* Same reason as above */
  h ^= objid << 2;

  h ^= p_handle->data.ns_handle.CoreServerUUID.time_low << 3;
  h ^= p_handle->data.ns_handle.CoreServerUUID.time_mid << 4;
  h ^= p_handle->data.ns_handle.CoreServerUUID.time_hi_and_version << 5;
  h ^= p_handle->data.ns_handle.CoreServerUUID.clock_seq_hi_and_reserved << 6;
  h ^= p_handle->data.ns_handle.CoreServerUUID.clock_seq_low << 7;

  for(i = 0; i < 6; i++)
    {
      h ^= ((unsigned int)p_handle->data.ns_handle.CoreServerUUID.node[i]) << 8 + i;
    }

  return h;
}

/**
 * FSAL_DigestHandle :
 *  Convert an hpssfsal_handle_t to a buffer
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

fsal_status_t HPSSFSAL_DigestHandle(hpssfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    hpssfsal_handle_t * in_fsal_handle, /* IN */
                                    caddr_t out_buff    /* OUT */
    )
{
  int memlen;
  unsigned32 objid;             /* to store objID */
  fsal_u64_t objid64;           /* to cast the objID */
  fsal_nodetype_t nodetype;     /* to store object  type */

  /* sanity checks */
  if(!in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:

        /* min size for nfs handle digest. */
        memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);

      /* The hpss handle must be converted
       * to a 25 bytes handle. To do so,
       * We copy all the fields from the hpss handle,
       *  except the Coreserver ID.
       */

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(memlen > FSAL_DIGEST_SIZE_HDLV2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      /* sanity check about core server ID */

      if(memcmp(&in_fsal_handle->data.ns_handle.CoreServerUUID,
                &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT)))
        {
          char buffer[128];
          snprintmem(buffer, 128, (caddr_t) & (in_fsal_handle->data.ns_handle.CoreServerUUID),
                     sizeof(TYPE_UUIDT));
          LogMajor(COMPONENT_FSAL,
                   "Invalid CoreServerUUID in HPSS handle: %s", buffer);
        }

      /* building digest :
       * - fill it with zeros
       * - setting the first bytes to the fsal_handle value
       *   (except CoreServerUUID)
       */
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, &(in_fsal_handle->data.ns_handle), memlen);

      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:

#ifdef _STRIP_CORESERVER_UUID
      memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);
#else /* store the whole HPSS handle in the NFS handle */
      memlen = sizeof(ns_ObjHandle_t);
#endif

      /* sanity check about output size */
      if(memlen > FSAL_DIGEST_SIZE_HDLV3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      /* sanity check about core server ID */

#ifdef _STRIP_CORESERVER_UUID
      if(memcmp(&in_fsal_handle->data.ns_handle.CoreServerUUID,
                &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT)))
        {
          char buffer[128];
          snprintmem(buffer, 128, (caddr_t) & (in_fsal_handle->data.ns_handle.CoreServerUUID),
                     sizeof(TYPE_UUIDT));
          LogMajor(COMPONENT_FSAL,
                   "Invalid CoreServerUUID in HPSS handle: %s", buffer);
        }
#endif

      /* building digest :
       * - fill it with zeros
       * - setting the first bytes to the fsal_handle value
       */
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, &(in_fsal_handle->data.ns_handle), memlen);

      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:

#ifdef _STRIP_CORESERVER_UUID
      memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);
#else /* store the whole HPSS handle in the NFS handle */
      memlen = sizeof(ns_ObjHandle_t);
#endif

      /* sanity check about output size */
      if(memlen > FSAL_DIGEST_SIZE_HDLV4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      /* sanity check about core server ID */
#ifdef _STRIP_CORESERVER_UUID
      if(memcmp(&in_fsal_handle->data.ns_handle.CoreServerUUID,
                &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT)))
        {
          char buffer[128];
          snprintmem(buffer, 128, (caddr_t) & (in_fsal_handle->data.ns_handle.CoreServerUUID),
                     sizeof(TYPE_UUIDT));
          LogMajor(COMPONENT_FSAL,
                   "Invalid CoreServerUUID in HPSS handle: %s", buffer);
        }
#endif

      /* building digest :
       * - fill it with zeros
       * - setting the first bytes to the fsal_handle value
       */
      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(out_buff, &(in_fsal_handle->data.ns_handle), memlen);

      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:

      /* get object ID from handle */
      objid = hpss_GetObjId(&in_fsal_handle->data.ns_handle);

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(unsigned32) > FSAL_DIGEST_SIZE_FILEID2)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID2);
      memcpy(out_buff, &objid, sizeof(unsigned32));

      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:

      /* get object ID from handle */
      objid64 = hpss_GetObjId(&in_fsal_handle->data.ns_handle);

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(fsal_u64_t) > FSAL_DIGEST_SIZE_FILEID3)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID3);
      memcpy(out_buff, &objid64, sizeof(fsal_u64_t));
      break;

      /* FileId digest for NFSv4 */

    case FSAL_DIGEST_FILEID4:
      /* get object ID from handle */
      objid64 = hpss_GetObjId(&in_fsal_handle->data.ns_handle);

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(fsal_u64_t) > FSAL_DIGEST_SIZE_FILEID4)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_FILEID4);
      memcpy(out_buff, &objid64, sizeof(fsal_u64_t));
      break;

      /* Nodetype digest. */
    case FSAL_DIGEST_NODETYPE:

      nodetype = in_fsal_handle->data.obj_type;

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(fsal_nodetype_t) > FSAL_DIGEST_SIZE_NODETYPE)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

#endif

      memset(out_buff, 0, FSAL_DIGEST_SIZE_NODETYPE);
      memcpy(out_buff, &nodetype, sizeof(fsal_nodetype_t));

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
fsal_status_t HPSSFSAL_ExpandHandle(hpssfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    hpssfsal_handle_t * out_fsal_handle /* OUT */
    )
{

  /* significant size in nfs digests */
  int memlen;

  /* sanity checks */
  if(!out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

    case FSAL_DIGEST_NFSV2:
      /* core server UUID is always stripped for NFSv2 handles */
      memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);

      memset(out_fsal_handle, 0, sizeof(out_fsal_handle->data));
      memcpy(&out_fsal_handle->data.ns_handle, in_buff, memlen);

      memcpy(&out_fsal_handle->data.ns_handle.CoreServerUUID,
             &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT));

      out_fsal_handle->data.obj_type = hpss2fsal_type(out_fsal_handle->data.ns_handle.Type);

      break;

    case FSAL_DIGEST_NFSV3:

#ifdef _STRIP_CORESERVER_UUID
      memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);
#else
      memlen = sizeof(ns_ObjHandle_t);
#endif
      memset(out_fsal_handle, 0, sizeof(out_fsal_handle->data));
      memcpy(&out_fsal_handle->data.ns_handle, in_buff, memlen);

#ifdef _STRIP_CORESERVER_UUID
      memcpy(&out_fsal_handle->data.ns_handle.CoreServerUUID,
             &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT));
#endif

      out_fsal_handle->data.obj_type = hpss2fsal_type(out_fsal_handle->data.ns_handle.Type);

      break;

    case FSAL_DIGEST_NFSV4:

#ifdef _STRIP_CORESERVER_UUID
      memlen = sizeof(ns_ObjHandle_t) - sizeof(TYPE_UUIDT);
#else
      memlen = sizeof(ns_ObjHandle_t);
#endif

      memset(out_fsal_handle, 0, sizeof(out_fsal_handle->data));
      memcpy(&out_fsal_handle->data.ns_handle, in_buff, memlen);

#ifdef _STRIP_CORESERVER_UUID
      memcpy(&out_fsal_handle->data.ns_handle.CoreServerUUID,
             &p_expcontext->fileset_root_handle.CoreServerUUID, sizeof(TYPE_UUIDT));
#endif

      out_fsal_handle->data.obj_type = hpss2fsal_type(out_fsal_handle->data.ns_handle.Type);

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
fsal_status_t HPSSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);


  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t HPSSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t HPSSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_specific_info */

#if HPSS_MAJOR_VERSION == 5

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, PrincipalName);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, KeytabPath);

#elif HPSS_MAJOR_VERSION >= 6

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, AuthnMech);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, NumRetries);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, BusyDelay);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, BusyRetries);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, MaxConnections);

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, DebugPath);

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, Principal);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, KeytabPath);

#endif

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, CredentialLifetime);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_specific_info, ReturnInconsistentDirent);

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

fsal_status_t HPSSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_FSAL);

  /* cannot read item */

  if(block == NULL)
    {
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
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
          LogCrit(COMPONENT_FSAL,
              "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
               var_index, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
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
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fsal_info.max_fs_calls = (unsigned int)maxcalls;

        }
      else
        {
          LogCrit(COMPONENT_FSAL,
              "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
               key_name, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t HPSSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
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
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
                 CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
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
          LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
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
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, xattr_access_rights,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else
        {
          LogCrit(COMPONENT_FSAL,
              "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
               key_name, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_common_parameter_from_conf */

/* load specific filesystem configuration options */

fsal_status_t HPSSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter)
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
#if HPSS_MAJOR_VERSION == 5
      /* does the variable exists ? */
      if(!STRCMP(key_name, "PrincipalName"))
        {

          out_parameter->fs_specific_info.behaviors.PrincipalName = FSAL_INIT_FORCE_VALUE;
          strcpy(out_parameter->fs_specific_info.hpss_config.PrincipalName, key_value);

        }
      else if(!STRCMP(key_name, "KeytabPath"))
        {

          out_parameter->fs_specific_info.behaviors.KeytabPath = FSAL_INIT_FORCE_VALUE;
          strcpy(out_parameter->fs_specific_info.hpss_config.KeytabPath, key_value);

        }
#else
      /* does the variable exists ? */
      if(!STRCMP(key_name, "PrincipalName"))
        {
          out_parameter->fs_specific_info.behaviors.Principal = FSAL_INIT_FORCE_VALUE;
          strcpy(out_parameter->fs_specific_info.Principal, key_value);

        }
      else if(!STRCMP(key_name, "KeytabPath"))
        {
          out_parameter->fs_specific_info.behaviors.KeytabPath = FSAL_INIT_FORCE_VALUE;
          strcpy(out_parameter->fs_specific_info.KeytabPath, key_value);
        }
      else if(!STRCMP(key_name, "AuthMech"))
        {
          int error;

          out_parameter->fs_specific_info.behaviors.AuthnMech = FSAL_INIT_FORCE_VALUE;

          error = hpss_AuthnMechTypeFromString(key_value,
                                               &out_parameter->fs_specific_info.
                                               hpss_config.AuthnMech);

          if(error != HPSS_E_NOERROR)
            {
              LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: ERROR: Unexpected value for %s.",
                         key_name);
              ReturnCode(ERR_FSAL_INVAL, error);
            }

        }
      else if(!STRCMP(key_name, "BusyDelay"))
        {
          int busydelay = s_read_int(key_value);

          if(busydelay < 0)
            {
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.behaviors.BusyDelay = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.hpss_config.BusyDelay = busydelay;
        }
      else if(!STRCMP(key_name, "BusyRetries"))
        {
          int busyretries;

          if(key_value[0] == '-')
            {
              busyretries = s_read_int((char *)(key_value + 1));
              if(busyretries < 0)
                {
                  LogCrit(COMPONENT_FSAL,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: integer expected.",
                       key_name);
                  ReturnCode(ERR_FSAL_INVAL, 0);
                }
              busyretries = -busyretries;
            }
          else
            {
              busyretries = s_read_int(key_value);

              if(busyretries < 0)
                {
                  LogCrit(COMPONENT_FSAL,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: integer expected.",
                       key_name);
                  ReturnCode(ERR_FSAL_INVAL, 0);
                }
            }

          out_parameter->fs_specific_info.behaviors.BusyRetries = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.hpss_config.BusyRetries = busyretries;
        }
      else if(!STRCMP(key_name, "NumRetries"))
        {
          int numretries;

          if(key_value[0] == '-')
            {
              numretries = s_read_int((char *)(key_value + 1));
              if(numretries < 0)
                {
                  LogCrit(COMPONENT_FSAL,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: integer expected.",
                       key_name);
                  ReturnCode(ERR_FSAL_INVAL, 0);
                }
              numretries = -numretries;
            }
          else
            {
              numretries = s_read_int(key_value);

              if(numretries < 0)
                {
                  LogCrit(COMPONENT_FSAL,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: integer expected.",
                       key_name);
                  ReturnCode(ERR_FSAL_INVAL, 0);
                }
            }

          out_parameter->fs_specific_info.behaviors.NumRetries = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.hpss_config.NumRetries = numretries;
        }
      else if(!STRCMP(key_name, "MaxConnections"))
        {
          int maxconn = s_read_int(key_value);

          if(maxconn < 0)
            {
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.behaviors.MaxConnections
              = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.hpss_config.MaxConnections = maxconn;
        }
      else if(!STRCMP(key_name, "DebugPath"))
        {
          out_parameter->fs_specific_info.behaviors.DebugPath = FSAL_INIT_FORCE_VALUE;
          strcpy(out_parameter->fs_specific_info.hpss_config.DebugPath, key_value);
        }
#endif
      else if(!STRCMP(key_name, "CredentialLifetime"))
        {
          int cred_life = s_read_int(key_value);

          if(cred_life < 1)
            {
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.behaviors.CredentialLifetime
              = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.CredentialLifetime = (fsal_uint_t) cred_life;
        }
      else if(!STRCMP(key_name, "ReturnInconsistentDirent"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_FSAL,
                  "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                   key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fs_specific_info.behaviors.ReturnInconsistentDirent
              = FSAL_INIT_FORCE_VALUE;
          out_parameter->fs_specific_info.ReturnInconsistentDirent = (fsal_uint_t) bool;
        }
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
