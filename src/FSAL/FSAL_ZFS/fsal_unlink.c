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

fsal_status_t ZFSFSAL_unlink(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * parentdir_attributes     /* [IN/OUT ] */
    )
{

  fsal_status_t st;
  int rc, type;
  inogen_t object;
  creden_t cred;

  /* sanity checks.
   * note : parentdir_attributes are optional.
   *        parentdir_handle is mandatory,
   *        because, we do not allow to delete FS root !
   */
  if(!parentdir_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* Hook to prevent removing anything from snapshots */
  if(((zfsfsal_handle_t *)parentdir_handle)->data.i_snap != 0)
  {
    LogDebug(COMPONENT_FSAL, "Trying to remove an object from a snapshot");
    Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_unlink);
  }

  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  TakeTokenFSCall();

  if(!(rc = libzfswrap_lookup(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs, &cred,
                              ((zfsfsal_handle_t *)parentdir_handle)->data.zfs_handle,
			      p_object_name->name, &object, &type)))
  {
    if(type == S_IFDIR)
      rc = libzfswrap_rmdir(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs, &cred,
			    ((zfsfsal_handle_t *)parentdir_handle)->data.zfs_handle, p_object_name->name);
    else
      rc = libzfswrap_unlink(((zfsfsal_op_context_t *)p_context)->export_context->p_vfs, &cred,
			     ((zfsfsal_handle_t *)parentdir_handle)->data.zfs_handle, p_object_name->name);
  }

  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_unlink);

  /* >> get post op attributes for the parent, if they are asked,
   * and your filesystem didn't return them << */

  if(parentdir_attributes)
    {

      st = ZFSFSAL_getattrs(parentdir_handle, p_context, parentdir_attributes);

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
