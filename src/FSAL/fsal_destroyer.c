/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2011 The Linux Box Corporation
 * Author: Adam C. Emerson
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
 * @addtogroup FSAL
 * @{
 */

#include "ganesha_list.h"
#include "fsal.h"
#include "fsal_api.h"
#include "nfs_exports.h"
#include "nfs_core.h"
#include "fsal_private.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @file fsal_destroyer.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Kill the FSAL with prejudice
 */

/**
 * @brief Dispose of lingering file handles
 *
 * @param[in] fsal fsal module to clean up
 */

static void shutdown_handles(struct fsal_module *fsal)
{
	/* Handle iterator */
	struct glist_head *hi = NULL;
	/* Next pointer in handle iteration */
	struct glist_head *hn = NULL;

	if (glist_empty(&fsal->handles))
		return;

	LogDebug(COMPONENT_FSAL, "Extra file handles hanging around.");
	glist_for_each_safe(hi, hn, &fsal->handles) {
		struct fsal_obj_handle *h = glist_entry(hi,
							struct fsal_obj_handle,
							handles);
		LogDebug(COMPONENT_FSAL,
			 "Releasing handle");
		h->ops->release(h);
	}
}

/**
 * @brief Dispose of lingering DS handles
 *
 * @param[in] export fsal module to clean up */
static void shutdown_ds_handles(struct fsal_module *fsal)
{
	/* Handle iterator */
	struct glist_head *hi = NULL;
	/* Next pointer in handle iteration */
	struct glist_head *hn = NULL;

	if (glist_empty(&fsal->ds_handles))
		return;

	LogDebug(COMPONENT_FSAL, "Extra DS file handles hanging around.");
	glist_for_each_safe(hi, hn, &fsal->ds_handles) {
		struct fsal_ds_handle *h = glist_entry(hi,
						       struct fsal_ds_handle,
						       ds_handles);
		int32_t refcount = atomic_fetch_int32_t(&h->refcount);
		if (refcount != 0) {
			LogDebug(COMPONENT_FSAL,
				 "Extra references (%"PRIi32") hanging around.",
				 refcount);
			atomic_store_int32_t(&h->refcount, 0);
		}
		h->ops->release(h);
	}
}

/**
 * @brief Shut down an individual export
 *
 * @param[in] export The export to shut down
 */

static void shutdown_export(struct fsal_export *export)
{
	struct fsal_module *fsal = export->fsal;
	LogDebug(COMPONENT_FSAL,
		 "Releasing export");

	export->ops->release(export);
	fsal_put(fsal);
}

/**
 * @brief Destroy FSALs
 */

void destroy_fsals(void)
{
	/* Module iterator */
	struct glist_head *mi = NULL;
	/* Next module */
	struct glist_head *mn = NULL;

	glist_for_each_safe(mi, mn, &fsal_list) {
		/* The module to destroy */
		struct fsal_module *m = glist_entry(mi,
						    struct fsal_module,
						    fsals);
		/* Iterator over exports */
		struct glist_head *ei = NULL;
		/* Next export */
		struct glist_head *en = NULL;
		int32_t refcount = atomic_fetch_int32_t(&m->refcount);

		LogEvent(COMPONENT_FSAL, "Shutting down handles for FSAL %s",
			 m->name);
		shutdown_handles(m);

		LogEvent(COMPONENT_FSAL, "Shutting down DS handles for FSAL %s",
			 m->name);
		shutdown_ds_handles(m);

		LogEvent(COMPONENT_FSAL, "Shutting down exports for FSAL %s",
			 m->name);

		glist_for_each_safe(ei, en, &m->exports) {
			/* The module to destroy */
			struct fsal_export *e = glist_entry(ei,
							    struct fsal_export,
							    exports);
			shutdown_export(e);
		}

		LogEvent(COMPONENT_FSAL, "Exports for FSAL %s shut down",
			 m->name);

		if (refcount != 0) {
			LogCrit(COMPONENT_FSAL,
				"Extra references (%"PRIi32
				") hanging around to FSAL %s",
				refcount, m->name);
			/**
			 * @todo Forcibly blowing away all references
			 * should work fine on files and objects if
			 * we're shutting down, however it will cause
			 * trouble once we have stackable FSALs.  As a
			 * practical matter, though, if the system is
			 * working properly we shouldn't even reach
			 * this point.
			 */
			atomic_store_int32_t(&m->refcount, 0);
		}
		if (m->dl_handle) {
			int rc = 0;
			char *fsal_name = gsh_strdup(m->name);

			LogEvent(COMPONENT_FSAL, "Unloading FSAL %s",
				 fsal_name);
			rc = m->ops->unload(m);
			if (rc != 0) {
				LogMajor(COMPONENT_FSAL,
					 "Unload of %s failed with error %d",
					 fsal_name, rc);
			}
			LogEvent(COMPONENT_FSAL, "FSAL %s unloaded", fsal_name);
			gsh_free(fsal_name);
		}
	}

	release_posix_file_systems();
}

/**
 * @brief Emergency Halt FSALs
 */

void emergency_cleanup_fsals(void)
{
	/* Module iterator */
	struct glist_head *mi = NULL;
	/* Next module */
	struct glist_head *mn = NULL;

	glist_for_each_safe(mi, mn, &fsal_list) {
		/* The module to destroy */
		struct fsal_module *m = glist_entry(mi,
						    struct fsal_module,
						    fsals);
		m->ops->emergency_cleanup();
	}
}

/** @} */
