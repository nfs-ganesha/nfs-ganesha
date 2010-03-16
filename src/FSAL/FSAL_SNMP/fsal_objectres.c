/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_objectres.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   FSAL remanent resources management routines.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal.h"
#include "fsal_internal.h"

/**
 * FSAL_CleanObjectResources:
 * This function cleans remanent internal resources
 * that are kept for a given FSAL handle.
 *
 * \param in_fsal_handle (input):
 *        The handle whose the resources are to be cleaned.
 */

fsal_status_t FSAL_CleanObjectResources(fsal_handle_t * in_fsal_handle)
{

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanObjectResources);

}
