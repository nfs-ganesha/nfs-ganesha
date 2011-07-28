/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_access.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:23:05 $
 * \version $Revision: 1.8 $
 * \brief   FSAL access permissions functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"

/**
 * FSAL_access :
 * Tests whether the user or entity identified by permcontext
 * can access the object identified by object_handle,
 * as indicated by the access_type parameters.
 *
 * \param object_handle (input):
 *        The handle of the object to test permissions on.
 * \param cred (input):
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
 * \param object_attributes (optional input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_ATTRNOTSUPP  (error getting unsupported post-op attributes).
 *        - ERR_FSAL_BADHANDLE    (illegal handle)
 *        - ERR_FSAL_NOT_INIT     (ghostfs not initialize)
 *        - ERR_FSAL_IO           (corrupted FS)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_access(fsal_handle_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{

  GHOSTFS_testperm_t test = 0;
  int rc;

  SetFuncID(INDEX_FSAL_access);

  /* sanity checks.
   * note : object_attributes is optionnal in FSAL_getattrs.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_access);

  /* converts fsal access type to ghostfs access type */
  test = fsal2ghost_testperm(access_type);

  /* call to GHOST_FS access */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*object_handle), test,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_access);

  /* get attributes if object_attributes is not null.
   * If an error occures during getattr operation,
   * it is returned, even though the access operation succeeded.
   */
  if(object_attributes)
    {
      fsal_status_t status;

      switch ((status = FSAL_getattrs(object_handle, p_context, object_attributes)).major)
        {
          /* change the FAULT error to appears as an internal error.
           * indeed, parameters should be null. */
        case ERR_FSAL_FAULT:
          Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_access);
          break;
        case ERR_FSAL_NO_ERROR:
          /* continue */
          break;
        default:
          Return(status.major, status.minor, INDEX_FSAL_access);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_access);

}
