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

#include <hpss_errno.h>

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
fsal_status_t HPSSFSAL_access(hpssfsal_handle_t * object_handle,        /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessflags_t access_type,   /* IN */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int hpss_test_mode = 0;
  int rc;
  fsal_status_t st;

  /* sanity checks.
   * note : object_attributes is optionnal in FSAL_getattrs.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_access);

  /* converts fsal access type to hpss access type */

  hpss_test_mode = fsal2hpss_testperm(access_type);

  /* call to HPSS access */

  TakeTokenFSCall();

  rc = hpss_AccessHandle(&(object_handle->data.ns_handle),   /* IN - parent object handle */
                         NULL,  /* IN - path of file to check access rights */
                         hpss_test_mode,        /* IN - Type of access to be checked */
                         &p_context->credential.hpss_usercred   /* IN - user credentials */
#if HPSS_MAJOR_VERSION < 7
                         , NULL /* OUT - authorization ticket */
#endif
      );

  ReleaseTokenFSCall();

  /* convert returned code */
  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_access);
  else if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_access);

  /* get attributes if object_attributes is not null.
   * If an error occures during getattr operation,
   * it is returned, even though the access operation succeeded.
   */
  if(object_attributes)
    {
      fsal_status_t status;

      status = FSAL_getattrs(object_handle, p_context, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_access);

}
