/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file  exports.c
 * @brief Export parsing and management
 */
#include "config.h"
#include "cidr.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_ip_stats.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "common_utils.h"
#include "nodelist.h"
#include <stdlib.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "export_mgr.h"
#include "fsal_up.h"

struct global_export_perms export_opt = {
	.def.anonymous_uid = ANON_UID,
	.def.anonymous_gid = ANON_GID,
	/* Note: Access_Type defaults to None on purpose */
	.def.options = EXPORT_OPTION_ROOT_SQUASH |
		       EXPORT_OPTION_NO_ACCESS |
		       EXPORT_OPTION_AUTH_NONE |
		       EXPORT_OPTION_AUTH_UNIX |
		       EXPORT_OPTION_NFSV3 |
		       EXPORT_OPTION_NFSV4 |
		       EXPORT_OPTION_UDP |
		       EXPORT_OPTION_TCP |
		       EXPORT_OPTION_NO_DELEGATIONS,
	.def.set = UINT32_MAX
};

static void StrExportOptions(struct export_perms *p_perms, char *buffer)
{
	char *buf = buffer;

	if ((p_perms->set & EXPORT_OPTION_SQUASH_TYPES) != 0) {
		if ((p_perms->options & EXPORT_OPTION_ROOT) != 0)
			buf += sprintf(buf, "no_root_squash");

		if ((p_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)  != 0)
			buf += sprintf(buf, "all_squash    ");

		if ((p_perms->options &
		     (EXPORT_OPTION_ROOT | EXPORT_OPTION_ALL_ANONYMOUS)) == 0)
			buf += sprintf(buf, "root_squash   ");
	} else
		buf += sprintf(buf, "              ");

	if ((p_perms->set & EXPORT_OPTION_ACCESS_TYPE) != 0) {
		if ((p_perms->options & EXPORT_OPTION_READ_ACCESS) != 0)
			buf += sprintf(buf, ", R");
		else
			buf += sprintf(buf, ", -");
		if ((p_perms->options & EXPORT_OPTION_WRITE_ACCESS) != 0)
			buf += sprintf(buf, "W");
		else
			buf += sprintf(buf, "-");
		if ((p_perms->options & EXPORT_OPTION_MD_READ_ACCESS) != 0)
			buf += sprintf(buf, "r");
		else
			buf += sprintf(buf, "-");
		if ((p_perms->options & EXPORT_OPTION_MD_WRITE_ACCESS) != 0)
			buf += sprintf(buf, "w");
		else
			buf += sprintf(buf, "-");
	} else
		buf += sprintf(buf, ",     ");

	if ((p_perms->set & EXPORT_OPTION_PROTOCOLS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_NFSV3) != 0)
			buf += sprintf(buf, ", 3");
		else
			buf += sprintf(buf, ", -");
		if ((p_perms->options & EXPORT_OPTION_NFSV4) != 0)
			buf += sprintf(buf, "4");
		else
			buf += sprintf(buf, "-");
		if ((p_perms->options & EXPORT_OPTION_9P) != 0)
			buf += sprintf(buf, "9");
		else
			buf += sprintf(buf, "-");
	} else
		buf += sprintf(buf, ",    ");

	if ((p_perms->set & EXPORT_OPTION_TRANSPORTS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_UDP) != 0)
			buf += sprintf(buf, ", UDP");
		else
			buf += sprintf(buf, ", ---");
		if ((p_perms->options & EXPORT_OPTION_TCP) != 0)
			buf += sprintf(buf, ", TCP");
		else
			buf += sprintf(buf, ", ---");
		if ((p_perms->options & EXPORT_OPTION_RDMA) != 0)
			buf += sprintf(buf, ", RDMA");
		else
			buf += sprintf(buf, ", ----");
	} else
		buf += sprintf(buf, ",               ");

	if ((p_perms->set & EXPORT_OPTION_MANAGE_GIDS) == 0)
		buf += sprintf(buf, ",               ");
	else if ((p_perms->options & EXPORT_OPTION_MANAGE_GIDS) != 0)
		buf += sprintf(buf, ", Manage_Gids   ");
	else
		buf += sprintf(buf, ", No Manage_Gids");

	if ((p_perms->set & EXPORT_OPTION_DELEGATIONS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_READ_DELEG) != 0)
			buf += sprintf(buf, ", R");
		else
			buf += sprintf(buf, ", -");
		if ((p_perms->options & EXPORT_OPTION_WRITE_DELEG) != 0)
			buf += sprintf(buf, "W Deleg");
		else
			buf += sprintf(buf, "- Deleg");
	} else
		buf += sprintf(buf, ",         ");

	if ((p_perms->set & EXPORT_OPTION_ANON_UID_SET) == 0)
		buf += sprintf(buf, ", anon_uid=%6d",
			       (int)p_perms->anonymous_uid);
	else
		buf += sprintf(buf, ",                ");

	if ((p_perms->set & EXPORT_OPTION_ANON_GID_SET) == 0)
		buf += sprintf(buf, ", anon_gid=%6d",
			       (int)p_perms->anonymous_gid);
	else
		buf += sprintf(buf, ",                ");

	if ((p_perms->set & EXPORT_OPTION_AUTH_TYPES) != 0) {
		if ((p_perms->options & EXPORT_OPTION_AUTH_NONE) != 0)
			buf += sprintf(buf, ", none");
		if ((p_perms->options & EXPORT_OPTION_AUTH_UNIX) != 0)
			buf += sprintf(buf, ", sys");
		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_NONE) != 0)
			buf += sprintf(buf, ", krb5");
		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_INTG) != 0)
			buf += sprintf(buf, ", krb5i");
		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_PRIV) != 0)
			buf += sprintf(buf, ", krb5p");
	}
}

void LogClientListEntry(log_components_t component,
			exportlist_client_entry_t *entry)
{
	char perms[1024];
	char addr[INET6_ADDRSTRLEN];
	char *paddr = addr;

	StrExportOptions(&entry->client_perms, perms);

	switch (entry->type) {
	case HOSTIF_CLIENT:
		if (inet_ntop
		    (AF_INET, &(entry->client.hostif.clientaddr), addr,
		     sizeof(addr)) == NULL) {
			paddr = "Invalid Host address";
		}
		LogMidDebug(component, "  %p HOSTIF_CLIENT: %s (%s)", entry,
			    paddr, perms);
		return;

	case NETWORK_CLIENT:
		if (inet_ntop
		    (AF_INET, &(entry->client.network.netaddr), addr,
		     sizeof(addr)) == NULL) {
			paddr = "Invalid Network address";
		}
		LogMidDebug(component, "  %p NETWORK_CLIENT: %s (%s)", entry,
			    paddr, perms);
		return;

	case NETGROUP_CLIENT:
		LogMidDebug(component, "  %p NETWORK_CLIENT: %s (%s)", entry,
			    entry->client.netgroup.netgroupname, perms);
		return;

	case WILDCARDHOST_CLIENT:
		LogMidDebug(component, "  %p WILDCARDHOST_CLIENT: %s (%s)",
			    entry, entry->client.wildcard.wildcard, perms);
		return;

	case GSSPRINCIPAL_CLIENT:
		LogMidDebug(component, "  %p NETWORK_CLIENT: %s (%s)", entry,
			    entry->client.gssprinc.princname, perms);
		return;

	case HOSTIF_CLIENT_V6:
		if (inet_ntop
		    (AF_INET6, &(entry->client.hostif.clientaddr6), addr,
		     sizeof(addr)) == NULL) {
			paddr = "Invalid Host address";
		}
		LogMidDebug(component, "  %p HOSTIF_CLIENT_V6: %s (%s)", entry,
			    paddr, perms);
		return;

	case MATCH_ANY_CLIENT:
		LogMidDebug(component, "  %p MATCH_ANY_CLIENT: * (%s)", entry,
			    perms);
		return;

	case RAW_CLIENT_LIST:
		LogCrit(component, "  %p RAW_CLIENT_LIST: <unknown>(%s)", entry,
			perms);
		return;
	case BAD_CLIENT:
		LogCrit(component, "  %p BAD_CLIENT: <unknown>(%s)", entry,
			perms);
		return;
	}

	LogCrit(component, "  %p UNKNOWN_CLIENT_TYPE: %08x (%s)", entry,
		entry->type, perms);
}


/**
 * @brief Expand the client name token into one or more client entries
 *
 * @param exp        [IN] the export this gets linked to (in tail order)
 * @param client_tok [IN] the name string.  We modify it.
 * @param perms      [IN] pointer to the permissions to copy into each
 *
 * @returns 0 on success, error count on failure
 */

static int add_client(struct gsh_export *export,
		      char *client_tok,
		      struct export_perms *perms,
		      struct config_error_type *err_type)
{
	struct exportlist_client_entry__ *cli;
	int errcnt = 0;
	struct addrinfo *info;

	cli = gsh_calloc(sizeof(struct exportlist_client_entry__), 1);
	if (cli == NULL) {
		LogMajor(COMPONENT_CONFIG,
			 "Allocate of client space failed");
		goto out;
	}
#ifdef USE_NODELIST
#error "Node list expansion goes here but not yet"
#endif
	glist_init(&cli->cle_list);
	if (client_tok[0] == '*' && client_tok[1] == '\0') {
		cli->type = MATCH_ANY_CLIENT;
	} else if (client_tok[0] == '@') {
		if (strlen(client_tok) > MAXHOSTNAMELEN) {
			LogMajor(COMPONENT_CONFIG,
				 "netgroup (%s) name too long",
				 client_tok);
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		cli->client.netgroup.netgroupname = gsh_strdup(client_tok + 1);
		cli->type = NETGROUP_CLIENT;
	} else if (index(client_tok, '/') != NULL) {
		CIDR *cidr;
		uint32_t addr;

		cidr = cidr_from_str(client_tok);
		if (cidr == NULL) {
			LogMajor(COMPONENT_CONFIG,
				 "Expected a CIDR address, got (%s)",
				 client_tok);
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		memcpy(&addr, &cidr->addr[12], 4);
		cli->client.network.netaddr = ntohl(addr);
		memcpy(&addr, &cidr->mask[12], 4);
		cli->client.network.netmask = ntohl(addr);
		cidr_free(cidr);
		cli->type = NETWORK_CLIENT;
	} else if (index(client_tok, '*') != NULL ||
		   index(client_tok, '?') != NULL) {
		if (strlen(client_tok) > MAXHOSTNAMELEN) {
			LogMajor(COMPONENT_CONFIG,
				 "Wildcard client (%s) name too long",
				 client_tok);
			err_type->invalid = true;
			errcnt++;
			goto out;
		}
		cli->client.wildcard.wildcard = gsh_strdup(client_tok);
		cli->type = WILDCARDHOST_CLIENT;
	} else if (getaddrinfo(client_tok, NULL, NULL, &info) == 0) {
		struct addrinfo *ap, *ap_last = NULL;
		struct in_addr in_addr_last;
		struct in6_addr in6_addr_last;

		for (ap = info; ap != NULL; ap = ap->ai_next) {
			LogFullDebug(COMPONENT_CONFIG,
				     "flags=%d family=%d socktype=%d protocol=%d addrlen=%d name=%s",
				     ap->ai_flags,
				     ap->ai_family,
				     ap->ai_socktype,
				     ap->ai_protocol,
				     (int) ap->ai_addrlen,
				     ap->ai_canonname);
			if (cli == NULL) {
				cli = gsh_calloc(
				    sizeof(struct exportlist_client_entry__),
				    1);
				if (cli == NULL) {
					LogMajor(COMPONENT_CONFIG,
						 "Allocate of client space failed");
					break;
				}
				glist_init(&cli->cle_list);
			}
			if (ap->ai_family == AF_INET &&
			    (ap->ai_socktype == SOCK_STREAM ||
			     ap->ai_socktype == SOCK_DGRAM)) {
				struct in_addr infoaddr =
					((struct sockaddr_in *)ap->ai_addr)->
					sin_addr;
				if (ap_last != NULL &&
				    ap_last->ai_family == ap->ai_family &&
				    memcmp(&infoaddr,
					   &in_addr_last,
					   sizeof(struct in_addr)) == 0)
					continue;
				memcpy(&(cli->client.hostif.clientaddr),
				       &infoaddr, sizeof(struct in_addr));
				cli->type = HOSTIF_CLIENT;
				ap_last = ap;
				in_addr_last = infoaddr;

			} else if (ap->ai_family == AF_INET6 &&
				   (ap->ai_socktype == SOCK_STREAM ||
				    ap->ai_socktype == SOCK_DGRAM)) {
				struct in6_addr infoaddr =
				    ((struct sockaddr_in6 *)ap->ai_addr)->
				    sin6_addr;

				if (ap_last != NULL &&
				    ap_last->ai_family == ap->ai_family &&
				    memcmp(&infoaddr,
					   &in6_addr_last,
					   sizeof(struct in6_addr)) == 0)
					continue;
				/* IPv6 address */
				memcpy(&(cli->client.hostif.clientaddr6),
				       &infoaddr, sizeof(struct in6_addr));
				cli->type = HOSTIF_CLIENT_V6;
				ap_last = ap;
				in6_addr_last = infoaddr;
			} else
				continue;
			cli->client_perms = *perms;
			LogClientListEntry(COMPONENT_CONFIG, cli);
			glist_add_tail(&export->clients,
				       &cli->cle_list);
			cli = NULL; /* let go of it */
		}
		freeaddrinfo(info);
		goto out;
	} else {  /* does gsspric decode go here? */
		LogMajor(COMPONENT_CONFIG,
			 "Unknown client token (%s)",
			 client_tok);
		err_type->bogus = true;
		errcnt++;
		goto out;
	}
	cli->client_perms = *perms;
	LogClientListEntry(COMPONENT_CONFIG, cli);
	glist_add_tail(&export->clients,
		       &cli->cle_list);
	cli = NULL;
out:
	if (cli != NULL)
		gsh_free(cli);
	return errcnt;
}

/**
 * @brief Commit and FSAL sub-block init/commit helpers
 */

/**
 * @brief Init for CLIENT sub-block of an export.
 *
 * Allocate one exportlist_client structure for parameter
 * processing. The client_commit will allocate additional
 * exportlist_client__ storage for each of its enumerated
 * clients and free the initial block.  We only free that
 * resource here on errors.
 */

static void *client_init(void *link_mem, void *self_struct)
{
	struct exportlist_client_entry__ *cli;

	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL) {
		struct glist_head *cli_list;
		struct gsh_export *export;

		cli_list = self_struct;
		export = container_of(cli_list, struct gsh_export,
				      clients);
		glist_init(&export->clients);
		return self_struct;
	} else if (self_struct == NULL) {
		cli = gsh_calloc(sizeof(struct exportlist_client_entry__), 1);
		if (cli == NULL)
			return NULL;
		glist_init(&cli->cle_list);
		cli->type = RAW_CLIENT_LIST;
		return cli;
	} else { /* free resources case */
		cli = self_struct;

		assert(glist_empty(&cli->cle_list));
		if (cli->type == RAW_CLIENT_LIST &&
		    cli->client.raw_client_str != NULL)
			gsh_free(cli->client.raw_client_str);
		gsh_free(cli);
		return NULL;
	}
}

/**
 * @brief Commit this client block
 *
 * Validate "clients" token(s) and perms.  We enter with a client entry
 * allocated by proc_block.  Since we expand the clients token both
 * here and in add_client, we allocate new client entries and free
 * what was passed to us rather than try and link it in.
 *
 * @param link_mem [IN] the exportlist entry. add_client adds to its glist.
 * @param self_struct  [IN] the filled out client entry with a RAW_CLIENT_LIST
 *
 * @return 0 on success, error count for failure.
 */

static int client_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	struct exportlist_client_entry__ *cli;
	struct gsh_export *export;
	struct glist_head *cli_list;
	char *client_list, *tok, *endptr;
	int errcnt = 0;

	cli_list = link_mem;
	export = container_of(cli_list, struct gsh_export, clients);
	cli = self_struct;
	assert(cli->type == RAW_CLIENT_LIST);

	client_list = cli->client.raw_client_str;
	cli->client.raw_client_str = NULL;
	if (client_list == NULL || client_list[0] == '\0') {
		LogCrit(COMPONENT_CONFIG,
			"No clients specified");
		err_type->invalid = true;
		errcnt++;
	}
	/* take the first token for ourselves.  it may expand!
	 * loop thru the rest and use our options as theirs (copy)
	 */
	tok = client_list;
	while (errcnt == 0 && tok != NULL) {
		endptr = index(tok, ',');
		if (endptr != NULL)
			*endptr++ = '\0';
		LogMidDebug(COMPONENT_CONFIG,
			    "Adding client %s", tok);
		errcnt += add_client(export, tok, &cli->client_perms,
				     err_type);
		tok = endptr;
	}
	if (errcnt == 0)
		client_init(link_mem, self_struct);
	return errcnt;
}

/**
 * @brief Commit a FSAL sub-block
 *
 * Use the Name parameter passed in via the link_mem to lookup the
 * fsal.  If the fsal is not loaded (yet), load it and call its init.
 *
 * Create an export and pass the FSAL sub-block to it so that the
 * fsal method can process the rest of the parameters in the block
 */

static int fsal_commit(void *node, void *link_mem, void *self_struct,
		       struct config_error_type *err_type)
{
	struct fsal_export **exp_hdl = link_mem;
	struct gsh_export *export =
	    container_of(exp_hdl, struct gsh_export, fsal_export);
	struct fsal_args *fp = self_struct;
	struct fsal_module *fsal;
	struct root_op_context root_op_context;
	uint64_t MaxRead, MaxWrite;
	fsal_status_t status;
	int errcnt;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, export, NULL, 0, 0,
			     UNKNOWN_REQUEST);

	errcnt = fsal_load_init(node, fp->name, &fsal, err_type);
	if (errcnt > 0)
		goto err;

	/* Some admins stuff a '/' at  the end for some reason.
	 * chomp it so we have a /dir/path/basename to work
	 * with. But only if it's a non-root path starting
	 * with /.
	 */
	if (export->fullpath[0] == '/') {
		int pathlen;
		pathlen = strlen(export->fullpath);
		while ((export->fullpath[pathlen - 1] == '/') &&
		       (pathlen > 1))
			pathlen--;
		export->fullpath[pathlen] = '\0';
	}
	status = fsal->m_ops.create_export(fsal,
					  node,
					  &fsal_up_top);
	if ((export->options_set & EXPORT_OPTION_EXPIRE_SET) == 0)
		export->expire_time_attr = cache_param.expire_time_attr;

	if (FSAL_IS_ERROR(status)) {
		fsal_put(fsal);
		LogCrit(COMPONENT_CONFIG,
			"Could not create export for (%s) to (%s)",
			export->pseudopath,
			export->fullpath);
		err_type->export = true;
		errcnt++;
		goto err;
	}

	assert(root_op_context.req_ctx.fsal_export != NULL);
	export->fsal_export = root_op_context.req_ctx.fsal_export;
	root_op_context.req_ctx.fsal_export->id_exports = export->export_id;

	/* We are connected up to the fsal side.  Now
	 * validate maxread/write etc with fsal params
	 */
	MaxRead = export->fsal_export->
		exp_ops.fs_maxread(export->fsal_export);
	MaxWrite = export->fsal_export->
		exp_ops.fs_maxwrite(export->fsal_export);

	if (export->MaxRead > MaxRead && MaxRead != 0) {
		LogInfo(COMPONENT_CONFIG,
			 "Readjusting MaxRead to FSAL, %" PRIu64 " -> %" PRIu64,
			 export->MaxRead,
			 MaxRead);
		export->MaxRead = MaxRead;
	}
	if (export->MaxWrite > MaxWrite && MaxWrite != 0) {
		LogInfo(COMPONENT_CONFIG,
			 "Readjusting MaxWrite to FSAL, %"PRIu64" -> %"PRIu64,
			 export->MaxWrite,
			 MaxWrite);
		export->MaxWrite = MaxWrite;
	}

err:
	release_root_op_context();
	return errcnt;
}

/**
 * @brief EXPORT block handlers
 */

/**
 * @brief Initialize an export block
 *
 * There is no link_mem init required because we are allocating
 * here and doing an insert_gsh_export at the end of export_commit
 * to attach it to the export manager.
 *
 * Use free_exportlist here because in this case, we have not
 * gotten far enough to hand it over to the export manager.
 */

static void *export_init(void *link_mem, void *self_struct)
{
	struct gsh_export *export;

	if (self_struct == NULL) {
		export = alloc_export();
		if (export == NULL)
			return NULL;
		return export;
	} else { /* free resources case */
		export = self_struct;
		free_export(export);
		return NULL;
	}
}

/**
 * @brief Commit an export block
 *
 * Validate the export level parameters.  fsal and client
 * parameters are already done.
 */

static int export_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	struct gsh_export *export, *probe_exp;
	int errcnt = 0;
	char perms[1024];

	export = self_struct;

	/* validate the export now */
	if (export->export_perms.options & EXPORT_OPTION_NFSV4) {
		if (export->pseudopath == NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Exporting to NFSv4 but not Pseudo path defined");
			err_type->invalid = true;
			errcnt++;
			goto err_out;
		} else if (export->export_id == 0 &&
			   strcmp(export->pseudopath, "/") != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Export id 0 can only export \"/\" not (%s)",
				export->pseudopath);
			err_type->invalid = true;
			errcnt++;
			goto err_out;
		}
	}
	if (export->pseudopath != NULL &&
	    export->pseudopath[0] != '/') {
		LogCrit(COMPONENT_CONFIG,
			"A Pseudo path must be an absolute path");
		err_type->invalid = true;
		errcnt++;
	}
	if (export->export_id == 0) {
		if (export->pseudopath == NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path must be \"/\" for export id 0");
			err_type->invalid = true;
			errcnt++;
		} else if (export->pseudopath[1] != '\0') {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path must be \"/\" for export id 0");
			err_type->invalid = true;
			errcnt++;
		}
		if ((export->export_perms.options &
		     EXPORT_OPTION_PROTOCOLS) != EXPORT_OPTION_NFSV4) {
			LogCrit(COMPONENT_CONFIG,
				"Export id 0 must indicate Protocols=4");
			err_type->invalid = true;
			errcnt++;
		}
	}
	if (errcnt)
		goto err_out;  /* have basic errors. don't even try more... */
	probe_exp = get_gsh_export(export->export_id);
	if (probe_exp != NULL) {
		LogDebug(COMPONENT_CONFIG,
			 "Export %d already exists", export->export_id);
		put_gsh_export(probe_exp);
		err_type->exists = true;
		errcnt++;
	}
	if (export->FS_tag != NULL) {
		probe_exp = get_gsh_export_by_tag(export->FS_tag);
		if (probe_exp != NULL) {
			put_gsh_export(probe_exp);
			LogCrit(COMPONENT_CONFIG,
				"Tag (%s) is a duplicate",
				export->FS_tag);
			if (!err_type->exists)
				err_type->invalid = true;
			errcnt++;
		}
	}
	if (export->pseudopath != NULL) {
		probe_exp = get_gsh_export_by_pseudo(export->pseudopath, true);
		if (probe_exp != NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path (%s) is a duplicate",
				export->pseudopath);
			if (!err_type->exists)
				err_type->invalid = true;
			errcnt++;
			put_gsh_export(probe_exp);
		}
	}
	probe_exp = get_gsh_export_by_path(export->fullpath, true);
	if (probe_exp != NULL &&
	    export->pseudopath == NULL &&
	    export->FS_tag == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Duplicate path (%s) without unique tag or Pseudo path",
			export->fullpath);
		err_type->invalid = true;
		errcnt++;
		put_gsh_export(probe_exp);
	}
	if (errcnt) {
		if (err_type->exists && !err_type->invalid)
			LogDebug(COMPONENT_CONFIG,
				 "Duplicate export id = %d",
				 export->export_id);
		else
			LogCrit(COMPONENT_CONFIG,
				 "Duplicate export id = %d",
				 export->export_id);
		goto err_out;  /* have errors. don't init or load a fsal */
	}
	glist_init(&export->exp_state_list);
	glist_init(&export->exp_lock_list);
	glist_init(&export->exp_nlm_share_list);
	glist_init(&export->mounted_exports_list);

	/* now probe the fsal and init it */
	/* pass along the block that is/was the FS_Specific */
	if (!insert_gsh_export(export)) {
		LogCrit(COMPONENT_CONFIG,
			"Export id %d already in use.",
			export->export_id);
		err_type->exists = true;
		errcnt++;
		goto err_out;
	}

	/* This export must be mounted to the PseudoFS if NFS v4 */
	if (export->export_perms.options & EXPORT_OPTION_NFSV4)
		export_add_to_mount_work(export);

	StrExportOptions(&export->export_perms, perms);

	LogEvent(COMPONENT_CONFIG,
		 "Export %d created at pseudo (%s) with path (%s) and tag (%s) perms (%s)",
		 export->export_id, export->pseudopath,
		 export->fullpath, export->FS_tag, perms);
	set_gsh_export_state(export, EXPORT_READY);
	put_gsh_export(export);
	return 0;

err_out:
	return errcnt;
}

/**
 * @brief Display an export block
 *
 * Validate the export level parameters.  fsal and client
 * parameters are already done.
 */

static void export_display(const char *step, void *node,
			   void *link_mem, void *self_struct)
{
	struct gsh_export *export = self_struct;
	char perms[1024];

	StrExportOptions(&export->export_perms, perms);

	LogMidDebug(COMPONENT_CONFIG,
		    "%s %p Export %d pseudo (%s) with path (%s) and tag (%s) perms (%s)",
		    step, export, export->export_id, export->pseudopath,
		    export->fullpath, export->FS_tag, perms);
}

/**
 * @brief Commit an add export
 * commit the export
 * init export root and mount it in pseudo fs
 */

static int add_export_commit(void *node, void *link_mem, void *self_struct,
			     struct config_error_type *err_type)
{
	struct gsh_export *export = self_struct;
	int errcnt = 0;
	int status;

	errcnt = export_commit(node, link_mem, self_struct, err_type);
	if (errcnt != 0)
		goto err_out;

	status = init_export_root(export);
	if (status) {
		export_revert(export);
		errcnt++;
		if (status == EINVAL)
			err_type->invalid = true;
		else if (status == EFAULT)
			err_type->internal = true;
		else
			err_type->resource = true;
		goto err_out;
	}

	if (!mount_gsh_export(export)) {
		export_revert(export);
		err_type->internal = true;
		errcnt++;
	}

err_out:
	return errcnt;
}

/**
 * @brief Initialize an EXPORT_DEFAULTS block
 *
 */

static void *export_defaults_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL)
		return &export_opt;
	else
		return NULL;
}

/**
 * @brief Commit an EXPORT_DEFAULTS block
 *
 * Validate the export level parameters.  fsal and client
 * parameters are already done.
 */

static int export_defaults_commit(void *node, void *link_mem,
				  void *self_struct,
				  struct config_error_type *err_type)
{
	return 0;
}

/**
 * @brief Display an EXPORT_DEFAULTS block
 *
 * Validate the export level parameters.  fsal and client
 * parameters are already done.
 */

static void export_defaults_display(const char *step, void *node,
			   void *link_mem, void *self_struct)
{
	struct export_perms *defaults = self_struct;
	char perms[1024];

	StrExportOptions(defaults, perms);

	LogEvent(COMPONENT_CONFIG,
		 "%s Export Defaults (%s)",
		 step, perms);
}

/**
 * @brief Configuration processing tables for EXPORT blocks
 */

/**
 * @brief Access types list for the Access_type parameter
 */

static struct config_item_list access_types[] = {
	CONFIG_LIST_TOK("NONE", 0),
	CONFIG_LIST_TOK("RW", (EXPORT_OPTION_RW_ACCESS |
			       EXPORT_OPTION_MD_ACCESS)),
	CONFIG_LIST_TOK("RO", (EXPORT_OPTION_READ_ACCESS |
			       EXPORT_OPTION_MD_READ_ACCESS)),
	CONFIG_LIST_TOK("MDONLY", EXPORT_OPTION_MD_ACCESS),
	CONFIG_LIST_TOK("MDONLY_RO", EXPORT_OPTION_MD_READ_ACCESS),
	CONFIG_LIST_EOL
};

/**
 * @brief Protocols options list for NFS_Protocols parameter
 */

static struct config_item_list nfs_protocols[] = {
	CONFIG_LIST_TOK("3", EXPORT_OPTION_NFSV3),
	CONFIG_LIST_TOK("4", EXPORT_OPTION_NFSV4),
	CONFIG_LIST_TOK("NFS3", EXPORT_OPTION_NFSV3),
	CONFIG_LIST_TOK("NFS4", EXPORT_OPTION_NFSV4),
	CONFIG_LIST_TOK("V3", EXPORT_OPTION_NFSV3),
	CONFIG_LIST_TOK("V4", EXPORT_OPTION_NFSV4),
	CONFIG_LIST_TOK("NFSV3", EXPORT_OPTION_NFSV3),
	CONFIG_LIST_TOK("NFSV4", EXPORT_OPTION_NFSV4),
	CONFIG_LIST_TOK("9P", EXPORT_OPTION_9P),
	CONFIG_LIST_EOL
};

/**
 * @brief Transport type options list for Transport_Protocols parameter
 */

static struct config_item_list transports[] = {
	CONFIG_LIST_TOK("UDP", EXPORT_OPTION_UDP),
	CONFIG_LIST_TOK("TCP", EXPORT_OPTION_TCP),
	CONFIG_LIST_EOL
};

/**
 * @brief Security options list for SecType parameter
 */

static struct config_item_list sec_types[] = {
	CONFIG_LIST_TOK("none", EXPORT_OPTION_AUTH_NONE),
	CONFIG_LIST_TOK("sys", EXPORT_OPTION_AUTH_UNIX),
	CONFIG_LIST_TOK("krb5", EXPORT_OPTION_RPCSEC_GSS_NONE),
	CONFIG_LIST_TOK("krb5i", EXPORT_OPTION_RPCSEC_GSS_INTG),
	CONFIG_LIST_TOK("krb5p", EXPORT_OPTION_RPCSEC_GSS_PRIV),
	CONFIG_LIST_EOL
};

/**
 * @brief Client UID squash item list for Squash parameter
 */

static struct config_item_list squash_types[] = {
	CONFIG_LIST_TOK("Root", EXPORT_OPTION_ROOT_SQUASH),
	CONFIG_LIST_TOK("Root_Squash", EXPORT_OPTION_ROOT_SQUASH),
	CONFIG_LIST_TOK("RootSquash", EXPORT_OPTION_ROOT_SQUASH),
	CONFIG_LIST_TOK("All", EXPORT_OPTION_ALL_ANONYMOUS),
	CONFIG_LIST_TOK("All_Squash", EXPORT_OPTION_ALL_ANONYMOUS),
	CONFIG_LIST_TOK("AllSquash", EXPORT_OPTION_ALL_ANONYMOUS),
	CONFIG_LIST_TOK("All_Anonymous", EXPORT_OPTION_ALL_ANONYMOUS),
	CONFIG_LIST_TOK("AllAnonymous", EXPORT_OPTION_ALL_ANONYMOUS),
	CONFIG_LIST_TOK("No_Root_Squash", EXPORT_OPTION_ROOT),
	CONFIG_LIST_TOK("None", EXPORT_OPTION_ROOT),
	CONFIG_LIST_TOK("NoIdSquash", EXPORT_OPTION_ROOT),
	CONFIG_LIST_EOL
};

/**
 * @brief Delegations types list for the Delegations parameter
 */

static struct config_item_list delegations[] = {
	CONFIG_LIST_TOK("NONE", EXPORT_OPTION_NO_DELEGATIONS),
	CONFIG_LIST_TOK("Read", EXPORT_OPTION_READ_DELEG),
	CONFIG_LIST_TOK("Write", EXPORT_OPTION_WRITE_DELEG),
	CONFIG_LIST_TOK("Readwrite", EXPORT_OPTION_DELEGATIONS),
	CONFIG_LIST_TOK("R", EXPORT_OPTION_READ_DELEG),
	CONFIG_LIST_TOK("W", EXPORT_OPTION_WRITE_DELEG),
	CONFIG_LIST_TOK("RW", EXPORT_OPTION_DELEGATIONS),
	CONFIG_LIST_EOL
};

struct config_item_list deleg_types[] =  {
	CONFIG_LIST_TOK("NONE", FSAL_OPTION_NO_DELEGATIONS),
	CONFIG_LIST_TOK("Read", FSAL_OPTION_FILE_READ_DELEG),
	CONFIG_LIST_TOK("Write", FSAL_OPTION_FILE_WRITE_DELEG),
	CONFIG_LIST_TOK("Readwrite", FSAL_OPTION_FILE_DELEGATIONS),
	CONFIG_LIST_TOK("R", FSAL_OPTION_FILE_READ_DELEG),
	CONFIG_LIST_TOK("W", FSAL_OPTION_FILE_WRITE_DELEG),
	CONFIG_LIST_TOK("RW", FSAL_OPTION_FILE_DELEGATIONS),
	CONFIG_LIST_EOL
};

#define CONF_EXPORT_PERMS(_struct_, _perms_)				\
	/* Note: Access_Type defaults to None on purpose */		\
	CONF_ITEM_ENUM_BITS_SET("Access_Type",				\
		EXPORT_OPTION_NO_ACCESS,				\
		EXPORT_OPTION_ACCESS_TYPE,				\
		access_types, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_LIST_BITS_SET("Protocols",				\
		EXPORT_OPTION_PROTOCOLS, EXPORT_OPTION_PROTOCOLS,	\
		nfs_protocols, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_LIST_BITS_SET("Transports",				\
		EXPORT_OPTION_TRANSPORTS, EXPORT_OPTION_TRANSPORTS,	\
		transports, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_ANONID("Anonymous_uid",				\
		ANON_UID, _struct_, _perms_.anonymous_uid,		\
		EXPORT_OPTION_ANON_UID_SET, _perms_.set),		\
	CONF_ITEM_ANONID("Anonymous_gid",				\
		ANON_GID, _struct_, _perms_.anonymous_gid,		\
		EXPORT_OPTION_ANON_GID_SET, _perms_.set),		\
	CONF_ITEM_LIST_BITS_SET("SecType",				\
		EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX,	\
		EXPORT_OPTION_AUTH_TYPES,				\
		sec_types, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_BOOLBIT_SET("PrivilegedPort",				\
		false, EXPORT_OPTION_PRIVILEGED_PORT,			\
		_struct_, _perms_.options, _perms_.set),		\
	CONF_ITEM_BOOLBIT_SET("Manage_Gids",				\
		false, EXPORT_OPTION_MANAGE_GIDS,			\
		_struct_, _perms_.options, _perms_.set),		\
	CONF_ITEM_LIST_BITS_SET("Squash",				\
		EXPORT_OPTION_ROOT_SQUASH, EXPORT_OPTION_SQUASH_TYPES,	\
		squash_types, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_BOOLBIT_SET("NFS_Commit",				\
		false, EXPORT_OPTION_COMMIT,				\
		_struct_, _perms_.options, _perms_.set),		\
	CONF_ITEM_ENUM_BITS_SET("Delegations",				\
		EXPORT_OPTION_NO_DELEGATIONS, EXPORT_OPTION_DELEGATIONS,\
		delegations, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_BOOLBIT_SET("Disable_ACL",				\
		false, EXPORT_OPTION_DISABLE_ACL,			\
		_struct_, _perms_.options, _perms_.set)

/**
 * @brief Table of client sub-block parameters
 *
 * NOTE: node discovery is ordered by this table!
 * "Access" is last because we must have all other params processed
 * before we walk the list of accessing clients!
 */

static struct config_item client_params[] = {
	CONF_EXPORT_PERMS(exportlist_client_entry__, client_perms),
	CONF_ITEM_STR("Clients", 1, MAXPATHLEN, NULL,
		      exportlist_client_entry__, client.raw_client_str),
	CONFIG_EOL
};

/**
 * @brief Table of DEXPORT_DEFAULTS block parameters
 *
 * NOTE: node discovery is ordered by this table!
 */

static struct config_item export_defaults_params[] = {
	CONF_EXPORT_PERMS(global_export_perms, conf),
	CONFIG_EOL
};

/**
 * @brief Table of FSAL sub-block parameters
 *
 * NOTE: this points to a struct that is private to
 * fsal_commit.
 */

static struct config_item fsal_params[] = {
	CONF_ITEM_STR("Name", 1, 10, NULL,
		      fsal_args, name), /* cheater union */
	CONFIG_EOL
};

/**
 * @brief Table of EXPORT block parameters
 *
 * NOTE: the Client and FSAL sub-blocks must be the *last*
 * two entries in the list.  This is so all other
 * parameters have been processed before these sub-blocks
 * are processed.
 */

static struct config_item export_params[] = {
	CONF_MAND_UI16("Export_id", 0, UINT16_MAX, 1,
		       gsh_export, export_id),
	CONF_MAND_PATH("Path", 1, MAXPATHLEN, NULL,
		       gsh_export, fullpath), /* must chomp '/' */
	CONF_UNIQ_PATH("Pseudo", 1, MAXPATHLEN, NULL,
		       gsh_export, pseudopath),
	CONF_ITEM_UI64("MaxRead", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       gsh_export, MaxRead),
	CONF_ITEM_UI64("MaxWrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       gsh_export, MaxWrite),
	CONF_ITEM_UI64("PrefRead", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       gsh_export, PrefRead),
	CONF_ITEM_UI64("PrefWrite", 512, FSAL_MAXIOSIZE, FSAL_MAXIOSIZE,
		       gsh_export, PrefWrite),
	CONF_ITEM_UI64("PrefReaddir", 512, FSAL_MAXIOSIZE, 16384,
		       gsh_export, PrefReaddir),
	CONF_ITEM_FSID_SET("Filesystem_id", 666, 666,
		       gsh_export, filesystem_id, /* major.minor */
		       EXPORT_OPTION_FSID_SET, options_set),
	CONF_ITEM_STR("Tag", 1, MAXPATHLEN, NULL,
		      gsh_export, FS_tag),
	CONF_ITEM_UI64("MaxOffsetWrite", 512, UINT64_MAX, UINT64_MAX,
		       gsh_export, MaxOffsetWrite),
	CONF_ITEM_UI64("MaxOffsetRead", 512, UINT64_MAX, UINT64_MAX,
		       gsh_export, MaxOffsetRead),
	CONF_ITEM_BOOLBIT_SET("UseCookieVerifier",
		true, EXPORT_OPTION_USE_COOKIE_VERIFIER,
		gsh_export, options, options_set),
	CONF_ITEM_BOOLBIT_SET("DisableReaddirPlus",
		false, EXPORT_OPTION_NO_READDIR_PLUS,
		gsh_export, options, options_set),
	CONF_ITEM_BOOLBIT_SET("Trust_Readdir_Negative_Cache",
		false, EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE,
		gsh_export, options, options_set),
	CONF_EXPORT_PERMS(gsh_export, export_perms),
	CONF_ITEM_BLOCK("Client", client_params,
			client_init, client_commit,
			gsh_export, clients),
	CONF_ITEM_I32_SET("Attr_Expiration_Time", -1, INT32_MAX, 60,
		       gsh_export, expire_time_attr,
		       EXPORT_OPTION_EXPIRE_SET,  options_set),
	CONF_RELAX_BLOCK("FSAL", fsal_params,
			 fsal_init, fsal_commit,
			 gsh_export, fsal_export),
	CONFIG_EOL
};

/**
 * @brief Top level definition for an EXPORT block
 */

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.%d",
	.blk_desc.name = "EXPORT",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = export_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = export_commit,
	.blk_desc.u.blk.display = export_display
};

/**
 * @brief Top level definition for an ADD EXPORT block
 */

struct config_block add_export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.%d",
	.blk_desc.name = "EXPORT",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = export_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = add_export_commit,
	.blk_desc.u.blk.display = export_display
};


/**
 * @brief Top level definition for an EXPORT_DEFAULTS block
 */

struct config_block export_defaults_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.defaults",
	.blk_desc.name = "EXPORT_DEFAULTS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = export_defaults_init,
	.blk_desc.u.blk.params = export_defaults_params,
	.blk_desc.u.blk.commit = export_defaults_commit,
	.blk_desc.u.blk.display = export_defaults_display
};

/**
 * @brief builds an export entry for '/' with default parameters
 *
 * If export_id = 0 has not been specified, and not other export
 * for Pseudo "/" has been specified, build an FSAL_PSEUDO export
 * for the root of the Pseudo FS.
 *
 * @return -1 on error, 0 if we already have one, 1 if created one
 */

static int build_default_root(void)
{
	struct gsh_export *export;
	struct fsal_module *fsal_hdl = NULL;
	struct root_op_context root_op_context;

	/* See if export_id = 0 has already been specified */
	export = get_gsh_export(0);

	if (export != NULL) {
		/* export_id = 0 has already been specified */
		LogDebug(COMPONENT_CONFIG,
			 "Export 0 already exists");
		put_gsh_export(export);
		return 0;
	}

	/* See if another export with Pseudo = "/" has already been specified.
	 */
	export = get_gsh_export_by_pseudo("/", true);

	if (export != NULL) {
		/* Pseudo = / has already been specified */
		LogDebug(COMPONENT_CONFIG,
			 "Pseudo root already exists");
		put_gsh_export(export);
		return 0;
	}

	/* allocate and initialize the exportlist part with the id */
	LogDebug(COMPONENT_CONFIG,
		 "Allocating Pseudo root export");
	export = alloc_export();

	if (export == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Could not allocate space for pseudoroot export");
		return -1;
	}

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, export, NULL, 0, 0,
			     UNKNOWN_REQUEST);

	export->filesystem_id.major = 152;
	export->filesystem_id.minor = 152;
	export->MaxWrite = FSAL_MAXIOSIZE;
	export->MaxRead = FSAL_MAXIOSIZE;
	export->PrefWrite = FSAL_MAXIOSIZE;
	export->PrefRead = FSAL_MAXIOSIZE;
	export->PrefReaddir = 16384;
	glist_init(&export->exp_state_list);
	glist_init(&export->exp_lock_list);
	glist_init(&export->exp_nlm_share_list);
	glist_init(&export->mounted_exports_list);
	glist_init(&export->clients);

	/* Default anonymous uid and gid */
	export->export_perms.anonymous_uid = (uid_t) ANON_UID;
	export->export_perms.anonymous_gid = (gid_t) ANON_GID;

	/* Support only NFS v4 and TCP.
	 * Root is allowed
	 * MD Read Access
	 * Allow use of default auth types
	 */
	export->export_perms.options = EXPORT_OPTION_ROOT |
					EXPORT_OPTION_MD_READ_ACCESS |
					EXPORT_OPTION_NFSV4 |
					EXPORT_OPTION_AUTH_TYPES |
					EXPORT_OPTION_TCP;

	export->export_perms.set = EXPORT_OPTION_SQUASH_TYPES |
				    EXPORT_OPTION_ACCESS_TYPE |
				    EXPORT_OPTION_PROTOCOLS |
				    EXPORT_OPTION_TRANSPORTS |
				    EXPORT_OPTION_AUTH_TYPES;

	export->options = EXPORT_OPTION_USE_COOKIE_VERIFIER;
	export->options_set = EXPORT_OPTION_FSID_SET |
			      EXPORT_OPTION_USE_COOKIE_VERIFIER;

	/* Set the fullpath to "/" */
	export->fullpath = gsh_strdup("/");

	/* Set Pseudo Path to "/" */
	export->pseudopath = gsh_strdup("/");

	/* Assign FSAL_PSEUDO */
	fsal_hdl = lookup_fsal("PSEUDO");

	if (fsal_hdl == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"FSAL PSEUDO is not loaded!");
		goto err_out;
	} else {
		fsal_status_t rc;

		rc = fsal_hdl->m_ops.create_export(fsal_hdl,
						  NULL,
						  &fsal_up_top);

		if (FSAL_IS_ERROR(rc)) {
			fsal_put(fsal_hdl);
			LogCrit(COMPONENT_CONFIG,
				"Could not create FSAL export for %s",
				export->fullpath);
			goto err_out;
		}

	}

	assert(root_op_context.req_ctx.fsal_export != NULL);
	export->fsal_export = root_op_context.req_ctx.fsal_export;
	root_op_context.req_ctx.fsal_export->id_exports = 0;

	if (!insert_gsh_export(export)) {
		export->fsal_export->exp_ops.release(export->fsal_export);
		fsal_put(fsal_hdl);
		LogCrit(COMPONENT_CONFIG,
			"Failed to insert pseudo root   In use??");
		goto err_out;
	}
	set_gsh_export_state(export, EXPORT_READY);

	/* This export must be mounted to the PseudoFS */
	export_add_to_mount_work(export);

	LogEvent(COMPONENT_CONFIG,
		 "Export 0 (/) successfully created");

	put_gsh_export(export);	/* all done, let go */
	release_root_op_context();
	return 1;

err_out:
	free_export(export);
	release_root_op_context();
	return -1;
}

/**
 * @brief Read the export entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the export list
 *
 * @return A negative value on error,
 *         the number of export entries else.
 */

int ReadExports(config_file_t in_config)
{
	struct config_error_type err_type;
	int rc, ret = 0;

	rc = load_config_from_parse(in_config,
				    &export_defaults_param,
				    NULL,
				    false,
				    &err_type);
	if (!config_error_is_harmless(&err_type))
		return -1;

	rc = load_config_from_parse(in_config,
				    &export_param,
				    NULL,
				    false,
				    &err_type);
	if (!config_error_is_harmless(&err_type))
		return -1;
	ret = build_default_root();
	if (ret < 0) {
		LogCrit(COMPONENT_CONFIG,
			"No pseudo root!");
		return -1;
	}
	return rc + ret;
}

static void FreeClientList(struct glist_head *clients)
{
	struct glist_head *glist;
	struct glist_head *glistn;

	glist_for_each_safe(glist, glistn, clients) {
		exportlist_client_entry_t *client;
		client =
		    glist_entry(glist, exportlist_client_entry_t, cle_list);
		glist_del(&client->cle_list);
		if (client->type == NETGROUP_CLIENT &&
		    client->client.netgroup.netgroupname != NULL)
			gsh_free(client->client.netgroup.netgroupname);
		if (client->type == WILDCARDHOST_CLIENT &&
		    client->client.wildcard.wildcard != NULL)
			gsh_free(client->client.wildcard.wildcard);
		if (client->type == GSSPRINCIPAL_CLIENT &&
		    client->client.gssprinc.princname != NULL)
			gsh_free(client->client.gssprinc.princname);
		gsh_free(client);
	}
}

/**
 * @brief Free resources attached to an export
 *
 * @param export [IN] pointer to export
 *
 * @return true if all went well
 */

void free_export_resources(struct gsh_export *export)
{
	FreeClientList(&export->clients);
	if (export->fsal_export != NULL) {
		struct fsal_module *fsal = export->fsal_export->fsal;
		export->fsal_export->exp_ops.release(export->fsal_export);
		fsal_put(fsal);
	}
	export->fsal_export = NULL;
	/* free strings here */
	if (export->fullpath != NULL)
		gsh_free(export->fullpath);
	if (export->pseudopath != NULL)
		gsh_free(export->pseudopath);
	if (export->FS_tag != NULL)
		gsh_free(export->FS_tag);
}

/**
 * @brief pkginit callback to initialize exports from nfs_init
 *
 * Assumes being called with the export_by_id.lock held.
 * true on success
 */

static bool init_export_cb(struct gsh_export *exp, void *state)
{
	return !(init_export_root(exp));
}

/**
 * @brief Initialize exports over a live cache inode and fsal layer
 */

void exports_pkginit(void)
{
	foreach_gsh_export(init_export_cb, NULL);
}

/**
 * @brief Function to be called from cache_inode_get_protected to get an
 * export's root entry.
 *
 * @param entry  [IN/OUT] call by ref pointer to store cache entry
 * @param source [IN] void pointer to the export
 *
 * @return cache inode status code
 * @retval CACHE_INODE_ESTALE indicates this export no longer has a root
 * entry
 */

cache_inode_status_t export_get_root_entry(cache_entry_t **entry, void *source)
{
	struct gsh_export *export = source;

	*entry = export->exp_root_cache_inode;

	if (unlikely((*entry) == NULL))
		return CACHE_INODE_ESTALE;
	else
		return CACHE_INODE_SUCCESS;
}

/**
 * @brief Return a reference to the root cache inode entry of the export
 *
 * Must be called with the caller holding a reference to the export.
 *
 * Returns with an additional reference to the cache inode held for use by the
 * caller.
 *
 * @param export [IN] the aforementioned export
 * @param entry  [IN/OUT] call by ref pointer to store cache entry
 *
 * @return cache inode status code
 */

cache_inode_status_t nfs_export_get_root_entry(struct gsh_export *export,
					       cache_entry_t **entry)
{
	return cache_inode_get_protected(entry,
					 &export->lock,
					 export_get_root_entry,
					 export);
}

/**
 * @brief Initialize the root cache inode for an export.
 *
 * Assumes being called with the export_by_id.lock held.
 *
 * @param exp [IN] the export
 *
 * @return 0 if successful otherwise err.
 */

int init_export_root(struct gsh_export *export)
{
	fsal_status_t fsal_status;
	cache_inode_status_t cache_status;
	struct fsal_obj_handle *root_handle;
	cache_entry_t *entry = NULL;
	struct root_op_context root_op_context;
	int my_status;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, export, export->fsal_export,
			     0, 0, UNKNOWN_REQUEST);

	/* Lookup for the FSAL Path */
	LogDebug(COMPONENT_EXPORT,
		 "About to lookup_path for ExportId=%u Path=%s",
		 export->export_id, export->fullpath);
	fsal_status =
	    export->fsal_export->exp_ops.lookup_path(export->fsal_export,
						  export->fullpath,
						  &root_handle);

	if (FSAL_IS_ERROR(fsal_status)) {
		my_status = EINVAL;

		LogCrit(COMPONENT_EXPORT,
			"Lookup failed on path, ExportId=%u Path=%s FSAL_ERROR=(%s,%u)",
			export->export_id, export->fullpath,
			msg_fsal_err(fsal_status.major), fsal_status.minor);
		goto out;
	}

	/* Add this entry to the Cache Inode as a "root" entry */

	/* Get the cache inode entry (and an LRU reference */
	cache_status = cache_inode_new_entry(root_handle, CACHE_INODE_FLAG_NONE,
					     &entry);

	if (entry == NULL) {
		/* EFAULT for any internal error */
		my_status = EFAULT;

		LogCrit(COMPONENT_EXPORT,
			"Error when creating root cached entry for %s, export_id=%d, cache_status=%s",
			export->fullpath,
			export->export_id,
			cache_inode_err_str(cache_status));
		goto out;
	}

	/* Instead of an LRU reference, we must hold a pin reference */
	cache_status = cache_inode_inc_pin_ref(entry);

	if (cache_status != CACHE_INODE_SUCCESS) {

		my_status = EFAULT;

		LogCrit(COMPONENT_EXPORT,
			"Error when creating root cached entry for %s, export_id=%d, cache_status=%s",
			export->fullpath,
			export->export_id,
			cache_inode_err_str(cache_status));

		/* Release the LRU reference and return failure. */
		cache_inode_put(entry);
		goto out;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&export->lock);

	export->exp_root_cache_inode = entry;

	glist_add_tail(&entry->object.dir.export_roots,
		       &export->exp_root_list);

	/* Protect this entry from removal (unlink) */
	atomic_inc_int32_t(&entry->exp_root_refcount);

	PTHREAD_RWLOCK_unlock(&export->lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (isDebug(COMPONENT_EXPORT)) {
		LogDebug(COMPONENT_EXPORT,
			 "Added root entry %p FSAL %s for path %s on export_id=%d",
			 entry,
			 entry->obj_handle->fsal->name,
			 export->fullpath, export->export_id);
	} else {
		LogInfo(COMPONENT_EXPORT,
			"Added root entry for path %s on export_id=%d",
			export->fullpath, export->export_id);
	}

	/* Release the LRU reference and return success. */
	cache_inode_put(entry);
	my_status = 0;
out:
	release_root_op_context();
	return my_status;
}

/**
 * @brief Release the root cache inode for an export.
 *
 * @param exp [IN] the export
 */

#define RELEASE_EXP_ROOT_FLAG_NONE		0x0000
#define RELEASE_EXP_ROOT_FLAG_ULOCK_ATTR	0x0001
#define RELEASE_EXP_ROOT_FLAG_ULOCK_LOCK	0x0002
#define RELEASE_EXP_ROOT_FLAG_ULOCK_BOTH \
	(RELEASE_EXP_ROOT_FLAG_ULOCK_ATTR|RELEASE_EXP_ROOT_FLAG_ULOCK_LOCK)

static inline void
release_export_root_locked(struct gsh_export *export,
			   cache_entry_t *entry, uint32_t flags)
{
	cache_entry_t *root_entry = NULL;

	glist_del(&export->exp_root_list);
	root_entry = export->exp_root_cache_inode;
	export->exp_root_cache_inode = NULL;

	if (root_entry != NULL) {
		/* Allow this entry to be removed (unlink) */
		(void)atomic_dec_int32_t(&entry->exp_root_refcount);

		/* We must not hold entry->attr_lock across
		 * cache_inode_dec_pin_ref (LRU lane lock order) */
		if (flags & RELEASE_EXP_ROOT_FLAG_ULOCK_ATTR)
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);

		if (flags & RELEASE_EXP_ROOT_FLAG_ULOCK_LOCK)
			PTHREAD_RWLOCK_unlock(&export->lock);

		/* Release the pin reference */
		cache_inode_dec_pin_ref(root_entry, false);
	}

	LogDebug(COMPONENT_EXPORT,
		 "Released root entry %p for path %s on export_id=%d",
		 root_entry, export->fullpath, export->export_id);
}

/**
 * @brief Release the root cache inode for an export.
 *
 * @param exp [IN] the export
 */

void release_export_root(struct gsh_export *export)
{
	cache_entry_t *entry = NULL;
	cache_inode_status_t status;

	/* Get a reference to the root entry */
	status = nfs_export_get_root_entry(export, &entry);

	if (status != CACHE_INODE_SUCCESS) {
		/* No more root entry, bail out, this export is
		 * probably about to be destroyed.
		 */
		LogInfo(COMPONENT_CACHE_INODE,
			"Export root for export id %d status %s",
			export->export_id, cache_inode_err_str(status));
		return;
	}

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&export->lock);

	/* Make the export unreachable as a root cache inode */
	release_export_root_locked(export, entry,
				   RELEASE_EXP_ROOT_FLAG_ULOCK_BOTH);

	cache_inode_put(entry);
}

void unexport(struct gsh_export *export)
{
	/* Make the export unreachable */
	LogDebug(COMPONENT_EXPORT,
		 "Unexport %s, Pseduo %s",
		 export->fullpath, export->pseudopath);
	pseudo_unmount_export(export);
	remove_gsh_export(export->export_id);
	release_export_root(export);
}

/**
 * @brief Handle killing a cache inode entry that might be an export root.
 *
 * @param entry [IN] the cache inode entry
 */

void kill_export_root_entry(cache_entry_t *entry)
{
	struct gsh_export *export;

	if (entry->type != DIRECTORY)
		return;

	while (true) {
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

		export = glist_first_entry(&entry->object.dir.export_roots,
					   struct gsh_export,
					   exp_root_list);

		if (export == NULL) {
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			return;
		}

		(void) get_gsh_export_ref(export, true);
		LogInfo(COMPONENT_CONFIG,
			"Killing export_id %d because root entry went bad",
			export->export_id);

		PTHREAD_RWLOCK_wrlock(&export->lock);

		/* Make the export unreachable as a root cache inode */
		release_export_root_locked(
			export, entry, RELEASE_EXP_ROOT_FLAG_ULOCK_BOTH);

		/* Make the export otherwise unreachable */
		pseudo_unmount_export(export);
		remove_gsh_export(export->export_id);

		put_gsh_export(export);
	}
}

/**
 * @brief Handle killing a cache inode entry that is a junction to an export.
 *
 * @param entry [IN] the cache inode entry
 */

void kill_export_junction_entry(cache_entry_t *entry)
{
	struct gsh_export *export;

	if (entry->type != DIRECTORY)
		return;

	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	export = entry->object.dir.junction_export;

	if (export == NULL) {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		return;
	}

	/* Detach the export from the inode */
	entry->object.dir.junction_export = NULL;

	(void) get_gsh_export_ref(export, true);

	LogInfo(COMPONENT_CONFIG,
		"Unmounting export_id %d because junction entry went bad",
		export->export_id);

	PTHREAD_RWLOCK_wrlock(&export->lock);

	/* Detach the export */
	export->exp_junction_inode = NULL;

	PTHREAD_RWLOCK_unlock(&export->lock);
	PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	/* Finish unmounting the export */
	pseudo_unmount_export(export);

	/* Don't remove the export (if export root is still valid, the
	 * export is still accessible via NFS v3.
	 */

	put_gsh_export(export);
}

static char *client_types[] = {
	[RAW_CLIENT_LIST] = "RAW_CLIENT_LIST",
	[HOSTIF_CLIENT] = "HOSTIF_CLIENT",
	[NETWORK_CLIENT] = "NETWORK_CLIENT",
	[NETGROUP_CLIENT] = "NETGROUP_CLIENT",
	[WILDCARDHOST_CLIENT] = "WILDCARDHOST_CLIENT",
	[GSSPRINCIPAL_CLIENT] = "GSSPRINCIPAL_CLIENT",
	[HOSTIF_CLIENT_V6] = "HOSTIF_CLIENT_V6",
	[MATCH_ANY_CLIENT] = "MATCH_ANY_CLIENT",
	[BAD_CLIENT] = "BAD_CLIENT"
	 };

/**
 * @brief Match a specific option in the client export list
 *
 * @param[in]  hostaddr      Host to search for
 * @param[in]  clients       Client list to search
 * @param[out] client_found Matching entry
 * @param[in]  export_option Option to search for
 *
 * @return true if found, false otherwise.
 */
static exportlist_client_entry_t *client_match(sockaddr_t *hostaddr,
					       struct gsh_export *export)
{
	struct glist_head *glist;
	in_addr_t addr = get_in_addr(hostaddr);
	int rc;
	int ipvalid = -1;	/* -1 need to print, 0 - invalid, 1 - ok */
	char hostname[MAXHOSTNAMELEN + 1];
	char ipstring[SOCK_NAME_MAX + 1];

	glist_for_each(glist, &export->clients) {
		exportlist_client_entry_t *client;

		client = glist_entry(glist, exportlist_client_entry_t,
				     cle_list);
		LogMidDebug(COMPONENT_EXPORT,
			    "Match %p, type = %s, options 0x%X",
			    client,
			    client_types[client->type],
			    client->client_perms.options);
		LogClientListEntry(COMPONENT_EXPORT, client);

		switch (client->type) {
		case HOSTIF_CLIENT:
			if (client->client.hostif.clientaddr == addr)
				return client;
			break;

		case NETWORK_CLIENT:
			if ((client->client.network.netmask & ntohl(addr)) ==
			    client->client.network.netaddr)
				return client;
			break;

		case NETGROUP_CLIENT:
			/* Try to get the entry from th IP/name cache */
			rc = nfs_ip_name_get(hostaddr, hostname,
					     sizeof(hostname));

			if (rc != IP_NAME_SUCCESS) {
				if (rc == IP_NAME_NOT_FOUND) {
					/* IPaddr was not cached, add it to the
					 * cache
					 */
					if (nfs_ip_name_add(hostaddr,
							    hostname,
							    sizeof(hostname))
					    != IP_NAME_SUCCESS) {
						/* Major failure, name not
						 * be resolved
						 */
						break;
					}
				}
			}

			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (innetgr(client->client.netgroup.netgroupname,
				    hostname, NULL, NULL) == 1) {
				return client;
			}
			break;

		case WILDCARDHOST_CLIENT:
			/* Now checking for IP wildcards */
			if (ipvalid < 0)
				ipvalid = sprint_sockip(hostaddr,
							ipstring,
							sizeof(ipstring));

			if (ipvalid &&
			    (fnmatch(client->client.wildcard.wildcard,
				     ipstring,
				     FNM_PATHNAME) == 0)) {
				return client;
			}

			/* Try to get the entry from th IP/name cache */
			rc = nfs_ip_name_get(hostaddr, hostname,
					     sizeof(hostname));

			if (rc != IP_NAME_SUCCESS) {
				if (rc == IP_NAME_NOT_FOUND) {
					/* IPaddr was not cached, add it to
					 * the cache
					 */
					if (nfs_ip_name_add(hostaddr,
							    hostname,
							    sizeof(hostname))
					    != IP_NAME_SUCCESS) {
						/* Major failure, name could
						 * not be resolved
						 */
/** @todo this change from 1.5 is not IPv6 useful.
 * come back to this and use the string from client mgr inside req_ctx...
 */
						break;
					}
				}
			}
			/* At this point 'hostname' should contain the
			 * name that was found
			 */
			if (fnmatch
			    (client->client.wildcard.wildcard, hostname,
			     FNM_PATHNAME) == 0) {
				return client;
			}
			break;

		case GSSPRINCIPAL_CLIENT:
	  /** @todo BUGAZOMEU a completer lors de l'integration de RPCSEC_GSS */
			LogCrit(COMPONENT_EXPORT,
				"Unsupported type GSS_PRINCIPAL_CLIENT");
			break;

		case HOSTIF_CLIENT_V6:
			break;

		case MATCH_ANY_CLIENT:
			return client;

		case BAD_CLIENT:
		default:
			continue;
		}
	}

	/* no export found for this option */
	return NULL;

}

/**
 * @brief Match a specific option in the client export list
 *
 * @param[in]  paddrv6       Host to search for
 * @param[in]  clients       Client list to search
 * @param[out] client_found Matching entry
 * @param[in]  export_option Option to search for
 *
 * @return true if found, false otherwise.
 */
static exportlist_client_entry_t *client_matchv6(struct in6_addr *paddrv6,
						 struct gsh_export *export)
{
	struct glist_head *glist;

	glist_for_each(glist, &export->clients) {
		exportlist_client_entry_t *client;

		client = glist_entry(glist, exportlist_client_entry_t,
				     cle_list);
		LogMidDebug(COMPONENT_EXPORT,
			    "Matchv6 %p, type = %s, options 0x%X",
			    client,
			    client_types[client->type],
			    client->client_perms.options);
		LogClientListEntry(COMPONENT_EXPORT, client);

		switch (client->type) {
		case HOSTIF_CLIENT:
		case NETWORK_CLIENT:
		case NETGROUP_CLIENT:
		case WILDCARDHOST_CLIENT:
		case GSSPRINCIPAL_CLIENT:
		case BAD_CLIENT:
			break;

		case HOSTIF_CLIENT_V6:
			if (!memcmp(client->client.hostif.clientaddr6.s6_addr,
				    paddrv6->s6_addr, 16)) {
				/* Remember that IPv6 address are
				 * 128 bits = 16 bytes long
				 */
				return client;
			}
			break;

		case MATCH_ANY_CLIENT:
			return client;

		default:
			break;
		}
	}

	/* no export found for this option */
	return NULL;
}

static exportlist_client_entry_t *client_match_any(sockaddr_t *hostaddr,
						   struct gsh_export *export)
{
	if (hostaddr->ss_family == AF_INET6) {
		struct sockaddr_in6 *psockaddr_in6 =
		    (struct sockaddr_in6 *)hostaddr;
		return client_matchv6(&(psockaddr_in6->sin6_addr), export);
	} else {
		return client_match(hostaddr, export);
	}
}

/**
 * @brief Checks if request security flavor is suffcient for the requested
 *        export
 *
 * @param[in] req     Related RPC request.
 *
 * @return true if the request flavor exists in the matching export
 * false otherwise
 */
bool export_check_security(struct svc_req *req)
{
	switch (req->rq_cred.oa_flavor) {
	case AUTH_NONE:
		if ((op_ctx->export_perms->options &
		     EXPORT_OPTION_AUTH_NONE) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support AUTH_NONE",
				op_ctx->export->fullpath);
			return false;
		}
		break;

	case AUTH_UNIX:
		if ((op_ctx->export_perms->options &
		     EXPORT_OPTION_AUTH_UNIX) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support AUTH_UNIX",
				op_ctx->export->fullpath);
			return false;
		}
		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		if ((op_ctx->export_perms->options &
				(EXPORT_OPTION_RPCSEC_GSS_NONE |
				 EXPORT_OPTION_RPCSEC_GSS_INTG |
				 EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support RPCSEC_GSS",
				op_ctx->export->fullpath);
			return false;
		} else {
			struct svc_rpc_gss_data *gd;
			rpc_gss_svc_t svc;
			gd = SVCAUTH_PRIVATE(req->rq_auth);
			svc = gd->sec.svc;
			LogFullDebug(COMPONENT_EXPORT, "Testing svc %d",
				     (int)svc);
			switch (svc) {
			case RPCSEC_GSS_SVC_NONE:
				if ((op_ctx->export_perms->options &
				     EXPORT_OPTION_RPCSEC_GSS_NONE) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_NONE",
						op_ctx->export->fullpath);
					return false;
				}
				break;

			case RPCSEC_GSS_SVC_INTEGRITY:
				if ((op_ctx->export_perms->options &
				     EXPORT_OPTION_RPCSEC_GSS_INTG) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_INTEGRITY",
						op_ctx->export->fullpath);
					return false;
				}
				break;

			case RPCSEC_GSS_SVC_PRIVACY:
				if ((op_ctx->export_perms->options &
				     EXPORT_OPTION_RPCSEC_GSS_PRIV) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_PRIVACY",
						op_ctx->export->fullpath);
					return false;
				}
				break;

			default:
				LogInfo(COMPONENT_EXPORT,
					"Export %s does not support unknown RPCSEC_GSS_SVC %d",
					op_ctx->export->fullpath,
					(int)svc);
				return false;
			}
		}
		break;
#endif
	default:
		LogInfo(COMPONENT_EXPORT,
			"Export %s does not support unknown oa_flavor %d",
			op_ctx->export->fullpath,
			(int)req->rq_cred.oa_flavor);
		return false;
	}

	return true;
}

static char ten_bytes_all_0[10];

sockaddr_t *convert_ipv6_to_ipv4(sockaddr_t *ipv6, sockaddr_t *ipv4)
{
	struct sockaddr_in *paddr = (struct sockaddr_in *)ipv4;
	struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *)ipv6;

	/* If the client socket is IPv4, then it is wrapped into a
	 * ::ffff:a.b.c.d IPv6 address. We check this here.
	 * This kind of adress is shaped like this:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 */
	if ((ipv6->ss_family == AF_INET6)
	    && !memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10)
	    && (psockaddr_in6->sin6_addr.s6_addr[10] == 0xFF)
	    && (psockaddr_in6->sin6_addr.s6_addr[11] == 0xFF)) {
		void *ab;
		memset(ipv4, 0, sizeof(*ipv4));
		ab = &(psockaddr_in6->sin6_addr.s6_addr[12]);

		paddr->sin_port = psockaddr_in6->sin6_port;
		paddr->sin_addr.s_addr = *(in_addr_t *) ab;
		ipv4->ss_family = AF_INET;

		if (isFullDebug(COMPONENT_EXPORT)) {
			char ipstring4[SOCK_NAME_MAX];
			char ipstring6[SOCK_NAME_MAX];

			sprint_sockaddr(ipv6, ipstring6, sizeof(ipstring6));
			sprint_sockaddr(ipv4, ipstring4, sizeof(ipstring4));
			LogMidDebug(COMPONENT_EXPORT,
				    "Converting IPv6 encapsulated IPv4 address %s to IPv4 %s",
				    ipstring6, ipstring4);
		}

		return ipv4;
	} else {
		return ipv6;
	}
}

/**
 * @brief Checks if a machine is authorized to access an export entry
 *
 * Permissions in the op context get updated based on export and client
 */

void export_check_access(void)
{
	exportlist_client_entry_t *client;
	sockaddr_t alt_hostaddr;
	sockaddr_t *hostaddr;

	/* Initialize permissions to allow nothing */
	op_ctx->export_perms->options = 0;
	op_ctx->export_perms->set = 0;
	op_ctx->export_perms->anonymous_uid = (uid_t) ANON_UID;
	op_ctx->export_perms->anonymous_gid = (gid_t) ANON_GID;

	assert(op_ctx != NULL && op_ctx->export != NULL);

	hostaddr = convert_ipv6_to_ipv4(op_ctx->caller_addr, &alt_hostaddr);

	if (isMidDebug(COMPONENT_EXPORT)) {
		char ipstring[SOCK_NAME_MAX];

		ipstring[0] = '\0';
		(void) sprint_sockip(hostaddr,
				     ipstring, sizeof(ipstring));
		LogMidDebug(COMPONENT_EXPORT,
			    "Check for address %s for export id %u fullpath %s",
			    ipstring, op_ctx->export->export_id,
			    op_ctx->export->fullpath);
	}

	/* Does the client match anyone on the client list? */
	client = client_match_any(hostaddr, op_ctx->export);
	if (client != NULL) {
		/* Take client options */
		op_ctx->export_perms->options = client->client_perms.options &
						 client->client_perms.set;

		if (client->client_perms.set & EXPORT_OPTION_ANON_UID_SET)
			op_ctx->export_perms->anonymous_uid =
					client->client_perms.anonymous_uid;

		if (client->client_perms.set & EXPORT_OPTION_ANON_GID_SET)
			op_ctx->export_perms->anonymous_gid =
					client->client_perms.anonymous_gid;

		op_ctx->export_perms->set = client->client_perms.set;
	}

	/* Any options not set by the client, take from the export */
	op_ctx->export_perms->options |=
				op_ctx->export->export_perms.options &
				op_ctx->export->export_perms.set &
				~op_ctx->export_perms->set;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_UID_SET) == 0 &&
	    (op_ctx->export->export_perms.set &
	     EXPORT_OPTION_ANON_UID_SET) != 0)
		op_ctx->export_perms->anonymous_uid =
			op_ctx->export->export_perms.anonymous_uid;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_GID_SET) == 0 &&
	    (op_ctx->export->export_perms.set &
	     EXPORT_OPTION_ANON_GID_SET) != 0)
		op_ctx->export_perms->anonymous_gid =
			op_ctx->export->export_perms.anonymous_gid;

	op_ctx->export_perms->set |= op_ctx->export->export_perms.set;

	/* Any options not set by the client or export, take from the
	 *  EXPORT_DEFAULTS block.
	 */
	op_ctx->export_perms->options |= export_opt.conf.options &
					  export_opt.conf.set &
					  ~op_ctx->export_perms->set;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_UID_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_ANON_UID_SET) != 0)
		op_ctx->export_perms->anonymous_uid =
					export_opt.conf.anonymous_uid;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_GID_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_ANON_GID_SET) != 0)
		op_ctx->export_perms->anonymous_gid =
					export_opt.conf.anonymous_gid;

	op_ctx->export_perms->set |= export_opt.conf.set;

	/* And finally take any options not yet set from global defaults */
	op_ctx->export_perms->options |= export_opt.def.options &
					  ~op_ctx->export_perms->set;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_UID_SET) == 0)
		op_ctx->export_perms->anonymous_uid =
					export_opt.def.anonymous_uid;

	if ((op_ctx->export_perms->set & EXPORT_OPTION_ANON_GID_SET) == 0)
		op_ctx->export_perms->anonymous_gid =
					export_opt.def.anonymous_gid;

	op_ctx->export_perms->set |= export_opt.def.set;

	if (isMidDebug(COMPONENT_EXPORT)) {
		char perms[1024];
		if (client != NULL) {
			StrExportOptions(&client->client_perms, perms);
			LogMidDebug(COMPONENT_EXPORT,
				    "CLIENT          (%s)",
				    perms);
		}
		StrExportOptions(&op_ctx->export->export_perms, perms);
		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT          (%s)",
			    perms);
		StrExportOptions(&export_opt.conf, perms);
		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT_DEFAULTS (%s)",
			    perms);
		StrExportOptions(&export_opt.def, perms);
		LogMidDebug(COMPONENT_EXPORT,
			    "default options (%s)",
			    perms);
		StrExportOptions(op_ctx->export_perms, perms);
		LogMidDebug(COMPONENT_EXPORT,
			    "Final options   (%s)",
			    perms);
	}
}				/* nfs_export_check_access */
