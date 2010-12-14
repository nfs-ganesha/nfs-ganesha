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
#include "fsal_common.h"

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
fsal_status_t SNMPFSAL_getattrs(snmpfsal_handle_t * filehandle, /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * object_attributes  /* IN/OUT */
    )
{

  int rc;
  fsal_status_t status;
  fsal_request_desc_t query_desc;
  struct tree *mib_node;
  netsnmp_variable_list *p_convert_var = NULL;

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!filehandle || !p_context || !object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  /* don't call GET request on directory */
  if(filehandle->data.object_type_reminder == FSAL_NODETYPE_LEAF && filehandle->data.oid_len != 0)
    {
      query_desc.request_type = SNMP_MSG_GET;

      TakeTokenFSCall();

      /* call to snmpget */
      rc = IssueSNMPQuery(p_context, filehandle->data.oid_tab, filehandle->data.oid_len,
                          &query_desc);

      ReleaseTokenFSCall();

      /* convert error code in case of error */
      if(rc != SNMPERR_SUCCESS && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
        Return(snmp2fsal_error(rc), rc, INDEX_FSAL_getattrs);

      if(snmp2fsal_error(rc) == ERR_FSAL_NOENT)
        Return(ERR_FSAL_STALE, rc, INDEX_FSAL_getattrs);

      p_convert_var = p_context->snmp_response->variables;

      /* check no such object, etc... */
      if(p_convert_var->type == SNMP_NOSUCHOBJECT
         || p_convert_var->type == SNMP_NOSUCHINSTANCE
         || p_convert_var->type == SNMP_ENDOFMIBVIEW)
        Return(ERR_FSAL_STALE, p_convert_var->type, INDEX_FSAL_getattrs);

      /* retrieve the associated MIB node (can be null) */
      mib_node = GetMIBNode(p_context, filehandle, TRUE);

    }                           /* endif not root  */
  else if(filehandle->data.object_type_reminder != FSAL_NODETYPE_ROOT
          && filehandle->data.oid_len != 0)
    {
      /* retrieve the associated MIB node (can be null) */
      mib_node = GetMIBNode(p_context, filehandle, TRUE);
    }
  else                          /* root  */
    mib_node = NULL;

  /* @todo check no such object, etc... */

  /* convert SNMP attributes to FSAL attributes */
  rc = snmp2fsal_attributes(filehandle, p_convert_var, mib_node, object_attributes);

  Return(rc, 0, INDEX_FSAL_getattrs);

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

fsal_status_t SNMPFSAL_setattrs(snmpfsal_handle_t * filehandle, /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * attrib_set,        /* IN */
                                fsal_attrib_list_t * object_attributes  /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_status_t status;
  fsal_attrib_list_t attrs;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context || !attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* no attributes can be set in SNMP */
  if(attrs.asked_attributes != 0)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
    }

  if(object_attributes)
    {

      status = SNMPFSAL_getattrs(filehandle, p_context, object_attributes);

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
fsal_status_t SNMPFSAL_getextattrs(snmpfsal_handle_t * p_filehandle, /* IN */
                                   snmpfsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_getextattrs);
} /* SNMPFSAL_getextattrs */
