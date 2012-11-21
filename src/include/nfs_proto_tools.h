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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 *
 * \file nfs_proto_tools.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:23 $
 * \version $Revision: 1.9 $
 * \brief   A set of functions used to managed NFS.
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
#include <sys/file.h>           /* for having FNDELAY */
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "cache_inode.h"
#include "nfs_tools.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "sal_data.h"

/* type flag into mode field */
#define NFS2_MODE_NFDIR 0040000
#define NFS2_MODE_NFCHR 0020000
#define NFS2_MODE_NFBLK 0060000
#define NFS2_MODE_NFREG 0100000
#define NFS2_MODE_NFLNK 0120000
#define NFS2_MODE_NFNON 0140000

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

	for(offset = (last_attr + 1) / 32;
	    offset >= 0 && offset < bits->bitmap4_len;
	    offset++) {
		if((bits->map[offset] &
		    (-1 << ((last_attr +1) % 32))) != 0) {
			for(bit = (last_attr +1) % 32; bit < 32; bit++) {
				if(bits->map[offset] & (1 << bit))
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

	if(offset >= bits->bitmap4_len)
		return FALSE;
	return !!(bits->map[offset] & (1 << (attr % 32)));
}

static inline bool set_attribute_in_bitmap(struct bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= 3)
		return FALSE; /* over upper bound */
	if(offset >= bits->bitmap4_len)
		bits->bitmap4_len = offset + 1; /* roll into the next word */
	bits->map[offset] |= (1 << (attr % 32));
	return TRUE;
}

static inline bool clear_attribute_in_bitmap(struct bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= bits->bitmap4_len)
		return FALSE;
	bits->map[offset] &= ~(1 << (attr % 32));
	return TRUE;
}

void nfs_FhandleToStr(u_long     rq_vers,
                      nfs_fh3   *pfh3,
                      nfs_fh4   *pfh4,
                      char      *str);

void nfs_SetWccData(const struct pre_op_attr *before_attr,
                    cache_entry_t *entry,
                    struct req_op_context *ctx,
                    wcc_data *pwcc_data);

void nfs_SetPostOpAttr(cache_entry_t *entry,
                       struct req_op_context *ctx,
                       post_op_attr *attr);

int nfs_SetPostOpXAttrDir(cache_entry_t *entry,
                          const struct req_op_context *ctx,
                          post_op_attr *result);

int nfs_SetPostOpXAttrFile(exportlist_t * pexport,
                           const struct attrlist *pfsal_attr, post_op_attr * presult);

void nfs_SetPreOpAttr(cache_entry_t *entry,
                      struct req_op_context *ctx,
                      pre_op_attr *attr);

int nfs_RetryableError(cache_inode_status_t cache_status);

int nfs3_Sattr_To_FSAL_attr(struct attrlist *pFSALattr,
                            sattr3 *psattr);

uint32_t nfs_get_access_mask(uint32_t op,
                             const object_file_type_t type);

void nfs3_access_debug(char *label, uint32_t access);

void nfs4_access_debug(char *label, uint32_t access, fsal_aceperm_t v4mask);
void nfs4_Fattr_Free(fattr4 *fattr);


nfsstat4 nfs4_return_one_state(cache_entry_t *entry,
                               struct req_op_context *req_ctx,
                               bool synthetic,
                               bool reclaim,
                               layoutreturn_type4 return_type,
                               state_t *layout_state,
                               struct pnfs_segment spec_segment,
                               size_t body_len,
                               const void* body_val,
                               bool *deleted,
                               bool hold_lcok);
nfsstat4 nfs4_sanity_check_FH(compound_data_t *data,
                              object_file_type_t required_type,
                              bool ds_allowed);

typedef enum {
        UTF8_SCAN_NONE = 0,    /* do no validation other than size */
        UTF8_SCAN_NOSLASH = 1, /* disallow '/' */
        UTF8_SCAN_NODOT = 2,   /* disallow '.' and '..' */
        UTF8_SCAN_CKUTF8 = 4,  /* validate utf8 */
        UTF8_SCAN_SYMLINK = 6, /* a symlink, allow '/', no "." or "..", utf8 */
        UTF8_SCAN_NAME = 3,    /* a name (no embedded /, "." or "..") */
        UTF8_SCAN_ALL = 7      /* do the whole thing, name+valid utf8 */
} utf8_scantype_t;

nfsstat4 utf8dup(utf8string * newstr, utf8string * oldstr,
                 utf8_scantype_t scan);
nfsstat4 nfs4_utf8string2dynamic(const utf8string *input,
                                 utf8_scantype_t scan,
                                 char **obj_name);

nfsstat4 nfs4_sanity_check_saved_FH(compound_data_t *data,
                                    object_file_type_t required_type,
                                    bool ds_allowed);
#endif                          /* _NFS_PROTO_TOOLS_H */

