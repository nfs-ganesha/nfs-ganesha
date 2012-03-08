/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Author: Adam C. Emerson
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
 * \file    pnfs_common.h
 * \brief   Common utility functions for pNFS
 *
 * fsal_pnfs.h: pNFS utility functions
 *
 *
 */

#ifndef _FSAL_PNFS_COMMON_H
#define _FSAL_PNFS_COMMON_H

#include <stdint.h>
#include "nfs4.h"

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _USE_NFS4_1

#ifndef false
#define false 0
#endif

#ifndef true
#define true 1
#endif

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
     offset4 offset;
     /** The length of the segment */
     length4 length;
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
 *               Utility functions for ranges
 ******************************************************/

/**
 * @brief Test for overlap and compatible io_mode of segments
 *
 * @param segment1 [IN] A layout segment
 * @param segmenta [IN] A layout segment
 *
 * @return True if there is one or more byte contained in both both
 *         segments and the io_modes are compatible.
 */
static inline bool_t pnfs_segments_overlap(struct pnfs_segment segment1,
                                           struct pnfs_segment segmenta)
{
     if (!(segment1.io_mode & segmenta.io_mode)) {
          return FALSE;
     } else if ((segment1.length == 0) || (segmenta.length == 0)) {
          return FALSE;
     } else if (segment1.offset < segmenta.offset) {
          if (segment1.length == NFS4_UINT64_MAX) {
               return TRUE;
          } else if (segment1.offset + segment1.length < segmenta.offset) {
               return FALSE;
          } else {
               return TRUE;
          }
     } else if (segmenta.offset < segment1.offset) {
          if (segmenta.length == NFS4_UINT64_MAX) {
               return TRUE;
          } else if ((segmenta.offset + segmenta.length)
                     < segment1.offset) {
               return FALSE;
          } else {
               return TRUE;
          }
     } else {
          return TRUE;
     }
}

/**
 * @brief Check if one segment contains the other
 *
 * This function checks whether segment2 is subsegment (not
 * necessarily proper) of segment1.
 *
 * @param segment1 [IN] The putative supersegment
 * @param segment2 [IN] The putative subsugment
 *
 * @return True if segment2 is completely contained within segment1
 */
static inline bool_t pnfs_segment_contains(struct pnfs_segment segment1,
                                           struct pnfs_segment segment2)
{
     if (!(segment1.io_mode & segment2.io_mode)) {
          return FALSE;
     } else if (segment1.length == 0) {
          return FALSE;
     } else if (segment1.offset <= segment2.offset) {
          if (segment1.length == NFS4_UINT64_MAX) {
               return TRUE;
          } else if (segment2.length == NFS4_UINT64_MAX) {
               return FALSE;
          } else if ((segment2.offset + segment2.length) <=
                     (segment1.offset + segment1.length)) {
               return TRUE;
          } else {
               return FALSE;
          }
     } else {
          return FALSE;
     }
}

/**
 * \brief Subtract the second segment from the first
 *
 * In the case that the subtrahend completely contains the minuend,
 * the return value has a length and offset of 0.  If the IO modes of
 * the two arguments are incompatible, the minuend is returned
 * unchanged.  If the subtrahend is a proper subset of the minuend,
 * the minuend is returned unchanged.  This is incorrect, but to
 * handle splitting a segment, we need to add split and merge support
 * to FSALs.
 *
 * @param minuend [IN] The putative supersegment
 * @param subtrahend [IN] The putative subsugment
 *
 * @return A layout segment that is the difference between the two
 *         segments.
 */

static inline struct pnfs_segment
pnfs_segment_difference(struct pnfs_segment minuend,
                        struct pnfs_segment subtrahend)
{
     if (!(minuend.io_mode & subtrahend.io_mode)) {
          return minuend;
     } else if (pnfs_segment_contains(subtrahend, minuend)) {
          struct pnfs_segment null = {
               .io_mode = minuend.io_mode,
               .offset = 0,
               .length = 0
          };
          return null;
     } else if (!(pnfs_segments_overlap(minuend, subtrahend))) {
          return minuend;
     } else if (minuend.offset <= subtrahend.offset) {
          if (minuend.length == NFS4_UINT64_MAX) {
               if (subtrahend.length == NFS4_UINT64_MAX) {
                    struct pnfs_segment difference = {
                         .io_mode = minuend.io_mode,
                         .offset = minuend.offset,
                         .length = subtrahend.offset - minuend.offset
                    };
                    return difference;
               } else {
                    return minuend;
               }
          } else {
               if ((minuend.length + minuend.offset) >
                   (subtrahend.length + subtrahend.offset)) {
                    return minuend;
               } else {
                    struct pnfs_segment difference = {
                         .io_mode = minuend.io_mode,
                         .offset = minuend.offset,
                         .length = minuend.offset - subtrahend.offset
                    };
                    return difference;
               }
          }
     } else {
          struct pnfs_segment difference = {
               .io_mode = minuend.io_mode,
               .offset = subtrahend.offset + subtrahend.length - 1,
               .length = minuend.length
          };
          return difference;
     }
}

/******************************************************
 *    Common functions for every pNFS implementation
 ******************************************************/

/******************************************************
 *            Convenience XDR functions
 ******************************************************/

fsal_boolean_t xdr_fsal_deviceid(XDR *xdrs, struct pnfs_deviceid *deviceid);

nfsstat4 FSAL_encode_ipv4_netaddr(XDR *xdrs,
                                  uint16_t proto,
                                  uint32_t addr,
                                  uint16_t port);

nfsstat4 posix2nfs4_error(int posix_errorcode);
#endif /* _USE_NFS4_1 */

#endif /* _FSAL_PNFS_COMMON_H */
