/*
 *
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
 * @file nfs_filehandle_mgmt.c
 * @brief Some tools for managing the file handles
 *
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"

/**
 *
 *  nfs3_FhandleToCache: gets the cache entry from the NFSv3 file handle.
 *
 * Validates and Converts a V3 file handle and then gets the cache entry.
 *
 * @param fh3 [IN] pointer to the file handle to be converted
 * @param req_ctx [IN] request context
 * @param exp_list [IN] export fsal to use
 * @param status [OUT] protocol status
 * @param rc [OUT] operation status
 *
 * @return cache entry or NULL on failure
 *
 */
cache_entry_t *nfs3_FhandleToCache(nfs_fh3 *fh3,
				   const struct req_op_context *req_ctx,
				   exportlist_t *exp_list, nfsstat3 *status,
				   int *rc)
{
	fsal_status_t fsal_status;
	file_handle_v3_t *v3_handle;
	struct gsh_export *exp = NULL;
	struct fsal_export *export;
	cache_entry_t *entry = NULL;
	cache_inode_fsal_data_t fsal_data;
	cache_inode_status_t cache_status;

	/* Default behaviour */
	*rc = NFS_REQ_OK;

	/* validate the filehandle  */
	*status = nfs3_Is_Fh_Invalid(fh3);

	if (*status != NFS3_OK)
		goto badhdl;

	/* Cast the fh as a non opaque structure */
	v3_handle = (file_handle_v3_t *) (fh3->data.data_val);

	if (v3_handle->exportid == req_ctx->export->export.id) {
		export = req_ctx->export->export.export_hdl;
	} else {
		exp = get_gsh_export(v3_handle->exportid, true);
		if (exp == NULL) {
			*status = NFS3ERR_STALE;
			goto badhdl;
		}
		export = exp->export.export_hdl;
	}

	/* Give the export a crack at it */
	fsal_data.export = export;
	fsal_data.fh_desc.len = v3_handle->fs_len;
	fsal_data.fh_desc.addr = &v3_handle->fsopaque;

	/* adjust the handle opaque into a cache key */
	fsal_status =
	    export->ops->extract_handle(export, FSAL_DIGEST_NFSV3,
					&fsal_data.fh_desc);
	if (exp != NULL)
		put_gsh_export(exp);

	if (FSAL_IS_ERROR(fsal_status))
		cache_status = cache_inode_error_convert(fsal_status);
	else
		cache_status = cache_inode_get(&fsal_data, req_ctx, &entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		*status = nfs3_Errno(cache_status);
		if (nfs_RetryableError(cache_status))
			*rc = NFS_REQ_DROP;
	}

 badhdl:
	return entry;
}

/**
 * @brief Converts an FSAL object to an NFSv4 file handle
 *
 * @param[out] fh4        The extracted file handle
 * @param[in]  fsalhandle The FSAL handle to be converted
 *
 * @return true if successful, false otherwise
 */
bool nfs4_FSALToFhandle(nfs_fh4 *fh4,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp)
{
	fsal_status_t fsal_status;
	file_handle_v4_t *file_handle;
	struct gsh_buffdesc fh_desc;

	/* reset the buffer to be used as handle */
	fh4->nfs_fh4_len = sizeof(struct alloc_file_handle_v4);
	memset(fh4->nfs_fh4_val, 0, fh4->nfs_fh4_len);
	file_handle = (file_handle_v4_t *) fh4->nfs_fh4_val;

	/* Fill in the fs opaque part */
	fh_desc.addr = &file_handle->fsopaque;
	fh_desc.len = fh4->nfs_fh4_len - offsetof(file_handle_v4_t, fsopaque);
	fsal_status =
	    fsalhandle->ops->handle_digest(fsalhandle, FSAL_DIGEST_NFSV4,
					   &fh_desc);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_FILEHANDLE,
			 "handle_digest FSAL_DIGEST_NFSV4 failed");
		return false;
	}

	file_handle->fhversion = GANESHA_FH_VERSION;
	file_handle->fs_len = fh_desc.len;	/* set the actual size */
	/* keep track of the export id */
	file_handle->exportid = exp->export.id;

	/* Set the len */
	fh4->nfs_fh4_len = nfs4_sizeof_handle(file_handle);

	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS4 Handle %s", LEN_FH_STR,
			   fh4->nfs_fh4_val, fh4->nfs_fh4_len);

	return true;
}

/**
 * @brief Converts an FSAL object to an NFSv3 file handle
 *
 * @param[out] fh3        The extracted file handle
 * @param[in]  fsalhandle The FSAL handle to be converted
 * @param[in]  exp        The gsh_export that this handle belongs to
 *
 * @return true if successful, false otherwise
 *
 * @todo Do we have to worry about buffer alignment and memcpy to
 * compensate??
 */
bool nfs3_FSALToFhandle(nfs_fh3 *fh3,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp)
{
	fsal_status_t fsal_status;
	file_handle_v3_t *file_handle;
	struct gsh_buffdesc fh_desc;

	/* reset the buffer to be used as handle */
	fh3->data.data_len = sizeof(struct alloc_file_handle_v3);
	memset(fh3->data.data_val, 0, fh3->data.data_len);
	file_handle = (file_handle_v3_t *) fh3->data.data_val;

	/* Fill in the fs opaque part */
	fh_desc.addr = &file_handle->fsopaque;
	fh_desc.len = fh3->data.data_len - offsetof(file_handle_v3_t, fsopaque);
	fsal_status =
	    fsalhandle->ops->handle_digest(fsalhandle, FSAL_DIGEST_NFSV3,
					   &fh_desc);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_FILEHANDLE,
			 "handle_digest FSAL_DIGEST_NFSV3 failed");
		return false;
	}

	file_handle->fhversion = GANESHA_FH_VERSION;
	file_handle->fs_len = fh_desc.len;	/* set the actual size */
	/* keep track of the export id */
	file_handle->exportid = exp->export.id;

	/* Set the len */
	/* re-adjust to as built */
	fh3->data.data_len = nfs3_sizeof_handle(file_handle);

	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS3 Handle %s", LEN_FH_STR,
			   fh3->data.data_val, fh3->data.data_len);

	return true;
}

/**
 * @brief Convert an FSAL object to an NFSv2 file handle
 *
 * @param[out] fh2        The extracted file handle
 * @param[in]  fsalhandle The FSAL handle to be converted
 * @param[in]  exp        The gsh_export that this handle belongs to
 *
 * @return true if successful, false otherwise
 */
bool nfs2_FSALToFhandle(fhandle2 *fh2,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp)
{
	fsal_status_t fsal_status;
	file_handle_v2_t *file_handle;
	struct gsh_buffdesc fh_desc;

	/* zero-ification of the buffer to be used as handle */
	memset(fh2, 0, sizeof(struct alloc_file_handle_v2));
	file_handle = (file_handle_v2_t *) fh2;

	/* Fill in the fs opaque part */
	fh_desc.addr = &file_handle->fsopaque;
	fh_desc.len = sizeof(file_handle->fsopaque);
	fsal_status =
	    fsalhandle->ops->handle_digest(fsalhandle, FSAL_DIGEST_NFSV2,
					   &fh_desc);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_TOOSMALL)
			LogCrit(COMPONENT_FILEHANDLE,
				"NFSv2 File handle is too small to manage this FSAL");
		else
			LogCrit(COMPONENT_FILEHANDLE,
				"FSAL_DigestHandle return (%u,%u) when called from %s",
				fsal_status.major, fsal_status.minor, __func__);
		return false;
	}

	file_handle->fhversion = GANESHA_FH_VERSION;
	/* keep track of the export id */
	file_handle->exportid = exp->export.id;

	/*   /\* Set the data *\/ */
	/*   memcpy((caddr_t) pfh2, &file_handle, sizeof(file_handle_v2_t)); */

	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS2 Handle %s", LEN_FH_STR,
			   fh2, sizeof(*fh2));

	return true;
}

/**
 *
 * nfs4_Is_Fh_DSHandle
 *
 * This routine is used to test if a fh is a DS fh
 *
 * @param fh [IN] file handle to test.
 *
 * @return true if DS fh, false otherwise
 *
 */
int nfs4_Is_Fh_DSHandle(nfs_fh4 *fh)
{
	file_handle_v4_t *fhandle4;

	if (fh == NULL)
		return 0;

	fhandle4 = (file_handle_v4_t *) (fh->nfs_fh4_val);

	return (fhandle4->flags & FILE_HANDLE_V4_FLAG_DS) != 0;
}

/**
 * @brief Test if a filehandle is invalid
 *
 * @param[in] fh File handle to test.
 *
 * @return NFS4_OK if successfull.
 */

int nfs4_Is_Fh_Invalid(nfs_fh4 *fh)
{
	file_handle_v4_t *pfile_handle;

	BUILD_BUG_ON(sizeof(struct alloc_file_handle_v4) != NFS4_FHSIZE);

	if (fh == NULL) {
		LogMajor(COMPONENT_FILEHANDLE, "INVALID HANDLE: fh==NULL");
		return NFS4ERR_BADHANDLE;
	}

	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS4 Handle %s", LEN_FH_STR,
			   fh->nfs_fh4_val, fh->nfs_fh4_len);

	/* Cast the fh as a non opaque structure */
	pfile_handle = (file_handle_v4_t *) (fh->nfs_fh4_val);

	/* validate the filehandle  */
	if (pfile_handle == NULL || fh->nfs_fh4_len == 0
	    || pfile_handle->fhversion != GANESHA_FH_VERSION
	    || fh->nfs_fh4_len < offsetof(struct file_handle_v4, fsopaque)
	    || fh->nfs_fh4_len > sizeof(struct alloc_file_handle_v4)
	    || fh->nfs_fh4_len != nfs4_sizeof_handle(pfile_handle)) {
		if (isInfo(COMPONENT_FILEHANDLE)) {
			if (pfile_handle == NULL) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: nfs_fh4_val=NULL");
			} else if (fh->nfs_fh4_len == 0) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: zero length handle");
			} else if (pfile_handle->fhversion !=
				   GANESHA_FH_VERSION) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: not a Ganesha handle, fhversion=%d",
					pfile_handle->fhversion);
			} else if (fh->nfs_fh4_len <
				   offsetof(struct file_handle_v4, fsopaque)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_len=%d is less than %d",
					fh->nfs_fh4_len,
					(int)offsetof(struct file_handle_v4,
						      fsopaque));
			} else if (fh->nfs_fh4_len >
				   sizeof(struct alloc_file_handle_v4)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_len=%d is greater than %d",
					fh->nfs_fh4_len,
					(int)sizeof(struct
						    alloc_file_handle_v4));
			} else if (fh->nfs_fh4_len !=
				   nfs4_sizeof_handle(pfile_handle)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: nfs_fh4_len=%d, should be %d",
					fh->nfs_fh4_len,
					(int)nfs4_sizeof_handle(pfile_handle));
			} else {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: is_pseudofs=%d",
					pfile_handle->exportid == 0);
			}
		}

		return NFS4ERR_BADHANDLE;	/* Bad FH */
	}

	return NFS4_OK;
}				/* nfs4_Is_Fh_Invalid */

/**
 * @brief Test if a filehandle is invalid.
 *
 * @param[in] fh3 File handle to test.
 *
 * @return NFS4_OK if successfull.
 *
 */
int nfs3_Is_Fh_Invalid(nfs_fh3 *fh3)
{
	file_handle_v3_t *pfile_handle;

	BUILD_BUG_ON(sizeof(struct alloc_file_handle_v3) != NFS3_FHSIZE);

	if (fh3 == NULL) {
		LogMajor(COMPONENT_FILEHANDLE, "INVALID HANDLE: fh3==NULL");
		return NFS3ERR_BADHANDLE;
	}

	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS3 Handle %s", LEN_FH_STR,
			   fh3->data.data_val, fh3->data.data_len);

	/* Cast the fh as a non opaque structure */
	pfile_handle = (file_handle_v3_t *) (fh3->data.data_val);

	/* validate the filehandle  */
	if (pfile_handle == NULL || fh3->data.data_len == 0
	    || pfile_handle->fhversion != GANESHA_FH_VERSION
	    || fh3->data.data_len < sizeof(file_handle_v3_t)
	    || fh3->data.data_len > sizeof(struct alloc_file_handle_v3)
	    || fh3->data.data_len != nfs3_sizeof_handle(pfile_handle)) {
		if (isInfo(COMPONENT_FILEHANDLE)) {
			if (pfile_handle == NULL) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_val=NULL");
			} else if (fh3->data.data_len == 0) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: zero length handle");
			} else if (pfile_handle->fhversion !=
				   GANESHA_FH_VERSION) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: not a Ganesha handle, fhversion=%d",
					pfile_handle->fhversion);
			} else if (fh3->data.data_len <
				   sizeof(file_handle_v3_t)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_len=%d is less than %d",
					fh3->data.data_len,
					(int)sizeof(file_handle_v3_t));
			} else if (fh3->data.data_len >
				   sizeof(struct alloc_file_handle_v3)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_len=%d is greater than %d",
					fh3->data.data_len,
					(int)sizeof(struct
						    alloc_file_handle_v3));
			} else if (fh3->data.data_len !=
				   nfs3_sizeof_handle(pfile_handle)) {
				LogInfo(COMPONENT_FILEHANDLE,
					"INVALID HANDLE: data.data_len=%d, should be %d",
					fh3->data.data_len,
					(int)nfs3_sizeof_handle(pfile_handle));
			}
		}

		return NFS3ERR_BADHANDLE;	/* Bad FH */
	}

	return NFS3_OK;
}				/* nfs3_Is_Fh_Invalid */

/**
 * @brief Print an NFSv2 file handle (for debugging purpose)
 *
 * @param[in] component Subsystem component ID
 * @param[in] fh        File handle to print
 */
void print_fhandle2(log_components_t component, fhandle2 *fh)
{
	if (isFullDebug(component)) {
		char str[LEN_FH_STR];

		sprint_fhandle2(str, fh);
		LogFullDebug(component, "%s", str);
	}
}

void sprint_fhandle2(char *str, fhandle2 *fh)
{
	char *tmp = str + sprintf(str, "File Handle V2: ");

	sprint_mem(tmp, (char *)fh, 32);
}				/* sprint_fhandle2 */

/**
 * @brief Print an NFSv3 file handle
 *
 * @param[in] component Subsystem component ID
 * @param[in] fh        File handle to prin
 */
void print_fhandle3(log_components_t component, nfs_fh3 *fh)
{
	if (isFullDebug(component)) {
		char str[LEN_FH_STR];

		sprint_fhandle3(str, fh);
		LogFullDebug(component, "%s", str);
	}
}

void sprint_fhandle3(char *str, nfs_fh3 *fh)
{
	char *tmp =
	    str + sprintf(str, "File Handle V3: Len=%u ", fh->data.data_len);

	sprint_mem(tmp, fh->data.data_val, fh->data.data_len);
}

/**
 * @brief Print an NFSv4 file handle
 *
 * @param[in] component Subsystem component ID
 * @param[in] fh        File handle to print
 */
void print_fhandle4(log_components_t component, nfs_fh4 *fh)
{
	if (isFullDebug(component)) {
		char str[LEN_FH_STR];

		sprint_fhandle4(str, fh);
		LogFullDebug(component, "%s", str);
	}
}

void sprint_fhandle4(char *str, nfs_fh4 *fh)
{
	char *tmp =
	    str + sprintf(str, "File Handle V4: Len=%u ", fh->nfs_fh4_len);

	sprint_mem(tmp, fh->nfs_fh4_val, fh->nfs_fh4_len);
}

/**
 * @brief Print an NLM netobj
 *
 * @param[in] component Subsystem component ID
 * @param[in] fh        File handle to print
 */
void print_fhandle_nlm(log_components_t component, netobj *fh)
{
	if (isFullDebug(component)) {
		char str[LEN_FH_STR];

		sprint_fhandle_nlm(str, fh);
		LogFullDebug(component, "%s", str);
	}
}				/* print_fhandle_nlm */

void sprint_fhandle_nlm(char *str, netobj *fh)
{
	char *tmp = str + sprintf(str, "File Handle V3: Len=%u ", fh->n_len);

	sprint_mem(tmp, fh->n_bytes, fh->n_len);
}				/* sprint_fhandle_nlm */

/**
 * @brief Print the content of a buffer.
 *
 * @param[in] component Logging subsystem ID
 * @param[in] buff      Buffer to print.
 * @param[in] len       Length of the buffer.
 */
void print_buff(log_components_t component, char *buff, int len)
{
	if (isFullDebug(component)) {
		char str[1024];

		sprint_buff(str, buff, len);
		LogFullDebug(component, "%s", str);
	}
}				/* print_buff */

void sprint_buff(char *str, char *buff, int len)
{
	char *tmp = str + sprintf(str, "  Len=%u Buff=%p Val: ", len, buff);

	sprint_mem(tmp, buff, len);
}				/* sprint_buff */

void sprint_mem(char *str, char *buff, int len)
{
	int i;

	if (buff == NULL)
		sprintf(str, "<null>");
	else
		for (i = 0; i < len; i++)
			sprintf(str + i * 2, "%02x", (unsigned char)buff[i]);
}

/**
 *
 * print_compound_fh
 *
 * This routine prints all the file handle within a compoud request's data
 * structure.
 *
 * @param data [IN] compound's data to manage.
 */
void LogCompoundFH(compound_data_t *data)
{
	if (isFullDebug(COMPONENT_FILEHANDLE)) {
		char str[LEN_FH_STR];

		sprint_fhandle4(str, &data->currentFH);
		LogFullDebug(COMPONENT_FILEHANDLE, "Current FH  %s", str);

		sprint_fhandle4(str, &data->savedFH);
		LogFullDebug(COMPONENT_FILEHANDLE, "Saved FH    %s", str);
	}
}
