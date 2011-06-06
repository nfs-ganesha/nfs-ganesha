/*
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _PNFS_FILES_BLOCKS_XDR_H
#define _PNFS_FILES_BLOCKS_XDR_H

/* FIXME: Needed from pnf_xdr.h
	struct xdr_netobj
	struct pnfs_deviceid
	struct pnfs_fh
	struct list_head
	enum nfsstat4
 */
#include <pnf_xdr.h>

/* the nfsd4_pnfs_devlist dev_addr for the file layout type */
struct pnfs_filelayout_devaddr {
	struct xdr_netobj	r_netid;
	struct xdr_netobj	r_addr;
};

/* list of multipath servers */
struct pnfs_filelayout_multipath {
	u32				fl_multipath_length;
	struct pnfs_filelayout_devaddr 	*fl_multipath_list;
};

struct pnfs_filelayout_device {
	u32					fl_stripeindices_length;
	u32       		 		*fl_stripeindices_list;
	u32					fl_device_length;
	struct pnfs_filelayout_multipath 	*fl_device_list;
};

struct pnfs_filelayout_layout {
	u32                             lg_layout_type; /* response */
	u32                             lg_stripe_type; /* response */
	u32                             lg_commit_through_mds; /* response */
	u64                             lg_stripe_unit; /* response */
	u64                             lg_pattern_offset; /* response */
	u32                             lg_first_stripe_index;	/* response */
	struct pnfs_deviceid		device_id;		/* response */
	u32                             lg_fh_length;		/* response */
	struct pnfs_fh			*lg_fh_list;		/* response */
};

enum stripetype4 {
	STRIPE_SPARSE = 1,
	STRIPE_DENSE = 2
};

enum pnfs_block_extent_state4 {
        PNFS_BLOCK_READWRITE_DATA       = 0,
        PNFS_BLOCK_READ_DATA            = 1,
        PNFS_BLOCK_INVALID_DATA         = 2,
        PNFS_BLOCK_NONE_DATA            = 3
};

enum pnfs_block_volume_type4 {
        PNFS_BLOCK_VOLUME_SIMPLE = 0,
        PNFS_BLOCK_VOLUME_SLICE = 1,
        PNFS_BLOCK_VOLUME_CONCAT = 2,
        PNFS_BLOCK_VOLUME_STRIPE = 3,
};
typedef enum pnfs_block_volume_type4 pnfs_block_volume_type4;

enum bl_cache_state {
	BLOCK_LAYOUT_NEW	= 0,
	BLOCK_LAYOUT_CACHE	= 1,
	BLOCK_LAYOUT_UPDATE	= 2,
};

typedef struct pnfs_blocklayout_layout {
        struct list_head                bll_list;
        struct pnfs_deviceid      bll_vol_id;
        u64                             bll_foff;	// file offset
        u64                             bll_len;
        u64                             bll_soff;	// storage offset
	int				bll_recalled;
        enum pnfs_block_extent_state4   bll_es;
	enum bl_cache_state		bll_cache_state;
} pnfs_blocklayout_layout_t;

typedef struct pnfs_blocklayout_devinfo {
        struct list_head                bld_list;
        pnfs_block_volume_type4         bld_type;
        struct pnfs_deviceid            bld_devid;
        int                             bld_index_loc;
        union {
                struct {
                        u64             bld_offset;
                        u32             bld_sig_len,
                                        *bld_sig;
                } simple;
                struct {
                        u64             bld_start,
                                        bld_len;
                        u32             bld_index;      /* Index of Simple Volume */
                } slice;
                struct {
                        u32             bld_stripes;
                        u64             bld_chunk_size;
                        u32             *bld_stripe_indexs;
                } stripe;
        } u;
} pnfs_blocklayout_devinfo_t;

struct xdr_stream_t;
enum nfsstat4
pnfs_files_encode_layout(struct xdr_stream_t *xdr,
			 const struct pnfs_filelayout_layout *flp);

enum nfsstat4
filelayout_encode_devinfo(struct xdr_stream_t *xdr,
			  const struct pnfs_filelayout_device *fdev);

enum nfsstat4
blocklayout_encode_layout(struct xdr_stream_t *xdr,
			  const struct list_head *bl_head);

enum nfsstat4
blocklayout_encode_devinfo(struct xdr_stream_t *xdr,
			   const struct list_head *volumes);

/* TBD:
blocklayout_decode_layout_commit(...)
blocklayout_decode_layout_return(...)
*/

#endif /* _PNFS_FILES_BLOCKS_XDR_H */
