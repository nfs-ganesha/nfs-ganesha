/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_creds.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.15 $
 * \brief   FSAL credentials handling functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "fsal_convert.h"
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 * 
 * @{
 */

/**
 * Parse FS specific option string
 * to build the export entry option.
 */
fsal_status_t SNMPFSAL_BuildExportContext(snmpfsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options     /* IN */
    )
{
  struct tree *tree_head, *sub_tree;
  char snmp_path[FSAL_MAX_PATH_LEN];
  int rc;

  /* sanity check */
  if(!p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);

  if((fs_specific_options != NULL) && (fs_specific_options[0] != '\0'))
    {
      LogCrit(COMPONENT_FSAL,
              "FSAL BUILD CONTEXT: ERROR: found an EXPORT::FS_Specific item whereas it is not supported for this filesystem.");
    }

  /* retrieves the MIB tree associated to this export */

  /*tree_head = get_tree_head(); */
  tree_head = read_all_mibs();

  if(tree_head == NULL)
    Return(ERR_FSAL_BAD_INIT, snmp_errno, INDEX_FSAL_BuildExportContext);

  /* convert the path to a SNMP path */
  if(p_export_path != NULL)
    PosixPath2SNMP(p_export_path->path, snmp_path);
  else
    strcpy(snmp_path, ".");

  if(!strcmp(snmp_path, "."))
    {
      /* the exported tree is the root tree */
      p_export_context->root_mib_tree = tree_head;
      BuildRootHandle(&p_export_context->root_handle);
    }
  else
    {
      /* convert the SNMP path to an oid */
      rc = ParseSNMPPath(snmp_path, &p_export_context->root_handle);

      if(rc)
        {
          LogCrit(COMPONENT_FSAL, "FSAL BUILD CONTEXT: ERROR parsing SNMP path '%s'", snmp_path);
          Return(rc, 0, INDEX_FSAL_BuildExportContext);
        }

      /* get the subtree */
      sub_tree =
          FSAL_GetTree(p_export_context->root_handle.data.oid_tab,
                       p_export_context->root_handle.data.oid_len, tree_head, FALSE);

      if(sub_tree == NULL)
        Return(ERR_FSAL_NOENT, snmp_errno, INDEX_FSAL_BuildExportContext);

      /* if it has some childs or the object is unknown, consider it has a node */
      if((sub_tree->child_list != NULL) || (sub_tree->type == TYPE_OTHER))
        {
          p_export_context->root_handle.data.object_type_reminder = FSAL_NODETYPE_NODE;
        }
      else
        {
          LogEvent(COMPONENT_FSAL, "FSAL BUILD CONTEXT: WARNING: '%s' seems to be a leaf !!!",
                   snmp_path);
        }

      p_export_context->root_mib_tree = tree_head;

    }

  /* save the root path (for lookupPath checks)  */
  if(p_export_path != NULL)
    p_export_context->root_path = *p_export_path;
  else
    FSAL_str2path("/", 2, &p_export_context->root_path);

  LogEvent(COMPONENT_FSAL, "CREATING EXPORT CONTEXT PATH=%s\n", snmp_path);

  if(isFullDebug(COMPONENT_FSAL))
    {
      int i;
      char oidstr[2048], *p = oidstr;

      oidstr[0] = '\0';
      for(i = 0; i < p_export_context->root_handle.data.oid_len; i++)
        p += sprintf(p, ".%ld", p_export_context->root_handle.data.oid_tab[i]);
      LogFullDebug(COMPONENT_FSAL, "oid %s", oidstr);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);

}


/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t SNMPFSAL_CleanUpExportContext(snmpfsal_export_context_t * p_export_context) 
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

fsal_status_t SNMPFSAL_InitClientContext(snmpfsal_op_context_t * p_thr_context)
{

  int rc, i;
  netsnmp_session session;

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  p_thr_context->user_credential.user = 0;
  p_thr_context->user_credential.group = 0;

  /* initialize the SNMP session  */

  snmp_sess_init(&session);
  session.version = snmp_glob_config.snmp_version;
  session.retries = snmp_glob_config.nb_retries;
  session.timeout = snmp_glob_config.microsec_timeout;
  session.peername = snmp_glob_config.snmp_server;
  if(session.version == SNMP_VERSION_3)
    {
      if(!strcasecmp(snmp_glob_config.auth_proto, "MD5"))
        {
          session.securityAuthProto = usmHMACMD5AuthProtocol;
          session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
        }
      else if(!strcasecmp(snmp_glob_config.auth_proto, "SHA"))
        {
          session.securityAuthProto = usmHMACSHA1AuthProtocol;
          session.securityAuthProtoLen = USM_AUTH_PROTO_SHA_LEN;
        }
      if(!strcasecmp(snmp_glob_config.enc_proto, "DES"))
        {
          session.securityPrivProto = usmDESPrivProtocol;
          session.securityPrivProtoLen = USM_PRIV_PROTO_DES_LEN;
        }
      else if(!strcasecmp(snmp_glob_config.enc_proto, "AES"))
        {
          session.securityPrivProto = usmAES128PrivProtocol;
          session.securityPrivProtoLen = USM_PRIV_PROTO_AES128_LEN;
        }

      session.securityName = snmp_glob_config.username; /* securityName is not allocated */
      session.securityNameLen = strlen(snmp_glob_config.username);
      session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;

      session.securityAuthKeyLen = USM_AUTH_KU_LEN;
      if(generate_Ku(session.securityAuthProto,
                     session.securityAuthProtoLen,
                     (u_char *) snmp_glob_config.auth_phrase,
                     strlen(snmp_glob_config.auth_phrase), session.securityAuthKey,
                     &session.securityAuthKeyLen) != SNMPERR_SUCCESS)
        {
          LogCrit(COMPONENT_FSAL,
                  "FSAL INIT CONTEXT: ERROR creating SNMP passphrase for authentification");
          Return(ERR_FSAL_BAD_INIT, snmp_errno, INDEX_FSAL_InitClientContext);
        }

      session.securityPrivKeyLen = USM_PRIV_KU_LEN;
      if(generate_Ku(session.securityAuthProto,
                     session.securityAuthProtoLen,
                     (u_char *) snmp_glob_config.enc_phrase,
                     strlen(snmp_glob_config.enc_phrase), session.securityPrivKey,
                     &session.securityPrivKeyLen) != SNMPERR_SUCCESS)
        {
          LogCrit(COMPONENT_FSAL, "FSAL INIT CONTEXT: ERROR creating SNMP passphrase for encryption");
          Return(ERR_FSAL_BAD_INIT, snmp_errno, INDEX_FSAL_InitClientContext);
        }
    }
  else                          /* v1 or v2c */
    {
      session.community = snmp_glob_config.community;
      session.community_len = strlen(snmp_glob_config.community);
    }
  p_thr_context->snmp_session = snmp_open(&session);

  if(p_thr_context->snmp_session == NULL)
    {
      char *err_msg;
      snmp_error(&session, &errno, &snmp_errno, &err_msg);

      LogCrit(COMPONENT_FSAL, "FSAL INIT CONTEXT: ERROR creating SNMP session: %s", err_msg);
      Return(ERR_FSAL_BAD_INIT, snmp_errno, INDEX_FSAL_InitClientContext);
    }

  p_thr_context->snmp_request = NULL;
  p_thr_context->snmp_response = NULL;
  p_thr_context->current_response = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

 /**
 * FSAL_GetClientContext :
 * Get a user credential from its uid.
 * 
 * \param p_cred (in out, snmpfsal_cred_t *)
 *        Initialized credential to be changed
 *        for representing user.
 * \param uid (in, fsal_uid_t)
 *        user identifier.
 * \param gid (in, fsal_gid_t)
 *        group identifier.
 * \param alt_groups (in, fsal_gid_t *)
 *        list of alternative groups.
 * \param nb_alt_groups (in, fsal_count_t)
 *        number of alternative groups.
 *
 * \return major codes :
 *      - ERR_FSAL_PERM : the current user cannot
 *                        get credentials for this uid.
 *      - ERR_FSAL_FAULT : Bad adress parameter.
 *      - ERR_FSAL_SERVERFAULT : unexpected error.
 */

fsal_status_t SNMPFSAL_GetClientContext(snmpfsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                        snmpfsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups      /* IN */
    )
{

  fsal_status_t st;

  /* sanity check */
  if(!p_thr_context || !p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the specific export context */
  p_thr_context->export_context = p_export_context;

  /* @todo for the moment, we only set uid and gid,
   * but they are not really used for authentication.
   */
  p_thr_context->user_credential.user = uid;
  p_thr_context->user_credential.group = gid;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);

}

/* @} */
