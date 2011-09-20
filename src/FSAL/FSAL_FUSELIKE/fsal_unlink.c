/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object removing function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"
#include "fsal_common.h"

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

fsal_status_t FUSEFSAL_unlink(fsal_handle_t * parent,     /* IN */
                              fsal_name_t * p_object_name,      /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t * parentdir_attributes /* [IN/OUT ] */
    )
{

  int rc;
  fsal_status_t st;
  fusefsal_handle_t obj_handle;
  char parent_path[FSAL_MAX_PATH_LEN];
  char child_path[FSAL_MAX_PATH_LEN];
  struct stat stbuff;
  fusefsal_handle_t * parentdir_handle = (fusefsal_handle_t *)parent;

  /* sanity checks.
   * note : parentdir_attributes are optional.
   *        parentdir_handle is mandatory,
   *        because, we do not allow to delete FS root !
   */
  if(!parentdir_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* set current FS context */
  fsal_set_thread_context(p_context);

  /* get parent directory path */

  rc = NamespacePath(parentdir_handle->data.inode,
                     parentdir_handle->data.device, parentdir_handle->data.validator, parent_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_unlink);

  /* We have to know what type of entry it is,
   * to switch between "unlink" and "rmdir".
   *
   * To do this, do a getattr.
   */
  FSAL_internal_append_path(child_path, parent_path, p_object_name->name);

  TakeTokenFSCall();
  rc = p_fs_ops->getattr(child_path, &stbuff);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, FALSE), rc, INDEX_FSAL_unlink);

  /* check type */

  if(posix2fsal_type(stbuff.st_mode) == FSAL_TYPE_DIR)
    {
      /* proceed rmdir call */

      /* operation not provided by filesystem */
      if(!p_fs_ops->rmdir)
        Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlink);

      TakeTokenFSCall();
      rc = p_fs_ops->rmdir(child_path);
      ReleaseTokenFSCall();

    }
  else
    {
      /* proceed unlink call */

      /* operation not provided by filesystem */
      if(!p_fs_ops->unlink)
        Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlink);

      TakeTokenFSCall();
      rc = p_fs_ops->unlink(child_path);
      ReleaseTokenFSCall();

    }

  if(rc == 0 || rc == -ENOENT)
    {
      /* remove the entry from namespace */
      NamespaceRemove(parentdir_handle->data.inode, parentdir_handle->data.device,
                      parentdir_handle->data.validator, p_object_name->name);
    }

  if(rc)
    Return(fuse2fsal_error(rc, FALSE), rc, INDEX_FSAL_unlink);

  if(parentdir_attributes)
    {
      st = FUSEFSAL_getattrs(parentdir_handle, p_context, parentdir_attributes);

      /* On error, we set a flag in the returned attributes */

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(parentdir_attributes->asked_attributes);
          FSAL_SET_MASK(parentdir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
