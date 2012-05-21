// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_access.c
// Description: FSAL access operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_access.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pt_ganesha.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"


/**
 * FSAL_access :
 * Tests whether the user or entity identified by its cred
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
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_access(fsal_handle_t      * p_object_handle,     /* IN */
		fsal_op_context_t  * p_context,           /* IN */
		fsal_accessflags_t   access_type,         /* IN */
		fsal_attrib_list_t * p_object_attributes  /* [ IN/OUT ] */)
{

  fsal_status_t status;
  ptfsal_handle_t * p_fsi_handle = (ptfsal_handle_t *)p_object_handle;

  /* sanity checks.
   * note : object_attributes is optionnal in PTFSAL_getattrs.
   */
  if(!p_object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_access);

  /*
   * If an error occures during getattr operation,
   * it is returned, even though the access operation succeeded.
   */

  if(p_object_attributes)
    {

      FSI_TRACE(FSI_DEBUG, "FSI - fsal_access for handle %s\n", p_fsi_handle->data.handle.f_handle );

      p_object_attributes->asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
      status = PTFSAL_getattrs(p_object_handle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
          Return(status.major, status.minor, INDEX_FSAL_access);
        }

      status = fsal_internal_access(p_context, p_object_handle, access_type,
                                    p_object_attributes);

    }
  else
    { 
      /* p_object_attributes is NULL */
      fsal_attrib_list_t attrs;

      FSI_TRACE(FSI_DEBUG, "FSI - fsal_access null %s \n", p_fsi_handle->data.handle.f_handle );

      FSAL_CLEAR_MASK(attrs.asked_attributes);
      attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;

      status = PTFSAL_getattrs(p_object_handle, p_context, &attrs);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_access);

      status = fsal_internal_access(p_context, p_object_handle, access_type, &attrs);
    }

  Return(status.major, status.minor, INDEX_FSAL_access);

}
