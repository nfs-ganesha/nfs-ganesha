/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_local_check.c
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/13 14:20:07 $
 * \version $Revision: 1.0 $
 * \brief   Check for FSAL authentication locally
 *
 */

/*
 * Copyright CEA/DAM/DIF  (2008)
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
 * ---------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_test_access :
 * Tests whether the user or entity identified by its cred
 * can access the object as indicated by the access_type parameter.
 * This function tests access rights using cached attributes
 * given as parameter.
 * Thus, it cannot test FSAL_F_OK flag, and asking such a flag
 * will result in a ERR_FSAL_INVAL error.
 *
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
 * \param object_attributes (mandatory input):
 *        The cached attributes for the object to test rights on.
 *        The following attributes MUST be filled :
 *        owner, group, mode, ACLs.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t XFSFSAL_test_access(fsal_op_context_t * p_context,     /* IN */
                                  fsal_accessflags_t access_type,       /* IN */
                                  fsal_attrib_list_t * p_object_attributes      /* IN */
    )
{
  fsal_status_t status;
  status = fsal_internal_testAccess(p_context, access_type, NULL, p_object_attributes);
  Return(status.major, status.minor, INDEX_FSAL_test_access);
}
