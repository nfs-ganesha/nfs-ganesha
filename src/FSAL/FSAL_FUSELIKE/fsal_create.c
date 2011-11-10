/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "fsal_common.h"
#include "namespace.h"

#include <string.h>

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
fsal_status_t FUSEFSAL_create(fsal_handle_t * parent_handle,      /* IN */
                              fsal_name_t * p_filename, /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_handle_t * obj_handle,        /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int rc;
  char parent_path[FSAL_MAX_PATH_LEN];
  char child_path[FSAL_MAX_PATH_LEN];
  struct stat buffstat;
  mode_t mode;
  struct fuse_file_info dummy;
  fusefsal_handle_t * parent_directory_handle = (fusefsal_handle_t *)parent_handle;
  fusefsal_handle_t * object_handle = (fusefsal_handle_t *)obj_handle;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle || !p_filename)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* convert arguments */
  mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  mode = mode & ~global_fs_info.umask;

  /* get the full path for parent inode */
  rc = NamespacePath(parent_directory_handle->data.inode,
                     parent_directory_handle->data.device,
                     parent_directory_handle->data.validator, parent_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_create);

  /* append child name to parent path */
  FSAL_internal_append_path(child_path, parent_path, p_filename->name);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  /* Call create + close, if implemented.
   * else, try with mknod.
   */
  if(p_fs_ops->create)
    {
      /* initialize dummy file info */
      memset(&dummy, 0, sizeof(struct fuse_file_info));

      dummy.flags = O_CREAT | O_EXCL;

      LogFullDebug(COMPONENT_FSAL, "Call to create( %s, %#o, %#X )", child_path, mode, dummy.flags);

      TakeTokenFSCall();
      rc = p_fs_ops->create(child_path, mode, &dummy);
      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_create);

      if(p_fs_ops->release)
        {
          /* don't worry about release return code,
           * since the object was created */
          TakeTokenFSCall();
          p_fs_ops->release(child_path, &dummy);
          ReleaseTokenFSCall();
        }

    }
  else if(p_fs_ops->mknod)
    {
      /* prepare mode including IFREG mask */
      mode |= S_IFREG;

      TakeTokenFSCall();
      rc = p_fs_ops->mknod(child_path, mode, 0);
      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_create);

    }
  else
    {
      /* operation not supported */
      Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_create);
    }

  /* set the owner for the newly created entry */

  if(p_fs_ops->chown)
    {
      TakeTokenFSCall();
      rc = p_fs_ops->chown(child_path, p_context->credential.user,
                           p_context->credential.group);
      ReleaseTokenFSCall();

      LogFullDebug(COMPONENT_FSAL, "chown: status = %d", rc);

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_create);
    }

  /* lookup for the newly created entry */

  TakeTokenFSCall();
  rc = p_fs_ops->getattr(child_path, &buffstat);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_create);

  object_handle->data.validator = buffstat.st_ctime;

  /* add handle to namespace */
  NamespaceAdd(parent_directory_handle->data.inode,
               parent_directory_handle->data.device,
               parent_directory_handle->data.validator,
               p_filename->name,
               buffstat.st_ino, buffstat.st_dev, &object_handle->data.validator);

  /* set output handle */
  object_handle->data.inode = buffstat.st_ino;
  object_handle->data.device = buffstat.st_dev;

  if(object_attributes)
    {
      fsal_status_t status = posix2fsal_attributes(&buffstat, object_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
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
fsal_status_t FUSEFSAL_mkdir(fsal_handle_t * parent_handle,       /* IN */
                             fsal_name_t * p_dirname,   /* IN */
                             fsal_op_context_t * p_context, /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_handle_t * obj_handle, /* OUT */
                             fsal_attrib_list_t * object_attributes     /* [ IN/OUT ] */
    )
{

  int rc;
  char parent_path[FSAL_MAX_PATH_LEN];
  char child_path[FSAL_MAX_PATH_LEN];
  struct stat buffstat;
  mode_t mode;
  fusefsal_handle_t * parent_directory_handle = (fusefsal_handle_t *)parent_handle;
  fusefsal_handle_t * object_handle = (fusefsal_handle_t *)obj_handle;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle || !p_dirname)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  /* Test if mkdir is allowed */
  if(!p_fs_ops->mkdir)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_mkdir);

  /* convert arguments */
  mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  mode = mode & ~global_fs_info.umask;

  /* get the full path for parent inode */
  rc = NamespacePath(parent_directory_handle->data.inode,
                     parent_directory_handle->data.device,
                     parent_directory_handle->data.validator, parent_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_mkdir);

  /* append child name to parent path */
  FSAL_internal_append_path(child_path, parent_path, p_dirname->name);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  TakeTokenFSCall();
  rc = p_fs_ops->mkdir(child_path, mode);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_mkdir);

  /* set the owner for the newly created entry */

  if(p_fs_ops->chown)
    {
      TakeTokenFSCall();
      rc = p_fs_ops->chown(child_path, p_context->credential.user,
                           p_context->credential.group);
      ReleaseTokenFSCall();

      LogFullDebug(COMPONENT_FSAL, "chown: status = %d", rc);

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_mkdir);
    }

  TakeTokenFSCall();
  rc = p_fs_ops->getattr(child_path, &buffstat);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_mkdir);

  object_handle->data.validator = buffstat.st_ctime;

  /* add handle to namespace */
  NamespaceAdd(parent_directory_handle->data.inode,
               parent_directory_handle->data.device,
               parent_directory_handle->data.validator,
               p_dirname->name,
               buffstat.st_ino, buffstat.st_dev, &object_handle->data.validator);

  /* set output handle */
  object_handle->data.inode = buffstat.st_ino;
  object_handle->data.device = buffstat.st_dev;

  if(object_attributes)
    {
      fsal_status_t status = posix2fsal_attributes(&buffstat, object_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
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
fsal_status_t FUSEFSAL_link(fsal_handle_t * target,  /* IN */
                            fsal_handle_t * dir_hdl,     /* IN */
                            fsal_name_t * p_link_name,  /* IN */
                            fsal_op_context_t * p_context,  /* IN */
                            fsal_attrib_list_t * attributes     /* [ IN/OUT ] */
    )
{

  int rc;
  char parent_path[FSAL_MAX_PATH_LEN];
  char child_path[FSAL_MAX_PATH_LEN];
  char target_path[FSAL_MAX_PATH_LEN];
  struct stat buffstat;
  unsigned int new_validator;
  fusefsal_handle_t * dir_handle = (fusefsal_handle_t *)dir_hdl;
  fusefsal_handle_t * target_handle = (fusefsal_handle_t *)target;

  /* sanity checks.
   * note : attributes is optional.
   */
  if(!target_handle || !dir_handle || !p_context || !p_link_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  /* Tests if hardlinking is allowed by configuration. */

  if(!global_fs_info.link_support || !p_fs_ops->link)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);

  LogFullDebug(COMPONENT_FSAL, "linking %lX.%lu/%s to %lX.%lu",
          dir_handle->data.device, dir_handle->data.inode, p_link_name->name,
          target_handle->data.device, target_handle->data.inode);

  /* get target inode path */
  rc = NamespacePath(target_handle->data.inode,
                     target_handle->data.device, target_handle->data.validator, target_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_link);

  /* get new directory path */
  rc = NamespacePath(dir_handle->data.inode,
                     dir_handle->data.device, dir_handle->data.validator, parent_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_link);

  /* append child name to parent path */
  FSAL_internal_append_path(child_path, parent_path, p_link_name->name);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  TakeTokenFSCall();
  rc = p_fs_ops->link(target_path, child_path);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_link);

  new_validator = target_handle->data.validator;

  /* add this hardlink to namespace */
  NamespaceAdd(dir_handle->data.inode,
               dir_handle->data.device,
               dir_handle->data.validator,
               p_link_name->name,
               target_handle->data.inode, target_handle->data.device, &new_validator);

  if(new_validator != target_handle->data.validator)
    {
      LogMajor(COMPONENT_FSAL,
               "A wrong behaviour has been detected is FSAL_link: An object and its hardlink don't have the same generation id");
    }

  if(attributes)
    {
      fsal_status_t st;

      st = FUSEFSAL_getattrs(target_handle, p_context, attributes);

      /* On error, we set a flag in the returned attributes */

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(attributes->asked_attributes);
          FSAL_SET_MASK(attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);

}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported in upper layers in this GANESHA's version.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t FUSEFSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_node_name,        /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_nodetype_t nodetype, /* IN */
                              fsal_dev_t * dev, /* IN */
                              fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                              fsal_attrib_list_t * node_attributes      /* [ IN/OUT ] */
    )
{

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parentdir_handle || !p_context || !nodetype || !dev || !p_node_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  /* Not implemented */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_mknode);

}
