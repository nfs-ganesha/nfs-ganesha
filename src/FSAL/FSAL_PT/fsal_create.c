/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_create.c
 * Description: FSAL create operations implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
/*
 * vim:noexpandtab:shiftwidth=4:tabstop=4:
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_methods.h"
#include <unistd.h>
#include <fcntl.h>
#include <fsal_api.h>

/* PTFSAL */
#include "pt_ganesha.h"

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_hdl (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param p_object_handle (output):
 *        Pointer to the handle of the created file.
 * \param p_object_attributes (optional input/output):
 *        The attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occurred.
 */
fsal_status_t PTFSAL_create(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_filename,	/* IN */
			    const struct req_op_context *p_context,	/* IN */
			    uint32_t accessmode,	/* IN */
			    ptfsal_handle_t *p_object_handle,	/* OUT */
			    struct attrlist *p_object_attributes)
{				/* IN/OUT */

	int errsv;
	fsal_status_t status;
	struct pt_fsal_obj_handle *pt_hdl;
	mode_t unix_mode;
	int open_rc;
	ptfsal_handle_t *p_fsi_handle = (ptfsal_handle_t *) p_object_handle;

	FSI_TRACE(FSI_DEBUG, "Begin to create file************************\n");

	/* sanity checks.
	 * note : object_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_object_handle || !p_filename)
		return fsalstat(ERR_FSAL_FAULT, 0);

	pt_hdl = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);

	/* convert fsal mode to unix mode. */
	unix_mode = fsal2unix_mode(accessmode);

	/* Apply umask */
	unix_mode = unix_mode & ~p_context->fsal_export->ops->
			fs_umask(p_context->fsal_export);

	LogFullDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

	open_rc =
	    ptfsal_open(pt_hdl, p_filename, p_context, unix_mode,
			p_object_handle);
	if (open_rc < 0) {
		errsv = errno;
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	FSI_TRACE(FSI_DEBUG, "New Handle = %s",
		  (char *)p_fsi_handle->data.handle.f_handle);

	/* retrieve file attributes */
	if (p_object_attributes) {
		status = PTFSAL_getattrs(p_context->fsal_export, p_context,
					 p_object_handle,
					 p_object_attributes);

		/* on error, we set a special bit in the mask. */
		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_object_attributes->mask);
			FSAL_SET_MASK(p_object_attributes->mask,
				      ATTR_RDATTR_ERR);
		}

	}

	FSI_TRACE(FSI_DEBUG, "End to create file************************\n");

	/* OK */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_mkdir:
 * Create a directory.
 *
 * \param dir_hdl (input):
 *        Handle of the parent directory where
 *        the subdirectory is to be created.
 * \param p_context (input):
 *        Pointer to the name of the directory to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param p_object_handle (output):
 *        Pointer to the handle of the created directory.
 * \param p_object_attributes (optionnal input/output):
 *        The attributes of the created directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t PTFSAL_mkdir(struct fsal_obj_handle *dir_hdl,	/* IN */
			   const char *p_dirname,	/* IN */
			   const struct req_op_context *p_context,	/* IN */
			   uint32_t accessmode,	/* IN */
			   ptfsal_handle_t *p_object_handle,	/* OUT */
			   struct attrlist *p_object_attributes)
{				/* IN/OUT */

	int rc, errsv;
	int setgid_bit = 0;
	mode_t unix_mode;
	fsal_status_t status;
	struct attrlist parent_dir_attrs;
	char newPath[PATH_MAX];
	struct pt_fsal_obj_handle *pt_hdl;

	FSI_TRACE(FSI_INFO, "MKDIR BEGIN-------------------------\n");

	/* sanity checks.
	 * note : object_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_object_handle || !p_dirname)
		return fsalstat(ERR_FSAL_FAULT, 0);

	pt_hdl = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);

	/* convert FSAL mode to unix mode. */
	unix_mode = fsal2unix_mode(accessmode);

	/* Apply umask */
	unix_mode = unix_mode & ~p_context->fsal_export->ops->
			fs_umask(p_context->fsal_export);

	/* get directory metadata */
	parent_dir_attrs.mask = p_context->fsal_export->ops->
				fs_supported_attrs(p_context->fsal_export);
	status =
	    PTFSAL_getattrs(p_context->fsal_export, p_context, pt_hdl->handle,
			    &parent_dir_attrs);

	if (FSAL_IS_ERROR(status))
		return status;

	/* Check the user can write in the directory, and check the
	 * setgid bit on the directory
	 */

	if (fsal2unix_mode(parent_dir_attrs.mode) & S_ISGID)
		setgid_bit = 1;

	rc = ptfsal_mkdir(pt_hdl, p_dirname, p_context, unix_mode,
			  p_object_handle);
	errsv = errno;
	if (rc)
		return fsalstat(posix2fsal_error(errsv), errsv);

	if (FSAL_IS_ERROR(status))
		return status;

	/* the directory has been created */
	/* chown the dir to the current user/group */

	if (p_context->creds->caller_uid != geteuid()) {
		FSI_TRACE(FSI_DEBUG, "MKDIR %d", __LINE__);
		/* if the setgid_bit was set on the parent directory, do not
		 * change the group of the created file, because it's already
		 * the parentdir's group
		 */

		if (fsi_get_name_from_handle
		    (p_context, p_context->fsal_export, pt_hdl->handle,
		     (char *)newPath, NULL) < 0) {
			FSI_TRACE(FSI_DEBUG,
				  "Failed to get name from handle %s",
				  (char *)p_object_handle->data.handle.
				  f_handle);
			return fsalstat(posix2fsal_error(errsv), errsv);
		}
		rc = ptfsal_chown(p_context, p_context->fsal_export, newPath,
				  p_context->creds->caller_uid,
				  setgid_bit ? -1 : (int)p_context->creds->
				  caller_gid);
		errsv = errno;
		if (rc)
			return fsalstat(posix2fsal_error(errsv), errsv);
	}

	/* retrieve file attributes */
	if (p_object_attributes) {
		FSI_TRACE(FSI_DEBUG, "MKDIR %d", __LINE__);
		status = PTFSAL_getattrs(p_context->fsal_export, p_context,
					 p_object_handle,
					 p_object_attributes);

		/* on error, we set a special bit in the mask. */
		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_object_attributes->mask);
			FSAL_SET_MASK(p_object_attributes->mask,
				      ATTR_RDATTR_ERR);
		}

	}
	FSI_TRACE(FSI_INFO, "MKDIR END ------------------\n");
	FSI_TRACE(FSI_DEBUG, "MKDIR %d", __LINE__);
	/* OK */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported upon HPSS.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t PTFSAL_mknode(struct fsal_obj_handle *dir_hdl,	/* IN */
			    const char *p_node_name,	/* IN */
			    const struct req_op_context *p_context,	/* IN */
			    uint32_t accessmode,	/* IN */
			    mode_t nodetype,	/* IN */
			    fsal_dev_t *dev,	/* IN */
			    ptfsal_handle_t *p_object_handle,	/* OUT */
			    struct attrlist *node_attributes)
{				/* IN/OUT */
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}
