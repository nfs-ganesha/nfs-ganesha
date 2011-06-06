/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_pnfs_functions.c
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_pnfs.h"

fsal_status_t FSAL_pnfs_layout_get( fsal_pnfs_file_t  * pfsal_pnfs_file, /* IN */ 
				    fsal_iomode_t       iomode,       /* IN */
				    fsal_off_t          offset,       /* IN */
				    fsal_size_t         length,       /* IN */
				    fsal_size_t         minlength,    /* IN */
				    fsal_layout_t     * pfsal_layout, /* INOUT */
				    fsal_op_context_t * pcontext )    /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* FSAL_pnfs_layout_get */

fsal_status_t  FSAL_pnfs_layout_commit( fsal_pnfs_file_t          * pfsal_pnfs_file,       /* INOUT */
					fsal_layout_t             * pfsal_layout,          /* INOUT */
				        fsal_time_t                 suggested_time_modify, /* IN */
					fsal_layout_update_data_t * pfsal_lyout_update,    /* IN */
					fsal_size_t               * pnewsize,              /* OUT */
					fsal_op_context_t         * pcontext )             /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* FSAL_pnfs_layout_commit */

fsal_status_t FSAL_pnfs_layout_return( fsal_pnfs_file_t          * pfsal_pnfs_file,       /* IN */
                                       fsal_layout_t             * parray_layout,         /* IN */
	 			       unsigned int                len_parray_layout,     /* IN */
				       fsal_layout_return_data_t * parray_layout_return,  /* OUT */
				       unsigned int              * plen_layout_return,    /* OUT */
				       fsal_op_context_t         * pcontext )             /* IN */
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* FSAL_pnfs_layout_return */

