/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_rcp.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:03 $
 * \version $Revision: 1.4 $
 * \brief   Transfer operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

fsal_status_t FSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt      /* IN */
    )
{

  /* for logging */
  SetFuncID(INDEX_FSAL_rcp);

  /* sanity checks. */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rcp);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rcp);

}
