/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_rename.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object renaming/moving function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

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

fsal_status_t FSAL_rename(fsal_handle_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes       /* [ IN/OUT ] */
    )
{

  int rc;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_parentdir_handle ||
     !new_parentdir_handle || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  TakeTokenFSCall();

  /* >> call your filesystem rename function << */

  ReleaseTokenFSCall();

  /* >> interpret the returned error << */

  /* >> get last parent post op attributes if asked
   * For example : << */

  if(src_dir_attributes)
    {
      fsal_status_t st;

      st = FSAL_getattrs(old_parentdir_handle, p_context, src_dir_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(src_dir_attributes->asked_attributes);
          FSAL_SET_MASK(src_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* >> get new parent post op attributes if asked
   * For example : << */

  if(tgt_dir_attributes)
    {
      fsal_status_t st;

      /* optimization when src=tgt : */

      if(!FSAL_handlecmp(old_parentdir_handle, new_parentdir_handle, &st)
         && src_dir_attributes)
        {

          /* If source dir = target dir, we just copy the attributes.
           * to avoid doing another getattr.
           */

          (*tgt_dir_attributes) = (*src_dir_attributes);

        }
      else
        {

          /* get attributes */
          st = FSAL_getattrs(new_parentdir_handle, p_context, tgt_dir_attributes);

          if(FSAL_IS_ERROR(st))
            {
              FSAL_CLEAR_MASK(tgt_dir_attributes->asked_attributes);
              FSAL_SET_MASK(tgt_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }

        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
