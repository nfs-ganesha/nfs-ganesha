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
fsal_status_t FSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t * object_attributes   /* IN */
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

/**
 * FSAL_test_setattr_access :
 * test if a client identified by cred can access setattr on the object
 * knowing its attributes and parent's attributes.
 * The following fields of the object_attributes structures MUST be filled :
 * acls (if supported), mode, owner, group.
 * This doesn't make any call to the filesystem,
 * as a result, this doesn't ensure that the file exists, nor that
 * the permissions given as parameters are the actual file permissions :
 * this must be ensured by the cache_inode layer, using FSAL_getattrs,
 * for example.
 *
 * \param cred (in fsal_cred_t *) user's identifier.
 * \param candidate_attrbutes the attributes we want to set on the object
 * \param object_attributes (in fsal_attrib_list_t *) the cached attributes
 *        for the object.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_setattr_access(fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t * candidate_attributes,    /* IN */
                                  fsal_attrib_list_t * object_attributes        /* IN */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_setattr_access);
}                               /* FSAL_test_setattr_access */

/**
 * FSAL_rename_access :
 * test if a client identified by cred can be renamed on the object
 * knowing the parents attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattrsrc      source directory attributes
 * \param pattrdest     destination directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */

fsal_status_t FSAL_rename_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattrsrc, /* IN */
                                 fsal_attrib_list_t * pattrdest)        /* IN */
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_rename_access);
}                               /* FSAL_rename_access */

/**
 * FSAL_create_access :
 * test if a client identified by cred can create an object within a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_create_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattr);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_create_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create_access);
}                               /* FSAL_create_access */

/**
 * FSAL_unlink_access :
 * test if a client identified by cred can unlink on a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_unlink_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattr);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_unlink_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink_access);

}                               /* FSAL_unlink_access */

/**
 * FSAL_merge_attrs: merge to attributes structure.
 *
 * This functions merge the second attributes list into the first argument. 
 * Results in returned in the last argument.
 *
 * @param pinit_attr   [IN] attributes to be changed
 * @param pnew_attr    [IN] attributes to be added
 * @param presult_attr [IN] resulting attributes
 * 
 * @return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_INVAL        Invalid argument(s)
 */

fsal_status_t FSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                               fsal_attrib_list_t * pnew_attr,
                               fsal_attrib_list_t * presult_attr)
{
  if(pinit_attr == NULL || pnew_attr == NULL || presult_attr == NULL)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_merge_attrs);

  /* The basis for the result attr is the fist argument */
  *presult_attr = *pinit_attr;

  /* Now deal with the attributes to be merged in this set of attributes */
  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_MODE))
    presult_attr->mode = pnew_attr->mode;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_OWNER))
    presult_attr->owner = pnew_attr->owner;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_GROUP))
    presult_attr->group = pnew_attr->group;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_SIZE))
    presult_attr->filesize = pnew_attr->filesize;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_SPACEUSED))
    presult_attr->spaceused = pnew_attr->spaceused;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_ATIME))
    {
      presult_attr->atime.seconds = pnew_attr->atime.seconds;
      presult_attr->atime.nseconds = pnew_attr->atime.nseconds;
    }

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_MTIME))
    {
      presult_attr->mtime.seconds = pnew_attr->mtime.seconds;
      presult_attr->mtime.nseconds = pnew_attr->mtime.nseconds;
    }

  /* Do not forget the ctime */
  FSAL_SET_MASK(presult_attr->asked_attributes, FSAL_ATTR_CTIME);
  presult_attr->ctime.seconds = pnew_attr->ctime.seconds;
  presult_attr->ctime.nseconds = pnew_attr->ctime.nseconds;

  /* Regular exit */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_merge_attrs);
}                               /* FSAL_merge_attrs */
