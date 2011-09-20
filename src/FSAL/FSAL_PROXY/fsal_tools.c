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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <ctype.h>              /* for isalpha */
#include <netdb.h>              /* for gethostbyname */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "fsal_common.h"
#include <string.h>

#ifdef _HANDLE_MAPPING
#include "handle_mapping/handle_mapping.h"
#endif

extern proxyfs_specific_initinfo_t global_fsal_proxy_specific_info;

/* case unsensitivity */
#define STRCMP   strcasecmp

char *PROXYFSAL_GetFSName()
{
  return "NFSv4 PROXY";
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

int PROXYFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                        fsal_status_t * status)
{
  *status = FSAL_STATUS_NO_ERROR;
  proxyfsal_handle_t * handle1 = (proxyfsal_handle_t *)handle_1;
  proxyfsal_handle_t * handle2 = (proxyfsal_handle_t *)handle_2;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  /* Check if size are the same for underlying's server FH */
  if(handle1->data.srv_handle_len != handle2->data.srv_handle_len)
    return -1;

  /* Check timestamp for server's instance (take care when volatile FH will be used) */
  if(handle1->data.timestamp != handle2->data.timestamp)
    return -1;

  /* At last, check underlying FH value. We use the fact that srv_handle_len is the same */
  if(memcmp(handle1->data.srv_handle_val, handle2->data.srv_handle_val, handle1->data.srv_handle_len))
    return -1;

  /* If this point is reached, then the FH are the same */
  return 0;
}                               /* FSAL_handlecmp */

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

unsigned int PROXYFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                           unsigned int cookie,
                                           unsigned int alphabet_len,
                                           unsigned int index_size)
{
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod;
  proxyfsal_handle_t * p_handle = (proxyfsal_handle_t *)handle;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.srv_handle_len % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < p_handle->data.srv_handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.srv_handle_val[cpt]), sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.srv_handle_len - mod; cpt < p_handle->data.srv_handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.srv_handle_val[cpt];
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

unsigned int PROXYFSAL_Handle_to_RBTIndex(fsal_handle_t *handle,
                                          unsigned int cookie)
{
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod;
  proxyfsal_handle_t * p_handle = (proxyfsal_handle_t *)handle;

  h = cookie;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.srv_handle_len % sizeof(unsigned int);

  for(cpt = 0; cpt < p_handle->data.srv_handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.srv_handle_val[cpt]), sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.srv_handle_len - mod; cpt < p_handle->data.srv_handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.srv_handle_val[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

/**
 * FSAL_DigestHandle :
 *  Convert an proxyfsal_handle_t to a buffer
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

#define NFSV4_FH_OPAQUE_SIZE 108 /* Take care of coherency with size of file_handle_v4_t::fsopaque */

fsal_status_t PROXYFSAL_DigestHandle(fsal_export_context_t * exp_context, /* IN */
                                     fsal_digesttype_t output_type,     /* IN */
                                     fsal_handle_t * in_handle,       /* IN */
                                     caddr_t out_buff   /* OUT */
    )
{
#ifdef _HANDLE_MAPPING
  nfs23_map_handle_t map_hdl;
#endif
  proxyfsal_export_context_t * p_expcontext = (proxyfsal_export_context_t *)exp_context;
  proxyfsal_handle_t * in_fsal_handle = (proxyfsal_handle_t *)in_handle;

  /* sanity checks */
  if(!in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFSV2 handle digest */
    case FSAL_DIGEST_NFSV2:

#ifdef _HANDLE_MAPPING
      if(!global_fsal_proxy_specific_info.enable_handle_mapping)
        ReturnCode(ERR_FSAL_NOTSUPP, 0);

      /* returns a digest and register it to handle map
       * (use the same checksum as cache inode's RBT index)
       */
      map_hdl.object_id = in_fsal_handle->data.fileid4;
      map_hdl.handle_hash = FSAL_Handle_to_RBTIndex(in_fsal_handle, 0);

      HandleMap_SetFH(&map_hdl, in_fsal_handle);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);
      memcpy(out_buff, &map_hdl, sizeof(nfs23_map_handle_t));

#else
      /* Proxy works only on NFSv4 requests */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);
#endif

      break;

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:
#ifdef _HANDLE_MAPPING

      /* returns a digest and register it to handle map
       * (use the same checksum as cache inode's RBT index)
       */
      map_hdl.object_id = in_fsal_handle->data.fileid4;
      map_hdl.handle_hash = FSAL_Handle_to_RBTIndex(in_fsal_handle, 0);

      HandleMap_SetFH(&map_hdl, in_fsal_handle);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(out_buff, &map_hdl, sizeof(nfs23_map_handle_t));

#else
      /* Proxy works only on NFSv4 requests */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);
#endif
      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:

     if(in_fsal_handle->data.srv_handle_len + sizeof(fsal_u64_t) + 2 * sizeof(unsigned int) >
       NFSV4_FH_OPAQUE_SIZE)
         ReturnCode(ERR_FSAL_INVAL, ENOSPC);

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV4);

      /* Keep the file id */
      memcpy(out_buff, (char *)&(in_fsal_handle->data.fileid4), sizeof(fsal_u64_t));

      /* Keep  the type of then object at the beginning */
      memcpy((char *)(out_buff + sizeof(fsal_u64_t)),
             (char *)&(in_fsal_handle->data.object_type_reminder), sizeof(unsigned int));

      /* Then the len of the file handle */
      memcpy((char *)(out_buff + sizeof(fsal_u64_t) + sizeof(unsigned int)),
             &(in_fsal_handle->data.srv_handle_len), sizeof(unsigned int));

      /* Then keep the value of the buff */
      memcpy((char *)(out_buff + sizeof(fsal_u64_t) + 2 * sizeof(unsigned int)),
             in_fsal_handle->data.srv_handle_val, in_fsal_handle->data.srv_handle_len);
      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:
      /* Just keep the most significant part */
      memcpy(out_buff, (char *)(&(in_fsal_handle->data.fileid4) + sizeof(u_int32_t)),
             sizeof(u_int32_t));
      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:
      memcpy(out_buff, (char *)&(in_fsal_handle->data.fileid4), sizeof(fsal_u64_t));
      break;

    case FSAL_DIGEST_FILEID4:
      memcpy(out_buff, (char *)&(in_fsal_handle->data.fileid4), sizeof(fsal_u64_t));
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
fsal_status_t PROXYFSAL_ExpandHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t in_type, /* IN */
                                     caddr_t in_buff,   /* IN */
                                     fsal_handle_t * out_fsal_handle       /* OUT */
    )
{
  fsal_nodetype_t nodetype;
  fsal_u64_t fileid;
  nfs_fh4 nfs4fh;
#ifdef _HANDLE_MAPPING
  nfs23_map_handle_t map_hdl;
  proxyfsal_handle_t tmp_hdl;
  int rc;
#endif

  /* sanity checks */
  if(!out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:

#ifdef _HANDLE_MAPPING
      if(!global_fsal_proxy_specific_info.enable_handle_mapping)
        ReturnCode(ERR_FSAL_NOTSUPP, 0);

      /* retrieve significant info */
      memcpy(&map_hdl, in_buff, sizeof(nfs23_map_handle_t));

      rc = HandleMap_GetFH(&map_hdl, &tmp_hdl);

      if(isFullDebug(COMPONENT_FSAL))
        {
          if(rc == HANDLEMAP_STALE)
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns HANDLEMAP_STALE\n",
                         (unsigned long long)map_hdl.object_id);
          else if(rc == 0)
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns HANDLEMAP_SUCCESS\n",
                         (unsigned long long)map_hdl.object_id);
          else
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns error %d\n",
                         (unsigned long long)map_hdl.object_id, rc);
        }

      if(rc == HANDLEMAP_STALE)
        ReturnCode(ERR_FSAL_STALE, rc);
      else if(rc != 0)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);

      /* The fsal_handle we get may not be up-to-date
       * so we pass an extract of its content to fsal_internal_proxy_create_fh()
       */
      fsal_internal_proxy_extract_fh(&nfs4fh, &tmp_hdl);

      if(fsal_internal_proxy_create_fh
         (&nfs4fh, tmp_hdl.data.object_type_reminder, tmp_hdl.data.fileid4,
          out_fsal_handle) != TRUE)
        ReturnCode(ERR_FSAL_FAULT, 0);

#else
      /* Proxy works only on NFSv4 requests */
      ReturnCode(ERR_FSAL_NOTSUPP, 0);
#endif
      break;

    case FSAL_DIGEST_NFSV4:
      /* take the file id */
      memcpy((char *)&fileid, in_buff, sizeof(fsal_u64_t));

      /* Keep  the type of then object at the beginning */
      memcpy((char *)&nodetype, (char *)(in_buff + sizeof(fsal_u64_t)),
             sizeof(unsigned int));

      /* Then the len of the file handle */
      memcpy((char *)&(nfs4fh.nfs_fh4_len),
             (char *)(in_buff + sizeof(fsal_u64_t) + sizeof(unsigned int)),
             sizeof(unsigned int));

      /* Then keep the value of the buff */
      nfs4fh.nfs_fh4_val =
          (char *)(in_buff + sizeof(fsal_u64_t) + 2 * sizeof(unsigned int));

      if(fsal_internal_proxy_create_fh(&nfs4fh, nodetype, fileid, out_fsal_handle) !=
         TRUE)
        ReturnCode(ERR_FSAL_FAULT, 0);

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
fsal_status_t PROXYFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

fsal_status_t PROXYFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t PROXYFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  proxyfs_specific_initinfo_t *init_info;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* >> set your default FS configuration into the
     out_parameter->fs_specific_info structure << */

  init_info = (proxyfs_specific_initinfo_t *) &(out_parameter->fs_specific_info);

  init_info->retry_sleeptime = FSAL_PROXY_RETRY_SLEEPTIME; /* Time to sleep when retrying */
  init_info->srv_addr = htonl(0x7F000001); /* 127.0.0.1 aka localhost     */
  init_info->srv_prognum = 100003; /* Default NFS prognum         */
  init_info->srv_port = htons(2049);       /* Default NFS port            */
  init_info->srv_timeout = 2;     /* RPC Client timeout          */
  init_info->srv_sendsize = FSAL_PROXY_SEND_BUFFER_SIZE;   /* Default Buffer Send Size    */
  init_info->srv_recvsize = FSAL_PROXY_RECV_BUFFER_SIZE;   /* Default Buffer Send Size    */
  init_info->use_privileged_client_port = FALSE;   /* No privileged port by default */

  init_info->active_krb5 = FALSE;  /* No RPCSEC_GSS by default */
  strncpy(init_info->local_principal, "(no principal set)", MAXNAMLEN);    /* Principal is nfs@<host>  */
  strncpy(init_info->remote_principal, "(no principal set)", MAXNAMLEN);   /* Principal is nfs@<host>  */
  strncpy(init_info->keytab, "etc/krb5.keytab", MAXPATHLEN);       /* Path to krb5 keytab file */
  init_info->cred_lifetime = 86400;        /* 24h is a good default    */
  init_info->sec_type = 0;

  strcpy(init_info->srv_proto, "tcp");
  strncpy(init_info->openfh_wd, "/.hl_dir", MAXPATHLEN);

#ifdef _HANDLE_MAPPING
  init_info->enable_handle_mapping = FALSE;
  strcpy(init_info->hdlmap_dbdir, "/var/ganesha/handlemap");
  strcpy(init_info->hdlmap_tmpdir, "/var/ganesha/tmp");
  init_info->hdlmap_dbcount = 8;
  init_info->hdlmap_hashsize = 103;
  init_info->hdlmap_nb_entry_prealloc = 16384;
  init_info->hdlmap_nb_db_op_prealloc = 1024;
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

fsal_status_t PROXYFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
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
    SetComponentLogLevel(COMPONENT_FSAL, DebugLevel);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t PROXYFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
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

/* load specific filesystem configuration options */

fsal_status_t PROXYFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                             fsal_parameter_t *
                                                             out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  struct hostent *hp = NULL;
  config_item_t block;
  proxyfs_specific_initinfo_t *init_info;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* >> set your default FS configuration into the
     out_parameter->fs_specific_info structure << */

  init_info = (proxyfs_specific_initinfo_t *) &(out_parameter->fs_specific_info);

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

      if(!STRCMP(key_name, "Srv_Addr"))
        {
          if(isdigit(key_value[0]))
            {
              /* Address begin with a digit, it is a address in the dotted form, translate it */
              init_info->srv_addr = inet_addr(key_value);
            }
          else
            {
              /* This is a serveur name that is to be resolved. Use gethostbyname */
              if((hp = gethostbyname(key_value)) == NULL)
                {
                  LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s",
                          key_name);
                  ReturnCode(ERR_FSAL_INVAL, 0);
                }
              memcpy(&init_info->srv_addr, hp->h_addr, hp->h_length);
            }
        }
      else if(!STRCMP(key_name, "NFS_Port"))
        {
          init_info->srv_port =
              htons((unsigned short)atoi(key_value));
        }
      else if(!STRCMP(key_name, "NFS_Service"))
        {
          init_info->srv_prognum = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "NFS_SendSize"))
        {
          init_info->srv_sendsize = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "NFS_RecvSize"))
        {
          init_info->srv_recvsize = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "Use_Privileged_Client_Port"))
        {
           init_info->use_privileged_client_port = StrToBoolean( key_value ) ;
        }
      else if(!STRCMP(key_name, "Retry_SleepTime"))
        {
          init_info->retry_sleeptime = (unsigned int)atoi(key_value);
        }
///#ifdef _ALLOW_NFS_PROTO_CHOICE
      else if(!STRCMP(key_name, "NFS_Proto"))
        {
          /* key_value should be either "udp" or "tcp" */
          if(strncasecmp(key_value, "udp", MAXNAMLEN)
             && strncasecmp(key_value, "tcp", MAXNAMLEN))
            {
              LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s --> %s",
                      key_name, key_value);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          strncpy(init_info->srv_proto, key_value, MAXNAMLEN);
        }
///#endif
      else if(!STRCMP(key_name, "Active_krb5"))
        {
          init_info->active_krb5 = StrToBoolean(key_value);
        }
      else if(!STRCMP(key_name, "Local_PrincipalName"))
        {
          strncpy(init_info->local_principal, key_value, MAXNAMLEN);
        }
      else if(!STRCMP(key_name, "Remote_PrincipalName"))
        {
          strncpy(init_info->remote_principal, key_value, MAXNAMLEN);
        }
      else if(!STRCMP(key_name, "KeytabPath"))
        {
          strncpy(init_info->keytab, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "Credential_LifeTime"))
        {
          init_info->cred_lifetime = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "Sec_Type"))
        {
#ifdef _USE_GSSRPC
          if(!STRCMP(key_value, "krb5"))
            init_info->sec_type = RPCSEC_GSS_SVC_NONE;
          else if(!STRCMP(key_value, "krb5i"))
            init_info->sec_type = RPCSEC_GSS_SVC_INTEGRITY;
          else if(!STRCMP(key_value, "krb5p"))
            init_info->sec_type = RPCSEC_GSS_SVC_PRIVACY;
          else
            {
              LogCrit(COMPONENT_CONFIG, "FSAL LOAD PARAMETER: bad value %s for parameter %s", key_value,
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
#endif
        }
      else if(!STRCMP(key_name, "Open_by_FH_Working_Dir"))
        {
          strncpy(init_info->openfh_wd, key_value, MAXPATHLEN);
        }

      else if(!STRCMP(key_name, "Enable_Handle_Mapping"))
        {
          init_info->enable_handle_mapping = StrToBoolean(key_value);

          if(init_info->enable_handle_mapping == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s --> %s (boolean expected)",
                      key_name, key_value);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
        }
      else if(!STRCMP(key_name, "HandleMap_DB_Dir"))
        {
          strncpy(init_info->hdlmap_dbdir, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "HandleMap_Tmp_Dir"))
        {
          strncpy(init_info->hdlmap_tmpdir, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "HandleMap_DB_Count"))
        {
          init_info->hdlmap_dbcount = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_HashTable_Size"))
        {
          init_info->hdlmap_hashsize = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_Nb_Entries_Prealloc"))
        {
          init_info->hdlmap_nb_entry_prealloc =
              (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_Nb_DB_Operations_Prealloc"))
        {
          init_info->hdlmap_nb_db_op_prealloc =
              (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "Open_by_FH_Working_Dir"))
        {
          strncpy(init_info->openfh_wd, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "Enable_Handle_Mapping"))
        {
          init_info->enable_handle_mapping = StrToBoolean(key_value);

          if(init_info->enable_handle_mapping == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s --> %s (boolean expected)",
                      key_name, key_value);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
        }
      else if(!STRCMP(key_name, "HandleMap_DB_Dir"))
        {
          strncpy(init_info->hdlmap_dbdir, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "HandleMap_Tmp_Dir"))
        {
          strncpy(init_info->hdlmap_tmpdir, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "HandleMap_DB_Count"))
        {
          init_info->hdlmap_dbcount = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_HashTable_Size"))
        {
          init_info->hdlmap_hashsize = (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_Nb_Entries_Prealloc"))
        {
          init_info->hdlmap_nb_entry_prealloc =
              (unsigned int)atoi(key_value);
        }
      else if(!STRCMP(key_name, "HandleMap_Nb_DB_Operations_Prealloc"))
        {
          init_info->hdlmap_nb_db_op_prealloc =
              (unsigned int)atoi(key_value);
        }

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
