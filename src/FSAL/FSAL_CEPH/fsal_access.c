/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

/**
 *
 * \file    fsal_access.c
 * \brief   FSAL access permissions functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_access :
 * Tests whether the user or entity identified by the context structure
 * can access the object identified by filehandle
 * as indicated by the access_type parameter.
 *
 * \param filehandle (input):
 *        The handle of the object to test permissions on.
 * \param context (input):
 *        Authentication context for the operation (export entry, user,...).
 * \param access_type (input):
 *        Indicates the permissions to be tested.
 *        This is an inclusive OR of the permissions
 *        to be checked for the user specified by context.
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
fsal_status_t CEPHFSAL_access(fsal_handle_t * exthandle,
                              fsal_op_context_t * extcontext,
                              fsal_accessflags_t access_type,
                              fsal_attrib_list_t * object_attributes)
{
  fsal_status_t status;

  /* sanity checks.
   * note : object_attributes is optionnal in VFSFSAL_getattrs.
   */
  if(!exthandle || !extcontext)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_access);

  /*
   * If an error occures during getattr operation,
   * it is returned, even though the access operation succeeded.
   */

  if(object_attributes)
    {
      FSAL_SET_MASK(object_attributes->asked_attributes,
                    FSAL_ATTR_OWNER | FSAL_ATTR_GROUP |
                    FSAL_ATTR_ACL | FSAL_ATTR_MODE);
      status = CEPHFSAL_getattrs(exthandle, extcontext, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
          Return(status.major, status.minor, INDEX_FSAL_access);
        }

      status =
           fsal_internal_testAccess((cephfsal_op_context_t*) extcontext,
                                    access_type, NULL,
                                    object_attributes);
    }
  else
    {                           /* p_object_attributes is NULL */
      fsal_attrib_list_t attrs;

      FSAL_CLEAR_MASK(attrs.asked_attributes);
      FSAL_SET_MASK(attrs.asked_attributes,
                    FSAL_ATTR_OWNER | FSAL_ATTR_GROUP |
                    FSAL_ATTR_ACL | FSAL_ATTR_MODE);

      status = CEPHFSAL_getattrs(exthandle, extcontext, &attrs);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_access);

      status = fsal_internal_testAccess((cephfsal_op_context_t*) extcontext,
                                        access_type, NULL, &attrs);
    }

  Return(status.major, status.minor, INDEX_FSAL_access);
}
