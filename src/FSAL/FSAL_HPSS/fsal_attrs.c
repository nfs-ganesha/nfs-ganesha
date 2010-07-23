/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/09/09 15:22:49 $
 * \version $Revision: 1.19 $
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "HPSSclapiExt/hpssclapiext.h"

#include <hpss_errno.h>

/**
 * FSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user, export...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Another error code if an error occured.
 */
fsal_status_t HPSSFSAL_getattrs(hpssfsal_handle_t * filehandle, /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * object_attributes  /* IN/OUT */
    )
{

  int rc;
  fsal_status_t status;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!filehandle || !p_context || !object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  /* get attributes */
  /* We use  HPSSFSAL_GetRawAttrHandle for not chasing junctions
   * nor solving symlinks. What's more, we want hpss_Attrs_t.
   */

  TakeTokenFSCall();

  rc = HPSSFSAL_GetRawAttrHandle(&(filehandle->data.ns_handle), NULL, &p_context->credential.hpss_usercred, FALSE,   /* don't solve junctions */
                                 &hpss_hdl, NULL, &hpss_attr);

  ReleaseTokenFSCall();

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_getattrs);
  else if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_getattrs);

  /* convert attributes */

  status = hpss2fsal_attributes(&hpss_hdl, &hpss_attr, object_attributes);

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_getattrs);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * FSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_INVAL        (tried to modify a read-only attribute)
 *        - ERR_FSAL_ATTRNOTSUPP  (tried to modify a non-supported attribute)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */

fsal_status_t HPSSFSAL_setattrs(hpssfsal_handle_t * filehandle, /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * attrib_set,        /* IN */
                                fsal_attrib_list_t * object_attributes  /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_status_t status;
  fsal_attrib_list_t attrs;

  hpss_fileattrbits_t hpss_attr_mask;
  hpss_fileattr_t hpss_fattr_in, hpss_fattr_out;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context || !attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* local copy of attributes */
  attrs = *attrib_set;

  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {

      if(attrs.asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
        {

          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }

    }

  /* apply umask, if mode attribute is to be changed */

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      attrs.mode &= (~global_fs_info.umask);
    }

  /** @todo : chown restricted seems to be OK. */

  /* init variables */
  memset(&hpss_fattr_in, 0, sizeof(hpss_fileattr_t));

  hpss_fattr_in.ObjectHandle = filehandle->data.ns_handle;

  /* Then, convert attribute set. */

  status = fsal2hpss_attribset(filehandle,
                               &attrs, &hpss_attr_mask, &(hpss_fattr_in.Attrs));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_setattrs);

  /* Call HPSS client API function. */

  TakeTokenFSCall();

  rc = HPSSFSAL_FileSetAttrHandle(&(filehandle->data.ns_handle),     /* IN  - object handle */
                                  NULL, /* IN  - path to the object */
                                  &(p_context->credential.hpss_usercred),       /* IN  - user credentials */
                                  hpss_attr_mask,       /* IN - attributes fields to set */
                                  &hpss_fattr_in,       /* IN  - input attributes */
                                  &hpss_fattr_out       /* OUT - attributes after change */
      );

  ReleaseTokenFSCall();

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_setattrs);
  else if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_setattrs);

  /* Optionaly fills output attributes. */

  /** @todo voir pourquoi hpss_fattr_out ne contient pas
     ce qu'il devrait contenir */

  /*
   * HPSS only fills the modified attribute in hpss_fattr_out.
   * Thus, if the modified attributes equal the attributes to be returned
   * there is no need to proceed a getattr.
   */
  if(object_attributes &&
     (object_attributes->asked_attributes == attrib_set->asked_attributes))
    {

      /* caution: hpss_fattr_out.ObjectHandle is not filled. */

      status = hpss2fsal_attributes(&(filehandle->data.ns_handle),
                                    &(hpss_fattr_out.Attrs), object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }
  /* if more attributes are asked, we have to proceed a getattr. */
  else if(object_attributes)
    {

      status = HPSSFSAL_getattrs(filehandle, p_context, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
