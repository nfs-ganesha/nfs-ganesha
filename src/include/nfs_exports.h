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
 * @file nfs_exports.h
 * @brief Prototypes for what's related to export list management.
 *
 * This file contains prototypes and data structures for related to
 * export list management and the NFSv4 compound.
 */

#ifndef NFS_EXPORTS_H
#define NFS_EXPORTS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "ganesha_rpc.h"
#ifdef _HAVE_GSSAPI
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#endif
#include <dirent.h> /* For having MAXNAMLEN */
#include <netdb.h> /* For having MAXHOSTNAMELEN */
#include "HashTable.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_stat.h"
#include "cache_inode.h"
#include "nfs_ip_stats.h"
#include "nlm_list.h"

/*
 * Export List structure
 */
#define EXPORT_KEY_SIZE 8
#define ANON_UID -2
#define ANON_GID -2

#define EXPORT_LINESIZE 1024
#define INPUT_SIZE 1024

typedef struct exportlist_client_hostif__ {
	unsigned int clientaddr;
	struct in6_addr clientaddr6;
} exportlist_client_hostif_t;

typedef struct exportlist_client_net__ {
	unsigned int netaddr;
	unsigned int netmask;
} exportlist_client_net_t;

typedef struct exportlist_client_netgrp__ {
	char netgroupname[MAXHOSTNAMELEN];
} exportlist_client_netgrp_t;

typedef struct exportlist_client_wildcard_host__ {
	char wildcard[MAXHOSTNAMELEN];
} exportlist_client_wildcard_host_t;

#define GSS_DEFINE_LEN_TEMP 255
typedef struct exportlist_client_gss__ {
	char princname[GSS_DEFINE_LEN_TEMP];
} exportlist_client_gss_t;

typedef enum exportlist_access_type__ {
	ACCESSTYPE_RW = 1, /*< All operations are allowed */
	ACCESSTYPE_RO = 2, /*< Filesystem is readonly (nfs_read allowed) */
	ACCESSTYPE_MDONLY = 3, /*< Data operations are forbidden */
	ACCESSTYPE_MDONLY_RO = 4  /*< Data operations are forbidden,
				      and the filesystem is
				      read-only. */
} exportlist_access_type_t;

typedef enum exportlist_client_type__ {
	HOSTIF_CLIENT = 1,
	NETWORK_CLIENT = 2,
	NETGROUP_CLIENT = 3,
	WILDCARDHOST_CLIENT = 4,
	GSSPRINCIPAL_CLIENT = 5,
	HOSTIF_CLIENT_V6 = 6,
	BAD_CLIENT = 7
} exportlist_client_type_t;

typedef enum exportlist_status__ {
	EXPORTLIST_OK = 1,
	EXPORTLIST_UNAVAILABLE = 2
} exportlist_status_t;

typedef union exportlist_client_union__ {
	exportlist_client_hostif_t hostif;
	exportlist_client_net_t network;
	exportlist_client_netgrp_t netgroup;
	exportlist_client_wildcard_host_t wildcard;
	exportlist_client_gss_t gssprinc;
} exportlist_client_union_t;

typedef struct exportlist_client_entry__ {
	exportlist_client_type_t type;
	exportlist_client_union_t client;
	unsigned int options; /*< Available mount options */
} exportlist_client_entry_t;

#define EXPORTS_NB_MAX_CLIENTS 128

typedef struct exportlist_client__ {
	unsigned int num_clients; /*< Number of clients */
	/** Allowed clients */
	exportlist_client_entry_t clientarray[EXPORTS_NB_MAX_CLIENTS];
} exportlist_client_t;

typedef struct exportlist__ {
	unsigned short id; /*< Entry identifier */
	exportlist_status_t status; /*< Entry's status */
	char dirname[MAXNAMLEN]; /*< Path relative to fs root */
	char fullpath[MAXPATHLEN]; /*< The path from the root */
	char fsname[MAXNAMLEN]; /*< File system name, MAXNAMLEN is used for
				    wanting of a better constant */
	char pseudopath[MAXPATHLEN]; /*< NFSv4 pseudo-filesystem
				      *  'virtual' path */
	char referral[MAXPATHLEN]; /*< String describing NFSv4 referral */
	char FS_specific[MAXPATHLEN]; /*< Filesystem specific option string */
	char FS_tag[MAXPATHLEN];      /*< Filesystem "tag" string */

	exportlist_access_type_t access_type; /*< Allowed operations
						  for this
						  export. Used by the
						  older Access list
						  Access_Type export
						  permissions scheme
						  as well as the newer
						  R_Access, RW_Access,
						  MDONLY_Access,
						  MDONLY_R_Access
						  lists.*/
	bool new_access_list_version; /*< The new access list version
					  (true) is the *_Access
					  lists.  The old (false) is
					  Access and Access_Type. */

	fsal_fsid_t filesystem_id; /*< Filesystem ID */
	struct fsal_obj_handle *proot_handle; /*< FSAL handle for the root of
						  the file system */

	uid_t anonymous_uid; /*< Root UID when no root access is available/
			         UID when access is available but all users
				 are being squashed. */
	gid_t anonymous_gid; /*< Root GID when no root access is available/
			         GID when access is available but all users
				 are being squashed. */
	bool all_anonymous; /*< When set to true, all users including root
			        will be given the anon uid/gid */
	unsigned int options; /*< Available mount options */
	unsigned char seckey[EXPORT_KEY_SIZE]; /*< Checksum for FH validity */
	bool use_ganesha_write_buffer;
	bool use_commit;
	uint32_t MaxRead; /*< Max Read for this entry */
	uint32_t MaxWrite; /*< Max Write for this entry */
	uint32_t PrefRead; /*< Preferred Read size */
	uint32_t PrefWrite; /*< Preferred Write size */
	uint32_t PrefReaddir; /*< Preferred Readdir size */
	uint64_t MaxOffsetWrite; /*< Maximum Offset allowed for write */
	uint64_t MaxOffsetRead; /*< Maximum Offset allowed for read */
	uint64_t MaxCacheSize;  /*< Maximum Cache Size allowed */
	bool UseCookieVerifier; /*< Is Cookie verifier to be used? */
	exportlist_client_t clients; /*< Allowed clients */
	struct exportlist__ *next; /*< Next entry */
	struct fsal_export *export_hdl; /*< Handle into our FSAL */

	pthread_mutex_t exp_state_mutex; /*< Mutex to protect per-export
					     state information. */
	struct glist_head exp_state_list; /*< List of NFS v4 state belonging
					      to this export */
	struct glist_head exp_lock_list; /*< List of locks belonging
					     to this export Only need
					     this list if NLM,
					     otherwise state list is
					     sufficient */
	nfs_worker_stat_t *worker_stats; /*< List of worker stats to support
					     per-share stat. */
} exportlist_t;

/* Constant for options masks */
#define EXPORT_OPTION_NOSUID 0x00000001 /*< Mask off setuid mode bit */
#define EXPORT_OPTION_NOSGID 0x00000002 /*< Mask off setgid mode bit */
#define EXPORT_OPTION_ROOT 0x00000004 /*< Allow root access as root uid */
#define EXPORT_OPTION_NETENT 0x00000008 /*< Client entry is a network entry */
#define EXPORT_OPTION_READ_ACCESS 0x00000010 /*< R_Access= option specified */
#define EXPORT_OPTION_NETGRP 0x00000020 /*< Client entry is a netgroup */
#define EXPORT_OPTION_WILDCARD 0x00000040 /*< Client entry is wildcarded */
#define EXPORT_OPTION_GSSPRINC 0x00000080 /*< Client entry is a GSS
                                              principal */
#define EXPORT_OPTION_PSEUDO 0x00000100 /*< Pseudopath is provided */
#define EXPORT_OPTION_MAXREAD 0x00000200 /*< Max read is provided */
#define EXPORT_OPTION_MAXWRITE 0x00000400 /*< Max write is provided */
#define EXPORT_OPTION_PREFREAD 0x00000800 /*< Pref read is provided */
#define EXPORT_OPTION_PREFWRITE 0x00001000 /*< Pref write is provided */
#define EXPORT_OPTION_PREFRDDIR 0x00002000 /*< Pref readdir size is provided */
#define EXPORT_OPTION_PRIVILEGED_PORT 0x00004000 /*< Clients use only
                                                   privileged port */
#define EXPORT_OPTION_WRITE_ACCESS 0x00010000 /*< RW_Access= option
                                                  specified */
#define EXPORT_OPTION_MD_WRITE_ACCESS 0x00020000 /*< MDONLY_Access= option
                                                     specified */
#define EXPORT_OPTION_MD_READ_ACCESS  0x00040000 /*< MDONLY_RO_Access= option
                                                     specified */

/* @todo BUGAZOMEU : Mettre au carre les flags des flavors */

#define EXPORT_OPTION_AUTH_NONE 0x00010000 /*< Auth None authentication
                                               supported  */
#define EXPORT_OPTION_AUTH_UNIX 0x00020000 /*< Auth Unix authentication
                                               supported  */

#define EXPORT_OPTION_RPCSEC_GSS_NONE 0x00040000 /*< RPCSEC_GSS_NONE
                                                     supported */
#define EXPORT_OPTION_RPCSEC_GSS_INTG 0x00080000 /*< RPCSEC_GSS INTEGRITY
                                                     supported */
#define EXPORT_OPTION_RPCSEC_GSS_PRIV 0x00100000 /*< RPCSEC_GSS PRIVACY
                                                     supported        */

/* Protocol flags */
#define EXPORT_OPTION_NFSV2 0x00200000 /*< NFSv2 operations are supported */
#define EXPORT_OPTION_NFSV3 0x00400000 /*< NFSv3 operations are supported */
#define EXPORT_OPTION_NFSV4 0x00800000 /*< NFSv4 operations are supported */
#define EXPORT_OPTION_UDP 0x01000000 /*< UDP protocol is supported */
#define EXPORT_OPTION_TCP 0x02000000 /*< TCP protocol is supported */

/* Maximum offset set for R/W */
#define EXPORT_OPTION_MAXOFFSETWRITE 0x04000000 /*< Maximum Offset for write
                                                    is set */
#define EXPORT_OPTION_MAXOFFSETREAD 0x08000000 /*< Maximum Offset for read is
                                                   set */
#define EXPORT_OPTION_MAXCACHESIZE 0x10000000 /*< Maximum Offset for read is
                                                  set */
#define EXPORT_OPTION_USE_PNFS   0x20000000 /*< Using pNFS or not using pNFS? */
#define EXPORT_OPTION_USE_UQUOTA 0x40000000 /*< Using user quota for this export */
#define EXPORT_OPTION_USE_DELEG  0x80000000 /*< Using delegations for this export */

/* nfs_export_check_access() return values */
#define EXPORT_PERMISSION_GRANTED 0x00000001
#define EXPORT_MDONLY_GRANTED 0x00000002
#define EXPORT_PERMISSION_DENIED 0x00000003
#define EXPORT_WRITE_ATTEMPT_WHEN_RO 0x00000004
#define EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO 0x00000005


/* NFS4 specific structures */

/*
 * PseudoFs Tree
 */
typedef struct pseudofs_entry {
	char name[MAXNAMLEN]; /*< The entry name */
	char fullname[MAXPATHLEN]; /*< The full path in the pseudo fs */
	unsigned int pseudo_id; /*< ID within the pseudoFS  */
	exportlist_t *junction_export; /*< Export list related to the junction,
					   NULL if entry is no junction */
	struct pseudofs_entry *sons; /*< Pointer to a linked list of sons */
	struct pseudofs_entry *parent; /*< Reverse pointer (for LOOKUPP) */
	struct pseudofs_entry *next; /*< Next entry in a list of sons */
	struct pseudofs_entry *last; /*< Last entry in a list of sons */
} pseudofs_entry_t;

#define MAX_PSEUDO_ENTRY 100
typedef struct pseudofs {
	pseudofs_entry_t root;
	unsigned int last_pseudo_id;
	pseudofs_entry_t *reverse_tab[MAX_PSEUDO_ENTRY];
} pseudofs_t;

#define NFS_CLIENT_NAME_LEN 256
typedef struct nfs_client_cred_gss__ {
	unsigned int svc;
	unsigned int qop;
	unsigned char cname[NFS_CLIENT_NAME_LEN];
	unsigned char stroid[NFS_CLIENT_NAME_LEN];
#ifdef _HAVE_GSSAPI
	gss_ctx_id_t gss_context_id;
#endif
} nfs_client_cred_gss_t;

typedef struct nfs_client_cred__ {
	unsigned int flavor;
	unsigned int length;
	union {
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
typedef struct compoud_data {
	nfs_fh4 currentFH; /*< Current filehandle */
	nfs_fh4 rootFH; /*< Root filehandle */
	nfs_fh4 savedFH; /*< Saved filehandle */
	nfs_fh4 publicFH; /*< Public filehandle */
	nfs_fh4 mounted_on_FH; /*< File handle to "mounted on" File System */
	stateid4 current_stateid; /*< Current stateid */
	bool current_stateid_valid; /*< Current stateid is valid */
	unsigned int minorversion; /*< NFSv4 minor version */
	cache_entry_t *current_entry; /*< Cache entry for current filehandle */
	cache_entry_t *saved_entry; /*< Cache entry for saved filehandle */
	struct fsal_ds_handle *current_ds; /*< current ds handle */
	struct fsal_ds_handle *saved_ds; /*< Saved DS handle */
	object_file_type_t current_filetype; /*< File type of current entry */
	object_file_type_t saved_filetype; /*< File type of saved entry */
	struct req_op_context *req_ctx; /*< the context including
					    related, mapped creds */
	exportlist_t *pexport; /*< Export entry related to the request */
	exportlist_t *pfullexportlist; /*< The whole exportlist */
	pseudofs_t *pseudofs; /*< Pointer to the pseudo filesystem tree */
	char MntPath[MAXPATHLEN]; /*< Path (in pseudofs) of the
				      current entry */
	struct svc_req *reqp; /*< RPC Request related to the compound */
	struct nfs_worker_data__ *pworker; /*< Worker thread data */
	nfs_client_cred_t credential; /*< Raw RPC credentials */
	nfs_client_id_t *preserved_clientid; /*< clientid that has lease
					         reserved, if any */
	COMPOUND4res_extended *pcached_res; /*< NFv41: pointer to
					        cached RPC res in a
					        session's slot */
	bool use_drc; /*< Set to true if session DRC is to be used */
	uint32_t oppos; /*< Position of the operation within the request
			    processed  */
	nfs41_session_t *psession; /*< Related session (found by
				       OP_SEQUENCE) */
	sequenceid4 sequence; /*< Sequence ID of the current compound
				  (if applicable) */
	slotid4 slot; /*< Slot ID of the current compound (if
			  applicable) */
} compound_data_t;

/* Export list related functions */
exportlist_t *nfs_Get_export_by_id(exportlist_t * exportroot,
				   unsigned short exportid);
int nfs_check_anon(exportlist_client_entry_t * pexport_client,
		   exportlist_t * pexport,
		   struct user_cred *user_credentials);
bool get_req_uid_gid(struct svc_req *ptr_req,
		     exportlist_t * pexport,
		     struct user_cred *user_credentials);


bool nfs_compare_clientcred(nfs_client_cred_t *cred1,
			    nfs_client_cred_t *cred2);
int nfs_rpc_req2client_cred(struct svc_req *reqp,
			    nfs_client_cred_t *pcred);

int nfs_export_check_access(sockaddr_t *hostaddr,
			    struct svc_req *ptr_req,
			    exportlist_t * pexport,
			    unsigned int nfs_prog,
			    unsigned int mnt_prog,
			    hash_table_t * ht_ip_stats,
			    pool_t *ip_stats_pool,
			    exportlist_client_entry_t * pclient_found,
			    const struct user_cred *user_credentials,
			    bool proc_makes_write);

bool nfs_export_check_security(struct svc_req *ptr_req, exportlist_t *pexport);

int nfs_export_tag2path(exportlist_t *exportroot, char *tag,
			int taglen, char *path, int pathlen);

void squash_setattr(exportlist_client_entry_t * pexport_client,
		    exportlist_t * pexport,
		    struct user_cred   * user_credentials,
		    struct attrlist * attr);
#endif/* !NFS_EXPORTS_H */
