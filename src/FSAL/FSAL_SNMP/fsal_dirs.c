/*
 * vim:expandtab:shiftwidth=4:tabstop=8:
 */

/**
 *
 * \file    fsal_dirs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:04 $
 * \version $Revision: 1.10 $
 * \brief   Directory browsing operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"
#include <string.h>

/**
 * FSAL_opendir :
 *     Opens a directory for reading its content.
 *     
 * \param dir_handle (input)
 *         the handle of the directory to be opened.
 * \param p_context (input)
 *         Permission context for the operation (user, export context...).
 * \param dir_descriptor (output)
 *         pointer to an allocated structure that will receive
 *         directory stream informations, on successfull completion.
 * \param dir_attributes (optional output)
 *         On successfull completion,the structure pointed
 *         by dir_attributes receives the new directory attributes.
 *         Can be NULL.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (user does not have read permission on directory)
 *        - ERR_FSAL_STALE        (dir_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t SNMPFSAL_opendir(snmpfsal_handle_t * dir_handle,  /* IN */
                               snmpfsal_op_context_t * p_context,       /* IN */
                               snmpfsal_dir_t * dir_descriptor, /* OUT */
                               fsal_attrib_list_t * dir_attributes      /* [ IN/OUT ] */
    )
{
  fsal_status_t st;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* check it is a node... */
  if(dir_handle->data.object_type_reminder == FSAL_NODETYPE_LEAF)
    Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_opendir);

  /* save request info to the dir_dircriptor */

  memcpy(&dir_descriptor->node_handle, dir_handle, sizeof(snmpfsal_handle_t));
  dir_descriptor->p_context = p_context;

  if(dir_attributes && dir_attributes->asked_attributes)
    {
      st = SNMPFSAL_getattrs(dir_handle, p_context, dir_attributes);
      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(dir_attributes->asked_attributes);
          FSAL_SET_MASK(dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);

}

/**
 * FSAL_readdir :
 *     Read the entries of an opened directory.
 *     
 * \param dir_descriptor (input):
 *        Pointer to the directory descriptor filled by FSAL_opendir.
 * \param start_position (input):
 *        Cookie that indicates the first object to be read during
 *        this readdir operation.
 *        This should be :
 *        - FSAL_READDIR_FROM_BEGINNING for reading the content
 *          of the directory from the beginning.
 *        - The end_position parameter returned by the previous
 *          call to FSAL_readdir.
 * \param get_attr_mask (input)
 *        Specify the set of attributes to be retrieved for directory entries.
 * \param buffersize (input)
 *        The size (in bytes) of the buffer where
 *        the direntries are to be stored.
 * \param pdirent (output)
 *        Adresse of the buffer where the direntries are to be stored.
 * \param end_position (output)
 *        Cookie that indicates the current position in the directory.
 * \param nb_entries (output)
 *        Pointer to the number of entries read during the call.
 * \param end_of_dir (output)
 *        Pointer to a boolean that indicates if the end of dir
 *        has been reached during the call.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t SNMPFSAL_readdir(snmpfsal_dir_t * dir_descriptor, /* IN */
                               snmpfsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * pdirent, /* OUT */
                               snmpfsal_cookie_t * end_position,        /* OUT */
                               fsal_count_t * nb_entries,       /* OUT */
                               fsal_boolean_t * end_of_dir      /* OUT */
    )
{
  fsal_count_t max_dir_entries;
  fsal_count_t cur_nb_entries;
  fsal_boolean_t bool_eod;
  snmpfsal_cookie_t last_listed;

  fsal_request_desc_t req_opt;
  netsnmp_variable_list *p_curr_var;
  struct tree *cur_node;
  struct tree *nearest_node;
  int rc;

  /* sanity checks */

  if(!dir_descriptor || !pdirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  /* The readdir call is a sequence of GET/GETNEXT operations :
   * The first call, is a get on the hypothetic next brother
   * => if it exists, we add it to the dirents, and try to get the next child.
   * => if it doesnt exist, we make a getnext on it. 
   *    then, when a next branch is discovered, the next item to be tested 
   *    is the next hypothetic child.
   */

  /* initial cookie  */

  if(start_position.data.oid_len == 0)       /* readdir from begginning  */
    {
      FSAL_OID_DUP(&last_listed, dir_descriptor->node_handle.data.oid_tab,
                   dir_descriptor->node_handle.data.oid_len);
      /* first try to get .0 child */
      last_listed.data.oid_tab[last_listed.data.oid_len] = 0;
      last_listed.data.oid_len++;
    }
  else                          /* readdir from another entry */
    {
      FSAL_OID_DUP(&last_listed, start_position.data.oid_tab, start_position.data.oid_len);
    }

  /* calculate the requested dircount */

  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));

  /* init counters and outputs */
  memset(pdirent, 0, buffersize);
  bool_eod = FALSE;
  cur_nb_entries = 0;

  if(max_dir_entries == 0)
    Return(ERR_FSAL_TOOSMALL, 0, INDEX_FSAL_readdir);

  while(!bool_eod && cur_nb_entries < max_dir_entries)
    {
      /* First, we proceed a GET request  */
      req_opt.request_type = SNMP_MSG_GET;

      TakeTokenFSCall();

      rc = IssueSNMPQuery(dir_descriptor->p_context, last_listed.data.oid_tab,
                          last_listed.data.oid_len, &req_opt);

      ReleaseTokenFSCall();

      if(rc != SNMP_ERR_NOERROR && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
        {
          LogDebug(COMPONENT_FSAL,
                   "SNMP GET request failed: error=%d, snmp_errno=%d, errno=%d, msg=%s",
                   rc, snmp_errno, errno, snmp_api_errstring(rc));
          Return(snmp2fsal_error(rc), rc, INDEX_FSAL_readdir);
        }
      else if(snmp2fsal_error(rc) != ERR_FSAL_NOENT)
        {

          p_curr_var = GetNextResponse(dir_descriptor->p_context);

          /* Then test if the object exists  */
          if(p_curr_var != NULL
             && p_curr_var->type != SNMP_NOSUCHOBJECT
             && p_curr_var->type != SNMP_NOSUCHINSTANCE
             && p_curr_var->type != SNMP_ENDOFMIBVIEW)
            {
              FSAL_OID_DUP(&pdirent[cur_nb_entries].handle, p_curr_var->name,
                           p_curr_var->name_length);
              pdirent[cur_nb_entries].handle.data.object_type_reminder = FSAL_NODETYPE_LEAF;

              /* object cookie is the hypothetic next object at the same level */
              FSAL_OID_DUP(&pdirent[cur_nb_entries].cookie, p_curr_var->name,
                           p_curr_var->name_length);
              FSAL_OID_INC(&pdirent[cur_nb_entries].cookie);

              cur_node =
                  GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                             FALSE);

              /* build the label */
              rc = snmp_object2name(p_curr_var, cur_node, &pdirent[cur_nb_entries].handle,
                                    &pdirent[cur_nb_entries].name);

              if(rc)
                Return(rc, 0, INDEX_FSAL_readdir);

              /* when a node does not exist, we take its nearest parent's rights */
              if(cur_node == NULL)
                nearest_node =
                    GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                               TRUE);
              else
                nearest_node = cur_node;

              if(isFullDebug(COMPONENT_FSAL))
                {
                  LogFullDebug(COMPONENT_FSAL,
                               "FOUND A DIRECT CHILD (LEAF) = %s, parent_oid_len=%d, oid_len=%d, index=%d",
                               pdirent[cur_nb_entries].name.name, dir_descriptor->node_handle.data.oid_len,
                               p_curr_var->name_length, p_curr_var->index);
                  if(nearest_node)
                    LogFullDebug(COMPONENT_FSAL, "type = %#X, last oid=%ld",
                                 nearest_node->type,
                                 p_curr_var->name[p_curr_var->name_length - 1]);
                }

              /* set entry attributes  */
              if(get_attr_mask)
                {
                  pdirent[cur_nb_entries].attributes.asked_attributes = get_attr_mask;
                  rc = snmp2fsal_attributes(&pdirent[cur_nb_entries].handle, p_curr_var,
                                            nearest_node,
                                            &pdirent[cur_nb_entries].attributes);

                  if(rc != ERR_FSAL_NO_ERROR)
                    {
                      FSAL_CLEAR_MASK(pdirent[cur_nb_entries].attributes.
                                      asked_attributes);
                      FSAL_SET_MASK(pdirent[cur_nb_entries].attributes.asked_attributes,
                                    FSAL_ATTR_RDATTR_ERR);
                    }
                }

              /* link entries together */
              pdirent[cur_nb_entries].nextentry = NULL;
              if(cur_nb_entries > 0)
                pdirent[cur_nb_entries - 1].nextentry = &pdirent[cur_nb_entries];

              /* copy hypothetic next entry to cookie and increment nb_entries  */
              FSAL_OID_DUP(&last_listed, pdirent[cur_nb_entries].cookie.data.oid_tab,
                           pdirent[cur_nb_entries].cookie.data.oid_len);
              cur_nb_entries++;

              /* restart a sequence from GET request  */
              continue;
            }
        }

      /* the entry has not been found, we can look for the next child */

      req_opt.request_type = SNMP_MSG_GETNEXT;

      TakeTokenFSCall();

      rc = IssueSNMPQuery(dir_descriptor->p_context, last_listed.data.oid_tab,
                          last_listed.data.oid_len, &req_opt);

      ReleaseTokenFSCall();

      if(rc != SNMP_ERR_NOERROR && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
        {
          LogDebug(COMPONENT_FSAL,
                   "SNMP GETNEXT request failed: error=%d, snmp_errno=%d, errno=%d, msg=%s",
                   rc, snmp_errno, errno, snmp_api_errstring(rc));
          Return(snmp2fsal_error(rc), rc, INDEX_FSAL_readdir);
        }
      else if(snmp2fsal_error(rc) == ERR_FSAL_NOENT)
        {
          bool_eod = TRUE;
          break;
        }

      p_curr_var = GetNextResponse(dir_descriptor->p_context);

      if(p_curr_var == NULL
         || p_curr_var->type == SNMP_NOSUCHOBJECT
         || p_curr_var->type == SNMP_NOSUCHINSTANCE
         || p_curr_var->type == SNMP_ENDOFMIBVIEW)
        {
          bool_eod = TRUE;
          break;
        }

      /* check if the response is under the root (else, end of dir is reached)  */
      if(IsSNMPChild
         (dir_descriptor->node_handle.data.oid_tab, dir_descriptor->node_handle.data.oid_len,
          p_curr_var->name, p_curr_var->name_length))
        {
          /* if the object is exactly 1 level under the dir, we insert it in the list */
          if(p_curr_var->name_length == dir_descriptor->node_handle.data.oid_len + 1)
            {
              FSAL_OID_DUP(&pdirent[cur_nb_entries].handle, p_curr_var->name,
                           p_curr_var->name_length);
              pdirent[cur_nb_entries].handle.data.object_type_reminder = FSAL_NODETYPE_LEAF;

              /* object cookie is the hypothetic next object */
              FSAL_OID_DUP(&pdirent[cur_nb_entries].cookie, p_curr_var->name,
                           p_curr_var->name_length);
              FSAL_OID_INC(&pdirent[cur_nb_entries].cookie);

              cur_node =
                  GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                             FALSE);

              /* build the label */
              rc = snmp_object2name(p_curr_var, cur_node, &pdirent[cur_nb_entries].handle,
                                    &pdirent[cur_nb_entries].name);

              if(rc)
                Return(rc, 0, INDEX_FSAL_readdir);

              /* when a node does not exist, we take its nearest parent's rights */
              if(cur_node == NULL)
                nearest_node =
                    GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                               TRUE);
              else
                nearest_node = cur_node;

              if(isFullDebug(COMPONENT_FSAL))
                {
                  LogFullDebug(COMPONENT_FSAL,
                               "FOUND A DIRECT CHILD (LEAF) = %s, parent_oid_len=%d, oid_len=%d, index=%d",
                               pdirent[cur_nb_entries].name.name, dir_descriptor->node_handle.data.oid_len,
                               p_curr_var->name_length, p_curr_var->index);
                  if(nearest_node)
                    LogFullDebug(COMPONENT_FSAL, "type = %#X, last oid=%ld",
                                 nearest_node->type,
                                 p_curr_var->name[p_curr_var->name_length - 1]);
                }

              /* set entry attributes  */
              if(get_attr_mask)
                {
                  pdirent[cur_nb_entries].attributes.asked_attributes = get_attr_mask;
                  rc = snmp2fsal_attributes(&pdirent[cur_nb_entries].handle, p_curr_var,
                                            nearest_node,
                                            &pdirent[cur_nb_entries].attributes);

                  if(rc != ERR_FSAL_NO_ERROR)
                    {
                      FSAL_CLEAR_MASK(pdirent[cur_nb_entries].attributes.
                                      asked_attributes);
                      FSAL_SET_MASK(pdirent[cur_nb_entries].attributes.asked_attributes,
                                    FSAL_ATTR_RDATTR_ERR);
                    }
                }

              /* link entries together */
              pdirent[cur_nb_entries].nextentry = NULL;
              if(cur_nb_entries > 0)
                pdirent[cur_nb_entries - 1].nextentry = &pdirent[cur_nb_entries];

              /* copy hypothetic next entry to cookie and increment nb_entries  */
              FSAL_OID_DUP(&last_listed, pdirent[cur_nb_entries].cookie.data.oid_tab,
                           pdirent[cur_nb_entries].cookie.data.oid_len);
              cur_nb_entries++;

              /* restart a sequence from GET request  */
              continue;
            }
          else                  /* directory item */
            {

              /* it the returned subdirectory is "smaller" than the cookie, we skip it
               * and incrment the cookie.
               */
              if(fsal_oid_cmp(p_curr_var->name, last_listed.data.oid_tab, last_listed.data.oid_len)
                 < 0)
                {
                  FSAL_OID_INC(&last_listed);
                  continue;
                }

              /* we found a new subsirectory */

              FSAL_OID_DUP(&pdirent[cur_nb_entries].handle, p_curr_var->name,
                           dir_descriptor->node_handle.data.oid_len + 1);
              pdirent[cur_nb_entries].handle.data.object_type_reminder = FSAL_NODETYPE_NODE;

              /* object cookie is the next potentiel object at this level */
              FSAL_OID_DUP(&pdirent[cur_nb_entries].cookie,
                           pdirent[cur_nb_entries].handle.data.oid_tab,
                           pdirent[cur_nb_entries].handle.data.oid_len);
              FSAL_OID_INC(&pdirent[cur_nb_entries].cookie);

              /* try to get the associated MIB node  */
              cur_node =
                  GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                             FALSE);

              /* build the label */
              rc = snmp_object2name(NULL, cur_node, &pdirent[cur_nb_entries].handle,
                                    &pdirent[cur_nb_entries].name);

              if(rc)
                Return(rc, 0, INDEX_FSAL_readdir);

              /* when a node does not exist, we take its nearest parent's rights */
              if(cur_node == NULL)
                nearest_node =
                    GetMIBNode(dir_descriptor->p_context, &pdirent[cur_nb_entries].handle,
                               TRUE);
              else
                nearest_node = cur_node;

              LogFullDebug(COMPONENT_FSAL, "FOUND A NEW SUBDIR = %s (%ld) (cookie->%ld)",
                           pdirent[cur_nb_entries].name.name,
                           pdirent[cur_nb_entries].handle.data.oid_tab[dir_descriptor->node_handle.data.oid_len],
                           pdirent[cur_nb_entries].cookie.data.oid_tab[pdirent[cur_nb_entries].cookie.data.oid_len - 1]);

              /* set entry attributes  */
              if(get_attr_mask)
                {
                  pdirent[cur_nb_entries].attributes.asked_attributes = get_attr_mask;
                  rc = snmp2fsal_attributes(&pdirent[cur_nb_entries].handle, NULL,
                                            nearest_node,
                                            &pdirent[cur_nb_entries].attributes);

                  if(rc != ERR_FSAL_NO_ERROR)
                    {
                      FSAL_CLEAR_MASK(pdirent[cur_nb_entries].attributes.
                                      asked_attributes);
                      FSAL_SET_MASK(pdirent[cur_nb_entries].attributes.asked_attributes,
                                    FSAL_ATTR_RDATTR_ERR);
                    }
                }

              /* link entries together */
              pdirent[cur_nb_entries].nextentry = NULL;
              if(cur_nb_entries > 0)
                pdirent[cur_nb_entries - 1].nextentry = &pdirent[cur_nb_entries];

              /* copy last listed item to cookie and increment nb_entries  */
              FSAL_OID_DUP(&last_listed, pdirent[cur_nb_entries].cookie.data.oid_tab,
                           pdirent[cur_nb_entries].cookie.data.oid_len);
              cur_nb_entries++;

              /* end of subdir processing */

            }

        }
      else                      /* no more objects in the directory tree */
        {
          bool_eod = TRUE;
          break;
        }

      /* loop until the requested count is reached
       * or the end of dir is reached.
       */
    }

  /* setting output vars : end_position, nb_entries, end_of_dir  */
  *end_of_dir = bool_eod;

  memset(end_position, 0, sizeof(snmpfsal_cookie_t));
  FSAL_OID_DUP(end_position, last_listed.data.oid_tab, last_listed.data.oid_len);

  *nb_entries = cur_nb_entries;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);

}

/**
 * FSAL_closedir :
 * Free the resources allocated for reading directory entries.
 *     
 * \param dir_descriptor (input):
 *        Pointer to a directory descriptor filled by FSAL_opendir.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t SNMPFSAL_closedir(snmpfsal_dir_t * dir_descriptor /* IN */
    )
{

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  memset(dir_descriptor, 0, sizeof(snmpfsal_dir_t));

  /* nothing to do, GETBULK response is freed at the next request */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
