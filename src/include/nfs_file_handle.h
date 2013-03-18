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
 * @brief Prototypes for the file handle in v2, v3, v4
 */

#ifndef NFS_FILE_HANDLE_H
#define NFS_FILE_HANDLE_H

#include <sys/types.h>
#include <sys/param.h>


#include "log.h"
#include "nfs23.h"
#include "nlm4.h"

/*
 * Structure of the filehandle
 * these structures must be naturally aligned.  The xdr buffer from/to which
 * they come/go are 4 byte aligned.
 */

#define ULTIMATE_ANSWER 0x42

#define GANESHA_FH_VERSION ULTIMATE_ANSWER - 1

/**
 * @brief An NFSv3 handle
 *
 * This may be up to 64 bytes long, aligned on 32 bits
 */

typedef struct file_handle_v3
{
  uint8_t fhversion; /*< Set to 0x41 to separate from Linux knfsd */
  uint8_t xattr_pos; /*< Used for xattr management */
  uint16_t exportid; /*< Must be correlated to exportlist_t::id */
  uint8_t fs_len; /*< Actual length of opaque handle */
  uint8_t fsopaque[]; /*< Persistent part of FSAL handle, <= 59 bytes */
} file_handle_v3_t;

/**
 * @brief A struct with the size of the largest v3 handle
 *
 * Used for allocations, sizeof, and memset only.  The pad space is
 * where the opaque handle expands into.  Pad is struct aligned.
 */

struct alloc_file_handle_v3 {
	struct file_handle_v3 handle; /*< The real handle */
	uint8_t pad[58]; /*< Pad to mandatory max 64 bytes */
};

/**
 * @brief Get the actual size of a v3 handle based on the sized fsopaque
 *
 * @return The filehandle size
 */

static inline size_t nfs3_sizeof_handle(struct file_handle_v3 *hdl)
{
  return offsetof(struct file_handle_v3, fsopaque) + hdl->fs_len;
}

/**
 * @brief An NFSv4 filehandle
 *
 * This may be up to 128 bytes, aligned on 32 bits.
 */

typedef struct file_handle_v4
{
  uint8_t fhversion; /*< Set to 0x41 to separate from Linux knfsd */
  uint8_t xattr_pos; /*< Index for named attribute handles */
  uint16_t exportid; /*< Must be correlated to exportlist_t::id */
  uint32_t srvboot_time; /*< 0 if FH won't expire.  This should be
			     reconsidered.  Server boot time is not
			     the ideal way to do volatile filehandles,
			     and it takes up four bytes. */
  uint16_t pseudofs_id; /*< Id for the pseudo fs related to this fh */
  uint16_t refid; /*< Used for referral.  Referral almost certainly
		      does not work and will need to be redone for 2.1.  */
  uint8_t ds_flag; /*< True if FH is a 'DS file handle'.  Consider
		       rolling this into a flags byte. */
  uint8_t pseudofs_flag; /*< True if FH is within pseudofs.  Consider
			     rolling this into a flags byte. */
  uint8_t fs_len; /*< Length of opaque handle */
  uint8_t fsopaque[]; /*< FSAL handle */
} file_handle_v4_t;

/**
 * @brief A struct with the size of the largest v4 handle
 *
 * Used for allocations, sizeof, and memset only.  The pad space is
 * where the opaque handle expands into.  Pad is struct aligned.
 */

struct alloc_file_handle_v4 {
	struct file_handle_v4 handle; /*< The real handle */
	uint8_t pad[112]; /*< Pad to mandatory max 128 bytes */
};

/**
 * @brief Get the actual size of a v4 handle based on the sized fsopaque
 *
 * @return The filehandle size
 */

static inline size_t nfs4_sizeof_handle(struct file_handle_v4 *hdl)
{
  return offsetof(struct file_handle_v4, fsopaque) + hdl->fs_len;
}

#define LEN_FH_STR 1024


/* File handle translation utility */
cache_entry_t *nfs4_FhandleToCache(nfs_fh4 *,
				   const struct req_op_context *,
				   exportlist_t *,
				   nfsstat4 *,
				   int *);
cache_entry_t * nfs3_FhandleToCache(nfs_fh3 *,
				   const struct req_op_context *,
				   exportlist_t *,
				   nfsstat3 *,
				   int *);

bool nfs4_FSALToFhandle(nfs_fh4 *,
			struct fsal_obj_handle *);
bool nfs3_FSALToFhandle(nfs_fh3 *fh3, struct fsal_obj_handle *);

/* Extraction of export id from a file handle */
short nfs4_FhandleToExportId(nfs_fh4 *);
short nfs3_FhandleToExportId(nfs_fh3 *);

short nlm4_FhandleToExportId(netobj *);

/* nfs3 validation */
int nfs3_Is_Fh_Invalid(nfs_fh3 *);

/* NFSv4 specific FH related functions */
int nfs4_Is_Fh_Xattr(nfs_fh4 *);
int nfs4_Is_Fh_Pseudo(nfs_fh4 *);
int nfs4_Is_Fh_Expired(nfs_fh4 *);
int nfs4_Is_Fh_Invalid(nfs_fh4 *);
int nfs4_Is_Fh_Referral(nfs_fh4 *);
int nfs4_Is_Fh_DSHandle(nfs_fh4 *);

/* This one is used to detect Xattr related FH */
int nfs3_Is_Fh_Xattr(nfs_fh3 *);

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

void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr) ;

#define LogHandleNFS4(label, fh4)			    \
  do {							    \
    if (isFullDebug(COMPONENT_NFS_V4))			    \
      {							    \
	char str[LEN_FH_STR];				    \
	sprint_fhandle4(str, fh4);			    \
	LogFullDebug(COMPONENT_NFS_V4, "%s%s", label, str); \
      }							    \
  } while (0)

#endif/* NFS_FILE_HANDLE_H */
