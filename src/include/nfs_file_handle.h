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
#include "nfs23.h"
#include "nlm4.h"
#include "nfs_exports.h"
#include "cache_inode.h"

/*
 * Structure of the filehandle
 * these structures must be naturally aligned.  The xdr buffer from/to which
 * they come/go are 4 byte aligned.
 */

#define GANESHA_FH_VERSION 0x41

/**
 * @brief An NFSv3 handle
 *
 * This may be up to 64 bytes long, aligned on 32 bits
 */

typedef struct file_handle_v3 {
	uint8_t fhversion;	/*< Set to 0x41 to separate from Linux knfsd */
	uint8_t xattr_pos;	/*< Used for xattr management */
	uint16_t exportid;	/*< Must be correlated to exportlist_t::id */
	uint8_t fs_len;		/*< Actual length of opaque handle */
	uint8_t fsopaque[];	/*< Persistent part of FSAL handle,
				    <= 59 bytes */
} file_handle_v3_t;

/**
 * @brief A struct with the size of the largest v3 handle
 *
 * Used for allocations, sizeof, and memset only.  The pad space is
 * where the opaque handle expands into.  Pad is struct aligned.
 */

struct alloc_file_handle_v3 {
	struct file_handle_v3 handle;	/*< The real handle */
	uint8_t pad[58];	/*< Pad to mandatory max 64 bytes */
};

/**
 * @brief Get the actual size of a v3 handle based on the sized fsopaque
 *
 * @return The filehandle size
 */

static inline size_t nfs3_sizeof_handle(struct file_handle_v3 *hdl)
{
	int padding = 0;
	int hsize = 0;

	hsize = offsetof(struct file_handle_v3, fsopaque) + hdl->fs_len;

	/* correct packet's fh length so it's divisible by 4 to trick dNFS into
	   working. This is essentially sending the padding. */
	padding = (4 - (hsize % 4)) % 4;
	if ((hsize + padding) <= sizeof(struct alloc_file_handle_v3))
		hsize += padding;

	return hsize;
}

#define FILE_HANDLE_V4_FLAG_DS 0x0001

/**
 * @brief An NFSv4 filehandle
 *
 * This may be up to 128 bytes, aligned on 32 bits.
 */

typedef struct __attribute__ ((__packed__)) file_handle_v4 {
	uint8_t fhversion;	/*< Set to 0x41 to separate from Linux knfsd */
	uint16_t exportid;	/*< Must be correlated to exportlist_t::id */
	uint16_t flags;		/*< To replace things like ds_flag */
	uint8_t fs_len;		/*< Length of opaque handle */
	uint8_t fsopaque[];	/*< FSAL handle */
} file_handle_v4_t;

/**
 * @brief A struct with the size of the largest v4 handle
 *
 * Used for allocations, sizeof, and memset only.  The pad space is
 * where the opaque handle expands into.  Pad is struct aligned.
 */

struct __attribute__ ((__packed__)) alloc_file_handle_v4 {
	struct file_handle_v4 handle;	/*< The real handle */
	uint8_t pad[122];	/*< Pad to mandatory max 128 bytes */
};

int nfs3_AllocateFH(nfs_fh3 *);
int nfs4_AllocateFH(nfs_fh4 *);

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
cache_entry_t *nfs3_FhandleToCache(nfs_fh3 *,
				   nfsstat3 *, int *);

bool nfs4_FSALToFhandle(nfs_fh4 *fh4,
			const struct fsal_obj_handle *fsalhandle,
			struct gsh_export *exp);

bool nfs3_FSALToFhandle(nfs_fh3 *fh3,
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
static inline short nfs3_FhandleToExportId(nfs_fh3 *pfh3)
{
	file_handle_v3_t *pfile_handle;

	if (nfs3_Is_Fh_Invalid(pfh3) != NFS4_OK)
		return -1;	/* Badly formed argument */

	pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

	return pfile_handle->exportid;
}				/* nfs3_FhandleToExportId */

static inline short nlm4_FhandleToExportId(netobj *pfh3)
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
