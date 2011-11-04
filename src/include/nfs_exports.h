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

#include <sys/types.h>
#include <sys/param.h>

#include "rpc.h"
#ifdef _HAVE_GSSAPI
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#endif
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>              /* for having MAXHOSTNAMELEN */
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_stat.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_ip_stats.h"

/*
 * Export List structure 
 */
#define EXPORT_KEY_SIZE 8
#define ANON_UID      -2
#define ANON_GID      -2

#define EXPORT_LINESIZE 1024
#define INPUT_SIZE      1024

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

typedef enum exportlist_access_type__
{
  ACCESSTYPE_RW        = 1,     /* All operations are allowed                */
  ACCESSTYPE_RO        = 2,     /* Filesystem is readonly (nfs_read allowed) */
  ACCESSTYPE_MDONLY    = 3,     /* Data operations are forbidden             */
  ACCESSTYPE_MDONLY_RO = 4      /* Data operations are forbidden,
                                   and the filesystem is read-only.          */
} exportlist_access_type_t;

typedef enum exportlist_client_type__
{ 
  HOSTIF_CLIENT       = 1,
  NETWORK_CLIENT      = 2,
  NETGROUP_CLIENT     = 3,
  WILDCARDHOST_CLIENT = 4,
  GSSPRINCIPAL_CLIENT = 5,
  HOSTIF_CLIENT_V6    = 6,
  BAD_CLIENT          = 7
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

typedef struct exportlist_client_entry__
{
  exportlist_client_type_t type;
  exportlist_client_union_t client;
  unsigned int options;         /* avail. mnt options */
} exportlist_client_entry_t;

#define EXPORTS_NB_MAX_CLIENTS 128

typedef struct exportlist_client__
{
  unsigned int num_clients;     /* num clients        */
  exportlist_client_entry_t clientarray[EXPORTS_NB_MAX_CLIENTS];        /* allowed clients    */
} exportlist_client_t;

/* fsal up filter list is needed in exportlist.
 * Inluding fsal_up.h would cause header file issues however. */
#ifdef _USE_FSAL_UP
struct fsal_up_filter_list_t_;
#endif

typedef struct exportlist__
{
  unsigned short id;            /* entry identifier   */
  exportlist_status_t status;   /* entry's status     */
  char dirname[MAXNAMLEN];      /* path relative to fs root */
  char fullpath[MAXPATHLEN];    /* the path from the root */
  char fsname[MAXNAMLEN];       /* File system name, MAXNAMLEN is used for wanting of a better constant */
  char pseudopath[MAXPATHLEN];  /* nfsv4 pseudo-filesystem 'virtual' path */
  char referral[MAXPATHLEN];    /* String describing NFSv4 referral */

  char FS_specific[MAXPATHLEN]; /* filesystem specific option string */
  char FS_tag[MAXPATHLEN];      /* filesystem "tag" string */
  fsal_export_context_t FS_export_context;      /* the export context associated with this export entry */

  exportlist_access_type_t access_type; /* allowed operations for this export. Used by the older Access
                                         * list Access_Type export permissions scheme as well as the newer
                                         * R_Access, RW_Access, MDONLY_Access, MDONLY_R_Access lists.*/
  bool_t new_access_list_version;   /* the new access list version (TRUE) is teh *_Access lists.
                                     * The old (FALSE) is Access and Access_Type. */

  fsal_fsid_t filesystem_id;    /* fileset id         */
  fsal_handle_t *proot_handle;  /* FSAL handle for the root of the file system */

  uid_t anonymous_uid;          /* root uid when no root access is available   */
                                /* uid when access is available but all users are being squashed. */
  gid_t anonymous_gid;          /* root gid when no root access is available   */
                                /* gid when access is available but all users are being squashed. */
  bool_t all_anonymous;         /* When set to true, all users including root will be given the anon uid/gid */
  unsigned int options;         /* avail. mnt options */

  unsigned char seckey[EXPORT_KEY_SIZE];        /* Checksum for FH validity */

  bool_t use_ganesha_write_buffer;
  bool_t use_commit;

  fsal_size_t MaxRead;          /* Max Read for this entry                           */
  fsal_size_t MaxWrite;         /* Max Write for this entry                          */
  fsal_size_t PrefRead;         /* Preferred Read size                               */
  fsal_size_t PrefWrite;        /* Preferred Write size                              */
  fsal_size_t PrefReaddir;      /* Preferred Readdir size                            */
  fsal_off_t MaxOffsetWrite;    /* Maximum Offset allowed for write                  */
  fsal_off_t MaxOffsetRead;     /* Maximum Offset allowed for read                   */
  fsal_off_t MaxCacheSize;      /* Maximum Cache Size allowed                        */
  unsigned int UseCookieVerifier;       /* Is Cookie verifier to be used ?                   */
  exportlist_client_t clients;  /* allowed clients                                   */
  struct exportlist__ *next;    /* next entry                                        */
  unsigned int fsalid ;

  cache_inode_policy_t cache_inode_policy ;

#ifdef _USE_FSAL_UP
  bool_t use_fsal_up;
  char fsal_up_type[MAXPATHLEN];
  fsal_time_t fsal_up_timeout;
  pthread_t fsal_up_thr; /* This value may be modified later to point to an FSAL CB thread. */
  struct fsal_up_filter_list_t_ *fsal_up_filter_list; /* List of filters to apply through FSAL CB interface. */
#endif /* _USE_FSAL_UP */
} exportlist_t;

/* Used to record the uid and gid of the client that made a request. */
struct user_cred {
  uid_t caller_uid;
  gid_t caller_gid;
  unsigned int caller_glen;
  gid_t *caller_garray;
};

/* Constant for options masks */
#define EXPORT_OPTION_NOSUID          0x00000001        /* mask off setuid mode bit            */
#define EXPORT_OPTION_NOSGID          0x00000002        /* mask off setgid mode bit            */
#define EXPORT_OPTION_ROOT            0x00000004        /* allow root access as root uid       */
#define EXPORT_OPTION_NETENT          0x00000008        /* client entry is a network entry     */
#define EXPORT_OPTION_READ_ACCESS     0x00000010        /* R_Access= option specified          */
#define EXPORT_OPTION_NETGRP          0x00000020        /* client entry is a netgroup          */
#define EXPORT_OPTION_WILDCARD        0x00000040        /* client entry is wildcarded          */
#define EXPORT_OPTION_GSSPRINC        0x00000080        /* client entry is a GSS principal     */
#define EXPORT_OPTION_PSEUDO          0x00000100        /* pseudopath is provided              */
#define EXPORT_OPTION_MAXREAD         0x00000200        /* Max read is provided                */
#define EXPORT_OPTION_MAXWRITE        0x00000400        /* Max write is provided               */
#define EXPORT_OPTION_PREFREAD        0x00000800        /* Pref read is provided               */
#define EXPORT_OPTION_PREFWRITE       0x00001000        /* Pref write is provided              */
#define EXPORT_OPTION_PREFRDDIR       0x00002000        /* Pref readdir size is provided       */
#define EXPORT_OPTION_PRIVILEGED_PORT 0x00004000        /* clients use only privileged port    */
#define EXPORT_OPTION_USE_DATACACHE   0x00008000        /* Is export entry data cached ?       */
#define EXPORT_OPTION_WRITE_ACCESS    0x00010000        /* RW_Access= option specified         */
#define EXPORT_OPTION_MD_WRITE_ACCESS 0x00020000        /* MDONLY_Access= option specified     */
#define EXPORT_OPTION_MD_READ_ACCESS  0x00040000        /* MDONLY_RO_Access= option specified  */

/* @todo BUGAZOMEU : Mettre au carre les flags des flavors */

#define EXPORT_OPTION_AUTH_NONE       0x00010000        /* Auth None authentication supported  */
#define EXPORT_OPTION_AUTH_UNIX       0x00020000        /* Auth Unix authentication supported  */

#define EXPORT_OPTION_RPCSEC_GSS_NONE 0x00040000        /* RPCSEC_GSS_NONE supported           */
#define EXPORT_OPTION_RPCSEC_GSS_INTG 0x00080000        /* RPCSEC_GSS INTEGRITY supported      */
#define EXPORT_OPTION_RPCSEC_GSS_PRIV 0x00100000        /* RPCSEC_GSS PRIVACY supported        */

/* protocol flags */
#define EXPORT_OPTION_NFSV2           0x00200000        /* NFSv2 operations are supported      */
#define EXPORT_OPTION_NFSV3           0x00400000        /* NFSv3 operations are supported      */
#define EXPORT_OPTION_NFSV4           0x00800000        /* NFSv4 operations are supported      */
#define EXPORT_OPTION_UDP             0x01000000        /* UDP protocol is supported      */
#define EXPORT_OPTION_TCP             0x02000000        /* TCP protocol is supported      */

/* Maximum offset set for R/W */
#define EXPORT_OPTION_MAXOFFSETWRITE  0x04000000        /* Maximum Offset for write is set */
#define EXPORT_OPTION_MAXOFFSETREAD   0x08000000        /* Maximum Offset for read is set  */
#define EXPORT_OPTION_MAXCACHESIZE    0x10000000        /* Maximum Offset for read is set  */
#define EXPORT_OPTION_USE_PNFS        0x20000000        /* Using pNFS or not using pNFS ?  */

/* nfs_export_check_access() return values */
#define EXPORT_PERMISSION_GRANTED            0x00000001
#define EXPORT_MDONLY_GRANTED                0x00000002
#define EXPORT_PERMISSION_DENIED             0x00000003
#define EXPORT_WRITE_ATTEMPT_WHEN_RO         0x00000004
#define EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO  0x00000005


/* NFS4 specific structures */

/*
 * PseudoFs Tree
 */
typedef struct pseudofs_entry
{
  char name[MAXNAMLEN];                         /**< The entry name          */
  char fullname[MAXPATHLEN];                    /**< The full path in the pseudo fs */
  unsigned int pseudo_id;                       /**< ID within the pseudoFS  */
  exportlist_t *junction_export;                /**< Export list related to the junction, NULL if entry is no junction*/
  struct pseudofs_entry *sons;                  /**< pointer to a linked list of sons */
  struct pseudofs_entry *parent;                /**< reverse pointer (for LOOKUPP)    */
  struct pseudofs_entry *next;                  /**< pointer to the next entry in a list of sons */
  struct pseudofs_entry *last;                  /**< pointer to the last entry in a list of sons */
} pseudofs_entry_t;

#define MAX_PSEUDO_ENTRY 100
typedef struct pseudofs
{
  pseudofs_entry_t root;
  unsigned int last_pseudo_id;
  pseudofs_entry_t *reverse_tab[MAX_PSEUDO_ENTRY];
} pseudofs_t;

#define NFS_CLIENT_NAME_LEN 256
typedef struct nfs_client_cred_gss__
{
  unsigned int svc;
  unsigned int qop;
  unsigned char cname[NFS_CLIENT_NAME_LEN];
  unsigned char stroid[NFS_CLIENT_NAME_LEN];
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

/*
 * NFS v4 Compound Data 
 */
/* this structure contains the necessary stuff for keeping the state of a V4 compound request */
typedef struct compoud_data
{
  nfs_fh4 currentFH;                                  /**< Current filehandle                                            */
  nfs_fh4 rootFH;                                     /**< Root filehandle                                               */
  nfs_fh4 savedFH;                                    /**< Saved filehandle                                              */
  nfs_fh4 publicFH;                                   /**< Public filehandle                                             */
  nfs_fh4 mounted_on_FH;                              /**< File handle to "mounted on" File System                       */
  stateid4 current_stateid;                           /**< Current stateid                                               */
  bool_t   current_stateid_valid;                     /**< Current stateid is valid                                      */
  unsigned int minorversion;                          /**< NFSv4 minor version                                           */
  cache_entry_t *current_entry;                       /**< cache entry related to current filehandle                     */
  cache_entry_t *saved_entry;                         /**< cache entry related to saved filehandle                       */
  cache_inode_file_type_t current_filetype;           /**< File type associated with the current filehandle and inode    */
  cache_inode_file_type_t saved_filetype;             /**< File type associated with the saved filehandle and inode      */
  fsal_op_context_t *pcontext;                        /**< Credentials related to this filesets                          */
                                                      /**< (to handle different uid mapping)                             */
  exportlist_t *pexport;                              /**< Export entry related to the request                           */
  exportlist_t *pfullexportlist;                      /**< Pointer to the whole exportlist                               */
  pseudofs_t *pseudofs;                               /**< Pointer to the pseudo filesystem tree                         */
  char MntPath[MAXPATHLEN];                           /**< Path (in pseudofs) of the current mounted entry               */
  struct svc_req *reqp;                               /**< Raw RPC credentials                                           */
  hash_table_t *ht;                                   /**< hashtable for cache_inode                                     */
  cache_inode_client_t *pclient;                      /**< client ressource for the request                              */
  nfs_client_cred_t credential;                       /**< RPC Request related to the compound                           */
#ifdef _USE_NFS4_1
  caddr_t pcached_res;                                /**< NFv41: pointer to cached RPC res in a session's slot          */
  bool_t use_drc;                                     /**< Set to TRUE if session DRC is to be used                      */
  uint32_t oppos;                                     /**< Position of the operation within the request processed        */
  nfs41_session_t *psession;                          /**< Related session (found by OP_SEQUENCE)                        */
#endif                          /* USE_NFS4_1 */
} compound_data_t;

/* Export list related functions */
exportlist_t *nfs_Get_export_by_id(exportlist_t * exportroot, unsigned short exportid);
int nfs_check_anon(exportlist_client_entry_t * pexport_client,
                    exportlist_t * pexport,
                    struct user_cred *user_credentials);
int nfs_build_fsal_context(struct svc_req *ptr_req,
                           exportlist_t * pexport,
                           fsal_op_context_t * pcontext,
                           struct user_cred *user_credentials);
int get_req_uid_gid(struct svc_req *ptr_req,
                    exportlist_t * pexport,
                    struct user_cred *user_credentials);


int nfs_compare_clientcred(nfs_client_cred_t * pcred1, nfs_client_cred_t * pcred2);
int nfs_rpc_req2client_cred(struct svc_req *reqp, nfs_client_cred_t * pcred);

cache_content_status_t cache_content_prepare_directories(exportlist_t * pexportlist,
                                                         char *cache_dir,
                                                         cache_content_status_t *
                                                         pstatus);

int nfs_export_check_access(sockaddr_t *hostaddr,
                            struct svc_req *ptr_req,
                            exportlist_t * pexport,
                            unsigned int nfs_prog,
                            unsigned int mnt_prog,
                            hash_table_t * ht_ip_stats,
                            struct prealloc_pool *ip_stats_pool,
                            exportlist_client_entry_t * pclient_found,
                            struct user_cred *user_credentials,
                            bool_t proc_makes_write);

int nfs_export_tag2path(exportlist_t * exportroot, char *tag, int taglen, char *path,
                        int pathlen);

#endif                          /* _NFS_EXPORTS_H */
