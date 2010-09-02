/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.18 $
 * \brief   Lookup operations.
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
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_filename does not exist)
 *        - ERR_FSAL_XDEV         (tried to operate a lookup on a filesystem junction.
 *                                 Use FSAL_lookupJunction instead)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
fsal_status_t SNMPFSAL_lookup(snmpfsal_handle_t * parent_directory_handle,      /* IN */
                              fsal_name_t * p_filename, /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              snmpfsal_handle_t * object_handle,        /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_status_t status;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  memset(object_handle, 0, sizeof(snmpfsal_handle_t));

  /* retrieves root handle */

  if(!parent_directory_handle)
    {

      /* check that p_filename is NULL,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* retrieve root filehandle here */
      BuildRootHandle(object_handle);

      /* retrieves root attributes, if asked */

      if(object_attributes)
        {
          fsal_status_t status;

          status = SNMPFSAL_getattrs(object_handle, p_context, object_attributes);

          /* On error, we set a flag in the returned attributes */

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }

    }
  else                          /* this is a real lookup(parent, name)  */
    {
      struct tree *curr_child = NULL;
      int found = FALSE;
      int type_ok = FALSE;
      char *end_ptr;
      fsal_request_desc_t query_desc;

      /* the filename should not be null */
      if(p_filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      LogFullDebug(COMPONENT_FSAL, "lookup for '%s'", p_filename->name);

      /* we check the parent type stored into the handle. */

      switch (parent_directory_handle->data.object_type_reminder)
        {
        case FSAL_NODETYPE_NODE:
        case FSAL_NODETYPE_ROOT:
          /* OK */
          break;

        case FSAL_NODETYPE_LEAF:
          /* not a directory */
          Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

        default:
          Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
        }

      /* look up for . or .. on root */

      if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT)
         || (!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT_DOT)
             && parent_directory_handle->data.oid_len == 0))
        {
          FSAL_OID_DUP(object_handle, parent_directory_handle->data.oid_tab,
                       parent_directory_handle->data.oid_len);
          object_handle->data.object_type_reminder =
              parent_directory_handle->data.object_type_reminder;

          if(object_attributes)
            {
              rc = snmp2fsal_attributes(object_handle, NULL,
                                        GetMIBNode(p_context, object_handle, TRUE),
                                        object_attributes);

              if(rc != 0)
                {
                  FSAL_CLEAR_MASK(object_attributes->asked_attributes);
                  FSAL_SET_MASK(object_attributes->asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                }
            }

          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

        }

      /* lookup up for parent entry  */
      if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT_DOT))
        {
          LogFullDebug(COMPONENT_FSAL, "lookup for parent (oid len = %u)",
                       parent_directory_handle->data.oid_len);

          FSAL_OID_DUP(object_handle, parent_directory_handle->data.oid_tab,
                       parent_directory_handle->data.oid_len - 1);

          if(object_handle->data.oid_len == 0)
            object_handle->data.object_type_reminder = FSAL_NODETYPE_ROOT;
          else
            object_handle->data.object_type_reminder = FSAL_NODETYPE_NODE;

          LogFullDebug(COMPONENT_FSAL, "parent handle has (oid len = %u)",
                       object_handle->data.oid_len);

          if(object_attributes)
            {
              rc = snmp2fsal_attributes(object_handle, NULL,
                                        GetMIBNode(p_context, object_handle, TRUE),
                                        object_attributes);

              if(rc != 0)
                {
                  FSAL_CLEAR_MASK(object_attributes->asked_attributes);
                  FSAL_SET_MASK(object_attributes->asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                }
            }

          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

        }

      /* first, we check if the object name is available in the MIB */

      for(curr_child = GetMIBChildList(p_context, parent_directory_handle);
          curr_child != NULL; curr_child = curr_child->next_peer)
        {
          LogFullDebug(COMPONENT_FSAL, "CHILD = %s (%lu)\n",
                       curr_child->label, curr_child->subid);

          if(curr_child->label
             && !strncmp(p_filename->name, curr_child->label, FSAL_MAX_NAME_LEN))
            {
              LogFullDebug(COMPONENT_FSAL, "MATCHES !!!!\n");

              /* we found it ! we fill the handle using the parent handle and adding the subid value */
              FSAL_OID_DUP(object_handle, parent_directory_handle->data.oid_tab,
                           parent_directory_handle->data.oid_len);
              object_handle->data.oid_len++;
              object_handle->data.oid_tab[object_handle->data.oid_len - 1] = curr_child->subid;

              /* if it has some childs, we are sure its a node */
              if(curr_child->child_list)
                {
                  type_ok = TRUE;
                  object_handle->data.object_type_reminder = FSAL_NODETYPE_NODE;
                }
              /* else, indetermined type */

              found = TRUE;

              break;

            }                   /* endif label match */
        }                       /* end for child list  */

      if(!found)
        {
          unsigned long subid;

          /* if not found, reset child MIB item  */
          curr_child = NULL;

          /* if the name is a numerical value, we parse it and try to get its attributes */
          subid = strtoul(p_filename->name, &end_ptr, 10);

          LogFullDebug(COMPONENT_FSAL, "Looking for subid = %lu end_ptr=%p='%s'\n",
                       subid, end_ptr, end_ptr);

          /* the object is not numerical and could not be found  */
          if(end_ptr != NULL && *end_ptr != '\0')
            Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_lookup);

          /* build the handle from parsed value */
          FSAL_OID_DUP(object_handle, parent_directory_handle->data.oid_tab,
                       parent_directory_handle->data.oid_len);
          object_handle->data.oid_len++;
          object_handle->data.oid_tab[object_handle->data.oid_len - 1] = subid;
        }

      /* type has not been set using the MIB */
      if(!type_ok)
        {

          /* we make snmp GET for checking existence
           * and for finding object type */
          query_desc.request_type = SNMP_MSG_GET;

          TakeTokenFSCall();

          /* call to snmpget */
          rc = IssueSNMPQuery(p_context, object_handle->data.oid_tab, object_handle->data.oid_len,
                              &query_desc);

          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "rc = %d, snmp_errno = %d\n", rc, snmp_errno);

          if(rc != 0 && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
            Return(snmp2fsal_error(rc), rc, INDEX_FSAL_lookup);

          if(snmp2fsal_error(rc) == ERR_FSAL_NOENT
             || p_context->snmp_response->variables->type == SNMP_NOSUCHOBJECT
             || p_context->snmp_response->variables->type == SNMP_NOSUCHINSTANCE
             || p_context->snmp_response->variables->type == SNMP_ENDOFMIBVIEW)
            {
              /* if it has childs, it is a NODE, else, return ENOENT */
              switch (HasSNMPChilds(p_context, object_handle))
                {
                case -1:
                  Return(ERR_FSAL_IO, snmp_errno, INDEX_FSAL_lookup);
                case 0:
                  Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_lookup);
                case 1:
                  object_handle->data.object_type_reminder = FSAL_NODETYPE_NODE;
                  break;
                default:
                  LogCrit(COMPONENT_FSAL,
                          "ERROR: unexpected return value from HasSNMPChild");
                  Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
                }
            }
          else
            {
              /* this is a typed object, so its a leaf */
              object_handle->data.object_type_reminder = FSAL_NODETYPE_LEAF;
            }
        }

      /* if it was not found by name, we try to get the MIB node with the found handle
       * (or its nearest parent)
       */
      if(!found)
        curr_child = GetMIBNode(p_context, object_handle, TRUE);

      if(object_attributes)
        {
          rc = snmp2fsal_attributes(object_handle,
                                    (object_handle->data.object_type_reminder ==
                                     FSAL_NODETYPE_LEAF ? p_context->snmp_response->
                                     variables : NULL), curr_child, object_attributes);

          if(rc != 0)
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }

        }

    }                           /* end of non-root entry */

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param cred (input)
 *        Authentication context for the operation (user,...).
 * \param p_fsroot_handle (output)
 *        The handle of root directory of the fileset.
 * \param p_fsroot_attributes (optional input/output)
 *        Pointer to the attributes of the root directory
 *        for the fileset.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (p_junction_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
fsal_status_t SNMPFSAL_lookupJunction(snmpfsal_handle_t * p_junction_handle,    /* IN */
                                      snmpfsal_op_context_t * p_context,        /* IN */
                                      snmpfsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t * p_fsroot_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  fsal_status_t status;

  /* sanity checks
   * note : p_fsroot_attributes is optionnal
   */
  if(!p_junction_handle || !p_fsoot_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupJunction);

  /* no junctions in SNMP */
  Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupJunction);

}

/**
 * FSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if path equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - ERR_FSAL_INVAL        (the path argument is not absolute)
 *        - ERR_FSAL_NOENT        (an element in the path does not exist)
 *        - ERR_FSAL_NOTDIR       (an element in the path is not a directory)
 *        - ERR_FSAL_XDEV         (tried to cross a filesystem junction,
 *                                 whereas is has not been authorized in the server
 *                                 configuration - FSAL::auth_xdev_export parameter)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t SNMPFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  snmpfsal_op_context_t * p_context,    /* IN */
                                  snmpfsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  snmpfsal_handle_t out_hdl;
  fsal_status_t status;
  int b_is_last = FALSE;        /* is it the last lookup ? */
  int rc;

  /* sanity checks
   * note : object_attributes is optionnal.
   */

  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */

  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* First, we check that the path begins with the export path */
  if(strncmp
     (p_context->export_context->root_path.path, p_path->path,
      p_context->export_context->root_path.len))
    {
      LogCrit(COMPONENT_FSAL,
              "ERROR: FSAL_lookupPath was called on a path that doesn't match export info (path=%s, export=%s)",
              p_path->path, p_context->export_context->root_path.path);
      Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_lookupPath);
    }

  /* now, make the pointer point to the next name in the path.
   */
  ptr_str = p_path->path + p_context->export_context->root_path.len;

  /* skip slashes */
  while(ptr_str[0] == '/')
    ptr_str++;

  /* is the next name empty ? */

  if(ptr_str[0] == '\0')
    b_is_last = TRUE;

  /* retrieves root directory */

  out_hdl = p_context->export_context->root_handle;

  if(b_is_last && object_attributes)
    {
      rc = snmp2fsal_attributes(&out_hdl, NULL, p_context->export_context->root_mib_tree,
                                object_attributes);

      if(rc != ERR_FSAL_NO_ERROR)
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* exits if this was the last lookup */

  if(b_is_last)
    {
      (*object_handle) = out_hdl;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);
    }

  /* proceed a step by step lookup */

  while(ptr_str[0])
    {

      snmpfsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

      in_hdl = out_hdl;

      /* compute next name */
      obj_name.len = 0;
      dest_ptr = obj_name.name;
      while(ptr_str[0] != '\0' && ptr_str[0] != '/')
        {
          dest_ptr[0] = ptr_str[0];
          dest_ptr++;
          ptr_str++;
          obj_name.len++;
        }
      /* final null char */
      dest_ptr[0] = '\0';

      /* skip multiple slashes */
      while(ptr_str[0] == '/')
        ptr_str++;

      /* is the next name empty ? */
      if(ptr_str[0] == '\0')
        b_is_last = TRUE;

      /*call to FSAL_lookup */
      status = SNMPFSAL_lookup(&in_hdl, /* parent directory handle */
                               &obj_name,       /* object name */
                               p_context,       /* user's credentials */
                               &out_hdl,        /* output root handle */
                               /* retrieves attributes if this is the last lookup : */
                               (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* ptr_str is ok, we are ready for next loop */
    }

  (*object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
