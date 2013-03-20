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

/* handle.c
 * VFS object (file|dir) handle object
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <mntent.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "hpss_methods.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include <stdbool.h>


/* helpers
 */

/* hpss_alloc_handle
 * allocate and fill in a handle
 * this uses malloc/free for the time being.
 */

static struct hpss_fsal_obj_handle *hpss_alloc_handle(struct hpss_file_handle *fh,
                                                struct attrlist *attr,
                                                char *link_content,
                                                struct fsal_export *exp_hdl)
{
  struct hpss_fsal_obj_handle *hdl;

  hdl = malloc(sizeof(struct hpss_fsal_obj_handle) +
		     sizeof(struct hpss_file_handle) ) ;
  if( hdl == NULL || attr == NULL)
    return NULL;
  memset(hdl, 0, (sizeof(struct hpss_fsal_obj_handle) +
                  sizeof(struct hpss_file_handle) ) ) ;
  hdl->handle = (struct hpss_file_handle *)&hdl[1];
  if( fh != NULL )
    memcpy(hdl->handle, fh,  sizeof(struct hpss_file_handle) );

  hdl->obj_handle.type = attr->type;

  if( (hdl->obj_handle.type == SYMBOLIC_LINK) &&
      (link_content != NULL)  )
    {
      size_t len = strlen(link_content) + 1;

      hdl->u.symlink.link_content = gsh_malloc(len);
      if(hdl->u.symlink.link_content == NULL) {
        goto spcerr;
      }

      memcpy(hdl->u.symlink.link_content, link_content, len);
      hdl->u.symlink.link_size = len;
    }
  hdl->obj_handle.export = exp_hdl;
  hdl->obj_handle.attributes.mask = exp_hdl->ops->fs_supported_attrs(exp_hdl);

  memcpy(&hdl->obj_handle.attributes, attr, sizeof(struct attrlist));
  

  if(!fsal_obj_handle_init(&hdl->obj_handle,
                           exp_hdl,
                           attr->type)) ;
  return hdl;

spcerr:
  hdl->obj_handle.ops = NULL;
  pthread_mutex_unlock(&hdl->obj_handle.lock);
  pthread_mutex_destroy(&hdl->obj_handle.lock);
  if(hdl->obj_handle.type == SYMBOLIC_LINK) {
    if(hdl->u.symlink.link_content != NULL)
      gsh_free(hdl->u.symlink.link_content);
  }
  gsh_free(hdl);  /* elvis has left the building */
  return NULL;
}

/* handle methods
 */


/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_filename does not exist)
 *        - ERR_FSAL_XDEV         (tried to operate a lookup on a filesystem junction.
 *                                 Use FSAL_lookupJunction instead)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */

static fsal_status_t hpss_lookup(struct fsal_obj_handle *parent,      /* IN */
                                 const struct req_op_context *opctx,  /* IN */
                                 char *path,                    /* IN */
                                 struct fsal_obj_handle **handle      /* OUT */
    )
{

  int rc;
  fsal_status_t status;
  ns_ObjHandle_t obj_hdl;
  struct attrlist fsal_attr;
  hpss_Attrs_t hpss_attr;
  struct hpss_fsal_obj_handle *hdl, *parent_obj_handle;
  sec_cred_t ucreds;

  *handle = NULL; /* poison it */

  /* sanity checks
   * note : parent and path can no longer be NULL to get root handle
   */
  if(!parent || !path || !handle || !opctx)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* Check this is a directory, nor a junction nor a symlink */
  if( !parent->ops->handle_is(parent,DIRECTORY) )
      return fsalstat(ERR_FSAL_NOTDIR, 0);

  parent_obj_handle = container_of(parent, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);


  /* call to HPSS client api */
  /* We use hpss_GetRawAttrHandle for not chasing junctions nor symlinks. */

  rc = HPSSFSAL_GetRawAttrHandle(&(parent_obj_handle->handle->ns_handle), path, &ucreds, false,      /* don't traverse junctions */
                                 &obj_hdl, NULL, &hpss_attr);


    /**
     * /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
     *     Thus, in this case, we must double check
     *     by checking the directory handle.
     */
  if(rc == HPSS_ENOTDIR)
    {
      if(HPSSFSAL_IsStaleHandle(&parent_obj_handle->handle->ns_handle,
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  memset(&fsal_attr, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&obj_hdl, &hpss_attr, &fsal_attr);

  if(FSAL_IS_ERROR(status))
    return status;

  /* set output handle */
  hdl = hpss_alloc_handle(NULL, &fsal_attr, NULL, parent_obj_handle->obj_handle.export);

  if (hdl == NULL)
    return fsalstat(ERR_FSAL_NOMEM, 0);

  hdl->handle->obj_type = hpss2fsal_type(obj_hdl.Type);
  hdl->handle->ns_handle = obj_hdl;

 *handle = &hdl->obj_handle;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
    
}

/* lookup_path
 * should not be used for "/" only is exported */

fsal_status_t hpss_lookup_path(struct fsal_export *exp_hdl,
		     	      const struct req_op_context *opctx,
                              const char *path,
			      struct fsal_obj_handle **handle)
{
  int rc;
  fsal_status_t status;
  hpss_fileattr_t hpss_attr;
  struct attrlist fsal_attr;
  struct hpss_fsal_obj_handle *hdl;

  if( !exp_hdl || !path || !handle )
    return fsalstat(ERR_FSAL_FAULT, 0);

  if(strcmp(path, "/") != 0) {
    /* We only accept lookup_path to get root handle */
    return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
  }

  rc = hpss_FileGetAttributes("/", &hpss_attr);

  if (rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  memset(&fsal_attr, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&hpss_attr.ObjectHandle, &hpss_attr.Attrs, &fsal_attr);

  if(FSAL_IS_ERROR(status))
    return status;

  /* set output handle */
  hdl = hpss_alloc_handle(NULL, &fsal_attr, NULL, exp_hdl);

  if (hdl == NULL)
    return fsalstat(ERR_FSAL_NOMEM, 0);

  hdl->handle->obj_type = hpss2fsal_type(hpss_attr.ObjectHandle.Type);
  hdl->handle->ns_handle = hpss_attr.ObjectHandle;

  *handle = &hdl->obj_handle;
  
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param cred (input):
 *        Authentication context for the operation (user, export...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created file.
 * \param object_attributes (optionnal input/output): 
 *        The postop attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */

static fsal_status_t hpss_create( struct fsal_obj_handle *dir_hdl,
		                 const struct req_op_context *opctx,
                                 char *filename,
                                 struct attrlist *attrib,
                                 struct fsal_obj_handle **handle)
{

  struct hpss_fsal_obj_handle *parent_obj_handle, *hdl;
  int rc;
  fsal_status_t status;

  mode_t unix_mode;
  hpss_Attrs_t new_attr;
  ns_ObjHandle_t new_hdl;
  sec_cred_t ucreds;

  /* cos management */
  hpss_cos_hints_t hint;
  hpss_cos_priorities_t hintpri;

  /* If no COS is specified in the config file,
   * we give NULL pointers to CreateHandle,
   * to use the default Cos for this Fileset.
   */
  hpss_cos_hints_t *p_hint = NULL;
  hpss_cos_priorities_t *p_hintpri = NULL;

  /* sanity checks. */
  if(!dir_hdl || !opctx || !filename || !attrib || !handle)
    return fsalstat(ERR_FSAL_FAULT, 0);

  parent_obj_handle = container_of(dir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);

  /* convert fsal mode to unix mode. */
  unix_mode = fsal2unix_mode(attrib->mode);

  /* Apply umask */
  unix_mode = unix_mode & ~(dir_hdl->export->ops->fs_umask(dir_hdl->export));

  /* Eventually set cos */

  if((hpss_get_root_pvfs(dir_hdl->export))->default_cos != 0)
    {
      HPSSFSAL_BuildCos((hpss_get_root_pvfs(dir_hdl->export))->default_cos, &hint, &hintpri);
      p_hint = &hint;
      p_hintpri = &hintpri;
    }

  if((hpss_get_root_pvfs(dir_hdl->export))->default_cos != 0)
    LogDebug(COMPONENT_FSAL, "Creating file with COS = %d",
                      (hpss_get_root_pvfs(dir_hdl->export))->default_cos);
  else
    LogDebug(COMPONENT_FSAL, "Creating file with default fileset COS.");

  /* call to API */


  rc = HPSSFSAL_CreateHandle(&(parent_obj_handle->handle->ns_handle),     /* IN - Parent object handle */
                             filename,  /* IN - Name of the file to be created */
                             unix_mode, /* IN - Desired file perms */
                             &ucreds,    /* IN - User credentials */
                             p_hint,    /* IN - Desired class of service */
                             p_hintpri, /* IN - Priorities of hint struct */
                             NULL,      /* OUT - Granted class of service */
                             &new_attr,   /* OUT - File attributes */
                             &new_hdl,  /* OUT - File handle */
                             NULL);     /* OUT - Client authorization */


  /* /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
   * If the returned value is HPSS_ENOTDIR, parent handle MAY be stale.
   * Thus, we must double-check by calling getattrs.   
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&parent_obj_handle->handle->ns_handle,
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  /* other errors */

  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* convert hpss attributes to fsal attributes */
  memset(attrib, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&new_hdl, &new_attr, attrib);

  /* on error, we set a special bit in the mask. */
  if(FSAL_IS_ERROR(status))
    {
      FSAL_CLEAR_MASK(attrib->mask);
      FSAL_SET_MASK(attrib->mask, ATTR_RDATTR_ERR);
    }


  /* set output handle */
  hdl = hpss_alloc_handle(NULL, attrib, NULL, dir_hdl->export);

  if (hdl == NULL)
    return fsalstat(ERR_FSAL_NOMEM, 0);

  hdl->handle->obj_type = REGULAR_FILE;
  hdl->handle->ns_handle = new_hdl;

  *handle = &hdl->obj_handle;

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_mkdir:
 * Create a directory.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where
 *        the subdirectory is to be created.
 * \param p_dirname (input):
 *        Pointer to the name of the directory to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created directory.
 * \param object_attributes (optionnal input/output): 
 *        The attributes of the created directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
static fsal_status_t hpss_mkdir( struct fsal_obj_handle *dir_hdl,
	                        const struct req_op_context *opctx,
                                char *name,
                                struct attrlist *attrib,
                                struct fsal_obj_handle **handle)
{

  struct hpss_fsal_obj_handle *parent_obj_handle, *hdl;
  int rc;
  fsal_status_t status;
  mode_t unix_mode;
  ns_ObjHandle_t newdir_hdl;
  hpss_Attrs_t newdir_attr;
  sec_cred_t ucreds;

  /* sanity checks. */
  if( !dir_hdl || !opctx || !name || !attrib || !handle )
    return fsalstat(ERR_FSAL_FAULT, 0);

  parent_obj_handle = container_of(dir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);

  /* convert FSAL mode to HPSS mode. */
  unix_mode = fsal2unix_mode(attrib->mode);

  /* Apply umask */
  unix_mode = unix_mode & ~(dir_hdl->export->ops->fs_umask(dir_hdl->export));


  rc = HPSSFSAL_MkdirHandle(&(parent_obj_handle->handle->ns_handle),
                            name,
                            unix_mode,
                            &ucreds,
                            &newdir_hdl, &newdir_attr);


  /* /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
   * If the returned value is HPSS_ENOTDIR, parent handle MAY be stale.
   * Thus, we must double-check by calling getattrs.   
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&(parent_obj_handle->handle->ns_handle),
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  /* other errors */

  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* convert hpss attributes to fsal attributes */
  memset(attrib, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&newdir_hdl, &newdir_attr, attrib);

  /* on error, we set a special bit in the mask. */
  if(FSAL_IS_ERROR(status))
    {
      FSAL_CLEAR_MASK(attrib->mask);
      FSAL_SET_MASK(attrib->mask, ATTR_RDATTR_ERR);
    }


  /* set output handle   */
  hdl = hpss_alloc_handle(NULL, attrib, NULL, dir_hdl->export);

  if (hdl == NULL)
    return fsalstat(ERR_FSAL_NOMEM, 0);

  hdl->handle->obj_type = DIRECTORY;
  hdl->handle->ns_handle = newdir_hdl;

  *handle = &hdl->obj_handle;

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}


static fsal_status_t hpss_makenode( struct fsal_obj_handle *dir_hdl,
                                   const struct req_op_context *opctx,
                                   char *name,
                                   object_file_type_t nodetype,  /* IN */
                                   fsal_dev_t *dev,              /* IN */
                                   struct attrlist *attrib,
                                   struct fsal_obj_handle **handle )
{
     return fsalstat(ERR_FSAL_NOTSUPP, 0);
}



/**
 * FSAL_symlink:
 * Create a symbolic link.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the link is to be created.
 * \param p_linkname (input):
 *        Name of the link to be created.
 * \param p_linkcontent (input):
 *        Content of the link to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (ignored input):
 *        Mode of the link to be created.
 *        It has no sense in HPSS nor UNIX filesystems.
 * \param link_handle (output):
 *        Pointer to the handle of the created symlink.
 * \param link_attributes (optionnal input/output): 
 *        Attributes of the newly created symlink.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

static fsal_status_t hpss_makesymlink(struct fsal_obj_handle *dir_hdl,
                                     const struct req_op_context *opctx,
                                     char *link_name,
                                     char *link_content,
                                     struct attrlist *attrib,
                                     struct fsal_obj_handle **handle)
{

  int rc;
  fsal_status_t status;
  struct hpss_fsal_obj_handle *parent_obj_handle, *hdl;
  ns_ObjHandle_t lnk_hdl;
  hpss_Attrs_t lnk_attr;
  sec_cred_t ucreds;


  /* sanity checks */
  if( !dir_hdl || !opctx || !link_name || !link_content ||
      !attrib || !handle )
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* Tests if symlinking is allowed by configuration. */
  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_symlink_support))
    return fsalstat(ERR_FSAL_NOTSUPP, 0);


  parent_obj_handle = container_of(dir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);


  /* call to hpss client API. */
  memset( (char *)(&lnk_hdl), 0, sizeof( ns_ObjHandle_t ) ) ;
  rc = HPSSFSAL_SymlinkHandle(&(parent_obj_handle->handle->ns_handle),    /* IN - Handle of existing file */
                              link_content,      /* IN - Desired contents of the link */
                              link_name,         /* IN - New name of the symbolic link */
                              &ucreds,   /* IN - pointer to user credentials */
                              &lnk_hdl,        /* OUT */
                              &lnk_attr    /* OUT - symbolic link attributes */
      );


  /* /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
   * If the returned value is HPSS_ENOTDIR, parent handle MAY be stale.
   * Thus, we must double-check by calling getattrs.   
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&(parent_obj_handle->handle->ns_handle),
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  /* other errors */
  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* convert hpss attributes to fsal attributes */
  memset(attrib, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&lnk_hdl, &lnk_attr, attrib);

  /* on error, we set a special bit in the mask. */
  if(FSAL_IS_ERROR(status))
    {    
      FSAL_CLEAR_MASK(attrib->mask);
      FSAL_SET_MASK(attrib->mask, ATTR_RDATTR_ERR);
    }


  /* set output handle */
  hdl = hpss_alloc_handle(NULL, attrib, NULL, dir_hdl->export);

  if (hdl == NULL)       
    return fsalstat(ERR_FSAL_NOMEM, 0);

  hdl->handle->obj_type = SYMBOLIC_LINK;
  hdl->handle->ns_handle = lnk_hdl;

  *handle = &hdl->obj_handle;

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param linkhandle (input):
 *        Handle of the link to be read.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_attributes (optionnal input/output): 
 *        The post operation attributes of the symlink link.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (linkhandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (linkhandle does not address a symbolic link)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 * */

static fsal_status_t hpss_readsymlink(struct fsal_obj_handle *lnk_fsal_hdl,
                                 const struct req_op_context *opctx,
                                 struct gsh_buffdesc *link_content,
                                 bool refresh)
{

  int rc;
  struct hpss_fsal_obj_handle *lnk_hdl;
  sec_cred_t ucreds;

  /* sanity checks. */
  if( !lnk_fsal_hdl || !opctx || !link_content )
    return fsalstat(ERR_FSAL_FAULT, 0);

  lnk_hdl = container_of(lnk_fsal_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);

  link_content->len = lnk_fsal_hdl->attributes.filesize ?
                        lnk_fsal_hdl->attributes.filesize + 1 :
                        fsal_default_linksize;
  link_content->addr = gsh_malloc(link_content->len);


  /* call to the API */
  rc = hpss_ReadlinkHandle(&(lnk_hdl->handle->ns_handle),    /* IN - Handle of symbolic link */
                           NULL,        /* IN - Path of the link (null=>ignored) */
                           link_content->addr,    /* OUT - contents of the link */
                           link_content->len,   /* IN - Size, in bytes, of contents buffer */
                           &ucreds);       /* IN - pointer to user credentials */


  /* /!\ rc is the length for the symlink content !!! */

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT) {
    gsh_free(link_content->addr);
    link_content->addr = NULL;
    link_content->len = 0;
    return fsalstat(ERR_FSAL_STALE, -rc);
  } else if(rc < 0) {
    gsh_free(link_content->addr);
    link_content->addr = NULL;
    link_content->len = 0;
    return fsalstat(hpss2fsal_error(rc), -rc);
  }


  /* FIXME: do we want to check with strlen or something ? */
  link_content->len = rc + 1;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_link:
 * Create a hardlink.
 *
 * \param target_handle (input):
 *        Handle of the target object.
 * \param dir_handle (input):
 *        Pointer to the directory handle where
 *        the hardlink is to be created.
 * \param p_link_name (input):
 *        Pointer to the name of the hardlink to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param attributes (optionnal input/output): 
 *        The post_operation attributes of the linked object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (target_handle or dir_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the attributes->asked_attributes field.
 */
static fsal_status_t hpss_linkfile(struct fsal_obj_handle *obj_hdl,
                              const struct req_op_context *opctx,
			      struct fsal_obj_handle *destdir_hdl,
			      char *name)
{

  int rc;
  struct hpss_fsal_obj_handle *destdir_obj_hdl, *file_obj_hdl;
  sec_cred_t ucreds;


  /* sanity checks. */
  if( !obj_hdl || !opctx || !destdir_hdl || !name )
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* Tests if hardlinking is allowed by configuration. */
  if(!obj_hdl->export->ops->fs_supports(obj_hdl->export, fso_link_support))
    return fsalstat(ERR_FSAL_NOTSUPP, 0);

  destdir_obj_hdl = container_of(destdir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  file_obj_hdl = container_of(obj_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);


  /* Call to HPSS API */
  rc = hpss_LinkHandle(&(file_obj_hdl->handle->ns_handle),         /* IN - Handle of existing file */
                       &(destdir_obj_hdl->handle->ns_handle),      /* IN - parent directory handle */
                       name,                                       /* IN - New name of the object */
                       &ucreds); /* IN - pointer to user credentials */


  /* /!\ WARNING : When one of the handles is stale, HPSS returns ENOTDIR or ENOENT.
   * Thus, we must check this by calling HPSSFSAL_IsStaleHandle.
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&(destdir_obj_hdl->handle->ns_handle),
                                &ucreds)
         ||
         HPSSFSAL_IsStaleHandle(&(file_obj_hdl->handle->ns_handle),
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  /* other errors */
  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_readdir :
 *     Read the entries of an opened directory.
 *     
 * \param dir_descriptor (input):
 *        Pointer to the directory descriptor filled by FSAL_opendir.
 * \param start_position (input):
 *        Cookie that indicates the first object to be read during
 *        this readdir operation.
 *        This should be :
 *        - FSAL_READDIR_FROM_BEGINNING for reading the content
 *          of the directory from the beginning.
 *        - The end_position parameter returned by the previous
 *          call to FSAL_readdir.
 * \param get_attr_mask (input)
 *        Specify the set of attributes to be retrieved for directory entries.
 * \param buffersize (input)
 *        The size (in bytes) of the buffer where
 *        the direntries are to be stored.
 * \param pdirent (output)
 *        Adresse of the buffer where the direntries are to be stored.
 * \param end_position (output)
 *        Cookie that indicates the current position in the directory.
 * \param nb_entries (output)
 *        Pointer to the number of entries read during the call.
 * \param end_of_dir (output)
 *        Pointer to a boolean that indicates if the end of dir
 *        has been reached during the call.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

/**
 * read_dirents
 * read the directory and call through the callback function for
 * each entry.
 * @param dir_hdl [IN] the directory to read
 * @param entry_cnt [IN] limit of entries. 0 implies no limit
 * @param whence [IN] where to start (next)
 * @param dir_state [IN] pass thru of state to callback
 * @param cb [IN] callback function
 * @param eof [OUT] eof marker true == end of dir
 */

#define MAX_ENTRIES 256
static fsal_status_t hpss_readdir(struct fsal_obj_handle *dir_hdl,
			      	  const struct req_op_context *opctx,
				  fsal_cookie_t *whence,
				  void *dir_state,
                                  fsal_readdir_cb cb,
                                  bool *eod)
{
  int rc, returned, i;

  /* hpss_ReadRawAttrsHandle arguments. */

  u_signed64 cookie;
  unsigned32 end_of_dir;

  struct hpss_fsal_obj_handle *dir_obj_hdl;
  sec_cred_t ucreds;

  hpss_dirent_t dirent[MAX_ENTRIES];

  /* sanity checks */
  if( !dir_hdl || !opctx || !eod )
    return fsalstat(ERR_FSAL_FAULT, 0);

  LogFullDebug(COMPONENT_FSAL,
               "hpss_readdir called");


  /* init values */
  dir_obj_hdl = container_of(dir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);

  /** @todo: FIXME, make this a compiler check in cmake somehow */
  assert(sizeof(fsal_cookie_t) == sizeof(u_signed64));

  if(whence != NULL) {
    memcpy(&cookie, whence, sizeof(fsal_cookie_t));
  } else {
    memset(&cookie, 0, sizeof(u_signed64));
  }

  end_of_dir = 0;

  /* Process everything */
  while( ! end_of_dir )
    {
#if 0
        /* Keep this around because readdir is likely to require metadatas again */
        rc = HPSSFSAL_ReadRawAttrsHandle(&(dir_obj_hdl->handle->ns_handle),
                                       cookie,
                                       &ucreds,
                                       MAX_ENTRIES * sizeof(ns_DirEntry_t),
                                       false, /* don't get attributes */
                                       hpss_specific_initinfo(dir_hdl->export->fsal)->ReturnInconsistentDirent,
                                       &end_of_dir, &cookie, dirent);

#endif

      /* call to hpss clapi */
      rc = HPSSFSAL_ReaddirHandle(&(dir_obj_hdl->handle->ns_handle), /* dir handle */
                              cookie,               /* cookie IN */
                              &ucreds, /* crentials */
                              MAX_ENTRIES * sizeof(hpss_dirent_t), /* size of dirent */
                              hpss_specific_initinfo(dir_hdl->export->fsal)->ReturnInconsistentDirent,
                              &end_of_dir, &cookie, dirent);

     if(rc < 0)
         return fsalstat(hpss2fsal_error(rc), -rc);

      returned = rc;

      /* Process dirent */
      for(i = 0; i < returned; i++)
        {
          /* Skip . and .. */
          if(!strcmp(dirent[i].d_name, ".") || !strcmp(dirent[i].d_name, ".."))
            continue;

          /* callback to cache inode - stop if it returns 0 */
          if(!cb( opctx,
                  dirent[i].d_name,
                  dir_state,
                  (fsal_cookie_t)cookie) )
            goto done;         

        }
    }

done:
  *eod = end_of_dir;
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_parentdir_handle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_parentdir_handle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param src_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the source directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 * \param tgt_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the target directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (a parent directory handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (a parent directory handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_old_name does not exist)
 *        - ERR_FSAL_NOTEMPTY     (the target object is a non empty directory)
 *        - ERR_FSAL_XDEV         (tried to move an object across different filesystems)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
  */

static fsal_status_t hpss_rename( struct fsal_obj_handle *olddir_hdl,
                                  const struct req_op_context *opctx,
			          char *old_name,
				  struct fsal_obj_handle *newdir_hdl,
				  char *new_name)
{

  int rc;

  struct hpss_fsal_obj_handle *olddir_obj_hdl, *newdir_obj_hdl;
  sec_cred_t ucreds;

  /* sanity checks. */
  if( !olddir_hdl || ! newdir_hdl || !old_name || !new_name )
    return fsalstat(ERR_FSAL_FAULT, 0);

  olddir_obj_hdl = container_of(olddir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  newdir_obj_hdl = container_of(newdir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);


  rc = hpss_RenameHandle(&(olddir_obj_hdl->handle->ns_handle),
                         old_name,
                         &(newdir_obj_hdl->handle->ns_handle),
                         new_name, &ucreds);


  /* convert the HPSS EEXIST error to the expected error ENOTEMPTY */
  if(rc == HPSS_EEXIST)
    return fsalstat(ERR_FSAL_NOTEMPTY, -rc);

  /* the source or the target directory handles may be stale */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&(olddir_obj_hdl->handle->ns_handle),
                                &ucreds) ||
         HPSSFSAL_IsStaleHandle(&(newdir_obj_hdl->handle->ns_handle),
                                &ucreds))
        {
          return fsalstat(ERR_FSAL_STALE, -rc);
        }
    }

  /* any other error */
  if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}


/* FIXME: attributes are now merged into fsal_obj_handle.  This
 * spreads everywhere these methods are used.  eventually deprecate
 * everywhere except where we explicitly want to to refresh them.
 * NOTE: this is done under protection of the attributes rwlock in the
 * cache entry.
 */

/**
 * FSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user, export...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Another error code if an error occured.
 */
static fsal_status_t hpss_getattrs(struct fsal_obj_handle *fsal_obj_hdl,
				  const struct req_op_context *opctx)
{

  int rc;
  fsal_status_t status;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;
  struct hpss_fsal_obj_handle *obj_hdl;
  sec_cred_t ucreds;


  /* sanity checks. */
  if( !fsal_obj_hdl || !opctx )
    return fsalstat(ERR_FSAL_FAULT, 0);

  obj_hdl = container_of(fsal_obj_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);


  /* get attributes */
  /* We use  HPSSFSAL_GetRawAttrHandle for not chasing junctions
   * nor solving symlinks. What's more, we want hpss_Attrs_t.
   */
  rc = HPSSFSAL_GetRawAttrHandle(&(obj_hdl->handle->ns_handle), NULL, /* no junction path */
                                 &ucreds, false,   /* don't solve junctions */
                                 &hpss_hdl, NULL, &hpss_attr);


  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    return fsalstat(ERR_FSAL_STALE, -rc);
  else if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* convert attributes */
  memset(&fsal_obj_hdl->attributes, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&hpss_hdl, &hpss_attr, &fsal_obj_hdl->attributes);

  if(FSAL_IS_ERROR(status)) {
    FSAL_CLEAR_MASK(fsal_obj_hdl->attributes.mask);
    FSAL_SET_MASK(fsal_obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
    return fsalstat(status.major, status.minor);
  }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_INVAL        (tried to modify a read-only attribute)
 *        - ERR_FSAL_ATTRNOTSUPP  (tried to modify a non-supported attribute)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */

/*
 * NOTE: this is done under protection of the attributes rwlock in the cache entry.
 */

static fsal_status_t hpss_setattrs( struct fsal_obj_handle *fsal_obj_hdl,
				    const struct req_op_context *opctx,
			            struct attrlist *attrib_set)
{

  int rc;
  fsal_status_t status;
  struct attrlist attrs;

  hpss_fileattrbits_t hpss_attr_mask;
  hpss_fileattr_t hpss_fattr_in, hpss_fattr_out;

  struct hpss_fsal_obj_handle *obj_hdl;
  sec_cred_t ucreds;

  /* sanity checks. */
  if( !fsal_obj_hdl || !opctx || !attrib_set )
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* init variables */
  obj_hdl = container_of(fsal_obj_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);
  attrs = *attrib_set;
  hpss_fattr_in.ObjectHandle = obj_hdl->handle->ns_handle;

  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */
  if(!fsal_obj_hdl->export->ops->fs_supports(fsal_obj_hdl->export, fso_cansettime))
    {
      if(attrs.mask & (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME))
        {
          /* handled as an unsettable attribute. */
          return fsalstat(ERR_FSAL_INVAL, 0);
        }

    }

  /* apply umask, if mode attribute is to be changed */

  if(FSAL_TEST_MASK(attrs.mask, ATTR_MODE))
    {
      attrs.mode &= ~fsal_obj_hdl->export->ops->fs_umask(fsal_obj_hdl->export);
    }

  /** @todo : chown restricted seems to be OK. */


  /* Then, convert attribute set. */

  status = fsal2hpss_attribset(fsal_obj_hdl,
                               &attrs, &hpss_attr_mask, &(hpss_fattr_in.Attrs));

  if(FSAL_IS_ERROR(status))
    return fsalstat(status.major, status.minor);

  /* Call HPSS client API function. */


  rc = HPSSFSAL_FileSetAttrHandle(&(obj_hdl->handle->ns_handle),     /* IN  - object handle */
                                  NULL,                 /* IN  - path to the object */
                                  &ucreds,   /* IN  - user credentials */
                                  hpss_attr_mask,       /* IN - attributes fields to set */
                                  &hpss_fattr_in,       /* IN  - input attributes */
                                  &hpss_fattr_out       /* OUT - attributes after change */
      );


  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    return fsalstat(ERR_FSAL_STALE, -rc);
  else if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* Optionaly fills output attributes. */

  /** @todo see why/if hpss_fattr_out isn't complete? */


  /* convert attributes */
  memset(&fsal_obj_hdl->attributes, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&(obj_hdl->handle->ns_handle), &hpss_fattr_out.Attrs, &fsal_obj_hdl->attributes);

  if(FSAL_IS_ERROR(status)) {
    FSAL_CLEAR_MASK(fsal_obj_hdl->attributes.mask);
    FSAL_SET_MASK(fsal_obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
    return fsalstat(status.major, status.minor);
  }  

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}


/**
 * FSAL_unlink:
 * Remove a filesystem object .
 *
 * \param parentdir_handle (input):
 *        Handle of the parent directory of the object to be deleted.
 * \param p_object_name (input):
 *        Name of the object to be removed.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param parentdir_attributes (optionnal input/output): 
 *        Post operation attributes of the parent directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parentdir_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parentdir_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_object_name does not exist)
 *        - ERR_FSAL_NOTEMPTY     (tried to remove a non empty directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

static fsal_status_t hpss_unlink( struct fsal_obj_handle *dir_hdl,
			          const struct req_op_context *opctx,
			          char *name)
{
  int rc;
  fsal_status_t st;
  struct fsal_obj_handle *fsal_obj_hdl;
  struct hpss_fsal_obj_handle *dir_obj_hdl;
  sec_cred_t ucreds;

  /* sanity checks. */
  if( !dir_hdl || !opctx || !name )
    return fsalstat(ERR_FSAL_FAULT, 0);

  dir_obj_hdl = container_of(dir_hdl, struct hpss_fsal_obj_handle, obj_handle);
  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);

  /* Action depends on the object type to be deleted.
   * To know that, we get fsal object handle.
   */
  st = dir_hdl->export->obj_ops->lookup(dir_hdl, opctx, name, &fsal_obj_hdl);

  if(FSAL_IS_ERROR(st))
    return fsalstat(st.major, st.minor);

  switch (fsal_obj_hdl->type)
    {

    case DIRECTORY:

      /* remove a directory */


      rc = hpss_RmdirHandle(&(dir_obj_hdl->handle->ns_handle),
                            name, &ucreds);


      /* The EEXIST error is actually an NOTEMPTY error. */

      if(rc == EEXIST || rc == -EEXIST)
        return fsalstat(ERR_FSAL_NOTEMPTY, -rc);
      else if(rc)
        return fsalstat(hpss2fsal_error(rc), -rc);

      break;

    case SYMBOLIC_LINK:
    case REGULAR_FILE:
      /* remove an object */

      rc = hpss_UnlinkHandle(&(dir_obj_hdl->handle->ns_handle),
                             name, &ucreds);

      if(rc)
        return fsalstat(hpss2fsal_error(rc), -rc);

      break;

    case FS_JUNCTION:
      /* remove a junction */

      rc = hpss_JunctionDeleteHandle(&(dir_obj_hdl->handle->ns_handle),
                                     name, &ucreds);

      if(rc)
        return fsalstat(hpss2fsal_error(rc), -rc);

      break;

    case FIFO_FILE:
    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    default:
      LogCrit(COMPONENT_FSAL, "Unexpected object type : %d\n",
                        fsal_obj_hdl->type);
      return fsalstat(ERR_FSAL_SERVERFAULT, 0);

    }

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
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

static fsal_status_t hpss_handle_digest(const struct fsal_obj_handle *fsal_obj_hdl,
                                        fsal_digesttype_t output_type,
                                        struct gsh_buffdesc *fh_desc)

{
  int memlen;
  uint64_t objid64;           /* to cast the objID */

  const struct hpss_fsal_obj_handle *obj_hdl;
  ns_ObjHandle_t *ns_handle;

  /* sanity checks */
  if( !fsal_obj_hdl || !fh_desc )
    return fsalstat(ERR_FSAL_FAULT, 0);

  obj_hdl = container_of(fsal_obj_hdl, const struct hpss_fsal_obj_handle, obj_handle);
  ns_handle = &obj_hdl->handle->ns_handle;

  switch (output_type)
    {

      /* NFSV3 handle digest */
    case FSAL_DIGEST_NFSV3:

      memlen = sizeof(ns_ObjHandle_t);

      /* sanity check about output size */
      if(memlen > FSAL_DIGEST_SIZE_HDLV3)
        return fsalstat(ERR_FSAL_TOOSMALL, 0);

      /* building digest :
       * - fill it with zeros
       * - setting the first bytes to the fsal_handle value
       */
      fh_desc->len = memlen;
      memset(fh_desc->addr, 0, FSAL_DIGEST_SIZE_HDLV3);
      memcpy(fh_desc->addr, ns_handle, memlen);

      break;

      /* NFSV4 handle digest */
    case FSAL_DIGEST_NFSV4:

      memlen = sizeof(ns_ObjHandle_t);

      /* sanity check about output size */
      if(memlen > FSAL_DIGEST_SIZE_HDLV4)
        return fsalstat(ERR_FSAL_TOOSMALL, 0);

      /* building digest :
       * - fill it with zeros
       * - setting the first bytes to the fsal_handle value
       */
      fh_desc->len = memlen;
      memset(fh_desc->addr, 0, FSAL_DIGEST_SIZE_HDLV4);
      memcpy(fh_desc->addr, ns_handle, memlen);

      break;

      /* FileId digest for NFSv3 */
    case FSAL_DIGEST_FILEID3:

      /* get object ID from handle */
      objid64 = hpss_GetObjId(ns_handle);

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(uint64_t) > FSAL_DIGEST_SIZE_FILEID3)
        return fsalstat(ERR_FSAL_TOOSMALL, 0);

#endif

      fh_desc->len = sizeof(uint64_t);
      memset(fh_desc->addr, 0, FSAL_DIGEST_SIZE_FILEID3);
      memcpy(fh_desc->addr, &objid64, sizeof(uint64_t));
      break;

      /* FileId digest for NFSv4 */

    case FSAL_DIGEST_FILEID4:
      /* get object ID from handle */
      objid64 = hpss_GetObjId(ns_handle);

#ifndef _NO_CHECKS

      /* sanity check about output size */

      if(sizeof(uint64_t) > FSAL_DIGEST_SIZE_FILEID4)
        return fsalstat(ERR_FSAL_TOOSMALL, 0);

#endif

      fh_desc->len = sizeof(uint64_t);
      memset(fh_desc->addr, 0, FSAL_DIGEST_SIZE_FILEID4);
      memcpy(fh_desc->addr, &objid64, sizeof(uint64_t));
      break;

    default:
      return fsalstat(ERR_FSAL_SERVERFAULT, 0);
    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}


/**
 * handle_to_key
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 */

static void hpss_handle_to_key( struct fsal_obj_handle *obj_hdl,
                               struct gsh_buffdesc *fh_desc )
{
	struct hpss_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct hpss_fsal_obj_handle, obj_handle);
	fh_desc->addr = &myself->handle->ns_handle;
	fh_desc->len = sizeof(ns_ObjHandle_t);
}

/*
 * release
 * release our export first so they know we are gone
 */

static fsal_status_t release(struct fsal_obj_handle *obj_hdl)
{
	struct hpss_fsal_obj_handle *myself;
	int retval = 0;
        object_file_type_t type = obj_hdl->type;

	myself = container_of(obj_hdl, struct hpss_fsal_obj_handle, obj_handle);

        if(type == REGULAR_FILE && myself->u.file.openflags != FSAL_O_CLOSED) {
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle, "
                        "hdl = 0x%p, fd = %d, openflags = 0x%x",
                        obj_hdl,
                        myself->u.file.fd, myself->u.file.openflags);
                return fsalstat(ERR_FSAL_INVAL, 0);
        }

        retval = fsal_obj_handle_uninit(obj_hdl);
        if (retval != 0) {
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle, "
                        "hdl = 0x%p->refs = %d",
                        obj_hdl, obj_hdl->refs);
                return fsalstat(ERR_FSAL_DELAY, 0);
        }

	if(type == SYMBOLIC_LINK) {
		if(myself->u.symlink.link_content != NULL)
			free(myself->u.symlink.link_content);
	} 
	free(myself);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/* $#@! ugly casts because HPSS functions don't handle const char* and I have no clue what else I could cast */
void hpss_handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->lookup = (fsal_status_t (*)(struct fsal_obj_handle *,
                                const struct req_op_context *,
                                const char *,
                                struct fsal_obj_handle **))hpss_lookup;
	ops->readdir = hpss_readdir;
	ops->create = (fsal_status_t (*)(struct fsal_obj_handle *,
                                const struct req_op_context *,
                                const char *,
                                struct attrlist *,
                                struct fsal_obj_handle **))hpss_create;
	ops->mkdir = (fsal_status_t (*)(struct fsal_obj_handle *,
                               const struct req_op_context *,
                               const char *,
                               struct attrlist *,
                               struct fsal_obj_handle **))hpss_mkdir;
	ops->mknode = (fsal_status_t (*)(struct fsal_obj_handle *,
                                const struct req_op_context *,
                                const char *,
                                object_file_type_t,
                                fsal_dev_t *,
                                struct attrlist *,
                                struct fsal_obj_handle **))hpss_makenode;
	ops->symlink = (fsal_status_t (*)(struct fsal_obj_handle *,
                                 const struct req_op_context *,
                                 const char *,
                                 const char *,
                                 struct attrlist *,
                                 struct fsal_obj_handle **))hpss_makesymlink;
	ops->readlink = hpss_readsymlink;
	ops->test_access = fsal_test_access;
	ops->getattrs = hpss_getattrs;
	ops->setattrs = hpss_setattrs;
	ops->link = (fsal_status_t (*)(struct fsal_obj_handle *,
                              const struct req_op_context *,
                              struct fsal_obj_handle *,
                              const char *))hpss_linkfile;
	ops->rename = (fsal_status_t (*)(struct fsal_obj_handle *,
                                const struct req_op_context *,
                                const char *,
                                struct fsal_obj_handle *,
                                const char *))hpss_rename;
	ops->unlink = (fsal_status_t (*)(struct fsal_obj_handle *,
                                const struct req_op_context *,
                                const char *))hpss_unlink;
	ops->open = hpss_open;
	ops->status = hpss_status;
	ops->read = hpss_read;
	ops->write = hpss_write;
	ops->commit = hpss_commit;
	ops->lock_op = hpss_lock_op;
	ops->close = hpss_close;
	ops->lru_cleanup = hpss_lru_cleanup;
	ops->handle_digest = hpss_handle_digest;
	ops->handle_to_key = hpss_handle_to_key;

        /* xattr related functions */
        ops->list_ext_attrs = hpss_list_ext_attrs;
        ops->getextattr_id_by_name = hpss_getextattr_id_by_name;
        ops->getextattr_value_by_name = hpss_getextattr_value_by_name;
        ops->getextattr_value_by_id = hpss_getextattr_value_by_id;
        ops->setextattr_value = hpss_setextattr_value;
        ops->setextattr_value_by_id = hpss_setextattr_value_by_id;
        ops->getextattr_attrs = hpss_getextattr_attrs;
        ops->remove_extattr_by_id = hpss_remove_extattr_by_id;
        ops->remove_extattr_by_name = hpss_remove_extattr_by_name;
}

/* export methods that create object handles
 */

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in cache_inode etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 */

fsal_status_t hpss_create_handle( struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 struct gsh_buffdesc *hdl_desc,
				 struct fsal_obj_handle **handle)
{
  fsal_status_t status;
  char link_buff[PATH_MAX];
  char *link_content = NULL;
  struct attrlist fsal_attr;
  int rc, memlen;
  struct hpss_fsal_obj_handle *hdl;
  sec_cred_t ucreds;
  struct hpss_file_handle fh;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;

  *handle = NULL; /* poison it first */

  if( !exp_hdl || !opctx  || !hdl_desc || !handle )
    return fsalstat(ERR_FSAL_FAULT, 0);

  memlen = sizeof(ns_ObjHandle_t);

  if( hdl_desc->len != memlen )
    return fsalstat(ERR_FSAL_FAULT, 0);

  HPSSFSAL_ucreds_from_opctx(opctx, &ucreds);
  memcpy(&fh.ns_handle, hdl_desc->addr, memlen);
  fh.obj_type = hpss2fsal_type(fh.ns_handle.Type);


  /* Fill in attributes */  
  rc = HPSSFSAL_GetRawAttrHandle(&(fh.ns_handle), NULL, /* no junction path */
                                 &ucreds, false,   /* don't solve junctions */
                                 &hpss_hdl, NULL, &hpss_attr);


  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    return fsalstat(ERR_FSAL_STALE, -rc);
  else if(rc)
    return fsalstat(hpss2fsal_error(rc), -rc);

  /* convert attributes */
  memset(&fsal_attr, 0, sizeof(struct attrlist));
  status = hpss2fsal_attributes(&hpss_hdl, &hpss_attr, &fsal_attr);

  if(FSAL_IS_ERROR(status))
    return status;


  /* If link, fill in link content */
  if(S_ISLNK(fsal_attr.mode))  
    {
      rc = hpss_ReadlinkHandle(&(fh.ns_handle),    /* IN - Handle of symbolic link */
                               NULL,        /* IN - Path of the link (null=>ignored) */
                               link_buff,    /* OUT - contents of the link */
                               PATH_MAX,   /* IN - Size, in bytes, of contents buffer */
                               &ucreds);       /* IN - pointer to user credentials */
      /* /!\ rc is the length for the symlink content !!! */
  
      /* The HPSS_ENOENT error actually means that handle is STALE */
      if(rc == HPSS_ENOENT)
        return fsalstat(ERR_FSAL_STALE, -rc);
      else if(rc < 0)
        return fsalstat(hpss2fsal_error(rc), -rc);

      link_content = link_buff;
    }

  /* set output handle */
  hdl = hpss_alloc_handle(&fh, &fsal_attr, link_content, exp_hdl);

  if (hdl == NULL)
    return fsalstat(ERR_FSAL_NOMEM, 0);

 *handle = &hdl->obj_handle;

  return fsalstat(ERR_FSAL_NO_ERROR, 0 );
}
