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

fsal_status_t HPSSFSAL_unlink(hpssfsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_object_name,      /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t * parentdir_attributes /* [IN/OUT ] */
    )
{

  fsal_status_t st;
  int rc;
  hpssfsal_handle_t obj_handle;

  /* sanity checks.
   * note : parentdir_attributes are optional.
   *        parentdir_handle is mandatory,
   *        because, we do not allow to delete FS root !
   */
  if(!parentdir_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* Action depends on the object type to be deleted.
   * To know that, we get fsal object handle.
   */
  st = HPSSFSAL_lookup(parentdir_handle,        /* IN */
                       p_object_name,   /* IN */
                       p_context,       /* IN */
                       &obj_handle,     /* OUT */
                       NULL);   /* IN/OUT */

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_unlink);

  switch (obj_handle.data.obj_type)
    {

    case FSAL_TYPE_DIR:

      /* remove a directory */

      TakeTokenFSCall();

      rc = hpss_RmdirHandle(&(parentdir_handle->data.ns_handle),
                            p_object_name->name, &(p_context->credential.hpss_usercred));

      ReleaseTokenFSCall();

      /* The EEXIST error is actually an NOTEMPTY error. */

      if(rc == EEXIST || rc == -EEXIST)
        Return(ERR_FSAL_NOTEMPTY, -rc, INDEX_FSAL_unlink);
      else if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_unlink);

      break;

    case FSAL_TYPE_LNK:
    case FSAL_TYPE_FILE:

      /* remove an object */

      TakeTokenFSCall();

      rc = hpss_UnlinkHandle(&(parentdir_handle->data.ns_handle),
                             p_object_name->name, &(p_context->credential.hpss_usercred));

      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_unlink);

      break;

    case FSAL_TYPE_JUNCTION:
      /* remove a junction */

      TakeTokenFSCall();

      rc = hpss_JunctionDeleteHandle(&(parentdir_handle->data.ns_handle),
                                     p_object_name->name,
                                     &(p_context->credential.hpss_usercred));

      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_unlink);

      break;

    case FSAL_TYPE_FIFO:
    case FSAL_TYPE_CHR:
    case FSAL_TYPE_BLK:
    case FSAL_TYPE_SOCK:
    default:
      DisplayLogJdLevel(fsal_log, NIV_CRIT, "Unexpected object type : %d\n",
                        obj_handle.data.obj_type);
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_unlink);

    }

  /* Now, we get new attributes for the parent directory,
   * if they are asked.
   */

  if(parentdir_attributes)
    {

      st = HPSSFSAL_getattrs(parentdir_handle, p_context, parentdir_attributes);

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
