/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box Corporation
 * All Rights Reserved
 *
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
 * \file    fsal_ds.c
 * \brief   DS realisation for the filesystem abstraction
 *
 * filelayout.c: DS realisation for the filesystem abstraction
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "nfsv41.h"
#include <fcntl.h>
#include "HashTable.h"
#include <pthread.h>
#include "stuff_alloc.h"
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "pnfs_common.h"
#include "fsal_pnfs_files.h"

#define min(a,b)          \
     ({ typeof (a) _a = (a);                    \
          typeof (b) _b = (b);                  \
          _a < _b ? _a : _b; })

/**
 *
 * LUSTREFSAL_DS_read : the DS reads data to the FSAL.
 *
 * This function is used by the pNFS Data Server to write data to the FSAL.
 *
 * @param [IN] pfsalhandle : handle for the object to be written
 * @param [IN] pfsalcontent : FSAL's operation context
 * @param [IN] stateid : pointer to the stateid to be used (Why is this needed ?)
 * @param [IN] offset : offset for this IO
 * @param [IN] requested_length : length to be read
 * @param [OUT] buffer : place to put read data in
 * @param [OUT] pread_length: length actually read
 * @param [IN] verifier: operation's verifier (Why is this needed ?)
 * @param [OUT] end_of_file : set to TRUE if eof is reached.
 *
 * @return a NFSv4 status
 *
 */
nfsstat4 LUSTREFSAL_DS_read( fsal_handle_t     * pfsalhandle,
                             fsal_op_context_t * pfsalcontext,
                             const stateid4    * stateid,
                             offset4             offset,
                             count4              requested_length,
                             caddr_t             buffer,
                             count4            * pread_length,
                             fsal_boolean_t    * end_of_file)
{
#if 0
     /* Our format for the file handle */
     lustrefsal_handle_t * phandle = (lustrefsal_handle_t*)pfsalhandle;

     /* Our format for the operational context */
     lustrefsal_op_context_t* pcontext = (lustrefsal_op_context_t*)pfsalcontext;

     int uid = FSAL_OP_CONTEXT_TO_UID(context);
     int gid = FSAL_OP_CONTEXT_TO_GID(context);
#endif
     return NFS4_OK;
}

/**
 *
 * LUSTREFSAL_DS_write : the DS writes data to the FSAL.
 *
 * This function is used by the pNFS Data Server to write data to the FSAL.
 *
 * @param [IN] pfsalhandle : handle for the object to be written
 * @param [IN] pfsalcontent : FSAL's operation context
 * @param [IN] stateid : pointer to the stateid to be used (Why is this needed ?)
 * @param [IN] offset : offset for this IO
 * @param [IN] write_length : length to be written
 * @param [IN] buffer : data to be written
 * @param [IN] stability_wanted: "stable how" flag for this IO
 * @param [OUT] pwritten_length: length actually written
 * @param [IN] verifier: operation's verifier (Why is this needed ?)
 * @param [OUT] stability_got: the "stable_how" that was used for this IO
 *
 * @return a NFSv4 status
 *
 */
nfsstat4 LUSTREFSAL_DS_write( fsal_handle_t     * pfsalhandle,
                              fsal_op_context_t * pfsalcontext,
                              const stateid4    * stateid,
                              offset4             offset,
                              count4              write_length,
                              caddr_t             buffer,
                              stable_how4         stability_wanted,
                              count4            * pwritten_length,
                              verifier4           writeverf,
                              stable_how4       * stability_got )
{
#if 0
     /* Our format for the file handle */
     lustrefsal_handle_t * phandle = (lustrefsal_handle_t*)pfsalhandle;

     /* Our format for the operational context */
     lustrefsal_op_context_t* pcontext = (lustrefsal_op_context_t*)pfsalcontext;

     int uid = FSAL_OP_CONTEXT_TO_UID(context);
     int gid = FSAL_OP_CONTEXT_TO_GID(context);
#endif
     return NFS4_OK;
} /* LUSTREFSAL_DS_write */

/**
 *
 * LUSTREFSAL_DS_commit : commits an DS's unstable write.
 *
 * This function commits an unstable write by the pNFS Data Server.
 *
 * @param [IN] pfsalhandle : handle for the object to be written
 * @param [IN] pfsalcontent : FSAL's operation context
 * @param [IN] offset : offset for this IO
 * @param [IN] count : length to be commited
 * @param [IN] verifier: operation's verifier (Why is this needed ?)
 *
 * @return a NFSv4 status
 *
 */
nfsstat4 LUSTREFSAL_DS_commit( fsal_handle_t     * pfsalhandle,
                               fsal_op_context_t * pfsalcontext,
                               offset4             offset,
                               count4              count,
                               verifier4           writeverf)
{
#if 0
     /* Our format for the file handle */
     lustrefsal_handle_t * phandle = (lustrefsal_handle_t*)pfsalhandle;

     /* Our format for the operational context */
     lustrefsal_op_context_t* pcontext = (lustrefsal_op_context_t*)pfsalcontext;

     int uid = FSAL_OP_CONTEXT_TO_UID(context);
     int gid = FSAL_OP_CONTEXT_TO_GID(context);
#endif
     return NFS4_OK;
} /* LUSTREFSAL_DS_commit */
