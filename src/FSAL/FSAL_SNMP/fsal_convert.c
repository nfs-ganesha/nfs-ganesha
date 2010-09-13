/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.32 $
 * \brief   FS-FSAL type translation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_convert.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* convert a slash separated path to a dot separated path */
void PosixPath2SNMP(char *in_path, char *out_path)
{
  char *p_c;

  strcpy(out_path, in_path);

  p_c = out_path;

  /* change all '/' to '.' */
  while((p_c = strchr(p_c, (int)'/')))
    {
      *p_c = '.';
      p_c++;
    }

}

/* convert a snmp error code to an FSAL code */
int snmp2fsal_error(int snmp_error)
{
  switch (snmp_error)
    {
      /* snmp errstat (positive codes)  */

    case SNMP_ERR_NOERROR:
      return ERR_FSAL_NO_ERROR;
    case SNMP_ERR_TOOBIG:
      return ERR_FSAL_TOOSMALL;
    case SNMP_ERR_NOSUCHNAME:
      return ERR_FSAL_NOENT;
    case SNMP_ERR_BADVALUE:
      return ERR_FSAL_INVAL;
    case SNMP_ERR_READONLY:
      return ERR_FSAL_ACCESS;
    case SNMP_ERR_GENERR:
      return ERR_FSAL_IO;
    case SNMP_ERR_NOACCESS:
      return ERR_FSAL_PERM;
    case SNMP_ERR_WRONGTYPE:
      return ERR_FSAL_INVAL;
    case SNMP_ERR_WRONGLENGTH:
      return ERR_FSAL_INVAL;
    case SNMP_ERR_WRONGENCODING:
      return ERR_FSAL_INVAL;

      /* for bad range values, we choose returning DQUOT  */
    case SNMP_ERR_WRONGVALUE:
      return ERR_FSAL_DQUOT;

    case SNMP_ERR_NOCREATION:
      return ERR_FSAL_NOENT;
    case SNMP_ERR_INCONSISTENTVALUE:
      return ERR_FSAL_INVAL;
    case SNMP_ERR_RESOURCEUNAVAILABLE:
      return ERR_FSAL_PERM;
    case SNMP_ERR_COMMITFAILED:
      return ERR_FSAL_IO;
    case SNMP_ERR_UNDOFAILED:
      return ERR_FSAL_IO;
    case SNMP_ERR_AUTHORIZATIONERROR:
      return ERR_FSAL_ACCESS;
    case SNMP_ERR_NOTWRITABLE:
      return ERR_FSAL_ACCESS;
    case SNMP_ERR_INCONSISTENTNAME:
      return ERR_FSAL_NOENT;

      /* snmp_errno (negative codes)  */

    case SNMPERR_TOO_LONG:
      return ERR_FSAL_TOOSMALL;
    case SNMPERR_BAD_ASN1_BUILD:
      return ERR_FSAL_NOENT;
    case SNMPERR_BAD_PARSE:
      return ERR_FSAL_NOENT;

    case SNMPERR_BAD_COMMUNITY:
      return ERR_FSAL_PERM;

    case SNMPERR_NOAUTH_DESPRIV:
      return ERR_FSAL_SEC;

    case SNMPERR_UNKNOWN_USER_NAME:
      return ERR_FSAL_PERM;

    case SNMPERR_BAD_SEC_NAME:
      return ERR_FSAL_SEC;
    case SNMPERR_BAD_SEC_LEVEL:
      return ERR_FSAL_SEC;
    case SNMPERR_UNKNOWN_SEC_MODEL:
      return ERR_FSAL_SEC;
    case SNMPERR_UNSUPPORTED_SEC_LEVEL:
      return ERR_FSAL_SEC;
    case SNMPERR_AUTHENTICATION_FAILURE:
      return ERR_FSAL_SEC;
    case SNMPERR_DECRYPTION_ERR:
      return ERR_FSAL_SEC;
    case SNMPERR_KRB5:
      return ERR_FSAL_SEC;

      /* for bad range values, we choose returning DQUOT  */
    case SNMPERR_RANGE:
      return ERR_FSAL_DQUOT;

    case SNMPERR_MAX_SUBID:
    case SNMPERR_BAD_SUBID:
    case SNMPERR_LONG_OID:
    case SNMPERR_BAD_NAME:
      return ERR_FSAL_BADHANDLE;

    case SNMPERR_VALUE:
      return ERR_FSAL_INVAL;

    case SNMPERR_UNKNOWN_OBJID:
      return ERR_FSAL_NOENT;
    case SNMPERR_MALLOC:
      return ERR_FSAL_NOMEM;

    default:
      /* other unexpected errors */
      return ERR_FSAL_SERVERFAULT;
    }
}

/* extract the object handle from the variable info.
 */
int snmp_object2handle(netsnmp_variable_list * p_in_var, snmpfsal_handle_t * p_out_handle)
{
  /* sanity check */
  if(!p_out_handle || !p_in_var)
    return ERR_FSAL_FAULT;

  FSAL_OID_DUP(p_out_handle, p_in_var->name, p_in_var->name_length);

  /* this object is a variable, as a result, it's a leaf => except if type = NOSUCH */
  /*p_out_handle->object_type_reminder = FSAL_NODETYPE_LEAF ; */
  /* @todo a voir dans READDIR...  */

  return ERR_FSAL_NO_ERROR;
}

/* extract object name from the mib tree node (when available)
 * else, it returns the string representation of the object subid.
 */
int snmp_object2name(netsnmp_variable_list * p_in_var, struct tree *p_in_node,
                     snmpfsal_handle_t * p_handle, fsal_name_t * p_out_name)
{
  fsal_status_t st;

  char tmp_name[FSAL_MAX_NAME_LEN] = "";
  /* sanity check */
  if(!p_out_name)
    return ERR_FSAL_FAULT;

  if(p_in_node && p_in_node->label && p_in_node->label[0] != '\0')
    snprintf(tmp_name, FSAL_MAX_NAME_LEN, "%s", p_in_node->label);
  else if(p_in_node)
    snprintf(tmp_name, FSAL_MAX_NAME_LEN, "%lu", p_in_node->subid);
  else if(p_in_var && p_in_var->name && p_in_var->name_length > 0)
    snprintf(tmp_name, FSAL_MAX_NAME_LEN, "%lu",
             p_in_var->name[p_in_var->name_length - 1]);
  else if(p_handle && p_handle->data.oid_len > 0)
    snprintf(tmp_name, FSAL_MAX_NAME_LEN, "%lu",
             p_handle->data.oid_tab[p_handle->data.oid_len - 1]);
  else
    return ERR_FSAL_SERVERFAULT;

  st = FSAL_str2name(tmp_name, FSAL_MAX_NAME_LEN, p_out_name);

  return st.major;
}

static size_t Timeticks2Str(long timetick, char *out_str, size_t buflen)
{
  unsigned long days, hours, minutes, seconds, hseconds;
  long tt = timetick;

  days = tt / 8640000;
  tt %= 8640000;

  hours = tt / 360000;
  tt %= 360000;

  minutes = tt / 6000;
  tt %= 6000;

  seconds = tt / 100;
  tt %= 100;

  hseconds = tt;

  return snprintf(out_str, buflen, "%lu days, %02lu:%02lu:%02lu.%02lu", days, hours,
                  minutes, seconds, hseconds);
}

/* print the object value to a string, and return its length (the buffer ends with '\n\0', like /proc files)  */
int snmp_object2str(netsnmp_variable_list * p_in_var, char *p_out_string,
                    size_t * in_out_len)
{
  size_t written = 0;
  char tmp_buf[FSALSNMP_MAX_FILESIZE];
  unsigned long long int64;

  /* sanity check  */
  if(!p_in_var)
    return ERR_FSAL_FAULT;

  /* print string representation depending on data type */
  switch (p_in_var->type)
    {
    case ASN_INTEGER:
    case ASN_COUNTER:
    case ASN_GAUGE:
    case ASN_UINTEGER:
      if(p_in_var->val.integer)
        written = snprintf(p_out_string, *in_out_len, "%ld\n", *p_in_var->val.integer);
      else
        written = snprintf(p_out_string, *in_out_len, "(null int pointer)\n");
      break;

    case ASN_OCTET_STR:
      if(p_in_var->val.string)
        written = snprintf(p_out_string, *in_out_len, "%.*s\n",
                           p_in_var->val_len, p_in_var->val.string);
      else
        written = snprintf(p_out_string, *in_out_len, "(null string pointer)\n");
      break;

    case ASN_OBJECT_ID:
      if(p_in_var->val.objid)
        snprint_objid(tmp_buf, FSALSNMP_MAX_FILESIZE, p_in_var->val.objid,
                      p_in_var->val_len / sizeof(oid));
      else
        strcpy(tmp_buf, "(null oid pointer)");

      written = snprintf(p_out_string, *in_out_len, "%s\n", tmp_buf);

      break;

    case ASN_IPADDRESS:
      if(p_in_var->val.string)
        written = snprintf(p_out_string, *in_out_len, "%d.%d.%d.%d\n",
                           (p_in_var->val.string)[0], (p_in_var->val.string)[1],
                           (p_in_var->val.string)[2], (p_in_var->val.string)[3]);
      else
        written = snprintf(p_out_string, *in_out_len, "(null IP address pointer)\n");
      break;

    case ASN_TIMETICKS:
      if(p_in_var->val.integer)
        /* print the exact field value (then the humain readeable translation)  */
        Timeticks2Str(*p_in_var->val.integer, tmp_buf, FSALSNMP_MAX_FILESIZE);
      else
        strcpy(tmp_buf, "(null timeticks pointer)");

      written =
          snprintf(p_out_string, *in_out_len, "%ld (%s)\n", *p_in_var->val.integer,
                   tmp_buf);
      break;

    case ASN_OPAQUE:
      if(p_in_var->val.string)
        snprintmem(tmp_buf, FSALSNMP_MAX_FILESIZE, p_in_var->val.string,
                   p_in_var->val_len);
      else
        strcpy(tmp_buf, "(null opaque pointer)");

      written = snprintf(p_out_string, *in_out_len, "%s\n", tmp_buf);
      break;

    case ASN_COUNTER64:
      if(p_in_var->val.counter64)
        {
          int64 =
              (((unsigned long long)p_in_var->val.counter64->
                high) << 32) | (unsigned long long)p_in_var->val.counter64->low;
          written = snprintf(p_out_string, *in_out_len, "%llu\n", int64);
        }
      else
        written = snprintf(p_out_string, *in_out_len, "(null counter64 pointer)\n");
      break;

    case ASN_OPAQUE_FLOAT:
      if(p_in_var->val.floatVal)
        written = snprintf(p_out_string, *in_out_len, "%f\n", *p_in_var->val.floatVal);
      else
        written = snprintf(p_out_string, *in_out_len, "(null opaque float pointer)\n");
      break;

    case ASN_NULL:
      written = snprintf(p_out_string, *in_out_len, "(null object)\n");
      break;

    default:
      written =
          snprintf(p_out_string, *in_out_len, "(unsupported object type %#X)\n",
                   p_in_var->type);

    }

  /* snprintf() function return the number of bytes that would be written to s had n been sufficiently large */
  if(written > *in_out_len)
    {
      LogMajor(COMPONENT_FSAL,
               "Warning: actual datasize is over client buffer limit (%llu > %llu)",
               written, *in_out_len);
      written = *in_out_len;
    }

  /* if buffer is full like an egg ;-) replace the terminal '\0' with a '\n',
   * thus, its more compliant with the NFS client behavior
   * and it results in a better display.
   */
  if(written == *in_out_len)
    p_out_string[written - 1] = '\n';

  *in_out_len = written;

  return ERR_FSAL_NO_ERROR;

}

/* convert SNMP object's access rights to the associated FSAL mode (if mib info is available).
 * else, it returns 666 or 555, depending on the node type.
 * /!\ the p_in_node is the PARENT NODE (access right are stored in the parent node)
 */
fsal_accessmode_t snmp_object2access_mode(nodetype_t obj_type, struct tree * p_in_node)
{
  fsal_accessmode_t mode = 0;

  if(obj_type != FSAL_NODETYPE_LEAF)
    {
      mode = FSAL_MODE_RUSR | FSAL_MODE_RGRP | FSAL_MODE_ROTH
          | FSAL_MODE_XUSR | FSAL_MODE_XGRP | FSAL_MODE_XOTH;
    }
  else if(p_in_node)
    {
      switch (p_in_node->access)
        {
        case MIB_ACCESS_READONLY:
          /* mode read pour tous  */
          mode = FSAL_MODE_RUSR | FSAL_MODE_RGRP | FSAL_MODE_ROTH;
          break;

        case MIB_ACCESS_READWRITE:
          /* mode read et write pour tous  */
          mode = FSAL_MODE_RUSR | FSAL_MODE_RGRP | FSAL_MODE_ROTH
              | FSAL_MODE_WUSR | FSAL_MODE_WGRP | FSAL_MODE_WOTH;
          break;

        case MIB_ACCESS_NOACCESS:
          mode = 0;
          break;

        default:
          mode = 0;
          LogMajor(COMPONENT_FSAL, "Warning: unsupported access mode %#X",
                   p_in_node->access);
        }
    }
  else
    {
      /* set a default mode for files */
      mode = FSAL_MODE_RUSR | FSAL_MODE_RGRP | FSAL_MODE_ROTH
          | FSAL_MODE_WUSR | FSAL_MODE_WGRP | FSAL_MODE_WOTH;
    }

  return mode;
}

/* convert the FSAL internal node type (root, node, leaf) to a classical FSAL nodetype (regular file, directory, ...)
 */
fsal_nodetype_t intern2extern_type(nodetype_t internal_type)
{
  switch (internal_type)
    {
    case FSAL_NODETYPE_LEAF:
      return FSAL_TYPE_FILE;

    case FSAL_NODETYPE_NODE:
    case FSAL_NODETYPE_ROOT:
      return FSAL_TYPE_DIR;

    default:
      LogCrit(COMPONENT_FSAL, "Error: unexpected internal type %d",
              internal_type);
      return 0;

    }
}

#define PRIME_32BITS  479001599

/* compute the object id from the handle */
fsal_u64_t build_object_id(snmpfsal_handle_t * p_in_handle)
{
  unsigned int i;
  fsal_u64_t hash = 1;

  /* for the moment, we make a very silly hash of the object  */

  for(i = 0; i < p_in_handle->data.oid_len; i++)
    {
      hash = ((hash << 8) ^ p_in_handle->data.oid_tab[i]) % PRIME_32BITS;
    }

  return hash;

}                               /* build_object_id */

/* fill the p_fsalattr_out structure depending on the given information
 * /!\ the p_in_node is the PARENT NODE (access right are stored in the parent node)
 */
int snmp2fsal_attributes(snmpfsal_handle_t * p_handle, netsnmp_variable_list * p_var,
                         struct tree *p_in_node, fsal_attrib_list_t * p_fsalattr_out)
{
  fsal_attrib_mask_t supp_attr, unsupp_attr;
  int rc;

  /* sanity checks */
  if(!p_handle || !p_fsalattr_out)
    return ERR_FSAL_FAULT;

  if(p_fsalattr_out->asked_attributes == 0)
    {
      p_fsalattr_out->asked_attributes = global_fs_info.supported_attrs;

      LogMajor(COMPONENT_FSAL,
               "Error: p_fsalattr_out->asked_attributes was set to 0 in snmp2fsal_attributes line %d, file %s: retrieving all supported attributes",
               __LINE__, __FILE__);
    }

  /* check that asked attributes are supported */
  supp_attr = global_fs_info.supported_attrs;
  unsupp_attr = (p_fsalattr_out->asked_attributes) & (~supp_attr);

  if(unsupp_attr)
    {
      LogMajor(COMPONENT_FSAL,
               "Unsupported attributes: %#llX removing it from asked attributes ",
               unsupp_attr);
      p_fsalattr_out->asked_attributes =
          p_fsalattr_out->asked_attributes & (~unsupp_attr);
    }

  /* Fills the output struct */
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SUPPATTR))
    p_fsalattr_out->supported_attributes = supp_attr;

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_TYPE))
    p_fsalattr_out->type = intern2extern_type(p_handle->data.object_type_reminder);

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SIZE) ||
     FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SPACEUSED))
    {
      if(p_handle->data.object_type_reminder == FSAL_NODETYPE_LEAF)
        {
          char object_val_buf[FSALSNMP_MAX_FILESIZE];
          size_t buf_sz = FSALSNMP_MAX_FILESIZE;

          /* first, build an object data representation for estimating size  */
          rc = snmp_object2str(p_var, object_val_buf, &buf_sz);

          if(rc != 0)
            {
              LogCrit(COMPONENT_FSAL,
                      "Error %d converting object data to string", rc);
              return rc;
            }

          p_fsalattr_out->asked_attributes |= FSAL_ATTR_SIZE | FSAL_ATTR_SPACEUSED;

          p_fsalattr_out->filesize = buf_sz;
          p_fsalattr_out->spaceused = buf_sz;
        }
      else                      /* this is a directory */
        {
          p_fsalattr_out->asked_attributes |= FSAL_ATTR_SIZE | FSAL_ATTR_SPACEUSED;

          p_fsalattr_out->filesize = 0;
          p_fsalattr_out->spaceused = 0;
        }
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FSID))
    {
      p_fsalattr_out->fsid.major = 222;
      /* @todo: compute fsid from server address and port number  */ ;
      p_fsalattr_out->fsid.minor = 111;
      /* @todo: compute fsid from server address and port number  */ ;
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FILEID))
    p_fsalattr_out->fileid = build_object_id(p_handle);

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MODE))
    p_fsalattr_out->mode =
        snmp_object2access_mode(p_handle->data.object_type_reminder, p_in_node);

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_NUMLINKS))
    p_fsalattr_out->numlinks = 1;       /* @todo */

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_OWNER))
    p_fsalattr_out->owner = 0;  /* @todo */

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_GROUP))
    p_fsalattr_out->group = 0;  /* @todo */

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ATIME) ||
     FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MTIME) ||
     FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CTIME) ||
     FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      fsal_time_t curr_time;
      struct timeval now;

      gettimeofday(&now, NULL);

      curr_time.seconds = now.tv_sec;
      curr_time.nseconds = now.tv_usec * 1000;

      p_fsalattr_out->mtime =
          p_fsalattr_out->ctime =
          p_fsalattr_out->atime = p_fsalattr_out->chgtime = curr_time;

      p_fsalattr_out->asked_attributes |= FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
          | FSAL_ATTR_CTIME | FSAL_ATTR_CHGTIME;

    }

  return ERR_FSAL_NO_ERROR;

}

/* return the type for snmp_add_var, given the associated ASN_xxx type */
char ASN2add_var(u_char asn_type)
{
  switch (asn_type)
    {
    case ASN_INTEGER:
      return 'i';
    case ASN_COUNTER:
      return 'c';
    case ASN_GAUGE:
      return 'u';
    case ASN_UINTEGER:
      return 'u';
    case ASN_OCTET_STR:
      return 's';
    case ASN_OBJECT_ID:
      return 'o';
    case ASN_IPADDRESS:
      return 'a';
    case ASN_TIMETICKS:
      return 't';
    case ASN_OPAQUE:
      return 'x';
    case ASN_COUNTER64:
      return 'U';
    case ASN_OPAQUE_FLOAT:
      return 'F';
    case ASN_NULL:
      return 'n';
    default:
      /* give a chance to net-snmp for finding the type  */
      return '=';
    }

}
