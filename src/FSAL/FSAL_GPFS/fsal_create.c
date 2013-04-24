/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_create.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.18 $
 * \brief   Filesystem objects creation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/access_check.h"
#include <unistd.h>
#include <fcntl.h>

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
fsal_status_t GPFSFSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */
    )
{
  fsal_status_t status;

  mode_t unix_mode;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context || !p_object_handle || !p_filename)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* convert fsal mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

  /* call to filesystem */

  fsal_set_credentials(p_context);
  TakeTokenFSCall();
  status = fsal_internal_create(p_context, p_parent_directory_handle,
                                p_filename, unix_mode | S_IFREG, 0,
                                p_object_handle, NULL);
  ReleaseTokenFSCall();
  fsal_restore_ganesha_credentials();
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_create);

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(p_object_handle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  // error injection to test DRC
  //sleep(61);
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
fsal_status_t GPFSFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * p_object_handle,       /* OUT */
                         fsal_attrib_list_t * p_object_attributes       /* [ IN/OUT ] */
    )
{
/*   int setgid_bit = 0; */
  mode_t unix_mode;
  fsal_status_t status;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context || !p_object_handle || !p_dirname)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  /* convert FSAL mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  /* build new entry path */

  /* creates the directory and get its handle */

  fsal_set_credentials(p_context);

  TakeTokenFSCall();
  status = fsal_internal_create(p_context, p_parent_directory_handle,
                                p_dirname, unix_mode | S_IFDIR, 0,
                                p_object_handle, NULL);
  ReleaseTokenFSCall();
  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mkdir);

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(p_object_handle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

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
fsal_status_t GPFSFSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                        fsal_handle_t * p_dir_handle,   /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * p_attributes       /* [ IN/OUT ] */
    )
{
  fsal_status_t status;

  /* sanity checks.
   * note : attributes is optional.
   */
  if(!p_target_handle || !p_dir_handle || !p_context || !p_context->export_context
     || !p_link_name || !p_link_name->name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  /* Tests if hardlinking is allowed by configuration. */

  if(!global_fs_info.link_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);

  /* Create the link on the filesystem */

  fsal_set_credentials(p_context);

  TakeTokenFSCall();
  status = fsal_internal_link_fh(p_context, p_target_handle, p_dir_handle,
                                 p_link_name);
  ReleaseTokenFSCall();

  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
      goto out_status_fsal_err;

  /* optionnaly get attributes */

  if(p_attributes)
    {
      status = GPFSFSAL_getattrs(p_target_handle, p_context, p_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_attributes->asked_attributes);
          FSAL_SET_MASK(p_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);

 out_status_fsal_err:

  ReturnStatus(status, INDEX_FSAL_link);
}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported upon HPSS.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t GPFSFSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                          fsal_attrib_list_t * node_attributes  /* [ IN/OUT ] */
    )
{
  fsal_status_t status;
/*   int flags=(O_RDONLY|O_NOFOLLOW); */
  mode_t unix_mode = 0;
  dev_t unix_dev = 0;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parentdir_handle || !p_context || !p_node_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  switch (nodetype)
    {
    case FSAL_TYPE_BLK:
      if(!dev)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);
      unix_mode |= S_IFBLK;
      unix_dev = (dev->major << 8) | (dev->minor & 0xFF);
      break;

    case FSAL_TYPE_CHR:
      if(!dev)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);
      unix_mode |= S_IFCHR;
      unix_dev = (dev->major << 8) | (dev->minor & 0xFF);
      break;

    case FSAL_TYPE_SOCK:
      unix_mode |= S_IFSOCK;
      break;

    case FSAL_TYPE_FIFO:
      unix_mode |= S_IFIFO;
/*       flags = (O_RDONLY | O_NOFOLLOW | O_NONBLOCK); */
      break;

    default:
      LogMajor(COMPONENT_FSAL,
               "Invalid node type in FSAL_mknode: %d", nodetype);
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_mknode);
    }

  fsal_set_credentials(p_context);

  TakeTokenFSCall();
  status = fsal_internal_create(p_context, parentdir_handle,
                                p_node_name, unix_mode, unix_dev,
                                p_object_handle, NULL);
  ReleaseTokenFSCall();

  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mknode);

  /* Fills the attributes if needed */
  if(node_attributes)
    {

      status = GPFSFSAL_getattrs(p_object_handle, p_context, node_attributes);

      /* on error, we set a special bit in the mask. */

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(node_attributes->asked_attributes);
          FSAL_SET_MASK(node_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* Finished */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mknode);

}
