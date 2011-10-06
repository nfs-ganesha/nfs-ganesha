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
#include "namespace.h"

#include <string.h>

/* Those definitions are only used for attributes emulated by FSAL.
 * For FUSE filesystems, we call FS primitives directly.
 */
#if 0

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200

/* function for getting an attribute value */

typedef int (*xattr_getfunc_t) (fusefsal_handle_t *,    /* object handle */
                                fusefsal_op_context_t *,        /* context */
                                caddr_t,        /* output buff */
                                size_t, /* output buff size */
                                size_t *);      /* output size */

typedef int (*xattr_setfunc_t) (fusefsal_handle_t *,    /* object handle */
                                fusefsal_op_context_t *,        /* context */
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

int get_void_attr(fusefsal_handle_t * p_objecthandle,   /* IN */
                  fusefsal_op_context_t * p_context,    /* IN */
                  caddr_t buffer_addr,  /* IN/OUT */
                  size_t buffer_size,   /* IN */
                  size_t * p_output_size)       /* OUT */
{
  if(!p_objecthandle || !p_context || !p_output_size)
    return ERR_FSAL_FAULT;

  snprintf((char *)buffer_addr, buffer_size, "Hello World !");

  *p_output_size = strlen((char *)buffer_addr) + 1;

  return 0;

}

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static fsal_xattr_def_t xattr_list[] = {
  {"hello_world", get_void_attr, NULL, NULL, XATTR_FOR_ALL | XATTR_RO}
};

#define XATTR_COUNT 1

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
#else

#define XATTR_COUNT 0
#endif

#if 0
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
      p_xattr_attrs->change = (uint64_t) file_attrs->chgtime.seconds;
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
#endif

/**
 * Get the attributes of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_cookie xattr's cookie (as returned by listxattrs).
 * \param p_attrs xattr's attributes.
 */
fsal_status_t FUSEFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                     fsal_op_context_t * p_context, /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_attrib_list_t * p_attrs
                                          /**< IN/OUT xattr attributes (if supported) */
    )
{
  int rc;
  char buff[MAXNAMLEN];
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_attrs)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrAttrs);

  /* @todo: to be implemented */

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_GetXAttrAttrs);
#if 0
  /* check that this index match the type of entry */
  if(xattr_id >= XATTR_COUNT
     || !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->object_type_reminder))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrAttrs);
    }

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_FILEID | FSAL_ATTR_OWNER
      | FSAL_ATTR_GROUP | FSAL_ATTR_ATIME | FSAL_ATTR_MTIME
      | FSAL_ATTR_CTIME | FSAL_ATTR_CREATION | FSAL_ATTR_CHGTIME | FSAL_ATTR_FSID;

  /* don't retrieve attributes not asked */

  file_attrs.asked_attributes &= p_attrs->asked_attributes;

  st = FSAL_getattrs(p_objecthandle, p_context, &file_attrs);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_GetXAttrAttrs);

  if((rc = file_attributes_to_xattr_attrs(&file_attrs, p_attrs, xattr_id)))
    {
      Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_GetXAttrAttrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrAttrs);
#endif

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
fsal_status_t FUSEFSAL_ListXAttrs(fsal_handle_t *obj_handle,   /* IN */
                                  unsigned int cookie,  /* IN */
                                  fsal_op_context_t * p_context,    /* IN */
                                  fsal_xattrent_t * xattrs_tab, /* IN/OUT */
                                  unsigned int xattrs_tabsize,  /* IN */
                                  unsigned int *p_nb_returned,  /* OUT */
                                  int *end_of_list      /* OUT */
    )
{
  int rc;
  unsigned int index;
  unsigned int out_index;
  char object_path[FSAL_MAX_PATH_LEN];
  fsal_status_t st;
  fsal_attrib_list_t file_attrs;
  fusefsal_handle_t * p_objecthandle = (fusefsal_handle_t *)obj_handle;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !xattrs_tab || !p_nb_returned || !end_of_list)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_ListXAttrs);

  /* get the full path for the object */
  rc = NamespacePath(p_objecthandle->data.inode, p_objecthandle->data.device,
                     p_objecthandle->data.validator, object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_ListXAttrs);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  /* @todo: to be implemented */

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_ListXAttrs);

#if 0

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
      if(do_match_type(xattr_list[index].flags, p_objecthandle->object_type_reminder))
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
#endif

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
fsal_status_t FUSEFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN/OUT */
                                         size_t buffer_size,    /* IN */
                                         size_t * p_output_size /* OUT */
    )
{
  int rc;
#if 0
  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* check that this index match the type of entry */
  if(xattr_id >= XATTR_COUNT
     || !do_match_type(xattr_list[xattr_id].flags, p_objecthandle->object_type_reminder))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrValue);
    }

  /* get the value */
  rc = xattr_list[xattr_id].get_func(p_objecthandle,
                                     p_context, buffer_addr, buffer_size, p_output_size);
#endif
  Return(rc, 0, INDEX_FSAL_GetXAttrValue);

}

/**
 * Get the index of an xattr based on its name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param pxattr_id found xattr_id
 *
 * \return ERR_FSAL_NO_ERROR if xattr_name exists, ERR_FSAL_NOENT otherwise
 */
fsal_status_t FUSEFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                        const fsal_name_t * xattr_name, /* IN */
                                        fsal_op_context_t * p_context,      /* IN */
                                        unsigned int *pxattr_id /* OUT */
    )
{
  unsigned int index;
  int found = FALSE;

#if 0
  /* sanity checks */
  if(!p_objecthandle || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->info.ftype)
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
#endif

  Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);
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
fsal_status_t FUSEFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                           const fsal_name_t * xattr_name,      /* IN */
                                           fsal_op_context_t * p_context,   /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size       /* OUT */
    )
{
  unsigned int index;

  /* sanity checks */
  if(!p_objecthandle || !p_context || !p_output_size || !buffer_addr || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  /* look for this name */
#if 0
  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, p_objecthandle->object_type_reminder)
         && !strcmp(xattr_list[index].xattr_name, xattr_name->name))
        {

          return FSAL_GetXAttrValueById(p_objecthandle, index, p_context, buffer_addr,
                                        buffer_size, p_output_size);

        }
    }
#endif
  /* not found */
  Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_GetXAttrValue);

}

fsal_status_t FUSEFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                     const fsal_name_t * xattr_name,    /* IN */
                                     fsal_op_context_t * p_context, /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size,        /* IN */
                                     int create /* IN */
    )
{
  Return(ERR_FSAL_PERM, 0, INDEX_FSAL_SetXAttrValue);
}

fsal_status_t FUSEFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size     /* IN */
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
fsal_status_t FUSEFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                       fsal_op_context_t * p_context,       /* IN */
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
fsal_status_t FUSEFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         const fsal_name_t * xattr_name)        /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_RemoveXAttrById */
