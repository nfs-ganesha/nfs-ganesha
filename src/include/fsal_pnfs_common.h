/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Contributor: Adam C. Emerson
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
 * \file    fsal_pnfs_common.h
 * \brief   Common utility functions for pNFS
 *
 * fsal_pnfs.h: pNFS utility functions
 *
 *
 */

#ifndef _FSAL_PNFS_COMMON_H
#define _FSAL_PNFS_COMMON_H

#include "nfs4.h"
#include "fsal_pnfs.h"

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _USE_FSALMDS

/******************************************************
 *               Utility functions for ranges
 ******************************************************/

/**
 * FSAL_range_overlaps: Return true if ranges overlap
 *
 * @param offset1 [IN] Offset of the first range
 * @param length1 [IN] Length of the first range
 * @param offset2 [IN] Offset of the second range
 * @param length2 [IN] Length of the second range
 *
 * @return True if there is one or more byte contained in both
 * ranges.
 */

static inline fsal_boolean_t FSAL_range_overlaps(offset4 offset1,
                                                 length4 length1,
                                                 offset4 offset2,
                                                 length4 length2)
{
  if ((length1 == 0) || (length2 == 0))
    {
      return FALSE;
    }
  else if (offset1 < offset2)
    {
      if (length1 == NFS4_UINT64_MAX)
        return TRUE;
      else if (offset1 + length1 < offset2)
        return FALSE;
      else
        return TRUE;
    }
  else if (offset2 < offset1)
    {
      if (length2 == NFS4_UINT64_MAX)
        return TRUE;
      else if (offset2 + length2 < offset1)
        return FALSE;
      else
        return TRUE;
    }
  else
    return TRUE;
}

/**
 * FSAL_range_overlaps: Check if one range contains the other
 *
 * @param offset1 [IN] Offset of the first range
 * @param length1 [IN] Length of the first range
 * @param offset2 [IN] Offset of the second range
 * @param length2 [IN] Length of the second range
 *
 * @return True if the first range completely contains the second.
 */

static inline fsal_boolean_t FSAL_range_contains(offset4 offset1,
                                                 length4 length1,
                                                 offset4 offset2,
                                                 length4 length2)
{
  if (length1 == 0)
    return FALSE;

  if (offset1 < offset2)
    {
      if (length1 == NFS4_UINT64_MAX)
        return TRUE;
      else if ((offset2 + length2) <= (offset1 + length1))
        return TRUE;
      else
        return FALSE;
    }
  else
    return FALSE;
}

/******************************************************
 *            Convenience XDR functions
 ******************************************************/

bool_t xdr_fsal_deviceid(XDR *xdrs, fsal_deviceid_t *deviceid);

nfsstat4 FSAL_encode_ipv4_netaddr(XDR *xdrs,
                                  uint16_t proto,
                                  uint32_t addr,
                                  uint16_t port);

#endif /* _USE_FSALMDS */

#if defined(_USE_FSALMDS) || defined(_USE_FSALDS)
nfsstat4 posix2nfs4_error(int posix_errorcode);
#endif /* _USE_FSALMDS || _USE_FSALDS */

#endif /* _FSAL_PNFS_COMMON_H */
