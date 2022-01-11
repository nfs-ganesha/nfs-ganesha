// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright 2020-2021 Google LLC
 * Author: Solomon Boulos <boulos@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include "config.h"

#include "fsal.h"
#include "fsal_types.h"
#include "nfs_file_handle.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_init.h"

#include "proxyv3_fsal_methods.h"

/**
 * @struct PROXY_V3
 * @brief Struct telling Ganesha what things we can handle or not.
 *
 *        Some fields are overwritten later via an FSINFO call.
 */

struct proxyv3_fsal_module PROXY_V3 = {
	.module = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = 1024,
			.maxpathlen = 1024,
			.no_trunc = true,
			.chown_restricted = true,
			.cansettime = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = false,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW,
			.homogenous = true,
			.supported_attrs = ((const attrmask_t) ATTRS_NFS3),
			.link_supports_permission_checks = true,
			.readdir_plus = true,
			.expire_time_parent = -1,
		}
	}
};

/**
 * @struct proxyv3_params
 * @brief Global/server-wide parameters for NFSv3 proxying.
 */
static struct config_item proxyv3_params[] = {
	/*  Maximum read/write size in bytes */
	CONF_ITEM_UI64("maxread", 1024, FSAL_MAXIOSIZE,
		       1048576,
		       proxyv3_fsal_module,
		       module.fs_info.maxread),

	CONF_ITEM_UI64("maxwrite", 1024, FSAL_MAXIOSIZE,
		       1048576,
		       proxyv3_fsal_module,
		       module.fs_info.maxwrite),

	/* How many sockets for our rpc layer */
	CONF_ITEM_UI32("num_sockets", 1, 1000,
		       32,
		       proxyv3_fsal_module,
		       num_sockets),

	CONFIG_EOL
};

/**
 * @struct proxyv3_export_params
 * @brief Per export config parameters (just srv_addr currently).
 */
static struct config_item proxyv3_export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_MAND_IP_ADDR("Srv_Addr", "127.0.0.1",
			  proxyv3_client_params, srv_addr),
	CONFIG_EOL
};

/**
 * @struct proxyv3_param
 * @brief Config block for PROXY v3 parameters.
 */

struct config_block proxyv3_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.proxyv3",
	.blk_desc.name = "PROXY_V3",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = proxyv3_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/**
 * @struct proxyv3_export_param
 * @brief Config block for PROXY v3 per-export parameters.
 */

struct config_block proxyv3_export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.proxyv3-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = proxyv3_export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};


/**
 * @brief Grab the sockaddr from our params via op_ctx->fsal_export.
 */
const struct sockaddr *proxyv3_sockaddr(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.sockaddr;
}

/**
 * @brief Grab the socklen from our params via op_ctx.
 */
const socklen_t proxyv3_socklen(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.socklen;
}

/**
 * @brief Grab the sockname from our params via op_ctx.
 */
static const char *proxyv3_sockname(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.sockname;
}

/**
 * @brief Grab the mountd port from our params via op_ctx.
 */
static const uint proxyv3_mountd_port(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.mountd_port;
}

/**
 * @brief Grab the nfsd port from our params via op_ctx.
 */
static const uint proxyv3_nfsd_port(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.nfsd_port;
}

/**
 * @brief Grab the NLM port from our params via op_ctx.
 */

const uint proxyv3_nlm_port(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	return export->params.nlm_port;
}

/**
 * @brief Grab the user credentials from op_ctx.
 */
const struct user_cred *proxyv3_creds(void)
{
	/* We want the *original* credentials, so we reflect the client */
	return &op_ctx->original_creds;
}

/**
 * @brief Grab the preferred bytes per READDIRPLUS from our params via op_ctx.
 */

const uint proxyv3_readdir_preferred(void)
{
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);
	fsal_staticfsinfo_t *fsinfo = &PROXY_V3.module.fs_info;

	uint preferred = export->params.readdir_preferred;
	uint maxread = fsinfo->maxread;

	/* If it's 0, just return maxread. */
	if (preferred == 0) {
		return maxread;
	}

	/* If it's too big, clamp it. */
	if (preferred > maxread) {
		return maxread;
	}

	return preferred;
}


/**
 * @brief Load configuration from the config file.
 *
 * @param fsal_handle A handle to the inner FSAL module.
 * @param config_file The config file to ask to parse.
 * @param error_type An output parameter for error reporting.
 *
 * @return - ERR_FSAL_NO_ERROR on success, ERR_FSAL_INVAL otherwise.
 */

static fsal_status_t
proxyv3_init_config(struct fsal_module *fsal_handle,
		    config_file_t config_file,
		    struct config_error_type *error_type)
{
	struct proxyv3_fsal_module *proxy_v3 =
		container_of(fsal_handle, struct proxyv3_fsal_module, module);

	LogDebug(COMPONENT_FSAL,
		 "Loading the Proxy V3 Config");

	(void) load_config_from_parse(config_file,
				      &proxyv3_param,
				      proxy_v3,
				      true,
				      error_type);
	if (!config_error_is_harmless(error_type)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	display_fsinfo(&(proxy_v3->module));

	/* Now that we have our config, try to setup our RPC layer. */
	if (!proxyv3_rpc_init(proxy_v3->num_sockets)) {
		LogCrit(COMPONENT_FSAL,
			"ProxyV3 RPC failed to initialize");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (!proxyv3_nlm_init()) {
		LogCrit(COMPONENT_FSAL,
			"ProxyV3 NLM failed to initialize");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Given a filehandle and attributes make a new object handle.
 *
 * @param export_handle A handle to our FSAL export.
 * @param fh3 The input NFSv3 file handle.
 * @param attrs The input fattr3 file attributes.
 * @param parent An optional pointer to this object's parent.
 * @param fsal_attrs_out An optional output for the FSAL version of attributes.
 *
 * @returns - A new proxyv3_obj_handle on success, NULL otherwise.
 */

static struct proxyv3_obj_handle *
proxyv3_alloc_handle(struct fsal_export *export_handle,
		     const nfs_fh3 *fh3,
		     const fattr3 *attrs,
		     const struct proxyv3_obj_handle *parent,
		     struct fsal_attrlist *fsal_attrs_out)
{
	/* Fill the attributes first to avoid an alloc on failure. */
	struct fsal_attrlist local_attributes;
	struct fsal_attrlist *attrs_out;

	LogDebug(COMPONENT_FSAL,
		 "Making handle from fh3 %p with parent %p",
		 fh3, parent);

	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 handle is %s", LEN_FH_STR,
			   fh3->data.data_val, fh3->data.data_len);


	/* If we aren't given a destination, make up our own. */
	if (fsal_attrs_out != NULL) {
		attrs_out = fsal_attrs_out;
	} else {
		/* Say we only want NFSv3 attributes. */
		memset(&local_attributes, 0, sizeof(struct fsal_attrlist));
		attrs_out = &local_attributes;
		FSAL_SET_MASK(attrs_out->request_mask, ATTRS_NFS3);
	}

	if (!fattr3_to_fsalattr(attrs, attrs_out)) {
		/* @note The callee already warned. No need to repeat. */
		return NULL;
	}

	/*
	 * Alright, ready to go. Instead of being fancy like the NFSv4 proxy,
	 * we'll allocate the nested fh3 with an additional calloc call.
	 */

	struct proxyv3_obj_handle *result =
		gsh_calloc(1, sizeof(struct proxyv3_obj_handle));

	/* Copy the fh3 struct. */
	size_t len = fh3->data.data_len;

	result->fh3.data.data_len = len;
	result->fh3.data.data_val = gsh_calloc(1, len);
	memcpy(result->fh3.data.data_val, fh3->data.data_val, len);

	/* Copy the NFSv3 attrs. */
	memcpy(&result->attrs, attrs, sizeof(fattr3));

	fsal_obj_handle_init(&result->obj, export_handle, attrs_out->type);

	result->obj.fsid = attrs_out->fsid;
	result->obj.fileid = attrs_out->fileid;
	result->obj.obj_ops = &PROXY_V3.handle_ops;

	result->parent = parent;

	return result;
}

/**
 * @brief Clean up an object handle, freeing its memory.
 *
 * @param obj_hdl The object handle.
 */

static void proxyv3_handle_release(struct fsal_obj_handle *obj_hdl)
{
	struct proxyv3_obj_handle *handle =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "Cleaning up handle %p", handle);

	/* Free the underlying filehandle bytes. */
	gsh_free(handle->fh3.data.data_val);

	/* Finish the outer object. */
	fsal_obj_handle_fini(obj_hdl);

	/* Free our allocated handle. */
	gsh_free(handle);
}


/**
 * @brief Given a path and parent object, do a *single* LOOKUP3.
 *
 * @param export_handle A pointer to our FSAL export.
 * @param path The file path (must not be NULL).
 * @param parent The parent directory of path.
 * @param handle The output argument for the new object handle.
 * @param attrs_out The output argument for the attributes.
 *
 * @returns - ERR_FSAL_NO_ERROR on success, an error otherwise.
 */

static fsal_status_t
proxyv3_lookup_internal(struct fsal_export *export_handle,
			const char *path,
			struct fsal_obj_handle *parent,
			struct fsal_obj_handle **handle,
			struct fsal_attrlist *attrs_out)
{
	LogDebug(COMPONENT_FSAL,
		 "Doing a lookup of '%s'", path);

	if (parent == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Error, expected a parent handle.");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (parent->type != DIRECTORY) {
		LogCrit(COMPONENT_FSAL,
			"Error, expected parent to be a directory. Got %u",
			parent->type);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	if (handle == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Error, expected an output handle.");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Mark as NULL in case we fail along the way. */
	*handle = NULL;

	if (path == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Error, received garbage path");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (*path == '\0') {
		/*
		 * @todo What does an empty path mean? We shouldn't have gotten
		 * here...
		 */

		LogCrit(COMPONENT_FSAL,
			"Error. Path is NUL. Should have exited earlier.");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (strchr(path, '/') != NULL) {
		LogCrit(COMPONENT_FSAL,
			"Path (%s) contains embedded forward slash.", path);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	struct proxyv3_obj_handle *parent_obj =
		container_of(parent, struct proxyv3_obj_handle, obj);

	/*
	 * Small optimization to avoid a network round-trip: if we already know
	 * the answer, hand it back.
	 */
	if (true && /* @todo Turn this optimization into a flag */
	    (strcmp(path, ".") == 0 ||
	     /*
	      * We may not have the parent pointer information (could be from a
	      * create_handle from key thing, so let the backend respond)
	      */
	     (strcmp(path, "..") == 0 && parent_obj->parent != NULL))) {
		/* We know the answer, just give it to them. */
		LogDebug(COMPONENT_FSAL,
			 "Got a lookup for '%s' returning the directory handle",
			 path);

		struct proxyv3_obj_handle *which_dir;

		if (strcmp(path, ".") == 0) {
			which_dir = parent_obj;
		} else {
			/*
			 * Sigh, cast away the const here. FSAL shouldn't be
			 * asking to edit parent handles...
			 */
			const struct proxyv3_obj_handle *const_dir =
				parent_obj->parent;

			which_dir = (struct proxyv3_obj_handle *) const_dir;
		}

		/* Make a copy for the result. */
		struct proxyv3_obj_handle *result_handle =
			proxyv3_alloc_handle(export_handle,
					     &which_dir->fh3,
					     &which_dir->attrs,
					     which_dir->parent,
					     attrs_out);

		if (result_handle == NULL) {
			return fsalstat(ERR_FSAL_FAULT, 0);
		}

		*handle = &result_handle->obj;

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	LOOKUP3args args;
	LOOKUP3res result;
	LOOKUP3resok *resok = &result.LOOKUP3res_u.resok;

	/* The directory is the parent's fh3 handle. */
	args.what.dir = parent_obj->fh3;
	/* @todo Is it actually safe to const cast this away? */
	args.what.name = (char *) path;

	memset(&result, 0, sizeof(result));

	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_LOOKUP,
			      (xdrproc_t) xdr_LOOKUP3args, &args,
			      (xdrproc_t) xdr_LOOKUP3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"LOOKUP3 failed");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "LOOKUP3 failed, got %u", result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	/* We really need the attributes. Fail if we didn't get them. */
	if (!resok->obj_attributes.attributes_follow) {
		/* Clean up, even though we're exiting early. */
		xdr_free((xdrproc_t) xdr_LOOKUP3res, &result);
		LogDebug(COMPONENT_FSAL,
			 "LOOKUP3 didn't return attributes");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	const nfs_fh3 *obj_fh = &resok->object;
	const fattr3 *obj_attrs =
		&resok->obj_attributes.post_op_attr_u.attributes;

	struct proxyv3_obj_handle *result_handle =
		proxyv3_alloc_handle(export_handle,
				     obj_fh,
				     obj_attrs,
				     parent_obj,
				     attrs_out);

	/* At this point, we've copied out the result. Clean up. */
	xdr_free((xdrproc_t) xdr_LOOKUP3res, &result);

	if (result_handle == NULL) {
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	*handle = &result_handle->obj;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Do a GETATTR3 on an NFS fh3.
 *
 * @param fh3 The input fh3.
 * @param attrs_out The resulting attributes.
 *
 * @returns - ERR_FSAL_NO_ERROR on success, an error otherwise.
 */

static fsal_status_t
proxyv3_getattr_from_fh3(struct nfs_fh3 *fh3,
			 struct fsal_attrlist *attrs_out)
{
	GETATTR3args args;
	GETATTR3res result;

	LogDebug(COMPONENT_FSAL,
		 "Doing a getattr on fh3 (%p) with len %" PRIu32,
		 fh3->data.data_val, fh3->data.data_len);

	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 handle is %s", LEN_FH_STR,
			   fh3->data.data_val, fh3->data.data_len);

	args.object.data.data_val = fh3->data.data_val;
	args.object.data.data_len = fh3->data.data_len;

	memset(&result, 0, sizeof(result));

	/* If the call fails for any reason, exit. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_GETATTR,
			      (xdrproc_t) xdr_GETATTR3args, &args,
			      (xdrproc_t) xdr_GETATTR3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			result.status);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* If we didn't get back NFS3_OK, return the appropriate error. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "GETATTR failed. %u",
			 result.status);
		/* If the request wants to know about errors, let them know. */
		if (FSAL_TEST_MASK(attrs_out->request_mask, ATTR_RDATTR_ERR)) {
			FSAL_SET_MASK(attrs_out->valid_mask, ATTR_RDATTR_ERR);
		}

		return nfsstat3_to_fsalstat(result.status);
	}

	if (!fattr3_to_fsalattr(&result.GETATTR3res_u.resok.obj_attributes,
				attrs_out)) {
		/* The callee already complained, just exit. */
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Do a GETATTR3 for an object (see proxyv3_getattr_from_fh3).
 */

static fsal_status_t
proxyv3_getattrs(struct fsal_obj_handle *obj_hdl,
		 struct fsal_attrlist *attrs_out)
{
	struct proxyv3_obj_handle *handle =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "Responding to GETATTR request for handle %p",
		 handle);

	return proxyv3_getattr_from_fh3(&handle->fh3, attrs_out);
}

/**
 * @brief Do a SETATTR3 for an object.
 *
 * @param obj_hdl The object handle.
 * @param bypass Whether to bypass share reservations (ignored, we're v3).
 * @param state Object lock/share state (ignored, MDCACHE handles conflicts).
 * @param attrib_set The attributes to set on the object.
 *
 * @returns - ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_setattr2(struct fsal_obj_handle *obj_hdl,
		 bool bypass /* ignored, since we'll happily "bypass" */,
		 struct state_t *state,
		 struct fsal_attrlist *attrib_set)
{
	struct proxyv3_obj_handle *handle =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	SETATTR3args args;
	SETATTR3res result;

	memset(&result, 0, sizeof(result));

	LogDebug(COMPONENT_FSAL,
		 "Responding to SETATTR request for handle %p",
		 handle);

	if (state != NULL &&
	    (state->state_type != STATE_TYPE_SHARE &&
	     state->state_type != STATE_TYPE_LOCK)) {
		LogDebug(COMPONENT_FSAL,
			 "Asked for a stateful SETATTR2 of type %d. Probably a mistake",
			 state->state_type);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	nfs_fh3 *fh3 = &handle->fh3;

	args.object.data.data_val = fh3->data.data_val;
	args.object.data.data_len = fh3->data.data_len;
	/* NOTE(boulos): Ganesha NFSD handles this above us in nfs3_setattr. */
	args.guard.check = false;
	const bool allow_rawdev = false;

	if (!fsalattr_to_sattr3(attrib_set,
				allow_rawdev,
				&args.new_attributes)) {
		LogWarn(COMPONENT_FSAL,
			"SETATTR3() with invalid attributes");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* If the call fails for any reason, exit. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_SETATTR,
			      (xdrproc_t) xdr_SETATTR3args, &args,
			      (xdrproc_t) xdr_SETATTR3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			result.status);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* If we didn't get back NFS3_OK, return the appropriate error. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "SETATTR failed. %u", result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	/* Must have worked :). */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Do a specialized lookup for the root FH3 of an export via GETATTR3.
 */

fsal_status_t proxyv3_lookup_root(struct fsal_export *export_handle,
				  struct fsal_obj_handle **handle,
				  struct fsal_attrlist *attrs_out)
{
	struct proxyv3_export *export =
		container_of(export_handle, struct proxyv3_export, export);

	nfs_fh3 fh3;
	struct fsal_attrlist tmp_attrs;

	fh3.data.data_val = export->root_handle;
	fh3.data.data_len = export->root_handle_len;

	memset(&tmp_attrs, 0, sizeof(tmp_attrs));
	if (attrs_out != NULL) {
		FSAL_SET_MASK(tmp_attrs.request_mask, attrs_out->request_mask);
	}

	fsal_status_t rc = proxyv3_getattr_from_fh3(&fh3, &tmp_attrs);

	if (FSAL_IS_ERROR(rc)) {
		return rc;
	}

	/* Bundle up the result into a new object handle. */
	struct proxyv3_obj_handle *result_handle =
		proxyv3_alloc_handle(export_handle,
				     &fh3,
				     &tmp_attrs,
				     NULL /* no parent */,
				     attrs_out);

	/* If we couldn't allocate the handle, fail. */
	if (result_handle == NULL) {
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	/* Shove this into our export for future use. */
	export->root_handle_obj = result_handle;
	*handle = &(result_handle->obj);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Given an export and a path, try to perform a lookup.
 *
 * @param export_handle Our per-export handle
 * @param path The path on the export (including the root of the mount).
 * @param handle The output object handle for the result.
 * @param attrs_out The output attributes.
 *
 * @return - ERR_FSAL_NO_ERROR on success. An error code, otherwise.
 */

fsal_status_t proxyv3_lookup_path(struct fsal_export *export_handle,
				  const char *path,
				  struct fsal_obj_handle **handle,
				  struct fsal_attrlist *attrs_out)
{
	struct proxyv3_export *export =
		container_of(export_handle, struct proxyv3_export, export);

	LogDebug(COMPONENT_FSAL, "Looking up path '%s'", path);

	/* Check that the first part of the path matches our root. */
	const char *root_path = CTX_FULLPATH(op_ctx);
	const size_t root_len = strlen(root_path);

	const char *p = path;

	/*  Check that the path matches our root prefix. */
	if (strncmp(path, root_path, root_len) != 0) {
		LogDebug(COMPONENT_FSAL,
			 "path ('%s') doesn't match our root ('%s')",
			 path, root_path);
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	/* The prefix matches our root path, move forward. */
	p += root_len;

	if (*p == '\0') {
		/* Nothing left. Must have been just the root. */
		LogDebug(COMPONENT_FSAL, "Root Lookup. Doing GETATTR instead");
		return proxyv3_lookup_root(export_handle, handle, attrs_out);
	}

	/*
	 * Okay, we've got a potential path with slashes.
	 *
	 * @todo Split up path, calling lookup internal on each part.
	 */

	return proxyv3_lookup_internal(export_handle, p,
				       &export->root_handle_obj->obj,
				       handle, attrs_out);
}

/**
 * @brief Perform a lookup by handle. See proxyv3_lookup_internal.
 */

static fsal_status_t
proxyv3_lookup_handle(struct fsal_obj_handle *parent,
		      const char *path,
		      struct fsal_obj_handle **handle,
		      struct fsal_attrlist *attrs_out)
{
	LogDebug(COMPONENT_FSAL,
		 "lookup_handle for path '%s'", path);
	return proxyv3_lookup_internal(op_ctx->fsal_export, path,
				       parent, handle, attrs_out);
}


/**
 * @brief Issue a CREATE3/MKDIR3/SYMLINK style operation.
 *
 *        This function handles all the "make sure we got back the attributes"
 *        that is sadly optional in the NFS v3 spec.
 *
 * @param parent_obj The parent object.
 * @param nfsProc The NFS RPC to run (e.g., CREATE3).
 * @param procName The NFS RPC as a const char* (for error messages).
 * @param encFunc An XDR encoding function (e.g., xdr_CREATE3args)
 * @param encArgs The argument data (passed to encFunc).
 * @param decFunc The XDR decoding function (e.g., xdr_CREATE3res)
 * @param decArgs The output argument (passed to decFunc).
 * @param status The result nfsstat3 pointer 9inside of decArgs).
 * @param op_fh3 The result post_op_fh3 pointer (inside of decArgs)
 * @param op_attr The result post_op_attr (inside of decArgs).
 * @param new_obj The output argument for the new object.
 * @param attrs_out The output argument for the output attributes.
 *
 * @returns - ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_issue_createlike(struct proxyv3_obj_handle *parent_obj,
			 const rpcproc_t nfsProc, const char *procName,
			 xdrproc_t encFunc, void *encArgs,
			 xdrproc_t decFunc, void *decArgs,
			 nfsstat3 *status,
			 struct post_op_fh3 *op_fh3,
			 struct post_op_attr *op_attr,
			 struct fsal_obj_handle **new_obj,
			 struct fsal_attrlist *attrs_out)
{
	LogDebug(COMPONENT_FSAL,
		 "Issuing a %s", procName);

	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      nfsProc,
			      encFunc, encArgs,
			      decFunc, decArgs)) {
		LogWarn(COMPONENT_FSAL,
			"%s failed", procName);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Okay, let's see what we got. */
	if (*status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "%s failed, got %u", procName, *status);
		return nfsstat3_to_fsalstat(*status);
	}

	/* We need both the handle and attributes to fill in the results. */
	if (!op_attr->attributes_follow ||
	    !op_fh3->handle_follows) {
		/* Since status was NFS3_OK, we may have allocated something. */
		xdr_free(decFunc, decArgs);

		LogDebug(COMPONENT_FSAL,
			 "%s didn't return obj attributes (%s) or handle (%s)",
			 procName,
			 op_attr->attributes_follow ? "T" : "F",
			 op_fh3->handle_follows ? "T" : "F");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	const struct nfs_fh3 *obj_fh = &op_fh3->post_op_fh3_u.handle;
	const fattr3 *obj_attrs = &op_attr->post_op_attr_u.attributes;

	struct proxyv3_obj_handle *result_handle =
		proxyv3_alloc_handle(op_ctx->fsal_export,
				     obj_fh,
				     obj_attrs,
				     parent_obj,
				     attrs_out);

	/* At this point, we've copied out the result. Clean up. */
	xdr_free(decFunc, decArgs);

	if (result_handle == NULL) {
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	*new_obj = &result_handle->obj;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief  Perform an "open" by handle.
 *
 *         This comes from NFSv4 clients and we need to correctly allow it, and
 *         replace the "opens" with either get/setattrs.
 *
 * @returns - ERR_FSAL_NOTSUPP if we're confused.
 *          - Otherwise the result of proxyv3_getattrs.
 */

static fsal_status_t
proxyv3_open_by_handle(struct fsal_obj_handle *obj_hdl,
		       struct state_t *state,
		       fsal_openflags_t openflags,
		       enum fsal_create_mode createmode,
		       struct fsal_attrlist *attrib_set,
		       fsal_verifier_t verifier,
		       struct fsal_obj_handle **out_obj,
		       struct fsal_attrlist *attrs_out,
		       bool *caller_perm_check)
{
	LogDebug(COMPONENT_FSAL,
		 "open2 of obj_hdl %p flags %" PRIx16 " and mode %u",
		 obj_hdl, openflags, createmode);

	if (createmode != FSAL_NO_CREATE) {
		/* They're not trying to open for read/write. */
		LogCrit(COMPONENT_FSAL,
			"Don't know how to do create via handle");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/* Otherwise, this is actually a getattr. */
	*out_obj = obj_hdl;
	return proxyv3_getattrs(obj_hdl, attrs_out);
}



/**
 * @brief Perform an "open" (really CREATE3). See proxyv3_issue_createlike.
 */

static fsal_status_t
proxyv3_open2(struct fsal_obj_handle *obj_hdl,
	      struct state_t *state,
	      fsal_openflags_t openflags,
	      enum fsal_create_mode createmode,
	      const char *name,
	      struct fsal_attrlist *attrib_set,
	      fsal_verifier_t verifier,
	      struct fsal_obj_handle **out_obj,
	      struct fsal_attrlist *attrs_out,
	      bool *caller_perm_check)
{
	/* If name is NULL => open by handle. */
	if (name == NULL) {
		return proxyv3_open_by_handle(obj_hdl,
					      state,
					      openflags,
					      createmode,
					      attrib_set,
					      verifier,
					      out_obj,
					      attrs_out,
					      caller_perm_check);
	}

	struct proxyv3_obj_handle *parent_obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "open2 of obj_hdl %p, name %s with flags %" PRIx16
		 " and mode %u",
		 obj_hdl, name, openflags, createmode);

	/* @todo Do we need to check the openflags, too? */
	if (state != NULL &&
	    (state->state_type != STATE_TYPE_SHARE &&
	     state->state_type != STATE_TYPE_LOCK)) {
		LogCrit(COMPONENT_FSAL,
			"Asked for a stateful open2() of type %d. Probably a mistake",
			state->state_type);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	CREATE3args args;
	CREATE3res result;
	CREATE3resok *resok = &result.CREATE3res_u.resok;

	memset(&result, 0, sizeof(result));

	args.where.dir.data.data_val = parent_obj->fh3.data.data_val;
	args.where.dir.data.data_len = parent_obj->fh3.data.data_len;
	/* We can safely const-cast away, this is an input. */
	args.where.name = (char *) name;

	switch (createmode) {
	case FSAL_NO_CREATE:
		/* No create should have been handled via open_by_handle. */
	case FSAL_EXCLUSIVE_41:
	case FSAL_EXCLUSIVE_9P:
		LogCrit(COMPONENT_FSAL,
			"Invalid createmode (%u) for NFSv3. Must be one of UNCHECKED, GUARDED, or EXCLUSIVE",
			createmode);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	case FSAL_UNCHECKED:
		args.how.mode = UNCHECKED;
		break;
	case FSAL_GUARDED:
		args.how.mode = GUARDED;
		break;
	case FSAL_EXCLUSIVE:
		args.how.mode = EXCLUSIVE;
		break;
	}

	if (createmode == FSAL_EXCLUSIVE) {
		/* Set the verifier */
		memcpy(&args.how.createhow3_u.verf, verifier,
		       sizeof(fsal_verifier_t));
	} else {
		sattr3 *attrs;

		/* Otherwise, set the attributes for the file. */
		if (attrib_set == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Non-exclusive CREATE() without attributes.");
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}


		attrs = &args.how.createhow3_u.obj_attributes;
		const bool allow_rawdev = false;

		if (!fsalattr_to_sattr3(attrib_set, allow_rawdev, attrs)) {
			LogCrit(COMPONENT_FSAL,
				"CREATE() with invalid attributes");
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	/* Issue the CREATE3 call. */
	return proxyv3_issue_createlike(parent_obj,
					NFSPROC3_CREATE, "CREATE3",
					(xdrproc_t) xdr_CREATE3args, &args,
					(xdrproc_t) xdr_CREATE3res, &result,
					&result.status,
					&resok->obj,
					&resok->obj_attributes,
					out_obj,
					attrs_out);
}

/**
 * @brief Make a new symlink from dir/name => link_path.
 */

static fsal_status_t
proxyv3_symlink(struct fsal_obj_handle *dir_hdl,
		const char *name,
		const char *link_path,
		struct fsal_attrlist *attrs_in,
		struct fsal_obj_handle **new_obj,
		struct fsal_attrlist *attrs_out)
{
	LogDebug(COMPONENT_FSAL,
		 "symlink of parent %p, name %s to => %s",
		 dir_hdl, name, link_path);

	SYMLINK3args args;
	SYMLINK3res result;
	SYMLINK3resok *resok = &result.SYMLINK3res_u.resok;

	memset(&result, 0, sizeof(result));

	struct proxyv3_obj_handle *parent_obj =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	args.where.dir.data.data_val = parent_obj->fh3.data.data_val;
	args.where.dir.data.data_len = parent_obj->fh3.data.data_len;
	/* We can safely const-cast away, this is an input. */
	args.where.name = (char *) name;

	if (attrs_in == NULL) {
		LogWarn(COMPONENT_FSAL,
			"symlink called without attributes. Unexpected");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	const bool allow_rawdev = false;

	if (!fsalattr_to_sattr3(attrs_in,
				allow_rawdev,
				&args.symlink.symlink_attributes)) {
		LogWarn(COMPONENT_FSAL,
			"SYMLINK3 with invalid attributes");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Again, we can safely const-cast away, because this is an input. */
	args.symlink.symlink_data = (char *) link_path;

	/* Issue the SYMLINK3 call. */
	return proxyv3_issue_createlike(parent_obj,
					NFSPROC3_SYMLINK, "SYMLINK3",
					(xdrproc_t) xdr_SYMLINK3args, &args,
					(xdrproc_t) xdr_SYMLINK3res, &result,
					&result.status,
					&resok->obj,
					&resok->obj_attributes,
					new_obj,
					attrs_out);
}

/**
 * @brief Make a hardlink from obj => dir/name.
 */

static fsal_status_t
proxyv3_hardlink(struct fsal_obj_handle *obj_hdl,
		 struct fsal_obj_handle *dir_hdl,
		 const char *name)
{
	LogDebug(COMPONENT_FSAL,
		 "(hard)link of object %p to %p/%s",
		 obj_hdl, dir_hdl, name);

	LINK3args args;
	LINK3res result;

	memset(&result, 0, sizeof(result));

	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	struct proxyv3_obj_handle *dir =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	args.file.data.data_val = obj->fh3.data.data_val;
	args.file.data.data_len = obj->fh3.data.data_len;
	args.link.dir.data.data_val = dir->fh3.data.data_val;
	args.link.dir.data.data_len = dir->fh3.data.data_len;
	/* We can safely const-cast away, this is an input. */
	args.link.name = (char *) name;

	/* If the call fails for any reason, exit. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_LINK,
			      (xdrproc_t) xdr_LINK3args, &args,
			      (xdrproc_t) xdr_LINK3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"LINK3 failed");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* If we didn't get back NFS3_OK, leave a debugging note.*/
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "NFSPROC3_LINK failed. %u", result.status);
	}

	return nfsstat3_to_fsalstat(result.status);
}

/**
 * @brief Handle readlink requests.
 */

static fsal_status_t
proxyv3_readlink(struct fsal_obj_handle *obj_hdl,
		 struct gsh_buffdesc *link_content,
		 bool refresh)
{
	LogDebug(COMPONENT_FSAL,
		 "readlink of %p of type %d",
		 obj_hdl, obj_hdl->type);

	READLINK3args args;
	READLINK3res result;

	memset(&result, 0, sizeof(result));

	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	if (obj_hdl->type != SYMBOLIC_LINK) {
		LogCrit(COMPONENT_FSAL,
			"Symlink called with obj %p type %d != symlink (%d)",
			obj_hdl, obj_hdl->type, SYMBOLIC_LINK);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	args.symlink.data.data_val = obj->fh3.data.data_val;
	args.symlink.data.data_len = obj->fh3.data.data_len;

	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_READLINK,
			      (xdrproc_t) xdr_READLINK3args, &args,
			      (xdrproc_t) xdr_READLINK3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"rpc for READLINK3 failed.");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "READLINK3 failed (%u)", result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	/* The result is a char*. */
	link_content->addr = gsh_strdup(result.READLINK3res_u.resok.data);
	link_content->len = strlen(link_content->addr) + 1;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Handle a "close" for a file. See proxyv3_close2.
 */

static fsal_status_t
proxyv3_close(struct fsal_obj_handle *obj_hdl)
{
	LogDebug(COMPONENT_FSAL,
		 "Asking for stateless CLOSE of handle %p. Say its not 'opened'!",
		 obj_hdl);

	return fsalstat(ERR_FSAL_NOT_OPENED, 0);
}


/**
 * @brief Perform a "close" on an object (with optional state).
 *
 *        Since we're an NFSv3 proxy, we don't have anything "open". So we need
 *        to return ERR_FSAL_NOT_OPENED to the layers above us (they try to keep
 *        count of open FDs and such).
 *
 * @param obj_hdl The obj to close.
 * @param state Optional state (used for locking).
 *
 * @return - ERR_FSAL_NOT_OPENED on success.
 *         - ERR_FSAL_NOTSUPP if we're confused.
 */

static fsal_status_t
proxyv3_close2(struct fsal_obj_handle *obj_hdl,
	       struct state_t *state)
{
	LogDebug(COMPONENT_FSAL,
		 "Asking for CLOSE of handle %p (state is %p)",
		 obj_hdl, state);

	if (state != NULL) {
		if (state->state_type == STATE_TYPE_NLM_LOCK ||
		    state->state_type == STATE_TYPE_LOCK) {
			/*
			 * This is a cleanup of our lock. Callers don't seem to
			 * care about the result. Stick with ERR_FSAL_NOT_OPENED
			 * like close().
			 */
			return fsalstat(ERR_FSAL_NOT_OPENED, 0);
		}

		if (state->state_type == STATE_TYPE_SHARE) {
			/* This is a close of a "regular" NFSv4 open. */
			return fsalstat(ERR_FSAL_NOT_OPENED, 0);
		}

		LogWarn(COMPONENT_FSAL,
			"Received unexpected stateful CLOSE with state_type %d",
			state->state_type);

		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/*
	 * Stateless close through the other door, say it's not opened (avoid's
	 * the decref in fsal_close).
	 */
	return fsalstat(ERR_FSAL_NOT_OPENED, 0);
}


/**
 * @brief Issue a MKDIR. See proxyv3_issue_createlike.
 */

static fsal_status_t
proxyv3_mkdir(struct fsal_obj_handle *dir_hdl,
	      const char *name, struct fsal_attrlist *attrs_in,
	      struct fsal_obj_handle **new_obj,
	      struct fsal_attrlist *attrs_out)
{
	struct proxyv3_obj_handle *parent_obj =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "mkdir of %s in parent %p",
		 name, dir_hdl);

	/* In case we fail along the way. */
	*new_obj = NULL;

	MKDIR3args args;
	MKDIR3res result;
	MKDIR3resok *resok = &result.MKDIR3res_u.resok;

	memset(&result, 0, sizeof(result));

	args.where.dir.data.data_val = parent_obj->fh3.data.data_val;
	args.where.dir.data.data_len = parent_obj->fh3.data.data_len;
	args.where.name = (char *) name;

	const bool allow_rawdev = false;

	if (!fsalattr_to_sattr3(attrs_in, allow_rawdev, &args.attributes)) {
		LogWarn(COMPONENT_FSAL,
			"MKDIR() with invalid attributes");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Issue the MKDIR3 call. */
	return proxyv3_issue_createlike(parent_obj,
					NFSPROC3_MKDIR, "MKDIR3",
					(xdrproc_t) xdr_MKDIR3args, &args,
					(xdrproc_t) xdr_MKDIR3res, &result,
					&result.status,
					&resok->obj,
					&resok->obj_attributes,
					new_obj,
					attrs_out);
}

/**
 * @brief Issue a MKNOD. See proxyv3_issue_createlike.
 */

static fsal_status_t
proxyv3_mknode(struct fsal_obj_handle *dir_hdl,
	       const char *name,
	       object_file_type_t nodetype,
	       struct fsal_attrlist *attrs_in,
	       struct fsal_obj_handle **new_obj,
	       struct fsal_attrlist *attrs_out)
{
	struct proxyv3_obj_handle *parent_obj =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "mknod of %s in parent %p (type is %d)",
		 name, dir_hdl, nodetype);

	/* In case we fail along the way, mark the output as NULL. */
	*new_obj = NULL;

	MKNOD3args args;
	MKNOD3res result;
	sattr3 *attrs;
	MKNOD3resok *resok = &result.MKNOD3res_u.resok;

	memset(&result, 0, sizeof(result));

	args.where.dir.data.data_val = parent_obj->fh3.data.data_val;
	args.where.dir.data.data_len = parent_obj->fh3.data.data_len;
	/* Const-cast away is okay here, as it's an input. */
	args.where.name = (char *) name;

	switch (nodetype) {
	case CHARACTER_FILE:
		args.what.type = NF3CHR;
		break;
	case BLOCK_FILE:
		args.what.type = NF3BLK;
		break;
	case SOCKET_FILE:
		args.what.type = NF3SOCK;
		break;
	case FIFO_FILE:
		args.what.type = NF3FIFO;
		break;
	default:
		LogWarn(COMPONENT_FSAL,
			"mknode got invalid MKNOD type %d",
			nodetype);
	}

	switch (nodetype) {
	case CHARACTER_FILE:
	case BLOCK_FILE:
		attrs = &args.what.mknoddata3_u.device.dev_attributes;
		break;
	case SOCKET_FILE:
	case FIFO_FILE:
		attrs = &args.what.mknoddata3_u.pipe_attributes;
		break;
	default:
		/* Unreachable.*/
		attrs = NULL;
		break;
	}

	const bool allow_rawdev = true;

	if (!fsalattr_to_sattr3(attrs_in, allow_rawdev, attrs)) {
		LogWarn(COMPONENT_FSAL,
			"MKNOD() with invalid attributes");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* Issue the MKNODE3 call. */
	return proxyv3_issue_createlike(parent_obj,
					NFSPROC3_MKNOD, "MKNODE3",
					(xdrproc_t) xdr_MKNOD3args, &args,
					(xdrproc_t) xdr_MKNOD3res, &result,
					&result.status,
					&resok->obj,
					&resok->obj_attributes,
					new_obj,
					attrs_out);
}

/**
 * @brief Process the entries from a READDIR3 response.
 */

static fsal_status_t
proxyv3_readdir_process_entries(entryplus3 *entry,
				cookie3 *cookie,
				struct proxyv3_obj_handle *parent_dir,
				fsal_readdir_cb cb,
				void *cbarg,
				attrmask_t attrmask)
{
	int count = 0;
	bool readahead = false;

	/*
	 * Loop over all the entries, making fsal objects from the
	 * results and calling the given callback.
	 */
	for (; entry != NULL; entry = entry->nextentry, count++) {
		struct nfs_fh3 *fh3 =
			&entry->name_handle.post_op_fh3_u.handle;
		post_op_attr *post_op_attr =
			&entry->name_attributes;
		fattr3 *attrs =
			&post_op_attr->post_op_attr_u.attributes;
		struct fsal_attrlist cb_attrs;
		struct proxyv3_obj_handle *result_handle;
		enum fsal_dir_result cb_rc;

		/*
		 * Don't forget to update the cookie, as long as we're
		 * not just doing readahead.
		 */

		if (!readahead) {
			*cookie = entry->cookie;
		}

		if (strcmp(entry->name, ".") == 0 ||
		    strcmp(entry->name, "..") == 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "Skipping special value of '%s'",
				     entry->name);
			continue;
		}


		if (!entry->name_handle.handle_follows) {
			/*
			 * We didn't even get back a handle, so neither fh3 nor
			 * attrs are going to be filled in. NFS clients seem to
			 * issue a LOOKUP3 in response to that, so we'll do the
			 * same (since Ganesha doesn't let us say "no fh3").
			 */

			fsal_status_t rc;
			struct fsal_obj_handle *lookup_handle;
			struct proxyv3_obj_handle *lookup_obj;

			LogFullDebug(COMPONENT_FSAL,
				     "READDIRPLUS didn't return a handle for '%s'. Trying LOOKUP",
				     entry->name);

			rc = proxyv3_lookup_internal(op_ctx->fsal_export,
						     entry->name,
						     &parent_dir->obj,
						     &lookup_handle,
						     NULL /* drop attrs */);

			if (FSAL_IS_ERROR(rc)) {
				LogWarn(COMPONENT_FSAL,
					"Last chance LOOKUP failed for READDIRPLUS entry '%s'",
					entry->name);
				return rc;
			}

			/* Pull the fh3 out of the lookup_handle */
			lookup_obj =
				container_of(lookup_handle,
					     struct proxyv3_obj_handle,
					     obj);

			memcpy(fh3, &lookup_obj->fh3, sizeof(struct nfs_fh3));

			/*
			 * We could use the attrs from the LOOKUP. But we're
			 * also hoping that this code is temporary. So just fall
			 * through and let the last-chance GETATTR below handle
			 * it.
			 */
		}

		if (!entry->name_attributes.attributes_follow) {
			/*
			 * We didn't get back attributes, so attrs is
			 * currently not filled in / filled with
			 * garbage. Let's do an explicit GETATTR as a
			 * last chance.
			 */

			fsal_status_t rc;

			LogFullDebug(COMPONENT_FSAL,
				     "READDIRPLUS didn't return attributes for '%s'. Trying GETATTR",
				     entry->name);

			rc = proxyv3_getattr_from_fh3(fh3, attrs);

			if (FSAL_IS_ERROR(rc)) {
				LogWarn(COMPONENT_FSAL,
					"Last chance GETATTR failed for READDIRPLUS entry '%s'",
					entry->name);
				return rc;
			}
		}

		/*
		 * Tell alloc_handle we just want the requested
		 * attributes.
		 */

		memset(&cb_attrs, 0, sizeof(cb_attrs));
		FSAL_SET_MASK(cb_attrs.request_mask, attrmask);

		result_handle =
			proxyv3_alloc_handle(op_ctx->fsal_export,
					     fh3, attrs, parent_dir,
					     &cb_attrs);

		if (result_handle == NULL) {
			LogCrit(COMPONENT_FSAL,
				"Failed to make a handle for READDIRPLUS result for entry '%s'",
				entry->name);
			return fsalstat(ERR_FSAL_FAULT, 0);
		}

		cb_rc = cb(entry->name,
			   &result_handle->obj,
			   &cb_attrs, cbarg, entry->cookie);

		/*
		 * Other FSALs do this as >= DIR_READAHEAD, but I prefer
		 * an explicit switch with no default.
		 */

		switch (cb_rc) {
		case DIR_CONTINUE:
			/* Next entry. */
			continue;
		case DIR_READAHEAD:
			/* Keep processing the entries we've got. */
			readahead = true;
			continue;
		case DIR_TERMINATE:
			/* Okay, all done. */
			break;
		}
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Do a READDIR3 for a given directory.
 *
 *        Do a READDIR3 for a given directory, calling a callback for each
 *        resulting item. To support listing directories in chunks, the whence
 *        object might be provided, which directs us where to pick up.
 *
 * @param dir_hdl The object handle for the directory.
 * @param whence An optional "start here".
 * @param cbarg Argument passed to the callback.
 * @param cb The readdir callback for each entry.
 * @param attrmask The requested attribute mask.
 * @param eof Output bool saying whether or not we reached the end.
 *
 * @returns - ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_readdir(struct fsal_obj_handle *dir_hdl,
		fsal_cookie_t *whence, void *cbarg,
		fsal_readdir_cb cb, attrmask_t attrmask,
		bool *eof)
{
	struct proxyv3_obj_handle *dir =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	/*
	 * The NFS V3 spec says:
	 *   "This should be set to 0 on the first request to read a directory."
	 */

	cookie3 cookie = (whence == NULL) ? 0 : *whence;

	/*
	 * @todo Ganesha doesn't seem to have any way to pass this in alongside
	 * whence... The comments in the Ganesha NFSD implementation for
	 * READDIRPLUS suggest that most clients just ignore it / expect 0s.
	 */

	cookieverf3 cookie_verf;

	memset(&cookie_verf, 0, sizeof(cookie_verf));

	LogDebug(COMPONENT_FSAL,
		 "Doing READDIR for dir %p (cookie = %" PRIu64 ")",
		 dir, cookie);

	/* Check that attrmask is at most NFSv3 */
	if (!attrmask_is_nfs3(attrmask)) {
		LogWarn(COMPONENT_FSAL,
			"readdir asked for incompatible output attrs");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	*eof = false;

	while (!(*eof)) {
		/* @todo Move this entire block to a helper function. */
		READDIRPLUS3args args;
		READDIRPLUS3res result;
		fsal_status_t rc;

		memset(&result, 0, sizeof(result));

		args.dir.data.data_val = dir->fh3.data.data_val;
		args.dir.data.data_len = dir->fh3.data.data_len;
		args.cookie = cookie;
		memcpy(&args.cookieverf, &cookie_verf, sizeof(args.cookieverf));
		/*
		 * We need to let the server know how much data to return per
		 * chunk. The V4 proxy uses 4KB and 16KB, but we should have
		 * picked up the preferred amount from fsinfo. Use that for both
		 * the dircount (we'll read all the data) and maxcount.
		 */

		args.dircount = args.maxcount = proxyv3_readdir_preferred();

		LogFullDebug(COMPONENT_FSAL,
			     "Calling READDIRPLUS with cookie %" PRIu64,
			     cookie);

		xdrproc_t encFunc = (xdrproc_t) xdr_READDIRPLUS3args;
		xdrproc_t decFunc = (xdrproc_t) xdr_READDIRPLUS3res;

		if (!proxyv3_nfs_call(proxyv3_sockaddr(),
				      proxyv3_socklen(),
				      proxyv3_nfsd_port(),
				      proxyv3_creds(),
				      NFSPROC3_READDIRPLUS,
				      encFunc, &args,
				      decFunc, &result)) {
			LogWarn(COMPONENT_FSAL,
				"proxyv3_nfs_call for READDIRPLUS failed (%u)",
				result.status);
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}

		if (result.status != NFS3_OK) {
			LogDebug(COMPONENT_FSAL,
				 "READDIRPLUS failed. %u",
				 result.status);
			return nfsstat3_to_fsalstat(result.status);
		}

		LogFullDebug(COMPONENT_FSAL,
			     "READDIRPLUS succeeded, looping over dirents");


		READDIRPLUS3resok *resok = &result.READDIRPLUS3res_u.resok;
		/* Mark EOF now, if true. */
		*eof = resok->reply.eof;
		/* Update the cookie verifier for the next iteration. */
		memcpy(&cookie_verf, &resok->cookieverf, sizeof(cookie_verf));

		/* Lookup over the entries, calling our callback for each. */
		rc = proxyv3_readdir_process_entries(resok->reply.entries,
						     &cookie, dir,
						     cb, cbarg, attrmask);

		/* Cleanup any memory that READDIRPLUS3res allocated for us. */
		xdr_free(decFunc, &result);

		if (FSAL_IS_ERROR(rc))
			return rc;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Handle a read from `obj_hdl` at offset read_arg->offset.
 *
 *        Handle a read via READ3. When we're done, let done_cb know.
 *
 * @param obj_hdl The object for reading.
 * @param bypass Whether to bypass shares/delegations (ignored)
 * @param done_cb The callback for when we're done reading.
 * @param read_arg The offset and lengths to read.
 * @param cb_arg The additional arguments passed to done_cb.
 *
 */

static void
proxyv3_read2(struct fsal_obj_handle *obj_hdl,
	      bool bypass /* unused */,
	      fsal_async_cb done_cb,
	      struct fsal_io_arg *read_arg,
	      void *cb_arg)
{
	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "Doing read2 at offset %" PRIu64 " in handle %p of len %zu",
		 read_arg->offset, obj_hdl, read_arg->iov[0].iov_len);

	/* Signal that we've read 0 bytes. */
	read_arg->io_amount = 0;

	/* Like Ceph, we don't handle READ_PLUS. */
	if (read_arg->info != NULL) {
		LogCrit(COMPONENT_FSAL,
			"Got a READPLUS request. Not supported");
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0),
			read_arg, cb_arg);
		return;
	}

	/*
	 * Since we're just a V3 proxy, we are stateless. If we get an actually
	 * stateful request, something bad must have happened.
	 */

	if (read_arg->state != NULL &&
	    (read_arg->state->state_type != STATE_TYPE_SHARE &&
	     read_arg->state->state_type != STATE_TYPE_LOCK)) {
		LogCrit(COMPONENT_FSAL,
			"Got a stateful READ w/ type %d. Not supported",
			read_arg->state->state_type);
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0),
			read_arg, cb_arg);
		return;
	}

	/*
	 * NOTE(boulos): NFSv4 (and therefore Ganesha) doesn't actually have a
	 * useful readv() equivalent, since it only allows a single offset
	 * (read_arg->offset), so read2 implementations can only fill in
	 * different amounts at an offset. NFSv3 doesn't have a readv()
	 * equivalent, and Ganesha's NFSD won't generate it from clients anyway,
	 * but warn here.
	 */

	if (read_arg->iov_count > 1) {
		LogCrit(COMPONENT_FSAL,
			"Got asked for multiple reads at once. Unsupported.");
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0),
			read_arg, cb_arg);
		return;
	}

	char *dst = read_arg->iov[0].iov_base;
	uint64_t offset = read_arg->offset;
	size_t bytes_to_read = read_arg->iov[0].iov_len;

	/*
	 * @todo Maybe check / clamp read size against maxRead (but again,
	 * Ganesha's NFSD layer will have already done so).
	 */

	READ3args args;
	READ3res result;
	READ3resok *resok = &result.READ3res_u.resok;

	memset(&result, 0, sizeof(result));

	args.file.data.data_val = obj->fh3.data.data_val;
	args.file.data.data_len = obj->fh3.data.data_len;
	args.offset = offset;
	args.count = bytes_to_read;

	/*
	 * Setup the resok struct to fill in bytes on success. This avoids an
	 * unnecessary allocation (on xdr_decode of the READ3res) and memcpy
	 * afterwards.
	 */
	resok->data.data_val = dst;
	resok->data.data_len = bytes_to_read;

	/* Issue the read. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_READ,
			      (xdrproc_t) xdr_READ3args, &args,
			      (xdrproc_t) xdr_READ3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			result.status);
		done_cb(obj_hdl, fsalstat(ERR_FSAL_SERVERFAULT, 0),
			read_arg, cb_arg);
		return;
	}

	/* If the read failed, tell the callback about the error. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "READ failed: %u", result.status);
		done_cb(obj_hdl, nfsstat3_to_fsalstat(result.status),
			read_arg, cb_arg);
		return;
	}

	/*
	 * NOTE(boulos): data_len is not part of the NFS spec, but Ganesha
	 * should be getting the same number of bytes in the result.
	 */
	if (resok->count != resok->data.data_len) {
		LogCrit(COMPONENT_FSAL,
			"read of len %" PRIu32 " (resok.count) != %" PRIu32,
			resok->count, resok->data.data_len);
		done_cb(obj_hdl, fsalstat(ERR_FSAL_SERVERFAULT, 0),
			read_arg, cb_arg);
		return;
	}

	/* We already filled in the actual bytes by setting up resok.data */
	read_arg->end_of_file = resok->eof;
	read_arg->io_amount = resok->count;

	/* Let the caller know that we're done. */
	done_cb(obj_hdl, fsalstat(ERR_FSAL_NO_ERROR, 0), read_arg, cb_arg);
}


/**
 * @brief Handle a write to a given object. See also proxyv3_read2.
 */

static void
proxyv3_write2(struct fsal_obj_handle *obj_hdl,
	       bool bypass /* unused */,
	       fsal_async_cb done_cb,
	       struct fsal_io_arg *write_arg,
	       void *cb_arg)
{
	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "Doing write2 at offset %" PRIu64 " in handle %p of len %zu",
		 write_arg->offset, obj_hdl, write_arg->iov[0].iov_len);

	/* Signal that we've written 0 bytes so far. */
	write_arg->io_amount = 0;

	/* If info is only for READPLUS, it should definitely be NULL. */
	if (write_arg->info != NULL) {
		LogCrit(COMPONENT_FSAL,
			"Write had 'readplus' info. Something went wrong");
		done_cb(obj_hdl, fsalstat(ERR_FSAL_SERVERFAULT, 0),
			write_arg, cb_arg);
		return;
	}

	/*
	 * Since we're just a V3 proxy, we are stateless. If we get an actually
	 * stateful request, something bad must have happened.
	 */
	if (write_arg->state != NULL &&
	    (write_arg->state->state_type != STATE_TYPE_SHARE &&
	     write_arg->state->state_type != STATE_TYPE_LOCK)) {
		LogCrit(COMPONENT_FSAL,
			"Got a stateful WRITE of type %d. Not supported",
			write_arg->state->state_type);
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0),
			write_arg, cb_arg);
		return;
	}

	/*
	 * NOTE(boulos): NFSv4 and therefore Ganesha doesn't actually have a
	 * useful writev() equivalent, since it only allows a single offset
	 * (write_arg->offset), so write2 implementations can just uselessly
	 * fill in different amounts at an offset. NFSv3 doesn't have a writev()
	 * equivalent, and Ganesha's NFSD won't generate it from clients anyway,
	 * but warn here.
	 */
	if (write_arg->iov_count > 1) {
		LogCrit(COMPONENT_FSAL,
			"Got asked for multiple writes at once. Unsupported.");
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0),
			write_arg, cb_arg);
		return;
	}

	char *src = write_arg->iov[0].iov_base;
	uint64_t offset = write_arg->offset;
	size_t bytes_to_write = write_arg->iov[0].iov_len;

	/*
	 * @todo Check/clamp write size against maxWrite (but again, Ganesha's
	 * NFSD layer will have already done so).
	 */

	WRITE3args args;
	WRITE3res result;
	WRITE3resok *resok = &result.WRITE3res_u.resok;

	memset(&result, 0, sizeof(result));

	args.file.data.data_val = obj->fh3.data.data_val;
	args.file.data.data_len = obj->fh3.data.data_len;
	args.offset = offset;
	args.count = bytes_to_write;
	args.data.data_len = bytes_to_write;
	args.data.data_val = src;

	/*
	 * If the request is for a stable write, ask for FILE_SYNC (rather than
	 * just DATA_SYNC), like nfs3_write.c does.
	 */

	args.stable = (write_arg->fsal_stable) ? FILE_SYNC : UNSTABLE;

	/* Issue the write. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_WRITE,
			      (xdrproc_t) xdr_WRITE3args, &args,
			      (xdrproc_t) xdr_WRITE3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			result.status);
		done_cb(obj_hdl, fsalstat(ERR_FSAL_SERVERFAULT, 0),
			write_arg, cb_arg);
		return;
	}

	/* If the write failed, tell the callback about the error. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "WRITE failed: %u", result.status);
		done_cb(obj_hdl, nfsstat3_to_fsalstat(result.status),
			write_arg, cb_arg);
		return;
	}

	/* Signal that we wrote resok->count bytes. */
	write_arg->io_amount = resok->count;

	/* Let the caller know that we're done. */
	done_cb(obj_hdl, fsalstat(ERR_FSAL_NO_ERROR, 0), write_arg, cb_arg);
}

/**
 * @brief Handle COMMIT requests.
 */

static fsal_status_t
proxyv3_commit2(struct fsal_obj_handle *obj_hdl,
		off_t offset,
		size_t len)
{
	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "Doing commit at offset %" PRIu64 " in handle %p of len %zu",
		 offset, obj_hdl, len);
	COMMIT3args args;
	COMMIT3res result;

	memset(&result, 0, sizeof(result));

	args.file.data.data_val = obj->fh3.data.data_val;
	args.file.data.data_len = obj->fh3.data.data_len;
	args.offset = offset;
	args.count = len;

	/* Issue the COMMIT. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_COMMIT,
			      (xdrproc_t) xdr_COMMIT3args, &args,
			      (xdrproc_t) xdr_COMMIT3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			result.status);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* If the commit failed, report the error upwards. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "COMMIT failed: %u", result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	/* Commit happened, no problems to report. */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Handle REMOVE3/RMDIR3 requests.
 */

static fsal_status_t
proxyv3_unlink(struct fsal_obj_handle *dir_hdl,
	       struct fsal_obj_handle *obj_hdl,
	       const char *name)
{
	struct proxyv3_obj_handle *dir =
		container_of(dir_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "REMOVE request for dir %p of %s %s",
		 dir_hdl,
		 (obj_hdl->type == DIRECTORY) ? "directory" : "file",
		 name);

	/*
	 * NOTE(boulos): While the NFSv3 spec says:
	 *
	 *  In general, REMOVE is intended to remove non-directory file objects
	 *  and RMDIR is to be used to remove directories.  However, REMOVE can
	 *  be used to remove directories, subject to restrictions imposed by
	 *  either the client or server interfaces."
	 *
	 *  It seems that in practice, Linux's kNFSd at least does not go in for
	 *  using REMOVE3 for directories and returns NFS3_ISDIR.
	 */

	bool is_rmdir = obj_hdl->type == DIRECTORY;

	REMOVE3args regular_args;
	REMOVE3res regular_result;

	RMDIR3args dir_args;
	RMDIR3res dir_result;

	diropargs3 *diropargs = (is_rmdir) ?
		&dir_args.object : &regular_args.object;

	memset(&regular_result, 0, sizeof(regular_result));
	memset(&dir_result, 0, sizeof(dir_result));

	diropargs->dir.data.data_val = dir->fh3.data.data_val;
	diropargs->dir.data.data_len = dir->fh3.data.data_len;
	diropargs->name = (char *) name;

	rpcproc_t method = (is_rmdir) ? NFSPROC3_RMDIR : NFSPROC3_REMOVE;
	xdrproc_t enc = (is_rmdir) ? (xdrproc_t) xdr_RMDIR3args :
		(xdrproc_t) xdr_REMOVE3args;
	xdrproc_t dec = (is_rmdir) ? (xdrproc_t) xdr_RMDIR3res :
		(xdrproc_t) xdr_REMOVE3res;

	void *args   = (is_rmdir) ?
		(void *) &dir_args : (void *) &regular_args;
	void *result = (is_rmdir) ?
		(void *) &dir_result : (void *) &regular_result;

	nfsstat3 *status = (is_rmdir) ?
		&dir_result.status : &regular_result.status;

	/* Issue the REMOVE. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      method,
			      enc, args,
			      dec, result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call failed (%u)",
			*status);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* If the REMOVE/RMDIR failed, report the error upwards. */
	if (*status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "%s failed: %u",
			 (is_rmdir) ? "RMDIR" : "REMOVE",
			 *status);
		return nfsstat3_to_fsalstat(*status);
	}

	/* Remove happened, no problems to report. */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Ask to rename obj_hdl from olddir/old_name to newdir/new_name.
 */

static fsal_status_t
proxyv3_rename(struct fsal_obj_handle *obj_hdl,
	       struct fsal_obj_handle *olddir_hdl,
	       const char *old_name,
	       struct fsal_obj_handle *newdir_hdl,
	       const char *new_name)
{
	LogDebug(COMPONENT_FSAL,
		 "Rename of obj %p which is at %p/%s => %p/%s",
		 obj_hdl, olddir_hdl, old_name, newdir_hdl, new_name);

	RENAME3args args;
	RENAME3res result;

	memset(&result, 0, sizeof(result));

	struct proxyv3_obj_handle *old_dir =
		container_of(olddir_hdl, struct proxyv3_obj_handle, obj);

	struct proxyv3_obj_handle *new_dir =
		container_of(newdir_hdl, struct proxyv3_obj_handle, obj);

	args.from.dir.data.data_val = old_dir->fh3.data.data_val;
	args.from.dir.data.data_len = old_dir->fh3.data.data_len;
	args.from.name = (char *) old_name;

	args.to.dir.data.data_val = new_dir->fh3.data.data_val;
	args.to.dir.data.data_len = new_dir->fh3.data.data_len;
	args.to.name = (char *) new_name;

	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_RENAME,
			      (xdrproc_t) xdr_RENAME3args, &args,
			      (xdrproc_t) xdr_RENAME3res, &result))  {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call for RENAME failed");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "Rename failed! Got %d", result.status);
	}

	return nfsstat3_to_fsalstat(result.status);
}


/**
 * @brief Do an FSSTAT on an object in our export, and fill in infop.
 *
 * @param export_handle The fsal_export pointer (to us, a PROXY V3). Unused.
 * @param obj_hdl The fsal object handle enclosed by our proxyv3_obj_handle.
 * @param infop The output fsal_dynamicfsinfo_t struct.
 *
 * @return ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_get_dynamic_info(struct fsal_export *export_handle,
			 struct fsal_obj_handle *obj_hdl,
			 fsal_dynamicfsinfo_t *infop)
{
	struct proxyv3_obj_handle *obj =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	FSSTAT3args args;
	FSSTAT3res result;

	args.fsroot.data.data_val = obj->fh3.data.data_val;
	args.fsroot.data.data_len = obj->fh3.data.data_len;

	memset(&result, 0, sizeof(result));
	/* If the call fails for any reason, exit. */
	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_FSSTAT,
			      (xdrproc_t) xdr_FSSTAT3args, &args,
			      (xdrproc_t) xdr_FSSTAT3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"proxyv3_nfs_call for FSSTAT3 failed (%u)",
			result.status);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* If we didn't get back NFS3_OK, return the appropriate error. */
	if (result.status != NFS3_OK) {
		LogDebug(COMPONENT_FSAL,
			 "FSSTAT3 failed. %u",
			 result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	infop->total_bytes = result.FSSTAT3res_u.resok.tbytes;
	infop->free_bytes = result.FSSTAT3res_u.resok.fbytes;
	infop->avail_bytes = result.FSSTAT3res_u.resok.abytes;
	infop->total_files = result.FSSTAT3res_u.resok.tfiles;
	infop->free_files = result.FSSTAT3res_u.resok.ffiles;
	infop->avail_files = result.FSSTAT3res_u.resok.afiles;
	/*
	 * maxread/maxwrite are *static* not dynamic info, we picked them up on
	 * export init.
	 */
	/* time_delta should actually come from an FSINFO call which has
	 * a timespec for time_delta, HOWEVER, the kernel NFS server
	 * just reports 1 sec for time_delta which is proving to cause a
	 * problem for some clients. So we are just going to hard code for now.
	 */
	infop->time_delta.tv_sec = 0;
	infop->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief "Convert" from our handle to an on-the-wire buffer.
 *
 *        We use FH3s as our "handles", so this function just takes the fh3 from
 *        the object handle and copies it into the fh_desc output.
 *
 * @param obj_hdl The input fsal object handle.
 * @param output_type The type of digest requested (NFSv4 or NFSv3). Ignored.
 * @param fh_desc The output file handle description (len and buf).
 *
 * @return ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_handle_to_wire(const struct fsal_obj_handle *obj_hdl,
		       fsal_digesttype_t output_type,
		       struct gsh_buffdesc *fh_desc)
{
	struct proxyv3_obj_handle *handle =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	if (fh_desc == NULL) {
		LogCrit(COMPONENT_FSAL,
			"received null output buffer");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	LogDebug(COMPONENT_FSAL,
		 "handle_to_wire %p, with len %" PRIu32,
		 handle->fh3.data.data_val, handle->fh3.data.data_len);
	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 value is %s", LEN_FH_STR,
			   handle->fh3.data.data_val,
			   handle->fh3.data.data_len);

	size_t len = handle->fh3.data.data_len;
	const char *bytes = handle->fh3.data.data_val;

	/* Make sure the output buffer can handle our filehandle. */
	if (fh_desc->len < len) {
		LogCrit(COMPONENT_FSAL,
			"not given enough buffer (%zu) for fh (%zu)",
			fh_desc->len, len);
		return fsalstat(ERR_FSAL_TOOSMALL, 0);
	}

	memcpy(fh_desc->addr, bytes, len);
	fh_desc->len = len;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief "Convert" from the on-the-wire format to FSAL.
 *
 *        We use FH3s as our "handles", so this function just checks that the
 *        requested handle is representable in NFSv3 (i.e., that fh_desc->len
 *        fits within NFS3_FHSIZE).
 *
 * @param export_handle The fsal_export pointer (to us, a PROXY V3). Unused.
 * @param in_type The type of digest requested (NFSv4 or NFSv3). Ignored.
 * @param fh_desc The file handle description (len and buf).
 * @param flags Unused.
 *
 * @return ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_wire_to_host(struct fsal_export *export_handle,
		     fsal_digesttype_t in_type,
		     struct gsh_buffdesc *fh_desc,
		     int flags)
{
	if (fh_desc == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Got NULL input pointers");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}


	LogDebug(COMPONENT_FSAL,
		 "wire_to_host of %p, with len %zu",
		 fh_desc->addr, fh_desc->len);

	if (fh_desc->addr == NULL) {
		LogCrit(COMPONENT_FSAL,
			"wire_to_host received NULL address");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 handle is %s", LEN_FH_STR,
			   fh_desc->addr, fh_desc->len);

	if (fh_desc->len > NFS3_FHSIZE) {
		LogCrit(COMPONENT_FSAL,
			"wire_to_host: handle that is too long for NFSv3");
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* fh_desc->addr and fh_desc->len are already the nfs_fh3 we want. */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a new fsal_obj_handle from a given key (hdl_desc).
 *
 * @param export_handle The fsal_export pointer (to us, a PROXY V3).
 * @param hdl_desc A buffer and length from wire_to_host (an fh3).
 * @param handle Output param for new object handle.
 * @param attrs_out Optional file attributes.
 *
 * @return ERR_FSAL_NO_ERROR on success, an error code otherwise.
 */

static fsal_status_t
proxyv3_create_handle(struct fsal_export *export_handle,
		      struct gsh_buffdesc *hdl_desc,
		      struct fsal_obj_handle **handle,
		      struct fsal_attrlist *attrs_out)
{
	struct nfs_fh3 fh3;

	LogDebug(COMPONENT_FSAL,
		 "Creating handle from %p with len %zu",
		 hdl_desc->addr, hdl_desc->len);

	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 handle is %s", LEN_FH_STR,
			   hdl_desc->addr, hdl_desc->len);

	/* In case we die along the way. */
	*handle = NULL;

	fh3.data.data_val = hdl_desc->addr;
	fh3.data.data_len = hdl_desc->len;

	struct fsal_attrlist tmp_attrs;

	memset(&tmp_attrs, 0, sizeof(tmp_attrs));
	if (attrs_out != NULL) {
		FSAL_SET_MASK(tmp_attrs.request_mask, attrs_out->request_mask);
	}

	fsal_status_t rc = proxyv3_getattr_from_fh3(&fh3, &tmp_attrs);

	if (FSAL_IS_ERROR(rc)) {
		return rc;
	}

	/* Bundle up the result into a new object handle. */
	struct proxyv3_obj_handle *result_handle =
		proxyv3_alloc_handle(export_handle,
				     &fh3,
				     &tmp_attrs,
				     NULL /* don't have parent info */,
				     attrs_out);

	/* If we couldn't allocate the handle, fail. */
	if (result_handle == NULL) {
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	*handle = &(result_handle->obj);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief "Convert" an fsal_obj_handle to an MDCACHE key.
 *
 * @param obj_hdl The input object handle.
 * @param fh_desc The output key description (in our case an fh3).
 */

static void
proxyv3_handle_to_key(struct fsal_obj_handle *obj_hdl,
		      struct gsh_buffdesc *fh_desc)
{
	struct proxyv3_obj_handle *handle =
		container_of(obj_hdl, struct proxyv3_obj_handle, obj);

	LogDebug(COMPONENT_FSAL,
		 "handle to key for %p", handle);

	if (fh_desc == NULL) {
		LogCrit(COMPONENT_FSAL,
			"received null output buffer");
		return;
	}

	LogFullDebugOpaque(COMPONENT_FSAL, " fh3 handle is %s", LEN_FH_STR,
			   handle->fh3.data.data_val,
			   handle->fh3.data.data_len);

	fh_desc->addr = handle->fh3.data.data_val;
	fh_desc->len = handle->fh3.data.data_len;
}


/**
 * @brief Fill in fs_info state for our export for a given file handle.
 *
 * @param fh3 The NFS v3 file handle.
 *
 * @return - fsal_status_t result of the FSINFO operation.
 */

static fsal_status_t
proxyv3_fill_fsinfo(nfs_fh3 *fh3)
{
	/*
	 * Issue an FSINFO to ask the server about its max read/write sizes.
	 */

	FSINFO3args args;
	FSINFO3res result;
	FSINFO3resok *resok = &result.FSINFO3res_u.resok;
	fsal_staticfsinfo_t *fsinfo = &PROXY_V3.module.fs_info;
	struct proxyv3_export *export =
		container_of(op_ctx->fsal_export,
			     struct proxyv3_export, export);

	memcpy(&args.fsroot, fh3, sizeof(*fh3));
	memset(&result, 0, sizeof(result));

	if (!proxyv3_nfs_call(proxyv3_sockaddr(),
			      proxyv3_socklen(),
			      proxyv3_nfsd_port(),
			      proxyv3_creds(),
			      NFSPROC3_FSINFO,
			      (xdrproc_t) xdr_FSINFO3args, &args,
			      (xdrproc_t) xdr_FSINFO3res, &result)) {
		LogWarn(COMPONENT_FSAL,
			"FSINFO failed");
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	if (result.status != NFS3_OK) {
		/* Okay, let's see what we got. */
		LogDebug(COMPONENT_FSAL,
			 "FSINFO failed, got %u", result.status);
		return nfsstat3_to_fsalstat(result.status);
	}

	LogDebug(COMPONENT_FSAL,
		 "FSINFO3 returned maxread %" PRIu32
		 "maxwrite %" PRIu32 " maxfilesize %" PRIu64,
		 resok->rtmax, resok->wtmax, resok->maxfilesize);

	/*
	 * Lower any values we need to. NOTE(boulos): The export manager code
	 * reads fsinfo->maxread/maxwrite/maxfilesize, but the *real* values are
	 * the op_ctx->ctx_export->MaxRead/MaxWrite/PrefRead/PrefWrite fields
	 * (which it feels gross to go writing into...).
	 */

	if (resok->rtmax != 0 && fsinfo->maxread > resok->rtmax) {
		LogWarn(COMPONENT_FSAL,
			"Changing maxread from %" PRIu64 " to %" PRIu32,
			fsinfo->maxread, resok->rtmax);
		fsinfo->maxread = resok->rtmax;
	}

	if (resok->wtmax != 0 && fsinfo->maxwrite > resok->wtmax) {
		LogWarn(COMPONENT_FSAL,
			"Reducing maxwrite from %" PRIu64 " to %" PRIu32,
			fsinfo->maxwrite, resok->wtmax);
		fsinfo->maxwrite = resok->wtmax;
	}

	if (resok->maxfilesize != 0 &&
	    fsinfo->maxfilesize > resok->maxfilesize) {
		LogWarn(COMPONENT_FSAL,
			"SKIPPING: Asked to change maxfilesize from %" PRIu64
			" to %" PRIu64,
			fsinfo->maxfilesize, resok->maxfilesize);

		/*
		 * NOTE(boulos): nlm_util tries to enforce the NFSv4 "offset +
		 * length" > UINT_64_MAX => error but nothing else. This is best
		 * described in the description of the LOCK op in NFSv4 in RFC
		 * 5661, Section 18.10.3
		 * (https://tools.ietf.org/html/rfc5661#section-18.10.3). The
		 * change to Ganesha's behavior was introduced in c811fe9323,
		 * and means that if you set maxfilesize to what the backend
		 * NFSD reports, we'll incorrectly fail various Lock requests as
		 * NLM4_FBIG.
		 */

		/*
		 * @todo Fix the ganesha handling of maxfilesize if
		 * possible, by having a separate concept of "the maximum thing
		 * I could ever support" (which isn't maxfilesize) and "the
		 * maximum thing my export supports" (which might have
		 * restrictions). dang@redhat.com worked around this for the
		 * NFSv4 handlers in 3d069bf, but didn't do the same for
		 * nlm_util.
		 */

		/* fsinfo->maxfilesize = resok->maxfilesize; */
	}

	/* Pickup the preferred maxcount parameter for READDIR. */
	if (resok->dtpref != 0) {
		LogDebug(COMPONENT_FSAL,
			 "Setting dtpref to %" PRIu32 " based on fsinfo result",
			 resok->dtpref);
		export->params.readdir_preferred = resok->dtpref;
	}

	/* Check that our assumptions about are true (or warn loudly). */
	if ((resok->properties & FSF3_LINK) == 0) {
		LogWarn(COMPONENT_FSAL,
			"FSINFO says this backend doesn't support hard links");
	}

	if ((resok->properties & FSF3_SYMLINK) == 0) {
		LogWarn(COMPONENT_FSAL,
			"FSINFO says this backend doesn't support symlinks");
	}

	if ((resok->properties & FSF3_HOMOGENEOUS) == 0) {
		LogWarn(COMPONENT_FSAL,
			"FSINFO says this backend is not homogeneous");
	}

	if ((resok->properties & FSF3_CANSETTIME) == 0) {
		LogWarn(COMPONENT_FSAL,
			"FSINFO says this backend cannot set time in setattr");
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


/**
 * @brief Create a PROXY_V3 export.
 *
 * @param fsal_handle The fsal_module (currently unused).
 * @param parse_node The input config data from parsing.
 * @param error_type An output argument for load_config_from_node.
 * @param up_ops The input up_ops for upcalls.
 *
 * @return - ERR_FSAL_NO_ERROR and a mounted NFSv3 backend on success.
 *         - An error status otherwise (e.g., ERR_FSAL_INVAL).
 */

static fsal_status_t
proxyv3_create_export(struct fsal_module *fsal_handle,
		      void *parse_node,
		      struct config_error_type *error_type,
		      const struct fsal_up_vector *up_ops)
{
	struct proxyv3_export *export = gsh_calloc(1, sizeof(*export));
	int ret;

	/* NOTE(boulos): fsal_export_init sets the export ops to defaults. */
	fsal_export_init(&export->export);

	/* Set the export functions we know how to handle. */
	export->export.exp_ops.lookup_path = proxyv3_lookup_path;
	export->export.exp_ops.get_fs_dynamic_info = proxyv3_get_dynamic_info;
	export->export.exp_ops.wire_to_host = proxyv3_wire_to_host;
	export->export.exp_ops.create_handle = proxyv3_create_handle;

	/*
	 * Try to load the config. If it fails (say they didn't provide
	 * Srv_Addr), exit early and free the allocated export.
	 */
	ret = load_config_from_node(parse_node,
				    &proxyv3_export_param,
				    &export->params,
				    true,
				    error_type);
	if (ret != 0) {
		LogCrit(COMPONENT_FSAL,
			"Bad params for export %s",
			CTX_FULLPATH(op_ctx));
		gsh_free(export);
		return fsalstat(ERR_FSAL_INVAL, ret);
	}

	export->export.fsal = fsal_handle;
	export->export.up_ops = up_ops;
	op_ctx->fsal_export = &export->export;

	/*
	 * Attempt to "attach" our FSAL to the export. (I think this just always
	 * works...).
	 */
	ret = fsal_attach_export(fsal_handle, &export->export.exports);
	if (ret != 0) {
		LogCrit(COMPONENT_FSAL,
			"Failed to attach export %s",
			CTX_FULLPATH(op_ctx));
		gsh_free(export);
		return fsalstat(ERR_FSAL_INVAL, ret);
	}

	/* Setup the pointer and socklen arguments. */
	sockaddr_t *sockaddr = &export->params.srv_addr;

	export->params.sockaddr = (struct sockaddr *) sockaddr;
	if (sockaddr->ss_family == AF_INET) {
		export->params.socklen = sizeof(struct sockaddr_in);
	} else {
		export->params.socklen = sizeof(struct sockaddr_in6);
	}

	/* String-ify the "name" for debugging statements. */
	struct display_buffer dspbuf = {
		sizeof(export->params.sockname),
		export->params.sockname,
		export->params.sockname
	};

	display_sockaddr(&dspbuf, &export->params.srv_addr);

	LogDebug(COMPONENT_FSAL,
		 "Got sockaddr %s", export->params.sockname);

	u_int mountd_port = 0;
	u_int nfsd_port = 0;
	u_int nlm_port = 0;

	if (!proxyv3_find_ports(proxyv3_sockaddr(),
				proxyv3_socklen(),
				&mountd_port,
				&nfsd_port,
				&nlm_port)) {
		LogDebug(COMPONENT_FSAL,
			 "Failed to find mountd/nfsd/nlm, oh well");
	}
	/* Copy into our param struct. */
	export->params.mountd_port = mountd_port;
	export->params.nfsd_port = nfsd_port;
	export->params.nlm_port = nlm_port;

	mnt3_dirpath dirpath = CTX_FULLPATH(op_ctx);
	mountres3 result;

	memset(&result, 0, sizeof(result));

	LogDebug(COMPONENT_FSAL,
		 "Going to try to issue a NULL MOUNT at %s",
		 proxyv3_sockname());

	/* Be nice and try a MOUNT NULL first. */
	if (!proxyv3_mount_call(proxyv3_sockaddr(),
				proxyv3_socklen(),
				proxyv3_mountd_port(),
				proxyv3_creds(),
				MOUNTPROC3_NULL,
				(xdrproc_t) xdr_void, NULL,
				(xdrproc_t) xdr_void, NULL)) {
		LogCrit(COMPONENT_FSAL,
			"proxyv3_mount_call for NULL failed");
		gsh_free(export);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	LogDebug(COMPONENT_FSAL,
		 "Going to try to mount '%s' on %s",
		 dirpath, proxyv3_sockname());

	if (!proxyv3_mount_call(proxyv3_sockaddr(),
				proxyv3_socklen(),
				proxyv3_mountd_port(),
				proxyv3_creds(),
				MOUNTPROC3_MNT,
				(xdrproc_t) xdr_dirpath, &dirpath,
				(xdrproc_t) xdr_mountres3, &result)) {
		LogCrit(COMPONENT_FSAL,
			"proxyv3_mount_call for path '%s' failed", dirpath);
		gsh_free(export);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	if (result.fhs_status != MNT3_OK) {
		LogCrit(COMPONENT_FSAL,
			"Mount failed. Got back %u for path '%s'",
			result.fhs_status, dirpath);
		gsh_free(export);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	nfs_fh3 *fh3 = (nfs_fh3 *) &result.mountres3_u.mountinfo.fhandle;

	LogDebug(COMPONENT_FSAL,
		 "Mount successful. Got back a %" PRIu32 " len fhandle",
		 fh3->data.data_len);

	/* Copy the result for later use. */
	export->root_handle_len = fh3->data.data_len;
	memcpy(export->root_handle, fh3->data.data_val, fh3->data.data_len);

	if (proxyv3_nlm_port() != 0) {
		/* Try to test NLM by sending a NULL command. */
		if (!proxyv3_nlm_call(proxyv3_sockaddr(),
				      proxyv3_socklen(),
				      proxyv3_nlm_port(),
				      proxyv3_creds(),
				      NLMPROC4_NULL,
				      (xdrproc_t) xdr_void, NULL,
				      (xdrproc_t) xdr_void, NULL)) {
			/* nlm_call will already have said the RPC failed. */
			gsh_free(export);
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	/* Now fill in the fsinfo and we're done.*/
	return proxyv3_fill_fsinfo(fh3);
}

/**
 * @brief Initialize the PROXY_V3 FSAL.
 */

MODULE_INIT void proxy_v3_init(void)
{
	/* Try to register our FSAL. If it fails, exit. */
	if (register_fsal(&PROXY_V3.module, "PROXY_V3", FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS) != 0) {
		return;
	}

	/*
	 * NOTE(boulos): We used to setup our RPC and NLM connections here
	 * before exiting, but we need to wait for init_config in order to make
	 * those configurable. The FSAL manager doesn't call anything else in
	 * between anyway.
	 */

	PROXY_V3.module.m_ops.init_config = proxyv3_init_config;
	PROXY_V3.module.m_ops.create_export = proxyv3_create_export;

	/*
	 * Fill in the objecting handling ops with the default "Hey! NOT
	 * IMPLEMENTED!!" ones, and then override the ones we handle.
	 */
	fsal_default_obj_ops_init(&PROXY_V3.handle_ops);

	/* FSAL handle-related ops. */
	PROXY_V3.handle_ops.handle_to_wire = proxyv3_handle_to_wire;
	PROXY_V3.handle_ops.handle_to_key = proxyv3_handle_to_key;
	PROXY_V3.handle_ops.release = proxyv3_handle_release;

	/* Attributes. */
	PROXY_V3.handle_ops.lookup = proxyv3_lookup_handle;
	PROXY_V3.handle_ops.getattrs = proxyv3_getattrs;
	PROXY_V3.handle_ops.setattr2 = proxyv3_setattr2;

	/* Mkdir/Readir. (RMDIR is under unlink). */
	PROXY_V3.handle_ops.mkdir = proxyv3_mkdir;
	PROXY_V3.handle_ops.readdir = proxyv3_readdir;

	/* Symlink and hardlink. */
	PROXY_V3.handle_ops.link = proxyv3_hardlink;
	PROXY_V3.handle_ops.readlink = proxyv3_readlink;
	PROXY_V3.handle_ops.symlink = proxyv3_symlink;

	/* Block/Character/Fifo/Device files. */
	PROXY_V3.handle_ops.mknode = proxyv3_mknode;

	/* Read/write/flush */
	PROXY_V3.handle_ops.read2 = proxyv3_read2;
	PROXY_V3.handle_ops.write2 = proxyv3_write2;
	PROXY_V3.handle_ops.commit2 = proxyv3_commit2;

	/* Open/close. */
	PROXY_V3.handle_ops.open2 = proxyv3_open2;
	PROXY_V3.handle_ops.close = proxyv3_close;
	PROXY_V3.handle_ops.close2 = proxyv3_close2;

	/* Remove (and RMDIR) and rename. */
	PROXY_V3.handle_ops.unlink = proxyv3_unlink;
	PROXY_V3.handle_ops.rename = proxyv3_rename;

	/* Locking */
	PROXY_V3.handle_ops.lock_op2 = proxyv3_lock_op2;
}
