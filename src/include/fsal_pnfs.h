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
 * \file    fsal_pnfs.h
 * \author  $Author: Philippe Deniel $
 * \date    $Date: 2011/04/05 14:00:005 $
 * \brief   Management of the pNFS features at the FSAL level 
 *
 * fsal_pnfs.h : Management of the pNFS features  at the FSAL level.
 *
 *
 */

#ifndef _FSAL_PNFS_H
#define _FSAL_PNFS_H

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */


#include "fsal_pnfs_types.h"

/** FSAL_pnfs_layout_get: Retrieve and encode a layout for pnfs_file_t, onto the xdr
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
//enum nfsstat4 __FSAL_pnfs_layout_get (pnfs_file_t *file, xdr_stream_t *xdr,
//			       const struct pnfs_layoutget_arg *arg,
//				struct pnfs_layoutget_res *res);

fsal_status_t FSAL_pnfs_layout_get( fsal_pnfs_file_t  * pfsal_pnfs_file, /* IN */ 
				    fsal_iomode_t       iomode,       /* IN */
				    fsal_off_t          offset,       /* IN */
				    fsal_size_t         length,       /* IN */
				    fsal_size_t         minlength,    /* IN */
				    fsal_layout_t     * pfsal_layout, /* INOUT */
				    fsal_op_context_t * pcontext ) ;  /* IN */

/** FSAL_pnfs_layout_commit: Commit meta-data changes to file
 *	@xdr: In blocks and objects contains the type-specific commit info.
 *	@arg: The passed in parameters (See struct pnfs_layoutcommit_arg)
 *	@res: The returned information (See struct pnfs_layoutcommit_res)
 *
 * Return: one of the appropriate nfs errors.
 * Comments: In some files-layout systems where the DSs are set to return
 *           S_FILE_SYNC for the WRITE operation, or when the COMMIT is through
 *           the MDS, this function may be empty.
 */
fsal_status_t  FSAL_pnfs_layout_commit( fsal_pnfs_file_t          * pfsal_pnfs_file,       /* INOUT */
					fsal_layout_t             * pfsal_layout,          /* INOUT */
				        fsal_time_t                 suggested_time_modify, /* IN */
					fsal_layout_update_data_t * pfsal_lyout_update,    /* IN */
					fsal_size_t               * pnewsize,              /* OUT */
					fsal_op_context_t         * pcontext ) ;           /* IN */

/** FSAL_pnfs_layout_return: Client Returns the layout
 *
 *	Or a return is simulated by NFS-GANESHA.
 *
 *	@xdr: In blocks and objects contains the type-specific return info.
 *	@arg: The passed in parameters (See struct pnfs_layoutreturn_arg)
 */
fsal_status_t FSAL_pnfs_layout_return ( fsal_pnfs_file_t          * pfsal_pnfs_file,       /* IN */
                                        fsal_layout_t             * parray_layout,         /* IN */
	 			        unsigned int                len_parray_layout,     /* IN */
				        fsal_layout_return_data_t * parray_layout_return,  /* OUT */
				        unsigned int              * plen_layout_return,    /* OUT */
				        fsal_op_context_t         * pcontext ) ;           /* IN */



#if 0
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


#endif

#endif /* _FSAL_PNFS_H */


