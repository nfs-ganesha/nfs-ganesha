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

int SNMPFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                       fsal_status_t * status)
{

  fsal_u64_t fileid1, fileid2;
  snmpfsal_handle_t * handle1 = (snmpfsal_handle_t *)handle_1;
  snmpfsal_handle_t * handle2 = (snmpfsal_handle_t *)handle_2;

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

unsigned int SNMPFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size)
{
  unsigned int i;
  unsigned int h = 1 + cookie;
  snmpfsal_handle_t * p_handle = (snmpfsal_handle_t *)handle;

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

unsigned int SNMPFSAL_Handle_to_RBTIndex(fsal_handle_t *handle,
                                         unsigned int cookie)
{
  unsigned int i;
  unsigned int h = 1 + cookie;
  snmpfsal_handle_t * p_handle = (snmpfsal_handle_t *)handle;

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

fsal_status_t SNMPFSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    fsal_handle_t * in_handle, /* IN */
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
  snmpfsal_handle_t * in_fsal_handle = (snmpfsal_handle_t *)in_handle;

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
      root_len = ((snmpfsal_export_context_t *)p_expcontext)->root_handle.data.oid_len;

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
fsal_status_t SNMPFSAL_ExpandHandle(fsal_export_context_t *exp_context,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    fsal_handle_t * out_handle /* OUT */
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
  snmpfsal_handle_t * out_fsal_handle = (snmpfsal_handle_t *)out_handle;
  snmpfsal_export_context_t * p_expcontext = (snmpfsal_export_context_t *)exp_context;

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

fsal_status_t SNMPFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  snmpfs_specific_initinfo_t *spec_info;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  spec_info = (snmpfs_specific_initinfo_t *) &out_parameter->fs_specific_info;
  spec_info->snmp_version = SNMP_VERSION_2c;
  strcpy(spec_info->snmp_server, "localhost");
  strcpy(spec_info->community, "public");
  spec_info->nb_retries = SNMP_DEFAULT_RETRIES;
  spec_info->microsec_timeout = SNMP_DEFAULT_TIMEOUT;
  spec_info->enable_descriptions = FALSE;
  strcpy(spec_info->client_name, "GANESHA");
  spec_info->getbulk_count = 64;
  /* we fill the snmpv3 part of the structure even if we have set v2.
     The purpose is to have a complete structure if user chooses v3 and forgets some
     parameters.
   */
  strcpy(spec_info->auth_proto, "MD5");
  strcpy(spec_info->enc_proto, "DES");
  strcpy(spec_info->username, "snmpadm");
  strcpy(spec_info->auth_phrase, "password");
  strcpy(spec_info->enc_phrase, "password");

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
  snmpfs_specific_initinfo_t *spec_info;

  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  spec_info = (snmpfs_specific_initinfo_t *) &out_parameter->fs_specific_info;

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
          spec_info->snmp_version = version;

        }
      else if(!STRCMP(key_name, "snmp_server"))
        {
          strncpy(spec_info->snmp_server, key_value, HOST_NAME_MAX);
        }
      else if(!STRCMP(key_name, "community"))
        {
          strncpy(spec_info->community, key_value,
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
          spec_info->nb_retries = retries;

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
          spec_info->microsec_timeout = timeout;

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
          spec_info->enable_descriptions = bool;

        }
      else if(!STRCMP(key_name, "client_name"))
        {
          strncpy(spec_info->client_name, key_value, 256);
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
          spec_info->getbulk_count = count;

        }
      else if(!STRCMP(key_name, "auth_proto"))
        {
          strncpy(spec_info->auth_proto, key_value,
                  FSAL_MAX_PROTO_LEN);
        }
      else if(!STRCMP(key_name, "enc_proto"))
        {
          strncpy(spec_info->enc_proto, key_value,
                  FSAL_MAX_PROTO_LEN);
        }
      else if(!STRCMP(key_name, "username"))
        {
          strncpy(spec_info->username, key_value,
                  FSAL_MAX_USERNAME_LEN);
        }
      else if(!STRCMP(key_name, "auth_phrase"))
        {
          strncpy(spec_info->auth_phrase, key_value,
                  FSAL_MAX_PHRASE_LEN);
        }
      else if(!STRCMP(key_name, "enc_phrase"))
        {
          strncpy(spec_info->enc_phrase, key_value,
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
