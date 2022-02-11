// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2019 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/**
 * @addtogroup FSAL_MDCACHE
 * @{
 */

/**
 * @file  main.c
 * @brief FSAL export functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "gsh_list.h"
#include "config_parsing.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "mdcache_lru.h"
#include "mdcache_hash.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "gsh_config.h"

/*
 * helpers to/from other NULL objects
 */


/*
 * export object methods
 */

/**
 * @brief Return the name of the sub-FSAL
 *
 * For MDCACHE, we append "/MDC" onto the name.
 *
 * @param[in] exp_hdl	Our export handle
 * @return Name of sub-FSAL
 */
static const char *mdcache_get_name(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_cur_export();

	return exp->name;
}

/**
 * @brief Un-export an MDCACHE export
 *
 * Clean up all the cache entries on this export.
 *
 * @param[in] exp_hdl	Export to unexport
 * @param[in] root_obj	Root object for export
 */
static void mdcache_unexport(struct fsal_export *exp_hdl,
			     struct fsal_obj_handle *root_obj)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	mdcache_entry_t *root_entry = container_of(root_obj, mdcache_entry_t,
						   obj_handle);
	mdcache_entry_t *entry;
	struct entry_export_map *expmap;
	fsal_status_t status;

	/* Indicate this export is going away so we don't create any new
	 * export map entries.
	 */
	atomic_set_uint8_t_bits(&exp->flags, MDC_UNEXPORT);

	/* Next, clean up our cache entries on the export */
	while (true) {
		PTHREAD_RWLOCK_rdlock(&exp->mdc_exp_lock);
		expmap = glist_first_entry(&exp->entry_list,
					   struct entry_export_map,
					   entry_per_export);

		if (unlikely(expmap == NULL)) {
			PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);
			break;
		}

		entry = expmap->entry;

		if (entry == root_entry) {
			LogDebug(COMPONENT_EXPORT,
				 "About to unmap root entry %p and possibly free it for export %d path %s pseudo %s",
				 root_entry, op_ctx->ctx_export->export_id,
				 CTX_FULLPATH(op_ctx), CTX_PSEUDOPATH(op_ctx));
		} else {
			LogDebug(COMPONENT_EXPORT,
				 "About to unmap entry %p and possibly free it",
				 entry);
		}

		/* Get a ref across cleanup.  This must be an initial ref, so
		 * that it takes the LRU lane lock, keeping it from racing with
		 * lru_lane_run() */
		status = mdcache_lru_ref(entry, LRU_REQ_INITIAL);
		PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);

		if (FSAL_IS_ERROR(status)) {
			/* Entry was stale; skip it */
			LogFullDebug(COMPONENT_EXPORT,
				     "Error %s on entry %p",
				     msg_fsal_err(status.major), entry);
			continue;
		}

		/* Must get attr_lock before mdc_exp_lock */
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
		PTHREAD_RWLOCK_wrlock(&exp->mdc_exp_lock);

		mdc_remove_export_map(expmap);

		expmap = glist_first_entry(&entry->export_list,
					   struct entry_export_map,
					   export_per_entry);
		if (expmap == NULL) {
			/* Entry is unmapped, clear first_export_id.  This is to
			 * close a race caused by lru_run_lane() taking a ref
			 * before we call mdcache_lru_cleanup_try_push() below.
			 * */
			atomic_store_int32_t(&entry->first_export_id, -1);

			/* We must not hold entry->attr_lock across
			 * try_cleanup_push (LRU lane lock order) */
			PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);
			LogFullDebug(COMPONENT_EXPORT,
				     "Disposing of entry %p",
				     entry);

			/* There are no exports referencing this entry, attempt
			 * to push it to cleanup queue. Note that if the export
			 * root is in fact only used by one export, it will
			 * be unhashed here.
			 */
			mdcache_lru_cleanup_try_push(entry);
		} else {
			/* Make sure first export pointer is still valid */
			atomic_store_int32_t(
				&entry->first_export_id,
				(int32_t) expmap->exp->mfe_exp.export_id);

			PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);

			LogFullDebug(COMPONENT_EXPORT,
				     "entry %p is still exported by export id %d",
				     entry, expmap->exp->mfe_exp.export_id);
		}

		/* Release above ref */
		mdcache_put(entry);
	};

	/* Last unexport for the sub-FSAL */
	subcall_raw(exp,
		sub_export->exp_ops.unexport(sub_export, root_entry->sub_handle)
	);

	/* NOTE: we do NOT need to unhash the root entry, it was unhashed above
	 *       (if it was not used by another export) in the loop since it is
	 *       an entry that belongs to the export.
	 */
}

/**
 * @brief Handle the unmounting of an export.
 *
 * This function is called when the export is unmounted.  The FSAL may need
 * to clean up references to the root_obj and junction_obj and connections
 * between them.
 *
 * Specifically, mdcache must remove the export mapping and schedule for
 * cleanup the junction node (which may be the same node as the unmounted
 * export's root node).
 *
 * @param[in] parent_exp_hdl	The parent export of the mount.
 * @param[in] junction_obj	The junction object the export was mounted on
 */
static void mdcache_unmount(struct fsal_export *parent_exp_hdl,
			    struct fsal_obj_handle *junction_obj)
{
	struct mdcache_fsal_export *exp = mdc_export(parent_exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	mdcache_entry_t *entry = container_of(junction_obj, mdcache_entry_t,
					      obj_handle);
	struct glist_head *glist;
	struct entry_export_map *expmap = NULL;

	/* Take locks to perform unmap. Must get attr_lock before mdc_exp_lock
	 */
	PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
	PTHREAD_RWLOCK_wrlock(&exp->mdc_exp_lock);

	glist_for_each(glist, &entry->export_list) {
		expmap = glist_entry(glist,
				     struct entry_export_map,
				     export_per_entry);

		if (expmap->exp == exp) {
			/* Found it. */
			break;
		}

		/* Not this one... */
		expmap = NULL;
	}

	if (expmap == NULL) {
		LogFatal(COMPONENT_EXPORT,
			 "export map not found for export %p",
			 parent_exp_hdl);
	}

	/* Next, clean up junction cache entry on the export */
	LogDebug(COMPONENT_EXPORT,
		 "About to unmap junction entry %p and possibly free it",
		 entry);

	/* Now remove the export map */
	mdc_remove_export_map(expmap);

	/* And look at the export map for the junction entry now */
	expmap = glist_first_entry(&entry->export_list,
				   struct entry_export_map,
				   export_per_entry);

	if (expmap == NULL) {
		/* Entry is unmapped, clear first_export_id.  This is to
		 * close a race caused by lru_run_lane() taking a ref
		 * before we call mdcache_lru_cleanup_try_push() below.
		 * */
		atomic_store_int32_t(&entry->first_export_id, -1);

		/* We must not hold entry->attr_lock across
		 * try_cleanup_push (LRU lane lock order) */
		PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		LogFullDebug(COMPONENT_EXPORT,
			     "Disposing of entry %p",
			     entry);

		/* There are no exports referencing this entry, attempt
		 * to push it to cleanup queue. Note that if the export
		 * root is in fact only used by one export, it will
		 * be unhashed here.
		 */
		mdcache_lru_cleanup_try_push(entry);
	} else {
		/* Make sure first export pointer is still valid */
		atomic_store_int32_t(
			&entry->first_export_id,
			(int32_t) expmap->exp->mfe_exp.export_id);

		PTHREAD_RWLOCK_unlock(&exp->mdc_exp_lock);
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);

		LogFullDebug(COMPONENT_EXPORT,
			     "entry %p is still exported by export id %d",
			     entry, expmap->exp->mfe_exp.export_id);
	}

	/* Last unmount for the sub-FSAL */
	subcall_raw(exp,
		sub_export->exp_ops.unmount(sub_export, entry->sub_handle)
	);
}

/**
 * @brief Release an MDCACHE export
 *
 * @param[in] exp_hdl	Export to release
 */
static void mdcache_exp_release(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	struct fsal_module *fsal_hdl;

	fsal_hdl = sub_export->fsal;

	LogInfo(COMPONENT_FSAL, "Releasing %s export %" PRIu16 " for %s",
		fsal_hdl->name, op_ctx->ctx_export->export_id,
		ctx_export_path(op_ctx));

	/* Stop the dirmap thread */
	dirmap_lru_stop(exp);

	/* Release the sub_export */
	subcall_shutdown_raw(exp,
		sub_export->exp_ops.release(sub_export)
	);

	fsal_put(fsal_hdl);

	LogFullDebug(COMPONENT_FSAL,
		     "FSAL %s refcount %"PRIu32,
		     fsal_hdl->name,
		     atomic_fetch_int32_t(&fsal_hdl->refcount));

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(exp->name);

	gsh_free(exp);	/* elvis has left the building */
}

/**
 * @brief Get FS information
 *
 * Pass through to underlying FSAL.
 *
 * Note dang: Should this gather info about MDCACHE?
 *
 * @param[in] exp_hdl	Export to operate on
 * @param[in] obj_hdl	Object to operate on
 * @param[out] infop	Output information on success
 * @return FSAL status
 */
static fsal_status_t mdcache_get_dynamic_info(struct fsal_export *exp_hdl,
					      struct fsal_obj_handle *obj_hdl,
					      fsal_dynamicfsinfo_t *infop)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.get_fs_dynamic_info(
			sub_export, entry->sub_handle, infop)
	       );

	return status;
}

/**
 * @brief See if a feature is supported
 *
 * For the moment, MDCACHE supports no features, so just pass through to the
 * base FSAL.
 *
 * @param[in] exp_hdl	Export to check
 * @param[in] option	Option to check for support
 * @return true if supported, false otherwise
 */
static bool mdcache_fs_supports(struct fsal_export *exp_hdl,
				fsal_fsinfo_options_t option)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	bool result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_supports(sub_export, option)
	       );

	return result;
}

/**
 * @brief Find the maximum supported file size
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max size in bytes
 */
static uint64_t mdcache_fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint64_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxfilesize(sub_export)
	       );

	return result;
}

/**
 * @brief Get the maximum supported read size
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max size in bytes
 */
static uint32_t mdcache_fs_maxread(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxread(sub_export)
	       );

	return result;
}

/**
 * @brief Get the maximum supported write size
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max size in bytes
 */
static uint32_t mdcache_fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxwrite(sub_export)
	       );

	return result;
}

/**
 * @brief Get the maximum supported link count
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max number of links to a file
 */
static uint32_t mdcache_fs_maxlink(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxlink(sub_export)
	       );

	return result;
}

/**
 * @brief Get the maximum supported name length
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max length of name in bytes
 */
static uint32_t mdcache_fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxnamelen(sub_export)
	       );

	return result;
}

/**
 * @brief Get the maximum supported name length
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return Max length of name in bytes
 */
static uint32_t mdcache_fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_maxpathlen(sub_export)
	       );

	return result;
}

/**
 * @brief Get the NFSv4 ACLSUPPORT attribute
 *
 * MDCACHE does not provide or restrict ACLs
 *
 * @param[in] exp_hdl	Export to query
 * @return ACLSUPPORT
 */
static fsal_aclsupp_t mdcache_fs_acl_support(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_aclsupp_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_acl_support(sub_export)
	       );

	return result;
}

/**
 * @brief Get the list of supported attributes
 *
 * MDCACHE does not provide or restrict attributes
 *
 * @param[in] exp_hdl	Export to query
 * @return Mask of supported attributes
 */
static attrmask_t mdcache_fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	attrmask_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_supported_attrs(sub_export)
	       );

	return result;
}

/**
 * @brief Get the configured umask on the export
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @return umask value
 */
static uint32_t mdcache_fs_umask(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_umask(sub_export)
	       );

	return result;
}

/**
 * @brief Get the configured expiration time for parent handle
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl   Export to query
 * @return Expiry time for parent handle
 */
static int32_t mdcache_fs_expiretimeparent(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	int32_t result;

	subcall_raw(exp,
		result = sub_export->exp_ops.fs_expiretimeparent(sub_export)
	);

	return result;
}

/**
 * @brief Check quota on a file
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @param[in] filepath	Path to file to query
 * @param[in] quota_type	Type of quota (user or group)
 * @return FSAL status
 */
static fsal_status_t mdcache_check_quota(struct fsal_export *exp_hdl,
					 const char *filepath, int quota_type)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.check_quota(sub_export, filepath,
							 quota_type)
	       );

	return status;
}

/**
 * @brief Get quota information for a file
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @param[in] filepath	Path to file to query
 * @param[in] quota_type	Type of quota (user or group)
 * @param[in] quota_id  Id for getting quota information
 * @param[out] pquota	Resulting quota information
 * @return FSAL status
 */
static fsal_status_t mdcache_get_quota(struct fsal_export *exp_hdl,
				       const char *filepath, int quota_type,
				       int quota_id,
				       fsal_quota_t *pquota)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.get_quota(sub_export, filepath,
						       quota_type, quota_id,
						       pquota));

	return status;
}

/**
 * @brief Set a quota for a file
 *
 * MDCACHE only caches metadata, so it imposes no restrictions itself.
 *
 * @param[in] exp_hdl	Export to query
 * @param[in] filepath	Path to file to query
 * @param[in] quota_type	Type of quota (user or group)
 * @param[in] quota_id  Id for which quota is set
 * @param[in] pquota	Quota information to set
 * @param[out] presquota	Quota after set
 * @return FSAL status
 */
static fsal_status_t mdcache_set_quota(struct fsal_export *exp_hdl,
				       const char *filepath, int quota_type,
				       int quota_id,
				       fsal_quota_t *pquota,
				       fsal_quota_t *presquota)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.set_quota(sub_export,
			filepath, quota_type, quota_id, pquota, presquota)
	       );

	return status;
}

/**
 * @brief List pNFS devices
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @param[in] type	Layout type for query
 * @param[in] cb	Callback for devices
 * @param[in] res	Devicelist result
 * @return NFSv4 Status
 */
static nfsstat4 mdcache_getdevicelist(struct fsal_export *exp_hdl,
					   layouttype4 type, void *opaque,
					   bool (*cb)(void *opaque,
						      const uint64_t id),
					   struct fsal_getdevicelist_res *res)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	nfsstat4 status;

	subcall_raw(exp,
		status = sub_export->exp_ops.getdevicelist(sub_export, type,
							   opaque, cb, res)
	       );

	return status;
}

/**
 * @brief List supported pNFS layout types
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @param[out] count	Number of types returned
 * @param[out] types	Layout types supported
 */
static void mdcache_fs_layouttypes(struct fsal_export *exp_hdl,
					    int32_t *count,
					    const layouttype4 **types)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;

	subcall_raw(exp,
		sub_export->exp_ops.fs_layouttypes(sub_export, count, types)
	       );
}

/**
 * @brief Get pNFS layout block size
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @return Number of bytes in block
 */
static uint32_t mdcache_fs_layout_blocksize(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.fs_layout_blocksize(sub_export)
	       );

	return status;
}

/**
 * @brief Get pNFS maximum number of segments
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @return Number of segments
 */
static uint32_t mdcache_fs_maximum_segments(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	uint32_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.fs_maximum_segments(sub_export)
	       );

	return status;
}

/**
 * @brief Get size of pNFS loc_body
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @return Size of loc_body
 */
static size_t mdcache_fs_loc_body_size(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	size_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.fs_loc_body_size(sub_export)
	       );

	return status;
}

/**
 * @brief Get write verifier
 *
 * MDCACHE only caches metadata, pass it through
 *
 * @param[in] exp_hdl	Export to query
 * @param[in,out] verf_desc Address and length of verifier
 */
static void mdcache_get_write_verifier(struct fsal_export *exp_hdl,
				       struct gsh_buffdesc *verf_desc)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;

	subcall_raw(exp,
		sub_export->exp_ops.get_write_verifier(sub_export, verf_desc)
	       );
}

/**
 * @brief Decode the wire handle into something the FSAL can understand
 *
 * Wire formats are delegated to the underlying FSAL.
 *
 * @param[in] exp_hdl	Export to operate on
 * @param[in] in_type	Type of handle to extract
 * @param[in,out] fh_desc	Source/dest for extracted digest
 * @param[in] flags	Related flages (currently endian)
 * @return FSAL status
 */
static fsal_status_t mdcache_wire_to_host(struct fsal_export *exp_hdl,
					  fsal_digesttype_t in_type,
					  struct gsh_buffdesc *fh_desc,
					  int flags)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.wire_to_host(sub_export, in_type,
							  fh_desc, flags)
	       );

	return status;
}

/**
 * @brief Produce handle-key from host-handle
 *
 * delegated to the underlying FSAL.
 *
 * @param[in] exp_hdl	Export to operate on
 * @param[in,out] fh_desc	Source/dest for extracted digest
 * @return FSAL status
 */
static fsal_status_t mdcache_host_to_key(struct fsal_export *exp_hdl,
					    struct gsh_buffdesc *fh_desc)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	fsal_status_t status;

	subcall_raw(exp,
		status = sub_export->exp_ops.host_to_key(sub_export, fh_desc)
	       );

	return status;
}

/**
 * @brief Allocate state_t structure
 *
 * @param[in] exp_hdl	Export to operate on
 * @param[in] state_type Type of state to allocate
 * @param[in] related_state Related state if appropriate
 * @return New state structure
 */
static struct state_t *mdcache_alloc_state(struct fsal_export *exp_hdl,
					   enum state_type state_type,
					   struct state_t *related_state)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	struct state_t *state;

	subcall_raw(exp,
		state = sub_export->exp_ops.alloc_state(sub_export, state_type,
							related_state)
	       );

	/* Replace stored export with ours so stacking works */
	state->state_exp = exp_hdl;

	return state;
}

/**
 * @brief Free state_t structure
 *
 * @param[in] exp_hdl	Export state is associated with
 * @param[in] state	State to free
 */
static void mdcache_free_state(struct fsal_export *exp_hdl,
			       struct state_t *state)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;

	subcall_raw(exp,
		sub_export->exp_ops.free_state(sub_export, state)
	       );
}

/**
 * @brief Check to see if a user is superuser
 *
 * @param[in] exp_hdl               Export state_t is associated with
 * @param[in] creds                 Credentials to check for superuser
 *
 * @returns NULL on failure otherwise a state structure.
 */

static bool mdcache_is_superuser(struct fsal_export *exp_hdl,
				 const struct user_cred *creds)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;
	bool status;

	subcall_raw(exp,
		status = sub_export->exp_ops.is_superuser(sub_export, creds)
	       );

	return status;
}

/**
 * @brief Prepare an export to be unexported
 *
 * @param[in] exp_hdl               Export state_t is associated with
 *
 * @returns NULL on failure otherwise a state structure.
 */

static void mdcache_prepare_unexport(struct fsal_export *exp_hdl)
{
	struct mdcache_fsal_export *exp = mdc_export(exp_hdl);
	struct fsal_export *sub_export = exp->mfe_exp.sub_export;

	subcall_raw(exp, sub_export->exp_ops.prepare_unexport(sub_export));
}

/* mdcache_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void mdcache_export_ops_init(struct export_ops *ops)
{
	ops->get_name = mdcache_get_name;
	ops->prepare_unexport = mdcache_prepare_unexport;
	ops->unexport = mdcache_unexport;
	ops->unmount = mdcache_unmount;
	ops->release = mdcache_exp_release;
	ops->lookup_path = mdcache_lookup_path;
	/* lookup_junction unimplemented because deprecated */
	ops->wire_to_host = mdcache_wire_to_host;
	ops->host_to_key = mdcache_host_to_key;
	ops->create_handle = mdcache_create_handle;
	ops->get_fs_dynamic_info = mdcache_get_dynamic_info;
	ops->fs_supports = mdcache_fs_supports;
	ops->fs_maxfilesize = mdcache_fs_maxfilesize;
	ops->fs_maxread = mdcache_fs_maxread;
	ops->fs_maxwrite = mdcache_fs_maxwrite;
	ops->fs_maxlink = mdcache_fs_maxlink;
	ops->fs_maxnamelen = mdcache_fs_maxnamelen;
	ops->fs_maxpathlen = mdcache_fs_maxpathlen;
	ops->fs_acl_support = mdcache_fs_acl_support;
	ops->fs_supported_attrs = mdcache_fs_supported_attrs;
	ops->fs_umask = mdcache_fs_umask;
	ops->check_quota = mdcache_check_quota;
	ops->get_quota = mdcache_get_quota;
	ops->set_quota = mdcache_set_quota;
	ops->getdevicelist = mdcache_getdevicelist;
	ops->fs_layouttypes = mdcache_fs_layouttypes;
	ops->fs_layout_blocksize = mdcache_fs_layout_blocksize;
	ops->fs_maximum_segments = mdcache_fs_maximum_segments;
	ops->fs_loc_body_size = mdcache_fs_loc_body_size;
	ops->get_write_verifier = mdcache_get_write_verifier;
	ops->alloc_state = mdcache_alloc_state;
	ops->free_state = mdcache_free_state;
	ops->is_superuser = mdcache_is_superuser;
	ops->fs_expiretimeparent = mdcache_fs_expiretimeparent;
}

#if 0
struct mdcache_fsal_args {
	struct subfsal_args subfsal;
};

static struct config_item sub_fsal_params[] = {
	CONF_ITEM_STR("name", 1, 10, NULL,
		      subfsal_args, name),
	CONFIG_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_RELAX_BLOCK("FSAL", sub_fsal_params,
			 noop_conf_init, subfsal_commit,
			 mdcache_fsal_args, subfsal),
	CONFIG_EOL
};

static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.mdcache-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};
#endif

/** @} */
