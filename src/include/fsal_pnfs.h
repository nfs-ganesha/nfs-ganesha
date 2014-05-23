/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Author: Adam C. Emerson
 *	   Boaz Harrosh
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal_pnfs.h
 * @brief pNFS functions and structures used at the FSAL level
 */

#ifndef FSAL_PNFS_H
#define FSAL_PNFS_H

#include "nfs4.h"

/**
 * @page FSAL_PNFS How to pNFS enable your FSAL
 *
 * Meta-data server
 * ================
 *
 * Your FSAL must indicate to callers that it supports pNFS.  Ensure
 * that the @c fs_supports method returns @c true when queried with
 * @c fso_pnfs_mds_supported.
 *
 * You must implement @c getdeviceinfo on the export and may impelment
 * @c getdevicelist, if you wish.  To let clients know what layouts
 * they may request, be sure to implement @c fs_layouttypes.  You
 * should implement @c fs_maximum_segments to inform the protocol
 * layer the maximum number of segments you will ever provide for a
 * single layoutget call.  (The current Linux kernel only supports one
 * segment per LAYOUTGET, unfortunately, so that's a good maximum for
 * now.)  Other hints for the protocol layer are @c fs_loc_body_size
 * (to determine how much space it will allocate for your loc_body XDR
 * stream) and @c fs_da_addr_size (the same thing for da_addr).
 *
 * On @c fsal_obj_handle, you should implement @c layoutget, @c
 * layoutreturn, and @c layoutcommit.  If you want to be able to
 * recall layouts, you will need to send a request of the type
 * @c FSAL_UP_EVENT_LAYOUTRECALL with @c fsal_up_submit.  For details,
 * see the documentation for the FSAL Upcall System.
 *
 * Data server
 * ===========
 *
 * This is only relevant if you are using the LAYOUT4_NFSV4_1_FILES
 * layouts.  If you are using OSD or Object layouts, or plan to use an
 * spNFS-like configuration employing naíve data servers, you do not
 * need to worry about this.
 *
 * Your FSAL must indicate to callers that it supports pNFS DS
 * operations.  Ensure that the @c fs_supports method returns @c true
 * when queried with @c fso_pnfs_ds_supported.
 *
 * You must implement the @c create_ds_handle method on the export.
 * This must create an object of type @c fsal_ds_handle from the NFS
 * handle supplied as part of your layout.  See the @c fsal_ds_handle
 * documentation for details.  You must implement the @c release, @c
 * read, @c write, and @c commit methods.
 */

/******************************************************
 *		 Basic in-memory types
 ******************************************************/

/**
 * @brief Represent a layout segment
 *
 * This structure not only represents segments granted by the FSAL or
 * being committed or returned, but also selectors as used in
 * LAYOUTRETURN4_FILE.
 */

struct pnfs_segment {
	/** The IO mode (must be read or write) */
	layoutiomode4 io_mode;
	/** The offset of the segment */
	uint64_t offset;
	/** The length of the segment */
	uint64_t length;
};

enum fsal_id {
	/** The following FSAL_ID implies no PNFS support. */
	FSAL_ID_NO_PNFS = 0,
	/** The following ID is to be used by out of tree implementations
	  * during an experimental phase before we are able to add an
	  * official FSAL_ID.
	  */
	FSAL_ID_EXPERIMENTAL = 1,
	FSAL_ID_VFS = 2,
	FSAL_ID_GPFS = 3,
	FSAL_ID_CEPH = 4,
	FSAL_ID_LUSTRE = 5,
	FSAL_ID_GLUSTER = 6,
	FSAL_ID_COUNT
};

struct fsal_module;

struct fsal_module *pnfs_fsal[FSAL_ID_COUNT];

/**
 * @brief FSAL view of the NFSv4.1 deviceid4.
 *
 * Note that this will be encoded as an opaque, thus the byte order on the wire
 * will be host order NOT network order.
 */

struct pnfs_deviceid {
	/** FSAL_ID - to dispatch getdeviceinfo based on */
	uint8_t fsal_id;
	/** Break up the remainder into useful chunks */
	uint8_t device_id1;
	uint16_t device_id2;
	uint32_t device_id4;
	uint64_t devid;
};

#define DEVICE_ID_INIT_ZERO(fsal_id) {fsal_id, 0, 0, 0, 0}

/******************************************************
 *	   FSAL MDS function argument structs
 ******************************************************/

/**
 * @brief Input parameters to FSAL_layoutget
 */

struct fsal_layoutget_arg {
	/** The type of layout being requested */
	layouttype4 type;
	/** The minimum length that must be granted if a layout is to be
	 *  granted at all. */
	uint64_t minlength;
	/** Ths FSAL must use this value (in network byte order) as the
	 *  high quad of any deviceid4 it returns in the loc_body. */
	uint64_t export_id;
	/** The maximum number of bytes the client is willing to accept
	    in the response, including XDR overhead. */
	uint32_t maxcount;
};

/**
 * In/out and output parameters to FSAL_layoutget
 */

struct fsal_layoutget_res {
	/** As input, the offset, length, and iomode requested by the
	 *  caller. As output, the offset, length, and iomode of a given
	 *  segment granted by the FSAL. */
	struct pnfs_segment segment;
	/** Whatever value the FSAL stores here is saved with the segment
	 *  and supplied to it on future calls to LAYOUTCOMMIT and
	 *  LAYOUTRETURN.  Any memory allocated must be freed on layout
	 *  disposal. */
	void *fsal_seg_data;
	/** Whether the layout should be returned on last close.  Note
	 *  that this flag being set on one segment makes all layout
	 *  segments associated with the same stateid return_on_close. */
	bool return_on_close;
	/** This pointer is NULL on the first call FSAL_layoutget.  The
	 *  FSAL may store a pointer to any data it wishes, and this
	 *  pointer will be supplied to future calls to FSAL_layoutget
	 *  that serve the same LAYOUTGET operation.  The FSAL must
	 *  de-allocate any memory it allocated when it sets the
	 *  last_segment flag */
	void *context;
	/** The FSAL must set this to true when it has granted the last
	 *  segment to satisfy this operation.	Currently, no production
	 *  clients support multiple segments granted by a single
	 *  LAYOUTGET operation, so FSALs should grant a single segment
	 *  and set this value on the first call. */
	bool last_segment;
	/** On input, this field signifies a request by the client to be
	 *  signaled when a requested but unavailable layout becomes
	 *  available.	In output, it signifies the FSAL's willingness to
	 *  make a callback when the layout becomes available.	We do not
	 *  yet implement callbacks, so it should always be set to
	 *  false. */
	bool signal_available;
};

/**
 * @brief Circumstance that triggered the layoutreturn
 */

enum fsal_layoutreturn_circumstance {
	/** Return initiated by client call. */
	circumstance_client,
	/** Indicates that the client is performing a return of a
	 *  layout it held prior to a server reboot.  As such,
	 *  cur_segment is meaningless (no record of the layout having
	 *  been granted exists). */
	circumstance_reclaim,
	/** This is a return following from the last close on a file
	    with return_on_close layouts. */
	circumstance_roc,
	/** The client has behaved badly and we are taking its layout
	    away forcefully. */
	circumstance_revoke,
	/** The client forgot this layout and requested a new layout
	    on the same file without an layout stateid. */
	circumstance_forgotten,
	/** This layoutrecall is a result of system shutdown */
	circumstance_shutdown
};

/**
 * Input parameters to FSAL_layoutreturn
 */

struct fsal_layoutreturn_arg {
	/** The type of layout being returned */
	layouttype4 lo_type;
	/** The return type of the LAYOUTRETURN call.  Meaningless
	    if fsal_layoutreturn_synthetic is set. */
	layoutreturn_type4 return_type;
	/** The circumstances under which the return was triggered. */
	enum fsal_layoutreturn_circumstance circumstance;
	/** Layout for specified for return.  This need not match any
	 *  actual granted layout.  Offset and length are set to 0 and
	 *  NFS4_UINT64_MAX in the case of bulk or synthetic returns.
	 *  For synthetic returns, the io_mode is set to
	 *  LAYOUTIOMODE4_ANY. */
	struct pnfs_segment spec_segment;
	/** The current segment in the return iteration which is to be
	 *  returned. */
	struct pnfs_segment cur_segment;
	/** Pointer to layout specific data supplied by LAYOUTGET.  If
	 *  dispose is true, any memory allocated for this value must be
	 *  freed. */
	void *fsal_seg_data;
	/** If true, the FSAL must free all resources associated with
	 *  res.segment. */
	bool dispose;
	/** After this return, there will be no more layouts associated
	 *  with this layout state (that is, there will be no more
	 *  layouts for this (clientid, handle, layout type) triple. */
	bool last_segment;
	/** Count of recall tokens.  0 if no LAYOUTRECALLs are
	 *  satisfied. */
	size_t ncookies;
	/** Array of pointers to layout specific data supplied by
	 *  LAYOUTRECALL.  If this LAYOUTRETURN completly satisfies
	 *  one or more invoked LAYOUTRECALLs, the tokens of the
	 *  recalls will be supplied. */
	const void *recall_cookies[1];
};

/**
 * Input parameters to FSAL_layoutcommit
 */

struct fsal_layoutcommit_arg {
	/** The type of the layout being committed */
	layouttype4 type;
	/** The segment being committed on this call */
	struct pnfs_segment segment;
	/** Pointer to layout specific data supplied by LAYOUTGET. */
	void *fsal_seg_data;
	/** True if this is a reclaim commit */
	bool reclaim;
	/** True if the client has suggested a new offset */
	bool new_offset;
	/** The offset of the last byte written, if new_offset if set,
	 *  otherwise undefined. */
	uint64_t last_write;
	/** True if the client provided a new value for mtime */
	bool time_changed;
	/** If new_time is true, the client-supplied modification tiem
	 *  for the file.  otherwise, undefined. */
	nfstime4 new_time;
};

/**
 * In/out and output parameters to FSAL_layoutcommit
 */

struct fsal_layoutcommit_res {
	/** A pointer, NULL on the first call to FSAL_layoutcommit.  The
	 *  FSAL may store whatever it wishes in this field and it will
	 *  be supplied on all subsequent calls.  If the FSAL has
	 *  allocated any memory, this memory must be freed if
	 *  commit_done is set. */
	void *context;
	/** True if the FSAL is returning a new file size */
	bool size_supplied;
	/** The new file size returned by the FSAL */
	uint64_t new_size;
	/** The FSAL has completed the LAYOUTCOMMIT operation and
	 *  FSAL_layoutcommit need not be called again, even if more
	 *  segments are left in the layout. */
	bool commit_done;
};

/**
 * In/out and output parameters to FSAL_getdevicelist
 */

struct fsal_getdevicelist_res {
	/** Input, cookie indicating position in device list from which
	 *  to begin.  Output, cookie that may be supplied to get the
	 *  entry after the alst one returned.	Undefined if EOF is
	 *  set. */
	uint64_t cookie;
	/** For any non-zero cookie, this must be the verifier returned
	 *  from a previous call to getdevicelist.  The FSAL may use this
	 *  value to verify that the cookie is not out of date. A cookie
	 *  verifier may be supplied by the FSAL on output. */
	uint64_t cookieverf;
	/** True if the last deviceid has been returned. */
	bool eof;
};

#endif				/* !FSAL_PNFS_H */
/** @} */
