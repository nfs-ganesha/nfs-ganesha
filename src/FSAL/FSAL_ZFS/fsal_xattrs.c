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
#include "fsal_common.h"

#include <string.h>

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200

/* function for getting an attribute value */

typedef int (*xattr_getfunc_t) (zfsfsal_handle_t *,        /* object handle */
                                zfsfsal_op_context_t *,    /* context */
                                caddr_t,        /* output buff */
                                size_t, /* output buff size */
                                size_t *);      /* output size */

typedef int (*xattr_setfunc_t) (zfsfsal_handle_t *,        /* object handle */
                                zfsfsal_op_context_t *,    /* context */
                                caddr_t,        /* input buff */
                                size_t, /* input size */
                                int);   /* creation flag */

typedef int (*xattr_printfunc_t) (caddr_t,      /* Input buffer */
                                  size_t,       /* Input size   */
                                  caddr_t,      /* Output (ASCII) buffer */
                                  size_t *);    /* Output size */

typedef struct fsal_xattr_def__
{
  char xattr_name[FSAL_MAX_NAME_LEN];
  xattr_getfunc_t get_func;
  xattr_setfunc_t set_func;
  xattr_printfunc_t print_func;
  int flags;
} fsal_xattr_def_t;

/*
 * DEFINE HERE YOUR GET/SET FUNCTIONS
 */

int get_generation(zfsfsal_handle_t * p_objecthandle,       /* IN */
                   zfsfsal_op_context_t * p_context,        /* IN */
                   caddr_t buffer_addr,  /* IN/OUT */
                   size_t buffer_size,   /* IN */
                   size_t * p_output_size)       /* OUT */
{
  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  memcpy(buffer_addr, &p_objecthandle->data.zfs_handle.generation,
         sizeof(p_objecthandle->data.zfs_handle.generation));
  *p_output_size = sizeof(p_objecthandle->data.zfs_handle.generation);

  return 0;
}

int print_generation(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  uint64_t generation;

  memcpy((char *)&generation, InBuff, sizeof(generation));

  *pOutSize = snprintf(OutBuff, *pOutSize, "%"PRIu64, generation);

  return 0;
}

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static fsal_xattr_def_t xattr_list[] = {
  {"generation", get_generation, NULL, print_generation, XATTR_FOR_ALL}// | XATTR_RO}
};

#define XATTR_COUNT 1

/* we assume that this number is < 254 */
#if ( XATTR_COUNT > 254 )
#error "ERROR: xattr count > 254"
#endif

/* YOUR SHOULD NOT HAVE TO MODIFY THE FOLLOWING FUNCTIONS */

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
  /* else : standard xattr */
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
fsal_status_t ZFSFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                    fsal_op_context_t * p_context, /* IN */
                                    unsigned int xattr_id, /* IN */
                                    fsal_attrib_list_t * p_attrs
                                          /**< IN/OUT xattr attributes (if supported) */
    )
{
  int rc;
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_attrs)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrAttrs);

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME | FSAL_ATTR_TYPE
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve attributes not asked */
  file_attrs.asked_attributes &= p_attrs->asked_attributes;

  st = ZFSFSAL_getattrs(p_objecthandle, p_context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_GetXAttrAttrs);

  /* check that this index match the type of entry */
  if(xattr_id < XATTR_COUNT
     && !do_match_type(xattr_list[xattr_id].flags, file_attrs.type))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrAttrs);
    }
  else if(xattr_id >= XATTR_COUNT)
    {
      /* This is user defined xattr */
      LogFullDebug(COMPONENT_FSAL,
                        "Getting attributes for xattr #%u", xattr_id - XATTR_COUNT);
    }

  if((rc = file_attributes_to_xattr_attrs(&file_attrs, p_attrs, xattr_id)))
    {
      Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_GetXAttrAttrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrAttrs);

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
fsal_status_t ZFSFSAL_ListXAttrs(fsal_handle_t * obj_handle,   /* IN */
                              unsigned int cookie,      /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_xattrent_t * xattrs_tab,     /* IN/OUT */
                              unsigned int xattrs_tabsize,      /* IN */
                              unsigned int *p_nb_returned,      /* OUT */
                              int *end_of_list  /* OUT */
    )
{
  unsigned int index;
  unsigned int out_index;
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;
  int rc;
  creden_t cred;
  zfsfsal_handle_t *p_objecthandle = (zfsfsal_handle_t *)obj_handle;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !xattrs_tab || !p_nb_returned || !end_of_list)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_ListXAttrs);

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME | FSAL_ATTR_TYPE
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve unsuipported attributes */
  file_attrs.asked_attributes &= global_fs_info.supported_attrs;

  st = ZFSFSAL_getattrs(obj_handle, p_context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_ListXAttrs);

  /* Get the right VFS */
  ZFSFSAL_VFS_RDLock();
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(p_objecthandle);
  if(!p_vfs)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_ListXAttrs);
  }


  for(index = cookie, out_index = 0;
      index < XATTR_COUNT && out_index < xattrs_tabsize; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.type))
        {
          /* fills an xattr entry */
          xattrs_tab[out_index].xattr_id = index;
          FSAL_str2name(xattr_list[index].xattr_name, FSAL_MAX_NAME_LEN,
                        &xattrs_tab[out_index].xattr_name);
          xattrs_tab[out_index].xattr_cookie = index + 1;

          /* set asked attributes (all supported) */
          xattrs_tab[out_index].attributes.asked_attributes =
              global_fs_info.supported_attrs;

          if(file_attributes_to_xattr_attrs
             (&file_attrs, &xattrs_tab[out_index].attributes, index))
            {
              /* set error flag */
              xattrs_tab[out_index].attributes.asked_attributes = FSAL_ATTR_RDATTR_ERR;
            }

          /* next output slot */
          out_index++;
        }
    }

  /* Save a call if the output array is full */
  if(out_index == xattrs_tabsize)
  {
    *end_of_list = FALSE;
    *p_nb_returned = out_index;
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_ListXAttrs);
  }

  /* List the extended attributes */
  char *psz_buffer;
  size_t i_size;

  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();
  rc = libzfswrap_listxattr(p_vfs, &cred,
                            p_objecthandle->data.zfs_handle, &psz_buffer, &i_size);
  ReleaseTokenFSCall();
  ZFSFSAL_VFS_Unlock();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_ListXAttrs);

  if(i_size > 0)
  {
    size_t len = 0;
    char *ptr;
    int xattr_idx;

    for(ptr = psz_buffer, xattr_idx = 0;
        (ptr < psz_buffer + i_size) && (out_index < xattrs_tabsize);
        xattr_idx++, ptr += len + 1)
    {
      len = strlen(ptr);
      index = XATTR_COUNT + xattr_idx;

      /* Skip if the index is before the cookie */
      if(index < cookie)
        continue;

      xattrs_tab[out_index].xattr_id = index;
      FSAL_str2name(ptr, len + 1, &xattrs_tab[out_index].xattr_name);
      xattrs_tab[out_index].xattr_cookie = index + 1;

      /* set asked attributes (all supported) */
      xattrs_tab[out_index].attributes.asked_attributes =
                                global_fs_info.supported_attrs;

      if(file_attributes_to_xattr_attrs(&file_attrs,
                                        &xattrs_tab[out_index].attributes,
                                        index))
      {
        /* set error flag */
        xattrs_tab[out_index].attributes.asked_attributes = FSAL_ATTR_RDATTR_ERR;
      }

      /* next output slot */
      out_index++;
    }

    /* Every xattrs are in the output array */
    if(ptr >= psz_buffer + i_size)
      *end_of_list = TRUE;
    else
      *end_of_list = FALSE;
  }
  else
    *end_of_list = TRUE;
  free(psz_buffer);

  *p_nb_returned = out_index;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_ListXAttrs);

}

static int xattr_id_to_name(libzfswrap_vfs_t *p_vfs, zfsfsal_op_context_t *p_context, zfsfsal_handle_t *p_objecthandle, unsigned int xattr_id, char *psz_name)
{
  unsigned int index;
  unsigned int curr_idx;
  char *psz_buffer, *ptr;
  size_t i_size;
  size_t len;
  int rc;
  creden_t cred;

  if(xattr_id < XATTR_COUNT)
    return ERR_FSAL_INVAL;

  index = xattr_id - XATTR_COUNT;
  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  /* get xattrs */

  TakeTokenFSCall();
  rc = libzfswrap_listxattr(p_vfs, &cred,
                            p_objecthandle->data.zfs_handle, &psz_buffer, &i_size);
  ReleaseTokenFSCall();

  if(rc)
    return posix2fsal_error(rc);

  for(ptr = psz_buffer, curr_idx = 0; ptr < psz_buffer + i_size; curr_idx++, ptr += len + 1)
  {
      len = strlen(ptr);
      if(curr_idx == index)
      {
          strcpy(psz_name, ptr);
          free(psz_buffer);
          return ERR_FSAL_NO_ERROR;
      }
  }
  free(psz_buffer);
  return ERR_FSAL_NOENT;
}

/**
 *  return index if found,
 *  negative value on error.
 */
static int xattr_name_to_id(libzfswrap_vfs_t *p_vfs, zfsfsal_op_context_t *p_context, zfsfsal_handle_t *p_objecthandle, const char *psz_name, unsigned int *p_id)
{
  unsigned int i;
  char *psz_buffer, *ptr;
  size_t i_size;
  creden_t cred;

  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  /* get xattrs */
  TakeTokenFSCall();
  int rc = libzfswrap_listxattr(p_vfs, &cred,
                                p_objecthandle->data.zfs_handle, &psz_buffer, &i_size);
  ReleaseTokenFSCall();

  if(rc)
    return posix2fsal_error(rc);

  for(ptr = psz_buffer, i = 0; ptr < psz_buffer + i_size; i++, ptr += strlen(ptr) + 1)
  {
      if(!strcmp(psz_name, ptr))
      {
        *p_id = i + XATTR_COUNT;
        free(psz_buffer);
        return ERR_FSAL_NO_ERROR;
      }
  }
  free(psz_buffer);
  return ERR_FSAL_NOENT;
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
fsal_status_t ZFSFSAL_GetXAttrValueById(fsal_handle_t * obj_handle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * context,     /* IN */
                                     caddr_t buffer_addr,       /* IN/OUT */
                                     size_t buffer_size,        /* IN */
                                     size_t * p_output_size     /* OUT */
    )
{
  int rc;
  char buff[MAXNAMLEN];
  zfsfsal_handle_t *p_objecthandle = (zfsfsal_handle_t *)obj_handle;
  zfsfsal_op_context_t *p_context = (zfsfsal_op_context_t *)context;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* check that this index match the type of entry */
  if(xattr_id < XATTR_COUNT
     && !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->data.type))
  {
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrValue);
  }

  /* Get the right VFS */
  ZFSFSAL_VFS_RDLock();
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(p_objecthandle);
  if(!p_vfs)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);
  }

  if(xattr_id >= XATTR_COUNT)
  {
    char psz_attr_name[MAXPATHLEN];
    char *psz_value;
    creden_t cred;

    if((rc = xattr_id_to_name(p_vfs, p_context, p_objecthandle, xattr_id, psz_attr_name)))
    {
      ZFSFSAL_VFS_Unlock();
      Return(rc, errno, INDEX_FSAL_GetXAttrValue);
    }
    cred.uid = p_context->credential.user;
    cred.gid = p_context->credential.group;

    if((rc = libzfswrap_getxattr(p_vfs, &cred,
                                 p_objecthandle->data.zfs_handle, psz_attr_name, &psz_value)))
    {
      ZFSFSAL_VFS_Unlock();
      Return(posix2fsal_error(rc), 0, INDEX_FSAL_GetXAttrValue);
    }

    /* Copy the string (remove this call by changing the libzfswrap API) */
    strncpy(buffer_addr, psz_value, buffer_size);
    buffer_addr[buffer_size - 1] = '\0';
    *p_output_size = strlen(psz_value);
    free(psz_value);
  }
  else
  {
    rc = xattr_list[xattr_id].get_func(p_objecthandle, p_context,
                                       buffer_addr, buffer_size,
                                       p_output_size);
    /* Get the value */
    if(xattr_list[xattr_id].print_func == NULL)
      rc = xattr_list[xattr_id].get_func(p_objecthandle, p_context,
                                         buffer_addr, buffer_size, p_output_size);
    else
    {
      rc = xattr_list[xattr_id].get_func(p_objecthandle, p_context,
                                         buff, MAXNAMLEN, p_output_size);
      xattr_list[xattr_id].print_func(buff, MAXNAMLEN, buffer_addr, p_output_size);
    }
  }

  ZFSFSAL_VFS_Unlock();
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
fsal_status_t ZFSFSAL_GetXAttrIdByName(fsal_handle_t * obj_handle,     /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * context,      /* IN */
                                    unsigned int *pxattr_id     /* OUT */
    )
{
  unsigned int index;
  int rc;
  int found = FALSE;
  zfsfsal_handle_t *p_objecthandle = (zfsfsal_handle_t *)obj_handle;
  zfsfsal_op_context_t *p_context = (zfsfsal_op_context_t *)context;

  /* sanity checks */
  if(!p_objecthandle || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(!strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {
          found = TRUE;
          break;
        }
    }

  if(!found)
  {

    /* Get the right VFS */
    ZFSFSAL_VFS_RDLock();
    libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(p_objecthandle);
    if(!p_vfs)
    {
      ZFSFSAL_VFS_Unlock();
      Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_access);
    }

    if((rc = xattr_name_to_id(p_vfs, p_context, p_objecthandle, xattr_name->name, &index)))
    {
      ZFSFSAL_VFS_Unlock();
      Return(rc, 0, INDEX_FSAL_GetXAttrValue);
    }
    found = TRUE;
  }

  if(found)
  {
    *pxattr_id = index;
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
  }
  else
    Return(ERR_FSAL_NOENT, ENOENT, INDEX_FSAL_GetXAttrValue);
}

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
fsal_status_t ZFSFSAL_GetXAttrValueByName(fsal_handle_t * obj_handle,  /* IN */
                                          const fsal_name_t * xattr_name,  /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,     /* IN/OUT */
                                          size_t buffer_size,      /* IN */
                                          size_t * p_output_size   /* OUT */
    )
{
  unsigned int index;
  char *psz_value;
  int rc;
  creden_t cred;
  zfsfsal_handle_t *p_objecthandle = (zfsfsal_handle_t *)obj_handle;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* look for this name */
  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.type)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {

          return ZFSFSAL_GetXAttrValueById((fsal_handle_t *)p_objecthandle, index, p_context, buffer_addr,
                                           buffer_size, p_output_size);
        }
    }

  /* Get the right VFS */
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(p_objecthandle);
  if(!p_vfs)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);
  }
  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();
  if((rc = libzfswrap_getxattr(p_vfs, &cred,
                               p_objecthandle->data.zfs_handle, xattr_name->name, &psz_value)))
  {
    ZFSFSAL_VFS_Unlock();
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_GetXAttrValue);
  }
  ZFSFSAL_VFS_Unlock();

  /* Copy the string (remove this call by changing the libzfswrap API) */
  strncpy(buffer_addr, psz_value, buffer_size);
  buffer_addr[buffer_size - 1] = '\0';
  *p_output_size = strlen(psz_value);
  free(psz_value);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);

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

fsal_status_t ZFSFSAL_SetXAttrValue(fsal_handle_t * obj_handle,        /* IN */
                                 const fsal_name_t * xattr_name,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 caddr_t buffer_addr,   /* IN */
                                 size_t buffer_size,    /* IN */
                                 int create     /* IN */
    )
{
  //@TODO: use the create parameter ?
  int rc;
  creden_t cred;
  zfsfsal_handle_t * p_objecthandle = (zfsfsal_handle_t *)obj_handle;

  /* Hook to prevent any modification in the snapshots */
  if(p_objecthandle->data.i_snap != 0)
    Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_SetXAttrValue);

  /* Remove trailing '\n', if any */
  chomp_attr_value((char*)buffer_addr, buffer_size);
  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();
  rc = libzfswrap_setxattr(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs, &cred,
                           p_objecthandle->data.zfs_handle, xattr_name->name, (char*)buffer_addr);
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_SetXAttrValue);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_SetXAttrValue);
}

fsal_status_t ZFSFSAL_SetXAttrValueById(fsal_handle_t * obj_handle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * context,     /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size /* IN */
    )
{
  int rc;
  char psz_name[FSAL_MAX_NAME_LEN];
  fsal_name_t attr_name;
  zfsfsal_handle_t * p_objecthandle = (zfsfsal_handle_t *)obj_handle;
  zfsfsal_op_context_t *p_context = (zfsfsal_op_context_t *)context;

  /* Hook to prevent any modification in the snapshots */
  if(p_objecthandle->data.i_snap != 0)
    Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_SetXAttrValue);

  if(attr_is_read_only(xattr_id))
    Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
  else if(xattr_id < XATTR_COUNT)
    Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);

  if((rc = xattr_id_to_name(p_context->export_context->p_vfs, p_context, p_objecthandle,
                            xattr_id, psz_name)))
    Return(rc, 0, INDEX_FSAL_SetXAttrValue);

  FSAL_str2name(psz_name, FSAL_MAX_NAME_LEN, &attr_name);

  return ZFSFSAL_SetXAttrValue(obj_handle, &attr_name, context,
                               buffer_addr, buffer_size, FALSE);
}

/**
 *  Removes a xattr by Id
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_id xattr's id
 */
fsal_status_t ZFSFSAL_RemoveXAttrById(fsal_handle_t * obj_handle,      /* IN */
                                   fsal_op_context_t * context,       /* IN */
                                   unsigned int xattr_id)       /* IN */
{
  int rc;
  creden_t cred;
  char psz_name[FSAL_MAX_NAME_LEN];
  zfsfsal_handle_t * p_objecthandle = (zfsfsal_handle_t *)obj_handle;
  zfsfsal_op_context_t *p_context = (zfsfsal_op_context_t *)context;

  /* Hook to prevent any modification in the snapshots */
  if(p_objecthandle->data.i_snap != 0)
    Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_SetXAttrValue);

  if((rc = xattr_id_to_name(p_context->export_context->p_vfs, p_context, p_objecthandle,
                            xattr_id, psz_name)))
    Return(rc, 0, INDEX_FSAL_SetXAttrValue);

  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();
  rc = libzfswrap_removexattr(p_context->export_context->p_vfs, &cred,
                              p_objecthandle->data.zfs_handle, psz_name);
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_SetXAttrValue);
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_SetXAttrValue);
}

/**
 *  Removes a xattr by Name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_name xattr's name
 */
fsal_status_t ZFSFSAL_RemoveXAttrByName(fsal_handle_t * obj_handle,    /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     const fsal_name_t * xattr_name)    /* IN */
{
  int rc;
  creden_t cred;
  zfsfsal_handle_t * p_objecthandle = (zfsfsal_handle_t *)obj_handle;

  /* Hook to prevent any modification in the snapshots */
  if(p_objecthandle->data.i_snap != 0)
    Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_SetXAttrValue);
  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();
  rc = libzfswrap_removexattr(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs, &cred,
                              p_objecthandle->data.zfs_handle, xattr_name->name);
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_SetXAttrValue);
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_SetXAttrValue);
}
