/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_truncate.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:37:29 $
 * \version $Revision: 1.3 $
 * \brief   Truncate function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

fsal_status_t FSAL_truncate(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_size_t length, /* IN */
                            fsal_file_t * file_descriptor,      /* Unused in this FSAL */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{

  /* for logging */
  SetFuncID(INDEX_FSAL_truncate);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* GHOSTFS is read only */
  Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_truncate);

}
