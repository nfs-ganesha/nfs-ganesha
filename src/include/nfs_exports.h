/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * @note  not called by other header files.
 *
 * This file contains prototypes and data structures for related to
 * export list management and the NFSv4 compound.
 */

#ifndef NFS_EXPORTS_H
#define NFS_EXPORTS_H

#include <sys/types.h>
#include <sys/param.h>

#ifdef _HAVE_GSSAPI
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#endif

#include "config_parsing.h"
#include "export_mgr.h"
#include "fsal_types.h"
#include "log.h"
#include "cidr.h"

/*
 * Export List structure
 */
#define EXPORT_KEY_SIZE 8
#define ANON_UID -2
#define ANON_GID -2

#define EXPORT_LINESIZE 1024
#define INPUT_SIZE 1024


typedef enum exportlist_client_type__ {
	PROTO_CLIENT = 0,
	NETWORK_CLIENT = 1,
	NETGROUP_CLIENT = 2,
	WILDCARDHOST_CLIENT = 3,
	GSSPRINCIPAL_CLIENT = 4,
	MATCH_ANY_CLIENT = 5,
	BAD_CLIENT = 6
} exportlist_client_type_t;

struct global_export_perms {
	struct export_perms def;
	struct export_perms conf;
};

#define GSS_DEFINE_LEN_TEMP 255

typedef struct exportlist_client_entry__ {
	struct glist_head cle_list;
	exportlist_client_type_t type;
	union {
		struct {
			CIDR *cidr;
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
	struct export_perms client_perms;	/*< Available mount options */
} exportlist_client_entry_t;

/* Constants for export options masks */
#define EXPORT_OPTION_FSID_SET 0x00000001 /* Set if Filesystem_id is set */
#define EXPORT_OPTION_USE_COOKIE_VERIFIER 0x00000002 /* Use cookie verifier */
/** Controls whether a directory's dirent cache is trusted for
    negative results. */
#define EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE 0x00000008
#define EXPORT_OPTION_MAXREAD_SET 0x00000010 /* Set if MaxRead was specified */
#define EXPORT_OPTION_MAXWRITE_SET 0x00000020 /* Set if MaxWrite was
						 specified */
#define EXPORT_OPTION_PREFREAD_SET 0x00000040 /* Set if PrefRead was
						 specified */
#define EXPORT_OPTION_PREFWRITE_SET 0x00000080 /* Set if PrefWrite was
						  specified */
#define EXPORT_OPTION_SECLABEL_SET 0x00000100 /* Set if export supports v4.2
						 security labels */

/* Constants for export permissions masks */
#define EXPORT_OPTION_ROOT 0	/*< Allow root access as root uid */
#define EXPORT_OPTION_ROOT_ID_SQUASH 0x00000001	/*< Disallow root access as
						    root uid but preserve
						    alt_groups */
#define EXPORT_OPTION_ROOT_SQUASH 0x00000002	/*< Disallow root access as root
						    uid */
#define EXPORT_OPTION_ALL_ANONYMOUS 0x00000004	/*< all users are squashed to
						    anonymous */
#define EXPORT_OPTION_SQUASH_TYPES (EXPORT_OPTION_ROOT_SQUASH | \
				    EXPORT_OPTION_ROOT_ID_SQUASH | \
				    EXPORT_OPTION_ALL_ANONYMOUS) /*< All squash
								   types */
#define EXPORT_OPTION_ANON_UID_SET 0x00000008	/*< Indicates Anon_uid was set
						 */
#define EXPORT_OPTION_ANON_GID_SET 0x00000010	/*< Indicates Anon_gid was set
						 */
#define EXPORT_OPTION_READ_ACCESS 0x00000020	/*< R_Access= option specified
						 */
#define EXPORT_OPTION_WRITE_ACCESS 0x00000040	/*< RW_Access= option specified
						 */
#define EXPORT_OPTION_RW_ACCESS       (EXPORT_OPTION_READ_ACCESS     | \
				       EXPORT_OPTION_WRITE_ACCESS)
#define EXPORT_OPTION_MD_READ_ACCESS 0x00000080	/*< MDONLY_RO_Access= option
						    specified */
#define EXPORT_OPTION_MD_WRITE_ACCESS 0x00000100 /*< MDONLY_Access= option
						     specified */
#define EXPORT_OPTION_MD_ACCESS       (EXPORT_OPTION_MD_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_READ_ACCESS)
#define EXPORT_OPTION_MODIFY_ACCESS   (EXPORT_OPTION_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_WRITE_ACCESS)
#define EXPORT_OPTION_ACCESS_MASK     (EXPORT_OPTION_READ_ACCESS     | \
				       EXPORT_OPTION_WRITE_ACCESS    | \
				       EXPORT_OPTION_MD_WRITE_ACCESS | \
				       EXPORT_OPTION_MD_READ_ACCESS)

#define EXPORT_OPTION_NO_ACCESS 0	/*< Access_Type = None */

#define EXPORT_OPTION_PRIVILEGED_PORT 0x00000200	/*< Clients use only
							   privileged port */

#define EXPORT_OPTION_COMMIT 0x00000400		/*< NFS Commit writes */
#define EXPORT_OPTION_DISABLE_ACL   0x00000800	/*< ACL is disabled */

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
#define EXPORT_OPTION_AUTH_DEFAULTS   (EXPORT_OPTION_AUTH_NONE	     | \
				       EXPORT_OPTION_AUTH_UNIX)

#define EXPORT_OPTION_EXPIRE_SET 0x00080000	/*< Inode expire was set */
#define EXPORT_DEFAULT_CACHE_EXPIRY 60	/*< Default cache expiry */

/* Protocol flags */
#define EXPORT_OPTION_NFSV3 0x00100000	/*< NFSv3 operations are supported */
#define EXPORT_OPTION_NFSV4 0x00200000	/*< NFSv4 operations are supported */
#define EXPORT_OPTION_9P 0x00400000	/*< 9P operations are supported */
#define EXPORT_OPTION_UDP 0x01000000	/*< UDP protocol is supported */
#define EXPORT_OPTION_TCP 0x02000000	/*< TCP protocol is supported */
#define EXPORT_OPTION_RDMA 0x04000000	/*< RDMA protocol is supported */
#define EXPORT_OPTION_PROTOCOLS	      (EXPORT_OPTION_NFSV3	     | \
				       EXPORT_OPTION_NFSV4	     | \
				       EXPORT_OPTION_9P)
#define EXPORT_OPTION_PROTO_DEFAULTS  (EXPORT_OPTION_NFSV3	     | \
				       EXPORT_OPTION_NFSV4)
#define EXPORT_OPTION_TRANSPORTS      (EXPORT_OPTION_UDP	     | \
				       EXPORT_OPTION_TCP	     | \
				       EXPORT_OPTION_RDMA)
#define EXPORT_OPTION_XPORT_DEFAULTS  (EXPORT_OPTION_UDP	     | \
				       EXPORT_OPTION_TCP)

#define EXPORT_OPTION_READ_DELEG 0x10000000	/*< Enable read delegations */
#define EXPORT_OPTION_WRITE_DELEG 0x20000000	/*< Using write delegations */
#define EXPORT_OPTION_DELEGATIONS (EXPORT_OPTION_READ_DELEG | \
				   EXPORT_OPTION_WRITE_DELEG)
#define EXPORT_OPTION_NO_DELEGATIONS 0

#define EXPORT_OPTION_MANAGE_GIDS 0x40000000 /*< Do not trust
						    altgrp in AUTH_SYS creds */
#define EXPORT_OPTION_NO_READDIR_PLUS 0x80000000 /*< Disallow readdir plus */

#define EXPORT_OPTION_PERM_UNUSED 0x08860000

/* Export list related functions */
uid_t get_anonymous_uid(void);
gid_t get_anonymous_gid(void);
void export_check_access(void);

bool export_check_security(struct svc_req *req);

int init_export_root(struct gsh_export *exp);

fsal_status_t nfs_export_get_root_entry(struct gsh_export *exp,
					struct fsal_obj_handle **obj);
void release_export(struct gsh_export *exp, bool config);
/* XXX */
/*void kill_export_root_entry(cache_entry_t *entry);*/
/*void kill_export_junction_entry(cache_entry_t *entry);*/

int ReadExports(config_file_t in_config,
		struct config_error_type *err_type);
int reread_exports(config_file_t in_config,
		struct config_error_type *err_type);
void free_export_resources(struct gsh_export *exp, bool config);

void exports_pkginit(void);

#endif				/* !NFS_EXPORTS_H */
