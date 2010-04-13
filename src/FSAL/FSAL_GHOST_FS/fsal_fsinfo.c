/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:01 $
 * \version $Revision: 1.4 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

fsal_status_t FSAL_static_fsinfo(fsal_handle_t * filehandle,    /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_staticfsinfo_t * staticinfo       /* OUT */
    )
{

  /* For logging */
  SetFuncID(INDEX_FSAL_static_fsinfo);

  /* sanity checks. */
  if(!filehandle || !staticinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_static_fsinfo);

  /* returning static info about the filesystem */
  (*staticinfo) = global_fs_info;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_static_fsinfo);

}

fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * filehandle,   /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * dynamicinfo    /* OUT */
    )
{

  /* For logging */
  SetFuncID(INDEX_FSAL_dynamic_fsinfo);

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  dynamicinfo->total_bytes = 0;
  dynamicinfo->free_bytes = 0;
  dynamicinfo->avail_bytes = 0;
  dynamicinfo->total_files = 0;
  dynamicinfo->free_files = 0;
  dynamicinfo->avail_files = 0;
  dynamicinfo->time_delta.seconds = 1;
  dynamicinfo->time_delta.nseconds = 0;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
