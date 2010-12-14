/*
 *
 *
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

/**
 * \file    fsal_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 * \brief   File System specific types and constants.
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

#include "config_parsing.h"
#include "err_fsal.h"

#include <sys/types.h>
#include <sys/param.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "fsal_glue_const.h"

#define fsal_handle_t snmpfsal_handle_t
#define fsal_op_context_t snmpfsal_op_context_t
#define fsal_file_t snmpfsal_file_t
#define fsal_dir_t snmpfsal_dir_t
#define fsal_export_context_t snmpfsal_export_context_t
#define fsal_lockdesc_t snmpfsal_lockdesc_t
#define fsal_cookie_t snmpfsal_cookie_t
#define fs_specific_initinfo_t snmpfs_specific_initinfo_t
#define fsal_cred_t snmpfsal_cred_t


#ifdef _APPLE
#define HOST_NAME_MAX          64
#endif

  /* Change bellow the label of your filesystem configuration
   * section in the GANESHA's configuration file.
   */
# define CONF_LABEL_FS_SPECIFIC   "SNMP"

  /* In this section, you must define your own FSAL internal types.
   * Here are some template types :
   */

/* prefered readdir size */

//# define FSAL_MAX_NAME_LEN  MAXLABEL
//# define FSAL_MAX_PATH_LEN  SNMP_MAXPATH

#define FSAL_MAX_PROTO_LEN  16
#define FSAL_MAX_USERNAME_LEN   256
#define FSAL_MAX_PHRASE_LEN   USM_AUTH_KU_LEN

typedef enum
{
  FSAL_NODETYPE_ROOT = 1,
  FSAL_NODETYPE_NODE = 2,
  FSAL_NODETYPE_LEAF = 3
} nodetype_t;

  /* The handle consists in an oid table.  */

typedef union {
 struct 
  {
    oid oid_tab[MAX_OID_LEN];
    size_t oid_len;
    nodetype_t object_type_reminder;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} snmpfsal_handle_t;

typedef struct fsal_cred__
{
  fsal_uid_t user;
  fsal_gid_t group;
  /*
     int    ticket_handle;
     time_t ticket_renewal_time;
   */
} snmpfsal_cred_t;

typedef struct fsal_export_context__
{
  snmpfsal_handle_t root_handle;
  struct tree *root_mib_tree;
  fsal_path_t root_path;

} snmpfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct fsal_op_context__
{
  /* the export context for the next request */
  snmpfsal_export_context_t *export_context;    /* Must be the first entry in this structure */

  /* user authentication info */
  snmpfsal_cred_t user_credential;

  /* SNMP session and the associated info  */
  netsnmp_session *snmp_session;
  netsnmp_pdu *snmp_request;
  netsnmp_pdu *snmp_response;
  netsnmp_variable_list *current_response;

} snmpfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct fsal_dir__
{
  snmpfsal_handle_t node_handle;
  snmpfsal_op_context_t *p_context;
} snmpfsal_dir_t;

typedef struct fsal_file__
{
  snmpfsal_handle_t file_handle;
  snmpfsal_op_context_t *p_context;

  enum
  {
    FSAL_MODE_READ = 1,
    FSAL_MODE_WRITE = 2
  } rw_mode;

} snmpfsal_file_t;

//# define FSAL_FILENO(_f) (0)

typedef union {
  struct fsal_cookie__
  {
    /* in SNMP the cookie is the last listed entry */
    oid oid_tab[MAX_OID_LEN];
    unsigned int oid_len;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} snmpfsal_cookie_t;

//static snmpfsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { {0,}, 0 };

typedef struct fs_specific_initinfo__
{
  long snmp_version;
  char snmp_server[HOST_NAME_MAX];
  char community[COMMUNITY_MAX_LEN];
  int nb_retries;               /* Number of retries before timeout */
  int microsec_timeout;         /* Number of uS until first timeout, then exponential backoff */
  int enable_descriptions;
  char client_name[HOST_NAME_MAX];
  unsigned int getbulk_count;
  char auth_proto[FSAL_MAX_PROTO_LEN];
  char enc_proto[FSAL_MAX_PROTO_LEN];
  char username[FSAL_MAX_NAME_LEN];
  char auth_phrase[FSAL_MAX_PHRASE_LEN];
  char enc_phrase[FSAL_MAX_PHRASE_LEN];
} snmpfs_specific_initinfo_t;

typedef void *snmpfsal_lockdesc_t;


#endif                          /* _FSAL_TYPES_SPECIFIC_H */
