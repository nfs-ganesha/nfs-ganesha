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
 * \brief   File System Abstraction Layer types and constants.
 *
 *
 *
 */

#ifndef _FSAL_TYPES__SPECIFIC_H
#define _FSAL_TYPES__SPECIFIC_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * FS relative includes
 */

#ifdef _HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include <dirent.h>
#endif

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"
#include "rpc.h"
#include "nfs4.h"

#define CONF_LABEL_FS_SPECIFIC   "NFSv4_Proxy"

#define FSAL_PROXY_FILEHANDLE_MAX_LEN 128
#define FSAL_PROXY_SEND_BUFFER_SIZE   32768
#define FSAL_PROXY_RECV_BUFFER_SIZE   32768
#define FSAL_PROXY_NFS_V4             4
#define FSAL_PROXY_RETRY_SLEEPTIME    10

#include "fsal_glue_const.h"

  /* some void types for this template... */
typedef union {
 struct 
  {
    fsal_nodetype_t object_type_reminder;
    uint64_t fileid4;
    unsigned int timestamp;
    unsigned int srv_handle_len;
    char srv_handle_val[FSAL_PROXY_FILEHANDLE_MAX_LEN] ; 
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
}  proxyfsal_handle_t;

typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  proxyfsal_handle_t root_handle;
} proxyfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(pexport_context->root_handle.fileid4)

typedef struct
{
  proxyfsal_export_context_t *export_context;   /* Must be the first entry in this structure */
  struct user_credentials credential;

  unsigned int retry_sleeptime;
  unsigned int srv_prognum;
  unsigned int srv_addr;
  unsigned int srv_sendsize;
  unsigned int srv_recvsize;
  unsigned short srv_port;
  unsigned int use_privileged_client_port ;
  char srv_proto[MAXNAMLEN];
  clientid4 clientid;
  time_t    clientid_renewed ;
  CLIENT *rpc_client;
  int socket ;
  pthread_mutex_t lock;
  proxyfsal_handle_t openfh_wd_handle;
  time_t last_lease_renewal;
  uint64_t file_counter;
} proxyfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct
{
  proxyfsal_handle_t fhandle;
  verifier4 verifier;
  proxyfsal_op_context_t *pcontext;
} proxyfsal_dir_t;

typedef struct
{
  proxyfsal_handle_t fhandle;
  unsigned int openflags;
  u_int32_t ownerid;
  stateid4 stateid;
  fsal_off_t current_offset;
  proxyfsal_op_context_t *pcontext;
} proxyfsal_file_t;

//# define FSAL_FILENO(_pf) ((_pf))

typedef union {
  nfs_cookie4 data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif

#define FSAL_SET_PCOOKIE_BY_OFFSET( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   ((proxyfsal_cookie_t *)__pfsal_cookie)->data = (off_t)__cookie ; \
} while( 0 )

#define FSAL_SET_OFFSET_BY_PCOOKIE( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   __cookie =  ((proxyfsal_cookie_t *)__pfsal_cookie)->data ;       \
} while( 0 )

} proxyfsal_cookie_t;

// #define FSAL_READDIR_FROM_BEGINNING 0

typedef struct
{
  unsigned int retry_sleeptime;
  unsigned int srv_addr;
  unsigned int srv_prognum;
  unsigned int srv_sendsize;
  unsigned int srv_recvsize;
  unsigned int srv_timeout;
  unsigned short srv_port;
  unsigned int use_privileged_client_port ;
  char srv_proto[MAXNAMLEN];
  char local_principal[MAXNAMLEN];
  char remote_principal[MAXNAMLEN];
  char keytab[MAXPATHLEN];
  unsigned int cred_lifetime;
  unsigned int sec_type;
  bool_t active_krb5;
  char openfh_wd[MAXPATHLEN];

  /* initialization info for handle mapping */

  int enable_handle_mapping;

  char hdlmap_dbdir[MAXPATHLEN];
  char hdlmap_tmpdir[MAXPATHLEN];
  unsigned int hdlmap_dbcount;
  unsigned int hdlmap_hashsize;
  unsigned int hdlmap_nb_entry_prealloc;
  unsigned int hdlmap_nb_db_op_prealloc;
} proxyfs_specific_initinfo_t;

#endif
