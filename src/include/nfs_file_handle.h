/*
 *
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
 * @file nfs_file_handle.h
 * @brief Prototypes for the file handle in v3 and v4
 */

#ifndef NFS_FILE_HANDLE_H
#define NFS_FILE_HANDLE_H

#include <sys/types.h>
#include <sys/param.h>

#include "log.h"
#include "sal_data.h"
#include "export_mgr.h"
#include "nfs_fh.h"

/**
 * @brief Get the actual size of a v3 handle based on the sized fsopaque
 *
 * @return The filehandle size
 */

static inline size_t nfs3_sizeof_handle(struct file_handle_v3 *hdl)
{
	int hsize;
	int aligned_hsize;

	hsize = offsetof(struct file_handle_v3, fsopaque) + hdl->fs_len;

	/* correct packet's fh length so it's divisible by 4 to trick dNFS into
	   working. This is essentially sending the padding. */
	aligned_hsize = roundup(hsize, 4);
	if (aligned_hsize <= NFS3_FHSIZE)
		hsize = aligned_hsize;

	return hsize;
}

/**
 *
 * @brief Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * Allocates a buffer to be used for storing a NFSv3 filehandle.
 *
 * @param fh [INOUT] the filehandle to manage.
 *
 */
static inline
void nfs3_AllocateFH(nfs_fh3 *fh)
{
	/* Allocating the filehandle in memory */
	fh->data.data_len = NFS3_FHSIZE;
	fh->data.data_val = (char *) gsh_calloc(1, NFS3_FHSIZE);
}

static inline void nfs3_freeFH(nfs_fh3 *fh)
{
	fh->data.data_len = 0;
	gsh_free(fh->data.data_val);
	fh->data.data_val = NULL;
}

/**
 *
 * @brief Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * @param fh [INOUT] the filehandle to manage.
 *
 */
static inline
void nfs4_AllocateFH(nfs_fh4 *fh)
{
	/* Allocating the filehandle in memory */
	fh->nfs_fh4_len = NFS4_FHSIZE;
	fh->nfs_fh4_val = (char *) gsh_calloc(1, NFS4_FHSIZE);
}

static inline void nfs4_freeFH(nfs_fh4 *fh)
{
	fh->nfs_fh4_len = 0;
	gsh_free(fh->nfs_fh4_val);
	fh->nfs_fh4_val = NULL;
}

/**
 * @brief Get the actual size of a v4 handle based on the sized fsopaque
 *
 * @return The filehandle size
 */

static inline size_t nfs4_sizeof_handle(struct file_handle_v4 *hdl)
{
	return offsetof(struct file_handle_v4, fsopaque)+hdl->fs_len;
}

#define LEN_FH_STR 1024

/* File handle translation utility */
#ifdef _USE_NFS3
struct fsal_obj_handle *nfs3_FhandleToCache(nfs_fh3 *, nfsstat3 *, int *);
#endif

bool nfs4_FSALToFhandle(bool allocate,
			nfs_fh4 *fh4,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp);

bool nfs3_FSALToFhandle(bool allocate,
			nfs_fh3 *fh3,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp);

/* nfs3 validation */
int nfs3_Is_Fh_Invalid(nfs_fh3 *);

/**
 *
 * nfs3_FhandleToExportId
 *
 * This routine extracts the export id from the file handle NFSv3
 *
 * @param pfh3 [IN] file handle to manage.
 *
 * @return the export id.
 *
 */
static inline int nfs3_FhandleToExportId(nfs_fh3 *pfh3)
{
	file_handle_v3_t *pfile_handle;

	if (nfs3_Is_Fh_Invalid(pfh3) != NFS4_OK)
		return -1;	/* Badly formed argument */

	pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

	/*exportid is in network byte order in nfs_fh3*/
	return ntohs(pfile_handle->exportid);
}				/* nfs3_FhandleToExportId */

static inline int nlm4_FhandleToExportId(netobj *pfh3)
{
	nfs_fh3 fh3;

	if (pfh3 == NULL)
		return nfs3_FhandleToExportId(NULL);
	fh3.data.data_val = pfh3->n_bytes;
	fh3.data.data_len = pfh3->n_len;
	return nfs3_FhandleToExportId(&fh3);
}

/**
 *
 * @brief Test if an NFS v4 file handle is empty.
 *
 * This routine is used to test if a fh is empty (contains no data).
 *
 * @param pfh [IN] file handle to test.
 *
 * @return NFS4_OK if successfull, NFS4ERR_NOFILEHANDLE is fh is empty.
 *
 */
static inline int nfs4_Is_Fh_Empty(nfs_fh4 *pfh)
{
	if (pfh == NULL) {
		LogMajor(COMPONENT_FILEHANDLE, "INVALID HANDLE: pfh=NULL");
		return NFS4ERR_NOFILEHANDLE;
	}

	if (pfh->nfs_fh4_len == 0) {
		LogInfo(COMPONENT_FILEHANDLE, "INVALID HANDLE: empty");
		return NFS4ERR_NOFILEHANDLE;
	}

	return NFS4_OK;
}				/* nfs4_Is_Fh_Empty */

/* NFSv4 specific FH related functions */
int nfs4_Is_Fh_Invalid(nfs_fh4 *);
int nfs4_Is_Fh_DSHandle(nfs_fh4 *);

nfsstat4 nfs4_sanity_check_FH(compound_data_t *data,
			      object_file_type_t required_type,
			      bool ds_allowed);

nfsstat4 nfs4_sanity_check_saved_FH(compound_data_t *data, int required_type,
				    bool ds_allowed);

/* File handle print function (mostly used for debugging) */
void print_fhandle3(log_components_t, nfs_fh3 *);
void print_fhandle4(log_components_t, nfs_fh4 *);
void print_fhandle_nlm(log_components_t, netobj *);
void print_buff(log_components_t, char *, int);
void LogCompoundFH(compound_data_t *);

void sprint_fhandle3(char *str, nfs_fh3 *fh);
void sprint_fhandle4(char *str, nfs_fh4 *fh);
void sprint_fhandle_nlm(char *str, netobj *fh);
void sprint_buff(char *str, char *buff, int len);
void sprint_mem(char *str, char *buff, int len);

void nfs_FhandleToStr(u_long rq_vers, nfs_fh3 *pfh3, nfs_fh4 *pfh4, char *str);

#define LogHandleNFS4(label, fh4) \
	do { \
		if (isFullDebug(COMPONENT_NFS_V4)) { \
			char str[LEN_FH_STR]; \
			sprint_fhandle4(str, fh4); \
			LogFullDebug(COMPONENT_NFS_V4, "%s%s", label, str); \
		} \
	} while (0)

#endif				/* NFS_FILE_HANDLE_H */
