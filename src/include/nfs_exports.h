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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs_exports.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/23 16:15:10 $
 * \version $Revision: 1.32 $
 * \brief   Prototypes for what's related to export list management.
 *
 * nfs_exports.h : Prototypes for what's related to export list management.
 *
 */

#ifndef _NFS_EXPORTS_H
#define _NFS_EXPORTS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "ganesha_rpc.h"
#ifdef _HAVE_GSSAPI
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#endif
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>              /* for having MAXHOSTNAMELEN */
#include "HashData.h"
#include "HashTable.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_stat.h"
#include "cache_inode.h"
#include "nfs_ip_stats.h"
#include "nlm_list.h"
#include "city.h"

/*
 * Export List structure 
 */
#define EXPORT_KEY_SIZE 8
#define ANON_UID      -2
#define ANON_GID      -2

#define EXPORT_LINESIZE 1024
#define INPUT_SIZE      1024
#define EXPORT_BY_ID_HASHSIZE 1024

typedef struct eid_cache_ {
  struct eid_cache_ *eidc_next;
  exportlist_t *eidc_cache_entry;
} eid_cache_t;

typedef struct export_by_id_ {
  eid_cache_t *eid_cache;
} export_by_id_t;

typedef struct exportlist_client_hostif__
{
  unsigned int clientaddr;
  struct in6_addr clientaddr6;
} exportlist_client_hostif_t;

typedef struct exportlist_client_net__
{
  unsigned int netaddr;
  unsigned int netmask;
} exportlist_client_net_t;

typedef struct exportlist_client_netgrp__
{
  char netgroupname[MAXHOSTNAMELEN];
} exportlist_client_netgrp_t;

typedef struct exportlist_client_wildcard_host__
{
  char wildcard[MAXHOSTNAMELEN];
} exportlist_client_wildcard_host_t;

#define GSS_DEFINE_LEN_TEMP 255
typedef struct exportlist_client_gss__
{
  char princname[GSS_DEFINE_LEN_TEMP];
} exportlist_client_gss_t;

typedef enum exportlist_client_type__
{ 
  HOSTIF_CLIENT       = 1,
  NETWORK_CLIENT      = 2,
  NETGROUP_CLIENT     = 3,
  WILDCARDHOST_CLIENT = 4,
  GSSPRINCIPAL_CLIENT = 5,
  HOSTIF_CLIENT_V6    = 6,
  MATCH_ANY_CLIENT    = 7,
  BAD_CLIENT          = 8
} exportlist_client_type_t;

typedef enum exportlist_status__
{ EXPORTLIST_OK = 1,
  EXPORTLIST_UNAVAILABLE = 2
} exportlist_status_t;

typedef union exportlist_client_union__
{
  exportlist_client_hostif_t hostif;
  exportlist_client_net_t network;
  exportlist_client_netgrp_t netgroup;
  exportlist_client_wildcard_host_t wildcard;
  exportlist_client_gss_t gssprinc;
} exportlist_client_union_t;

typedef struct export_perms__
{
  uid_t anonymous_uid;          /* root uid when no root access is available   */
                                /* uid when access is available but all users are being squashed. */
  gid_t anonymous_gid;          /* root gid when no root access is available   */
                                /* gid when access is available but all users are being squashed. */
  unsigned int options;         /* avail. mnt options */
} export_perms_t;

typedef struct exportlist_client_entry__
{
  struct glist_head cle_list;
  exportlist_client_type_t type;
  exportlist_client_union_t client;
  export_perms_t client_perms;         /* avail. mnt options */
} exportlist_client_entry_t;

typedef struct exportlist_client__
{
  unsigned int num_clients;     /* num clients        */
  struct glist_head client_list; /* Allowed clients */
} exportlist_client_t;

/* fsal up filter list is needed in exportlist.
 * Inluding fsal_up.h would cause header file issues however. */
#ifdef _USE_FSAL_UP
struct fsal_up_filter_list_t_;
#endif

struct exportlist__
{
  struct glist_head exp_list;
  unsigned short id;            /* entry identifier   */
  char fullpath[MAXPATHLEN+2];  /* the path from the root */
  char pseudopath[MAXPATHLEN+2];/* nfsv4 pseudo-filesystem 'virtual' path */
  char referral[MAXPATHLEN+2];  /* String describing NFSv4 referral */

  char FS_specific[MAXPATHLEN]; /* filesystem specific option string */
  char FS_tag[MAXPATHLEN];      /* filesystem "tag" string */
  fsal_export_context_t FS_export_context;      /* the export context associated with this export entry */

  fsal_fsid_t filesystem_id;    /* fileset id         */
  fsal_handle_t *proot_handle;  /* FSAL handle for the root of the file system */

  export_perms_t export_perms;  /* avail. mnt options */

  bool_t use_ganesha_write_buffer;
  bool_t use_commit;

  fsal_size_t MaxRead;          /* Max Read for this entry                           */
  fsal_size_t MaxWrite;         /* Max Write for this entry                          */
  fsal_size_t PrefRead;         /* Preferred Read size                               */
  fsal_size_t PrefWrite;        /* Preferred Write size                              */
  fsal_size_t PrefReaddir;      /* Preferred Readdir size                            */
  fsal_off_t MaxOffsetWrite;    /* Maximum Offset allowed for write                  */
  fsal_off_t MaxOffsetRead;     /* Maximum Offset allowed for read                   */
  unsigned int UseCookieVerifier;       /* Is Cookie verifier to be used ?                   */
  exportlist_client_t clients;  /* allowed clients                                   */

  pthread_mutex_t   exp_state_mutex; /* Mutex to protect the following two lists */
  struct glist_head exp_state_list;  /* List of NFS v4 state belonging to this export */
#ifdef _USE_NLM
  struct glist_head exp_lock_list;   /* List of locks belonging to this export
                                      * Only need this list if NLM, otherwise
                                      * state list is sufficient
                                      */
#endif

#ifdef _USE_FSAL_UP
  bool_t use_fsal_up;
  char fsal_up_type[MAXPATHLEN];
  fsal_time_t fsal_up_timeout;
  pthread_t fsal_up_thr; /* This value may be modified later to point to an FSAL CB thread. */
  struct fsal_up_filter_list_t_ *fsal_up_filter_list; /* List of filters to apply through FSAL CB interface. */
#endif /* _USE_FSAL_UP */

#ifdef _USE_STAT_EXPORTER
  nfs_worker_stat_t *worker_stats; /* List of worker stats to support per-share stat. */
#endif

  fsal_u64_t      exp_mounted_on_file_id; /* Id of the node this export is mounted on */
  cache_entry_t * exp_root_cache_inode;   /* cache inode entry for the root of this export  */
};

/* Constant for options masks */
#define EXPORT_OPTION_NOSUID          0x00000001        /* mask off setuid mode bit            */
#define EXPORT_OPTION_NOSGID          0x00000002        /* mask off setgid mode bit            */
#define EXPORT_OPTION_ROOT            0x00000004        /* allow root access as root uid       */
#define EXPORT_OPTION_ALL_ANONYMOUS   0x00000008        /* all users are squashed to anonymous */
#define EXPORT_OPTION_READ_ACCESS     0x00000010        /* R_Access= option specified          */
#define EXPORT_OPTION_WRITE_ACCESS    0x00000020        /* RW_Access= option specified         */
#define EXPORT_OPTION_RW_ACCESS       (EXPORT_OPTION_READ_ACCESS     | \
                                       EXPORT_OPTION_WRITE_ACCESS)
#define EXPORT_OPTION_MD_WRITE_ACCESS 0x00000040        /* MDONLY_Access= option specified     */
#define EXPORT_OPTION_MD_READ_ACCESS  0x00000080        /* MDONLY_RO_Access= option specified  */
#define EXPORT_OPTION_MD_ACCESS       (EXPORT_OPTION_MD_WRITE_ACCESS | \
                                       EXPORT_OPTION_MD_READ_ACCESS)
#define EXPORT_OPTION_MODIFY_ACCESS   (EXPORT_OPTION_WRITE_ACCESS | \
                                       EXPORT_OPTION_MD_WRITE_ACCESS)
#define EXPORT_OPTION_ACCESS_TYPE     (EXPORT_OPTION_READ_ACCESS     | \
                                       EXPORT_OPTION_WRITE_ACCESS    | \
                                       EXPORT_OPTION_MD_WRITE_ACCESS | \
                                       EXPORT_OPTION_MD_READ_ACCESS)
#define EXPORT_OPTION_CUR_ACCESS      (EXPORT_OPTION_ROOT            | \
                                       EXPORT_OPTION_READ_ACCESS     | \
                                       EXPORT_OPTION_WRITE_ACCESS    | \
                                       EXPORT_OPTION_RW_ACCESS       | \
                                       EXPORT_OPTION_MD_WRITE_ACCESS | \
                                       EXPORT_OPTION_MD_READ_ACCESS  | \
                                       EXPORT_OPTION_MD_ACCESS)

#define EXPORT_OPTION_PSEUDO          0x00000100        /* pseudopath is provided              */
#define EXPORT_OPTION_MAXREAD         0x00000200        /* Max read is provided                */
#define EXPORT_OPTION_MAXWRITE        0x00000400        /* Max write is provided               */
#define EXPORT_OPTION_PREFREAD        0x00000800        /* Pref read is provided               */
#define EXPORT_OPTION_PREFWRITE       0x00001000        /* Pref write is provided              */
#define EXPORT_OPTION_PREFRDDIR       0x00002000        /* Pref readdir size is provided       */
#define EXPORT_OPTION_PRIVILEGED_PORT 0x00004000        /* clients use only privileged port    */
#define EXPORT_OPTION_NETBIOS_NAME    0x00008000        /* Strip_NETBIOS_Name= option specified*/

/* @todo BUGAZOMEU : Mettre au carre les flags des flavors */

#define EXPORT_OPTION_AUTH_NONE       0x00010000        /* Auth None authentication supported  */
#define EXPORT_OPTION_AUTH_UNIX       0x00020000        /* Auth Unix authentication supported  */

#define EXPORT_OPTION_RPCSEC_GSS_NONE 0x00040000        /* RPCSEC_GSS_NONE supported           */
#define EXPORT_OPTION_RPCSEC_GSS_INTG 0x00080000        /* RPCSEC_GSS INTEGRITY supported      */
#define EXPORT_OPTION_RPCSEC_GSS_PRIV 0x00100000        /* RPCSEC_GSS PRIVACY supported        */
#define EXPORT_OPTION_AUTH_TYPES      (EXPORT_OPTION_AUTH_NONE       | \
                                       EXPORT_OPTION_AUTH_UNIX       | \
                                       EXPORT_OPTION_RPCSEC_GSS_NONE | \
                                       EXPORT_OPTION_RPCSEC_GSS_INTG | \
                                       EXPORT_OPTION_RPCSEC_GSS_PRIV)

/* protocol flags */
#define EXPORT_OPTION_NFSV2           0x00200000        /* NFSv2 operations are supported      */
#define EXPORT_OPTION_NFSV3           0x00400000        /* NFSv3 operations are supported      */
#define EXPORT_OPTION_NFSV4           0x00800000        /* NFSv4 operations are supported      */
#define EXPORT_OPTION_UDP             0x01000000        /* UDP protocol is supported      */
#define EXPORT_OPTION_TCP             0x02000000        /* TCP protocol is supported      */
#define EXPORT_OPTION_PROTOCOLS       (EXPORT_OPTION_NFSV2           | \
                                       EXPORT_OPTION_NFSV3           | \
                                       EXPORT_OPTION_NFSV4)
#define EXPORT_OPTION_TRANSPORTS      (EXPORT_OPTION_UDP             | \
                                       EXPORT_OPTION_TCP)
#define EXPORT_OPTION_ALL_ACCESS      (EXPORT_OPTION_ROOT            | \
                                       EXPORT_OPTION_ALL_ANONYMOUS   | \
                                       EXPORT_OPTION_READ_ACCESS     | \
                                       EXPORT_OPTION_WRITE_ACCESS    | \
                                       EXPORT_OPTION_RW_ACCESS       | \
                                       EXPORT_OPTION_MD_WRITE_ACCESS | \
                                       EXPORT_OPTION_MD_READ_ACCESS  | \
                                       EXPORT_OPTION_MD_ACCESS       | \
                                       EXPORT_OPTION_PRIVILEGED_PORT | \
                                       EXPORT_OPTION_AUTH_NONE       | \
                                       EXPORT_OPTION_AUTH_UNIX       | \
                                       EXPORT_OPTION_RPCSEC_GSS_NONE | \
                                       EXPORT_OPTION_RPCSEC_GSS_INTG | \
                                       EXPORT_OPTION_RPCSEC_GSS_PRIV | \
                                       EXPORT_OPTION_NFSV2           | \
                                       EXPORT_OPTION_NFSV3           | \
                                       EXPORT_OPTION_NFSV4           | \
                                       EXPORT_OPTION_UDP             | \
                                       EXPORT_OPTION_TCP)
#define EXPORT_OPTION_BASE_ACCESS     (EXPORT_OPTION_PROTOCOLS       | \
                                       EXPORT_OPTION_TRANSPORTS      | \
                                       EXPORT_OPTION_AUTH_TYPES      | \
                                       EXPORT_OPTION_ALL_ANONYMOUS   | \
                                       EXPORT_OPTION_PRIVILEGED_PORT)

/* Maximum offset set for R/W */
#define EXPORT_OPTION_MAXOFFSETWRITE  0x04000000        /* Maximum Offset for write is set  */
#define EXPORT_OPTION_MAXOFFSETREAD   0x08000000        /* Maximum Offset for read is set   */
#define EXPORT_OPTION_ACCESS_OPT_LIST 0x10000000        /* Access list from EXPORT_CLIENT stanza */
#define EXPORT_OPTION_USE_PNFS        0x20000000        /* Using pNFS or not using pNFS ?   */
#define EXPORT_OPTION_USE_UQUOTA      0x40000000        /* Using user quota for this export */

#define EXPORT_OPTION_ACCESS_LIST     0x80000000        /* Flags access list entry as Access=  */

/* NFS4 specific structures */

/*
 * PseudoFs Tree
 */
typedef struct pseudofs_entry
{
  struct avltree_node idavlnode;      /** AVL node in tree */
  struct avltree_node nameavlnode;      /** AVL node in tree */
  char name[MAXNAMLEN+1];           /** The entry name, not full path */
  uint8_t *fsopaque;                /** do not garbage collect this, it points
                                        to an already gc'd file_handle_v4_t. 
                                        This is used for convenience when
                                        converting from entry to handle. */
  struct pseudofs_entry *parent;
  uint64_t pseudo_id;               /** ID within the pseudoFS */
  exportlist_t *junction_export;    /** Export list related to the junction, NULL if entry is no junction*/
  struct avltree child_tree_byname; /** tree of children organized by name */
  struct avltree child_tree_byid;   /** tree of children organized by id */
} pseudofs_entry_t;

typedef struct pseudofs
{
  pseudofs_entry_t root;
} pseudofs_t;

typedef struct nfs_client_cred_gss__
{
  unsigned int svc;
  unsigned int qop;
#ifdef _HAVE_GSSAPI
  gss_ctx_id_t gss_context_id;
#endif
} nfs_client_cred_gss_t;

typedef struct nfs_client_cred__
{
  unsigned int flavor;
  unsigned int length;
  union
  {
    struct authunix_parms auth_unix;
    nfs_client_cred_gss_t auth_gss;
  } auth_union;
} nfs_client_cred_t;

typedef struct nfs_worker_data__ nfs_worker_data_t;

/**
 * @brief NFS v4 Compound Data
 *
 * This structure contains the necessary stuff for keeping the state
 * of a V4 compound request.
 */
/* Forward references to SAL types */
typedef struct nfs41_session__ nfs41_session_t;
typedef struct nfs_client_id_t nfs_client_id_t;
typedef struct COMPOUND4res_extended COMPOUND4res_extended;

/**
 * @brief Compound data
 *
 * This structure contains the necessary stuff for keeping the state
 * of a V4 compound request.
 */
typedef struct compoud_data
{
  nfs_fh4 currentFH; /*< Current filehandle */
  nfs_fh4 rootFH; /*< Root filehandle */
  nfs_fh4 savedFH; /*< Saved filehandle */
  nfs_fh4 publicFH; /*< Public filehandle */
  stateid4 current_stateid; /*< Current stateid */
  bool_t current_stateid_valid; /*< Current stateid is valid */
  unsigned int minorversion; /*< NFSv4 minor version */
  cache_entry_t *current_entry; /*< Cache entry for current filehandle */
  cache_entry_t *saved_entry; /*< Cache entry for saved filehandle */
  cache_inode_file_type_t current_filetype; /*< File type of current entry */
  cache_inode_file_type_t saved_filetype; /*< File type of saved entry */
  fsal_op_context_t *pcontext; /*< Credentials related to this
                                   fileset (to handle different uid
                                   mapping) */
  exportlist_t *pexport; /*< Export entry related to the currentFH */
  exportlist_t *saved_pexport; /*< Export entry related to the savedFH */
  export_perms_t export_perms; /*< Permissions for export for currentFH */
  export_perms_t saved_export_perms; /*< Permissions for export for savedFH */
  pseudofs_t *pseudofs; /*< Pointer to the pseudo filesystem tree */
  struct svc_req *reqp; /*< RPC Request related to the compound */
  struct nfs_worker_data__ *pworker; /*< Worker thread data */
  nfs_client_cred_t credential; /*< Raw RPC credentials */
  nfs_client_id_t *preserved_clientid; /*< clientid that has lease
                                           reserved, if any */
#ifdef _USE_NFS4_1
  COMPOUND4res_extended *pcached_res; /*< NFv41: pointer to cached RPC res in
                                          a session's slot */
  bool_t use_drc; /*< Set to TRUE if session DRC is to be used */
  uint32_t oppos; /*< Position of the operation within the request
                      processed  */
  nfs41_session_t *psession; /*< Related session (found by OP_SEQUENCE) */
#endif                          /* USE_NFS4_1 */
} compound_data_t;

/* Exports by id cache */
extern export_by_id_t export_by_id;

/* Export list data */
extern struct glist_head exportlist;

/* Export list related functions */
sockaddr_t * check_convert_ipv6_to_ipv4(sockaddr_t * ipv6, sockaddr_t *ipv4);

exportlist_t *nfs_Get_export_by_id(struct glist_head * exportlist,
                                   unsigned short exportid);
exportlist_t *nfs_Get_export_by_path(struct glist_head * exportlist,
                                     char * path);
exportlist_t *nfs_Get_export_by_pseudo(struct glist_head * exportlist,
                                       char * path);
exportlist_t *nfs_Get_export_by_tag(struct glist_head * exportlist,
                                    char * tag);
void nfs_check_anon(export_perms_t * pexport_perms,
                    exportlist_t * pexport,
                    struct user_cred *user_credentials);
int nfs_build_fsal_context(struct svc_req *ptr_req,
                           exportlist_t * pexport,
                           fsal_op_context_t * pcontext,
                           struct user_cred *user_credentials);
int get_req_uid_gid(struct svc_req *ptr_req,
                    struct user_cred *user_credentials);

void init_credentials(struct user_cred *user_credentials);
void clean_credentials(struct user_cred *user_credentials);

int nfs_compare_clientcred(nfs_client_cred_t * pcred1, nfs_client_cred_t * pcred2);
int nfs_rpc_req2client_cred(struct svc_req *reqp, nfs_client_cred_t * pcred);

int export_client_match_any(sockaddr_t                * hostaddr,
                            exportlist_client_t       * clients,
                            exportlist_client_entry_t * pclient_found,
                            unsigned int                export_option);

void nfs_export_check_access(sockaddr_t     * hostaddr,
                             exportlist_t   * pexport,
                             export_perms_t * pexport_perms);

int nfs_export_check_security(struct svc_req * ptr_req,
                              export_perms_t * p_export_perms,
                              exportlist_t   * pexport);

int nfs_export_tag2path(struct glist_head * pexportlist,
                        char *tag, int taglen,
                        char *path, int pathlen);

void LogClientListEntry(log_components_t            component,
                        exportlist_client_entry_t * entry);

void FreeClientList(exportlist_client_t * clients);
void RemoveExportEntry(exportlist_t * p_entry);

void set_mounted_on_fileid(cache_entry_t      * entry,
                           fsal_attrib_list_t * attr,
                           exportlist_t       * exp);

void squash_setattr(export_perms_t     * pexport_perms,
                    struct user_cred   * user_credentials,
                    fsal_attrib_list_t * attr);

#endif                          /* _NFS_EXPORTS_H */
