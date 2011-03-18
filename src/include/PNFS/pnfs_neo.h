/*
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributor : bharrosh@panasas.com
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
 * \file    pnfs.h
 * \author  $Author: Boaz $
 * \date    $Date: 2010/01/27 12:44:15 $
 * \brief   Management of the pNFS features.
 *
 * pnfs.h : Management of the pNFS features.
 *
 *
 */

#ifndef _PNFS_H
#define _PNFS_H

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
bool FSAL_is_pnfs_enabled(fsal_pnfs_context_t context);

/* Basic pnfs in-memory types */
struct pnfs_lo_segment {
	u32 lo_type;
	u32 io_mode;
	u64 offset;
	u64 len;
};

struct pnfs_deviceid {
	u64	sbid;			/* FSAL export_root unique ID */
	u64	devid;			/* export_root-wide unique device ID */
};

/* LAYOUT GET OPERATION */
struct pnfs_layoutget_arg {
	u64			lga_minlength;	/* minimum bytes requested */
	u64			lga_sbid;	/* FSAL use this as the sbid
						 * part of the device ID
						 */
};

struct pnfs_layoutget_res {
	/* request/response: At input this contains the Client's preferred range.
	 * On return contains the range given. It should contain at least
	 * offset..offset+lga_minlength.
	 * io_mode: read maybe promoted to read/write
	 * lo_type: Is the format of the layout that will be returned in @xdr.
	 */
	struct pnfs_lo_segment	lgr_seg;

	/* Should layout be returned before CLOSE */
	bool			lgr_return_on_close;
	/* This cookie is returned in FSAL_pnfs_layout_return() when all bytes
	 * Handed here are returned by the client. (This layout was removed from
	 * Ganesha's internal list for this file)
	 */
	long			lgr_layout_cookie;
};

/** pnfs_layout_get: Retrieve and encode a layout for pnfs_file_t, onto the xdr
 *                  stream.
 * Return one of the following nfs errors:
 *      NFS_OK: Success
 *      NFS4ERR_ACCESS: Permission error
 *      NFS4ERR_BADIOMODE: Server does not support requested iomode
 *      NFS4ERR_BADLAYOUT: No layout matching loga_minlength rules
 *      NFS4ERR_INVAL: Parameter other than layout is invalid
 *      NFS4ERR_IO: I/O error
 *      NFS4ERR_LAYOUTTRYLATER: Layout may be retrieved later
 *      NFS4ERR_LAYOUTUNAVAILABLE: Layout unavailable for this file
 *      NFS4ERR_LOCKED: Lock conflict
 *      NFS4ERR_NOSPC: Out-of-space error occurred
 *      NFS4ERR_RECALLCONFLICT:
 *                             Layout currently unavailable due to a
 *                             conflicting CB_LAYOUTRECALL
 *      NFS4ERR_SERVERFAULT: Server went bezerk
 *      NFS4ERR_TOOSMALL: loga_maxcount too small to fit layout
 *      NFS4ERR_WRONG_TYPE: Wrong file type (not a regular file)
 *
 * Comments: Implementer should use one of pnfs_files_encode_layout(),
 *           pnfs_blocks_encode_layout(), or pnfs_objects_encode_layout()
 *           with the passed xdr, and a FSAL supplied layout information.
 */
enum nfsstat4 FSAL_pnfs_layout_get (pnfs_file_t *file, xdr_stream_t *xdr,
			       const struct pnfs_layoutget_arg *arg,
			       struct pnfs_layoutget_res *res);

/*TODO: Support return of multple segments, @res will be an array with an
 *      additional array_size returned.
 */

/* GET_DEVICE_INFO OPERATION */
/** pnfs_get_device_info: Given a pnfs_deviceid, Encode device info onto the xdr
 *                        stream
 * Return one of the appropriate nfs errors.
 * Comments: Implementor should use one of pnfs_filelayout_encode_devinfo(),
 *           pnfs_blocklayout_encode_devinfo(), or pnfs_objects_encode_devinfo()
 *           with the passed xdr, and a FSAL supplied device information.
 */

enum nfsstat4 FSAL_pnfs_get_device_info (fsal_pnfs_context_t, xdr_stream_t *xdr,
				   u32 layout_type,
				   const struct pnfs_deviceid *did);

/* LAYOUT_COMMIT OPERATION */
struct pnfs_layoutcommit_arg {
	struct pnfs_lo_segment	lca_seg;
	u32			lca_reclaim;
	u32			lca_newoffset;
	u64			lca_last_wr; /* The highest byte written by client */
	struct nfstime4		lca_mtime;   /* last modification time */
};

struct pnfs_layoutcommit_res {
	u32			lcr_size_chg;	/* boolean is lcr_newsize set */
	u64			lcr_newsize;	/* The new current file size */
};

/** pnfs_layout_commit: Commit meta-data changes to file
 *	@xdr: In blocks and objects contains the type-specific commit info.
 *	@arg: The passed in parameters (See struct pnfs_layoutcommit_arg)
 *	@res: The returned information (See struct pnfs_layoutcommit_res)
 *
 * Return: one of the appropriate nfs errors.
 * Comments: In some files-layout systems where the DSs are set to return
 *           S_FILE_SYNC for the WRITE operation, or when the COMMIT is through
 *           the MDS, this function may be empty.
 */
enum nfsstat4 FSAL_pnfs_layout_commit (pnfs_file_t *file, xdr_stream_t *xdr,
				  const struct pnfs_layoutcommit_arg *args,
				  struct pnfs_layoutcommit_res *res);

/* LAYOUT_RETURN OPERATION */
struct pnfs_layoutreturn_arg {
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

/** FSAL_pnfs_layout_return: Client Returns the layout
 *
 *	Or a return is simulated by NFS-GANESHA.
 *
 *	@xdr: In blocks and objects contains the type-specific return info.
 *	@arg: The passed in parameters (See struct pnfs_layoutreturn_arg)
 */
enum nfsstat4 FSAL_pnfs_layout_return (pnfs_file_t *file, xdr_stream_t *xdr,
				      const struct pnfs_layoutreturn_arg *args);

/* CB_LAYOUTRECALL facility implemented by NFS-GANESHA */

/* TODO: this enum is from the nfs std */
enum cb_recall_type {
	CBT_FILE,
	CBT_ALL,
	CBT_ANY,
};

enum CBRL_ret {
	CBRL_OK = 0, CBRL_NOT_FOUND,
	CBRL_PROGRESS_MADE, CBRL_ENOMEM, CBRL_ERROR,
};

enum CBRL_search_flags {
	SF_SINGLE_CLIENT = 0,
	SF_ALL_CLIENTS_BUT = 1,
	SF_SIMULATE_ONLY = 2,
};

struct cb_layoutrecall_arg {
	enum recall_type 	cb_type;
	struct pnfs_lo_segment	cb_seg;
	nfs_client_id		cb_client;
	pnfs_file_t		cb_file;
	int 			cb_search_flags;
	void 			*cb_recall_cookie;
};

/** pnfs_cb_layout_recall: filesystems which need to LAYOUT_RECALL an outstanding
list of LAYOUTS, do to clients access conflicts or error conditions.
 */
enum CBRL_ret pnfs_cb_layout_recall(fsal_pnfs_context_t fsal,
				    struct cb_layoutrecall_arg *args);


#endif                          /* _PNFS_H */
