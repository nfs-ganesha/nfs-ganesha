/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_create.c
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.18 $
 * \brief   Filesystem objects creation functions.
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include <unistd.h>
#include <fcntl.h>
#include <fsal_api.h>
#include "FSAL/access_check.h"

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_hdl (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param p_object_handle (output):
 *        Pointer to the handle of the created file.
 * \param p_object_attributes (optional input/output):
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
fsal_status_t GPFSFSAL_create(struct fsal_obj_handle *dir_hdl,         /* IN */
                          const char * p_filename,                     /* IN */
                          const struct req_op_context *p_context,      /* IN */
                          uint32_t accessmode,                        /* IN */
                          struct gpfs_file_handle * p_object_handle,  /* OUT */
                          struct attrlist * p_object_attributes)    /* IN/OUT */
{
  fsal_status_t status;
  int mount_fd;
  mode_t unix_mode;
  fsal_accessflags_t access_mask = 0;
  struct attrlist parent_dir_attrs;
  struct gpfs_fsal_obj_handle *gpfs_hdl;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!dir_hdl || !p_context || !p_object_handle || !p_filename)
    return fsalstat(ERR_FSAL_FAULT, 0);

  gpfs_hdl = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  mount_fd = gpfs_get_root_fd(dir_hdl->export);

  /* convert fsal mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);

  LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

  /* retrieve directory metadata */
  parent_dir_attrs.mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
  status = GPFSFSAL_getattrs(dir_hdl->export, p_context, gpfs_hdl->handle,
                             &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);

  /* Check the user can write in the directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export,
                                        fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &parent_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, gpfs_hdl->handle,
                                  access_mask, &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);
  /* call to filesystem */

  fsal_set_credentials(p_context->creds);
  status = fsal_internal_create(mount_fd, gpfs_hdl->handle,
                                p_filename, unix_mode | S_IFREG, 0,
                                p_object_handle, NULL);
  fsal_restore_ganesha_credentials();
  if(FSAL_IS_ERROR(status))
    return(status);

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(dir_hdl->export, p_context, p_object_handle,
                                 p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->mask);
          FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
        }

    }

  // error injection to test DRC
  //sleep(61);
  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_mkdir:
 * Create a directory.
 *
 * \param dir_hdl (input):
 *        Handle of the parent directory where
 *        the subdirectory is to be created.
 * \param p_dirname (input):
 *        Pointer to the name of the directory to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param p_object_handle (output):
 *        Pointer to the handle of the created directory.
 * \param p_object_attributes (optionnal input/output):
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
fsal_status_t GPFSFSAL_mkdir(struct fsal_obj_handle *dir_hdl,            /* IN */
                         const char * p_dirname,                         /* IN */
                         const struct req_op_context *p_context,          /* IN */
                         uint32_t accessmode,                           /* IN */
                         struct gpfs_file_handle * p_object_handle,     /* OUT */
                         struct attrlist * p_object_attributes)      /* IN/OUT */
{
/*   int setgid_bit = 0; */
  mode_t unix_mode;
  fsal_status_t status;
  fsal_accessflags_t access_mask = 0;
  struct attrlist parent_dir_attrs;
  int mount_fd;
  struct gpfs_fsal_obj_handle *gpfs_hdl;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!dir_hdl || !p_context || !p_object_handle || !p_dirname)
    return fsalstat(ERR_FSAL_FAULT, 0);

  gpfs_hdl = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);

  mount_fd = gpfs_get_root_fd(dir_hdl->export);

  /* convert FSAL mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);

  /* get directory metadata */
  parent_dir_attrs.mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
  status = GPFSFSAL_getattrs(dir_hdl->export, p_context, gpfs_hdl->handle,
                             &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);

  /* Check the user can write in the directory, and check the setgid bit on the directory */

/*   if(fsal2unix_mode(parent_dir_attrs.mode) & S_ISGID) */
/*     setgid_bit = 1; */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_SUBDIRECTORY);

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &parent_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, gpfs_hdl->handle,
                                  access_mask, &parent_dir_attrs);
    if(FSAL_IS_ERROR(status))
      return(status);
  /* build new entry path */

  /* creates the directory and get its handle */

  fsal_set_credentials(p_context->creds);
  status = fsal_internal_create(mount_fd, gpfs_hdl->handle,
                                p_dirname, unix_mode | S_IFDIR, 0,
                                p_object_handle, NULL);
  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
    return(status);

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(dir_hdl->export, p_context, p_object_handle,
                                 p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->mask);
          FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
        }
    }
  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_link:
 * Create a hardlink.
 *
 * \param destdir_hdl (input):
 *        Handle of the target object.
 * \param target_handle (input):
 *        Pointer to the directory handle where
 *        the hardlink is to be created.
 * \param p_link_name (input):
 *        Pointer to the name of the hardlink to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_attributes (optionnal input/output):
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
fsal_status_t GPFSFSAL_link(struct fsal_obj_handle *destdir_hdl,   /* IN */
                        struct gpfs_file_handle  *target_handle,   /* IN */
                        const char  *p_link_name,                  /* IN */
                        const struct req_op_context *p_context,     /* IN */
                        struct attrlist * p_attributes)            /* IN/OUT */
{
  int mount_fd;
  fsal_status_t status;
  fsal_accessflags_t access_mask = 0;
  struct attrlist parent_dir_attrs;
  struct gpfs_fsal_obj_handle *dest_dir;

  /* sanity checks.
   * note : attributes is optional.
   */
  if(!destdir_hdl || !target_handle || !p_context || !p_link_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  dest_dir = container_of(destdir_hdl, struct gpfs_fsal_obj_handle, obj_handle);

  mount_fd = gpfs_get_root_fd(destdir_hdl->export);

  /* Tests if hardlinking is allowed by configuration. */

  if(!destdir_hdl->export->ops->fs_supports(destdir_hdl->export, fso_link_support))
    return fsalstat(ERR_FSAL_NOTSUPP, 0);

  /* retrieve target directory metadata */
  parent_dir_attrs.mask = destdir_hdl->export->ops->fs_supported_attrs(destdir_hdl->export);
  status = GPFSFSAL_getattrs(destdir_hdl->export, p_context, dest_dir->handle,
                             &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
      goto out_status_fsal_err;

  /* check permission on target directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);

  if(!destdir_hdl->export->ops->fs_supports(destdir_hdl->export,
                                            fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &parent_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, dest_dir->handle,
                                  access_mask, &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    goto out_status_fsal_err;

  /* Create the link on the filesystem */

  fsal_set_credentials(p_context->creds);
  status = fsal_internal_link_fh(mount_fd, target_handle, dest_dir->handle,
                                 p_link_name);

  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
    goto out_status_fsal_err;

  /* optionnaly get attributes */

  if(p_attributes)
    {
      status = GPFSFSAL_getattrs(destdir_hdl->export, p_context, target_handle,
                                 p_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_attributes->mask);
          FSAL_SET_MASK(p_attributes->mask, ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

out_status_fsal_err:

  return(status);
}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 *
 * \param dir_hdl (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_node_name (input):
 *        Pointer to the name of the file to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param nodetype (input):
 *        Type of file to create.
 * \param dev (input):
 *        Device id of file to create.
 * \param p_object_handle (output):
 *        Pointer to the handle of the created file.
 * \param p_object_attributes (optional input/output):
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
 *
 */
fsal_status_t GPFSFSAL_mknode(struct fsal_obj_handle *dir_hdl,       /* IN */
                          const char * p_node_name,                  /* IN */
                          const struct req_op_context *p_context,     /* IN */
                          uint32_t accessmode,                       /* IN */
                          mode_t nodetype,                           /* IN */
                          fsal_dev_t * dev,                          /* IN */
                          struct gpfs_file_handle * p_object_handle, /* OUT */
                          struct attrlist * node_attributes          /* IN/OUT */
    )
{
  fsal_status_t status;
/*   int flags=(O_RDONLY|O_NOFOLLOW); */
  int mount_fd;

  mode_t unix_mode = 0;
  dev_t unix_dev = 0;
  fsal_accessflags_t access_mask = 0;
  struct attrlist parent_dir_attrs;
  struct gpfs_fsal_obj_handle *gpfs_hdl;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!dir_hdl || !p_context || !p_node_name)
    return fsalstat(ERR_FSAL_FAULT, 0);

  gpfs_hdl = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  mount_fd = gpfs_get_root_fd(dir_hdl->export);

  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~dir_hdl->export->ops->fs_umask(dir_hdl->export);

  switch (nodetype)
    {
    case BLOCK_FILE:
      if(!dev)
        return fsalstat(ERR_FSAL_FAULT, 0);
      unix_mode |= S_IFBLK;
      unix_dev = (dev->major << 8) | (dev->minor & 0xFF);
      break;

    case CHARACTER_FILE:
      if(!dev)
        return fsalstat(ERR_FSAL_FAULT, 0);
      unix_mode |= S_IFCHR;
      unix_dev = (dev->major << 8) | (dev->minor & 0xFF);
      break;

    case SOCKET_FILE:
      unix_mode |= S_IFSOCK;
      break;

    case FIFO_FILE:
      unix_mode |= S_IFIFO;
/*       flags = (O_RDONLY | O_NOFOLLOW | O_NONBLOCK); */
      break;

    default:
      LogMajor(COMPONENT_FSAL,
               "Invalid node type in FSAL_mknode: %d", nodetype);
      return fsalstat(ERR_FSAL_INVAL, 0);
    }

  /* retrieve directory attributes */
  parent_dir_attrs.mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
  status = GPFSFSAL_getattrs(dir_hdl->export, p_context, gpfs_hdl->handle,
                             &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);

  /* Check the user can write in the directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &parent_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, gpfs_hdl->handle,
                                  access_mask, &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);

  fsal_set_credentials(p_context->creds);
  status = fsal_internal_create(mount_fd, gpfs_hdl->handle,
                                p_node_name, unix_mode, unix_dev,
                                p_object_handle, NULL);

  fsal_restore_ganesha_credentials();

  if(FSAL_IS_ERROR(status))
    return(status);

  /* Fills the attributes if needed */
  if(node_attributes)
    {

      status = GPFSFSAL_getattrs(dir_hdl->export, p_context, gpfs_hdl->handle,
                                 node_attributes);

      /* on error, we set a special bit in the mask. */

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(node_attributes->mask);
          FSAL_SET_MASK(node_attributes->mask, ATTR_RDATTR_ERR);
        }

    }

  /* Finished */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
