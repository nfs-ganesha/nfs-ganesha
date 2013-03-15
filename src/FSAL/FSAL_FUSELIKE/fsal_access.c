/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_access.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.16 $
 * \brief   FSAL access permissions functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"

/**
 * FSAL_access :
 * Tests whether the user or entity identified by the p_context structure
 * can access the object identified by object_handle,
 * as indicated by the access_type parameter.
 *
 * \param object_handle (input):
 *        The handle of the object to test permissions on.
 * \param p_context (input):
 *        Authentication context for the operation (export entry, user,...).
 * \param access_type (input):
 *        Indicates the permissions to be tested.
 *        This is an inclusive OR of the permissions
 *        to be checked for the user specified by p_context.
 *        Permissions constants are :
 *        - FSAL_R_OK : test for read permission
 *        - FSAL_W_OK : test for write permission
 *        - FSAL_X_OK : test for exec permission
 *        - FSAL_F_OK : test for file existence
 * \param object_attributes (optional input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error, asked permission is granted)
 *        - ERR_FSAL_ACCESS       (object permissions doesn't fit asked access type)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes when something anormal occurs.
 */
fsal_status_t FUSEFSAL_access(fsal_handle_t * obj_handle,        /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessflags_t access_type,   /* IN */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int mask, rc;
  char object_path[FSAL_MAX_PATH_LEN];
  fsal_attrib_list_t tmp_attrs;
  fusefsal_handle_t * object_handle = (fusefsal_handle_t *)obj_handle;

  /* sanity checks.
   * note : object_attributes is optional in FSAL_access.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_access);

  /* convert fsal access mask to FUSE access mask */
  mask = fsal2posix_testperm(access_type);

  /* get the full path for the object */
  rc = NamespacePath(object_handle->data.inode, object_handle->data.device,
                     object_handle->data.validator, object_path);

  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_access);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  if(p_fs_ops->access)
    {

      TakeTokenFSCall();

      rc = p_fs_ops->access(object_path, mask);

      ReleaseTokenFSCall();

      /* TODO : remove entry from namespace if entry is stale */
      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_access);
    }
  else if(p_fs_ops->getattr)
    {
      /* we emulate 'access' call using getattr + fsal_test_access
       * from attributes value (mode, owner, group, ...)
       */
      fsal_status_t status;

      FSAL_CLEAR_MASK(tmp_attrs.asked_attributes);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_TYPE);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_MODE);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_OWNER);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_GROUP);

      status = FUSEFSAL_getattrs(obj_handle, p_context, &tmp_attrs);

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_access);

      status = FSAL_test_access(p_context, access_type, &tmp_attrs);

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_access);

    }
  /* else : always grant access */

  /* get attributes if object_attributes is not null.
   * If an error occures during getattr operation,
   * an error bit is set in the output structure.
   */
  if(object_attributes)
    {
      fsal_status_t status;

      status = FUSEFSAL_getattrs(obj_handle, p_context, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_access);

}
