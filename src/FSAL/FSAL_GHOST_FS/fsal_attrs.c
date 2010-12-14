/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/20 15:19:30 $
 * \version $Revision: 1.10 $
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"
#include <string.h>

/**
 * FSAL_getattrs.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ATTRNOTSUPP  (attribute not supported)
 *        - ERR_FSAL_BADHANDLE    (illegal handle)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_IO           (corrupted FS)
 *        - ERR_FSAL_NOT_INIT     (ghostfs not initialize)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_getattrs(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * object_attributes      /* IN/OUT */
    )
{

  int rc;
  GHOSTFS_Attrs_t ghost_attrs;
  fsal_attrib_mask_t supp_attr, unsupp_attr;

  /* For logging */
  SetFuncID(INDEX_FSAL_getattrs);

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!filehandle || !p_context || !object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  rc = GHOSTFS_GetAttrs((GHOSTFS_handle_t) (*filehandle), &ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_getattrs);

  /* Credentials are not tested because
   * we consider that if the user got an handle to the object,
   * he has the right for retrieving its attributes.
   */

  /* supported attrs for GHOSTFS */
  supp_attr = GHOSTFS_SUPPORTED_ATTRIBUTES;

  /* tests whether we can supply all asked attributes */
  unsupp_attr = (object_attributes->asked_attributes) & (~supp_attr);
  if(unsupp_attr)
    {
      LogMajor(COMPONENT_FSAL,
               "Unsupported attributes: %#llX removing it from asked attributes ",
               unsupp_attr);
      object_attributes->asked_attributes =
          object_attributes->asked_attributes & (~unsupp_attr);
    }

  /* Fills the output struct */
  ghost2fsal_attrs(object_attributes, &ghost_attrs);

  /* everything has been copied ! */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

fsal_status_t FSAL_setattrs(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * attrib_set,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  GHOSTFS_setattr_mask_t set_mask = 0;
  GHOSTFS_Attrs_t ghost_attrs;
  int rc;

  memset(&ghost_attrs, 0, sizeof(GHOSTFS_Attrs_t));

  /* For logging */
  SetFuncID(INDEX_FSAL_setattrs);

#define SETTABLE_ATTRIBUTES ( FSAL_ATTR_SIZE |\
                              FSAL_ATTR_MODE |\
                              FSAL_ATTR_OWNER |\
                              FSAL_ATTR_GROUP |\
                              FSAL_ATTR_ATIME |\
                              FSAL_ATTR_MTIME )
  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context || !attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* first convert attributes and mask */

  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_SIZE))
    {
      set_mask |= SETATTR_SIZE;
      ghost_attrs.size = attrib_set->filesize;
    }
  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_MODE))
    {
      set_mask |= SETATTR_MODE;
      ghost_attrs.mode = fsal2ghost_mode(attrib_set->mode);
    }

  /* ghostfs does not check chown restrictions,
   * so we check this for it. */

  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_OWNER))
    {

      if(p_context->credential.user != 0)
        Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);

      set_mask |= SETATTR_UID;
      ghost_attrs.uid = attrib_set->owner;
    }

  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_GROUP))
    {
      if(p_context->credential.user != 0)
        Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);

      set_mask |= SETATTR_GID;
      ghost_attrs.gid = attrib_set->group;
    }

  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_ATIME))
    {
      set_mask |= SETATTR_ATIME;
      ghost_attrs.atime = attrib_set->atime.seconds;
    }
  if(FSAL_TEST_MASK(attrib_set->asked_attributes, FSAL_ATTR_MTIME))
    {
      set_mask |= SETATTR_MTIME;
      ghost_attrs.mtime = attrib_set->mtime.seconds;
    }

  if(attrib_set->asked_attributes & ~SETTABLE_ATTRIBUTES)
    {
      LogFullDebug(COMPONENT_FSAL, "FSAL: To be set %llX, Settable %llX",
             (unsigned long long)object_attributes->asked_attributes,
             (unsigned long long)SETTABLE_ATTRIBUTES);

      Return(ERR_FSAL_ATTRNOTSUPP, 0, INDEX_FSAL_setattrs);
    }

  /* appel a setattr */
  rc = GHOSTFS_SetAttrs((GHOSTFS_handle_t) (*filehandle), set_mask, ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_setattrs);

  if(object_attributes)
    {
      fsal_status_t status = FSAL_getattrs(filehandle, p_context, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
/**
 * FSAL_getetxattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t FSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_extattrib_list_t * p_object_attributes /* OUT */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_getextattrs);
} /* FSAL_getextattrs */
