/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* xattrs.c
 * VFS object (file|dir) handle object extended attributes
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <fsal_handle_syscalls.h>
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <sys/syscall.h>
#include <ctype.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include <FSAL/FSAL_LUSTRE/fsal_handle.h>
#include "lustre_methods.h"
#include <stdbool.h>


#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>

typedef int (*xattr_getfunc_t) (struct fsal_obj_handle *,   /* object handle */
                                caddr_t,                    /* output buff */
                                size_t,                     /* output buff size */
                                size_t *,                   /* output size */
                                void *arg);                 /* optionnal argument */

typedef int (*xattr_setfunc_t) (struct fsal_obj_handle *,   /* object handle */
                                caddr_t,                    /* input buff */
                                size_t,                     /* input size */
                                int,                        /* creation flag */
                                void *arg);                 /* optionnal argument */

typedef struct fsal_xattr_def__
{
  char xattr_name[MAXNAMLEN];
  xattr_getfunc_t get_func;
  xattr_setfunc_t set_func;
  int flags;
  void *arg;
} fsal_xattr_def_t;


/*
 * DEFINE GET/SET FUNCTIONS
 */

int print_vfshandle(struct fsal_obj_handle *obj_hdl,
                    caddr_t buffer_addr,      
                    size_t buffer_size, 
                    size_t * p_output_size,   
                    void *arg)
{
  *p_output_size = snprintf( buffer_addr, buffer_size, "(not yet implemented)" ) ;

  return 0;
}                               /* print_fid */

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static fsal_xattr_def_t xattr_list[] = {
  {"vfshandle", print_vfshandle, NULL, XATTR_FOR_ALL | XATTR_RO, NULL},
};

#define XATTR_COUNT 1

/* we assume that this number is < 254 */
#if ( XATTR_COUNT > 254 )
#error "ERROR: xattr count > 254"
#endif
/* test if an object has a given attribute */
static int do_match_type(int xattr_flag, object_file_type_t obj_type)
{
  switch (obj_type)
    {
    case REGULAR_FILE:
      return ((xattr_flag & XATTR_FOR_FILE) == XATTR_FOR_FILE);

    case DIRECTORY:
      return ((xattr_flag & XATTR_FOR_DIR) == XATTR_FOR_DIR);

    case SYMBOLIC_LINK:
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

static int file_attributes_to_xattr_attrs( struct attrlist * file_attrs,
                                           struct attrlist * xattr_attrs,
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
  attrmask_t supported = ATTR_SUPPATTR | ATTR_MODE | ATTR_FILEID
      | ATTR_TYPE | ATTR_OWNER | ATTR_GROUP
      | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME
      | ATTR_CREATION | ATTR_CHGTIME | ATTR_SIZE
      | ATTR_SPACEUSED | ATTR_NUMLINKS | ATTR_RAWDEV | ATTR_FSID;
  attrmask_t unsupp;

  if(xattr_attrs->mask == 0)
    {
      xattr_attrs->mask = supported;

      LogCrit(COMPONENT_FSAL,
                        "Error: xattr_attrs->mask was 0 in %s() line %d, file %s",
                        __FUNCTION__, __LINE__, __FILE__);
    }

  unsupp = xattr_attrs->mask & (~supported);

  if(unsupp)
    {
      LogDebug(COMPONENT_FSAL,
               "Asking for unsupported attributes in %s(): %#llX removing it from asked attributes",
               __FUNCTION__, (long long unsigned int) unsupp);

      xattr_attrs->mask &= (~unsupp);
    }

  if(xattr_attrs->mask & ATTR_SUPPATTR)
    xattr_attrs->supported_attributes = supported;

  if(xattr_attrs->mask & ATTR_MODE)
    {
      xattr_attrs->mode = file_attrs->mode ;

      if(attr_is_read_only(attr_index))
        xattr_attrs->mode &= ~(0222);
    }

  if(xattr_attrs->mask & ATTR_FILEID)
    {
      unsigned int i;
      unsigned long hash = attr_index + 1;
      char *str = (char *)&file_attrs->fileid;

      for(i = 0; i < sizeof(xattr_attrs->fileid); i++, str++)
        {
          hash = (hash << 5) - hash + (unsigned long)(*str);
        }
      xattr_attrs->fileid = hash;
    }

  if(xattr_attrs->mask & ATTR_TYPE)
    xattr_attrs->type = EXTENDED_ATTR;

  if(xattr_attrs->mask & ATTR_OWNER)
    xattr_attrs->owner = file_attrs->owner;

  if(xattr_attrs->mask & ATTR_GROUP)
    xattr_attrs->group = file_attrs->group;

  if(xattr_attrs->mask & ATTR_ATIME)
    xattr_attrs->atime = file_attrs->atime;

  if(xattr_attrs->mask & ATTR_MTIME)
    xattr_attrs->mtime = file_attrs->mtime;

  if(xattr_attrs->mask & ATTR_CTIME)
    xattr_attrs->ctime = file_attrs->ctime;

  if(xattr_attrs->mask & ATTR_CREATION)
    xattr_attrs->creation = file_attrs->creation;

  if(xattr_attrs->mask & ATTR_CHGTIME)
    {
      xattr_attrs->chgtime = file_attrs->chgtime;
      xattr_attrs->change = (uint64_t) xattr_attrs->chgtime.seconds;
    }

  if(xattr_attrs->mask & ATTR_SIZE)
    xattr_attrs->filesize = DEV_BSIZE;

  if(xattr_attrs->mask & ATTR_SPACEUSED)
    xattr_attrs->spaceused = DEV_BSIZE;

  if(xattr_attrs->mask & ATTR_NUMLINKS)
    xattr_attrs->numlinks = 1;

  if(xattr_attrs->mask & ATTR_RAWDEV)
    {
      xattr_attrs->rawdev.major = 0;
      xattr_attrs->rawdev.minor = 0;
    }

  if(xattr_attrs->mask & ATTR_FSID)
    {
      xattr_attrs->fsid = file_attrs->fsid;
    }

  /* if mode==0, then owner is set to root and mode is set to 0600 */
  if((xattr_attrs->mask & ATTR_OWNER)
     && (xattr_attrs->mask & ATTR_MODE) && (xattr_attrs->mode == 0))
    {
      xattr_attrs->owner = 0;
      xattr_attrs->mode = 0600;
      if(attr_is_read_only(attr_index))
        xattr_attrs->mode &= ~(0200);
    }

  return 0;

}

static int xattr_id_to_name(char *lustre_path, unsigned int xattr_id, char *name)
{
  unsigned int index;
  unsigned int curr_idx;
  char names[MAXPATHLEN], *ptr;
  size_t namesize;
  size_t len = 0;

  if(xattr_id < XATTR_COUNT)
    return ERR_FSAL_INVAL;

  index = xattr_id - XATTR_COUNT;

  /* get xattrs */

  namesize = llistxattr(lustre_path, names, sizeof(names));

  if(namesize < 0)
    return ERR_FSAL_NOENT;

  errno = 0;

  for(ptr = names, curr_idx = 0; ptr < names + namesize; curr_idx++, ptr += len + 1)
    {
      len = strlen(ptr);
      if(curr_idx == index)
        {
          strcpy(name, ptr);
          return ERR_FSAL_NO_ERROR;
        }
    }
  return ERR_FSAL_NOENT;
}


/**
 *  return index if found,
 *  negative value on error.
 */
/**
 *  return index if found,
 *  negative value on error.
 */
static int xattr_name_to_id(char *lustre_path, const char *name)
{
  unsigned int i;
  char names[MAXPATHLEN], *ptr;
  size_t namesize;

  /* get xattrs */

  namesize = llistxattr(lustre_path, names, sizeof(names));

  if(namesize < 0)
    return -ERR_FSAL_NOENT;

  for(ptr = names, i = 0; ptr < names + namesize; i++, ptr += strlen(ptr) + 1)
    {
      if(!strcmp(name, ptr))
        return i + XATTR_COUNT;
    }
  return -ERR_FSAL_NOENT;
}

static int xattr_format_value(caddr_t buffer, size_t * datalen, size_t maxlen)
{
  size_t size_in = *datalen;
  size_t len = strnlen((char *)buffer, size_in);
  int i;

  if(len == size_in - 1 || len == size_in)
    {
      int ascii = TRUE;
      char *str = buffer;
      int i;

      for(i = 0; i < len; i++)
        {
          if(!isprint(str[i]) && !isspace(str[i]))
            {
              ascii = FALSE;
              break;
            }
        }

      if(ascii)
        {
          *datalen = size_in;
          /* add additional '\n', if missing */
          if((size_in + 1 < maxlen) && (str[len - 1] != '\n'))
            {
              str[len] = '\n';
              str[len + 1] = '\0';
              (*datalen) += 2;
            }
          return ERR_FSAL_NO_ERROR;
        }
    }

  /* byte, word, 32 or 64 bits */
  if(size_in == 1)
    {
      unsigned char val = *((unsigned char *)buffer);
      *datalen = 1 + snprintf((char *)buffer, maxlen, "%hhu\n", val);
      return ERR_FSAL_NO_ERROR;
    }
  else if(size_in == 2)
    {
      unsigned short val = *((unsigned short *)buffer);
      *datalen = 1 + snprintf((char *)buffer, maxlen, "%hu\n", val);
      return ERR_FSAL_NO_ERROR;
    }
  else if(size_in == 4)
    {
      unsigned int val = *((unsigned int *)buffer);
      *datalen = 1 + snprintf((char *)buffer, maxlen, "%u\n", val);
      return ERR_FSAL_NO_ERROR;
    }
  else if(size_in == 8)
    {
      unsigned long long val = *((unsigned long long *)buffer);
      *datalen = 1 + snprintf((char *)buffer, maxlen, "%llu\n", val);
      return ERR_FSAL_NO_ERROR;
    }
  else
    {
      /* 2 bytes per initial byte +'0x' +\n +\0 */
      char *curr_out;
      char *tmp_buf = (char *)gsh_malloc(3 * size_in + 4);
      if(!tmp_buf)
        return ERR_FSAL_NOMEM;
      curr_out = tmp_buf;
      curr_out += sprintf(curr_out, "0x");
      /* hexa representation */
      for(i = 0; i < size_in; i++)
        {
          unsigned char *p8 = (unsigned char *)(buffer + i);
          if((i % 4 == 3) && (i != size_in - 1))
            curr_out += sprintf(curr_out, "%02hhX.", *p8);
          else
            curr_out += sprintf(curr_out, "%02hhX", *p8);
        }
      *curr_out = '\n';
      curr_out++;
      *curr_out = '\0';
      curr_out++;
      strncpy((char *)buffer, tmp_buf, maxlen);
      *datalen = strlen(tmp_buf) + 1;
      if(*datalen > maxlen)
        *datalen = maxlen;
      gsh_free(tmp_buf);
      return ERR_FSAL_NO_ERROR;
    }
}



fsal_status_t lustre_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 unsigned int argcookie,
				 fsal_xattrent_t * xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned,
				 int *end_of_list)
{
  unsigned int index;
  unsigned int out_index;
  unsigned int cookie = argcookie ;
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  char mypath[MAXPATHLEN] ;

  char names[MAXPATHLEN], *ptr;
  size_t namesize;
  int xattr_idx;

  /* sanity checks */
  if(!obj_hdl || !xattrs_tab || !p_nb_returned || !end_of_list)
    return fsalstat( ERR_FSAL_FAULT, 0 ) ;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);
  

  /* Deal with special cookie */
  if( cookie == XATTR_RW_COOKIE ) cookie = XATTR_COUNT ;

  for(index = cookie, out_index = 0;
      index < XATTR_COUNT && out_index < xattrs_tabsize; index++)
    {
      if(do_match_type(xattr_list[index].flags, obj_hdl->attributes.type))
        {
          /* fills an xattr entry */
          xattrs_tab[out_index].xattr_id = index;
          strncpy(xattr_list[index].xattr_name, 
                  xattrs_tab[out_index].xattr_name, MAXNAMLEN);
          xattrs_tab[out_index].xattr_cookie = index + 1;

          /* set asked attributes (all supported) */
          xattrs_tab[out_index].attributes.mask = obj_hdl->attributes.mask ;

          if(file_attributes_to_xattr_attrs(&obj_hdl->attributes,
                                            &xattrs_tab[out_index].attributes, 
                                            index))
            {
              /* set error flag */
              xattrs_tab[out_index].attributes.mask = ATTR_RDATTR_ERR;
            }

          /* next output slot */
          out_index++;
        }
    }

  /* save a call if output array is full */
  if(out_index == xattrs_tabsize)
    {
      *end_of_list = FALSE;
      *p_nb_returned = out_index;
      return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
    }

  /* get the path of the file in Lustre */
  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;

  /* get xattrs */
  namesize = llistxattr(mypath, names, sizeof(names));

  if(namesize >= 0)
    {
      size_t len = 0;

      errno = 0;

      for(ptr = names, xattr_idx = 0;
          (ptr < names + namesize) && (out_index < xattrs_tabsize);
          xattr_idx++, ptr += len + 1)
        {
          len = strlen(ptr);
          index = XATTR_COUNT + xattr_idx;

          /* skip if index is before cookie */
          if(index < cookie)
            continue;

          /* fills an xattr entry */
          xattrs_tab[out_index].xattr_id = index;
          strncpy( xattrs_tab[out_index].xattr_name, ptr, len+1 ) ;
          xattrs_tab[out_index].xattr_cookie = index + 1;

          /* set asked attributes (all supported) */
          xattrs_tab[out_index].attributes.mask = obj_hdl->attributes.mask ;

          if(file_attributes_to_xattr_attrs(&obj_hdl->attributes,
                                            &xattrs_tab[out_index].attributes, index))
            {
              /* set error flag */
              xattrs_tab[out_index].attributes.mask = ATTR_RDATTR_ERR;
            }

          /* next output slot */
          out_index++;
        }
      /* all xattrs are in the output array */
      if(ptr >= names + namesize)
        *end_of_list = TRUE;
      else
        *end_of_list = FALSE;
    }
  else                          /* no xattrs */
    *end_of_list = TRUE;

  *p_nb_returned = out_index;

  return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
}

fsal_status_t lustre_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const char *xattr_name,
					unsigned int *pxattr_id)
{
  unsigned int index;
  int rc;
  int found = FALSE;
  char mypath[MAXPATHLEN] ;
  struct lustre_fsal_obj_handle * obj_handle = NULL ;

  /* sanity checks */
  if(!obj_hdl || !xattr_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(!strcmp(xattr_list[index].xattr_name, xattr_name))
        {
          found = TRUE;
          break;
        }
    }

  /* search in xattrs */
  if(!found)
    {
      lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;
      
      errno = 0;
      rc = xattr_name_to_id(mypath, xattr_name);
      if(rc < 0)
        {
          return fsalstat(-rc, errno ) ;
        }
      else
        {
          index = rc;
          found = TRUE;
        }
    }

  if(found)
    {
      *pxattr_id = index;
      return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
    }
  else
    return fsalstat(ERR_FSAL_NOENT, ENOENT ) ;
}

fsal_status_t lustre_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size)
{
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  int rc = 0 ;
  char mypath[MAXPATHLEN] ;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  /* sanity checks */
  if(!obj_hdl || !p_output_size || !buffer_addr)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* check that this index match the type of entry */
  if((xattr_id < XATTR_COUNT)
     && !do_match_type(xattr_list[xattr_id].flags, obj_hdl->attributes.type))
    {
      return fsalstat(ERR_FSAL_INVAL, 0 ) ;
    }
  else if(xattr_id >= XATTR_COUNT)
    {
      char attr_name[MAXPATHLEN];
 
      /* get the name for this attr */
      lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;
      rc = xattr_id_to_name(mypath, xattr_id, attr_name);
      if(rc)
        {
          return fsalstat(rc, errno ) ;
        }

      rc = lgetxattr( mypath, attr_name, buffer_addr, buffer_size);
      if(rc < 0)
        {
          return fsalstat(posix2fsal_error(errno), errno ) ;
        }

      /* the xattr value can be a binary, or a string.
       * trying to determine its type...
       */
      *p_output_size = rc;
      xattr_format_value(buffer_addr, p_output_size, buffer_size);

      return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
    }
  else                          /* built-in attr */
    {
      /* get the value */
      rc = xattr_list[xattr_id].get_func(obj_hdl,
                                         buffer_addr, buffer_size,
                                         p_output_size, xattr_list[xattr_id].arg);
      return fsalstat(rc, 0 ) ;
    }


  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


fsal_status_t lustre_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size,
					   size_t * p_output_size)
{
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  int rc = 0 ;
  char mypath[MAXPATHLEN] ;
  unsigned int index;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  /* sanity checks */
  if(!obj_hdl || !p_output_size || !buffer_addr || !xattr_name)
    return fsalstat(ERR_FSAL_FAULT, 0 ) ;

  /* look for this name */
  for(index = 0; index < XATTR_COUNT; index++)
    {
      if(do_match_type(xattr_list[index].flags, obj_hdl->attributes.type)
         && !strcmp(xattr_list[index].xattr_name, xattr_name))
        {
          return lustre_getextattr_value_by_id( obj_hdl, index, buffer_addr, buffer_size, p_output_size ) ;
        }
    }

  /* is it an xattr? */
  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;
  rc = lgetxattr(mypath, xattr_name, buffer_addr, buffer_size);
  if(rc < 0)
    {
      return fsalstat(posix2fsal_error(errno), errno ) ;
    }
  /* the xattr value can be a binary, or a string.
   * trying to determine its type...
   */
  *p_output_size = rc;
  xattr_format_value(buffer_addr, p_output_size, buffer_size);

  return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
}

fsal_status_t lustre_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const char *xattr_name,
				   caddr_t buffer_addr,
				   size_t buffer_size,
				   int create)
{
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  char mypath[MAXPATHLEN] ;
  int rc = 0 ;
  size_t len;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  /* remove final '\n', if any */
  chomp_attr_value((char *)buffer_addr, buffer_size);

  len = strnlen((char *)buffer_addr, buffer_size);

  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;
  if(len == 0)
    rc = lsetxattr(mypath, xattr_name, "", 1, create ? XATTR_CREATE : XATTR_REPLACE);
  else
    rc = lsetxattr(mypath, xattr_name, (char *)buffer_addr,
                   len, create ? XATTR_CREATE : XATTR_REPLACE);


  if(rc != 0)
    return fsalstat(posix2fsal_error(errno), errno ) ;
  else
    return fsalstat(ERR_FSAL_NO_ERROR, 0  ) ;

}

fsal_status_t lustre_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size)
{
  char name[MAXNAMLEN];
  char mypath[MAXPATHLEN] ;
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  int rc = 0 ;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  if(attr_is_read_only(xattr_id))
    return fsalstat(ERR_FSAL_PERM, 0 ) ;
  else if(xattr_id < XATTR_COUNT)
    return fsalstat(ERR_FSAL_PERM, 0 ) ;

  /* build fid path in lustre */
  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;

  rc = xattr_id_to_name(mypath, xattr_id, name);
  if(rc)
    return fsalstat(rc, errno ) ;

  return lustre_setextattr_value( obj_hdl, name, buffer_addr, buffer_size, FALSE);
}

fsal_status_t lustre_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				   unsigned int xattr_id,
                                   struct attrlist *p_attrs)
{
  int rc;

  /* sanity checks */
  if(!obj_hdl || !p_attrs)
    return fsalstat(ERR_FSAL_FAULT, 0 ) ;


  /* check that this index match the type of entry */
  if(xattr_id < XATTR_COUNT
     && !do_match_type(xattr_list[xattr_id].flags, obj_hdl->attributes.type))
    {
      return fsalstat(ERR_FSAL_INVAL, 0 ) ;
    }
  else if(xattr_id >= XATTR_COUNT)
    {
      /* This is user defined xattr */
      LogFullDebug(COMPONENT_FSAL,
                        "Getting attributes for xattr #%u", xattr_id - XATTR_COUNT);
    }

  if((rc = file_attributes_to_xattr_attrs(&obj_hdl->attributes, p_attrs, xattr_id)))
    {
      return fsalstat(ERR_FSAL_INVAL, rc ) ;
    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0 ) ;
}

fsal_status_t lustre_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       unsigned int xattr_id)
{
  int rc;
  char name[MAXNAMLEN];
  char mypath[MAXPATHLEN];
  struct lustre_fsal_obj_handle * obj_handle = NULL ;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;
  rc = xattr_id_to_name(mypath, xattr_id, name);
  if(rc)
   {
     return fsalstat(rc, errno ) ;
   }
  rc = lremovexattr(mypath, name);

  if(rc != 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t lustre_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name)
{
  struct lustre_fsal_obj_handle * obj_handle = NULL ;
  int rc = 0 ;
  char mypath[MAXPATHLEN] ;

  obj_handle = container_of( obj_hdl, struct lustre_fsal_obj_handle, obj_handle);

  lustre_handle_to_path( lustre_get_root_path( obj_hdl->export ), obj_handle->handle, mypath ) ;

  rc = lremovexattr(mypath, xattr_name);

  if(rc != 0)
    return fsalstat(posix2fsal_error(errno), errno);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

