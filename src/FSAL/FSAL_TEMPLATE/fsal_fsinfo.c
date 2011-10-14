/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:20:22 $
 * \version $Revision: 1.12 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * filehandle,   /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * dynamicinfo    /* OUT */
    )
{

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  TakeTokenFSCall();

  /* >> retrieve filesystem usage statistiques << */

  ReleaseTokenFSCall();

  /* >> interpret returned status << */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
