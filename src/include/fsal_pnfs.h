/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Author: Adam C. Emerson
 *         Boaz Harrosh
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
 * ---------------------------------------
 */

/**
 * @file    fsal_pnfs.h
 * @brief   Management of the pNFS features at the FSAL level
 *
 * FSAL based pNFS interfaces
 */

#ifndef FSAL_PNFS_H_
#define FSAL_PNFS_H_

#include "nfs4.h"

/******************************************************
 *               Basic in-memory types
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

/**
 * @brief FSAL view of the NFSv4.1 deviceid4.
 */

struct pnfs_deviceid {
     /** Identifier for the given export.  Currently ganesha uses an
      *  unsigned short as the export identifier, but we want room for
      *  whatever the multi-FSAL work ends up needing. */
     uint64_t export_id;
     /** Low quad of the deviceid, must be unique within a given export. */
     uint64_t devid;
};

/******************************************************
 *         FSAL MDS function argument structs
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
         *  segment to satisfy this operation.  Currently, no production
         *  clients support multiple segments granted by a single
         *  LAYOUTGET operation, so FSALs should grant a single segment
         *  and set this value on the first call. */
        bool last_segment;
        /** On input, this field signifies a request by the client to be
            signaled when a requested but unavailable layout becomes
            available.  In output, it signifies the FSAL's willingness to
            make a callback when the layout becomes available.  We do not
            yet implement callbacks, so it should always be set to
            false. */
        bool signal_available;
};

/**
 * Input parameters to FSAL_layoutreturn
 */

struct fsal_layoutreturn_arg {
        /** Indicates that the client is performing a return of a layout
            it held prior to a server reboot.  As such, cur_segment is
            meaningless (no record of the layout having been granted
            exists). */
        bool reclaim;
        /** The type of layout being returned */
        layouttype4 lo_type;
        /** The return type of the LAYOUTRETURN call.  Meaningless if
         *  synthetic is true. */
        layoutreturn_type4 return_type;
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
        /** Whether this return was synthesized a result of
         *  return_on_close or lease expiration. */
        bool synthetic;
        /** If true, the FSAL must free all resources associated with
         *  res.segment. */
        bool dispose;
        /** After this return, there will be no more layouts associated
         *  with this layout state (that is, there will be no more
         *  layouts for this (clientid, handle, layout type) triple. */
        bool last_segment;
        /** Count of recall tokens.  0 if no LAYOUTRECALLs are
            satisfied. */
        size_t ncookies;
        /** Array of pointers to layout specific data supplied by
         *  LAYOUTRECALL.  If this LAYOUTRETURN completly satisfies
         *  one or more invoked LAYOUTRECALLs, the tokens of the
         *  recalls will be supplied. */
        const void *recall_cookies[];
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
         *  entry after the alst one returned.  Undefined if EOF is
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

#endif /* !FSAL_PNFS_H_ */
