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

  fsal_status_t st;
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

      status = FUSEFSAL_test_access(p_context, access_type, &tmp_attrs);

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

/**
 * FSAL_test_access :
 * Tests whether the user identified by the p_context structure
 * can access the object as indicated by the access_type parameter.
 * This function tests access rights using cached attributes
 * given as parameter (no calls to filesystem).
 * Thus, it cannot test FSAL_F_OK flag, and asking such a flag
 * will result in a ERR_FSAL_INVAL error.
 *
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param access_type (input):
 *        Indicates the permissions to test.
 *        This is an inclusive OR of the permissions
 *        to be checked for the user identified by cred.
 *        Permissions constants are :
 *        - FSAL_R_OK : test for read permission
 *        - FSAL_W_OK : test for write permission
 *        - FSAL_X_OK : test for exec permission
 *        - FSAL_F_OK : test for file existence
 * \param object_attributes (mandatory input):
 *        The cached attributes for the object to test rights on.
 *        The following attributes MUST be filled :
 *        owner, group, mode, ACLs.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error, permission granted)
 *        - ERR_FSAL_ACCESS       (object permissions doesn't fit asked access type)
 *        - ERR_FSAL_INVAL        (FSAL_test_access is not able to test such a permission)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 */
fsal_status_t FUSEFSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                                   fsal_accessflags_t access_type,      /* IN */
                                   fsal_attrib_list_t * object_attributes       /* IN */
    )
{
  fsal_accessflags_t missing_access;
  gid_t grp;
  int is_grp;
  unsigned int i;

  /* sanity checks. */

  if(!object_attributes || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_test_access);

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_test_access);

  LogFullDebug(COMPONENT_FSAL, "test_access: mode=%#o, user=%d, owner=%u",
               object_attributes->mode, p_context->credential.user,
               object_attributes->owner);

  /* test root access */

  if(p_context->credential.user == 0)
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_test_access);

  /* unsatisfied permissions */

  missing_access = FSAL_MODE_MASK(access_type); /* only modes, no ACLs here */

  /* Test if file belongs to user. */

  if(p_context->credential.user == object_attributes->owner)
    {

      if(object_attributes->mode & FSAL_MODE_RUSR)
        missing_access &= ~FSAL_R_OK;

      if(object_attributes->mode & FSAL_MODE_WUSR)
        missing_access &= ~FSAL_W_OK;

      if(object_attributes->mode & FSAL_MODE_XUSR)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_test_access);
      else
        Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_test_access);

    }

  /* Test if the file belongs to user's group. */

  is_grp = (p_context->credential.group == object_attributes->group);

  /* XXX Test here if file belongs to user's alt groups */

  /* finally apply group rights */

  if(is_grp)
    {
      if(object_attributes->mode & FSAL_MODE_RGRP)
        missing_access &= ~FSAL_R_OK;

      if(object_attributes->mode & FSAL_MODE_WGRP)
        missing_access &= ~FSAL_W_OK;

      if(object_attributes->mode & FSAL_MODE_XGRP)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_test_access);
      else
        Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_test_access);

    }

  /* test other perms */

  if(object_attributes->mode & FSAL_MODE_ROTH)
    missing_access &= ~FSAL_R_OK;

  if(object_attributes->mode & FSAL_MODE_WOTH)
    missing_access &= ~FSAL_W_OK;

  if(object_attributes->mode & FSAL_MODE_XOTH)
    missing_access &= ~FSAL_X_OK;

  /** @todo: ACLs. */

  if(missing_access == 0)
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_test_access);
  else
    Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_test_access);

}
