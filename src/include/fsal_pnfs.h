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
 * \file    fsal_pnfs.h
 * \brief   Management of the pNFS features at the FSAL level
 *
 * fsal_pnfs.h: FSAL based pNFS interfaces
 */

#ifndef _FSAL_PNFS_H
#define _FSAL_PNFS_H

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <stdint.h>
#include "pnfs_common.h"

#include "nfs4.h"

#ifdef _USE_NFS4_1

#ifdef _USE_LUSTRE
#include "FSAL/FSAL_LUSTRE/fsal_pnfs_types.h"
#endif 

#ifdef _USE_CEPH
#include "FSAL/FSAL_CEPH/fsal_pnfs_types.h"
#endif 

/******************************************************
 *         FSAL MDS function argument structs
 ******************************************************/

/**
 * \brief Input parameters to FSAL_layoutget
 */

struct fsal_layoutget_arg {
     /** The type of layout being requested */
     layouttype4 type;
     /** The minimum length that must be granted if a layout is to be
      *  granted at all. */
     length4 minlength;
     /** Ths FSAL must use this value (in network byte order) as the
      *  high quad of any deviceid4 it returns in the loc_body. */
     uint64_t export_id;
     /** The maximum number of bytes the client is willing to accept
         in the response, including XDR overhead. */
     count4 maxcount;
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
     fsal_boolean_t return_on_close;
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
     fsal_boolean_t last_segment;
     /** On input, this field signifies a request by the client to be
         signaled when a requested but unavailable layout becomes
         available.  In output, it signifies the FSAL's willingness to
         make a callback when the layout becomes available.  We do not
         yet implement callbacks, so it should always be set to
         false. */
     fsal_boolean_t signal_available;
};

/**
 * Input parameters to FSAL_layoutreturn
 */

struct fsal_layoutreturn_arg {
     /** Indicates that the client is performing a return of a layout
         it held prior to a server reboot.  As such, cur_segment is
         meaningless (no record of the layout having been granted
         exists). */
     fsal_boolean_t reclaim;
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
     fsal_boolean_t synthetic;
     /** If true, the FSAL must free all resources associated with
      *  res.segment. */
     fsal_boolean_t dispose;
     /** After this return, there will be no more layouts associated
      *  with this layout state (that is, there will be no more
      *  layouts for this (clientid, handle, layout type) triple. */
     fsal_boolean_t last_segment;
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
     fsal_boolean_t reclaim;
     /** True if the client has suggested a new offset */
     fsal_boolean_t new_offset;
     /** The offset of the last byte written, if new_offset if set,
      *  otherwise undefined. */
     offset4 last_write;
     /** True if the client provided a new value for mtime */
     fsal_boolean_t time_changed;
     /** If new_time is true, the client-supplied modification tiem
      *  for the file.  otherwise, undefined. */
     fsal_time_t new_time;
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
     fsal_boolean_t size_supplied;
     /** The new file size returned by the FSAL */
     length4 new_size;
     /** The FSAL has completed the LAYOUTCOMMIT operation and
      *  FSAL_layoutcommit need not be called again, even if more
      *  segments are left in the layout. */
     fsal_boolean_t commit_done;
};

/**
 * Input parameters to FSAL_getdevicelist
 */

struct fsal_getdevicelist_arg {
     /** The ID of the export on which the device list is requested */
     uint64_t export_id;
     /** The type of layout for which a device list is being
      *  requested */
     layouttype4 type;
};

/**
 * struct fsal_getdevicelist_res: In/out and output parameters to
 * FSAL_getdevicelist
 */

struct fsal_getdevicelist_res {
     /** Input, cookie indicating position in device list from which
      *  to begin.  Output, cookie that may be supplied to get the
      *  entry after the alst one returned.  Undefined if EOF is
      *  set. */
     nfs_cookie4 cookie;
     /** For any non-zero cookie, this must be the verifier returned
      *  from a previous call to getdevicelist.  The FSAL may use this
      *  value to verify that the cookie is not out of date. A cookie
      *  verifier may be supplied by the FSAL on output. */
     verifier4 cookieverf;
     /** True if the last deviceid has been returned. */
     fsal_boolean_t eof;
     /** Input, the number of devices requested (and the number of
      *  devices there is space for).  Output, the number of devices
      *  supplied by the FSAL. */
     unsigned int count;
     /** An array of the low quads of deviceids.  The high quad will
      *  be supplied by nfs41_op_getdevicelist, derived from the
      *  export. */
     uint64_t *devids;
};

/**
 * Pointers to FSAL implementations of pNFS MDS functions
 */

typedef struct fsal_mdsfunctions__ {
     /**
      * \brief Grant a layout segment.
      *
      * This function is called by nfs41_op_layoutget.  It may be
      * called multiple times, to satisfy a request with multiple
      * segments.  The FSAL may track state (what portion of the
      * request has been or remains to be satisfied or any other
      * information it wishes) in the bookkeeper member of res.  Each
      * segment may have FSAL-specific information associated with it
      * its segid.  This segid will be supplied to the FSAL when the
      * segment is committed or returned.  When the granting the last
      * segment it intends to grant, the FSAL must set the
      * last_segment flag in res.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success
      *   \retval Valid error codes in RFC 5661, pp. 366-7.
      */
     nfsstat4 (*layoutget)(
          /** [IN] The handle of the file on which the layout is
           *  requested. */
          fsal_handle_t *handle,
          /** [IN] The FSAL operation context.*/
          fsal_op_context_t *context,
          /** [OUT] An XDR stream to which the FSAL must encode the
           *  layout specific portion of the granted layout
           *  segment. */
          XDR *loc_body,
          /** [IN] Input arguments of the function */
          const struct fsal_layoutget_arg *arg,
          /** [IN,OUT] In/out and output arguments of the function */
          struct fsal_layoutget_res *res);

     /**
      * \brief Potentially return one layout segment
      *
      * This function is called once on each segment matching the IO
      * mode and intersecting the range specified in a LAYOUTRETURN
      * operation or for all layouts corresponding to a given stateid
      * on last close, leas expiry, or a layoutreturn with a
      * return-type of FSID or ALL.  Whther it is called in the former
      * or latter case is indicated by the synthetic flag in the arg
      * structure, with synthetic being true in the case of last-close
      * or lease expiry.
      *
      * If arg->dispose is true, all resources associated with the
      * layout must be freed.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, p. 367.
      */
     nfsstat4 (*layoutreturn)(
          /** [IN] The handle corresponding to the segment being
           *  returned */
          fsal_handle_t* handle,
          /** [IN] The FSAL operation context */
          fsal_op_context_t* context,
          /** [IN] In the case of a non-synthetic return, this is an
           *  XDR stream corresponding to the layout type-specific
           *  argument to LAYOUTRETURN.  In the case of a synthetic or
           *  bulk return, this is a NULL pointer. */
          XDR *lrf_body,
          /** [IN] Input arguments of the function */
          const struct fsal_layoutreturn_arg *arg);


     /**
      * \brief Commit a segment of a layout
      *
      * This function is called once on every segment of a layout.
      * The FSAL may avoid being called again after it has finished
      * all tasks necessary for the commit by setting res->commit_done
      * to TRUE.
      *
      * The calling function does not inspect or act on the value of
      * size_supplied or new_size until after the last call to
      * FSAL_layoutcommit.
      *
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, p. 366.
      */
     nfsstat4 (*layoutcommit)(
          /** [IN] Handle for the file being committed */
          fsal_handle_t *handle,
          /** [IN] The FSAL operation context */
          fsal_op_context_t *context,
          /** [IN] An XDR stream containing the layout type-specific
           *  portion of the LAYOUTCOMMIT arguments.*/
          XDR *lou_body,
          /** [IN] Input arguments to the function */
          const struct fsal_layoutcommit_arg *arg,
          /** [IN,OUT] In/out and output arguments to the function */
          struct fsal_layoutcommit_res *res);

     /**
      * \brief Get information about a pNFS device
      *
      * When this function is called, the FSAL should write device
      * information to the da_addr_body stream.
      *
      *
      * \return An NFSv4.1 status code.  NFS4_OK on success.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, p. 365.
      */
     nfsstat4 (*getdeviceinfo)(
          /** [IN] The FSAL operation context */
          fsal_op_context_t *context,
          /** [OUT] An XDR stream to which the FSAL is to write the
           *  layout type-specific information corresponding to the
           *  deviceid. */
          XDR* da_addr_body,
          /** [IN] The type of layout that specified the device */
          const layouttype4 type,
          /** [IN] The deviceid */
          const struct pnfs_deviceid *deviceid);

     /**
      * \brief Get list of available devices
      *
      * This function should populate res->devids with from 0 to
      * res->count values representing the low quad of deviceids of
      * which it wishes to make the caller available, then set
      * res->count to the number it returned.  if it wishes to return
      * more than the caller has provided space for, it should set
      * res->eof to false and place a suitable value in res->cookie.
      *
      * If it wishes to return no deviceids, it may set res->count to
      * 0 and res->eof to TRUE on any call.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, pp. 365-6.
      */
     nfsstat4 (*getdevicelist)(
          /** [IN] Filehandle on the filesystem for which deviceids
           *  are requested */
          fsal_handle_t *handle,
          /** [IN] FSAL operation context */
          fsal_op_context_t *context,
          /** [IN] Input arguments of the function */
          const struct fsal_getdevicelist_arg *arg,
          /** [IN,OUT] In/out and output arguments of the function */
          struct fsal_getdevicelist_res *res);
} fsal_mdsfunctions_t;

/*
 * XXX This is built on the assumption of a single FSAL.  This
 * XXX variable must be removed and references to it must be updated
 * XXX after the Lieb Rearchitecture.
 */
#ifdef _PNFS_MDS
extern fsal_mdsfunctions_t fsal_mdsfunctions;
#endif /* _PNFS_MDS */

/**
 * Pointers to FSAL implementations of pNFS DS functions
 */

typedef struct fsal_dsfunctions__ {
     /**
      *
      * \brief Read from a data-server filehandle.
      *
      * NFSv4.1 data server filehandles are disjount from normal
      * filehandles (in Ganesha, there is a ds_flag in the
      * filehandle_v4_t structure) and do not get loaded into
      * cache_inode or processed the normal way.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, pp. 371.
      */
  nfsstat4 (*DS_read)(
       /** [IN] FSAL file handle */
       fsal_handle_t *handle,
       /** [IN] Operation context */
       fsal_op_context_t *context,
       /** [IN] The stateid supplied with the
        *  READ operation, for validation */
       const stateid4 *stateid,
       /** [IN] Offset at which to read */
       offset4 offset,
       /** [IN] Length of read requested (and size of buffer) */
       count4 requested_length,
       /** [OUT] Buffer to which read data is stored */
       caddr_t buffer,
       /** [OUT] Amount of data actually read */
       count4 *supplied_length,
       /** [OUT] End of file was reached */
       fsal_boolean_t *end_of_file);

     /**
      *
      * \brief Write to a data-server filehandle.
      *
      * NFSv4.1 data server filehandles are disjount from normal
      * filehandles (in Ganesha, there is a ds_flag in the
      * filehandle_v4_t structure) and do not get loaded into
      * cache_inode or processed the normal way.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, pp. 376.
      */
     nfsstat4 (*DS_write)(
          /** [IN] FSAL file handle */
          fsal_handle_t *handle,
          /** [IN] Operation context */
          fsal_op_context_t *context,
          /** [IN] The stateid supplied with the WRITE operation, for
           *  validation */
          const stateid4 *stateid,
          /** [IN] Offset at which to write */
          offset4 offset,
          /** [IN] Length of write data */
          count4 write_length,
          /** [OUT] Buffer from which written data is fetched */
          const caddr_t buffer,
          /** [IN] Stability of write requested */
          stable_how4 stability_wanted,
          /** [OUT] Amount of data actually written */
          count4 *written_length,
          /** [OUT] Write verifier */
          verifier4 *writeverf,
          /** [OUT] Stability of write performed */
          stable_how4 *stability_got);

     /**
      *
      * \brief Commit a byte range
      *
      * NFSv4.1 data server filehandles are disjount from normal
      * filehandles (in Ganesha, there is a ds_flag in the
      * filehandle_v4_t structure) and do not get loaded into
      * cache_inode or processed the normal way.
      *
      * \return An NFSv4.1 status code.
      *   \retval NFS4_OK on success.
      *   \retval Valid error codes in RFC 5661, pp. 376.
      */
  nfsstat4 (*DS_commit)(
       /** [IN] FSAL file handle */
       fsal_handle_t *handle,
       /** [IN] Operation context */
       fsal_op_context_t *context,
       /** [IN] Start of commit window */
       offset4 offset,
       /** [IN] Number of bytes to commit */
       count4 count,
       /** [OUT] Write verifier */
       verifier4 *writeverf);
} fsal_dsfunctions_t;

/*
 * XXX This is built on the assumption of a single FSAL.  This
 * XXX variable must be removed and references to it must be updated
 * XXX after the Lieb Rearchitecture.
 */
#ifdef _PNFS_DS
extern fsal_dsfunctions_t fsal_dsfunctions;
#endif /* _PNFS_DS */

fsal_mdsfunctions_t FSAL_GetMDSFunctions(void);
void FSAL_LoadMDSFunctions(void);

fsal_dsfunctions_t FSAL_GetDSFunctions();
void FSAL_LoadDSFunctions(void);

#endif /* _USE_NFS4_1 */

#endif /* _FSAL_PNFS_H */
