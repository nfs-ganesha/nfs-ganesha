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
#include "FSAL/fsal_commonlib.h"
#include "pnfs_utils.h"

/**
 * @brief Servers are stored in an AVL tree with front-end cache.
 *
 * @note  number of cache slots should be prime.
 */
#define SERVER_BY_ID_CACHE_SIZE 193

struct server_by_id {
	pthread_rwlock_t lock;
	struct avltree t;
	struct avltree_node *cache[SERVER_BY_ID_CACHE_SIZE];
};

static struct server_by_id server_by_id;

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slots (which should be prime).
 *
 * @param k [in] Entry index value
 *
 * @return The computed offset.
 */
static inline uint16_t id_cache_offsetof(uint16_t k)
{
	return k % SERVER_BY_ID_CACHE_SIZE;
}

/**
 * @brief Server id comparator for AVL tree walk
 *
 */
static int server_id_cmpf(const struct avltree_node *lhs,
			  const struct avltree_node *rhs)
{
	struct fsal_pnfs_ds *lk, *rk;

	lk = avltree_container_of(lhs, struct fsal_pnfs_ds, ds_node);
	rk = avltree_container_of(rhs, struct fsal_pnfs_ds, ds_node);
	if (lk->id_servers != rk->id_servers)
		return (lk->id_servers < rk->id_servers) ? -1 : 1;
	else
		return 0;
}

/**
 * @brief Allocate the pDS entry.
 *
 * @return pointer to fsal_pnfs_ds.
 * NULL on allocation errors.
 */

struct fsal_pnfs_ds *pnfs_ds_alloc(void)
{
	return gsh_calloc(sizeof(struct fsal_pnfs_ds), 1);
}

/**
 * @brief Free the pDS entry.
 */

void pnfs_ds_free(struct fsal_pnfs_ds *pds)
{
	if (!pds->refcount)
		return;

	gsh_free(pds);
}

/**
 * @brief Insert the pDS entry into the AVL tree.
 *
 * @param exp [IN] the server entry
 *
 * @return false on failure.
 */

bool pnfs_ds_insert(struct fsal_pnfs_ds *pds)
{
	struct avltree_node *node;
	void **cache_slot = (void **)
		&(server_by_id.cache[id_cache_offsetof(pds->id_servers)]);

	/* we will hold a ref starting out... */
	assert(pds->refcount == 1);

	PTHREAD_RWLOCK_wrlock(&server_by_id.lock);
	node = avltree_insert(&pds->ds_node, &server_by_id.t);
	if (node) {
		/* somebody beat us to it */
		PTHREAD_RWLOCK_unlock(&server_by_id.lock);
		return false;
	}

	/* update cache */
	atomic_store_voidptr(cache_slot, &pds->ds_node);

	pnfs_ds_get_ref(pds);		/* == 2 */
	if (pds->mds_export != NULL) {
		/* also bump related export for duration */
		get_gsh_export_ref(pds->mds_export);
		pds->mds_export->has_pnfs_ds = true;
	}

	PTHREAD_RWLOCK_unlock(&server_by_id.lock);
	return true;
}

/**
 * @brief Lookup the fsal_pnfs_ds struct for this server id
 *
 * Lookup the fsal_pnfs_ds struct by id_servers.
 * Server ids are assigned by the config file and carried about
 * by file handles.
 *
 * @param id_servers   [IN] the server id extracted from the handle
 *
 * @return pointer to ref locked server
 */
struct fsal_pnfs_ds *pnfs_ds_get(uint16_t id_servers)
{
	struct fsal_pnfs_ds v;
	struct avltree_node *node;
	struct fsal_pnfs_ds *pds;
	void **cache_slot = (void **)
		&(server_by_id.cache[id_cache_offsetof(id_servers)]);

	v.id_servers = id_servers;
	PTHREAD_RWLOCK_rdlock(&server_by_id.lock);

	/* check cache */
	node = (struct avltree_node *)atomic_fetch_voidptr(cache_slot);
	if (node) {
		pds = avltree_container_of(node, struct fsal_pnfs_ds, ds_node);
		if (pds->id_servers == id_servers) {
			/* got it in 1 */
			LogDebug(COMPONENT_HASHTABLE_CACHE,
				 "server_by_id cache hit slot %d",
				 id_cache_offsetof(id_servers));
			goto out;
		}
	}

	/* fall back to AVL */
	node = avltree_lookup(&v.ds_node, &server_by_id.t);
	if (node) {
		pds = avltree_container_of(node, struct fsal_pnfs_ds, ds_node);
		/* update cache */
		atomic_store_voidptr(cache_slot, node);
	} else {
		PTHREAD_RWLOCK_unlock(&server_by_id.lock);
		return NULL;
	}

 out:
	pnfs_ds_get_ref(pds);
	if (pds->mds_export != NULL)
		/* also bump related export for duration */
		get_gsh_export_ref(pds->mds_export);

	PTHREAD_RWLOCK_unlock(&server_by_id.lock);
	return pds;
}

/**
 * @brief Release the fsal_pnfs_ds struct
 *
 * @param exp [IN] the server entry
 */

void pnfs_ds_put(struct fsal_pnfs_ds *pds)
{
	int32_t refcount = atomic_dec_int32_t(&pds->refcount);

	if (refcount != 0) {
		assert(refcount > 0);
		return;
	}

	/* free resources */
	fsal_pnfs_ds_fini(pds);
	gsh_free(pds);
}

/**
 * @brief Remove the pDS entry from the AVL tree.
 *
 * @param id_servers   [IN] the server id extracted from the handle
 * @param final        [IN] Also drop from FSAL.
 */

void pnfs_ds_remove(uint16_t id_servers, bool final)
{
	struct fsal_pnfs_ds v;
	struct avltree_node *node;
	struct fsal_pnfs_ds *pds = NULL;
	void **cache_slot = (void **)
		&(server_by_id.cache[id_cache_offsetof(id_servers)]);

	v.id_servers = id_servers;
	PTHREAD_RWLOCK_wrlock(&server_by_id.lock);

	node = avltree_lookup(&v.ds_node, &server_by_id.t);
	if (node) {
		struct avltree_node *cnode = (struct avltree_node *)
			 atomic_fetch_voidptr(cache_slot);

		/* Remove from the AVL cache and tree */
		if (node == cnode)
			atomic_store_voidptr(cache_slot, NULL);
		avltree_remove(node, &server_by_id.t);

		pds = avltree_container_of(node, struct fsal_pnfs_ds, ds_node);

		/* Eliminate repeated locks during draining. Idempotent. */
		pds->pnfs_ds_status = PNFS_DS_STALE;
	}

	PTHREAD_RWLOCK_unlock(&server_by_id.lock);

	/* removal has a once-only semantic */
	if (pds != NULL) {
		if (pds->mds_export != NULL)
			/* special case: avoid lookup of related export.
			 * get_gsh_export_ref() was bumped in pnfs_ds_insert()
			 *
			 * once-only, so no need for lock here.
			 * do not pre-clear related export (mds_export).
			 * always check pnfs_ds_status instead.
			 */
			put_gsh_export(pds->mds_export);

		/* Release table reference to the server.
		 * Release of resources will occur on last reference.
		 * Which may or may not be from this call.
		 */
		pnfs_ds_put(pds);

		if (final) {
			/* Also drop from FSAL.  Instead of pDS thread,
			 * relying on export cleanup thread.
			 */
			pnfs_ds_put(pds);
		}
	}
}

/**
 * @brief Commit a FSAL sub-block
 *
 * Use the Name parameter passed in via the self_struct to lookup the
 * fsal.  If the fsal is not loaded (yet), load it and call its init.
 *
 * Create the pDS and pass the FSAL sub-block to it so that the
 * fsal method can process the rest of the parameters in the block
 */

static int fsal_commit(void *node, void *link_mem, void *self_struct,
		       struct config_error_type *err_type)
{
	struct fsal_args *fp = self_struct;
	struct fsal_module **pds_fsal = link_mem;
	struct fsal_pnfs_ds *pds =
		container_of(pds_fsal, struct fsal_pnfs_ds, fsal);
	struct fsal_module *fsal;
	struct root_op_context root_op_context;
	fsal_status_t status;
	int errcnt;

	/* Initialize req_ctx */
	init_root_op_context(&root_op_context, NULL, NULL, 0, 0,
			     UNKNOWN_REQUEST);

	errcnt = fsal_load_init(node, fp->name, &fsal, err_type);
	if (errcnt > 0)
		goto err;

	status = fsal->m_ops.fsal_pnfs_ds(fsal, node, &pds);
	if (status.major != ERR_FSAL_NO_ERROR) {
		fsal_put(fsal);
		LogCrit(COMPONENT_CONFIG,
			"Could not create pNFS DS");
		err_type->init = true;
		errcnt++;
	}

	LogEvent(COMPONENT_CONFIG,
		 "DS %d fsal_commit at FSAL (%s) with path (%s)",
		 pds->id_servers, pds->fsal->name, pds->fsal->path);

err:
	release_root_op_context();
	return errcnt;
}

/**
 * @brief pNFS DS block handlers
 */

/**
 * @brief Initialize the DS block
 */

static void *pds_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL) {
		return pnfs_ds_alloc();
	} else { /* free resources case */
		pnfs_ds_free(self_struct);
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
	struct fsal_pnfs_ds *probe = pnfs_ds_get(pds->id_servers);

	/* redundant probe before insert??? */
	if (probe != NULL) {
		LogDebug(COMPONENT_CONFIG,
			 "Server %d already exists!",
			 pds->id_servers);
		pnfs_ds_put(probe);
		err_type->exists = true;
		return 1;
	}

	if (!pnfs_ds_insert(pds)) {
		LogCrit(COMPONENT_CONFIG,
			"Server id %d already in use.",
			pds->id_servers);
		err_type->exists = true;
		return 1;
	}

	LogEvent(COMPONENT_CONFIG,
		 "DS %d created at FSAL (%s) with path (%s)",
		 pds->id_servers, pds->fsal->name, pds->fsal->path);
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
		    step, pds, pds->id_servers, fsal->name, fsal->path);
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
	CONF_ITEM_UI16("Number", 0, UINT16_MAX, 0,
		       fsal_pnfs_ds, id_servers),
	CONF_RELAX_BLOCK("FSAL", fsal_params,
			 fsal_init, fsal_commit,
			 fsal_pnfs_ds, fsal),
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
 * @brief Read the DS blocks from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the DS list
 *
 * @return A negative value on error;
 *         otherwise, the number of DS blocks.
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

/**
 * @brief Initialize server tree
 */

void server_pkginit(void)
{
	pthread_rwlockattr_t rwlock_attr;

	assert(pthread_rwlockattr_init(&rwlock_attr) == 0);
#ifdef GLIBC
	assert(pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP) == 0);
#endif
	assert(pthread_rwlock_init(&server_by_id.lock, &rwlock_attr) == 0);
	avltree_init(&server_by_id.t, server_id_cmpf, 0);
	memset(&server_by_id.cache, 0, sizeof(server_by_id.cache));
}
