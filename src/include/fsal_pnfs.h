/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Contributor: Adam C. Emerson
 *              Boaz Harrosh
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
 * \file    fsal_pnfs.h
 * \brief   Management of the pNFS features at the FSAL level
 *
 * fsal_pnfs.h: FSAL based pNFS interfaces
 *
 *
 */

#ifndef _FSAL_PNFS_H
#define _FSAL_PNFS_H

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "nfs4.h"

#ifdef _USE_FSALMDS

/******************************************************
 *               Basic in-memory types
 ******************************************************/

/**
 * fsal_segment_t: Represent a layout segment to the FSAL
 */

typedef struct fsal_segment
{
  layoutiomode4 io_mode; /**< The IO mode (must be read or write) */
  offset4 offset; /**< The offset of the segment */
  length4 length; /**< The length of the segment */
  fsal_layout_segid_t segid; /**< A value meaningful to the FSAL but
                                  opaque to the rest of Ganesha that
                                  will be supplied in future calls to
                                  return or commit this segment. */
} fsal_segment_t;

/**
 * fsal_deviceid_t: FSAL view of the NFSv4.1 deviceid4.
 */

typedef struct fsal_deviceid
{
  uint64_t export_id; /**< Identifier for the given export.  Currently
                           ganesha uses an unsigned short as the export
                           identifier, but we want room for whatever
                           the multi-FSAL work ends up needing. */
  uint64_t devid; /**< Low quad of the deviceid, must be unique
                       within a given export. */
} fsal_deviceid_t;

/******************************************************
 *                 FSAL MDS functions
 ******************************************************/


/**
 * struct fsal_layoutget_arg: Input parameters to FSAL_layoutget
 */

struct fsal_layoutget_arg
{
  layouttype4 type; /**< The type of layout being requested */
  length4 minlength; /**< The minimum length that must be granted if
                          a layout is to be granted at all. */
  unsigned short export_id; /**< Ths FSAL must use this value (in
                                 network byte order) as the high quad
                                 of any deviceid4 it returns in the
                                 loc_body. */
};

/**
 * struct fsal_layoutget_res: Output and in/out parameters to
 * FSAL_layoutget
 */

struct fsal_layoutget_res
{
  fsal_segment_t segment; /**< As input, the offset, length, and iomode
                               requested by the caller.  As output, the
                               offset, length, and iomode of a given
                               segment granted by the FSAL. */
  fsal_boolean_t return_on_close; /**< Whether the layout should be
                                       returned on last close.  Note
                                       that this flag being set on one
                                       segment makes all layout
                                       segments associated with the
                                       same stateid
                                       return_on_close. */
  fsal_multiget_mark_t bookkeeper; /**< Pointer to FSAL-specified tracking data
                                        used between calls
                                        FSAL_layoutget that serve a
                                        single operation. */
  fsal_boolean_t last_segment; /***< The FSAL must set this to TRUE when
                                     it has granted the last segment
                                     to satisfy this operation. */
};

/**
 * FSAL_layoutget: Grant a layout segment.
 *
 * This function is called by nfs41_op_layoutget.  It may be called
 * multiple times, to satisfy a request with multiple segments.  The
 * FSAL may track state (what portion of the request has been or
 * remains to be satisfied or any other information it wishes) in the
 * bookkeeper member of res.  Each segment may have FSAL-specific
 * information associated with it its segid.  This segid will be
 * supplied to the FSAL when the segment is committed or returned.
 * When the granting the last segment it intends to grant, the FSAL
 * must set the last_segment flag in res.
 *
 * @param handle     [IN]     The handle of the file on which the layout is
 *                            requested.
 * @param context    [IN]     The FSAL operation context.
 * @param loc_body   [OUT]    An XDR stream to which the FSAL must encode
 *                            the layout specific portion of the granted
 *                            layout segment.
 * @param arg        [IN]     Input arguments of the function
 * @param res        [IN,OUT] In/out and output arguments of the
 *                            function
 * @return An NFSv4.1 status code.  NFS4_OK on success.
 */

nfsstat4 FSAL_layoutget(fsal_handle_t *handle,
                        fsal_op_context_t *context,
                        XDR *loc_body,
                        const struct fsal_layoutget_arg *arg,
                        struct fsal_layoutget_res *res);

/**
 * struct fsal_layoutreturn_arg: Input parameters to
 * FSAL_layoutreturn
 */

struct fsal_layoutreturn_arg
{
  layouttype4 type; /**< The type of layout being returned */
  offset4 offset; /**< The offset specified in the return operation */
  length4 length; /**< The length specified in the return operation*/
  fsal_boolean_t synthetic; /**< Whether this return was synthesized
                                 a result of return_on_close or lease
                                 expiration. */
};

/**
 * struct fsal_layoutreturn_res: Output and in/out parameters to
 * FSAL_layoutreturn
 */

struct fsal_layoutreturn_res
{
  fsal_segment_t segment; /**< The current segment in the return iteration,
                               may be modified (changing offset or length)
                               to reflect a partial return. */
  fsal_boolean_t disposed; /**< Set to true if the segment has been
                                completely disposed. */
};

/**
 * FSAL_layoutreturn: Potentially return one layout segment
 *
 * This function is called once on each segment matching the IO mode
 * and intersecting the range specified in a LAYOUTRETURN operation or
 * for all layouts corresponding to a given stateid on last close,
 * leas expiry, a layoutreturn with a return-type of FSID or ALL.
 * Whther it is called in the former or latter case is indicated by
 * the 'synthetic' flag in the arg structure, with synthetic being
 * TRUE in the case of last-close, lease expiry, or bulk return.
 *
 * If 'synthetic' is false, the FSAL is free to return all, part, or
 * none of any segment, recording this by modifying the contents of
 * the segment (res->seg).  If it disposes of all of a segment, it
 * must set res->disposed to true.  If synthetic is set, all layouts
 * on the file are being returned and each segment must be freed and
 * disposed of by the FSAL.
 *
 * In the case of a non-synthetic return, the offset and length of the
 * returned area are given in arg->offset and arg->length.
 *
 * @param handle   [IN]     The handle corresponding to the segment
 *                          being returned
 * @param context  [IN]     The FSAL operation context
 * @param lrf_body [IN]     In the case of a non-synthetic return, this is
 *                          an XDR stream corresponding to the layout
 *                          type-specific argument to LAYOUTRETURN.   In
 *                          the case of a synthetic return, this is a NULL
 *                          pointer.
 * @param args     [IN]     Input arguments of the function
 * @param res      [IN,OUT] In/out and out arguments of the function
 *
 * @return An NFSv4.1 status code.  NFS4_OK on success.
 */

nfsstat4 FSAL_layoutreturn(fsal_handle_t* handle,
                           fsal_op_context_t* context,
                           XDR *lrf_body,
                           const struct fsal_layoutreturn_arg *arg,
                           struct fsal_layoutreturn_res *res);

/**
 * struct fsal_layoutcommit_arg: Input parameters to
 * FSAL_layoutcommit
 */

struct fsal_layoutcommit_arg
{
  layouttype4 type; /**< The type of the layout being committed */
  fsal_segment_t segment; /**< The segment being committed on this call */
  fsal_boolean_t reclaim; /**< True if this is a reclaim commit */
  fsal_boolean_t new_offset; /**< True if the client has suggested a
                                  new offset */
  offset4 last_write; /**< The offset of the last byte written, if
                           new_offset if set, otherwise undefined. */
  fsal_boolean_t time_changed; /**< True if the client provided a new value
                                    for mtime */
  fsal_time_t new_time; /**< If new_time is true, the client-supplied
                             modification tiem for the file.  otherwise,
                             undefined. */
};

/**
 * struct fsal_layoutcommit_res: In/out and output parameters to
 * FSAL_layoutcommit
 */

struct fsal_layoutcommit_res
{
  fsal_multicommit_mark_t bookmark; /**< State preserved between calls
                                         to FSAL_layoutcommit. */
  fsal_boolean_t size_supplied; /**< True if the FSAL is returning a
                                     new filesize */
  length4 new_size; /**< The new file size returned by the FSAL */
  fsal_boolean_t commit_done; /**< The FSAL has completed the
                                 LAYOUTCOMMIT operation and
                                 FSAL_layoutcommit need not be
                                 called again, even if more segments
                                 are left in the layout. */
};

/**
 * FSAL_layoutcommit: Commit a segment of a layout
 *
 * This function is called once on every segment of a layout.  The
 * FSAL may avoid being called again after it has finished all tasks
 * necessary for the commit by setting res->commit_done to TRUE.
 *
 * The calling function does not inspect or act on the value of
 * size_supplied or new_size until after the last call to
 * FSAL_layoutcommit.
 *
 * handle   [IN]     Handle for the file being committed
 * context  [IN]     The FSAL operation context
 * out_body [IN]     An XDR stream containing the layout type-specific
 *                   portion of the LAYOUTCOMMIT arguments.
 * arg      [IN]     Input arguments to the function
 * res      [IN,OUT] In/out and output arguments to the function
 *
 * @return An NFSv4.1 status code.  NFS4_OK on success.
 */

nfsstat4 FSAL_layoutcommit(fsal_handle_t *handle,
                           fsal_op_context_t *context,
                           XDR *lou_body,
                           const struct fsal_layoutcommit_arg *arg,
                           struct fsal_layoutcommit_res *res);

/**
 * FSAL_getdeviceinfo: Get information about a pNFS device
 *
 * This function is called once on every segment of a layout.  The
 * FSAL may avoid being called again after it has finished all tasks
 * necessary for the commit by setting res->commit_done to TRUE.
 *
 * context      [IN]  The FSAL operation context
 * da_addr_body [OUT] An XDR stream to which the FSAL is to write the
 *                    layout type-specific information about the
 *                    device.
 * type        [IN]  The type of layout that specified the device
 * deviceid     [IN]  The deviceid
 *
 * @return An NFSv4.1 status code.  NFS4_OK on success.
 */

nfsstat4 FSAL_getdeviceinfo(fsal_op_context_t *context,
                            XDR *da_addr_body,
                            layouttype4 type,
                            const fsal_deviceid_t *deviceid);

/**
 * struct fsal_getdevicelist_arg: Input parameters to
 * FSAL_getdevicelist
 */

struct fsal_getdevicelist_arg
{
  unsigned short export_id; /**< The ID of the export on which the
                                 device list is requested */
  layouttype4 type; /**< The type of layout for which a device list is
                         being requested */
};

/**
 * struct fsal_getdevicelist_res: In/out and output parameters to
 * FSAL_getdevicelist
 */

struct fsal_getdevicelist_res
{
  nfs_cookie4 cookie; /**< Input, cookie indicating position in device
                           list from which to begin.
                           Output, cookie that may be supplied to get
                           the entry after the alst one returned.
                           Undefined if EOF is set. */
  verifier4 cookieverf; /**< For any non-zero cookie, this must be the
                             verifier returned from a previous call to
                             getdevicelist.  The FSAL may use this
                             value to verify that the cookie is not out
                             of date. A cookie verifier may be supplied
                             by the FSAL on output. */
  fsal_boolean_t eof; /**< True if the last deviceid has been returned. */
  unsigned int count; /**< Input, the number of devices requested (and
                           the number of devices there is space for).
                           Output, the number of devices supplied by
                           the FSAL. */
  uint64_t *devids; /**< An array of the low quads of deviceids.  The
                         high quad will be supplied by
                         nfs41_op_getdevicelist, derived from the
                         export. */
};

/**
 * FSAL_getdevicelist: Get list of available devices
 *
 * This function should populate res->devids with from 0 to res->count
 * values representing the low quad of deviceids of which it wishes to
 * make the caller available, then set res->count to the number it
 * returned.  if it wishes to return more than the caller has provided
 * space for, it should set res->eof to false and place a suitable
 * value in res->cookie.
 *
 * If it wishes to return no deviceids, it may set res->count to 0 and
 * res->eof to TRUE on any call.
 *
 * handle  [IN]     Filehandle ont he filesystem for which
 *                  deviceids are requested.
 * context [IN]     FSAL operation context
 * arg     [IN]     Input arguments of the function
 * res     [IN/OUT] In/out and output arguments of the function
 *
 * @return An NFSv4.1 status code.  NFS4_OK on success.
 */

nfsstat4 FSAL_getdevicelist(fsal_handle_t *handle,
                            fsal_op_context_t *context,
                            const struct fsal_getdevicelist_arg *arg,
                            struct fsal_getdevicelist_res *res);
#endif /* _USE_FSALMDS */

/******************************************************
 *               FSAL DS related functions.
 ******************************************************/

#ifdef _USE_FSALDS

/**
 *
 * FSAL_DS_read: Read from a data-server filehandle.
 *
 * @param handle           [IN]  FSAL file handle
 * @param context          [IN]  Operation context
 * @param offset           [IN]  Offset at which to read
 * @param requested_length [IN]  Length of read requested (and size of
 *                               buffer)
 * @param buffer           [OUT] Buffer to which read data is stored
 * @param supplied_elngth  [OUT] Amount of data actually read
 * @param end_of_file      [OUT] End of file was reached
 */

nfsstat4 FSAL_DS_read(fsal_handle_t *handle,
                      fsal_op_context_t *context,
                      offset4 offset,
                      count4 requested_length,
                      caddr_t buffer,
                      count4 *supplied_length,
                      fsal_boolean_t *end_of_file);

/**
 *
 * FSAL_DS_write: Write to a data-server filehandle.
 *
 * @param handle           [IN]  FSAL file handle
 * @param context          [IN]  Operation context
 * @param offset           [IN]  Offset at which to read
 * @param write_length     [IN]  Length of write data
 * @param buffer           [OUT] Buffer from which written data is fetched
 * @param stability_wanted [IN]  Stability of write requested
 * @param written_length   [OUT] Amount of data actually written
 * @param writeverf        [OUT] Write verifier
 * @param stability_got    [OUT] Stability of write performed
 */


nfsstat4 FSAL_DS_write(fsal_handle_t *handle,
                       fsal_op_context_t *context,
                       offset4 offset,
                       count4 write_length,
                       caddr_t buffer,
                       stable_how4 stability_wanted,
                       count4 *written_length,
                       verifier4 writeverf,
                       stable_how4 *stability_got);

/**
 *
 * FSAL_DS_commit: Commit a byte range
 *
 * @param handle         [IN]     FSAL file handle
 * @param context        [IN]     Operation context
 * @param offset         [IN]     Start of commit window
 * @param count          [IN]     Number of bytes to commit
 * @param writeverf      [OUT]    Write verifier
 */

nfsstat4 FSAL_DS_commit(fsal_handle_t *handle,
                        fsal_op_context_t *context,
                        offset4 offset,
                        count4 count,
                        verifier4 writeverf);

#endif /* _USE_FSALDS */

#ifdef _USE_FSALMDS
typedef struct fsal_mdsfunctions__
{
  nfsstat4 (*fsal_layoutget)(fsal_handle_t *handle,
                             fsal_op_context_t *context,
                             XDR *loc_body,
                             const struct fsal_layoutget_arg *arg,
                             struct fsal_layoutget_res *res);

  nfsstat4 (*fsal_layoutreturn)(fsal_handle_t* handle,
                                fsal_op_context_t* context,
                                XDR *lrf_body,
                                const struct fsal_layoutreturn_arg *arg,
                                struct fsal_layoutreturn_res *res);
  nfsstat4 (*fsal_layoutcommit)(fsal_handle_t *handle,
                                fsal_op_context_t *context,
                                XDR *lou_body,
                                const struct fsal_layoutcommit_arg *arg,
                                struct fsal_layoutcommit_res *res);
  nfsstat4 (*fsal_getdeviceinfo)(fsal_op_context_t *context,
                                 XDR* da_addr_body,
                                 layouttype4 type,
                                 const fsal_deviceid_t *deviceid);
  nfsstat4 (*fsal_getdevicelist)(fsal_handle_t *handle,
                                 fsal_op_context_t *context,
                                 const struct fsal_getdevicelist_arg *arg,
                                 struct fsal_getdevicelist_res *res);
} fsal_mdsfunctions_t;
#endif /* _USE_FSALMDS */

#ifdef _USE_FSALDS
typedef struct fsal_dsfunctions__
{
  nfsstat4 (*fsal_DS_read)(fsal_handle_t *handle,
                           fsal_op_context_t *context,
                           offset4 offset,
                           count4 requested_length,
                           caddr_t buffer,
                           count4 *supplied_length,
                           fsal_boolean_t *end_of_file);

     nfsstat4 (*fsal_DS_write)(fsal_handle_t *handle,
                               fsal_op_context_t *context,
                               offset4 offset,
                               count4 write_length,
                               caddr_t buffer,
                               stable_how4 stability_wanted,
                               count4 *written_length,
                               verifier4 writeverf,
                               stable_how4 *stability_got);

  nfsstat4 (*fsal_DS_commit)(fsal_handle_t *handle,
                             fsal_op_context_t *context,
                             offset4 offset,
                             count4 count,
                             verifier4 writever4);
} fsal_dsfunctions_t;
#endif /* _USE_FSALDS */

#ifdef _USE_FSALMDS
fsal_mdsfunctions_t FSAL_GetMDSFunctions(void);
void FSAL_LoadMDSFunctions(void);
#endif

#ifdef _USE_FSALDS
fsal_dsfunctions_t FSAL_GetDSFunctions();
void FSAL_LoadDSFunctions(void);
#endif

#endif /* _FSAL_PNFS_H */
