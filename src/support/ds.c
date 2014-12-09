/*
 * Copyright (C) CohortFS (2014)
 * contributor : William Allen Simpson <bill@CohortFS.com>
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
 * @file  ds.c
 * @brief Data Server parsing and management
 */
#include "config.h"
#include "config_parsing.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "pnfs_utils.h"

/**
 * @brief Free the DS block
 */

void fsal_pnfs_ds_free(struct fsal_pnfs_ds *pds)
{
	if (!pds->refcount)
		return;
	gsh_free(pds);
}

/**
 * @brief Commit a FSAL sub-block
 *
 * Use the Name parameter passed in via the self_struct to lookup the
 * fsal.  If the fsal is not loaded (yet), load it and call its init.
 *
 * Create the DS and pass the FSAL sub-block to it so that the
 * fsal method can process the rest of the parameters in the block
 */

static int fsal_commit(void *node, void *link_mem, void *self_struct,
		       struct config_error_type *err_type)
{
	struct fsal_args *fp = self_struct;
	struct fsal_module *fsal;
	struct fsal_pnfs_ds *pds;
	struct root_op_context root_op_context;
	nfsstat4 status;
	int errcnt;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL, 0, 0,
			     UNKNOWN_REQUEST);

	errcnt = fsal_load_init(node, fp->name, &fsal, err_type);
	if (errcnt > 0)
		goto err;

	status = fsal->m_ops.fsal_pnfs_ds(fsal, node, &pds);

	if (status != 0) {
		fsal_put(fsal);
		LogCrit(COMPONENT_CONFIG,
			"Could not create pNFS DS");
		err_type->init = true;
		errcnt++;
	}

err:
	release_root_op_context();
	return errcnt;
}

/**
 * @brief DS block handlers
 */

/**
 * @brief Initialize the DS block
 */

static void *pds_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL) {
		return gsh_calloc(sizeof(struct fsal_pnfs_ds), 1);
	} else { /* free resources case */
		fsal_pnfs_ds_free(self_struct);
		return NULL;
	}
}

/**
 * @brief Commit the DS block
 *
 * Validate the DS level parameters?  fsal and client
 * parameters are already done.
 */

static int pds_commit(void *node, void *link_mem, void *self_struct,
		      struct config_error_type *err_type)
{
	struct fsal_pnfs_ds *pds = self_struct;
	struct fsal_module *fsal = pds->fsal;

	LogEvent(COMPONENT_CONFIG,
		 "DS %d created at FSAL (%s) with path (%s)",
		 pds->pds_number, fsal->name, fsal->path);
	return 0;
}

/**
 * @brief Display the DS block
 */

static void pds_display(const char *step, void *node,
		       void *link_mem, void *self_struct)
{
	struct fsal_pnfs_ds *pds = self_struct;
	struct fsal_module *fsal = pds->fsal;

	LogMidDebug(COMPONENT_CONFIG,
		    "%s %p DS %d FSAL (%s) with path (%s)",
		    step, pds, pds->pds_number, fsal->name, fsal->path);
}

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
 * @brief Table of DS block parameters
 *
 * NOTE: the Client and FSAL sub-blocks must be the *last*
 * two entries in the list.  This is so all other
 * parameters have been processed before these sub-blocks
 * are processed.
 */

static struct config_item pds_items[] = {
	CONF_ITEM_UI32("Number", 0, INT32_MAX, 0,
		       fsal_pnfs_ds, pds_number),
	CONF_RELAX_BLOCK("FSAL", fsal_params,
			 fsal_init, fsal_commit,
			 fsal_pnfs_ds, servers), /* ??? placeholder */
	CONFIG_EOL
};

/**
 * @brief Top level definition for each DS block
 */

static struct config_block pds_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.ds.%d",
	.blk_desc.name = "DS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = pds_init,
	.blk_desc.u.blk.params = pds_items,
	.blk_desc.u.blk.commit = pds_commit,
	.blk_desc.u.blk.display = pds_display
};

/**
 * @brief Read the DS entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the DS list
 *
 * @return A negative value on error;
 *         otherwise, the number of DS entries.
 */

int ReadDataServers(config_file_t in_config)
{
	struct config_error_type err_type;
	int rc;

	rc = load_config_from_parse(in_config,
				    &pds_block,
				    NULL,
				    false,
				    &err_type);
	if (!config_error_is_harmless(&err_type))
		return -1;

	return rc;
}
