/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_glue.h"
#include "fsal_internal.h"
#include "FSAL/common_methods.h"
#ifdef _PNFS
#include "fsal_pnfs.h"
#endif /* _PNFS */

#ifdef _PNFS_MDS
fsal_mdsfunctions_t fsal_ceph_mdsfunctions = {
     .layoutget = CEPHFSAL_layoutget,
     .layoutreturn = CEPHFSAL_layoutreturn,
     .layoutcommit = CEPHFSAL_layoutcommit,
     .getdeviceinfo = CEPHFSAL_getdeviceinfo,
     .getdevicelist = CEPHFSAL_getdevicelist
};

fsal_mdsfunctions_t FSAL_GetMDSFunctions(void)
{
     return fsal_ceph_mdsfunctions;
}
#endif /* _PNFS_MDS */

#ifdef _PNFS_DS
fsal_dsfunctions_t fsal_ceph_dsfunctions = {
     .DS_read = CEPHFSAL_DS_read,
     .DS_write = CEPHFSAL_DS_write,
     .DS_commit = CEPHFSAL_DS_commit
};
#endif /* _PNFS_DS */

#ifdef _PNFS_DS
fsal_dsfunctions_t FSAL_GetDSFunctions(void)
{
     return fsal_ceph_dsfunctions;
}
#endif /* _PNFS_DS */
