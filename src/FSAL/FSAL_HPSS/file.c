/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DISCLAIMER
 * ----------
 * This file is part of FSAL_HPSS.
 * FSAL HPSS provides the glue in-between FSAL API and HPSS CLAPI
 * You need to have HPSS installed to properly compile this file.
 *
 * Linkage/compilation/binding/loading/etc of HPSS licensed Software
 * must occur at the HPSS partner's or licensee's location.
 * It is not allowed to distribute this software as compiled or linked
 * binaries or libraries, as they include HPSS licensed material.
 * -------------
 */

/* file.c
 * File I/O methods for VFS module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "hpss_methods.h"
#include <stdbool.h>

/** lustre_open
 * called with appropriate locks taken at the cache inode level
 */

/**
 * FSAL_open:
 * Open a regular file for reading/writing its data content.
 *
 * \param filehandle (input):
 *        Handle of the file to be read/modified.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) :
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writing only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_attributes (optional input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS
 *      - ERR_FSAL_STALE
 *      - ERR_FSAL_INVAL
 *
 *      - ERR_FSAL_FAULT
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t hpss_open(struct fsal_obj_handle *fsal_obj_hdl,
			fsal_openflags_t openflags)
{
	int rc;
	int hpss_flags;
	struct hpss_fsal_obj_handle *obj_hdl;
	sec_cred_t ucreds;

	/* sanity checks. */
	if (!fsal_obj_hdl)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

	/* check if it is a file */
	if (obj_hdl->handle->obj_type != REGULAR_FILE)
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* convert fsal open flags to hpss open flags */
	rc = fsal2hpss_openflags(openflags, &hpss_flags);

	/* flags conflicts. */
	if (rc) {
		LogEvent(COMPONENT_FSAL,
			 "Invalid/conflicting flags : %#X", openflags);
		return fsalstat(rc, 0);
	}


	rc = hpss_OpenHandle(&(obj_hdl->handle->ns_handle),
			     NULL,
			     hpss_flags,
			     (mode_t) 0644,
			     &ucreds,
			     NULL,
			     NULL,
			     NULL,
			     NULL);

	/* /!\ rc is the file descriptor number !!! */

	/* The HPSS_ENOENT error actually means that handle is STALE */
	if (rc == HPSS_ENOENT)
		return fsalstat(ERR_FSAL_STALE, -rc);

	else
		if (rc < 0)
			return fsalstat(hpss2fsal_error(rc), -rc);

	/* fill file data */
	obj_hdl->u.file.fd = rc;
	obj_hdl->u.file.openflags = openflags;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* lustre_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t hpss_status(struct fsal_obj_handle *obj_hdl)
{
	struct hpss_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct hpss_fsal_obj_handle, obj_handle);
	return myself->u.file.openflags;
}

/**
 * FSAL_read:
 * Perform a read operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be read.
 *        If not specified, data will be read at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be read.
 * \param buffer (output):
 *        Address where the read data is to be stored in memory.
 * \param read_amount (output):
 *        Pointer to the amount of data (in bytes) that have been read
 *        during this call.
 * \param end_of_file (output):
 *        Pointer to a boolean that indicates whether the end of file
 *        has been reached during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR
 *      - ERR_FSAL_INVAL
 *      - ERR_FSAL_NOT_OPENED
 *      - ERR_FSAL_FAULT
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t hpss_read(struct fsal_obj_handle *fsal_obj_hdl,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *read_amount,
			bool *end_of_file)
{
	ssize_t nb_read;
	int rc = 0;
	u_signed64 offset_in, offset_out;
	struct hpss_fsal_obj_handle *obj_hdl;

	/* sanity checks. */
	if (!fsal_obj_hdl || !buffer || !read_amount || !end_of_file)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	offset_in = fsal2hpss_64(offset);

	/* positioning */
	rc = hpss_SetFileOffset(obj_hdl->u.file.fd,
				offset_in,
				SEEK_SET,
				HPSS_SET_OFFSET_FORWARD,
				&offset_out);

	if (rc < 0)
		return fsalstat(hpss2fsal_error(rc), -rc);

	if (offset != hpss2fsal_64(offset_out))
		return fsalstat(ERR_FSAL_IO, 0); /* shouldn't happen anyway */


	/* read operation */
	nb_read = hpss_Read(obj_hdl->u.file.fd,
			    (void *)buffer,
			    buffer_size);


	/** @todo: manage ssize_t to size_t conversion */
	if  (nb_read < 0) {
		rc = (int)nb_read;
		return fsalstat(hpss2fsal_error(rc), -rc);
	}

	/* set output vars */
	*read_amount = (size_t) nb_read;
	*end_of_file = (nb_read == 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_write:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be written.
 *        If not specified, data will be written at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be written.
 * \param buffer (input):
 *        Address in memory of the data to write to file.
 * \param write_amount (output):
 *        Pointer to the amount of data (in bytes) that have been written
 *        during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR
 *      - ERR_FSAL_INVAL
 *      - ERR_FSAL_NOT_OPENED
 *      - ERR_FSAL_FAULT
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t hpss_write(struct fsal_obj_handle *fsal_obj_hdl,
			 uint64_t offset,
			 size_t buffer_size,
			 void *buffer,
			 size_t *write_amount,
			 bool *fsal_stable)
{
	ssize_t nb_written;
	int rc = 0;
	u_signed64 offset_out, offset_in;
	struct hpss_fsal_obj_handle *obj_hdl;

	/* sanity checks. */
	if (!fsal_obj_hdl || !buffer || !write_amount || !fsal_stable)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	offset_in = fsal2hpss_64(offset);

	/* positioning */
	rc = hpss_SetFileOffset(obj_hdl->u.file.fd,
				offset_in,
				SEEK_SET,
				HPSS_SET_OFFSET_FORWARD,
				&offset_out);

	if (rc < 0)
		return fsalstat(hpss2fsal_error(rc), -rc);

	if (offset != hpss2fsal_64(offset_out))
		return fsalstat(ERR_FSAL_IO, 0); /* shouldn't happen anyway */

	/* write operation */
	nb_written = hpss_Write(obj_hdl->u.file.fd,
				(void *)buffer,
				buffer_size);

	/** @todo: manage ssize_t to size_t conversion */
	if (nb_written < 0) {
		rc = (int)nb_written;
		return fsalstat(hpss2fsal_error(rc), -rc);
	}

	/* set output vars */
	*write_amount = (size_t) nb_written;
	*fsal_stable = false;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_commit:
 * This function is used for processing stable writes and COMMIT requests.
 * Calling this function makes sure the changes to a specific file are
 * written to disk rather than kept in memory.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param offset:
 *        The starting offset for the portion of file to be synced
 * \param length:
 *        The length for the portion of file to be synced.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occurred during this call.
 */
fsal_status_t hpss_commit(struct fsal_obj_handle *fsal_obj_hdl, /* sync */
			  off_t offset,
			  size_t len)
{
	int rc, errsv;
	struct hpss_fsal_obj_handle *obj_hdl;

	/* sanity checks. */
	if (!fsal_obj_hdl)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);

	/* Flush data. */
	rc = hpss_Fsync(obj_hdl->u.file.fd);
	errsv = errno;

	if (rc) {
		LogEvent(COMPONENT_FSAL, "Error in fsync operation");
		return fsalstat(hpss2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_close:
 * Free the resources allocated by the FSAL_open call.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 *
 * \return Major error codes:
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t hpss_close(struct fsal_obj_handle *fsal_obj_hdl)
{
	int rc;
	struct hpss_fsal_obj_handle *obj_hdl;

	/* sanity checks. */
	if (!fsal_obj_hdl)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);


	/* call to close */
	rc = hpss_Close(obj_hdl->u.file.fd);

	if (rc)
		return fsalstat(hpss2fsal_error(rc), -rc);

	obj_hdl->u.file.fd = 0;
	obj_hdl->u.file.openflags = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* lustre_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t hpss_lru_cleanup(struct fsal_obj_handle *fsal_obj_hdl,
			      lru_actions_t requests)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
	int retval = 0;

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);

	if (fsal_obj_hdl->type == REGULAR_FILE && obj_hdl->u.file.fd > 0)
		return hpss_close(fsal_obj_hdl);

	return fsalstat(fsal_error, retval);
}

fsal_status_t hpss_lock_op(struct fsal_obj_handle *obj_hdl,
			   void *p_owner,
			   fsal_lock_op_t lock_op,
			   fsal_lock_param_t *request_lock,
			   fsal_lock_param_t *conflicting_lock)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
