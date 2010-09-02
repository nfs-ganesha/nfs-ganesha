/*
 * vim:expandtab:shiftwidth=4:tabstop=8:
 */

/**
 * Common FS tools for internal use in the FSAL.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal_common.h"
#include "fsal_convert.h"
#include "fsal_internal.h"

#include <strings.h>

void BuildRootHandle(snmpfsal_handle_t * p_hdl)
{
  memset(p_hdl, 0, sizeof(snmpfsal_handle_t));
  p_hdl->data.oid_len = 0;
  p_hdl->data.object_type_reminder = FSAL_NODETYPE_ROOT;
}

int ParseSNMPPath(char *in_path, snmpfsal_handle_t * out_handle)
{
  if(!in_path || !out_handle)
    return ERR_FSAL_FAULT;

  out_handle->data.oid_len = MAX_OID_LEN;

  if(!snmp_parse_oid(in_path, out_handle->data.oid_tab, &out_handle->data.oid_len))
    return ERR_FSAL_NOENT;

  return ERR_FSAL_NO_ERROR;
}

int IssueSNMPQuery(snmpfsal_op_context_t * p_context, oid * oid_tab, int oid_len,
                   fsal_request_desc_t * p_req_desc)
{
  int rc;
  char oid_str[FSAL_MAX_PATH_LEN] = "";

  /* sanity checks  */
  if(!p_context || !oid_tab || !p_req_desc)
    return (SNMPERR_MAX - 1);

  /* clean the thread context before issuing the request */
  if(p_context->snmp_response)
    {
      if(p_context->snmp_response->variables)
        {
          snmp_free_varbind(p_context->snmp_response->variables);
          p_context->snmp_response->variables = NULL;
        }
      /* clean up the last response */
      snmp_free_pdu(p_context->snmp_response);
      p_context->snmp_response = NULL;
    }

  /* reset snmp error */
  snmp_errno = 0;

  /* reset the pointer to current response (used for getbulk requests) */
  p_context->current_response = NULL;

  /* now create the request to be sent */
  p_context->snmp_request = snmp_pdu_create(p_req_desc->request_type);

  if(p_context->snmp_request == NULL)
    return snmp_errno;

  /* used for logging  */
  snprint_objid(oid_str, FSAL_MAX_PATH_LEN, oid_tab, oid_len);

  switch (p_req_desc->request_type)
    {
    case SNMP_MSG_GET:
      LogFullDebug(COMPONENT_FSAL, "Issuing SNMP GET request on %s", oid_str);
      /* for a get request, the value field is empty */
      snmp_add_null_var(p_context->snmp_request, oid_tab, oid_len);
      break;

    case SNMP_MSG_GETNEXT:
      LogFullDebug(COMPONENT_FSAL, "Issuing SNMP GETNEXT request on %s", oid_str);
      /* for a get request, the value field is empty */
      snmp_add_null_var(p_context->snmp_request, oid_tab, oid_len);
      break;

    case SNMP_MSG_SET:
      LogFullDebug(COMPONENT_FSAL,
                   "Issuing SNMP SET request on %s (type '%c', value='%s')", oid_str,
                   p_req_desc->SET_REQUEST_INFO.type,
                   p_req_desc->SET_REQUEST_INFO.value);
      /* for a set request, we set the value and type */
      snmp_add_var(p_context->snmp_request, oid_tab, oid_len,
                   p_req_desc->SET_REQUEST_INFO.type, p_req_desc->SET_REQUEST_INFO.value);
      break;

    case SNMP_MSG_TRAP:
      LogFullDebug(COMPONENT_FSAL, "Issuing a SNMP TRAP (v1) on %s", oid_str);
      break;

    case SNMP_MSG_GETBULK:
      LogFullDebug(COMPONENT_FSAL,
                   "Issuing SNMP GETBULK request on %s (max-repetitions=%ld)",
                   oid_str, p_req_desc->GETBULK_REQUEST_INFO.max_repetitions);
      /* in case of a GETBULK request, we set the request options */
      p_context->snmp_request->non_repeaters =
          p_req_desc->GETBULK_REQUEST_INFO.non_repeaters;
      p_context->snmp_request->max_repetitions =
          p_req_desc->GETBULK_REQUEST_INFO.max_repetitions;

      /* for a get request, the value field is empty */
      snmp_add_null_var(p_context->snmp_request, oid_tab, oid_len);
      break;

    case SNMP_MSG_INFORM:
      LogFullDebug(COMPONENT_FSAL, "Issuing a SNMP INFORM message on %s", oid_str);
      break;

    case SNMP_MSG_TRAP2:
      LogFullDebug(COMPONENT_FSAL, "Issuing a SNMP TRAP (v2,3) on %s", oid_str);
      break;

    case SNMP_MSG_REPORT:
      LogFullDebug(COMPONENT_FSAL, "Issuing a SNMP REPORT message on %s", oid_str);
      break;

    default:
      LogCrit(COMPONENT_FSAL, "ERROR: Unknown request %#X on %s",
              p_req_desc->request_type, oid_str);
      return SNMPERR_MAX - 1;

    }

  /* issue the message and wait for server response  */
  rc = snmp_synch_response(p_context->snmp_session, p_context->snmp_request,
                           &(p_context->snmp_response));

  if(rc)
    return snmp_errno;
  else if(!p_context->snmp_response)
    return SNMP_ERR_GENERR;
  else
    return p_context->snmp_response->errstat;

}

/**
 * It has the same behavior as get_tree in net-snmp library,
 * except that it always NULL if it didn't find.
 */
struct tree *FSAL_GetTree(oid * objid, int objidlen, struct tree *subtree,
                          int return_nearest_parent)
{
  struct tree *return_tree = NULL;

  for(; subtree; subtree = subtree->next_peer)
    {
      if(*objid == subtree->subid)
        goto found;
    }

  return NULL;

 found:
  while(subtree->next_peer && subtree->next_peer->subid == *objid)
    subtree = subtree->next_peer;

  if(return_nearest_parent)
    {
      /* if child not found, return nearest parent */
      if(objidlen > 1)
        return_tree = FSAL_GetTree(objid + 1, objidlen - 1, subtree->child_list, TRUE);

      if(return_tree != NULL)
        return return_tree;
      else
        return subtree;

    }
  else                          /* only return node when it is found */
    {
      if(objidlen == 1)
        return subtree;

      if(objidlen > 1)
        return FSAL_GetTree(objid + 1, objidlen - 1, subtree->child_list, FALSE);

      /* no node for root */
      return NULL;
    }

}

struct tree *GetMIBNode(snmpfsal_op_context_t * p_context, snmpfsal_handle_t * p_handle,
                        int return_nearest_parent)
{
  if(!p_context || !p_handle)
    return NULL;

  /* SNMP root "." has no proper tree node */
  if(p_handle->data.object_type_reminder == FSAL_NODETYPE_ROOT || p_handle->data.oid_len == 0)
    return NULL;

  /* in the other cases, get the node from the export context  */
  return FSAL_GetTree(p_handle->data.oid_tab, p_handle->data.oid_len,
                      p_context->export_context->root_mib_tree, return_nearest_parent);
}

struct tree *GetMIBChildList(snmpfsal_op_context_t * p_context,
                             snmpfsal_handle_t * p_handle)
{
  struct tree *obj_tree;

  if(!p_context || !p_handle)
    return NULL;

  /* root's child pointer is the whole MIB tree */
  if(p_handle->data.object_type_reminder == FSAL_NODETYPE_ROOT || p_handle->data.oid_len == 0)
    return p_context->export_context->root_mib_tree;    /* it should be the tree retruned by read_all_mibs */

  /* retrieve object associated subtree, and return its childs  */
  obj_tree =
      FSAL_GetTree(p_handle->data.oid_tab, p_handle->data.oid_len,
                   p_context->export_context->root_mib_tree, FALSE);

  if(obj_tree == NULL)
    return NULL;

  return obj_tree->child_list;

}

/* test is a given oid is in the subtree whose parent_oid is the root */
int IsSNMPChild(oid * parent_oid, int parent_oid_len, oid * child_oid, int child_oid_len)
{
  return ((child_oid_len > parent_oid_len)
          && (!memcmp(parent_oid, child_oid, parent_oid_len * sizeof(oid))));
}

/* check (using an SNMP GETNEXT request) if the snmp object has some childs.
 * NB: the object_type_reminder handle's field is not used in this call.
 * @return 0 if it is not a parent node, 1 if it is, -1 on SNMP error. 
 */
int HasSNMPChilds(snmpfsal_op_context_t * p_context, snmpfsal_handle_t * p_handle)
{
  fsal_request_desc_t req_desc;
  int rc;

  req_desc.request_type = SNMP_MSG_GETNEXT;
  rc = IssueSNMPQuery(p_context, p_handle->data.oid_tab, p_handle->data.oid_len, &req_desc);

  if(rc != SNMPERR_SUCCESS && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
    return -1;

  if(snmp2fsal_error(rc) == ERR_FSAL_NOENT
     || p_context->snmp_response->variables->type == SNMP_NOSUCHOBJECT
     || p_context->snmp_response->variables->type == SNMP_NOSUCHINSTANCE
     || p_context->snmp_response->variables->type == SNMP_ENDOFMIBVIEW)
    return 0;                   /* no childs, return 0 */

  /* it is a root node if getnext returned a child of it */
  return IsSNMPChild(p_handle->data.oid_tab, p_handle->data.oid_len,
                     p_context->snmp_response->variables->name,
                     p_context->snmp_response->variables->name_length);

}

/**
 * get the next response for a GETBULK response sequence. 
 * @param p_context the current request context. 
 */
netsnmp_variable_list *GetNextResponse(snmpfsal_op_context_t * p_context)
{
  if(!p_context)
    return NULL;

  /* initialize or increment current_response */
  if(p_context->current_response == NULL)
    p_context->current_response = p_context->snmp_response->variables;
  else
    p_context->current_response = p_context->current_response->next_variable;

  return p_context->current_response;
}

/**
 * this function compares the <count> first oids of the 2 SNMP paths
 * returns a negative value when oid_tab1 < oid_tab2,
 * a positive value when oid_tab1 > oid_tab2,
 * 0 if they are equal.
 */
int fsal_oid_cmp(oid * oid_tab1, oid * oid_tab2, unsigned int count)
{
  unsigned int i;

  for(i = 0; i < count; i++)
    {
      if(oid_tab1[i] < oid_tab2[i])
        return -1;
      if(oid_tab1[i] > oid_tab2[i])
        return 1;
    }
  return 0;
}

long StrToSNMPVersion(char *str)
{
  if(str == NULL)
    return -1;

  if(!strcmp(str, "1"))
    return SNMP_VERSION_1;
  if(!strcasecmp(str, "2c"))
    return SNMP_VERSION_2c;
  if(!strcmp(str, "3"))
    return SNMP_VERSION_3;

  return -1;
}
