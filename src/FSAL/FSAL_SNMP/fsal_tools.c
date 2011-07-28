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

char *SNMPFSAL_GetFSName()
{
  return "SNMP";
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

int SNMPFSAL_handlecmp(snmpfsal_handle_t * handle1, snmpfsal_handle_t * handle2,
                       fsal_status_t * status)
{

  fsal_u64_t fileid1, fileid2;

  *status = FSAL_STATUS_NO_ERROR;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  if(handle1->data.oid_len != handle2->data.oid_len)
    return (handle1->data.oid_len - handle2->data.oid_len);
  return memcmp(handle1->data.oid_tab, handle2->data.oid_tab, handle1->data.oid_len * sizeof(oid));

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

unsigned int SNMPFSAL_Handle_to_HashIndex(snmpfsal_handle_t * p_handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size)
{
  unsigned int i;
  unsigned int h = 1 + cookie;

  for(i = 0; i < p_handle->data.oid_len; i++)
    h = (691 * h ^ (unsigned int)p_handle->data.oid_tab[i]) % 479001599;

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

unsigned int SNMPFSAL_Handle_to_RBTIndex(snmpfsal_handle_t * p_handle,
                                         unsigned int cookie)
{
  unsigned int i;
  unsigned int h = 1 + cookie;

  for(i = 0; i < p_handle->data.oid_len; i++)
    h = (857 * h ^ (unsigned int)p_handle->data.oid_tab[i]) % 715827883;

  return h;

}

/**
 * FSAL_DigestHandle :
 *  Convert an snmpfsal_handle_t to a buffer
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

/* /!\ /!\ /!\ /!\ /!\ /!\ /!\ /!\ /!\ /!\ /!\ /!\
 * We compact handles the following way:
 * we "forget" the beginning of the handle,
 * because we can retrieve it from the export entry.
 * - the first value indicates the entry type (3 possible values)
 * - the 2nd value indicates the relative oid length (<32)
 * - the 3rd value indicate the number of oids between 255 and 65536 (<32)
 * - the 4th value is the number of oids over 65536 (<16).
 * The following bytes indicate the indexes of those oids in the list. 
 */

#define DGST_FLAG_ROOT 1
#define DGST_FLAG_NODE 2
#define DGST_FLAG_LEAF 3

#define MAX_CHAR_VAL  ((1<<8)-1)        /* 2^8-1 */
#define MAX_SHORT_VAL ((1<<16)-1)       /* 2^16-1 */

typedef struct fsal_digest__
{
  unsigned int type_flag:2;
  unsigned int relative_oid_len:5;
  unsigned int nb_short_oids:5;
  unsigned int nb_int_oids:4;
  /* total --------------------> 16 = 2 bytes */

  unsigned char char_tab[0];    /* 23 remaining bytes */

} fsal_digest_t;

fsal_status_t SNMPFSAL_DigestHandle(snmpfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    snmpfsal_handle_t * in_fsal_handle, /* IN */
                                    caddr_t out_buff    /* OUT */
    )
{
  unsigned int i;
  unsigned int objid32;
  fsal_digest_t *p_digest;
  fsal_u64_t objid;

  unsigned int nb_oids;
  unsigned int nb_short;
  unsigned int nb_int;
  unsigned int root_len;

  int short_tab_indexes[32];
  int int_tab_indexes[32];

  unsigned char *curr_addr;

  /* sanity checks */
  if(!in_fsal_handle || !out_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {

      /* NFS handle digest */
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:

      /* FSAL handle must be converted to a 25 bytes digest */

      memset(out_buff, 0, FSAL_DIGEST_SIZE_HDLV2);

      p_digest = (fsal_digest_t *) out_buff;

      /* first, set the type flag */
      switch (in_fsal_handle->data.object_type_reminder)
        {
        case FSAL_NODETYPE_ROOT:
          p_digest->type_flag = DGST_FLAG_ROOT;
          break;

        case FSAL_NODETYPE_NODE:
          p_digest->type_flag = DGST_FLAG_NODE;
          break;

        case FSAL_NODETYPE_LEAF:
          p_digest->type_flag = DGST_FLAG_LEAF;
          break;

        default:
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

      /* for lighter code  */
      root_len = p_expcontext->root_handle.data.oid_len;

      /* then set the relative oid tab len */
      nb_oids = in_fsal_handle->data.oid_len - root_len;

      /* if buffer is too small, no need to continue */
      if(nb_oids > 23)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      p_digest->relative_oid_len = nb_oids;

      /* now, count the number of oid of each type  */
      nb_short = 0;
      nb_int = 0;

      for(i = root_len; i < in_fsal_handle->data.oid_len; i++)
        {
          if(in_fsal_handle->data.oid_tab[i] > MAX_SHORT_VAL)
            {
              int_tab_indexes[nb_int] = i - root_len;
              nb_int++;
            }
          else if(in_fsal_handle->data.oid_tab[i] > MAX_CHAR_VAL)
            {
              short_tab_indexes[nb_short] = i - root_len;
              nb_short++;
            }
        }

      /* 3 bytes for each short : 1 for its index and 2 for its storage */
      /* 5 bytes for each int : 1 for its index and 4 for its storage */
      if(3 * nb_short + 5 * nb_int + (nb_oids - nb_short - nb_int) > 23)
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      /* fill the digest header */
      p_digest->nb_short_oids = nb_short;
      p_digest->nb_int_oids = nb_int;

      /* now, store their indexes */

      curr_addr = p_digest->char_tab;

      for(i = 0; i < nb_short; i++)
        {
          *curr_addr = (unsigned char)short_tab_indexes[i];
          curr_addr++;
        }

      for(i = 0; i < nb_int; i++)
        {
          *curr_addr = (unsigned char)int_tab_indexes[i];
          curr_addr++;
        }

      /* now store each oid */
      for(i = 0; i < nb_oids; i++)
        {
          /* if the oid is bigger that short, write 4 bytes */
          if(in_fsal_handle->data.oid_tab[i + root_len] > MAX_SHORT_VAL)
            {
              if(curr_addr + 4 > (unsigned char *)out_buff + FSAL_DIGEST_SIZE_HDLV2)
                ReturnCode(ERR_FSAL_TOOSMALL, 0);

              curr_addr[3] =
                  (unsigned char)(in_fsal_handle->data.oid_tab[i + root_len] & 0xFF);
              curr_addr[2] =
                  (unsigned char)((in_fsal_handle->data.oid_tab[i + root_len] >> 8) & 0xFF);
              curr_addr[1] =
                  (unsigned char)((in_fsal_handle->data.oid_tab[i + root_len] >> 16) & 0xFF);
              curr_addr[0] =
                  (unsigned char)((in_fsal_handle->data.oid_tab[i + root_len] >> 24) & 0xFF);

              curr_addr += 4;
            }
          /* if the oid is bigger that byte, write 2 bytes */
          else if(in_fsal_handle->data.oid_tab[i + root_len] > MAX_CHAR_VAL)
            {
              if(curr_addr + 2 > (unsigned char *)out_buff + FSAL_DIGEST_SIZE_HDLV2)
                ReturnCode(ERR_FSAL_TOOSMALL, 0);

              curr_addr[1] =
                  (unsigned char)(in_fsal_handle->data.oid_tab[i + root_len] & 0xFF);
              curr_addr[0] =
                  (unsigned char)((in_fsal_handle->data.oid_tab[i + root_len] >> 8) & 0xFF);

              curr_addr += 2;
            }
          else                  /* write 1 byte */
            {
              if(curr_addr + 1 > (unsigned char *)out_buff + FSAL_DIGEST_SIZE_HDLV2)
                ReturnCode(ERR_FSAL_TOOSMALL, 0);

              curr_addr[0] =
                  (unsigned char)(in_fsal_handle->data.oid_tab[i + root_len] & 0xFF);
              curr_addr++;
            }

        }
      /* That's all folks ! */

      break;

      /* FileId digest for NFSv2 */
    case FSAL_DIGEST_FILEID2:

      objid = build_object_id(in_fsal_handle);
      objid32 = (unsigned int)(objid & 0x00000000FFFFFFFFLL);

      memcpy(out_buff, &objid32, FSAL_DIGEST_SIZE_FILEID2);

      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:
    case FSAL_DIGEST_FILEID4:

      objid = build_object_id(in_fsal_handle);
      memcpy(out_buff, &objid, FSAL_DIGEST_SIZE_FILEID3);

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
fsal_status_t SNMPFSAL_ExpandHandle(snmpfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    snmpfsal_handle_t * out_fsal_handle /* OUT */
    )
{
  fsal_digest_t *p_digest;
  unsigned int i;

  unsigned char *curr_addr;

  unsigned int root_len;
  unsigned int nb_oids;
  unsigned int nb_short;
  unsigned int nb_int;

  unsigned int short_tab_indexes[32];
  unsigned int int_tab_indexes[32];

  unsigned int *curr_short_idx;
  unsigned int *curr_int_idx;

  /* sanity checks */
  if(!out_fsal_handle || !in_buff || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {

    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:

      /* First, clear the output handle  */
      memset(out_fsal_handle, 0, sizeof(snmpfsal_handle_t));

      /* map the buffer to a fsal_digest_t  */
      p_digest = (fsal_digest_t *) in_buff;

      /* then set object type  */
      switch (p_digest->type_flag)
        {
        case DGST_FLAG_ROOT:
          out_fsal_handle->data.object_type_reminder = FSAL_NODETYPE_ROOT;
          break;
        case DGST_FLAG_NODE:
          out_fsal_handle->data.object_type_reminder = FSAL_NODETYPE_NODE;
          break;
        case DGST_FLAG_LEAF:
          out_fsal_handle->data.object_type_reminder = FSAL_NODETYPE_LEAF;
          break;
        default:
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

      /* restore the root handle  */
      root_len = p_expcontext->root_handle.data.oid_len;
      memcpy(out_fsal_handle->data.oid_tab, p_expcontext->root_handle.data.oid_tab,
             root_len * sizeof(oid));

      /* for lighter code  */
      nb_oids = root_len + p_digest->relative_oid_len;
      nb_short = p_digest->nb_short_oids;
      nb_int = p_digest->nb_int_oids;

      /* set the final handle size */
      out_fsal_handle->data.oid_len = nb_oids;

      curr_addr = p_digest->char_tab;
      curr_short_idx = short_tab_indexes;
      curr_int_idx = int_tab_indexes;

      /* retrieve short and int indexes */
      for(i = 0; i < nb_short; i++)
        {
          *curr_short_idx = *curr_addr;
          curr_short_idx++;
          curr_addr++;
        }

      for(i = 0; i < nb_int; i++)
        {
          *curr_int_idx = *curr_addr;
          curr_int_idx++;
          curr_addr++;
        }

      /* reset pointers */
      curr_short_idx = short_tab_indexes;
      curr_int_idx = int_tab_indexes;

      /* now, read the oid values */
      for(i = 0; i < p_digest->relative_oid_len; i++)
        {
          /* is the current oid on 32 bits ?  */
          if((curr_int_idx < int_tab_indexes + nb_int) && (i == *curr_int_idx))
            {
              /* read 4 bytes */
              out_fsal_handle->data.oid_tab[i + root_len] =
                  ((unsigned long)curr_addr[3] + ((unsigned long)curr_addr[2] << 8) +
                   ((unsigned long)curr_addr[1] << 16) +
                   ((unsigned long)curr_addr[0] << 24));
              curr_addr += 4;
              curr_int_idx++;
            }
          else if((curr_short_idx < short_tab_indexes + nb_short)
                  && (i == *curr_short_idx))
            {
              /* read 2 bytes */
              out_fsal_handle->data.oid_tab[i + root_len] =
                  ((unsigned long)curr_addr[1] + ((unsigned long)curr_addr[0] << 8));
              curr_addr += 2;
              curr_short_idx++;
            }
          else
            {
              /* read 1 byte */
              out_fsal_handle->data.oid_tab[i + root_len] = (unsigned long)curr_addr[0];
              curr_addr++;
            }

        }
      /* thats all folks ! */

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
fsal_status_t SNMPFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t SNMPFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
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

fsal_status_t SNMPFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  out_parameter->fs_specific_info.snmp_version = SNMP_VERSION_2c;
  strcpy(out_parameter->fs_specific_info.snmp_server, "localhost");
  strcpy(out_parameter->fs_specific_info.community, "public");
  out_parameter->fs_specific_info.nb_retries = SNMP_DEFAULT_RETRIES;
  out_parameter->fs_specific_info.microsec_timeout = SNMP_DEFAULT_TIMEOUT;
  out_parameter->fs_specific_info.enable_descriptions = FALSE;
  strcpy(out_parameter->fs_specific_info.client_name, "GANESHA");
  out_parameter->fs_specific_info.getbulk_count = 64;
  /* we fill the snmpv3 part of the structure even if we have set v2.
     The purpose is to have a complete structure if user chooses v3 and forgets some
     parameters.
   */
  strcpy(out_parameter->fs_specific_info.auth_proto, "MD5");
  strcpy(out_parameter->fs_specific_info.enc_proto, "DES");
  strcpy(out_parameter->fs_specific_info.username, "snmpadm");
  strcpy(out_parameter->fs_specific_info.auth_phrase, "password");
  strcpy(out_parameter->fs_specific_info.enc_phrase, "password");

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

fsal_status_t SNMPFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
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

fsal_status_t SNMPFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
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

fsal_status_t SNMPFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
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

      if(!STRCMP(key_name, "snmp_version"))
        {
          long version = StrToSNMPVersion(key_value);

          if(version == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 1, 2c or 3 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          out_parameter->fs_specific_info.snmp_version = version;

        }
      else if(!STRCMP(key_name, "snmp_server"))
        {
          strncpy(out_parameter->fs_specific_info.snmp_server, key_value, HOST_NAME_MAX);
        }
      else if(!STRCMP(key_name, "community"))
        {
          strncpy(out_parameter->fs_specific_info.community, key_value,
                  COMMUNITY_MAX_LEN);
        }
      else if(!STRCMP(key_name, "nb_retries"))
        {
          int retries = s_read_int(key_value);

          if(retries < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          out_parameter->fs_specific_info.nb_retries = retries;

        }
      else if(!STRCMP(key_name, "microsec_timeout"))
        {
          int timeout = s_read_int(key_value);

          if(timeout < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          out_parameter->fs_specific_info.microsec_timeout = timeout;

        }
      else if(!STRCMP(key_name, "enable_descriptions"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          out_parameter->fs_specific_info.enable_descriptions = bool;

        }
      else if(!STRCMP(key_name, "client_name"))
        {
          strncpy(out_parameter->fs_specific_info.client_name, key_value, 256);
        }
      else if(!STRCMP(key_name, "snmp_getbulk_count"))
        {
          int count = s_read_int(key_value);

          if(count <= 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }
          out_parameter->fs_specific_info.getbulk_count = count;

        }
      else if(!STRCMP(key_name, "auth_proto"))
        {
          strncpy(out_parameter->fs_specific_info.auth_proto, key_value,
                  FSAL_MAX_PROTO_LEN);
        }
      else if(!STRCMP(key_name, "enc_proto"))
        {
          strncpy(out_parameter->fs_specific_info.enc_proto, key_value,
                  FSAL_MAX_PROTO_LEN);
        }
      else if(!STRCMP(key_name, "username"))
        {
          strncpy(out_parameter->fs_specific_info.username, key_value,
                  FSAL_MAX_USERNAME_LEN);
        }
      else if(!STRCMP(key_name, "auth_phrase"))
        {
          strncpy(out_parameter->fs_specific_info.auth_phrase, key_value,
                  FSAL_MAX_PHRASE_LEN);
        }
      else if(!STRCMP(key_name, "enc_phrase"))
        {
          strncpy(out_parameter->fs_specific_info.enc_phrase, key_value,
                  FSAL_MAX_PHRASE_LEN);
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
