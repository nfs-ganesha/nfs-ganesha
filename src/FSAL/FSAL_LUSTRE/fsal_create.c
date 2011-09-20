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
fsal_status_t LUSTREFSAL_create(fsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_filename,       /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                fsal_handle_t * p_object_handle,  /* OUT */
                                fsal_attrib_list_t * p_object_attributes        /* [ IN/OUT ] */
    )
{

  int rc, fd, errsv;
  int setgid_bit = 0;
  fsal_status_t status;

  fsal_path_t fsalpath;
  struct stat buffstat;
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

  /* build the destination path */
  status = fsal_internal_Handle2FidPath(p_context, p_parent_directory_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_create);

  /* retrieve directory metadata */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_create);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
    }

  /* Check the user can write in the directory, and check the setgid bit on the directory */

  if(buffstat.st_mode & S_ISGID)
    setgid_bit = 1;

  status = fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_create);

  /* build new entry path */
  status = fsal_internal_appendNameToPath(&fsalpath, p_filename);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_create);

  /* call to filesystem */

  TakeTokenFSCall();
  /* create the file.
   * O_EXCL=>  error if the file already exists */
  fd = open(fsalpath.path, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, unix_mode);
  errsv = errno;

  if(fd == -1)
    {
      ReleaseTokenFSCall();
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
    }

  /* close the file descriptor */
  rc = close(fd);
  errsv = errno;
  if(rc)
    {
      ReleaseTokenFSCall();
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
    }

  /* get the new file handle */
  status = fsal_internal_Path2Handle(p_context, &fsalpath, p_object_handle);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_create);

  /* the file has been created */
  /* chown the file to the current user */

  if(p_context->credential.user != geteuid())
    {
      TakeTokenFSCall();
      /* if the setgid_bit was set on the parent directory, do not change the group of the created file, because it's already the parentdir's group */
      rc = lchown(fsalpath.path, p_context->credential.user,
                  setgid_bit ? -1 : (int)p_context->credential.group);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
    }

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = LUSTREFSAL_getattrs(p_object_handle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

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
fsal_status_t LUSTREFSAL_mkdir(fsal_handle_t * p_parent_directory_handle, /* IN */
                               fsal_name_t * p_dirname, /* IN */
                               fsal_op_context_t * p_context,     /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * p_object_handle,   /* OUT */
                               fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  int setgid_bit = 0;
  struct stat buffstat;
  mode_t unix_mode;
  fsal_status_t status;
  fsal_path_t fsalpath;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context || !p_object_handle || !p_dirname)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  /* convert FSAL mode to HPSS mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  /* build the destination path */
  status = fsal_internal_Handle2FidPath(p_context, p_parent_directory_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mkdir);

  /* get directory metadata */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_create);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_create);
    }

  /* Check the user can write in the directory, and check the setgid bit on the directory */

  if(buffstat.st_mode & S_ISGID)
    setgid_bit = 1;

  status = fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mkdir);

  /* build new entry path */

  status = fsal_internal_appendNameToPath(&fsalpath, p_dirname);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mkdir);

  /* creates the directory and get its handle */

  TakeTokenFSCall();
  rc = mkdir(fsalpath.path, unix_mode);
  errsv = errno;
  if(rc)
    {
      ReleaseTokenFSCall();
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mkdir);
    }

  /* get the new handle */
  status = fsal_internal_Path2Handle(p_context, &fsalpath, p_object_handle);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mkdir);

  /* the directory has been created */
  /* chown the file to the current user/group */

  if(p_context->credential.user != geteuid())
    {
      TakeTokenFSCall();
      /* if the setgid_bit was set on the parent directory, do not change the group of the created file, because it's already the parentdir's group */
      rc = lchown(fsalpath.path, p_context->credential.user,
                  setgid_bit ? -1 : (int)p_context->credential.group);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mkdir);
    }

  /* retrieve file attributes */
  if(p_object_attributes)
    {
      status = LUSTREFSAL_getattrs(p_object_handle, p_context, p_object_attributes);

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
fsal_status_t LUSTREFSAL_link(fsal_handle_t * p_target_handle,    /* IN */
                              fsal_handle_t * p_dir_handle,       /* IN */
                              fsal_name_t * p_link_name,        /* IN */
                              fsal_op_context_t * p_context,      /* IN */
                              fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;
  fsal_path_t fsalpath_old, fsalpath_new;
  struct stat buffstat_dir;

  /* sanity checks.
   * note : attributes is optional.
   */
  if(!p_target_handle || !p_dir_handle || !p_context || !p_link_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  /* Tests if hardlinking is allowed by configuration. */

  if(!global_fs_info.link_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);

/*  LogFullDebug(COMPONENT_FSAL, "linking %#llx:%#x:%#x to %#llx:%#x:%#x/%s \n", */

  /* get the target handle access by fid */
  status = fsal_internal_Handle2FidPath(p_context, p_target_handle, &fsalpath_old);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_link);

  /* build the destination path and check permissions on the directory */
  status = fsal_internal_Handle2FidPath(p_context, p_dir_handle, &fsalpath_new);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_link);

  /* retrieve target directory metadata */

  TakeTokenFSCall();
  rc = lstat(fsalpath_new.path, &buffstat_dir);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_link);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_link);
    }

  /* check permission on target directory */
  status =
      fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &buffstat_dir, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_link);

  /* build the new entry path */
  status = fsal_internal_appendNameToPath(&fsalpath_new, p_link_name);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_link);

  /* Create the link on the filesystem */

  TakeTokenFSCall();
  rc = link(fsalpath_old.path, fsalpath_new.path);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_link);

  /* optionnaly get attributes */

  if(p_attributes)
    {
      status = LUSTREFSAL_getattrs(p_target_handle, p_context, p_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_attributes->asked_attributes);
          FSAL_SET_MASK(p_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);

}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported upon HPSS.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t LUSTREFSAL_mknode(fsal_handle_t * parentdir_handle, /* IN */
                                fsal_name_t * p_node_name,      /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                fsal_nodetype_t nodetype,       /* IN */
                                fsal_dev_t * dev,       /* IN */
                                fsal_handle_t * p_object_handle,  /* OUT (handle to the created node) */
                                fsal_attrib_list_t * node_attributes    /* [ IN/OUT ] */
    )
{
  int rc, errsv;
  int setgid_bit = 0;
  struct stat buffstat;
  fsal_status_t status;
  fsal_path_t fsalpath;

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
      break;

    default:
      LogMajor(COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d",
                        nodetype);
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_mknode);
    }

  /* build the directory path */
  status = fsal_internal_Handle2FidPath(p_context, parentdir_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mknode);

  /* retrieve directory attributes */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_mknode);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mknode);
    }

  /* Check the user can write in the directory, and check weither the setgid bit on the directory */
  if(buffstat.st_mode & S_ISGID)
    setgid_bit = 1;

  status = fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mknode);

  status = fsal_internal_appendNameToPath(&fsalpath, p_node_name);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mknode);

  /* creates the node, then stats it */
  TakeTokenFSCall();
  rc = mknod(fsalpath.path, unix_mode, unix_dev);
  errsv = errno;

  if(rc)
    {
      ReleaseTokenFSCall();
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mknode);
    }

  /* get the new object handle */
  status = fsal_internal_Path2Handle(p_context, &fsalpath, p_object_handle);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_mknode);

  /* the node has been created */
  /* chown the file to the current user/group */

  if(p_context->credential.user != geteuid())
    {
      TakeTokenFSCall();

      /* if the setgid_bit was set on the parent directory, do not change the group of the created file, because it's already the parentdir's group */
      rc = lchown(fsalpath.path, p_context->credential.user,
                  setgid_bit ? -1 : (int)p_context->credential.group);
      errsv = errno;

      ReleaseTokenFSCall();

      if(rc)
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_mknode);
    }

  /* Fills the attributes if needed */
  if(node_attributes)
    {

      status = LUSTREFSAL_getattrs(p_object_handle, p_context, node_attributes);

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
