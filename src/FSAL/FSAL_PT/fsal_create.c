// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_create.c
// Description: FSAL create operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_create.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>

// PTFSAL
#include "pt_ganesha.h"

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created file.
 * \param object_attributes (optional input/output):
 *        The attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occurred.
 */
fsal_status_t
PTFSAL_create(fsal_handle_t      * p_parent_directory_handle, /* IN */
              fsal_name_t        * p_filename,                /* IN */
              fsal_op_context_t  * p_context,                 /* IN */
              fsal_accessmode_t    accessmode,                /* IN */
              fsal_handle_t      * p_object_handle,           /* OUT */
              fsal_attrib_list_t * p_object_attributes        /* [ IN/OUT ] */)
{

  int errsv;
  int setgid_bit = 0;
  fsal_status_t status;

  mode_t unix_mode;
  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t parent_dir_attrs;
  int open_rc;
  ptfsal_handle_t * p_fsi_handle = (ptfsal_handle_t *)p_object_handle;

  FSI_TRACE(FSI_DEBUG, "Begin to create file************************\n");

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context || !p_object_handle || 
     !p_filename) {
    FSI_TRACE(FSI_DEBUG, "BAD Happen!");
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);
  }

  /* convert fsal mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

  /* retrieve directory metadata */
  parent_dir_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
  status = PTFSAL_getattrs(p_parent_directory_handle, p_context, 
                           &parent_dir_attrs);
  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_create);
  }

  /* Check the user can write in the directory, and check the setgid bit 
   * on the directory 
   */

  if(fsal2unix_mode(parent_dir_attrs.mode) & S_ISGID) {
    setgid_bit = 1;
  }

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);

  if(!p_context->export_context->fe_static_fs_info->accesscheck_support) {
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &parent_dir_attrs);
  } else {
    status = fsal_internal_access(p_context, 
                                  p_parent_directory_handle, 
                                  access_mask,
                                  &parent_dir_attrs);
  }
  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_create);
  }

  // Create the file, return handle
  open_rc = ptfsal_open(p_parent_directory_handle, 
                        p_filename, p_context, unix_mode, p_object_handle);
  if (open_rc < 0) {
     errsv = errno;
     Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
  }

  FSI_TRACE(FSI_DEBUG, "New Handle = %s", 
            (char *)p_fsi_handle->data.handle.f_handle);

  /* retrieve file attributes */
  if(p_object_attributes) {
    status = PTFSAL_getattrs(p_object_handle, 
                             p_context, 
                             p_object_attributes);

    /* on error, we set a special bit in the mask. */
    if(FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }

  FSI_TRACE(FSI_DEBUG, "End to create file************************\n");

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);

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
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_mkdir(fsal_handle_t      * p_parent_directory_handle, /* IN */
             fsal_name_t        * p_dirname,                 /* IN */
             fsal_op_context_t  * p_context,                 /* IN */
             fsal_accessmode_t    accessmode,                /* IN */
             fsal_handle_t      * p_object_handle,           /* OUT */
             fsal_attrib_list_t * p_object_attributes        /* [ IN/OUT ] */)
{

  int rc, errsv;
  int setgid_bit = 0;
  mode_t unix_mode;
  fsal_status_t status;
  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t parent_dir_attrs;
  char               newPath[PATH_MAX];

  FSI_TRACE(FSI_INFO,"MKDIR BEGIN-------------------------\n");

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context || !p_object_handle || 
     !p_dirname) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);
  }

  /* convert FSAL mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  /* get directory metadata */
  parent_dir_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
  status = PTFSAL_getattrs(p_parent_directory_handle, p_context, 
                           &parent_dir_attrs);
  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_create);
  }

  /* Check the user can write in the directory, and check the 
   * setgid bit on the directory 
   */

  if(fsal2unix_mode(parent_dir_attrs.mode) & S_ISGID) {
    setgid_bit = 1;
  }

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_SUBDIRECTORY);

  if(!p_context->export_context->fe_static_fs_info->accesscheck_support) {
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &parent_dir_attrs);
  } else {
    status = fsal_internal_access(p_context, p_parent_directory_handle, 
                                  access_mask,
                                  &parent_dir_attrs);
  }
  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_mkdir);
  }

  rc = ptfsal_mkdir(p_parent_directory_handle, p_dirname, 
                    p_context, unix_mode, p_object_handle);
  errsv = errno;
  if(rc) {
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mkdir);
  }

  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_mkdir);
  }

  /* the directory has been created */
  /* chown the dir to the current user/group */

  if(p_context->credential.user != geteuid()) {
    FSI_TRACE(FSI_DEBUG, "MKDIR %d",__LINE__);
    /* if the setgid_bit was set on the parent directory, do not change 
     * the group of the created file, because it's already the parentdir's 
     * group       
     */

    if(fsi_get_name_from_handle(
       p_context, 
       (char *)p_object_handle->data.handle.f_handle, 
       (char *)newPath, NULL) < 0) {
       FSI_TRACE(FSI_DEBUG, "Failed to get name from handle %s", 
                 (char *)p_object_handle->data.handle.f_handle);
       Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mkdir);
    }  
    rc = ptfsal_chown(p_context, newPath,
                      p_context->credential.user,
                      setgid_bit ? -1 : (int)p_context->credential.group);
    errsv = errno;
    if(rc) {
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mkdir);
    }
  }

  /* retrieve file attributes */
  if(p_object_attributes) {
    FSI_TRACE(FSI_DEBUG, "MKDIR %d",__LINE__);
    status = PTFSAL_getattrs(p_object_handle, p_context, 
                             p_object_attributes);

    /* on error, we set a special bit in the mask. */
    if(FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }
  FSI_TRACE(FSI_INFO,"MKDIR END ------------------\n");
  FSI_TRACE(FSI_DEBUG, "MKDIR %d",__LINE__);
  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mkdir);

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
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_link(fsal_handle_t      * p_target_handle, /* IN */
            fsal_handle_t      * p_dir_handle,    /* IN */
            fsal_name_t        * p_link_name,     /* IN */
            fsal_op_context_t  * p_context,       /* IN */
            fsal_attrib_list_t * p_attributes     /* [ IN/OUT ] */)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);
}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported upon HPSS.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t
PTFSAL_mknode(fsal_handle_t      * parentdir_handle, /* IN */
              fsal_name_t        * p_node_name,      /* IN */
              fsal_op_context_t  * p_context,        /* IN */
              fsal_accessmode_t    accessmode,       /* IN */
              fsal_nodetype_t      nodetype,         /* IN */
              fsal_dev_t         * dev,              /* IN */
              fsal_handle_t      * p_object_handle,  /* OUT (handle to the created node) */
              fsal_attrib_list_t * node_attributes   /* [ IN/OUT ] */)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);
}
