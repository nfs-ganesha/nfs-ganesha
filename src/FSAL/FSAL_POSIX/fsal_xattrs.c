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

#include <string.h>
#include <time.h>

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200

/* function for getting an attribute value */

typedef int (*xattr_getfunc_t) (posixfsal_handle_t *,   /* object handle */
                                posixfsal_op_context_t *,       /* context */
                                caddr_t,        /* output buff */
                                size_t, /* output buff size */
                                size_t *);      /* output size */

typedef int (*xattr_setfunc_t) (posixfsal_handle_t *,   /* object handle */
                                posixfsal_op_context_t *,       /* context */
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
 * DEFINE GET/SET FUNCTIONS
 */

int get_fsalid(posixfsal_handle_t * p_objecthandle,     /* IN */
               posixfsal_op_context_t * p_context,      /* IN */
               caddr_t buffer_addr,     /* IN/OUT */
               size_t buffer_size,      /* IN */
               size_t * p_output_size)  /* OUT */
{
  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* assuming buffer size is large enough for an int ! */

  memcpy(buffer_addr, &p_objecthandle->data.id, sizeof(p_objecthandle->data.id));
  *p_output_size = sizeof(p_objecthandle->data.id);

  return 0;

}

int print_fsalid(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  fsal_u64_t fsalid = 0LL;

  memcpy((char *)&fsalid, InBuff, sizeof(fsalid));

  *pOutSize = snprintf(OutBuff, *pOutSize, "%llu", fsalid);
  return 0;
}                               /* print_file_fsalid */

int get_timestamp(posixfsal_handle_t * p_objecthandle,  /* IN */
                  posixfsal_op_context_t * p_context,   /* IN */
                  caddr_t buffer_addr,  /* IN/OUT */
                  size_t buffer_size,   /* IN */
                  size_t * p_output_size)       /* OUT */
{
  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* assuming buffer size is large enough for an int ! */

  memcpy(buffer_addr, &p_objecthandle->data.ts, sizeof(p_objecthandle->data.ts));
  *p_output_size = sizeof(p_objecthandle->data.ts);

  return 0;

}

int print_timestamp(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  unsigned int date = 0;
  struct tm date_tm;

  memcpy((char *)&date, InBuff, sizeof(date));

  /* localtime_r( &date, &date_tm ) ;

   *pOutSize = strftime( OutBuff, *pOutSize, "%F %T", &date_tm ) ; */
  *pOutSize = snprintf(OutBuff, *pOutSize, "%u", date);

  return 0;
}                               /* print_file_cos */

int get_deviceid(posixfsal_handle_t * p_objecthandle,   /* IN */
                 posixfsal_op_context_t * p_context,    /* IN */
                 caddr_t buffer_addr,   /* IN/OUT */
                 size_t buffer_size,    /* IN */
                 size_t * p_output_size)        /* OUT */
{
  fsal_status_t status;
  fsal_path_t fsalpath;
  struct stat buffstat;

  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* this retrieves device and inode from database */

  status =
      fsal_internal_getPathFromHandle(p_context, p_objecthandle, 0, &fsalpath, &buffstat);

  if(FSAL_IS_ERROR(status))
    return status.major;

  /* assuming buffer size is large enough for an int ! */

  memcpy(buffer_addr, &p_objecthandle->data.info.devid, sizeof(dev_t));
  *p_output_size = sizeof(dev_t);

  return 0;

}

int print_deviceid(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  unsigned long devid = 0LL;

  memcpy((char *)&devid, InBuff, sizeof(devid));

  *pOutSize = snprintf(OutBuff, *pOutSize, "%lu", devid);
  return 0;
}                               /* print_file_devid */

int get_inode(posixfsal_handle_t * p_objecthandle,      /* IN */
              posixfsal_op_context_t * p_context,       /* IN */
              caddr_t buffer_addr,      /* IN/OUT */
              size_t buffer_size,       /* IN */
              size_t * p_output_size)   /* OUT */
{
  fsal_status_t status;
  fsal_path_t fsalpath;
  struct stat buffstat;

  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  /* this retrieves device and inode from database */

  status =
      fsal_internal_getPathFromHandle(p_context, p_objecthandle, 0, &fsalpath, &buffstat);

  if(FSAL_IS_ERROR(status))
    return status.major;

  /* assuming buffer size is large enough for an int ! */

  memcpy(buffer_addr, &p_objecthandle->data.info.inode, sizeof(ino_t));
  *p_output_size = sizeof(ino_t);

  return 0;
}

int print_inode(caddr_t InBuff, size_t InSize, caddr_t OutBuff, size_t * pOutSize)
{
  fsal_u64_t inode = 0LL;

  memcpy((char *)&inode, InBuff, sizeof(inode));

  *pOutSize = snprintf(OutBuff, *pOutSize, "%llu", inode);
  return 0;
}                               /* print_file_inode */

int get_objtype(posixfsal_handle_t * p_objecthandle,    /* IN */
                posixfsal_op_context_t * p_context,     /* IN */
                caddr_t buffer_addr,    /* IN/OUT */
                size_t buffer_size,     /* IN */
                size_t * p_output_size) /* OUT */
{
  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  switch (p_objecthandle->data.info.ftype)
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

int get_path(posixfsal_handle_t * p_objecthandle,       /* IN */
             posixfsal_op_context_t * p_context,        /* IN */
             caddr_t buffer_addr,       /* IN/OUT */
             size_t buffer_size,        /* IN */
             size_t * p_output_size)    /* OUT */
{
  fsal_status_t status;
  fsal_path_t fsalpath;
  struct stat buffstat;

  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  status =
      fsal_internal_getPathFromHandle(p_context, p_objecthandle, 0, &fsalpath, &buffstat);

  if(FSAL_IS_ERROR(status))
    return status.major;

  strncpy(buffer_addr, fsalpath.path, buffer_size);
  *p_output_size = strlen((char *)buffer_addr) + 1;

  return 0;

}

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static fsal_xattr_def_t xattr_list[] = {
  {"device_id", get_deviceid, NULL, print_deviceid, XATTR_FOR_ALL | XATTR_RO},
  {"inode", get_inode, NULL, print_inode, XATTR_FOR_ALL | XATTR_RO},
  {"path", get_path, NULL, NULL, XATTR_FOR_ALL | XATTR_RO},
  {"fsal_object_id", get_fsalid, NULL, print_fsalid, XATTR_FOR_ALL | XATTR_RO},
  {"timestamp", get_timestamp, NULL, print_timestamp, XATTR_FOR_ALL | XATTR_RO},
  {"type", get_objtype, NULL, NULL, XATTR_FOR_ALL | XATTR_RO}
};

#define XATTR_COUNT 6

/* we assume that this number is < 254 */
#if ( XATTR_COUNT > 254 )
#error "ERROR: xattr count > 254"
#endif

/* YOUR SHOULD NOT HAVE TO MODIFY THE FOLLOWING FUNCTIONS */

/* test if an object has a given attribute */
int do_match_type(int xattr_flag, fsal_nodetype_t obj_type)
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
      if(xattr_list[attr_index].flags & XATTR_RO)
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
      if(xattr_list[attr_index].flags & XATTR_RO)
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
fsal_status_t POSIXFSAL_GetXAttrAttrs(fsal_handle_t * objecthandle,      /* IN */
                                      fsal_op_context_t * context,       /* IN */
                                      unsigned int xattr_id,    /* IN */
                                      fsal_attrib_list_t * p_attrs
                                          /**< IN/OUT xattr attributes (if supported) */
    )
{
  posixfsal_handle_t * p_objecthandle = (posixfsal_handle_t *) objecthandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  int rc;
  char buff[MAXNAMLEN];
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_attrs)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrAttrs);

  /* check that this index match the type of entry */
  if(xattr_id >= XATTR_COUNT
     || !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->data.info.ftype))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrAttrs);
    }

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve attributes not asked */

  file_attrs.asked_attributes &= p_attrs->asked_attributes;

  st = POSIXFSAL_getattrs(objecthandle, context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_GetXAttrAttrs);

  if((rc = file_attributes_to_xattr_attrs(&file_attrs, p_attrs, xattr_id)))
    {
      Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_GetXAttrAttrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrAttrs);

}                               /* FSAL_GetXAttrAttrs */

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
fsal_status_t POSIXFSAL_ListXAttrs(fsal_handle_t * objecthandle, /* IN */
                                   unsigned int cookie, /* IN */
                                   fsal_op_context_t * context,  /* IN */
                                   fsal_xattrent_t * xattrs_tab,        /* IN/OUT */
                                   unsigned int xattrs_tabsize, /* IN */
                                   unsigned int *p_nb_returned, /* OUT */
                                   int *end_of_list     /* OUT */
    )
{
  posixfsal_handle_t * p_objecthandle = (posixfsal_handle_t *) objecthandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  unsigned int index;
  unsigned int out_index;
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !xattrs_tab || !p_nb_returned || !end_of_list)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_ListXAttrs);

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve unsuipported attributes */
  file_attrs.asked_attributes &= global_fs_info.supported_attrs;

  st = POSIXFSAL_getattrs(objecthandle, context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_ListXAttrs);

  for(index = cookie, out_index = 0;
      index < XATTR_COUNT && out_index < xattrs_tabsize; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.info.ftype))
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

  *p_nb_returned = out_index;
  *end_of_list = (index == XATTR_COUNT);

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
fsal_status_t POSIXFSAL_GetXAttrValueById(fsal_handle_t * objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * context,   /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size        /* OUT */
    )
{
  posixfsal_handle_t * p_objecthandle = (posixfsal_handle_t *) objecthandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  int rc;
  char buff[MAXNAMLEN];

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* check that this index match the type of entry */
  if(xattr_id >= XATTR_COUNT
     || !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->data.info.ftype))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrValue);
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
 *  * Get the index of an xattr based on its name
 *   *
 *   \param p_objecthandle Handle of the object you want to get attribute for.
 *   \param xattr_name the name of the attribute to be read.
 *   \param pxattr_id found xattr_id
 *   
 *   \return ERR_FSAL_NO_ERROR if xattr_name exists, ERR_FSAL_NOENT otherwise
 */

fsal_status_t POSIXFSAL_GetXAttrIdByName(fsal_handle_t * objecthandle,   /* IN */
                                         const fsal_name_t * xattr_name, /* IN */
                                         fsal_op_context_t * context,    /* IN */
                                         unsigned int *pxattr_id        /* OUT */
    )
{
  posixfsal_handle_t * p_objecthandle = (posixfsal_handle_t *) objecthandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  unsigned int index;
  int found = FALSE;

  /* sanity checks */
  if(!p_objecthandle || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.info.ftype)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {
          found = TRUE;
          break;
        }
    }

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
fsal_status_t POSIXFSAL_GetXAttrValueByName(fsal_handle_t * objecthandle,   /* IN */
                                            const fsal_name_t * xattr_name, /* IN */
                                            fsal_op_context_t * context, /* IN */
                                            caddr_t buffer_addr,        /* IN/OUT */
                                            size_t buffer_size, /* IN */
                                            size_t * p_output_size      /* OUT */
    )
{
  posixfsal_handle_t * p_objecthandle = (posixfsal_handle_t *) objecthandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  unsigned int index;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* look for this name */

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->data.info.ftype)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {

          return POSIXFSAL_GetXAttrValueById(objecthandle, index, context,
                                             buffer_addr, buffer_size, p_output_size);

        }
    }

  /* not found */
  Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);

}

fsal_status_t POSIXFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,      /* IN */
                                      const fsal_name_t * xattr_name,   /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      caddr_t buffer_addr,      /* IN */
                                      size_t buffer_size,       /* IN */
                                      int create        /* IN */
    )
{
  Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
}

fsal_status_t POSIXFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,  /* IN */
                                          size_t buffer_size    /* IN */
    )
{
  Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
}

/**
 *  Removes a xattr by Id
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_id xattr's id
 */
fsal_status_t POSIXFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,    /* IN */
                                        fsal_op_context_t * p_context,     /* IN */
                                        unsigned int xattr_id)  /* IN */
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
fsal_status_t POSIXFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,  /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          const fsal_name_t * xattr_name)       /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_RemoveXAttrByName */
