/**
 *
 * \file    fsal_xattrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2007/08/23 $
 * \version $Revision: 1.0 $
 * \brief   Extended attributes functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "HPSSclapiExt/hpssclapiext.h"

#include <hpss_uuid.h>
#include <hpss_errno.h>
#include <hpss_limits.h>

#if HPSS_LEVEL >= 730
#include <hpss_xml.h>
#endif

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200

/* function for getting an attribute value */

typedef int (*xattr_getfunc_t) (hpssfsal_handle_t *,    /* object handle */
                                hpssfsal_op_context_t *,        /* context */
                                caddr_t,        /* output buff */
                                size_t, /* output buff size */
                                size_t *);      /* output size */

typedef int (*xattr_setfunc_t) (hpssfsal_handle_t *,    /* object handle */
                                hpssfsal_op_context_t *,        /* context */
                                caddr_t,        /* input buff */
                                size_t, /* input size */
                                int);   /* creation flag */

typedef int (*xattr_printfunc_t) (caddr_t,      /* Input buffer */
                                  size_t,       /* Input size   */
                                  caddr_t,      /* Output (ASCII) buffer */
                                  size_t *);    /* Output size */

typedef struct fsal_xattr_def__
{
  char xattr_name[FSAL_MAX_NAME_LEN+1];
  xattr_getfunc_t get_func;
  xattr_setfunc_t set_func;
  xattr_printfunc_t print_func;
  int flags;
} fsal_xattr_def_t;

/*
 * function for getting extended attributes for HPSS
 */

/* Class of service */
int get_file_cos(hpssfsal_handle_t * p_objecthandle,    /* IN */
                 hpssfsal_op_context_t * p_context,     /* IN */
                 caddr_t buffer_addr,   /* IN/OUT */
                 size_t buffer_size,    /* IN */
                 size_t * p_output_size)        /* OUT */
{
  int rc;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;

  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* get attributes */
  /* We use  HPSSFSAL_GetRawAttrHandle for not chasing junctions
   * nor solving symlinks. What's more, we want hpss_Attrs_t.
   */

  TakeTokenFSCall();

  rc = HPSSFSAL_GetRawAttrHandle(&(p_objecthandle->data.ns_handle), NULL, &p_context->credential.hpss_usercred, FALSE,       /* don't solve junctions */
                                 &hpss_hdl, NULL, &hpss_attr);

  ReleaseTokenFSCall();

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    return ERR_FSAL_STALE;
  else if(rc)
    return hpss2fsal_error(rc);

  /* then store cos id in the output buffer */

  if(buffer_size < sizeof(hpss_attr.COSId))
    return ERR_FSAL_TOOSMALL;

  memcpy(buffer_addr, &hpss_attr.COSId, sizeof(hpss_attr.COSId));

  *p_output_size = sizeof(hpss_attr.COSId);

  return 0;

}

int print_file_cos(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  unsigned int cosid = 0;

  memcpy((char *)&cosid, InBuff, sizeof(cosid));

  *pOutSize = snprintf(OutBuff, *pOutSize, "%u\n", cosid);
  return 0;
}                               /* print_file_cos */

/* Storage levels */
int get_file_slevel(hpssfsal_handle_t * p_objecthandle, /* IN */
                    hpssfsal_op_context_t * p_context,  /* IN */
                    caddr_t buffer_addr,        /* IN/OUT */
                    size_t buffer_size, /* IN */
                    size_t * p_output_size)     /* OUT */
{
  int rc, i;
  hpss_xfileattr_t hpss_xattr;
  char tmpstr[1024];
  char *outbuff;
  unsigned int index;

  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* get storage info */

  TakeTokenFSCall();

#if HPSS_LEVEL < 622
  rc = HPSSFSAL_FileGetXAttributesHandle(&(p_objecthandle->data.ns_handle),
                                         API_GET_STATS_FOR_ALL_LEVELS, 0, &hpss_xattr);
#elif HPSS_LEVEL == 622
  rc = hpss_FileGetXAttributesHandle(&(p_objecthandle->data.ns_handle),
                                     NULL,
                                     &(p_context->credential.hpss_usercred),
                                     API_GET_STATS_FOR_ALL_LEVELS, 0, NULL, &hpss_xattr);
#elif HPSS_MAJOR_VERSION >= 7
  rc = hpss_FileGetXAttributesHandle(&(p_objecthandle->data.ns_handle),
                                     NULL,
                                     &(p_context->credential.hpss_usercred),
                                     API_GET_STATS_FOR_ALL_LEVELS, 0, &hpss_xattr);
#endif

  ReleaseTokenFSCall();

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    return ERR_FSAL_STALE;
  else if(rc)
    return hpss2fsal_error(rc);

  /* now write info to buffer */

  outbuff = (char *)buffer_addr;
  outbuff[0] = '\0';

  for(index = 0; index < HPSS_MAX_STORAGE_LEVELS; index++)
    {
      if(hpss_xattr.SCAttrib[index].Flags == 0)
        continue;

      if(hpss_xattr.SCAttrib[index].Flags & BFS_BFATTRS_LEVEL_IS_DISK)
        snprintf(tmpstr, 1024, "Level %u (disk): %llu bytes\n", index,
                 hpss2fsal_64(hpss_xattr.SCAttrib[index].BytesAtLevel));
      else if(hpss_xattr.SCAttrib[index].Flags & BFS_BFATTRS_LEVEL_IS_TAPE)
        snprintf(tmpstr, 1024, "Level %u (tape): %llu bytes\n", index,
                 hpss2fsal_64(hpss_xattr.SCAttrib[index].BytesAtLevel));
      else
        snprintf(tmpstr, 1024, "Level %u: %llu bytes\n", index,
                 hpss2fsal_64(hpss_xattr.SCAttrib[index].BytesAtLevel));

      if(strlen(tmpstr) + strlen(outbuff) < buffer_size)
        strcat(outbuff, tmpstr);
      else
        break;

    }

    /* free the returned structure (Cf. HPSS ClAPI documentation) */
    for ( i = 0; i < HPSS_MAX_STORAGE_LEVELS; i++ )
    {
	int j;
        for ( j = 0; j < hpss_xattr.SCAttrib[i].NumberOfVVs; j++ )
        {
            if ( hpss_xattr.SCAttrib[i].VVAttrib[j].PVList != NULL )
            {
                free( hpss_xattr.SCAttrib[i].VVAttrib[j].PVList );
            }
        }
    }

    *p_output_size = strlen(outbuff) + 1;

    return 0;
}

int print_ns_handle(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  unsigned int i = 0;
  *pOutSize = 0;

  for(i = 0; i < InSize; i++)
    *pOutSize +=
        sprintf(&(((char *)OutBuff)[i * 2]), "%02x",
                (unsigned char)(((char *)InBuff)[i]));

  ((char *)OutBuff)[*pOutSize] = '\n';
  *pOutSize += 1;

  return 0;
}                               /* print_ns_handle */

/* Namespace handle */
int get_ns_handle(hpssfsal_handle_t * p_objecthandle,   /* IN */
                  hpssfsal_op_context_t * p_context,    /* IN */
                  caddr_t buffer_addr,  /* IN/OUT */
                  size_t buffer_size,   /* IN */
                  size_t * p_output_size)       /* OUT */
{

  if(buffer_size > sizeof(ns_ObjHandle_t))
    {
      memcpy(buffer_addr, (caddr_t) & p_objecthandle->data.ns_handle, sizeof(ns_ObjHandle_t));
      *p_output_size = sizeof(ns_ObjHandle_t);
    }
  else
    {
      memcpy(buffer_addr, (caddr_t) & p_objecthandle->data.ns_handle, buffer_size);
      *p_output_size = buffer_size;
    }

  return 0;

}

/* Object type */
int get_obj_type(hpssfsal_handle_t * p_objecthandle,    /* IN */
                 hpssfsal_op_context_t * p_context,     /* IN */
                 caddr_t buffer_addr,   /* IN/OUT */
                 size_t buffer_size,    /* IN */
                 size_t * p_output_size)        /* OUT */
{
  switch (p_objecthandle->data.obj_type)
    {
    case FSAL_TYPE_DIR:
      strncpy((char *)buffer_addr, "directory", buffer_size);
      break;

    case FSAL_TYPE_FILE:
      strncpy((char *)buffer_addr, "file", buffer_size);
      break;

    case FSAL_TYPE_LNK:
      strncpy((char *)buffer_addr, "symlink", buffer_size);
      break;

    case FSAL_TYPE_JUNCTION:
      strncpy((char *)buffer_addr, "junction", buffer_size);
      break;

    default:
      strncpy((char *)buffer_addr, "other/unknown", buffer_size);
      break;
    }
  ((char *)buffer_addr)[strlen((char *)buffer_addr)] = '\n';
  *p_output_size = strlen((char *)buffer_addr) + 1;
  return 0;
}

/* Bitfile ID */
int get_bfid(hpssfsal_handle_t * p_objecthandle,        /* IN */
             hpssfsal_op_context_t * p_context, /* IN */
             caddr_t buffer_addr,       /* IN/OUT */
             size_t buffer_size,        /* IN */
             size_t * p_output_size)    /* OUT */
{
  int rc;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;
  char *tmp_str_uuid;

  if((rc = HPSSFSAL_GetRawAttrHandle(&(p_objecthandle->data.ns_handle),
                                     NULL,
                                     &(p_context->credential.hpss_usercred),
                                     FALSE, &hpss_hdl, NULL, &hpss_attr)) != 0)
    {
      return hpss2fsal_error(rc);
    }

  uuid_to_string(&(hpss_attr.BitfileId.ObjectID), (char **)&tmp_str_uuid, &rc);
  if(rc != 0)
    {
      return hpss2fsal_error(rc);
    }

  strncpy((char *)buffer_addr, tmp_str_uuid, buffer_size);
  *p_output_size = strlen((char *)buffer_addr) + 1;

  /* HPSS returns a string that it has just allocated.
   * Free it to avoid memory leak.
   */
  free(tmp_str_uuid);

  return 0;
}

static fsal_xattr_def_t xattr_list[] = {
  /* for all kind of entries */
  {"ns_handle", get_ns_handle, NULL, print_ns_handle, XATTR_FOR_ALL | XATTR_RO},
  {"type", get_obj_type, NULL, NULL, XATTR_FOR_ALL | XATTR_RO},

  /* for files only */
  {"bitfile_id", get_bfid, NULL, NULL, XATTR_FOR_FILE | XATTR_RO},
  {"class_of_service", get_file_cos, NULL, print_file_cos, XATTR_FOR_FILE | XATTR_RO},
  {"storage_levels", get_file_slevel, NULL, NULL, XATTR_FOR_FILE | XATTR_RO}

};

#define XATTR_COUNT 5

/* we assume that this number is < 254 */
#if ( XATTR_COUNT > 254 )
#error "ERROR: xattr count > 254"
#endif

/* test if an object has a given attribute */
static int do_match_type(int xattr_flag, fsal_nodetype_t obj_type)
{
  switch (obj_type)
    {
    case FSAL_TYPE_FILE:
      return ((xattr_flag & XATTR_FOR_FILE) == XATTR_FOR_FILE);

    case FSAL_TYPE_DIR:
      return ((xattr_flag & XATTR_FOR_DIR) == XATTR_FOR_DIR);

    case FSAL_TYPE_LNK:
      return ((xattr_flag & XATTR_FOR_SYMLINK) == XATTR_FOR_SYMLINK);

    default:
      return ((xattr_flag & XATTR_FOR_ALL) == XATTR_FOR_ALL);
    }
}

static int attr_is_read_only(unsigned int attr_index)
{
  if(attr_index < XATTR_COUNT)
    {
      if(xattr_list[attr_index].flags & XATTR_RO)
        return TRUE;
    }
  /* else : UDA */
  return FALSE;
}

static int file_attributes_to_xattr_attrs(fsal_attrib_list_t * file_attrs,
                                          fsal_attrib_list_t * p_xattr_attrs,
                                          unsigned int attr_index)
{

  /* supported attributes are:
   * - owner (same as the objet)
   * - group (same as the objet)
   * - type FSAL_TYPE_XATTR
   * - fileid (attr index ? or (fileid^((index+1)<<24)) )
   * - mode (config & file)
   * - atime, mtime, ctime = these of the object ?
   * - size=1block, used=1block
   * - rdev=0
   * - nlink=1
   */
  fsal_attrib_mask_t supported = FSAL_ATTR_SUPPATTR | FSAL_ATTR_MODE | FSAL_ATTR_FILEID
      | FSAL_ATTR_TYPE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP
      | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME | FSAL_ATTR_CTIME
      | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_SIZE
      | FSAL_ATTR_SPACEUSED | FSAL_ATTR_NUMLINKS | FSAL_ATTR_RAWDEV | FSAL_ATTR_FSID;
  fsal_attrib_mask_t unsupp;

  /* only those supported by filesystem */
  supported &= global_fs_info.supported_attrs;

  if(p_xattr_attrs->asked_attributes == 0)
    {
      p_xattr_attrs->asked_attributes = supported;

      LogCrit(COMPONENT_FSAL,
                        "Error: p_xattr_attrs->asked_attributes was 0 in %s() line %d, file %s",
                        __FUNCTION__, __LINE__, __FILE__);
    }

  unsupp = p_xattr_attrs->asked_attributes & (~supported);

  if(unsupp)
    {
      LogDebug(COMPONENT_FSAL,
                        "Asking for unsupported attributes in %s(): %#llX removing it from asked attributes",
                        __FUNCTION__, unsupp);

      p_xattr_attrs->asked_attributes &= (~unsupp);
    }

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_SUPPATTR)
    p_xattr_attrs->supported_attributes = supported;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_MODE)
    {
      p_xattr_attrs->mode = file_attrs->mode & global_fs_info.xattr_access_rights;

      if(attr_is_read_only(attr_index))
        p_xattr_attrs->mode &= ~(0222);
    }

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_FILEID)
    {
      unsigned int i;
      unsigned long hash = attr_index + 1;
      char *str = (char *)&file_attrs->fileid;

      for(i = 0; i < sizeof(p_xattr_attrs->fileid); i++, str++)
        {
          hash = (hash << 5) - hash + (unsigned long)(*str);
        }
      p_xattr_attrs->fileid = hash;
    }

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_TYPE)
    p_xattr_attrs->type = FSAL_TYPE_XATTR;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_OWNER)
    p_xattr_attrs->owner = file_attrs->owner;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_GROUP)
    p_xattr_attrs->group = file_attrs->group;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_ATIME)
    p_xattr_attrs->atime = file_attrs->atime;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_MTIME)
    p_xattr_attrs->mtime = file_attrs->mtime;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_CTIME)
    p_xattr_attrs->ctime = file_attrs->ctime;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_CREATION)
    p_xattr_attrs->creation = file_attrs->creation;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_CHGTIME)
    {
      p_xattr_attrs->chgtime = file_attrs->chgtime;
      p_xattr_attrs->change = (uint64_t) p_xattr_attrs->chgtime.seconds;
    }

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_SIZE)
    p_xattr_attrs->filesize = DEV_BSIZE;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_SPACEUSED)
    p_xattr_attrs->spaceused = DEV_BSIZE;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_NUMLINKS)
    p_xattr_attrs->numlinks = 1;

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_RAWDEV)
    {
      p_xattr_attrs->rawdev.major = 0;
      p_xattr_attrs->rawdev.minor = 0;
    }

  if(p_xattr_attrs->asked_attributes & FSAL_ATTR_FSID)
    {
      p_xattr_attrs->fsid = file_attrs->fsid;
    }

  /* if mode==0, then owner is set to root and mode is set to 0600 */
  if((p_xattr_attrs->asked_attributes & FSAL_ATTR_OWNER)
     && (p_xattr_attrs->asked_attributes & FSAL_ATTR_MODE) && (p_xattr_attrs->mode == 0))
    {
      p_xattr_attrs->owner = 0;
      p_xattr_attrs->mode = 0600;
      if(attr_is_read_only(attr_index))
        p_xattr_attrs->mode &= ~(0200);
    }

  return 0;

}

/**
 * Get the attributes of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_cookie xattr's cookie (as returned by listxattrs).
 * \param p_attrs xattr's attributes.
 */
fsal_status_t HPSSFSAL_GetXAttrAttrs(hpssfsal_handle_t * p_objecthandle,        /* IN */
                                     hpssfsal_op_context_t * p_context, /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_attrib_list_t * p_attrs
                                          /**< IN/OUT xattr attributes (if supported) */
    )
{
  int rc;
  char buff[MAXNAMLEN+1];
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_attrs)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrAttrs);

  /* check that this index match the type of entry */
  if(xattr_id < XATTR_COUNT
     && !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->data.obj_type))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrAttrs);
    }
  else if(xattr_id >= XATTR_COUNT)
    {
      /* This is UDA */
      LogFullDebug(COMPONENT_FSAL, "Getting attributes for UDA #%u",
                        xattr_id - XATTR_COUNT);
    }

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve attributes not asked */

  file_attrs.asked_attributes &= p_attrs->asked_attributes;

  st = HPSSFSAL_getattrs(p_objecthandle, p_context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_GetXAttrAttrs);

  if((rc = file_attributes_to_xattr_attrs(&file_attrs, p_attrs, xattr_id)))
    {
      Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_GetXAttrAttrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrAttrs);

}                               /* FSAL_GetXAttrAttrs */

static int hpss_uda_name_2_fsal(const char *src, char *out)
{
  const char *curr_src = src;
  char *curr = out;

  /* skip first '/' */
  while((*curr_src == '/') && (*curr_src != '\0'))
    curr_src++;
  if(*curr_src == '\0')
    return ERR_FSAL_INVAL;

  strcpy(curr, curr_src);
  while((curr = strchr(out, '/')) != NULL)
    {
      *curr = '.';
    }
  return 0;
}

static int fsal_xattr_name_2_uda(const char *src, char *out)
{
  char *curr = out;

  /* add first / */
  *curr = '/';
  curr++;

  /* copy the xattr name */
  strcpy(curr, src);

  /* then replace '.' with '/' */
  while((curr = strchr(out, '.')) != NULL)
    {
      *curr = '/';
    }

  /* UDA path must start with '/hpss/' */
  if(strncmp(out, "/hpss/", 6) != 0)
    return ERR_FSAL_INVAL;

  return 0;
}

/**
 * Retrieves the list of extended attributes for an object in the filesystem.
 * 
 * \param p_objecthandle Handle of the object we want to get extended attributes.
 * \param cookie index of the next entry to be returned.
 * \param p_context pointer to the current security context.
 * \param xattrs_tab a table for storing extended attributes list to.
 * \param xattrs_tabsize the maximum number of xattr entries that xattrs_tab
 *            can contain.
 * \param p_nb_returned the number of xattr entries actually stored in xattrs_tab.
 * \param end_of_list this boolean indicates that the end of xattrs list has been reached.
 */
fsal_status_t HPSSFSAL_ListXAttrs(hpssfsal_handle_t * p_objecthandle,   /* IN */
                                  unsigned int argcookie,  /* IN */
                                  hpssfsal_op_context_t * p_context,    /* IN */
                                  fsal_xattrent_t * xattrs_tab, /* IN/OUT */
                                  unsigned int xattrs_tabsize,  /* IN */
                                  unsigned int *p_nb_returned,  /* OUT */
                                  int *end_of_list      /* OUT */
    )
{
  unsigned int index;
  unsigned int out_index;
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;
  int rc;
  unsigned int cookie = argcookie ;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !xattrs_tab || !p_nb_returned || !end_of_list)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_ListXAttrs);

  /* Deal with special cookie */
  if( argcookie == FSAL_XATTR_RW_COOKIE ) cookie = XATTR_COUNT ;

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve unsuipported attributes */
  file_attrs.asked_attributes &= global_fs_info.supported_attrs;

  st = FSAL_getattrs(p_objecthandle, p_context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_ListXAttrs);

  for(index = cookie, out_index = 0;
      index < XATTR_COUNT && out_index < xattrs_tabsize; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.obj_type))
        {

          /* fills an xattr entry */
          xattrs_tab[out_index].xattr_id = index;
          FSAL_str2name(xattr_list[index].xattr_name, FSAL_MAX_NAME_LEN,
                        &xattrs_tab[out_index].xattr_name);
          xattrs_tab[out_index].xattr_cookie = index + 1;

          /* set asked attributes (all supported) */
          xattrs_tab[out_index].attributes.asked_attributes =
              global_fs_info.supported_attrs;

          rc = file_attributes_to_xattr_attrs(&file_attrs,
                                              &xattrs_tab[out_index].attributes, index);

          if(rc != 0)
            {
              /* set error flag */
              LogDebug(COMPONENT_FSAL,
                                "Error %d getting attributes for xattr '%s'", rc,
                                xattrs_tab[out_index].xattr_name);
              xattrs_tab[out_index].attributes.asked_attributes = FSAL_ATTR_RDATTR_ERR;
            }

          /* next output slot */
          out_index++;
        }
    }

  *end_of_list = (index == XATTR_COUNT);

#if HPSS_LEVEL >= 730
  {
    /* save a call if output array is full */
    if(out_index == xattrs_tabsize)
      {
        *end_of_list = FALSE;
        *p_nb_returned = out_index;
        Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_ListXAttrs);
      }

    /* get list of UDAs for this entry */
    hpss_userattr_list_t attr_list;

    memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

    TakeTokenFSCall();
    rc = hpss_UserAttrListAttrHandle(&(p_objecthandle->data.ns_handle),
                                     NULL,
                                     &(p_context->credential.hpss_usercred),
                                     &attr_list, XML_ATTR);
    ReleaseTokenFSCall();

    if(rc == 0)
      {
        unsigned int i;
        for(i = 0; (i < attr_list.len) && (out_index < xattrs_tabsize); i++)
          {
            char attr_name[FSAL_MAX_NAME_LEN+1];

            /* the id is XATTR_COUNT + index of HPSS UDA */
            index = XATTR_COUNT + i;

            /* continue while index < cookie */
            if(index < cookie)
              continue;

            xattrs_tab[out_index].xattr_id = index;

            if(strlen(attr_list.Pair[i].Key) >= FSAL_MAX_NAME_LEN)
              Return(ERR_FSAL_NAMETOOLONG, 0, INDEX_FSAL_ListXAttrs);

            /* HPSS UDAs namespace is slash-separated.
             * we convert '/' to '.'
             */
            rc = hpss_uda_name_2_fsal(attr_list.Pair[i].Key, attr_name);

            if(rc != ERR_FSAL_NO_ERROR)
              Return(rc, 0, INDEX_FSAL_ListXAttrs);

            FSAL_str2name(attr_name, FSAL_MAX_NAME_LEN,
                          &xattrs_tab[out_index].xattr_name);
            xattrs_tab[out_index].xattr_cookie = index + 1;

            /* set asked attributes (all supported) */
            xattrs_tab[out_index].attributes.asked_attributes =
                global_fs_info.supported_attrs;

            rc = file_attributes_to_xattr_attrs(&file_attrs,
                                                &xattrs_tab[out_index].attributes, index);
            if(rc != 0)
              {
                /* set error flag */
                LogDebug(COMPONENT_FSAL,
                                  "Error %d getting attributes for xattr \'%s\'", rc,
                                  xattrs_tab[out_index].xattr_name);
                xattrs_tab[out_index].attributes.asked_attributes = FSAL_ATTR_RDATTR_ERR;
              }
            /* we know the size here (+2 for \n\0) */
            else if(attr_list.Pair[i].Value != NULL)
              xattrs_tab[out_index].attributes.filesize
                  = strlen(attr_list.Pair[i].Value) + 2;

            /* next output slot */
            out_index++;
          }
        /* not end of list if there is more UDAs */
        if(i < attr_list.len)
          *end_of_list = FALSE;
        else
          *end_of_list = TRUE;
      }
  }

#endif

  *p_nb_returned = out_index;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_ListXAttrs);

}

/**
 * Get the value of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t HPSSFSAL_GetXAttrValueById(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN/OUT */
                                         size_t buffer_size,    /* IN */
                                         size_t * p_output_size /* OUT */
    )
{
  int rc;
  char buff[MAXNAMLEN+1];

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* check that this index match the type of entry */
  if(xattr_id < XATTR_COUNT
     && !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->data.obj_type))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrValue);
    }
  else if(xattr_id >= XATTR_COUNT)
    {
#if HPSS_LEVEL >= 730
      /* This is a UDA */
      hpss_userattr_list_t attr_list;
      unsigned int i;
      memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

      LogFullDebug(COMPONENT_FSAL, "Getting value for UDA #%u",
                        xattr_id - XATTR_COUNT);

      /* get list of UDAs for this entry, and return the good value */

      TakeTokenFSCall();
      rc = hpss_UserAttrListAttrHandle(&(p_objecthandle->data.ns_handle),
                                       NULL,
                                       &(p_context->credential.hpss_usercred),
                                       &attr_list, XML_ATTR);
      ReleaseTokenFSCall();

      if(rc != 0)
        Return(hpss2fsal_error(rc), rc, INDEX_FSAL_GetXAttrValue);
      else if(xattr_id - XATTR_COUNT >= attr_list.len)
        /* this xattr does not exist anymore */
        Return(ERR_FSAL_STALE, 0, INDEX_FSAL_GetXAttrValue);

      if((attr_list.Pair[xattr_id - XATTR_COUNT].Value != NULL)
         && (attr_list.Pair[xattr_id - XATTR_COUNT].Value[0] != '\0'))
        snprintf((char *)buffer_addr, buffer_size, "%s\n",
                 attr_list.Pair[xattr_id - XATTR_COUNT].Value);
      else
        strcpy((char *)buffer_addr, "");

      *p_output_size = strlen((char *)buffer_addr) + 1;

      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);

#else
      /* udas are not supported. xattr_id is too high. */
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrValue);
#endif
    }

  /* get the value */

  if(xattr_list[xattr_id].print_func == NULL)
    {
      rc = xattr_list[xattr_id].get_func(p_objecthandle,
                                         p_context,
                                         buffer_addr, buffer_size, p_output_size);
    }
  else
    {
      rc = xattr_list[xattr_id].get_func(p_objecthandle,
                                         p_context, buff, MAXNAMLEN, p_output_size);

      xattr_list[xattr_id].print_func(buff, MAXNAMLEN, buffer_addr, p_output_size);
    }

  Return(rc, 0, INDEX_FSAL_GetXAttrValue);

}

/**
 * Get the index of an xattr based on its name
 *
 *   \param p_objecthandle Handle of the object you want to get attribute for.
 *   \param xattr_name the name of the attribute to be read.
 *   \param pxattr_id found xattr_id
 *   
 *   \return ERR_FSAL_NO_ERROR if xattr_name exists, ERR_FSAL_NOENT otherwise
 */
fsal_status_t HPSSFSAL_GetXAttrIdByName(hpssfsal_handle_t * p_objecthandle,     /* IN */
                                        const fsal_name_t * xattr_name, /* IN */
                                        hpssfsal_op_context_t * p_context,      /* IN */
                                        unsigned int *pxattr_id /* OUT */
    )
{
  unsigned int index, i;
  int found = FALSE;

  /* sanity checks */
  if(!p_objecthandle || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.obj_type)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {
          found = TRUE;
          break;
        }
    }

#if HPSS_LEVEL >= 730
  if(!found)
    {
      /* search for name in UDAs */
      hpss_userattr_list_t attr_list;
      unsigned int i;
      int rc;
      char attrpath[FSAL_MAX_NAME_LEN+1];

      /* convert FSAL xattr name to HPSS attr path.
       * returns error if it is not a UDA name.
       */
      if(fsal_xattr_name_2_uda(xattr_name->name, attrpath) == 0)
        {

          memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

          LogFullDebug(COMPONENT_FSAL, "looking for xattr '%s' in UDAs",
                            xattr_name->name);

          /* get list of UDAs for this entry, and return the good index */

          TakeTokenFSCall();
          rc = hpss_UserAttrListAttrHandle(&(p_objecthandle->data.ns_handle),
                                           NULL,
                                           &(p_context->credential.hpss_usercred),
                                           &attr_list, XML_ATTR);
          ReleaseTokenFSCall();

          if(rc == 0)
            {

              for(i = 0; i < attr_list.len; i++)
                {
                  if(!strcmp(attr_list.Pair[i].Key, attrpath))
                    {
                      /* xattr index is XATTR_COUNT + UDA index */
                      index = XATTR_COUNT + i;
                      found = TRUE;
                      break;
                    }
                }
            }

        }
      /* enf if valid UDA name */
    }                           /* end if not found */
#endif

  if(found)
    {
      *pxattr_id = index;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
    }
  else
    Return(ERR_FSAL_NOENT, ENOENT, INDEX_FSAL_GetXAttrValue);
}                               /* FSAL_GetXAttrIdByName */

/**
 * Get the value of an extended attribute from its name.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t HPSSFSAL_GetXAttrValueByName(hpssfsal_handle_t * p_objecthandle,  /* IN */
                                           const fsal_name_t * xattr_name,      /* IN */
                                           hpssfsal_op_context_t * p_context,   /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size       /* OUT */
    )
{
  unsigned int index;
  fsal_status_t st;
#if HPSS_LEVEL >= 730
  char attrpath[MAXPATHLEN];
  char attrval[MAXPATHLEN];
  int rc;
#endif

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* look for this name */

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.obj_type)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {

          return FSAL_GetXAttrValueById(p_objecthandle, index, p_context,
                                        buffer_addr, buffer_size, p_output_size);

        }
    }

#if HPSS_LEVEL >= 730
  if(fsal_xattr_name_2_uda(xattr_name->name, attrpath) == 0)
    {
      /* get uda value */
      hpss_userattr_list_t attr;

      attr.len = 1;
      /* use malloc because HPSS may free it */
      attr.Pair = malloc(sizeof(hpss_userattr_t));
      if(attr.Pair == NULL)
        Return(ERR_FSAL_NOMEM, errno, INDEX_FSAL_GetXAttrValue);

      attr.Pair[0].Key = attrpath;
      attr.Pair[0].Value = attrval;

      rc = hpss_UserAttrGetAttrHandle(&(p_objecthandle->data.ns_handle),
                                      NULL, &(p_context->credential.hpss_usercred),
                                      &attr, UDA_API_VALUE);
      if(rc)
        {
          free(attr.Pair);
          Return(hpss2fsal_error(rc), rc, INDEX_FSAL_GetXAttrValue);
        }

      if(attr.len > 0)
        {
          if(attr.Pair[0].Value != NULL)
            {
              char *noxml = hpss_ChompXMLHeader(attr.Pair[0].Value, NULL);
              strcpy(attrval, noxml);
              free(noxml);
              strncpy((char *)buffer_addr, attrval, buffer_size);
              *p_output_size = strlen(attrval) + 1;
            }
          else
            {
              strcpy((char *)buffer_addr, "");
              *p_output_size = 1;
            }

          free(attr.Pair);
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
        }
      else
        {
          free(attr.Pair);
          Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);
        }
    }
#endif

  /* not found */
  Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);

}

static void chomp_attr_value(char *str, size_t size)
{
  int len;

  if(str == NULL)
    return;

  /* security: set last char to '\0' */
  str[size - 1] = '\0';

  len = strnlen(str, size);
  if((len > 0) && (str[len - 1] == '\n'))
    str[len - 1] = '\0';
}

fsal_status_t HPSSFSAL_SetXAttrValue(hpssfsal_handle_t * p_objecthandle,        /* IN */
                                     const fsal_name_t * xattr_name,    /* IN */
                                     hpssfsal_op_context_t * p_context, /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size,        /* IN */
                                     int create /* IN */
    )
{
#if HPSS_LEVEL >= 730
  int rc;
  char attrpath[FSAL_MAX_NAME_LEN+1];
  hpss_userattr_list_t inAttr;

  /* check that UDA name is valid */
  if(fsal_xattr_name_2_uda(xattr_name->name, attrpath) != 0)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_SetXAttrValue);

  /* remove '\n" */
  chomp_attr_value((char *)buffer_addr, buffer_size);

  /* Set the UDA value */

  inAttr.len = 1;
  /* must use malloc()  here, because hpss clapi may free() it */
  inAttr.Pair = malloc(inAttr.len * sizeof(hpss_userattr_t));
  inAttr.Pair[0].Key = attrpath;
  inAttr.Pair[0].Value = (char *)buffer_addr;

  TakeTokenFSCall();
  rc = hpss_UserAttrSetAttrHandle(&(p_objecthandle->data.ns_handle),
                                  NULL,
                                  &(p_context->credential.hpss_usercred), &inAttr, NULL);
  ReleaseTokenFSCall();

  free(inAttr.Pair);

  Return(hpss2fsal_error(rc), rc, INDEX_FSAL_SetXAttrValue);

#else
  Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
#endif
}

fsal_status_t HPSSFSAL_SetXAttrValueById(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size     /* IN */
    )
{
#if HPSS_LEVEL >= 730
  hpss_userattr_list_t attr_list;
  hpss_userattr_list_t inAttr;
  unsigned int i;
  int rc;

  if(attr_is_read_only(xattr_id))
    Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
  else if(xattr_id < XATTR_COUNT)
    /* this is not a UDA (setattr not supported) */
    Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);

  /* remove '\n" */
  chomp_attr_value((char *)buffer_addr, buffer_size);

  memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

  LogFullDebug(COMPONENT_FSAL, "Getting name of UDA #%u",
                    xattr_id - XATTR_COUNT);

  TakeTokenFSCall();
  rc = hpss_UserAttrListAttrHandle(&(p_objecthandle->data.ns_handle),
                                   NULL,
                                   &(p_context->credential.hpss_usercred),
                                   &attr_list, XML_ATTR);
  ReleaseTokenFSCall();

  if(rc != 0)
    Return(hpss2fsal_error(rc), rc, INDEX_FSAL_SetXAttrValue);
  else if(xattr_id - XATTR_COUNT >= attr_list.len)
    /* this xattr does not exist anymore */
    Return(ERR_FSAL_STALE, 0, INDEX_FSAL_SetXAttrValue);

  /* set the UDA by its name */

  inAttr.len = 1;
  /* must use malloc()  here, because hpss clapi may free() it */
  inAttr.Pair = malloc(inAttr.len * sizeof(hpss_userattr_t));
  inAttr.Pair[0].Key = attr_list.Pair[xattr_id - XATTR_COUNT].Key;
  inAttr.Pair[0].Value = (char *)buffer_addr;

  TakeTokenFSCall();
  rc = hpss_UserAttrSetAttrHandle(&(p_objecthandle->data.ns_handle),
                                  NULL,
                                  &(p_context->credential.hpss_usercred), &inAttr, NULL);
  ReleaseTokenFSCall();

  free(inAttr.Pair);

  Return(hpss2fsal_error(rc), rc, INDEX_FSAL_SetXAttrValue);

#else
  Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
#endif
}

/**
 *  Removes a xattr by Id
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_id xattr's id
 */
fsal_status_t HPSSFSAL_RemoveXAttrById(hpssfsal_handle_t * p_objecthandle,      /* IN */
                                       hpssfsal_op_context_t * p_context,       /* IN */
                                       unsigned int xattr_id)   /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_RemoveXAttrById */

/**
 *  Removes a xattr by Name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_name xattr's name
 */
fsal_status_t HPSSFSAL_RemoveXAttrByName(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         const fsal_name_t * xattr_name)        /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_RemoveXAttrById */

int HPSSFSAL_GetXattrOffsetSetable( void )
{
  return XATTR_COUNT ;
}
