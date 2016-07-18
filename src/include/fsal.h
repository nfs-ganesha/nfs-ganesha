/*
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal.h
 * @brief Main FSAL externs and functions
 * @note  not called by other header files.
 */

#ifndef FSAL_H
#define FSAL_H

#include "fsal_api.h"

/**
 * @brief If we don't know how big a buffer we want for a link, use
 * this value.
 */

static const size_t fsal_default_linksize = 4096;

/**
 * @brief Pointer to FSAL module by number.
 * This is actually defined in common_pnfs.c
 */
extern struct fsal_module *pnfs_fsal[];

/**
 * @brief Delegations types list for the Delegations parameter in FSAL.
 * This is actually defined in exports.c
 */
extern struct config_item_list deleg_types[];

/**
 * @brief Thread Local Storage (TLS).
 *
 * TLS variables look like globals but since they are global only in the
 * context of a single thread, they do not require locks.  This is true
 * of all thread either within or separate from a/the fridge.
 *
 * All thread local storage is declared extern here.  The actual
 * storage declaration is in fridgethr.c.
 */

/**
 * @brief Operation context (op_ctx).
 *
 * This carries everything relevant to a protocol operation.
 * Space for the struct itself is allocated elsewhere.
 * Test/assert opctx != NULL first (or let the SEGV kill you)
 */

extern __thread struct req_op_context *op_ctx;

/* Export permissions for root op context, defined in protocol layer */
extern uint32_t root_op_export_options;
extern uint32_t root_op_export_set;

/**
 * @brief node id used to construct recovery directory in
 * cluster implementation.
 */
extern int g_nodeid;

/**
 * @brief Ops context for asynch and not protocol tasks that need to use
 * subsystems that depend on op_ctx.
 */

struct root_op_context {
	struct req_op_context req_ctx;
	struct req_op_context *old_op_ctx;
	struct user_cred creds;
	struct export_perms export_perms;
};

static inline void init_root_op_context(struct root_op_context *ctx,
					struct gsh_export *exp,
					struct fsal_export *fsal_exp,
					uint32_t nfs_vers,
					uint32_t nfs_minorvers,
					uint32_t req_type)
{
	/* Initialize req_ctx.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	memset(ctx, 0, sizeof(*ctx));
	ctx->req_ctx.creds = &ctx->creds;
	ctx->req_ctx.nfs_vers = nfs_vers;
	ctx->req_ctx.nfs_minorvers = nfs_minorvers;
	ctx->req_ctx.req_type = req_type;

	ctx->req_ctx.export = exp;
	ctx->req_ctx.fsal_export = fsal_exp;
	if (fsal_exp)
		ctx->req_ctx.fsal_module = fsal_exp->fsal;
	else if (op_ctx)
		ctx->req_ctx.fsal_module = op_ctx->fsal_module;

	ctx->req_ctx.export_perms = &ctx->export_perms;
	ctx->export_perms.set = root_op_export_set;
	ctx->export_perms.options = root_op_export_options;

	ctx->old_op_ctx = op_ctx;
	op_ctx = &ctx->req_ctx;
}

static inline void release_root_op_context(void)
{
	struct root_op_context *ctx;

	ctx = container_of(op_ctx, struct root_op_context, req_ctx);
	op_ctx = ctx->old_op_ctx;
}

/**
 * @brief init_complete used to indicate if ganesha is during
 * startup or not
 */
extern bool init_complete;

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

#include "FSAL/access_check.h"	/* rethink where this should go */

/**
 * Global fsal manager functions
 * used by nfs_main to initialize fsal modules.
 */

/* Called only within MODULE_INIT and MODULE_FINI functions of a fsal
 * module
 */

/**
 * @brief Register a FSAL
 *
 * This function registers an FSAL with ganesha and initializes the
 * public portion of the FSAL data structure, including providing
 * default operation vectors.
 *
 * @param[in,out] fsal_hdl      The FSAL module to register.
 * @param[in]     name          The FSAL's name
 * @param[in]     major_version Major version fo the API against which
 *                              the FSAL was written
 * @param[in]     minor_version Minor version of the API against which
 *                              the FSAL was written.
 *
 * @return 0 on success.
 * @return EINVAL on version mismatch.
 */

int register_fsal(struct fsal_module *fsal_hdl, const char *name,
		  uint32_t major_version, uint32_t minor_version,
		  uint8_t fsal_id);
/**
 * @brief Unregister an FSAL
 *
 * This function unregisters an FSAL from Ganesha.  It should be
 * called from the module finalizer as part of unloading.
 *
 * @param[in] fsal_hdl The FSAL to unregister
 *
 * @return 0 on success.
 * @return EBUSY if outstanding references or exports exist.
 */

int unregister_fsal(struct fsal_module *fsal_hdl);

/**
 * @brief Find and take a reference on an FSAL
 *
 * This function finds an FSAL by name and increments its reference
 * count.  It is used as part of export setup.  The @c put method
 * should be used  to release the reference before unloading.
 */
struct fsal_module *lookup_fsal(const char *name);

int load_fsal(const char *name,
	      struct fsal_module **fsal_hdl);

int fsal_load_init(void *node, const char *name,
		   struct fsal_module **fsal_hdl_p,
		   struct config_error_type *err_type);

struct fsal_args {
	char *name;
};

void *fsal_init(void *link_mem, void *self_struct);

struct subfsal_args {
	char *name;
	void *fsal_node;
};

int subfsal_commit(void *node, void *link_mem, void *self_struct,
		   struct config_error_type *err_type);

void destroy_fsals(void);
void emergency_cleanup_fsals(void);
void start_fsals(void);

void display_fsinfo(struct fsal_staticfsinfo_t *info);

const char *msg_fsal_err(fsal_errors_t fsal_err);

#endif				/* !FSAL_H */
/** @} */
