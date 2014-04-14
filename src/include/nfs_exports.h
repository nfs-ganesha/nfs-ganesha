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
#include <dirent.h>		/* For having MAXNAMLEN */
#include <netdb.h>		/* For having MAXHOSTNAMELEN */
#include "hashtable.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_ip_stats.h"

/*
 * Export List structure
 */
#define EXPORT_KEY_SIZE 8
#define ANON_UID -2
#define ANON_GID -2

#define EXPORT_LINESIZE 1024
#define INPUT_SIZE 1024


typedef enum exportlist_client_type__ {
	RAW_CLIENT_LIST = 0,
	HOSTIF_CLIENT = 1,
	NETWORK_CLIENT = 2,
	NETGROUP_CLIENT = 3,
	WILDCARDHOST_CLIENT = 4,
	GSSPRINCIPAL_CLIENT = 5,
	HOSTIF_CLIENT_V6 = 6,
	MATCH_ANY_CLIENT = 7,
	BAD_CLIENT = 8
} exportlist_client_type_t;


typedef struct export_perms__ {
	uid_t anonymous_uid;	/* root uid when no root access is available
				 * uid when access is available but all users
				 * are being squashed. */
	gid_t anonymous_gid;	/* root gid when no root access is available
				 * gid when access is available but all users
				 * are being squashed. */
	uint32_t options;	/* avail. mnt options */
	uint32_t set;		/* Options that have been set */
} export_perms_t;

struct global_export_perms {
	struct export_perms__ def;
	struct export_perms__ conf;
};

#define GSS_DEFINE_LEN_TEMP 255

typedef struct exportlist_client_entry__ {
	struct glist_head cle_list;
	exportlist_client_type_t type;
	union {
		char *raw_client_str;
		union {
			uint32_t clientaddr; /* wrong! fix to be struct */
			struct in6_addr clientaddr6;
		} hostif;
		struct {
			unsigned int netaddr;
			unsigned int netmask;
		} network;
		struct {
			char *netgroupname;
		} netgroup;
		struct {
			char *wildcard;
		} wildcard;
		struct {
			char *princname;
		} gssprinc;
	} client;
	export_perms_t client_perms;	/*< Available mount options */
} exportlist_client_entry_t;

typedef struct exportlist {
	struct glist_head exp_list;
	uint32_t id;		/*< Entry identifier */
	char *fullpath;		/*< The path from the root */
	char *pseudopath;	/*< NFSv4 pseudo-filesystem 'virtual' path */
	char *FS_tag;		/*< Filesystem "tag" string */

	fsal_fsid_t filesystem_id;	/*< Filesystem ID */
	export_perms_t export_perms;	/*< available mount options */
	uint64_t MaxRead;	/*< Max Read for this entry */
	uint64_t MaxWrite;	/*< Max Write for this entry */
	uint64_t PrefRead;	/*< Preferred Read size */
	uint64_t PrefWrite;	/*< Preferred Write size */
	uint64_t PrefReaddir;	/*< Preferred Readdir size */
	uint64_t MaxOffsetWrite;	/*< Maximum Offset allowed for write */
	uint64_t MaxOffsetRead;	/*< Maximum Offset allowed for read */
	uint64_t MaxCacheSize;	/*< Maximum Cache Size allowed */
	bool UseCookieVerifier;	/*< Is Cookie verifier to be used? */
	struct glist_head clients;	/*< Allowed clients */
	struct fsal_export *export_hdl;	/*< Handle into our FSAL */

	pthread_mutex_t exp_state_mutex;	/*< Mutex to protect per-export
						   state information. */
	struct glist_head exp_state_list;	/*< List of NFS v4 state
						    belonging to this export */
	struct glist_head exp_lock_list;	/*< List of locks belonging
						   to this export */
	struct glist_head exp_nlm_share_list;	/*< List of NLM shares
						   belonging to this export */
	struct glist_head exp_root_list;	/*< List of exports rooted
						    on the same inode */
	uint64_t exp_mounted_on_file_id;	/*< Node id this is mounted on
						    Protected by gsh_export lock
						 */
	cache_entry_t *exp_root_cache_inode;	/*< entry for root of this
						    export
						    Protected by gsh_export lock
						 */
	cache_entry_t *exp_junction_inode;	/*< entry for the junction of
						    this export
						    Protected by gsh_export lock
						 */
	struct gsh_export *exp_parent_exp;	/*< The export this export
						    sits on.
						    Protected by gsh_export lock
						 */
	/** Expiration type for attributes.  Defaults to never,
	    settable with Attr_Expiration_Time. */
	cache_inode_expire_type_t expire_type_attr;
	/** Expiration time interval in seconds for attributes.  Settable with
	    Attr_Expiration_Time. */
	time_t expire_time_attr;
} exportlist_t;

/* Constant for options masks */
#define EXPORT_OPTION_ROOT 0x00000001	/*< Allow root access as root uid */
#define EXPORT_OPTION_ROOT_SQUASH 0	/*< Disallow root access as root uid */
#define EXPORT_OPTION_ALL_ANONYMOUS 0x00000002	/*< all users are squashed to
						    anonymous */
#define EXPORT_OPTION_SQUASH_TYPES (EXPORT_OPTION_ROOT | \
				    EXPORT_OPTION_ALL_ANONYMOUS) /*< All squash
								   types */
#define EXPORT_OPTION_ANON_UID_SET 0x00000004	/*< Indicates Anon_uid was set
						 */
#define EXPORT_OPTION_ANON_GID_SET 0x00000008	/*< Indicates Anon_gid was set
						 */
#define EXPORT_OPTION_READ_ACCESS 0x00000010	/*< R_Access= option specified
						 */
#define EXPORT_OPTION_WRITE_ACCESS 0x00000020	/*< RW_Access= option specified
						 */
#define EXPORT_OPTION_RW_ACCESS       (EXPORT_OPTION_READ_ACCESS     | \
				       EXPORT_OPTION_WRITE_ACCESS)
#define EXPORT_OPTION_MD_READ_ACCESS 0x00000040	/*< MDONLY_RO_Access= option
						    specified */
#define EXPORT_OPTION_MD_WRITE_ACCESS 0x00000080 /*< MDONLY_Access= option
						     specified */
#define EXPORT_OPTION_MD_ACCESS       (EXPORT_OPTION_MD_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_READ_ACCESS)
#define EXPORT_OPTION_MODIFY_ACCESS   (EXPORT_OPTION_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_WRITE_ACCESS)
#define EXPORT_OPTION_ACCESS_TYPE     (EXPORT_OPTION_READ_ACCESS     | \
				       EXPORT_OPTION_WRITE_ACCESS    | \
				       EXPORT_OPTION_MD_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_READ_ACCESS)

#define EXPORT_OPTION_NO_ACCESS 0	/*< Access_Type = None */

#define EXPORT_OPTION_PRIVILEGED_PORT 0x00000100	/*< Clients use only
							   privileged port */

#define EXPORT_OPTION_DISABLE_ACL   0x00000200	/*<Indicates if ACL is disabled */

/* @todo BUGAZOMEU : Mettre au carre les flags des flavors */

#define EXPORT_OPTION_AUTH_NONE 0x00001000	/*< Auth None authentication
						   supported  */
#define EXPORT_OPTION_AUTH_UNIX 0x00002000	/*< Auth Unix authentication
						   supported  */

#define EXPORT_OPTION_RPCSEC_GSS_NONE 0x00004000	/*< RPCSEC_GSS_NONE
							    supported */
#define EXPORT_OPTION_RPCSEC_GSS_INTG 0x00008000	/*< RPCSEC_GSS
							    INTEGRITY supported
							 */
#define EXPORT_OPTION_RPCSEC_GSS_PRIV 0x00010000	/*< RPCSEC_GSS PRIVACY
							    supported	    */
#define EXPORT_OPTION_AUTH_TYPES      (EXPORT_OPTION_AUTH_NONE	     | \
				       EXPORT_OPTION_AUTH_UNIX	     | \
				       EXPORT_OPTION_RPCSEC_GSS_NONE | \
				       EXPORT_OPTION_RPCSEC_GSS_INTG | \
				       EXPORT_OPTION_RPCSEC_GSS_PRIV)

/* Protocol flags */
#define EXPORT_OPTION_NFSV3 0x00100000	/*< NFSv3 operations are supported */
#define EXPORT_OPTION_NFSV4 0x00200000	/*< NFSv4 operations are supported */
#define EXPORT_OPTION_9P 0x00400000	/*< 9P operations are supported */
#define EXPORT_OPTION_UDP 0x01000000	/*< UDP protocol is supported */
#define EXPORT_OPTION_TCP 0x02000000	/*< TCP protocol is supported */
#define EXPORT_OPTION_RDMA 0x04000000	/*< RDMA protocol is supported */
#define EXPORT_OPTION_EXPIRE_SET 0x08000000	/*< Inode expire was set */
#define EXPORT_OPTION_PROTOCOLS	      (EXPORT_OPTION_NFSV3	     | \
				       EXPORT_OPTION_NFSV4	     | \
				       EXPORT_OPTION_9P)
#define EXPORT_OPTION_TRANSPORTS      (EXPORT_OPTION_UDP	     | \
				       EXPORT_OPTION_TCP	     | \
				       EXPORT_OPTION_RDMA)

#define EXPORT_OPTION_READ_DELEG 0x10000000	/*< Enable read delegations */
#define EXPORT_OPTION_WRITE_DELEG 0x20000000	/*< Using write delegations */
#define EXPORT_OPTION_DELEGATIONS (EXPORT_OPTION_READ_DELEG | \
				   EXPORT_OPTION_WRITE_DELEG)
#define EXPORT_OPTION_NO_DELEGATIONS 0

#define EXPORT_OPTION_MANAGE_GIDS 0x40000000 /*< Do not trust
						    altgrp in AUTH_SYS creds */
#define EXPORT_OPTION_FSID_SET 0x80000000 /* Set if Filesystem_id is set */

/* NFS4 specific structures */

typedef struct nfs_client_cred_gss {
	unsigned int svc;
	unsigned int qop;
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

typedef struct nfs_worker_data nfs_worker_data_t;

/**
 * @brief NFS v4 Compound Data
 *
 * This structure contains the necessary stuff for keeping the state
 * of a V4 compound request.
 */
/* Forward references to SAL types */
typedef struct nfs41_session nfs41_session_t;
typedef struct nfs_client_id_t nfs_client_id_t;
typedef struct COMPOUND4res_extended COMPOUND4res_extended;

/**
 * @brief Compound data
 *
 * This structure contains the necessary stuff for keeping the state
 * of a V4 compound request.
 */
typedef struct compound_data {
	nfs_fh4 currentFH;	/*< Current filehandle */
	nfs_fh4 savedFH;	/*< Saved filehandle */
	stateid4 current_stateid;	/*< Current stateid */
	bool current_stateid_valid;	/*< Current stateid is valid */
	stateid4 saved_stateid;	/*< Saved stateid */
	bool saved_stateid_valid;	/*< Saved stateid is valid */
	unsigned int minorversion;	/*< NFSv4 minor version */
	cache_entry_t *current_entry;	/*< Cache entry for current filehandle
					 */
	cache_entry_t *saved_entry;	/*< Cache entry for saved filehandle */
	struct fsal_ds_handle *current_ds;	/*< current ds handle */
	struct fsal_ds_handle *saved_ds;	/*< Saved DS handle */
	object_file_type_t current_filetype;	/*< File type of current entry
						 */
	object_file_type_t saved_filetype;	/*< File type of saved entry */
	struct req_op_context *req_ctx;	/*< the context including
					   related, mapped creds */
/** @todo these members need to be cleaned up to refer to the gsh_export
 * at some point.
 */
	exportlist_t *export;	/*< Export entry related to the request */
	struct gsh_export *saved_export; /*< Export entry related to the
					     savedFH */
	export_perms_t export_perms; /*< Permissions for export for currentFH */
	export_perms_t saved_export_perms; /*< Permissions for export for
					       savedFH */
	struct svc_req *req;	/*< RPC Request related to the compound */
	struct nfs_worker_data *worker;	/*< Worker thread data */
	nfs_client_cred_t credential;	/*< Raw RPC credentials */
	nfs_client_id_t *preserved_clientid;	/*< clientid that has lease
						   reserved, if any */
	COMPOUND4res_extended *cached_res;	/*< NFv41: pointer to
						   cached RPC res in a
						   session's slot */
	bool use_drc;		/*< Set to true if session DRC is to be used */
	uint32_t oppos;		/*< Position of the operation within the
				    request processed  */
	nfs41_session_t *session;	/*< Related session (found by
					   OP_SEQUENCE) */
	sequenceid4 sequence;	/*< Sequence ID of the current compound
				   (if applicable) */
	slotid4 slot;		/*< Slot ID of the current compound (if
				   applicable) */
} compound_data_t;

static inline void set_current_entry(compound_data_t *data,
				     cache_entry_t *entry,
				     bool need_ref)
{
	/* Mark current_stateid as invalid */
	data->current_stateid_valid = false;

	/* Release the reference to the old entry */
	if (data->current_entry)
		cache_inode_put(data->current_entry);

	/* Clear out the current_ds */
	if (data->current_ds) {
		data->current_ds->ops->put(data->current_ds);
		data->current_ds = NULL;
	}

	data->current_entry = entry;

	if (entry == NULL) {
		data->current_filetype = NO_FILE_TYPE;
		return;
	}

	/* Set the current file type */
	data->current_filetype = entry->type;

	/* Take reference for the entry. */
	if (data->current_entry && need_ref)
		cache_inode_lru_ref(data->current_entry, LRU_FLAG_NONE);
}

/* Export list related functions */
sockaddr_t *check_convert_ipv6_to_ipv4(sockaddr_t *ipv6, sockaddr_t *ipv4);

bool nfs_compare_clientcred(nfs_client_cred_t *cred1,
			    nfs_client_cred_t *cred2);
int nfs_rpc_req2client_cred(struct svc_req *req, nfs_client_cred_t *pcred);

void nfs_export_check_access(sockaddr_t *hostaddr, exportlist_t *export,
			     export_perms_t *export_perms);

bool nfs_export_check_security(struct svc_req *req,
			       export_perms_t *export_perms,
			       exportlist_t *export);

void LogClientListEntry(log_components_t component,
			exportlist_client_entry_t *entry);

bool init_export_root(struct gsh_export *exp);

cache_inode_status_t nfs_export_get_root_entry(struct gsh_export *exp,
					       cache_entry_t **entry);
void unexport(struct gsh_export *export);
void kill_export_root_entry(cache_entry_t *entry);

int ReadExports(config_file_t in_config);
void free_export_resources(exportlist_t *export);
void exports_pkginit(void);

#endif				/* !NFS_EXPORTS_H */
