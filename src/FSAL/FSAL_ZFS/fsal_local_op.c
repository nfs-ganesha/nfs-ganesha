/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
fsal_status_t ZFSFSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t * object_attributes   /* IN */
    )
{
  fsal_accessflags_t missing_access;
  int is_grp;

  /* sanity checks. */

  if(!object_attributes || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_test_access);

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_test_access);

  /* ----- here is a code sample for this function ---- */

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

  if(!is_grp)
    {
      /* >> Test here if file belongs to user's alt groups << */
    }

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
