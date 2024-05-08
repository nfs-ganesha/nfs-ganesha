// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "common_utils.h"
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "export_mgr.h"
#include "fsal_up.h"
#include "sal_functions.h"
#include "pnfs_utils.h"
#include "mdcache.h"

/**
 * @brief Protect EXPORT_DEFAULTS structure for dynamic update.
 *
 * If an export->exp_lock is also held by the code, this lock MUST be
 * taken AFTER the export->exp_lock to avoid ABBA deadlock.
 *
 */
pthread_rwlock_t export_opt_lock;

#define GLOBAL_EXPORT_PERMS_INITIALIZER(self)			\
	.def.anonymous_uid = ANON_UID,				\
	.def.anonymous_gid = ANON_GID,				\
	.def.expire_time_attr = EXPORT_DEFAULT_CACHE_EXPIRY,	\
	/* Note: Access_Type defaults to None on purpose     */	\
	/*       And no PROTO is included - that is filled   */	\
	/*       from nfs_param.core_param.core_options.     */ \
	.def.options = EXPORT_OPTION_ROOT_SQUASH |		\
		       EXPORT_OPTION_NO_ACCESS |		\
		       EXPORT_OPTION_AUTH_DEFAULTS |		\
		       EXPORT_OPTION_XPORT_DEFAULTS |		\
		       EXPORT_OPTION_NO_DELEGATIONS,		\
	.def.set = UINT32_MAX,					\
	.clients = {&self.clients, &self.clients},

struct global_export_perms export_opt = {
	GLOBAL_EXPORT_PERMS_INITIALIZER(export_opt)
};

/* A second copy used in configuration, so we can atomically update the
 * primary set.
 */
struct global_export_perms export_opt_cfg = {
	GLOBAL_EXPORT_PERMS_INITIALIZER(export_opt_cfg)
};

static int StrExportOptions(struct display_buffer *dspbuf,
			    struct export_perms *p_perms)
{
	int b_left = display_start(dspbuf);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf, "options=%08"PRIx32"/%08"PRIx32" ",
				p_perms->options, p_perms->set);

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_SQUASH_TYPES) != 0) {
		if ((p_perms->options & EXPORT_OPTION_ROOT_SQUASH) != 0)
			b_left = display_cat(dspbuf, "root_squash   ");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_ROOT_ID_SQUASH) != 0)
			b_left = display_cat(dspbuf, "root_id_squash");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_ALL_ANONYMOUS)  != 0)
			b_left = display_cat(dspbuf, "all_squash    ");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_SQUASH_TYPES) == 0)
			b_left = display_cat(dspbuf, "no_root_squash");
	} else
		b_left = display_cat(dspbuf, "              ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_ACCESS_MASK) != 0) {
		if ((p_perms->options & EXPORT_OPTION_READ_ACCESS) != 0)
			b_left = display_cat(dspbuf, ", R");
		else
			b_left = display_cat(dspbuf, ", -");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_WRITE_ACCESS) != 0)
			b_left = display_cat(dspbuf, "W");
		else
			b_left = display_cat(dspbuf, "-");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_MD_READ_ACCESS) != 0)
			b_left = display_cat(dspbuf, "r");
		else
			b_left = display_cat(dspbuf, "-");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_MD_WRITE_ACCESS) != 0)
			b_left = display_cat(dspbuf, "w");
		else
			b_left = display_cat(dspbuf, "-");
	} else
		b_left = display_cat(dspbuf, ",     ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_PROTOCOLS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_NFSV3) != 0)
			b_left = display_cat(dspbuf, ", 3");
		else
			b_left = display_cat(dspbuf, ", -");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_NFSV4) != 0)
			b_left = display_cat(dspbuf, "4");
		else
			b_left = display_cat(dspbuf, "-");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_9P) != 0)
			b_left = display_cat(dspbuf, "9");
		else
			b_left = display_cat(dspbuf, "-");
	} else
		b_left = display_cat(dspbuf, ",    ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_TRANSPORTS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_UDP) != 0)
			b_left = display_cat(dspbuf, ", UDP");
		else
			b_left = display_cat(dspbuf, ", ---");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_TCP) != 0)
			b_left = display_cat(dspbuf, ", TCP");
		else
			b_left = display_cat(dspbuf, ", ---");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_RDMA) != 0)
			b_left = display_cat(dspbuf, ", RDMA");
		else
			b_left = display_cat(dspbuf, ", ----");
	} else
		b_left = display_cat(dspbuf, ",               ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_MANAGE_GIDS) == 0)
		b_left = display_cat(dspbuf, ",               ");
	else if ((p_perms->options & EXPORT_OPTION_MANAGE_GIDS) != 0)
		b_left = display_cat(dspbuf, ", Manage_Gids   ");
	else
		b_left = display_cat(dspbuf, ", No Manage_Gids");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_DELEGATIONS) != 0) {
		if ((p_perms->options & EXPORT_OPTION_READ_DELEG) != 0)
			b_left = display_cat(dspbuf, ", R");
		else
			b_left = display_cat(dspbuf, ", -");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_WRITE_DELEG) != 0)
			b_left = display_cat(dspbuf, "W Deleg");
		else
			b_left = display_cat(dspbuf, "- Deleg");
	} else
		b_left = display_cat(dspbuf, ",         ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_ANON_UID_SET) != 0)
		b_left = display_printf(dspbuf, ", anon_uid=%6d",
					(int)p_perms->anonymous_uid);
	else
		b_left = display_cat(dspbuf, ",                ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_ANON_GID_SET) != 0)
		b_left = display_printf(dspbuf, ", anon_gid=%6d",
					(int)p_perms->anonymous_gid);
	else
		b_left = display_cat(dspbuf, ",                ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_EXPIRE_SET) != 0)
		b_left = display_printf(dspbuf, ", expire=%8"PRIi32,
					(int)p_perms->expire_time_attr);
	else
		b_left = display_cat(dspbuf, ",                ");

	if (b_left <= 0)
		return b_left;

	if ((p_perms->set & EXPORT_OPTION_AUTH_TYPES) != 0) {
		if ((p_perms->options & EXPORT_OPTION_AUTH_NONE) != 0)
			b_left = display_cat(dspbuf, ", none");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_AUTH_UNIX) != 0)
			b_left = display_cat(dspbuf, ", sys");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_NONE) != 0)
			b_left = display_cat(dspbuf, ", krb5");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_INTG) != 0)
			b_left = display_cat(dspbuf, ", krb5i");

		if (b_left <= 0)
			return b_left;

		if ((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_PRIV) != 0)
			b_left = display_cat(dspbuf, ", krb5p");
	}

	return b_left;
}

void LogExportClientListEntry(log_levels_t level,
			      int line,
			      const char *func,
			      const char *tag,
			      struct exportlist_client_entry *entry)
{
	char buf[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(buf), buf, buf};
	int b_left = display_start(&dspbuf);

	if (!isLevel(COMPONENT_EXPORT, level))
		return;

	if (b_left > 0 && tag != NULL)
		b_left = display_cat(&dspbuf, tag);

	if (b_left > 0 && level >= NIV_DEBUG)
		b_left = display_printf(&dspbuf, "%p ", entry);

	if (b_left > 0)
		b_left = StrClient(&dspbuf, &entry->client_entry);

	if (b_left > 0)
		b_left = display_cat(&dspbuf, " (");

	if (b_left > 0)
		b_left = StrExportOptions(&dspbuf, &entry->client_perms);

	if (b_left > 0)
		b_left = display_cat(&dspbuf, ")");

	DisplayLogComponentLevel(COMPONENT_EXPORT,
				 (char *) __FILE__, line, func, level,
				 "%s", buf);

}

#define LogMidDebug_ExportClientListEntry(tag, cli) \
	LogExportClientListEntry(NIV_MID_DEBUG, \
				 __LINE__, (char *) __func__, tag, cli)

static void LogExportClients(log_levels_t level,
			     int line,
			     const char *func,
			     const char *tag,
			     struct gsh_export *export)
{
	struct glist_head *glist;

	PTHREAD_RWLOCK_rdlock(&export->exp_lock);

	glist_for_each(glist, &export->clients) {
		struct base_client_entry *client;

		client = glist_entry(glist,
				     struct base_client_entry,
				     cle_list);
		LogExportClientListEntry(level, line, func, tag,
					 container_of(
						client,
						struct exportlist_client_entry,
						client_entry));
	}

	PTHREAD_RWLOCK_unlock(&export->exp_lock);
}

#define LogMidDebug_ExportClients(export) \
	LogExportClients(NIV_MID_DEBUG, __LINE__, __func__, NULL, export)

void FreeExportClient(struct base_client_entry *client)
{
	struct exportlist_client_entry *expclient = NULL;

	expclient = container_of(client,
				 struct exportlist_client_entry,
				 client_entry);

	gsh_free(expclient);
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
	struct exportlist_client_entry *expcli;
	struct base_client_entry *cli;

	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL) {
		return self_struct;
	} else if (self_struct == NULL) {
		expcli = gsh_calloc(1,
				    sizeof(struct exportlist_client_entry));

		cli = &expcli->client_entry;
		glist_init(&cli->cle_list);
		cli->type = PROTO_CLIENT;
		return expcli;
	} else { /* free resources case */
		expcli = self_struct;

		cli = &expcli->client_entry;
		if (!glist_empty(&cli->cle_list))
			FreeClientList(&cli->cle_list, FreeExportClient);
		assert(glist_empty(&cli->cle_list));
		gsh_free(expcli);
		return NULL;
	}
}

/**
 * @brief Init for CLIENT sub-block of an export.
 *
 * Allocate one exportlist_client structure for parameter
 * processing. The client_commit will allocate additional
 * exportlist_client__ storage for each of its enumerated
 * clients and free the initial block.  We only free that
 * resource here on errors.
 */

static void *pseudofs_client_init(void *link_mem, void *self_struct)
{
	struct exportlist_client_entry *expcli;

	assert(link_mem != NULL || self_struct != NULL);

	expcli = client_init(link_mem, self_struct);

	if (self_struct != NULL)
		return expcli;

	expcli->client_perms.options =	EXPORT_OPTION_ROOT |
					EXPORT_OPTION_NFSV4;
	expcli->client_perms.set =	EXPORT_OPTION_SQUASH_TYPES |
					EXPORT_OPTION_PROTOCOLS;

	return expcli;
}

/**
 * @brief Commit this client block
 *
 * Validate "clients" token(s) and perms.  We enter with a client entry
 * allocated by proc_block.  Since we expand the clients token both
 * here and in add_client, we allocate new client entries and free
 * what was passed to us rather than try and link it in.
 *
 * @param node [IN] the config_node **not used**
 * @param link_mem [IN] the exportlist entry. add_client adds to its glist.
 * @param self_struct  [IN] the filled out client entry with a PROTO_CLIENT
 *
 * @return 0 on success, error count for failure.
 */

static int client_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	struct exportlist_client_entry *expcli;
	struct base_client_entry *cli;
	struct gsh_export *export;
	int errcnt = 0;

	export = container_of(link_mem, struct gsh_export, clients);

	expcli = self_struct;
	cli = &expcli->client_entry;

	assert(cli->type == PROTO_CLIENT);

	if (glist_empty(&cli->cle_list)) {
		LogCrit(COMPONENT_CONFIG,
			"No clients specified");
		err_type->invalid = true;
		errcnt++;
	} else {
		uint32_t cl_perm_opt, def_opt;

		cl_perm_opt = expcli->client_perms.options;
		def_opt = export_opt.def.options;

		if ((cl_perm_opt & def_opt & EXPORT_OPTION_PROTOCOLS) !=
		    (cl_perm_opt & EXPORT_OPTION_PROTOCOLS)) {
			/* There is a protocol bit set in the options that was
			 * not set by the core param Protocols.
			 */
			LogWarn(COMPONENT_CONFIG,
				"A protocol is specified for a CLIENT block that is not enabled in NFS_CORE_PARAM, fixing up");

			expcli->client_perms.options =
			    (cl_perm_opt & ~EXPORT_OPTION_PROTOCOLS) |
			    (cl_perm_opt & def_opt & EXPORT_OPTION_PROTOCOLS);
		}

		glist_splice_tail(&export->clients, &cli->cle_list);
	}
	if (errcnt == 0)
		client_init(link_mem, self_struct);
	return errcnt;
}

/**
 * @brief Clean up EXPORT path strings
 */
void clean_export_paths(struct gsh_export *export)
{
	LogFullDebug(COMPONENT_EXPORT,
		     "Cleaning paths for %d fullpath %s pseudopath %s",
		     export->export_id,
		     export->cfg_fullpath,
		     export->cfg_pseudopath);

	/* Some admins stuff a '/' at  the end for some reason.
	 * chomp it so we have a /dir/path/basename to work
	 * with. But only if it's a non-root path starting
	 * with /.
	 */
	if (export->cfg_fullpath && export->cfg_fullpath[0] == '/') {
		int pathlen;

		pathlen = strlen(export->cfg_fullpath);
		while ((export->cfg_fullpath[pathlen - 1] == '/') &&
		       (pathlen > 1))
			pathlen--;
		export->cfg_fullpath[pathlen] = '\0';
	}

	/* Remove trailing slash */
	if (export->cfg_pseudopath && export->cfg_pseudopath[0] == '/') {
		int pathlen;

		pathlen = strlen(export->cfg_pseudopath);
		while ((export->cfg_pseudopath[pathlen - 1] == '/') &&
		       (pathlen > 1))
			pathlen--;
		export->cfg_pseudopath[pathlen] = '\0';
	}

	LogFullDebug(COMPONENT_EXPORT,
		     "Final paths for %d fullpath %s pseudopath %s",
		     export->export_id,
		     export->cfg_fullpath,
		     export->cfg_pseudopath);
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

static int fsal_cfg_commit(void *node, void *link_mem, void *self_struct,
			   struct config_error_type *err_type)
{
	struct fsal_export **exp_hdl = link_mem;
	struct gsh_export *export =
	    container_of(exp_hdl, struct gsh_export, fsal_export);
	struct fsal_args *fp = self_struct;
	struct fsal_module *fsal;
	struct req_op_context op_context;
	uint64_t MaxRead, MaxWrite;
	fsal_status_t status;
	int errcnt;

	/* Get a ref to the export and initialize op_context */
	get_gsh_export_ref(export);
	init_op_context_simple(&op_context, export, NULL);

	errcnt = fsal_load_init(node, fp->name, &fsal, err_type);
	if (errcnt > 0)
		goto err;

	clean_export_paths(export);

	/* Since as yet, we don't have gsh_refstr for op_ctx, we need to
	 * create temporary ones here.
	 */
	op_ctx->ctx_fullpath = gsh_refstr_dup(export->cfg_fullpath);
	if (export->cfg_pseudopath != NULL) {
		op_ctx->ctx_pseudopath = gsh_refstr_dup(export->cfg_pseudopath);
	} else {
		/* An export that does not export NFSv4 may not have a
		 * Pseudo Path.
		 */
		op_ctx->ctx_pseudopath = gsh_refstr_get(no_export);
	}

	/* The handle cache (currently MDCACHE) must be at the top of the stack
	 * of FSALs.  To achieve this, call directly into MDCACHE, passing the
	 * sub-FSAL's fsal_module.  MDCACHE will stack itself on top of that
	 * FSAL, continuing down the chain. */
	status = mdcache_fsal_create_export(fsal, node, err_type, &fsal_up_top);

	if (FSAL_IS_ERROR(status)) {
		fsal_put(fsal);
		LogCrit(COMPONENT_CONFIG,
			"Could not create export for (%s) to (%s)",
			export->cfg_pseudopath,
			export->cfg_fullpath);
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %s refcount %"PRIu32,
			     fsal->name,
			     atomic_fetch_int32_t(&fsal->refcount));
		err_type->cur_exp_create_err = true;
		errcnt++;
		goto err;
	}

	assert(op_ctx->fsal_export != NULL);
	export->fsal_export = op_ctx->fsal_export;

	/* We are connected up to the fsal side.  Now
	 * validate maxread/write etc with fsal params
	 */
	MaxRead =
	    export->fsal_export->exp_ops.fs_maxread(export->fsal_export);
	MaxWrite =
	    export->fsal_export->exp_ops.fs_maxwrite(export->fsal_export);

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

	release_op_context();
	/* Don't leak the FSAL block */
	err_type->dispose = true;
	return errcnt;
}

/**
 * @brief Commit a FSAL sub-block for export update
 *
 * Use the Name parameter passed in via the link_mem to lookup the
 * fsal.  If the fsal is not loaded (yet), load it and call its init.
 *
 * Create an export and pass the FSAL sub-block to it so that the
 * fsal method can process the rest of the parameters in the block
 */

static int fsal_update_cfg_commit(void *node, void *link_mem, void *self_struct,
				  struct config_error_type *err_type)
{
	struct fsal_export **exp_hdl = link_mem;
	struct gsh_export *probe_exp;
	struct gsh_export *export =
	    container_of(exp_hdl, struct gsh_export, fsal_export);
	struct fsal_args *fp = self_struct;
	struct req_op_context op_context;
	uint64_t MaxRead, MaxWrite;
	struct fsal_module *fsal;
	fsal_status_t status;
	int errcnt;

	/* Determine if this is actually an update */
	probe_exp = get_gsh_export(export->export_id);

	if (probe_exp == NULL) {
		/* Export not found by ID, assume it's a new export. */
		return fsal_cfg_commit(node, link_mem, self_struct, err_type);
	}

	/* Initialize op_context from the probe_exp */
	init_op_context_simple(&op_context, probe_exp, probe_exp->fsal_export);

	errcnt = fsal_load_init(node, fp->name, &fsal, err_type);

	if (errcnt > 0)
		goto err;

	/* We have to clean the export paths so we can properly compare them
	 * later.
	 */
	clean_export_paths(export);

	/* The handle cache (currently MDCACHE) must be at the top of the stack
	 * of FSALs.  To achieve this, call directly into MDCACHE, passing the
	 * sub-FSAL's fsal_module.  MDCACHE will stack itself on top of that
	 * FSAL, continuing down the chain.
	 */
	status = mdcache_fsal_update_export(fsal, node, err_type,
					    probe_exp->fsal_export);

	if (FSAL_IS_ERROR(status)) {
		fsal_put(fsal);
		LogCrit(COMPONENT_CONFIG,
			"Could not update export for (%s) to (%s)",
			export->cfg_pseudopath,
			export->cfg_fullpath);
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %s refcount %"PRIu32,
			     fsal->name,
			     atomic_fetch_int32_t(&fsal->refcount));
		err_type->cur_exp_create_err = true;
		errcnt++;
		goto err;
	}

	/* We don't assign export->fsal_export because we don't have a new
	 * fsal_export to later release...
	 */

	/* Now validate maxread/write etc with fsal params based on the
	 * original export, which will then allow us to validate the
	 * possibly changed values in the new export config.
	 */
	MaxRead =
	    probe_exp->fsal_export->exp_ops.fs_maxread(probe_exp->fsal_export);
	MaxWrite =
	    probe_exp->fsal_export->exp_ops.fs_maxwrite(probe_exp->fsal_export);

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

	LogDebug(COMPONENT_EXPORT,
		 "Export %d FSAL config update processed",
		 export->export_id);

err:

	release_op_context();

	/* Don't leak the FSAL block */
	err_type->dispose = true;
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
		LogFullDebug(COMPONENT_EXPORT,
			     "Allocated export %p", export);
		return export;
	} else { /* free resources case */
		export = self_struct;
		/* As part of create_export(), FSAL shall take
		 * reference to the export if it supports pNFS.
		 */
		if (export->has_pnfs_ds) {
			assert(export->refcnt == 1);
			/* export is not yet added to the export
			 * manager. Hence there shall not be any
			 * other thread racing here. So no need
			 * to take lock. */
			export->has_pnfs_ds = false;

			/* Remove and destroy the fsal_pnfs_ds */
			pnfs_ds_remove(export->export_id);
		} else {
			/* Release the export allocated above */
			LogFullDebug(COMPONENT_EXPORT,
				     "Releasing export %p", export);
			put_gsh_export_config(export);
		}

		return NULL;
	}
}

static inline int strcmp_null(const char *s1, const char *s2)
{
	if (s1 == s2) {
		/* Both strings are NULL or both are same pointer */
		return 0;
	}

	if (s1 == NULL) {
		/* First string is NULL, consider that LESS than */
		return -1;
	}

	if (s2 == NULL) {
		/* Second string is NULL, consider that GREATER than */
		return 1;
	}

	return strcmp(s1, s2);
}

static inline void update_atomic_fields(struct gsh_export *export,
					struct gsh_export *src)
{
	atomic_store_uint64_t(&export->MaxRead, src->MaxRead);
	atomic_store_uint64_t(&export->MaxWrite, src->MaxWrite);
	atomic_store_uint64_t(&export->PrefRead, src->PrefRead);
	atomic_store_uint64_t(&export->PrefWrite, src->PrefWrite);
	atomic_store_uint64_t(&export->PrefReaddir, src->PrefReaddir);
	atomic_store_uint64_t(&export->MaxOffsetWrite, src->MaxOffsetWrite);
	atomic_store_uint64_t(&export->MaxOffsetRead, src->MaxOffsetRead);
	atomic_store_uint32_t(&export->options, src->options);
	atomic_store_uint32_t(&export->options_set, src->options_set);
}

static inline void copy_gsh_export(struct gsh_export *dest,
				   struct gsh_export *src)
{
	struct gsh_refstr *old_fullpath = NULL, *old_pseudopath = NULL;

	/* Update atomic fields */
	update_atomic_fields(dest, src);

	/* Now take lock and swap out client list and export_perms... */
	PTHREAD_RWLOCK_wrlock(&dest->exp_lock);

	/* Put references to old refstr */
	if (dest->fullpath != NULL)
		old_fullpath = rcu_dereference(dest->fullpath);

	if (dest->pseudopath != NULL)
		old_pseudopath = rcu_dereference(dest->pseudopath);

	/* Free old cfg_fullpath and cfg_pseudopath */
	gsh_free(dest->cfg_fullpath);
	gsh_free(dest->cfg_pseudopath);

	/* Copy config fullpath and create new refstr */
	if (src->cfg_fullpath != NULL) {
		dest->cfg_fullpath = gsh_strdup(src->cfg_fullpath);
		rcu_set_pointer(&(dest->fullpath),
				gsh_refstr_dup(dest->cfg_fullpath));
	} else {
		dest->cfg_fullpath = NULL;
		rcu_set_pointer(&(dest->fullpath), NULL);
	}

	/* Copy config pseudopath and create new refstr */
	if (src->cfg_pseudopath != NULL) {
		dest->cfg_pseudopath = gsh_strdup(src->cfg_pseudopath);
		rcu_set_pointer(&(dest->pseudopath),
				gsh_refstr_dup(dest->cfg_pseudopath));
	} else {
		dest->cfg_pseudopath = NULL;
		rcu_set_pointer(&(dest->pseudopath), NULL);
	}

	synchronize_rcu();

	if (old_fullpath)
		gsh_refstr_put(old_fullpath);

	if (old_pseudopath)
		gsh_refstr_put(old_pseudopath);

	/* Copy the export perms into the existing export. */
	dest->export_perms = src->export_perms;

	/* Swap the client list from the src export and the dest
	 * export. When we then dispose of the new export, the
	 * old client list will also be disposed of.
	 */
	LogFullDebug(COMPONENT_EXPORT,
		     "Original clients = (%p,%p) New clients = (%p,%p)",
		     dest->clients.next, dest->clients.prev,
		     src->clients.next, src->clients.prev);

	glist_swap_lists(&dest->clients, &src->clients);

	PTHREAD_RWLOCK_unlock(&dest->exp_lock);
}

uint32_t export_check_options(struct gsh_export *exp)
{
	struct export_perms perms;

	memset(&perms, 0, sizeof(perms));

	/* Take lock */
	PTHREAD_RWLOCK_rdlock(&exp->exp_lock);

	/* Start with options set for the export */
	perms.options = exp->export_perms.options & exp->export_perms.set;

	perms.set = exp->export_perms.set;

	PTHREAD_RWLOCK_rdlock(&export_opt_lock);

	/* Any options not set by the export, take from the EXPORT_DEFAULTS
	 * block.
	 */
	perms.options |= export_opt.conf.options & export_opt.conf.set &
								~perms.set;

	perms.set |= export_opt.conf.set;

	/* And finally take any options not yet set from global defaults */
	perms.options |= export_opt.def.options & ~perms.set;

	perms.set |= export_opt.def.set;

	if (isMidDebug(COMPONENT_EXPORT)) {
		char str[1024] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		(void) StrExportOptions(&dspbuf, &exp->export_perms);

		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT          (%s)",
			    str);

		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &export_opt.conf);

		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT_DEFAULTS (%s)",
			    str);

		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &export_opt.def);

		LogMidDebug(COMPONENT_EXPORT,
			    "default options (%s)",
			    str);

		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &perms);

		LogMidDebug(COMPONENT_EXPORT,
			    "Final options   (%s)",
			    str);
	}

	PTHREAD_RWLOCK_unlock(&export_opt_lock);

	/* Release lock */
	PTHREAD_RWLOCK_unlock(&exp->exp_lock);

	return perms.options;
}

/**
 * @brief Commit an export block
 *
 * Validate the export level parameters.  fsal and client
 * parameters are already done.
 */

enum export_commit_type {
	initial_export,
	add_export,
	update_export,
};

static int export_commit_common(void *node, void *link_mem, void *self_struct,
				struct config_error_type *err_type,
				enum export_commit_type commit_type)
{
	struct gsh_export *export = self_struct, *probe_exp;
	int errcnt = 0;
	char perms[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(perms), perms, perms};
	uint32_t options = export_check_options(export);

	LogFullDebug(COMPONENT_EXPORT, "Processing %p", export);

	/* Validate the pseudo path if present is an absolute path. */
	if (export->cfg_pseudopath != NULL &&
	    export->cfg_pseudopath[0] != '/') {
		LogCrit(COMPONENT_CONFIG,
			"A Pseudo path must be an absolute path");
		err_type->invalid = true;
		errcnt++;
		return errcnt;
	}

	/* Validate, if export_id 0 is explicitly configured, the pseudopath
	 * MUST be "/".
	 */
	if (export->export_id == 0 &&
	    export->cfg_pseudopath != NULL &&
	    export->cfg_pseudopath[1] != '\0') {
		LogCrit(COMPONENT_CONFIG,
			"Export id 0 can only export \"/\" not (%s)",
			export->cfg_pseudopath);
		err_type->invalid = true;
		errcnt++;
		return errcnt;
	}

	/* validate the export now */
	if ((export->export_perms.options & EXPORT_OPTION_NFSV4) != 0 &&
	    (export->export_perms.set & EXPORT_OPTION_NFSV4) != 0 &&
	    export->cfg_pseudopath == NULL) {
		/* This is only an error if the export is explicitly
		 * exported NFSv4.
		 */
		LogCrit(COMPONENT_CONFIG,
			"Export %d would be exported NFSv4 explicitly but no Pseudo path defined",
			export->export_id);
		err_type->invalid = true;
		errcnt++;
		return errcnt;
	} else if ((options & EXPORT_OPTION_NFSV4) != 0 &&
		   export->cfg_pseudopath == NULL) {
		/* This is an export without a pseudopath when the default
		 * options indicate all exports are to be exported NFSv4.
		 */
		LogWarn(COMPONENT_CONFIG,
			"Export %d would be exported NFSv4 by default but no Pseudo path defined",
			export->export_id);

		export->export_perms.options =
		    (export->export_perms.options & ~EXPORT_OPTION_PROTOCOLS) |
		    (options & EXPORT_OPTION_PROTOCOLS & ~EXPORT_OPTION_NFSV4);

		export->export_perms.set |= EXPORT_OPTION_PROTOCOLS;
	}

	/* Validate if export_id 0 is explicitly configured that it WILL be
	 * exported NFSv4, even if due to defaults.
	 */
	if (export->export_id == 0 && (options & EXPORT_OPTION_NFSV4) == 0) {
		LogCrit(COMPONENT_CONFIG,
			"Export id 0 MUST be exported at least NFSv4");
		err_type->invalid = true;
		errcnt++;
		return errcnt;
	}

	if ((export->export_perms.options & export_opt.def.options &
	     export->export_perms.set & EXPORT_OPTION_PROTOCOLS) !=
	    (export->export_perms.options & export->export_perms.set &
						EXPORT_OPTION_PROTOCOLS)) {
		/* There is a protocol bit set in the options that was not
		 * set by the core param Protocols.
		 */
		LogWarn(COMPONENT_CONFIG,
			"A protocol is specified for export %d that is not enabled in NFS_CORE_PARAM, fixing up",
			export->export_id);

		export->export_perms.options =
		    (export->export_perms.options & ~EXPORT_OPTION_PROTOCOLS) |
		    (export->export_perms.options & export_opt.def.options &
						EXPORT_OPTION_PROTOCOLS);
	}

	/* If we are using mount_path_pseudo = true we MUST have a Pseudo Path.
	 */
	if (nfs_param.core_param.mount_path_pseudo &&
	    export->cfg_pseudopath == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"NFS_CORE_PARAM mount_path_pseudo is TRUE but no Pseudo path defined");
		err_type->invalid = true;
		errcnt++;
		return errcnt;
	}

	if (export->export_id == 0) {
		if (export->cfg_pseudopath == NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path must be \"/\" for export id 0");
			err_type->invalid = true;
			errcnt++;
		} else if (export->cfg_pseudopath[1] != '\0') {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path must be \"/\" for export id 0");
			err_type->invalid = true;
			errcnt++;
		}
		if ((export->export_perms.options & export->export_perms.set &
		     EXPORT_OPTION_NFSV4) != EXPORT_OPTION_NFSV4) {
			LogCrit(COMPONENT_CONFIG,
				"Export id 0 must include 4 in Protocols");
			err_type->invalid = true;
			errcnt++;
		}
	}

	if (errcnt) {
		LogCrit(COMPONENT_CONFIG,
			"Error count %d exiting", errcnt);
		return errcnt;  /* have basic errors. don't even try more... */
	}

	/* Note: need to check export->fsal_export AFTER we have checked for
	 * duplicate export_id. That is because an update export WILL NOT
	 * have fsal_export attached.
	 */

	probe_exp = get_gsh_export(export->export_id);

	if (commit_type == update_export && probe_exp != NULL) {
		bool mount_export = false;
		bool mount_status_changed = false;

		/* We have an actual update case, probe_exp is the target
		 * to update. Check all the options that MUST match.
		 * Note that Path/fullpath will not be NULL, but we compare
		 * the same way as the other string options for code
		 * consistency.
		 *
		 * It's ok in here to directly access the gsh_refstr because we
		 * can't be racing with another thread for this export...
		 */
		LogFullDebug(COMPONENT_EXPORT, "Updating %p", probe_exp);

		LogMidDebug(COMPONENT_EXPORT, "Old Client List");
		LogMidDebug_ExportClients(probe_exp);

		LogMidDebug(COMPONENT_EXPORT, "New Client List");
		LogMidDebug_ExportClients(export);

		if (strcmp_null(export->FS_tag,
				probe_exp->FS_tag) != 0) {
			/* Tag does not match, currently not a candidate for
			 * update.
			 */
			LogCrit(COMPONENT_CONFIG,
				"Tag for export update %d %s doesn't match %s",
				export->export_id,
				export->FS_tag, probe_exp->FS_tag);
			err_type->invalid = true;
			errcnt++;
		}

		if (strcmp_null(export->cfg_pseudopath,
				probe_exp->cfg_pseudopath) != 0) {
			/* Pseudo does not match, mark to unmount old and
			 * mount at new location.
			 */
			LogInfo(COMPONENT_EXPORT,
				"Pseudo for export %d changing to %s from to %s",
				export->export_id,
				export->cfg_pseudopath,
				probe_exp->cfg_pseudopath);
			mount_status_changed |= true;
			mount_export |= export_can_be_mounted(export);
		}

		if (strcmp_null(export->cfg_fullpath,
				probe_exp->cfg_fullpath) != 0) {
			/* Path does not match, currently not a candidate for
			 * update.
			 */
			LogCrit(COMPONENT_CONFIG,
				"Path for export update %d %s doesn't match %s",
				export->export_id,
				export->cfg_fullpath,
				probe_exp->cfg_fullpath);
			err_type->invalid = true;
			errcnt++;
		}

		/* At present Filesystem_Id is not updateable, check that
		 * it did not change.
		 */
		if (probe_exp->filesystem_id.major
					!= export->filesystem_id.major ||
		    probe_exp->filesystem_id.minor
					!= export->filesystem_id.minor) {
			LogCrit(COMPONENT_CONFIG,
				"Filesystem_Id for export update %d %"
				PRIu64".%"PRIu64" doesn't match%"
				PRIu64".%"PRIu64,
				export->export_id,
				export->filesystem_id.major,
				export->filesystem_id.minor,
				probe_exp->filesystem_id.major,
				probe_exp->filesystem_id.minor);
			err_type->invalid = true;
			errcnt++;
		}

		/* We can't compare the FSAL names because we don't actually
		 * have an fsal_export for "export".
		 */

		if (errcnt > 0) {
			put_gsh_export(probe_exp);
			LogCrit(COMPONENT_CONFIG,
				"Error count %d exiting", errcnt);
			return errcnt;
		}

		if (((options & EXPORT_OPTION_NFSV4) == EXPORT_OPTION_NFSV4) !=
		    probe_exp->is_mounted) {
			/* The new options for NFSv4 probably don't match the
			 * old options.
			 */
			bool mountable = export_can_be_mounted(export);

			LogDebug(COMPONENT_EXPORT,
				 "Export %d NFSv4 changing from %s to %s",
				 probe_exp->export_id,
				 probe_exp->is_mounted
					? "mounted" : "not mounted",
				 mountable
					? "can be mounted"
					: "can not be mounted");
			mount_status_changed |= true;
			mount_export |= mountable;
		}

		if (mount_export) {
			/* This export has changed in a way that it needs to be
			 * remounted.
			 */
			probe_exp->update_remount = true;
		}

		if (mount_status_changed && export->is_mounted) {
			/* Mark this export to be unmounted during the prune
			 * phase, it will also be added to the remount work if
			 * appropriate.
			 */
			probe_exp->update_prune_unmount = true;
		}

		/* Grab config_generation for this config */
		probe_exp->config_gen = get_parse_root_generation(node);

		copy_gsh_export(probe_exp, export);

		/* We will need to dispose of the config export since we
		 * updated the existing export.
		 */
		err_type->dispose = true;

		/* Release the reference to the updated export. */
		put_gsh_export(probe_exp);
		goto success;
	}

	if (commit_type == update_export) {
		/* We found a new export during export update, consider it
		 * an add_export for the rest of configuration.
		 */
		commit_type = add_export;
	}

	if (probe_exp != NULL) {
		LogDebug(COMPONENT_EXPORT,
			 "Export %d already exists", export->export_id);
		put_gsh_export(probe_exp);
		err_type->exists = true;
		errcnt++;
	}

	/* export->fsal_export is valid iff fsal_cfg_commit succeeds.
	 * Config code calls export_commit even if fsal_cfg_commit fails at
	 * the moment, so error out here if fsal_cfg_commit failed.
	 */
	if (export->fsal_export == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"fsal_export is NULL");
		err_type->validate = true;
		errcnt++;
		return errcnt;
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

	if (export->cfg_pseudopath != NULL) {
		probe_exp =
			get_gsh_export_by_pseudo(export->cfg_pseudopath, true);
		if (probe_exp != NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Pseudo path (%s) is a duplicate",
				export->cfg_pseudopath);
			if (!err_type->exists)
				err_type->invalid = true;
			errcnt++;
			put_gsh_export(probe_exp);
		}
	}

	probe_exp = get_gsh_export_by_path(export->cfg_fullpath, true);

	if (probe_exp != NULL) {
		if (export->cfg_pseudopath == NULL &&
		    export->FS_tag == NULL) {
			LogCrit(COMPONENT_CONFIG,
				"Duplicate path (%s) without unique tag or Pseudo path",
				export->cfg_fullpath);
			err_type->invalid = true;
			errcnt++;
		}
		/* If unique Tag and/or Pseudo, there is no error, but we still
		 * need to release the export reference.
		 */
		put_gsh_export(probe_exp);
	}

	if (errcnt) {
		if (err_type->exists && !err_type->invalid)
			LogDebug(COMPONENT_EXPORT,
				 "Duplicate export id = %d",
				 export->export_id);
		else
			LogCrit(COMPONENT_CONFIG,
				 "Duplicate export id = %d",
				 export->export_id);
		return errcnt;  /* have errors. don't init or load a fsal */
	}

	/* Convert fullpath and pseudopath into gsh_refstr. Do this now so that
	 * init_export_root() has them available when it creates root context.
	 */
	export->fullpath = gsh_refstr_dup(export->cfg_fullpath);
	if (export->cfg_pseudopath != NULL) {
		export->pseudopath = gsh_refstr_dup(export->cfg_pseudopath);
	} else {
		/* An export that does not export NFSv4 may not have a
		 * Pseudo Path.
		 */
		export->pseudopath = NULL;
	}

	if (commit_type != initial_export) {
		/* add_export or update_export with new export_id. */
		int rc = init_export_root(export);

		if (rc) {
			switch (rc) {
			case EINVAL:
				err_type->invalid = true;
				break;

			case EFAULT:
				err_type->internal = true;
				break;

			default:
				err_type->resource = true;
			}

			LogCrit(COMPONENT_CONFIG,
				"init_export_root failed");
			errcnt++;
			return errcnt;
		}

		if (!mount_gsh_export(export)) {
			LogCrit(COMPONENT_CONFIG,
				"mount_gsh_export failed");
			err_type->internal = true;
			errcnt++;
			return errcnt;
		}
	}

	if (!insert_gsh_export(export)) {
		LogCrit(COMPONENT_CONFIG,
			"Export id %d already in use.",
			export->export_id);
		err_type->exists = true;
		errcnt++;
		return errcnt;
	}

	/* add_export_commit shouldn't add this export to mount work as
	 * add_export_commit deals with creating pseudo mount directly.
	 * So add this export to mount work only if is not a dynamically
	 * added export. We add all such exports because only once all the
	 * export config load is complete can we be sure of all the options.
	 */
	if (commit_type == initial_export)
		export_add_to_mount_work(export);

	LogMidDebug_ExportClients(export);

	/* Copy the generation */
	export->config_gen = get_parse_root_generation(node);

success:

	(void) StrExportOptions(&dspbuf, &export->export_perms);

	/* It's ok below to directly access the gsh_refstr without an additional
	 * reference because we can't be racing with another thread on this
	 * export...
	 */
	LogInfo(COMPONENT_CONFIG,
		"Export %d %s at pseudo (%s) with path (%s) and tag (%s) perms (%s)",
		export->export_id,
		commit_type == update_export ? "updated" : "created",
		export->cfg_pseudopath,
		export->cfg_fullpath, export->FS_tag, perms);

	LogInfo(COMPONENT_CONFIG,
		"Export %d has %zd defined clients", export->export_id,
		glist_length(&export->clients));

	if (commit_type != update_export) {
		/* For initial or add export, the alloc_export in export_init
		 * gave a reference to the export for use during the
		 * configuration. Above insert_gsh_export added a sentinel
		 * reference. Now, since this export commit is final, we can
		 * drop the reference from the alloc_export.
		 *
		 * In the case of update_export, we already dropped the
		 * reference to the updated export, and this export has
		 * no references and will be freed by the config code.
		 */
		put_gsh_export(export);
	}

	if (errcnt > 0) {
		LogCrit(COMPONENT_CONFIG,
			"Error count %d exiting", errcnt);
	}

	return errcnt;
}

static int export_commit(void *node, void *link_mem, void *self_struct,
			 struct config_error_type *err_type)
{
	LogDebug(COMPONENT_EXPORT, "EXPORT commit");

	return export_commit_common(node, link_mem, self_struct, err_type,
				    initial_export);
}

int pseudofs_fsal_commit(void *self_struct, struct config_error_type *err_type)
{
	struct gsh_export *export = self_struct;
	struct fsal_module *fsal_hdl = NULL;
	struct req_op_context op_context;
	int errcnt = 0;

	/* Take an export reference and initialize req_ctx with the export
	 * reasonably constructed
	 */
	get_gsh_export_ref(export);
	init_op_context_simple(&op_context, export, NULL);

	/* Assign FSAL_PSEUDO */
	fsal_hdl = lookup_fsal("PSEUDO");

	if (fsal_hdl == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"FSAL PSEUDO is not loaded!");
		err_type->invalid = true;
		errcnt = 1;
		goto err_out;
	} else {
		fsal_status_t rc;

		rc = mdcache_fsal_create_export(fsal_hdl, NULL, err_type,
						&fsal_up_top);

		if (FSAL_IS_ERROR(rc)) {
			fsal_put(fsal_hdl);
			LogCrit(COMPONENT_CONFIG,
				"Could not create FSAL export for %s",
				export->cfg_fullpath);
			LogFullDebug(COMPONENT_FSAL,
				     "FSAL %s refcount %"PRIu32,
				     fsal_hdl->name,
				     atomic_fetch_int32_t(&fsal_hdl->refcount));
			err_type->invalid = true;
			errcnt = 1;
			goto err_out;
		}

	}

	assert(op_ctx->fsal_export != NULL);
	export->fsal_export = op_ctx->fsal_export;

err_out:

	/* Release the export reference from above. */
	release_op_context();

	return errcnt;
}

static int pseudofs_commit(void *node, void *link_mem, void *self_struct,
			   struct config_error_type *err_type)
{
	int rc = pseudofs_fsal_commit(self_struct, err_type);

	if (rc != 0)
		return rc;

	LogDebug(COMPONENT_EXPORT, "PSEUDOFS commit");

	return export_commit_common(node, link_mem, self_struct, err_type,
				    initial_export);
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
	char perms[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(perms), perms, perms};

	(void) StrExportOptions(&dspbuf, &export->export_perms);

	LogMidDebug(COMPONENT_EXPORT,
		    "%s %p Export %d pseudo (%s) with path (%s) and tag (%s) perms (%s)",
		    step, export, export->export_id, export->cfg_pseudopath,
		    export->cfg_fullpath, export->FS_tag, perms);
}

/**
 * @brief Commit an add export
 * commit the export
 * init export root and mount it in pseudo fs
 */

static int add_export_commit(void *node, void *link_mem, void *self_struct,
			     struct config_error_type *err_type)
{
	LogDebug(COMPONENT_EXPORT, "ADD EXPORT commit");

	return export_commit_common(node, link_mem, self_struct, err_type,
				    add_export);
}

/**
 * @brief Check if the ExportId already exists and active
 * Duplicate ExportID cannot be exported more than once
 */

static bool check_export_duplicate(void *self_struct,
				   struct config_error_type *err_type)
{
	bool duplicate = false;
	struct gsh_export *export = self_struct, *probe_exp;

	probe_exp = get_gsh_export(export->export_id);
	if (probe_exp != NULL) {
		LogDebug(COMPONENT_EXPORT,
			"Export %d already exists", export->export_id);
		put_gsh_export(probe_exp);
		err_type->exists = true;
		duplicate = true;
	}
	return duplicate;
}

/**
 * @brief Commit an update export
 * commit the export
 * init export root and mount it in pseudo fs
 */

static int update_export_commit(void *node, void *link_mem, void *self_struct,
				struct config_error_type *err_type)
{
	LogDebug(COMPONENT_EXPORT, "UPDATE EXPORT commit");

	return export_commit_common(node, link_mem, self_struct, err_type,
				    update_export);
}

/**
 * @brief Commit an update export
 * commit the export
 * init export root and mount it in pseudo fs
 */

static int update_pseudofs_commit(void *node, void *link_mem, void *self_struct,
				  struct config_error_type *err_type)
{
	int rc;

	LogDebug(COMPONENT_EXPORT, "UPDATE PSEUDOFS commit");

	rc = pseudofs_fsal_commit(self_struct, err_type);

	if (rc != 0)
		return rc;

	return export_commit_common(node, link_mem, self_struct, err_type,
				    update_export);
}

/**
 * @brief Initialize an EXPORT_DEFAULTS block
 *
 */

static void *export_defaults_init(void *link_mem, void *self_struct)
{
	if (link_mem == NULL) {
		return self_struct;
	} else if (self_struct == NULL) {
		return &export_opt_cfg;
	} else { /* free resources case */
		FreeClientList(&export_opt_cfg.clients, FreeExportClient);
		return NULL;
	}
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
	char perms[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(perms), perms, perms};

	(void) StrExportOptions(&dspbuf, &export_opt_cfg.conf);

	LogInfo(COMPONENT_CONFIG, "Export Defaults now (%s)", perms);

	/* Update under lock. */
	PTHREAD_RWLOCK_wrlock(&export_opt_lock);
	export_opt.conf = export_opt_cfg.conf;

	/* Swap the client list from export_opt_cfg export and export_opt. */
	LogFullDebug(COMPONENT_EXPORT,
		     "Original clients = (%p,%p) New clients = (%p,%p)",
		     export_opt.clients.next, export_opt.clients.prev,
		     export_opt_cfg.clients.next, export_opt_cfg.clients.prev);

	glist_swap_lists(&export_opt.clients, &export_opt_cfg.clients);

	PTHREAD_RWLOCK_unlock(&export_opt_lock);

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
	char perms[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(perms), perms, perms};

	(void) StrExportOptions(&dspbuf, defaults);

	LogMidDebug(COMPONENT_EXPORT,
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
	CONFIG_LIST_TOK("RootId", EXPORT_OPTION_ROOT_ID_SQUASH),
	CONFIG_LIST_TOK("Root_Id_Squash", EXPORT_OPTION_ROOT_ID_SQUASH),
	CONFIG_LIST_TOK("RootIdSquash", EXPORT_OPTION_ROOT_ID_SQUASH),
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
		EXPORT_OPTION_ACCESS_MASK,				\
		access_types, _struct_, _perms_.options, _perms_.set),	\
	/* Note: Protocols will now pick up from NFS Core Param */	\
	CONF_ITEM_LIST_BITS_SET("Protocols",				\
		EXPORT_OPTION_PROTO_DEFAULTS, EXPORT_OPTION_PROTOCOLS,	\
		nfs_protocols, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_LIST_BITS_SET("Transports",				\
		EXPORT_OPTION_XPORT_DEFAULTS, EXPORT_OPTION_TRANSPORTS,	\
		transports, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_ANON_ID_SET("Anonymous_uid",				\
		ANON_UID, _struct_, _perms_.anonymous_uid,		\
		EXPORT_OPTION_ANON_UID_SET, _perms_.set),		\
	CONF_ITEM_ANON_ID_SET("Anonymous_gid",				\
		ANON_GID, _struct_, _perms_.anonymous_gid,		\
		EXPORT_OPTION_ANON_GID_SET, _perms_.set),		\
	CONF_ITEM_LIST_BITS_SET("SecType",				\
		EXPORT_OPTION_AUTH_DEFAULTS, EXPORT_OPTION_AUTH_TYPES,	\
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
		delegations, _struct_, _perms_.options, _perms_.set)

#define CONF_PSEUDOFS_PERMS(_struct_, _perms_)				\
	/* Note: Access_Type defaults to MD READ on purpose */		\
	/*       MD READ or NONE are the only access that makes sense. */ \
	CONF_ITEM_ENUM_BITS_SET("Access_Type",				\
		EXPORT_OPTION_MD_READ_ACCESS,				\
		EXPORT_OPTION_ACCESS_MASK,				\
		access_types, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_LIST_BITS_SET("Transports",				\
		EXPORT_OPTION_TCP, EXPORT_OPTION_TRANSPORTS,		\
		transports, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_LIST_BITS_SET("SecType",				\
		EXPORT_OPTION_AUTH_TYPES, EXPORT_OPTION_AUTH_TYPES,	\
		sec_types, _struct_, _perms_.options, _perms_.set),	\
	CONF_ITEM_BOOLBIT_SET("PrivilegedPort",				\
		false, EXPORT_OPTION_PRIVILEGED_PORT,			\
		_struct_, _perms_.options, _perms_.set)

void *export_client_allocator(void)
{
	struct exportlist_client_entry *expcli;

	expcli = gsh_calloc(1, sizeof(struct exportlist_client_entry));

	return &expcli->client_entry;
}

void export_client_filler(struct base_client_entry *client, void *private_data)
{
	struct exportlist_client_entry *expcli;
	struct export_perms *perms = private_data;

	expcli = container_of(client,
			      struct exportlist_client_entry,
			      client_entry);

	expcli->client_perms = *perms;

	LogMidDebug_ExportClientListEntry("", expcli);
}

/**
 * @brief Process a list of clients for a client block
 *
 * CONFIG_PROC handler that gets called for each token in the term list.
 * Create a exportlist_client_entry for each token and link it into
 * the proto client's cle_list list head.  We will pass that head to the
 * export in commit.
 *
 * NOTES: this is the place to expand a node list with perhaps moving the
 * call to add_client into the expander rather than build a list there
 * to be then walked here...
 *
 * @param token [IN] pointer to token string from parse tree
 * @param type_hint [IN] a type hint from what the parser recognized
 * @param item [IN] pointer to the config item table entry
 * @param param_addr [IN] pointer to prototype client entry
 * @param err_type [OUT] error handling
 * @return error count
 */

static int client_adder(const char *token,
			enum term_type type_hint,
			struct config_item *item,
			void *param_addr,
			void *cnode,
			struct config_error_type *err_type)
{
	struct base_client_entry *client;
	struct exportlist_client_entry *proto_cli;
	int rc;

	client = container_of(param_addr,
			      struct base_client_entry,
			      cle_list);

	proto_cli = container_of(client,
				 struct exportlist_client_entry,
				 client_entry);

	LogMidDebug(COMPONENT_EXPORT, "Adding client %s", token);

	rc = add_client(COMPONENT_EXPORT,
			&client->cle_list,
			token, type_hint, cnode, err_type,
			export_client_allocator,
			export_client_filler,
			&proto_cli->client_perms);
	return rc;
}

/**
 * @brief Table of client sub-block parameters
 *
 * NOTE: node discovery is ordered by this table!
 * "Clients" is last because we must have all other params processed
 * before we walk the list of accessing clients!
 */

static struct config_item client_params[] = {
	CONF_EXPORT_PERMS(exportlist_client_entry, client_perms),
	CONF_ITEM_PROC_MULT("Clients", noop_conf_init, client_adder,
			    base_client_entry, cle_list),
	CONFIG_EOL
};

/**
 * @brief Table of pseudofs client sub-block parameters
 *
 * NOTE: node discovery is ordered by this table!
 * "Clients" is last because we must have all other params processed
 * before we walk the list of accessing clients!
 */

static struct config_item pseudo_fs_client_params[] = {
	CONF_PSEUDOFS_PERMS(exportlist_client_entry, client_perms),
	CONF_ITEM_PROC_MULT("Clients", noop_conf_init, client_adder,
			    base_client_entry, cle_list),
	CONFIG_EOL
};

/**
 * @brief Table of DEXPORT_DEFAULTS block parameters
 *
 * NOTE: node discovery is ordered by this table!
 */

static struct config_item export_defaults_params[] = {
	CONF_EXPORT_PERMS(global_export_perms, conf),
	CONF_ITEM_I32_SET("Attr_Expiration_Time", -1, INT32_MAX,
		       EXPORT_DEFAULT_CACHE_EXPIRY,
		       global_export_perms, conf.expire_time_attr,
		       EXPORT_OPTION_EXPIRE_SET, conf.set),
	CONF_ITEM_BLOCK_MULT("Client", client_params,
			     client_init, client_commit,
			     global_export_perms, clients),
	CONFIG_EOL
};

/**
 * @brief Table of FSAL sub-block parameters
 *
 * NOTE: this points to a struct that is private to
 * fsal_cfg_commit.
 */

static struct config_item fsal_params[] = {
	CONF_ITEM_STR("Name", 1, 10, NULL,
		      fsal_args, name), /* cheater union */
	CONFIG_EOL
};

/**
 * @brief Common EXPORT block parameters
 */
#define CONF_EXPORT_PARAMS(_struct_)					\
	CONF_MAND_UI16("Export_id", 0, UINT16_MAX, 1,			\
		       _struct_, export_id),				\
	CONF_MAND_PATH("Path", 1, MAXPATHLEN, NULL,			\
		       _struct_, cfg_fullpath), /* must chomp '/' */	\
	CONF_ITEM_PATH("Pseudo", 1, MAXPATHLEN, NULL,			\
		       _struct_, cfg_pseudopath),			\
	CONF_ITEM_UI64_SET("MaxRead", 512, FSAL_MAXIOSIZE,		\
			FSAL_MAXIOSIZE, _struct_, MaxRead,		\
			EXPORT_OPTION_MAXREAD_SET, options_set),	\
	CONF_ITEM_UI64_SET("MaxWrite", 512, FSAL_MAXIOSIZE,		\
			FSAL_MAXIOSIZE, _struct_, MaxWrite,		\
			EXPORT_OPTION_MAXWRITE_SET, options_set),	\
	CONF_ITEM_UI64_SET("PrefRead", 512, FSAL_MAXIOSIZE,		\
			FSAL_MAXIOSIZE, _struct_, PrefRead,		\
			EXPORT_OPTION_PREFREAD_SET, options_set),	\
	CONF_ITEM_UI64_SET("PrefWrite", 512, FSAL_MAXIOSIZE,		\
			FSAL_MAXIOSIZE, _struct_, PrefWrite,		\
			EXPORT_OPTION_PREFWRITE_SET, options_set),	\
	CONF_ITEM_UI64("PrefReaddir", 512, FSAL_MAXIOSIZE, 16384,	\
		       _struct_, PrefReaddir),				\
	CONF_ITEM_FSID_SET("Filesystem_id", 666, 666,			\
		       _struct_, filesystem_id, /* major.minor */	\
		       EXPORT_OPTION_FSID_SET, options_set),		\
	CONF_ITEM_STR("Tag", 1, MAXPATHLEN, NULL,			\
		      _struct_, FS_tag),				\
	CONF_ITEM_UI64("MaxOffsetWrite", 512, UINT64_MAX, INT64_MAX,	\
		       _struct_, MaxOffsetWrite),			\
	CONF_ITEM_UI64("MaxOffsetRead", 512, UINT64_MAX, INT64_MAX,	\
		       _struct_, MaxOffsetRead),			\
	CONF_ITEM_BOOLBIT_SET("UseCookieVerifier",			\
		false, EXPORT_OPTION_USE_COOKIE_VERIFIER,		\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("DisableReaddirPlus",			\
		false, EXPORT_OPTION_NO_READDIR_PLUS,			\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("Trust_Readdir_Negative_Cache",		\
		false, EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE,	\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("Disable_ACL",				\
		false, EXPORT_OPTION_DISABLE_ACL,			\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("Security_Label",				\
		false, EXPORT_OPTION_SECLABEL_SET,			\
		_struct_, options, options_set)

/**
 * @brief Table of EXPORT block parameters
 */

static struct config_item export_params[] = {
	CONF_EXPORT_PARAMS(gsh_export),
	CONF_EXPORT_PERMS(gsh_export, export_perms),
	CONF_ITEM_I32_SET("Attr_Expiration_Time", -1, INT32_MAX,
		       EXPORT_DEFAULT_CACHE_EXPIRY,
		       gsh_export, export_perms.expire_time_attr,
		       EXPORT_OPTION_EXPIRE_SET, export_perms.set),

	/* NOTE: the Client and FSAL sub-blocks must be the *last*
	 * two entries in the list.  This is so all other
	 * parameters have been processed before these sub-blocks
	 * are processed.
	 */
	CONF_ITEM_BLOCK_MULT("Client", client_params,
			     client_init, client_commit,
			     gsh_export, clients),
	CONF_RELAX_BLOCK("FSAL", fsal_params,
			 fsal_init, fsal_cfg_commit,
			 gsh_export, fsal_export),
	CONFIG_EOL
};

/**
 * @brief Table of EXPORT update block parameters
 */

static struct config_item export_update_params[] = {
	CONF_EXPORT_PARAMS(gsh_export),
	CONF_EXPORT_PERMS(gsh_export, export_perms),
	CONF_ITEM_I32_SET("Attr_Expiration_Time", -1, INT32_MAX,
		       EXPORT_DEFAULT_CACHE_EXPIRY,
		       gsh_export, export_perms.expire_time_attr,
		       EXPORT_OPTION_EXPIRE_SET, export_perms.set),

	/* NOTE: the Client and FSAL sub-blocks must be the *last*
	 * two entries in the list.  This is so all other
	 * parameters have been processed before these sub-blocks
	 * are processed.
	 */
	CONF_ITEM_BLOCK_MULT("Client", client_params,
			     client_init, client_commit,
			     gsh_export, clients),
	CONF_RELAX_BLOCK("FSAL", fsal_params,
			 fsal_init, fsal_update_cfg_commit,
			 gsh_export, fsal_export),
	CONFIG_EOL
};

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

static void *pseudofs_init(void *link_mem, void *self_struct)
{
	struct gsh_export *export = export_init(link_mem, self_struct);

	if (self_struct != NULL) {
		return export;
	}

	/* The initialization case */
	export->filesystem_id.major = 152;
	export->filesystem_id.minor = 152;
	export->MaxWrite = FSAL_MAXIOSIZE;
	export->MaxRead = FSAL_MAXIOSIZE;
	export->PrefWrite = FSAL_MAXIOSIZE;
	export->PrefRead = FSAL_MAXIOSIZE;
	export->PrefReaddir = 16384;
	export->config_gen = UINT64_MAX;

	/*Don't set anonymous uid and gid, they will actually be ignored */

	/* Support only NFS v4 and TCP.
	 * Root is allowed
	 * MD Read Access
	 * Allow use of default auth types
	 *
	 * Allow non-privileged client ports to access pseudo export.
	 */
	export->export_perms.options = EXPORT_OPTION_ROOT |
					EXPORT_OPTION_MD_READ_ACCESS |
					EXPORT_OPTION_NFSV4 |
					EXPORT_OPTION_AUTH_TYPES |
					EXPORT_OPTION_TCP;

	export->export_perms.set = EXPORT_OPTION_SQUASH_TYPES |
				    EXPORT_OPTION_ACCESS_MASK |
				    EXPORT_OPTION_PROTOCOLS |
				    EXPORT_OPTION_TRANSPORTS |
				    EXPORT_OPTION_AUTH_TYPES |
				    EXPORT_OPTION_PRIVILEGED_PORT;

	export->options = EXPORT_OPTION_USE_COOKIE_VERIFIER;
	export->options_set = EXPORT_OPTION_FSID_SET |
			      EXPORT_OPTION_USE_COOKIE_VERIFIER |
			      EXPORT_OPTION_MAXREAD_SET |
			      EXPORT_OPTION_MAXWRITE_SET |
			      EXPORT_OPTION_PREFREAD_SET |
			      EXPORT_OPTION_PREFWRITE_SET;

	/* Set the fullpath to "/" */
	export->cfg_fullpath = gsh_strdup("/");

	/* Set Pseudo Path to "/" */
	export->cfg_pseudopath = gsh_strdup("/");

	export->pseudopath = gsh_refstr_dup("/");
	export->fullpath = gsh_refstr_dup("/");

	LOG_EXPORT(NIV_FULL_DEBUG, "pseudofs_init", export, true);

	return export;
}

/**
 * @brief Common PSEUDOFS block parameters
 */
#define CONF_PSEUDOFS_PARAMS(_struct_)					\
	CONF_ITEM_UI16("Export_id", 0, UINT16_MAX, 0,			\
		       _struct_, export_id),				\
	CONF_ITEM_UI64("PrefReaddir", 512, FSAL_MAXIOSIZE, 16384,	\
		       _struct_, PrefReaddir),				\
	CONF_ITEM_FSID_SET("Filesystem_id", 152, 152,			\
		       _struct_, filesystem_id, /* major.minor */	\
		       EXPORT_OPTION_FSID_SET, options_set),		\
	CONF_ITEM_BOOLBIT_SET("UseCookieVerifier",			\
		false, EXPORT_OPTION_USE_COOKIE_VERIFIER,		\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("DisableReaddirPlus",			\
		false, EXPORT_OPTION_NO_READDIR_PLUS,			\
		_struct_, options, options_set),			\
	CONF_ITEM_BOOLBIT_SET("Trust_Readdir_Negative_Cache",		\
		false, EXPORT_OPTION_TRUST_READIR_NEGATIVE_CACHE,	\
		_struct_, options, options_set)

/**
 * @brief Table of PSEUDOFS block parameters
 */

static struct config_item pseudofs_params[] = {
	CONF_PSEUDOFS_PARAMS(gsh_export),
	CONF_PSEUDOFS_PERMS(gsh_export, export_perms),

	/* NOTE: the Client sub-block must be the *last*
	 * entry in the list.  This is so all other
	 * parameters have been processed before this sub-block
	 * is processed.
	 */
	CONF_ITEM_BLOCK_MULT("Client", pseudo_fs_client_params,
			     pseudofs_client_init, client_commit,
			     gsh_export, clients),
	CONFIG_EOL
};

/**
 * @brief Table of PSEUDOFS update block parameters
 */

static struct config_item pseudofs_update_params[] = {
	CONF_PSEUDOFS_PARAMS(gsh_export),
	CONF_PSEUDOFS_PERMS(gsh_export, export_perms),

	/* NOTE: the Client sub-block must be the *last*
	 * entry in the list.  This is so all other
	 * parameters have been processed before this sub-block
	 * is processed.
	 */
	CONF_ITEM_BLOCK_MULT("Client", pseudo_fs_client_params,
			     pseudofs_client_init, client_commit,
			     gsh_export, clients),
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
	.blk_desc.u.blk.display = export_display,
	.blk_desc.u.blk.check = check_export_duplicate
};

/**
 * @brief Top level definition for an UPDATE EXPORT block
 */

struct config_block update_export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.%d",
	.blk_desc.name = "EXPORT",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = export_init,
	.blk_desc.u.blk.params = export_update_params,
	.blk_desc.u.blk.commit = update_export_commit,
	.blk_desc.u.blk.display = export_display
};

/**
 * @brief Top level definition for an PSEUDOFS block
 */

static struct config_block pseudofs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.%d",
	.blk_desc.name = "PSEUDOFS",
	.blk_desc.type = CONFIG_BLOCK,
	/* too risky to have more, and don't allocate if block not present */
	.blk_desc.flags = CONFIG_UNIQUE | CONFIG_NO_DEFAULT,
	.blk_desc.u.blk.init = pseudofs_init,
	.blk_desc.u.blk.params = pseudofs_params,
	.blk_desc.u.blk.commit = pseudofs_commit,
	.blk_desc.u.blk.display = export_display
};

/**
 * @brief Top level definition for an UPDATE PSEUDOFS block
 */

struct config_block update_pseudofs_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.%d",
	.blk_desc.name = "PSEUDOFS",
	.blk_desc.type = CONFIG_BLOCK,
	/* too risky to have more, and don't allocate if block not present */
	.blk_desc.flags = CONFIG_UNIQUE | CONFIG_NO_DEFAULT,
	.blk_desc.u.blk.init = pseudofs_init,
	.blk_desc.u.blk.params = pseudofs_update_params,
	.blk_desc.u.blk.commit = update_pseudofs_commit,
	.blk_desc.u.blk.display = export_display
};

/**
 * @brief Top level definition for an EXPORT_DEFAULTS block
 */

struct config_block export_defaults_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.defaults",
	.blk_desc.name = "EXPORT_DEFAULTS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE,  /* too risky to have more */
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

static int build_default_root(struct config_error_type *err_type)
{
	struct gsh_export *export;
	struct fsal_module *fsal_hdl = NULL;
	struct req_op_context op_context;

	/* See if export_id = 0 has already been specified */
	export = get_gsh_export(0);

	if (export != NULL) {
		/* export_id = 0 has already been specified */
		LogDebug(COMPONENT_EXPORT,
			 "Export 0 already exists");
		put_gsh_export(export);
		return 0;
	}

	/* See if another export with Pseudo = "/" has already been specified.
	 */
	export = get_gsh_export_by_pseudo("/", true);

	if (export != NULL) {
		/* Pseudo = / has already been specified */
		LogDebug(COMPONENT_EXPORT,
			 "Pseudo root already exists");
		put_gsh_export(export);
		return 0;
	}

	/* allocate and initialize the exportlist part with the id */
	LogDebug(COMPONENT_EXPORT,
		 "Allocating Pseudo root export");

	/* We can call the same function that config uses to allocate and
	 * initialize a gsh_export structure by passing both parameters as
	 * NULL.
	 */
	export = pseudofs_init(NULL, NULL);

	/* Initialize req_ctx with the export reasonably constructed using the
	 * reference provided above by alloc_export().
	 */
	init_op_context_simple(&op_context, export, NULL);

	/* Assign FSAL_PSEUDO */
	fsal_hdl = lookup_fsal("PSEUDO");

	if (fsal_hdl == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"FSAL PSEUDO is not loaded!");
		goto err_out;
	} else {
		fsal_status_t rc;

		rc = mdcache_fsal_create_export(fsal_hdl, NULL, err_type,
						&fsal_up_top);

		if (FSAL_IS_ERROR(rc)) {
			fsal_put(fsal_hdl);
			LogCrit(COMPONENT_CONFIG,
				"Could not create FSAL export for %s",
				export->cfg_fullpath);
			LogFullDebug(COMPONENT_FSAL,
				     "FSAL %s refcount %"PRIu32,
				     fsal_hdl->name,
				     atomic_fetch_int32_t(&fsal_hdl->refcount));
			goto err_out;
		}

	}

	assert(op_ctx->fsal_export != NULL);
	export->fsal_export = op_ctx->fsal_export;

	if (!insert_gsh_export(export)) {
		export->fsal_export->exp_ops.release(export->fsal_export);
		fsal_put(fsal_hdl);
		LogCrit(COMPONENT_CONFIG,
			"Failed to insert pseudo root   In use??");
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %s refcount %"PRIu32,
			     fsal_hdl->name,
			     atomic_fetch_int32_t(&fsal_hdl->refcount));
		goto err_out;
	}

	/* This export must be mounted to the PseudoFS */
	export_add_to_mount_work(export);

	LogInfo(COMPONENT_CONFIG,
		"Export 0 (/) successfully created");

	/* Release the reference from alloc_export() above, since the
	 * insert worked, a sentinel reference has been taken, so this
	 * reference release won't result in freeing the export.
	 */
	release_op_context();
	return 1;

err_out:
	/* Release the export reference from alloc_export() above which will
	 * result in cleaning up and freeing the export.
	 */
	release_op_context();
	return -1;
}

bool log_an_export(struct gsh_export *exp, void *state)
{
	struct log_exports_parms *lep = state;
	char perms[1024] = "\0";
	struct display_buffer dspbuf = {sizeof(perms), perms, perms};

	if (exp == NULL) {
		if (isLevel(COMPONENT_EXPORT, lep->level)) {
			DisplayLogComponentLevel(COMPONENT_EXPORT,
						 (char *) lep->file, lep->line,
						 lep->func, lep->level,
						 "%s%sNO EXPORT",
						 lep->tag ? lep->tag : "",
						 lep->tag ? " " : "");
		}

		return false;
	}

	(void) StrExportOptions(&dspbuf, &exp->export_perms);

	if (isLevel(COMPONENT_EXPORT, lep->level)) {
		DisplayLogComponentLevel(COMPONENT_EXPORT,
					 (char *) lep->file, lep->line,
					 lep->func, lep->level,
					 "%s%sExport %p %5d pseudo (%s) with path (%s) and tag (%s) perms (%s)",
					 lep->tag ? lep->tag : "",
					 lep->tag ? " " : "",
					 exp,
					 exp->export_id, exp->cfg_pseudopath,
					 exp->cfg_fullpath, exp->FS_tag, perms);
	}

	if (lep->clients)
		LogExportClients(lep->level, lep->line, lep->func, "   ", exp);

	return true;
}

void log_all_exports(log_levels_t level, int line, const char *func)
{
	struct log_exports_parms lep = {
				level, __FILE__, line, func, NULL, true};

	foreach_gsh_export(log_an_export, false, &lep);
}

/**
 * @brief Read the export entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the export list
 *
 * @return A negative value on error,
 *         the number of export entries else.
 */
#define NFS_options nfs_param.core_param.core_options

int ReadExports(config_file_t in_config,
		struct config_error_type *err_type)
{
	int rc, num_exp;

	LogMidDebug(COMPONENT_EXPORT,
		    "CORE_OPTION_NFSV3 %d CORE_OPTION_NFSV4 %d CORE_OPTION_9P %d",
		    (NFS_options & CORE_OPTION_NFSV3) != 0,
		    (NFS_options & CORE_OPTION_NFSV4) != 0,
		    (NFS_options & CORE_OPTION_9P) != 0);

	/* Set Protocols in export_opt.def.options from nfs_core_param. */
	if (NFS_options & CORE_OPTION_NFSV3)
		export_opt.def.options |= EXPORT_OPTION_NFSV3;
	if (NFS_options & CORE_OPTION_NFSV4)
		export_opt.def.options |= EXPORT_OPTION_NFSV4;
	if (NFS_options & CORE_OPTION_9P)
		export_opt.def.options |= EXPORT_OPTION_9P;

	rc = load_config_from_parse(in_config,
				    &export_defaults_param,
				    &export_opt_cfg,
				    false,
				    err_type);
	if (rc < 0) {
		LogCrit(COMPONENT_CONFIG, "Export defaults block error");
		return -1;
	}

	if (isMidDebug(COMPONENT_EXPORT)) {
		char perms[1024] = "\0";
		struct display_buffer dspbuf = {sizeof(perms), perms, perms};

		(void) StrExportOptions(&dspbuf, &export_opt.conf);
		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT_DEFAULTS (%s)",
			    perms);
		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &export_opt.def);
		LogMidDebug(COMPONENT_EXPORT,
			    "default options (%s)",
			    perms);
		display_reset_buffer(&dspbuf);
	}

	rc = load_config_from_parse(in_config,
				    &pseudofs_param,
				    NULL,
				    false,
				    err_type);
	if (rc < 0) {
		LogCrit(COMPONENT_CONFIG, "Pseudofs block error");
		return -1;
	}

	num_exp = load_config_from_parse(in_config,
				    &export_param,
				    NULL,
				    false,
				    err_type);
	if (num_exp < 0) {
		LogCrit(COMPONENT_CONFIG, "Export block error");
		return -1;
	}

	rc = build_default_root(err_type);
	if (rc < 0) {
		LogCrit(COMPONENT_CONFIG, "No pseudo root!");
		return -1;
	}

	log_all_exports(NIV_INFO, __LINE__, __func__);

	return num_exp;
}

/**
 * @brief Reread the export entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the export list
 *
 * @return A negative value on error,
 *         the number of export entries else.
 */

int reread_exports(config_file_t in_config,
		   struct config_error_type *err_type)
{
	int rc, num_exp;
	uint64_t generation;

	EXPORT_ADMIN_LOCK();

	LogInfo(COMPONENT_CONFIG, "Reread exports starting");

	LogDebug(COMPONENT_EXPORT, "Exports before update");
	log_all_exports(NIV_DEBUG, __LINE__, __func__);

	rc = load_config_from_parse(in_config,
				    &export_defaults_param,
				    &export_opt_cfg,
				    false,
				    err_type);

	if (rc < 0) {
		LogCrit(COMPONENT_CONFIG, "Export defaults block error");
		num_exp = -1;
		goto out;
	}

	LogDebug(COMPONENT_EXPORT, "About to update pseudofs block");

	rc = load_config_from_parse(in_config,
				    &update_pseudofs_param,
				    NULL,
				    false,
				    err_type);

	if (rc < 0) {
		LogCrit(COMPONENT_CONFIG, "Pseudofs block error");
		num_exp = -1;
		goto out;
	}

	num_exp = load_config_from_parse(in_config,
					 &update_export_param,
					 NULL,
					 false,
					 err_type);

	if (num_exp < 0) {
		LogCrit(COMPONENT_CONFIG, "Export block error");
		num_exp = -1;
		goto out;
	}

	generation = get_config_generation(in_config);

	/* Prune the pseudofs of all exports that will be unexported (defunct)
	 * as well as any descendant exports. Then unexport all defunct exports.
	 * Finally remount all the exports that were unmounted. If that fails,
	 * create_pseudofs() will LogFatal and abort.
	 */
	prune_pseudofs_subtree(NULL, generation, false);
	prune_defunct_exports(generation);
	create_pseudofs();

	LogEvent(COMPONENT_CONFIG, "Reread exports complete");
	LogInfo(COMPONENT_EXPORT, "Exports after update");
	log_all_exports(NIV_INFO, __LINE__, __func__);

out:

	EXPORT_ADMIN_UNLOCK();

	return num_exp;
}

/**
 * @brief Free resources attached to an export
 *
 * @param export [IN] pointer to export
 *
 * @return true if all went well
 */

void free_export_resources(struct gsh_export *export, bool config)
{
	struct req_op_context op_context;
	bool restore_op_ctx = false;

	LogDebug(COMPONENT_EXPORT,
		 "Free resources for export %p id %d path %s",
		 export, export->export_id, export->cfg_fullpath);

	if (op_ctx == NULL || op_ctx->ctx_export != export) {
		/* We need to complete export cleanup with this export.
		 * Otherwise we SHOULD be being called in the final throes
		 * of releasing an op context, or at least the export so
		 * attached. We don't need a reference to the export because we
		 * are already inside freeing it.
		 */
		init_op_context_simple(&op_context, export,
				       export->fsal_export);
		restore_op_ctx = true;
	}

	LogDebug(COMPONENT_EXPORT, "Export root %p", export->exp_root_obj);

	release_export(export, config);

	LogDebug(COMPONENT_EXPORT, "release_export complete");

	FreeClientList(&export->clients, FreeExportClient);
	if (export->fsal_export != NULL) {
		struct fsal_module *fsal = export->fsal_export->fsal;

		export->fsal_export->exp_ops.release(export->fsal_export);
		fsal_put(fsal);
		LogFullDebug(COMPONENT_FSAL,
			     "FSAL %s refcount %"PRIu32,
			     fsal->name,
			     atomic_fetch_int32_t(&fsal->refcount));
	}

	export->fsal_export = NULL;

	/* free strings here */
	gsh_free(export->cfg_fullpath);
	gsh_free(export->cfg_pseudopath);
	gsh_free(export->FS_tag);

	/* Release the refstr if they have been created. Note that we
	 * normally expect a refstr to be created, but we could be freeing an
	 * export for which config failed before we were able to create the
	 * refstr for it.
	 */
	if (export->fullpath != NULL)
		gsh_refstr_put(export->fullpath);

	if (export->pseudopath != NULL)
		gsh_refstr_put(export->pseudopath);

	/* At this point the export is no longer usable so poison the op
	 * context (whether the one set up above or a pre-existing one.
	 * However, we leave the refstr in the op context alone, they will be
	 * cleaned up by the eventual clear_op_context_export() on the current
	 * op context (either the original one or the temporary one created
	 * above).
	 */
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;

	LogDebug(COMPONENT_EXPORT,
		 "Goodbye export %p path %s pseudo %s",
		 export, CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx));

	if (restore_op_ctx) {
		/* And restore to the original op context */
		release_op_context();
	}
}

/**
 * @brief pkginit callback to initialize exports from nfs_init
 *
 * Assumes being called with the export_by_id.lock held.
 * true on success
 */

static bool init_export_cb(struct gsh_export *exp, void *state)
{
	struct glist_head *errlist = state;

	if (init_export_root(exp)) {
		glist_del(&exp->exp_list);
		glist_add(errlist, &exp->exp_list);
	}

	return true;
}

/**
 * @brief Initialize exports over a live fsal layer
 */

void exports_pkginit(void)
{
	struct glist_head errlist;
	struct glist_head *glist, *glistn;
	struct gsh_export *export;

	glist_init(&errlist);
	foreach_gsh_export(init_export_cb, true, &errlist);

	glist_for_each_safe(glist, glistn, &errlist) {
		export = glist_entry(glist, struct gsh_export, exp_list);
		export_revert(export);
	}
}

/**
 * @brief Return a reference to the root object of the export
 *
 * Must be called with the caller holding a reference to the export.
 *
 * Returns with an additional reference to the obj held for use by the
 * caller.
 *
 * @param export [IN] the aforementioned export
 * @param entry  [IN/OUT] call by ref pointer to store obj
 *
 * @return FSAL status
 */

fsal_status_t nfs_export_get_root_entry(struct gsh_export *export,
					struct fsal_obj_handle **obj)
{
	PTHREAD_RWLOCK_rdlock(&export->exp_lock);

	if (export->exp_root_obj)
		export->exp_root_obj->obj_ops->get_ref(export->exp_root_obj);

	*obj = export->exp_root_obj;

	PTHREAD_RWLOCK_unlock(&export->exp_lock);

	if (!(*obj))
		return fsalstat(ERR_FSAL_NOENT, 0);

	if ((*obj)->type != DIRECTORY) {
		(*obj)->obj_ops->put_ref(*obj);
		*obj = NULL;
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set file systems max read write sizes in the export
 *
 * @param export [IN] the export
 * @param maxread [IN] maxread size
 * @param maxwrite [IN] maxwrite size
 */

static void set_fs_max_rdwr_size(struct gsh_export *export, uint64_t maxread,
				 uint64_t maxwrite)
{
	if (maxread != 0) {
		if (!op_ctx_export_has_option_set(EXPORT_OPTION_MAXREAD_SET)) {
			LogInfo(COMPONENT_EXPORT,
				"Readjusting MaxRead to %" PRIu64,
				maxread);
			export->MaxRead = maxread;
		}
	}

	if (maxwrite != 0) {
		if (!op_ctx_export_has_option_set(EXPORT_OPTION_MAXWRITE_SET)) {
			LogInfo(COMPONENT_EXPORT,
				"Readjusting MaxWrite to %"PRIu64,
				maxwrite);
			export->MaxWrite = maxwrite;
		}
	}

	if (export->PrefRead > export->MaxRead) {
		LogInfo(COMPONENT_EXPORT,
			"Readjusting PrefRead to %"PRIu64,
			export->MaxRead);
		export->PrefRead = export->MaxRead;
	}

	if (export->PrefWrite > export->MaxWrite) {
		LogInfo(COMPONENT_EXPORT,
			"Readjusting PrefWrite to %"PRIu64,
			export->MaxWrite);
		export->PrefWrite = export->MaxWrite;
	}
}

/**
 * @brief Initialize the root object for an export.
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
	struct fsal_obj_handle *obj;
	struct req_op_context op_context;
	int my_status;

	/* Get a ref to the export and initialize op_context */
	get_gsh_export_ref(export);
	init_op_context_simple(&op_context, export, export->fsal_export);

	/* set expire_time_attr if appropriate */
	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0 &&
	    (op_ctx->ctx_export->export_perms.set &
	     EXPORT_OPTION_EXPIRE_SET) != 0) {
		op_ctx->export_perms.expire_time_attr =
			op_ctx->ctx_export->export_perms.expire_time_attr;
		op_ctx->export_perms.set |= EXPORT_OPTION_EXPIRE_SET;
	}
	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_EXPIRE_SET) != 0) {
		op_ctx->export_perms.expire_time_attr =
			export_opt.conf.expire_time_attr;
		op_ctx->export_perms.set |= EXPORT_OPTION_EXPIRE_SET;
	}
	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0)
		op_ctx->export_perms.expire_time_attr =
					export_opt.def.expire_time_attr;

	/* set the EXPORT_OPTION_EXPIRE_SET bit from the export
	 * into the op_ctx */
	op_ctx->export_perms.options |= EXPORT_OPTION_EXPIRE_SET;

	/* Lookup for the FSAL Path */
	LogDebug(COMPONENT_EXPORT,
		 "About to lookup_path for ExportId=%u Path=%s",
		 export->export_id, CTX_FULLPATH(op_ctx));

	/* This takes a reference, which will keep the root object around for
	 * the lifetime of the export. */
	fsal_status = export->fsal_export->exp_ops.lookup_path(
				export->fsal_export, CTX_FULLPATH(op_ctx),
				&obj, NULL);

	if (FSAL_IS_ERROR(fsal_status)) {
		my_status = EINVAL;

		LogCrit(COMPONENT_EXPORT,
			"Lookup failed on path, ExportId=%u Path=%s FSAL_ERROR=(%s,%u)",
			export->export_id, CTX_FULLPATH(op_ctx),
			msg_fsal_err(fsal_status.major), fsal_status.minor);
		goto out;
	}

	if (!op_ctx_export_has_option_set(EXPORT_OPTION_MAXREAD_SET) ||
	    !op_ctx_export_has_option_set(EXPORT_OPTION_MAXWRITE_SET) ||
	    !op_ctx_export_has_option_set(EXPORT_OPTION_PREFREAD_SET) ||
	    !op_ctx_export_has_option_set(EXPORT_OPTION_PREFWRITE_SET)) {

		fsal_dynamicfsinfo_t dynamicinfo;

		dynamicinfo.maxread = 0;
		dynamicinfo.maxwrite = 0;
		fsal_status =
			export->fsal_export->exp_ops.get_fs_dynamic_info(
				export->fsal_export, obj, &dynamicinfo);

		if (!FSAL_IS_ERROR(fsal_status)) {
			set_fs_max_rdwr_size(export,
					     dynamicinfo.maxread,
					     dynamicinfo.maxwrite);
		}
	}

	PTHREAD_RWLOCK_wrlock(&export->exp_lock);
	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->jct_lock);

	/* Get a reference on the object */
	obj->obj_ops->get_ref(obj);

	/* Pass ref off to export */
	export_root_object_get(obj);
	export->exp_root_obj = obj;
	glist_add_tail(&obj->state_hdl->dir.export_roots,
		       &export->exp_root_list);
	/* Protect this entry from removal (unlink) */
	(void) atomic_inc_int32_t(&obj->state_hdl->dir.exp_root_refcount);

	PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);
	PTHREAD_RWLOCK_unlock(&export->exp_lock);

	LogDebug(COMPONENT_EXPORT,
		 "Added root obj %p FSAL %s for path %s on export_id=%d",
		 obj, obj->fsal->name, CTX_FULLPATH(op_ctx),
		 export->export_id);

	my_status = 0;
out:

	release_op_context();
	return my_status;
}

/**
 * @brief Release all the export state, including the root object
 *
 * @param exp [IN]     the export
 * @param config [IN]  this export is only a config object
 */

void release_export(struct gsh_export *export, bool config)
{
	struct fsal_obj_handle *obj = NULL;
	fsal_status_t fsal_status;

	if (!config) {
		LogDebug(COMPONENT_EXPORT,
			 "Unexport %s, Pseudo %s",
			 CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx));
	}

	/* Get a reference to the root entry */
	fsal_status = nfs_export_get_root_entry(export, &obj);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* No more root entry, bail out, this export is
		 * probably about to be destroyed.
		 */
		LogInfo(COMPONENT_MDCACHE,
			"Export root for export id %d status %s",
			export->export_id, msg_fsal_err(fsal_status.major));
		return;
	}

	/* Make the export unreachable as a root object */
	PTHREAD_RWLOCK_wrlock(&export->exp_lock);
	PTHREAD_RWLOCK_wrlock(&obj->state_hdl->jct_lock);

	glist_del(&export->exp_root_list);
	export_root_object_put(export->exp_root_obj);
	export->exp_root_obj->obj_ops->put_ref(export->exp_root_obj);
	export->exp_root_obj = NULL;

	(void) atomic_dec_int32_t(&obj->state_hdl->dir.exp_root_refcount);

	PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);
	PTHREAD_RWLOCK_unlock(&export->exp_lock);

	LogDebug(COMPONENT_EXPORT,
		 "Released root obj %p for path %s, pseudo %s on export_id=%d",
		 obj, CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx),
		 export->export_id);

	if (!config) {
		/* Make export unreachable via pseudo fs.
		 * We keep the export in the export hash table through the
		 * following so that the underlying FSALs have access to the
		 * export while performing the various cleanup operations.
		 */
		pseudo_unmount_export_tree(export);
	}

	export->fsal_export->exp_ops.prepare_unexport(export->fsal_export);

	if (!config) {
		/* Release state belonging to this export */
		state_release_export(export);
	}

	/* Flush FSAL-specific state */
	LogFullDebug(COMPONENT_EXPORT,
		     "About to unexport from FSAL root obj %p for path %s, pseudo %s on export_id=%d",
		     obj, CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx),
		     export->export_id);

	export->fsal_export->exp_ops.unexport(export->fsal_export, obj);

	if (!config) {
		/* Remove the mapping to the export now that cleanup is
		 * complete.
		 */
		remove_gsh_export(export->export_id);
	}

	/* Release the reference */
	obj->obj_ops->put_ref(obj);

	/* Release ref taken above */
	LogFullDebug(COMPONENT_EXPORT,
		     "About to put_ref root obj %p for path %s, pseudo %s on export_id=%d",
		     obj, CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx),
		     export->export_id);

	obj->obj_ops->put_ref(obj);
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
	switch (req->rq_msg.cb_cred.oa_flavor) {
	case AUTH_NONE:
		if ((op_ctx->export_perms.options &
		     EXPORT_OPTION_AUTH_NONE) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support AUTH_NONE",
				op_ctx_export_path(op_ctx));
			return false;
		}
		break;

	case AUTH_UNIX:
		if ((op_ctx->export_perms.options &
		     EXPORT_OPTION_AUTH_UNIX) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support AUTH_UNIX",
				op_ctx_export_path(op_ctx));
			return false;
		}
		break;

#ifdef _HAVE_GSSAPI
	case RPCSEC_GSS:
		if ((op_ctx->export_perms.options &
				(EXPORT_OPTION_RPCSEC_GSS_NONE |
				 EXPORT_OPTION_RPCSEC_GSS_INTG |
				 EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0) {
			LogInfo(COMPONENT_EXPORT,
				"Export %s does not support RPCSEC_GSS",
				op_ctx_export_path(op_ctx));
			return false;
		} else {
			struct rpc_gss_cred *gc = (struct rpc_gss_cred *)
				req->rq_msg.rq_cred_body;
			rpc_gss_svc_t svc = gc->gc_svc;

			LogFullDebug(COMPONENT_EXPORT, "Testing svc %d",
				     (int)svc);
			switch (svc) {
			case RPCSEC_GSS_SVC_NONE:
				if ((op_ctx->export_perms.options &
				     EXPORT_OPTION_RPCSEC_GSS_NONE) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_NONE",
						op_ctx_export_path(op_ctx));
					return false;
				}
				break;

			case RPCSEC_GSS_SVC_INTEGRITY:
				if ((op_ctx->export_perms.options &
				     EXPORT_OPTION_RPCSEC_GSS_INTG) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_INTEGRITY",
						op_ctx_export_path(op_ctx));
					return false;
				}
				break;

			case RPCSEC_GSS_SVC_PRIVACY:
				if ((op_ctx->export_perms.options &
				     EXPORT_OPTION_RPCSEC_GSS_PRIV) == 0) {
					LogInfo(COMPONENT_EXPORT,
						"Export %s does not support RPCSEC_GSS_SVC_PRIVACY",
						op_ctx_export_path(op_ctx));
					return false;
				}
				break;

			default:
				LogInfo(COMPONENT_EXPORT,
					"Export %s does not support unknown RPCSEC_GSS_SVC %d",
					op_ctx_export_path(op_ctx),
					(int)svc);
				return false;
			}
		}
		break;
#endif
	default:
		LogInfo(COMPONENT_EXPORT,
			"Export %s does not support unknown oa_flavor %d",
			op_ctx_export_path(op_ctx),
			(int)req->rq_msg.cb_cred.oa_flavor);
		return false;
	}

	return true;
}

/**
 * @brief Get the best anonymous uid available.
 *
 * This is safe if there is no op_ctx or there is one but there is no
 * export_perms attached.
 *
 */

uid_t get_anonymous_uid(void)
{
	uid_t anon_uid;

	if (op_ctx != NULL &&
	    (op_ctx->export_perms.set & EXPORT_OPTION_ANON_UID_SET) != 0) {
		/* We have export_perms, use it. */
		return op_ctx->export_perms.anonymous_uid;
	}

	PTHREAD_RWLOCK_rdlock(&export_opt_lock);

	if ((export_opt.conf.set & EXPORT_OPTION_ANON_UID_SET) != 0) {
		/* Option was set in EXPORT_DEFAULTS */
		anon_uid = export_opt.conf.anonymous_uid;
	} else {
		/* Default to code default. */
		anon_uid = export_opt.def.anonymous_uid;
	}

	PTHREAD_RWLOCK_unlock(&export_opt_lock);

	return anon_uid;
}

/**
 * @brief Get the best anonymous gid available.
 *
 * This is safe if there is no op_ctx or there is one but there is no
 * export_perms attached.
 *
 */

gid_t get_anonymous_gid(void)
{
	/* Default to code default. */
	gid_t anon_gid = export_opt.def.anonymous_gid;

	if (op_ctx != NULL &&
	    (op_ctx->export_perms.set & EXPORT_OPTION_ANON_GID_SET) != 0) {
		/* We have export_perms, use it. */
		return op_ctx->export_perms.anonymous_gid;
	}

	PTHREAD_RWLOCK_rdlock(&export_opt_lock);

	if ((export_opt.conf.set & EXPORT_OPTION_ANON_GID_SET) != 0) {
		/* Option was set in EXPORT_DEFAULTS */
		anon_gid = export_opt.conf.anonymous_gid;
	} else {
		/* Default to code default. */
		anon_gid = export_opt.def.anonymous_gid;
	}

	PTHREAD_RWLOCK_unlock(&export_opt_lock);

	return anon_gid;
}

/**
 * @brief Checks if a machine is authorized to access an export entry
 *
 * Permissions in the op context get updated based on export and client.
 *
 * Takes the export->exp_lock in read mode to protect the client list and
 * export permissions while performing this work.
 */

void export_check_access(void)
{
	struct base_client_entry *client = NULL;
	struct exportlist_client_entry *expclient = NULL;
	char exp_str[PATH_MAX + 64];
	struct display_buffer dspbuf = {sizeof(exp_str), exp_str, exp_str};

	/* Initialize permissions to allow nothing, anonymous_uid and
	 * anonymous_gid will get set farther down.
	 */
	memset(&op_ctx->export_perms, 0, sizeof(op_ctx->export_perms));

	if (op_ctx->ctx_export != NULL) {
		/* Take lock */
		PTHREAD_RWLOCK_rdlock(&op_ctx->ctx_export->exp_lock);

		PTHREAD_RWLOCK_rdlock(&export_opt_lock);
	} else {
		/* Shortcut if no export */
		PTHREAD_RWLOCK_rdlock(&export_opt_lock);

		goto no_export;
	}

	if (isMidDebug(COMPONENT_EXPORT)) {
		display_printf(&dspbuf, " for export id %u path %s",
			       op_ctx->ctx_export->export_id,
			       op_ctx_export_path(op_ctx));
	} else {
		exp_str[0] = '\0';
	}

	if (glist_empty(&op_ctx->ctx_export->clients)) {
		/* No client list so use the export defaults client list to
		 * see if there's a match.
		 */
		client = client_match(COMPONENT_EXPORT, exp_str,
				      op_ctx->caller_addr,
				      &export_opt.clients);
	} else {
		/* Does the client match anyone on the client list? */
		client = client_match(COMPONENT_EXPORT, exp_str,
				      op_ctx->caller_addr,
				      &op_ctx->ctx_export->clients);
	}

	if (client != NULL) {
		/* Take client options */
		expclient = container_of(client,
					 struct exportlist_client_entry,
					 client_entry);

		op_ctx->export_perms.options = expclient->client_perms.options &
						 expclient->client_perms.set;

		if (expclient->client_perms.set & EXPORT_OPTION_ANON_UID_SET)
			op_ctx->export_perms.anonymous_uid =
					expclient->client_perms.anonymous_uid;

		if (expclient->client_perms.set & EXPORT_OPTION_ANON_GID_SET)
			op_ctx->export_perms.anonymous_gid =
					expclient->client_perms.anonymous_gid;

		op_ctx->export_perms.set = expclient->client_perms.set;
	}

	/* Any options not set by the client, take from the export */
	op_ctx->export_perms.options |=
				op_ctx->ctx_export->export_perms.options &
				op_ctx->ctx_export->export_perms.set &
				~op_ctx->export_perms.set;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_UID_SET) == 0 &&
	    (op_ctx->ctx_export->export_perms.set &
	     EXPORT_OPTION_ANON_UID_SET) != 0)
		op_ctx->export_perms.anonymous_uid =
			op_ctx->ctx_export->export_perms.anonymous_uid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_GID_SET) == 0 &&
	    (op_ctx->ctx_export->export_perms.set &
	     EXPORT_OPTION_ANON_GID_SET) != 0)
		op_ctx->export_perms.anonymous_gid =
			op_ctx->ctx_export->export_perms.anonymous_gid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0 &&
	    (op_ctx->ctx_export->export_perms.set &
	     EXPORT_OPTION_EXPIRE_SET) != 0)
		op_ctx->export_perms.expire_time_attr =
			op_ctx->ctx_export->export_perms.expire_time_attr;

	op_ctx->export_perms.set |= op_ctx->ctx_export->export_perms.set;

 no_export:

	/* Any options not set by the client or export, take from the
	 *  EXPORT_DEFAULTS block.
	 */
	op_ctx->export_perms.options |= export_opt.conf.options &
					  export_opt.conf.set &
					  ~op_ctx->export_perms.set;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_UID_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_ANON_UID_SET) != 0)
		op_ctx->export_perms.anonymous_uid =
					export_opt.conf.anonymous_uid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_GID_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_ANON_GID_SET) != 0)
		op_ctx->export_perms.anonymous_gid =
					export_opt.conf.anonymous_gid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0 &&
	    (export_opt.conf.set & EXPORT_OPTION_EXPIRE_SET) != 0)
		op_ctx->export_perms.expire_time_attr =
			export_opt.conf.expire_time_attr;

	op_ctx->export_perms.set |= export_opt.conf.set;

	/* And finally take any options not yet set from global defaults */
	op_ctx->export_perms.options |= export_opt.def.options &
					  ~op_ctx->export_perms.set;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_UID_SET) == 0)
		op_ctx->export_perms.anonymous_uid =
					export_opt.def.anonymous_uid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_ANON_GID_SET) == 0)
		op_ctx->export_perms.anonymous_gid =
					export_opt.def.anonymous_gid;

	if ((op_ctx->export_perms.set & EXPORT_OPTION_EXPIRE_SET) == 0)
		op_ctx->export_perms.expire_time_attr =
					export_opt.def.expire_time_attr;

	op_ctx->export_perms.set |= export_opt.def.set;

	if (isMidDebug(COMPONENT_EXPORT)) {
		char perms[1024] = "\0";
		struct display_buffer dspbuf = {sizeof(perms), perms, perms};

		if (expclient != NULL) {
			(void) StrExportOptions(&dspbuf,
						&expclient->client_perms);
			LogMidDebug(COMPONENT_EXPORT,
				    "CLIENT          (%s)",
				    perms);
			display_reset_buffer(&dspbuf);
		}

		if (op_ctx->ctx_export != NULL) {
			(void) StrExportOptions(
				&dspbuf, &op_ctx->ctx_export->export_perms);
			LogMidDebug(COMPONENT_EXPORT,
				    "EXPORT          (%s)",
				    perms);
			display_reset_buffer(&dspbuf);
		}

		(void) StrExportOptions(&dspbuf, &export_opt.conf);
		LogMidDebug(COMPONENT_EXPORT,
			    "EXPORT_DEFAULTS (%s)",
			    perms);
		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &export_opt.def);
		LogMidDebug(COMPONENT_EXPORT,
			    "default options (%s)",
			    perms);
		display_reset_buffer(&dspbuf);

		(void) StrExportOptions(&dspbuf, &op_ctx->export_perms);
		LogMidDebug(COMPONENT_EXPORT,
			    "Final options   (%s)",
			    perms);
	}

	PTHREAD_RWLOCK_unlock(&export_opt_lock);

	if (op_ctx->ctx_export != NULL) {
		/* Release lock */
		PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->exp_lock);
	}
}
