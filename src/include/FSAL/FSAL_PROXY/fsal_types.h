/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
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
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#endif
#include "nfs4.h"

#define CONF_LABEL_FS_SPECIFIC   "NFSv4_Proxy"

# define FSAL_MAX_NAME_LEN  256
# define FSAL_MAX_PATH_LEN  1024

#define FSAL_NGROUPS_MAX  32

/* prefered readdir size */
/* #define FSAL_READDIR_SIZE 2048  */
#define FSAL_READDIR_SIZE 3072

/** object name.  */

typedef struct fsal_name__
{
  char name[FSAL_MAX_NAME_LEN];
  unsigned int len;
} fsal_name_t;

/** object path.  */

typedef struct fsal_path__
{
  char path[FSAL_MAX_PATH_LEN];
  unsigned int len;
} fsal_path_t;

# define FSAL_NAME_INITIALIZER {"",0}
# define FSAL_PATH_INITIALIZER {"",0}

#define FSAL_PROXY_FILEHANDLE_MAX_LEN 128
#define FSAL_PROXY_SEND_BUFFER_SIZE   32768
#define FSAL_PROXY_RECV_BUFFER_SIZE   32768
#define FSAL_PROXY_NFS_V4             4
#define FSAL_PROXY_RETRY_SLEEPTIME    10

static fsal_name_t __attribute__ ((__unused__)) FSAL_DOT =
{
".", 1};

static fsal_name_t __attribute__ ((__unused__)) FSAL_DOT_DOT =
{
"..", 2};

  /* some void types for this template... */
typedef struct fsal_handle__
{
  fsal_nodetype_t object_type_reminder;
  uint64_t fileid4;
  unsigned int timestamp;
  unsigned int srv_handle_len;
  char srv_handle_val[FSAL_PROXY_FILEHANDLE_MAX_LEN];
} fsal_handle_t;

typedef struct fsal_cred__
{
  fsal_uid_t user;
  fsal_gid_t group;
  fsal_count_t nbgroups;
  fsal_gid_t alt_groups[FSAL_NGROUPS_MAX];
} fsal_cred_t;

typedef struct fsal_export_context__
{
  fsal_handle_t root_handle;
} fsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(pexport_context->root_handle.fileid4)

typedef struct fsal_op_context__
{
  fsal_cred_t user_credential;
  fsal_export_context_t *export_context;

  unsigned int retry_sleeptime;
  unsigned int srv_prognum;
  unsigned int srv_addr;
  unsigned int srv_sendsize;
  unsigned int srv_recvsize;
  unsigned short srv_port;
  char srv_proto[MAXNAMLEN];
  clientid4 clientid;
  CLIENT *rpc_client;
  pthread_mutex_t lock;
  fsal_handle_t openfh_wd_handle;
  time_t last_lease_renewal;
  uint64_t file_counter;
} fsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->user_credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->user_credential.group )

typedef struct fsal_dir__
{
  fsal_handle_t fhandle;
  verifier4 verifier;
  fsal_op_context_t *pcontext;
} fsal_dir_t;

typedef struct fsal_file__
{
  fsal_handle_t fhandle;
  unsigned int openflags;
  u_int32_t ownerid;
  stateid4 stateid;
  fsal_off_t current_offset;
  fsal_op_context_t *pcontext;
} fsal_file_t;

# define FSAL_FILENO(_pf) ((int)(_pf))

typedef nfs_cookie4 fsal_cookie_t;

#define FSAL_READDIR_FROM_BEGINNING 0

typedef struct fs_specific_initinfo__
{
  unsigned int retry_sleeptime;
  unsigned int srv_addr;
  unsigned int srv_prognum;
  unsigned int srv_sendsize;
  unsigned int srv_recvsize;
  unsigned int srv_timeout;
  unsigned short srv_port;
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

} fs_specific_initinfo_t;

typedef unsigned int fsal_lockdesc_t;

#endif
