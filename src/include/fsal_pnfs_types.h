/*
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 */

/**
 * \file    fsal_pnfs_types.h
 * \brief   Management of the pNFS features: FSAL/PNFS types.
 *
 * fsal_pnfs_types.h : Management of the pNFS features: FSAL/PNFS types.
 *
 *
 */

#ifndef _FSAL_PNFS_TYPES_H
#define _FSAL_PNFS_TYPES_H

/* FIXME: These are all wrongly ganesha name-conventioned and definitions.
 * Will fix in next iterations.
 */

/* Philippe: xdr.h (attached) introduces the xdr_stream_t struct and proposed
             helpers that facilitate in xdr encoding decoding.
             I'm putting this one just to make things easier on the eye,
             and it's what we are used too in Kernel. I think this thing can
             be good down the line when we want to support aio type of scattered
             dynamic XDR memory allocations. See at xdr.h for proposed helpers
             that facilitate in xdr encoding decoding.
 */
#include <xdr.h>

/* Philippe: I'm using fsal_pnfs_context_t below to mean the "super_block" or
 *	     "export_root" It's the same one that was received in create or
 *	     open.
 */
typedef fsal_op_context_t fsal_pnfs_context_t

/* It is assumed that a pnfs_file_t has a back pointer to it's
   parent fsal_pnfs_context_t.
*/
typedef fsal_handle_t fsal_pnfs_file_t

/* This is the main switch. If false returned, Ganesha will not enable pnfs
 * export extensions.
 */


/* Basic pnfs in-memory types */
struct fsal_pnfs_lo_segment {
	u32 lo_type;
	u32 io_mode;
	u64 offset;
	u64 len;
};

struct fsal_pnfs_deviceid {
	u64	sbid;			/* FSAL export_root unique ID */
	u64	devid;			/* export_root-wide unique device ID */
};

/* LAYOUT GET OPERATION */
struct fsal_pnfs_layoutget_arg {
	u64			lga_minlength;	/* minimum bytes requested */
	u64			lga_sbid;	/* FSAL use this as the sbid
						 * part of the device ID
						 */
};

struct fsal_pnfs_layoutget_res {
	/* request/response: At input this contains the Client's preferred range.
	 * On return contains the range given. It should contain at least
	 * offset..offset+lga_minlength.
	 * io_mode: read maybe promoted to read/write
	 * lo_type: Is the format of the layout that will be returned in @xdr.
	 */
	struct fsal_pnfs_lo_segment	lgr_seg;

	/* Should layout be returned before CLOSE */
	bool			lgr_return_on_close;
	/* This cookie is returned in FSAL_pnfs_layout_return() when all bytes
	 * Handed here are returned by the client. (This layout was removed from
	 * Ganesha's internal list for this file)
	 */
	long			lgr_layout_cookie;
};

/* LAYOUT_COMMIT OPERATION */
struct fsal_pnfs_layoutcommit_arg {
	struct fsal_pnfs_lo_segment	lca_seg;
	u32			lca_reclaim;
	u32			lca_newoffset;
	u64			lca_last_wr; /* The highest byte written by client */
	struct nfstime4		lca_mtime;   /* last modification time */
};

struct fsal_pnfs_layoutcommit_res {
	u32			lcr_size_chg;	/* boolean is lcr_newsize set */
	u64			lcr_newsize;	/* The new current file size */
};


/* LAYOUT_RETURN OPERATION */
struct fsal_pnfs_layoutreturn_arg {
	/* The byte range and io_mode returned */
	const struct pnfs_lo_segment lra_seg;
	/* This cookie was handed out by the pnfs_layout_get call
	 * It is only returned when the last byte of the layout is dereferenced
	 */
	void *lra_layout_cookie;
	/* This cookie was handed to the pnfs_cb_layout_recall call.
	 * When returned it means the recall was fully satisfied
	 */
	void *lra_recall_cookie;
	/* The return is part of a client expiration */
	bool lra_fence_off;
	/* The layout list of this file is now empty */
	bool lra_is_last;
};

#endif                          /* _FSAL_PNFS_TYPES_H */
