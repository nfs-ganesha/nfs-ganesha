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
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* file.c
 * File I/O methods for NULL module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "mdcache_int.h"
#include "mdcache_lru.h"
#include "mdcache.h"

/**
 * @brief Callback arg for MDCACHE async callbacks
 *
 * MDCACHE needs to know what its object is related to the sub-FSAL's object.
 * This wraps the given callback arg with MDCACHE specific info
 */
struct mdc_async_arg {
	struct fsal_obj_handle *obj_hdl; /**< MDCACHE's handle */
	fsal_async_cb cb; /**< Wrapped callback */
	void *cb_arg; /**< Wrapped callback data */
};

/**
 *
 * @brief Set a timestamp to the current time
 *
 * @param[out] time Pointer to time to be set
 *
 * @return true on success, false on failure
 *
 */
bool mdc_set_time_current(struct timespec *time)
{
	struct timeval t;

	if (time == NULL)
		return false;

	if (gettimeofday(&t, NULL) != 0)
		return false;

	time->tv_sec = t.tv_sec;
	time->tv_nsec = 1000 * t.tv_usec;

	return true;
}

/**
 * @brief IO Advise
 *
 * Delegate to sub-FSAL
 *
 * @param[in]  obj_hdl      File to be written
 * @param[in,out] info      Information about the data
 *
 * @return FSAL status.
 */
fsal_status_t mdcache_io_advise(struct fsal_obj_handle *obj_hdl,
				struct io_hints *hints)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->io_advise(
			entry->sub_handle, hints));

	return status;
}

/**
 * @brief Control
 *
 * Delegate to sub-FSAL
 *
 * @param[in]  obj_hdl      File on which to act
 * @param[in] operation     What to do
 * @param[in] void          parameters for doing it
 *
 * @return FSAL status.
 */
fsal_status_t mdcache_control(struct fsal_obj_handle *obj_hdl, int operation,
			      void *data)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->control(entry->sub_handle,
							     operation, data));

	return status;
}

/**
 * @brief Close a file
 *
 * @param[in] obj_hdl	File to close
 * @return FSAL status
 */
fsal_status_t mdcache_close(struct fsal_obj_handle *obj_hdl)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	/* XXX dang caching FDs?  How does it interact with multi-FD */
	subcall(status = entry->sub_handle->obj_ops->close(entry->sub_handle));

	return status;
}

static fsal_status_t
mdc_open2_by_name(mdcache_entry_t *mdc_parent, struct state_t *state,
		  fsal_openflags_t openflags, enum fsal_create_mode createmode,
		  const char *name, struct fsal_attrlist *attrib_set,
		  struct fsal_attrlist *attrs_out, fsal_verifier_t verifier,
		  mdcache_entry_t **new_entry, bool *caller_perm_check,
		  struct fsal_attrlist *parent_pre_attrs_out,
		  struct fsal_attrlist *parent_post_attrs_out)
{
	fsal_status_t status;
	bool uncached = createmode >= FSAL_GUARDED;
	mdcache_entry_t *entry;
	struct fsal_obj_handle *sub_handle;

	*new_entry = NULL;

	if (!name)
		return fsalstat(ERR_FSAL_INVAL, 0);

	status = mdc_lookup(mdc_parent, name, uncached, &entry, NULL);

	if (FSAL_IS_ERROR(status)) {
		/* Does not exist, or other error, return to open2 to
		 * proceed if not found, otherwise to return the error.
		 */
		LogFullDebug(COMPONENT_MDCACHE, "Lookup failed");
		return status;
	}

	/* Found to exist */
	if (createmode == FSAL_GUARDED) {
		status = fsalstat(ERR_FSAL_EXIST, 0);
		goto out_unref;
	} else if (createmode == FSAL_EXCLUSIVE) {
		/* Exclusive create with entry found, check verifier */
		if (!mdcache_check_verifier(&entry->obj_handle, verifier)) {
			/* Verifier check failed. */
			LogFullDebug(COMPONENT_MDCACHE,
				     "Verifier check failed.");
			status = fsalstat(ERR_FSAL_EXIST, 0);
			goto out_unref;
		}

		/* Verifier matches, go ahead and open the file. */

	} /* else UNGUARDED, go ahead and open the file. */

	/* Check if the object type is REGULAR_FILE. If not then give error. */
	if (entry->obj_handle.type != REGULAR_FILE) {
		LogDebug(COMPONENT_MDCACHE,
			 "Trying to open a non-regular file");
		if (entry->obj_handle.type == DIRECTORY) {
			/* Trying to open2 a directory */
			status = fsalstat(ERR_FSAL_ISDIR, 0);
			goto out_unref;
		} else {
			/* Trying to open2 any other non-regular file */
			status = fsalstat(ERR_FSAL_SYMLINK, 0);
			goto out_unref;
		}
	}
	subcall(status = entry->sub_handle->obj_ops->open2(
			entry->sub_handle, state, openflags, createmode, NULL,
			attrib_set, verifier, &sub_handle, attrs_out,
			caller_perm_check, parent_pre_attrs_out,
			parent_post_attrs_out));

	if (FSAL_IS_ERROR(status)) {
		/* Open failed. */
		LogFullDebug(COMPONENT_MDCACHE, "Open failed %s",
			     msg_fsal_err(status.major));
		goto out_unref;
	}

	LogFullDebug(COMPONENT_MDCACHE, "Opened entry %p, sub_handle %p", entry,
		     entry->sub_handle);

	if (openflags & FSAL_O_TRUNC) {
		/* Invalidate the attributes since we just truncated. */
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	if (attrs_out) {
		/* Handle attribute request */
		if (!(attrs_out->valid_mask & ATTR_RDATTR_ERR)) {
			struct fsal_attrlist attrs;

			/* open2() gave us attributes.  Update the cache */
			fsal_prepare_attrs(
				&attrs,
				op_ctx->fsal_export->exp_ops.fs_supported_attrs(
					op_ctx->fsal_export) |
					ATTR_RDATTR_ERR);
			fsal_copy_attrs(&attrs, attrs_out, false);

			PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
			mdc_update_attr_cache(entry, &attrs);
			PTHREAD_RWLOCK_unlock(&entry->attr_lock);

			/* mdc_update_attr_cache() consumes attrs; the release
			 * is here only for code inspection. */
			fsal_release_attrs(&attrs);
		} else if (attrs_out->request_mask & ATTR_RDATTR_ERR) {
			/* We didn't get attributes from open2, but the caller
			 * wants them.  Try a full getattrs() */
			status = entry->obj_handle.obj_ops->getattrs(
				&entry->obj_handle, attrs_out);
			if (FSAL_IS_ERROR(status)) {
				LogFullDebug(COMPONENT_MDCACHE,
					     "getattrs failed status=%s",
					     fsal_err_txt(status));
			}
		}
	}
	*new_entry = entry;

	return status;

out_unref:

	mdcache_lru_unref(entry, LRU_ACTIVE_REF);
	return status;
}

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * At least the mode attribute must be set if createmode is FSAL_UNCHECKED,
 * FSAL_GUARDED, FSAL_EXCLUSIVE_41, or FSAL_EXCLUSIVE_9P.
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * The caller is expected to invoke fsal_release_attrs to release any
 * resources held by the set attributes. The FSAL layer MAY have added an
 * inherited ACL.
 *
 * The caller will set the request_mask in attrs_out to indicate the attributes
 * of interest. ATTR_ACL SHOULD NOT be requested and need not be provided. If
 * not all the requested attributes can be provided, this method MUST return
 * an error unless the ATTR_RDATTR_ERR bit was set in the request_mask.
 *
 * Since this method may instantiate a new fsal_obj_handle, it will be forced
 * to fetch at least some attributes in order to even know what the object
 * type is (as well as it's fileid and fsid). For this reason, the operation
 * as a whole can be expected to fail if the attributes were not able to be
 * fetched.
 *
 * The attributes will not be returned if this is an open by object as
 * opposed to an open by name.
 *
 * @note If the file was created, @a new_obj has been ref'd
 *
 * @param[in]     obj_hdl               File to open or parent directory
 * @param[in,out] state                 state_t to use for this operation
 * @param[in]     openflags             Mode for open
 * @param[in]     createmode            Mode for create
 * @param[in]     name                  Name for file if being created or opened
 * @param[in]     attrs_in              Attributes to set on created file
 * @param[in]     verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj               Newly created object
 * @param[in,out] attrs_out             Optional attributes for newly created
 *                                      object
 * @param[in,out] caller_perm_check     The caller must do a permission check
 * @param[in,out] parent_pre_attrs_out  Optional attributes for parent dir
 *                                      before the operation. Should be atomic.
 * @param[in,out] parent_post_attrs_out Optional attributes for parent dir
 *                                      after the operation. Should be atomic.
 *
 * @return FSAL status.
 */

fsal_status_t mdcache_open2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state, fsal_openflags_t openflags,
			    enum fsal_create_mode createmode, const char *name,
			    struct fsal_attrlist *attrs_in,
			    fsal_verifier_t verifier,
			    struct fsal_obj_handle **new_obj,
			    struct fsal_attrlist *attrs_out,
			    bool *caller_perm_check,
			    struct fsal_attrlist *parent_pre_attrs_out,
			    struct fsal_attrlist *parent_post_attrs_out)
{
	mdcache_entry_t *mdc_parent =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	mdcache_entry_t *new_entry = NULL;
	struct fsal_obj_handle *sub_handle = NULL;
	fsal_status_t status;
	struct fsal_attrlist attrs;
	const char *dispname = name != NULL ? name : "<by-handle>";
	struct mdcache_fsal_export *export = mdc_cur_export();
	bool invalidate;

	LogAttrlist(COMPONENT_MDCACHE, NIV_FULL_DEBUG, "attrs_in ", attrs_in,
		    false);

	if (name) {
		/* Check if we have the file already cached, in which case
		 * we can open by object instead of by name.
		 */
		status = mdc_open2_by_name(mdc_parent, state, openflags,
					   createmode, name, attrs_in,
					   attrs_out, verifier, &new_entry,
					   caller_perm_check,
					   parent_pre_attrs_out,
					   parent_post_attrs_out);

		if (status.major == ERR_FSAL_NO_ERROR) {
			/* Return the newly opened file. */
			*new_obj = &new_entry->obj_handle;

			return status;
		}

		if (status.major != ERR_FSAL_NOENT) {
			/* Return the error */
			*new_obj = NULL;
			return status;
		}
	}

	/* Ask for all supported attributes except ACL and FS_LOCATIONS (we
	 * defer fetching ACL/FS_LOCATIONS until asked for it (including a
	 * permission check).
	 *
	 * We can survive if we don't actually succeed in fetching the
	 * attributes.
	 */
	fsal_prepare_attrs(&attrs,
			   (op_ctx->fsal_export->exp_ops.fs_supported_attrs(
				    op_ctx->fsal_export) &
			    ~(ATTR_ACL | ATTR4_FS_LOCATIONS)) |
				   ATTR_RDATTR_ERR);

	subcall(status = mdc_parent->sub_handle->obj_ops->open2(
			mdc_parent->sub_handle, state, openflags, createmode,
			name, attrs_in, verifier, &sub_handle, &attrs,
			caller_perm_check, parent_pre_attrs_out,
			parent_post_attrs_out));

	if (unlikely(FSAL_IS_ERROR(status))) {
		LogDebug(COMPONENT_MDCACHE, "open2 %s failed with %s", dispname,
			 fsal_err_txt(status));
		if (status.major == ERR_FSAL_STALE) {
			/* If we got ERR_FSAL_STALE, the previous FSAL call
			 * must have failed with a bad parent.
			 */
			mdcache_kill_entry(mdc_parent);
		}
		fsal_release_attrs(&attrs);
		*new_obj = NULL;
		return status;
	}

	if (!name) {
		/* Wasn't a create and/or entry already found */
		if (openflags & FSAL_O_TRUNC) {
			/* Mark the attributes as not-trusted, so we will
			 * refresh the attributes.
			 */
			atomic_clear_uint32_t_bits(&mdc_parent->mde_flags,
						   MDCACHE_TRUST_ATTRS);
		}

		LogFullDebug(COMPONENT_MDCACHE, "Open2 of object succeeded.");
		*new_obj = obj_hdl;
		/* We didn't actually get any attributes, but release anyway
		 * for code consistency.
		 */
		fsal_release_attrs(&attrs);
		return status;
	}

	invalidate = createmode != FSAL_NO_CREATE;

	/* We will invalidate parent attrs if we did any form of create. */
	status = mdcache_alloc_and_check_handle(export, sub_handle, new_obj,
						false, &attrs, attrs_out,
						"open2 ", mdc_parent, name,
						&invalidate, state, false);

	fsal_release_attrs(&attrs);

	if (FSAL_IS_SUCCESS(status) && createmode != FSAL_NO_CREATE &&
	    !invalidate) {
		/* Refresh destination directory attributes without
		 * invalidating dirents.
		 */
		status = mdcache_refresh_attrs_no_invalidate(mdc_parent);
	}

	return status;
}

/**
 * @brief Check the verifier
 *
 * @param[in] obj_hdl	File to check
 * @param[in] verifier	Verifier to check
 * @return FSAL status
 */
bool mdcache_check_verifier(struct fsal_obj_handle *obj_hdl,
			    fsal_verifier_t verifier)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	bool result;

	/* XXX dang caching FDs?  How does it interact with multi-FD */
	subcall(result = entry->sub_handle->obj_ops->check_verifier(
			entry->sub_handle, verifier));

	return result;
}

/**
 * @brief Get the open status of a file (new style)
 *
 * Delegate to sub-FSAL, since this isn't cached metadata currently
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to check
 * @return Open flags indicating state
 */
fsal_openflags_t mdcache_status2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_openflags_t status;

	subcall(status = entry->sub_handle->obj_ops->status2(entry->sub_handle,
							     state));

	return status;
}

/**
 * @brief Re-open a file with different flags (new style)
 *
 * Delegate to sub-FSAL.  This should not be called unless the sub-FSAL supports
 * reopen2.
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to re-open
 * @param[in] openflags	New open flags
 * @return FSAL status
 */
fsal_status_t mdcache_reopen2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state, fsal_openflags_t openflags)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;
	bool truncated = openflags & FSAL_O_TRUNC;

	subcall(status = entry->sub_handle->obj_ops->reopen2(entry->sub_handle,
							     state, openflags));

	if (FSAL_IS_ERROR(status) && (status.major == ERR_FSAL_STALE))
		mdcache_kill_entry(entry);

	if (truncated && !FSAL_IS_ERROR(status)) {
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	return status;
}

/**
 * @brief Callback for MDCACHE read calls in MDCACHE context
 *
 * Unstack, and call up.
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] obj_data		Data for call
 * @param[in] caller_data	Data for caller
 */
static void mdc_read_super_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			      void *obj_data, void *caller_data)
{
	struct mdc_async_arg *arg = caller_data;
	mdcache_entry_t *entry =
		container_of(arg->obj_hdl, mdcache_entry_t, obj_handle);

	/* Fixup FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	/*
	 * cb will drop the initial ref, take an additional ref to prevent the
	 * entry from getting freed/reaped.
	 */
	mdcache_lru_ref(entry, LRU_ACTIVE_REF);
	arg->cb(arg->obj_hdl, ret, obj_data, arg->cb_arg);

	if (!FSAL_IS_ERROR(ret))
		mdc_set_time_current(&entry->attrs.atime);
	else if (ret.major == ERR_FSAL_STALE)
		mdcache_kill_entry(entry);
	mdcache_lru_unref(entry, LRU_ACTIVE_REF);
	gsh_free(arg);
}

/**
 * @brief Callback for MDCACHE read calls
 *
 * Call super callback in MDCACHE context
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] obj_data		Data for call
 * @param[in] caller_data	Data for caller
 */
static void mdc_read_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			void *obj_data, void *caller_data)
{
	supercall(mdc_read_super_cb(obj, ret, obj_data, caller_data););
}

/**
 * @brief Read from a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */
void mdcache_read2(struct fsal_obj_handle *obj_hdl, bool bypass,
		   fsal_async_cb done_cb, struct fsal_io_arg *read_arg,
		   void *caller_arg)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	struct mdc_async_arg *arg;

	/* Set up async callback */
	arg = gsh_calloc(1, sizeof(*arg));
	arg->obj_hdl = obj_hdl;
	arg->cb = done_cb;
	arg->cb_arg = caller_arg;

	subcall(entry->sub_handle->obj_ops->read2(entry->sub_handle, bypass,
						  mdc_read_cb, read_arg, arg));
}

/**
 * @brief Callback for MDCACHE write calls in MDCACHE context
 *
 * Unstack, and call up.
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] obj_data		Data for call
 * @param[in] caller_data	Data for caller
 */
static void mdc_write_super_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			       void *obj_data, void *caller_data)
{
	struct mdc_async_arg *arg = caller_data;
	mdcache_entry_t *entry =
		container_of(arg->obj_hdl, mdcache_entry_t, obj_handle);

	if (ret.major == ERR_FSAL_STALE) {
		/*
		 * killing the entry might drop the sentinel ref. Take an
		 * extra ref to prevent this entry for being reused.
		 */
		mdcache_lru_ref(entry, LRU_ACTIVE_REF);
		mdcache_kill_entry(entry);
	} else {
		/*
		 * attr_generation must be increased prior to
		 * MDCACHE_TRUST_ATTRS to prevent a case where this flag
		 * was queried before the generation was updated
		 */
		atomic_inc_uint32_t(&entry->attr_generation);
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	}

	arg->cb(arg->obj_hdl, ret, obj_data, arg->cb_arg);

	if (ret.major == ERR_FSAL_STALE)
		mdcache_lru_unref(entry, LRU_ACTIVE_REF);
	gsh_free(arg);
}

/**
 * @brief Callback for MDCACHE write calls
 *
 * Call super callback in MDCACHE context
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] obj_data		Data for call
 * @param[in] caller_data	Data for caller
 */
static void mdc_write_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			 void *obj_data, void *caller_data)
{
	supercall(mdc_write_super_cb(obj, ret, obj_data, caller_data););
}

/**
 * @brief Write to a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl		Object owning state
 * @param[in] bypass		Bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */
void mdcache_write2(struct fsal_obj_handle *obj_hdl, bool bypass,
		    fsal_async_cb done_cb, struct fsal_io_arg *write_arg,
		    void *caller_arg)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	struct mdc_async_arg *arg;

	/* Set up async callback */
	arg = gsh_calloc(1, sizeof(*arg));
	arg->obj_hdl = obj_hdl;
	arg->cb = done_cb;
	arg->cb_arg = caller_arg;

	subcall(entry->sub_handle->obj_ops->write2(
		entry->sub_handle, bypass, mdc_write_cb, write_arg, arg));
}

/**
 * @brief Seek within a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to seek within
 * @param[in] info	io_info for seek
 * @return FSAL status
 */
fsal_status_t mdcache_seek2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state, struct io_info *info)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->seek2(entry->sub_handle,
							   state, info));

	if (status.major == ERR_FSAL_STALE)
		mdcache_kill_entry(entry);

	return status;
}

/**
 * @brief Advise access pattern for a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to advise on
 * @param[in] hints	io_hints containing advice
 * @return FSAL status
 */
fsal_status_t mdcache_io_advise2(struct fsal_obj_handle *obj_hdl,
				 struct state_t *state, struct io_hints *hints)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->io_advise2(
			entry->sub_handle, state, hints));

	if (status.major == ERR_FSAL_STALE)
		mdcache_kill_entry(entry);

	return status;
}

/**
 * @brief Commit to a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl	Object to commit
 * @param[in] offset	Offset into file
 * @param[in] len	Length of commit
 * @return FSAL status
 */
fsal_status_t mdcache_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
			      size_t len)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->commit2(entry->sub_handle,
							     offset, len));

	if (status.major == ERR_FSAL_STALE)
		mdcache_kill_entry(entry);
	else
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);

	return status;
}

/**
 * @brief Lock/unlock a range in a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to lock/unlock
 * @param[in] p_owner	Private data for lock
 * @param[in] lock_op	Lock operation
 * @param[in] req_lock	Parameters for requested lock
 * @param[in] conflicting_lock	Description of existing conflicting lock
 * @return FSAL status
 */
fsal_status_t mdcache_lock_op2(struct fsal_obj_handle *obj_hdl,
			       struct state_t *state, void *p_owner,
			       fsal_lock_op_t lock_op,
			       fsal_lock_param_t *req_lock,
			       fsal_lock_param_t *conflicting_lock)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->lock_op2(
			entry->sub_handle, state, p_owner, lock_op, req_lock,
			conflicting_lock));

	return status;
}

/**
 * @brief Get/Release delegation for a file (new style)
 *
 * Delegate to sub-FSAL
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to get/release
 * @param[in] p_owner	Private owner
 * @param[in] deleg	Delegation operation
 * @return FSAL status
 */
fsal_status_t mdcache_lease_op2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state, void *p_owner,
				fsal_deleg_t deleg)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->lease_op2(
			entry->sub_handle, state, p_owner, deleg););

	return status;
}

/**
 * @brief Close a file (new style)
 *
 * @param[in] obj_hdl	Object owning state
 * @param[in] state	Open file state to close
 * @return FSAL status
 */
fsal_status_t mdcache_close2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->close2(entry->sub_handle,
							    state));

	if (test_mde_flags(entry, MDCACHE_UNREACHABLE) &&
	    !mdc_has_state(entry)) {
		/* Entry was marked unreachable, and last state is gone */
		(void)mdcache_kill_entry(entry);
	}

	return status;
}

fsal_status_t mdcache_fallocate(struct fsal_obj_handle *obj_hdl,
				struct state_t *state, uint64_t offset,
				uint64_t length, bool allocate)
{
	mdcache_entry_t *entry =
		container_of(obj_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	subcall(status = entry->sub_handle->obj_ops->fallocate(
			entry->sub_handle, state, offset, length, allocate););

	if (status.major == ERR_FSAL_STALE)
		mdcache_kill_entry(entry);
	else
		atomic_clear_uint32_t_bits(&entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);

	return status;
}

/**
 * @brief Server-side copy between two files (NFSv4.2 COPY offload)
 *
 * Unwraps both mdcache handles and routes to the most efficient path:
 *
 *   FS1 -> FS1 (same sub-FSAL module):
 *     Delegates to the sub-FSAL's copy_file_range so it can use an
 *     optimised path (e.g. Linux copy_file_range(2) in FSAL_VFS).
 *
 *   FS1 -> FS1, different exports (e.g. two Ceph mount points):
 *     Same path as above: subcall_raw uses the src export so the
 *     sub-FSAL always sees the correct context for the source object.
 *     fsal_buffered_copy_fsal()'s switch_src_ctx guard detects that
 *     op_ctx is already at the sub-FSAL layer and does not override it.
 *
 *   FS1 -> FS2 (different sub-FSAL modules):
 *     Falls back to the generic buffered read/write loop via
 *     fsal_buffered_copy_fsal().  That function switches op_ctx to
 *     the src export for each read and restores it for the write.
 *
  * After a successful copy the destination entry's attribute cache is
 * invalidated because the file size and mtime will have changed.
 *
 * @param[in]  src_hdl    Source object handle (mdcache entry)
 * @param[in]  src_state  Source open-state (may be NULL)
 * @param[in]  dst_hdl    Destination object handle (mdcache entry)
 * @param[in]  dst_state  Destination open-state (may be NULL)
 * @param[in]  src_offset Byte offset to start reading from
 * @param[in]  dst_offset Byte offset to start writing at
 * @param[in]  count      Bytes to copy
 * @param[out] copied     Bytes actually copied
 * @return FSAL status.
 */
fsal_status_t mdcache_copy_file_range(struct fsal_obj_handle *src_hdl,
				      struct state_t *src_state,
				      struct fsal_obj_handle *dst_hdl,
				      struct state_t *dst_state,
				      uint64_t src_offset, uint64_t dst_offset,
				      uint64_t count, uint64_t *copied)
{
	mdcache_entry_t *src_entry =
		container_of(src_hdl, mdcache_entry_t, obj_handle);
	mdcache_entry_t *dst_entry =
		container_of(dst_hdl, mdcache_entry_t, obj_handle);
	fsal_status_t status;

	if (src_entry->sub_handle->fsal == dst_entry->sub_handle->fsal) {
		/*
		 * Same sub-FSAL: zero-copy via obj_ops->copy_file_range.
		 * op_ctx->fsal_export is currently the dst MDCache export.
		 * We must switch to the src's sub-FSAL export before calling
		 * into the sub-FSAL so that FSALs that resolve their private
		 * context from op_ctx->fsal_export (e.g. Ceph picks its
		 * cmount via container_of(op_ctx->fsal_export, ...)) get the
		 * correct object for the source even when src and dst are on
		 * different exports of the same FSAL (e.g. two Ceph mounts).
		 *
		 * Use the src state's export when available; fall back to the
		 * current op_ctx export (same export, intra-export copy).
		 *
		 * subcall_raw() restores op_ctx->fsal_export to the src MDCache
		 * export after the call, but our caller expects the dst MDCache
		 * export to remain in op_ctx.  Save and restore it explicitly.
		 */
		struct fsal_export *src_fsal_exp =
			(src_state != NULL)
				? src_state->state_export->fsal_export
				: op_ctx->fsal_export;
		struct mdcache_fsal_export *src_mdc_exp =
			mdc_export(src_fsal_exp);
		/* dst MDCache export — op_ctx->fsal_export on entry */
		struct mdcache_fsal_export *dst_mdc_exp = mdc_cur_export();

		LogFullDebug(COMPONENT_NFS_V4,
			     "COPY mdcache: same sub-FSAL (%s) src_off=%" PRIu64
			     " dst_off=%" PRIu64 " count=%" PRIu64
			     "src_exp=%s dst_exp=%s ",
			     src_entry->sub_handle->fsal->name, src_offset,
			     dst_offset, count,
			     src_mdc_exp->name, dst_mdc_exp->name);

		op_ctx->fsal_export = &src_mdc_exp->mfe_exp;
		subcall(status =
				src_entry->sub_handle->obj_ops->copy_file_range(
					src_entry->sub_handle, src_state,
					dst_entry->sub_handle, dst_state,
					src_offset, dst_offset, count, copied));
		op_ctx->fsal_export = &dst_mdc_exp->mfe_exp;
	} else {
		/*
		 * Different sub-FSALs (FS1->FS2): buffered fallback since no
		 * zero-copy is available across FSAL boundaries.
		 */
		LogFullDebug(
			COMPONENT_NFS_V4,
			"COPY mdcache: crossFSAL (%s->%s) BFC src_off=%" PRIu64
			" dst_off=%" PRIu64 " count=%" PRIu64,
			src_entry->sub_handle->fsal->name,
			dst_entry->sub_handle->fsal->name, src_offset,
			dst_offset, count);
		status = fsal_buffered_copy_fsal(src_hdl, dst_hdl, src_state,
						 dst_state, src_offset,
						 dst_offset, count, copied);
	}

	if (FSAL_IS_SUCCESS(status)) {
		LogFullDebug(COMPONENT_NFS_V4,
			     "COPY mdcache: done OK copied=%" PRIu64
			     " — invalidating dst attrs",
			     *copied);
		/*
		 * Destination size/mtime changed.  Bump attr_generation before
		 * clearing MDCACHE_TRUST_ATTRS so concurrent getattrs see a
		 * consistent view.
		 */
		atomic_inc_uint32_t(&dst_entry->attr_generation);
		atomic_clear_uint32_t_bits(&dst_entry->mde_flags,
					   MDCACHE_TRUST_ATTRS);
	} else if (status.major == ERR_FSAL_STALE) {
		LogWarn(COMPONENT_NFS_V4,
			"COPY mdcache: dst entry STALE killing mdcache entry");
		mdcache_kill_entry(dst_entry);
	} else {
		LogWarn(COMPONENT_NFS_V4,
			"COPY mdcache: FSAL error major=%u minor=%u",
			status.major, status.minor);
	}

	return status;
}
