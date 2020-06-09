/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 *
 * @file nfs_proto_tools.c
 * @brief   A set of functions used to managed NFS.
 *
 * nfs_proto_tools.c -  A set of functions used to managed NFS.
 *
 *
 */

#ifndef _NFS_PROTO_TOOLS_H
#define _NFS_PROTO_TOOLS_H

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "nfs_file_handle.h"
#include "sal_data.h"
#include "fsal.h"
#include "posix_acls.h"
#ifdef USE_NFSACL3
#include <sys/acl.h>
#endif				/* USE_NFSACL3 */


/* Hard and soft limit for nfsv4 quotas */
#define NFS_V4_MAX_QUOTA_SOFT 4294967296LL	/*  4 GB */
#define NFS_V4_MAX_QUOTA_HARD 17179869184LL	/* 16 GB */
#define NFS_V4_MAX_QUOTA      34359738368LL	/* 32 GB */

#define  NFS4_ATTRVALS_BUFFLEN  1024

/*
 * Definition of an array for the characteristics of each GETATTR sub-operations
 */

#define FATTR4_ATTR_READ       0x00001
#define FATTR4_ATTR_WRITE      0x00010
#define FATTR4_ATTR_READ_WRITE 0x00011

typedef enum {
	FATTR_XDR_NOOP,
	FATTR_XDR_SUCCESS,
	FATTR_XDR_SUCCESS_EXP,
	FATTR_XDR_FAILED,
	FATTR_BADOWNER
} fattr_xdr_result;

struct xdr_attrs_args {
	struct attrlist *attrs;
	nfs_fh4 *hdl4;
	uint32_t rdattr_error;
	uint64_t mounted_on_fileid;	/*< If this is the root directory
					   of a filesystem, the fileid of
					   the directory on which the
					   filesystem is mounted. */
	/* Static attributes */
	object_file_type_t type;	/*< Object file type */
	fsal_fsid_t fsid;	/*< Filesystem on which this object is
				   stored */
	uint64_t fileid;	/*< Unique identifier for this object within
				   the scope of the fsid, (e.g. inode number) */
	int nfs_status;
	compound_data_t *data;
	bool statfscalled;
	fsal_dynamicfsinfo_t *dynamicinfo;
};

typedef struct fattr4_dent {
	char *name;		/* The name of the operation */
	unsigned int supported;	/* Is this action supported? */
	unsigned int encoded;	/* Can we encode this attribute? */
	unsigned int size_fattr4; /* The size of the dedicated attr subtype */
	unsigned int access;	/* The access type for this attributes */
	attrmask_t attrmask;	/* attr bit for decoding to attrs */
	attrmask_t exp_attrmask; /* attr bit for decoding to attrs in
				    case of exepction */
	fattr_xdr_result(*encode) (XDR * xdr, struct xdr_attrs_args *args);
	fattr_xdr_result(*decode) (XDR * xdr, struct xdr_attrs_args *args);
	fattr_xdr_result(*compare) (XDR * xdr1, XDR * xdr2);
} fattr4_dent_t;

extern const struct fattr4_dent fattr4tab[];

#define WORD0_FATTR4_RDATTR_ERROR (1 << FATTR4_RDATTR_ERROR)
#define WORD1_FATTR4_MOUNTED_ON_FILEID (1 << (FATTR4_MOUNTED_ON_FILEID - 32))

static inline int check_for_wrongsec_ok_attr(struct bitmap4 *attr_request)
{
	if (attr_request->bitmap4_len < 1)
		return true;
	if ((attr_request->map[0] & ~WORD0_FATTR4_RDATTR_ERROR) != 0)
		return false;
	if (attr_request->bitmap4_len < 2)
		return true;
	if ((attr_request->map[1] & ~WORD1_FATTR4_MOUNTED_ON_FILEID) != 0)
		return false;
	if (attr_request->bitmap4_len < 3)
		return true;
	if (attr_request->map[2] != 0)
		return false;
	return true;
}

static inline int check_for_rdattr_error(struct bitmap4 *attr_request)
{
	if (attr_request->bitmap4_len < 1)
		return false;
	if ((attr_request->map[0] & WORD0_FATTR4_RDATTR_ERROR) != 0)
		return true;
	return false;
}

/**
 *
 * Attribute bitmap decoders
 */

/* bitmap is up to 3 x uint32_t.
 *
 * Structure of the bitmap is as follows
 *
 *                  0         1          2
 *    +-------+---------+----------+----------+
 *    | count | 31 .. 0 | 63 .. 32 | 95 .. 64 |
 *    +-------+---------+----------+----------+
 *
 * One bit is set for every possible attributes. The bits are packed together
 * in a uint32_T (XDR alignment reason probably)
 * As said in the RFC3530, the n-th bit is with the uint32_t #(n/32),
 * and its position with the uint32_t is n % 32
 *
 * Example
 *     1st bit = FATTR4_TYPE            = 1
 *     2nd bit = FATTR4_LINK_SUPPORT    = 5
 *     3rd bit = FATTR4_SYMLINK_SUPPORT = 6
 *
 *     Juste one uint32_t is necessay: 2**1 + 2**5 + 2**6 = 2 + 32 + 64 = 98
 *   +---+----+
 *   | 1 | 98 |
 *   +---+----+
 *
 * Other Example
 *
 *     1st bit = FATTR4_LINK_SUPPORT    = 5
 *     2nd bit = FATTR4_SYMLINK_SUPPORT = 6
 *     3rd bit = FATTR4_MODE            = 33
 *     4th bit = FATTR4_OWNER           = 36
 *
 *     Two uint32_t will be necessary there:
 *            #1 = 2**5 + 2**6 = 32 + 64 = 96
 #            #2 = 2**(33-32) + 2**(36-32) = 2**1 + 2**4 = 2 + 16 = 18
 *   +---+----+----+
 *   | 2 | 98 | 18 |
 *   +---+----+----+
 *
 */

static inline int next_attr_from_bitmap(struct bitmap4 *bits, int last_attr)
{
	int offset, bit;

	for (offset = (last_attr + 1) / 32;
	     offset >= 0 && offset < bits->bitmap4_len; offset++) {
		if ((bits->map[offset] & (-1 << ((last_attr + 1) % 32))) != 0) {
			for (bit = (last_attr + 1) % 32; bit < 32; bit++) {
				if (bits->map[offset] & (1 << bit))
					return offset * 32 + bit;
			}
		}
		last_attr = -1;
	}
	return -1;
}

static inline bool attribute_is_set(struct bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if (offset >= bits->bitmap4_len)
		return false;
	return (bits->map[offset] & (1 << (attr % 32))) != 0;
}

static inline bool set_attribute_in_bitmap(struct bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if (offset >= 3)
		return false;	/* over upper bound */
	if (offset >= bits->bitmap4_len)
		bits->bitmap4_len = offset + 1;	/* roll into the next word */
	bits->map[offset] |= (1 << (attr % 32));
	return true;
}

static inline bool clear_attribute_in_bitmap(struct bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if (offset >= bits->bitmap4_len)
		return false;
	bits->map[offset] &= ~(1 << (attr % 32));
	return true;
}

#ifdef _USE_NFS3
void nfs_SetWccData(const struct pre_op_attr *before_attr,
		    struct fsal_obj_handle *entry,
		    wcc_data * pwcc_data);

void nfs_SetPostOpAttr(struct fsal_obj_handle *entry,
		       post_op_attr *attr,
		       struct attrlist *attrs);

void nfs_SetPreOpAttr(struct fsal_obj_handle *entry,
		      pre_op_attr *attr);
#endif

bool nfs_RetryableError(fsal_errors_t fsal_errors);

int nfs3_Sattr_To_FSAL_attr(struct attrlist *pFSALattr, sattr3 *psattr);

void nfs4_Fattr_Free(fattr4 *fattr);

nfsstat4 nfs4_return_one_state(struct fsal_obj_handle *obj,
			       layoutreturn_type4 return_type,
			       enum fsal_layoutreturn_circumstance circumstance,
			       state_t *layout_state,
			       struct pnfs_segment spec_segment,
			       size_t body_len, const void *body_val,
			       bool *deleted);

#define UTF8_SCAN_NONE    0x00	/* do no validation other than size */
#define UTF8_SCAN_NOSLASH 0x01	/* disallow '/' */
#define UTF8_SCAN_NODOT   0x02	/* disallow '.' and '..' */
#define UTF8_SCAN_CKUTF8  0x04	/* validate utf8 */
#define UTF8_SCAN_PATH    0x10	/* validate path length */

/* Do UTF-8 checking if Enforce_UTF8_Validation is true */
#define UTF8_SCAN_STRICT \
	(nfs_param.nfsv4_param.enforce_utf8_vld ? UTF8_SCAN_CKUTF8 : 0)

/* Validate path components, with optional UTF-8 validation */
#define UTF8_SCAN_PATH_COMP \
	(UTF8_SCAN_NOSLASH | UTF8_SCAN_NODOT | UTF8_SCAN_STRICT)

nfsstat4 path_filter(const char *name, int scan);

static inline
nfsstat4 nfs4_utf8string_scan(const utf8string *input, int scan)
{
	if (input->utf8string_val == NULL || input->utf8string_len == 0)
		return NFS4ERR_INVAL;

	if (((scan & UTF8_SCAN_PATH) && input->utf8string_len > MAXPATHLEN) ||
	    (!(scan & UTF8_SCAN_PATH) && input->utf8string_len > MAXNAMLEN))
		return NFS4ERR_NAMETOOLONG;

	/* Nothing else to do */
	if (scan == UTF8_SCAN_NONE || scan == UTF8_SCAN_PATH)
		return NFS4_OK;

	/* utf8strings are now NUL terminated, so it's ok to call the filter */
	return path_filter(input->utf8string_val, scan);
}

int bitmap4_to_attrmask_t(bitmap4 *bitmap4, attrmask_t *mask);

nfsstat4 file_To_Fattr(compound_data_t *data,
		       attrmask_t mask,
		       struct attrlist *attr,
		       fattr4 *Fattr,
		       struct bitmap4 *Bitmap);

bool nfs4_Fattr_Check_Access(fattr4 *, int);
bool nfs4_Fattr_Check_Access_Bitmap(struct bitmap4 *, int);
bool nfs4_Fattr_Supported(fattr4 *);
int nfs4_Fattr_cmp(fattr4 *, fattr4 *);

bool nfs3_Fixup_FSALattr(struct fsal_obj_handle *obj,
			 const struct attrlist *FSAL_attr);

bool is_sticky_bit_set(struct fsal_obj_handle *obj,
		       const struct attrlist *attr);

bool nfs3_Sattr_To_FSALattr(struct attrlist *, sattr3 *);

int nfs4_Fattr_To_FSAL_attr(struct attrlist *, fattr4 *, compound_data_t *);

int nfs4_Fattr_To_fsinfo(fsal_dynamicfsinfo_t *, fattr4 *);

int nfs4_Fattr_Fill_Error(compound_data_t *, fattr4 *, nfsstat4,
			  struct bitmap4 *, struct xdr_attrs_args *args);

bool xdr_nfs4_fattr_fill_error(XDR *xdrs, struct bitmap4 *req_attrmask,
			       nfs_cookie4 cookie, component4 *name,
			       struct xdr_attrs_args *args);
bool xdr_encode_entry4(XDR *xdrs, struct xdr_attrs_args *args,
		       struct bitmap4 *req_bitmap, nfs_cookie4 cookie,
		       component4 *name);
int nfs4_FSALattr_To_Fattr(struct xdr_attrs_args *, struct bitmap4 *,
			   fattr4 *);

void nfs4_bitmap4_Remove_Unsupported(struct bitmap4 *);

enum nfs4_minor_vers {
	NFS4_MINOR_VERS_0,
	NFS4_MINOR_VERS_1,
	NFS4_MINOR_VERS_2
};

void nfs4_pathname4_alloc(pathname4 *, char *);

void nfs4_pathname4_free(pathname4 *);

uint32_t resp_room(compound_data_t *data);

nfsstat4 check_resp_room(compound_data_t *data, uint32_t op_resp_size);

void get_mounted_on_fileid(compound_data_t *data, uint64_t *mounted_on_fileid);

#ifdef USE_NFSACL3
posix_acl *encode_posix_acl(const acl_t acl, uint32_t type,
		struct attrlist *attrs);

acl_t decode_posix_acl(posix_acl *nfs3_acl, uint32_t type);

int nfs3_acl_2_fsal_acl(struct attrlist *attr, nfs3_int32 mask,
		posix_acl *a_acl, posix_acl *d_acl, bool is_dir);
#endif				/* USE_NFSACL3 */


#endif				/* _NFS_PROTO_TOOLS_H */
