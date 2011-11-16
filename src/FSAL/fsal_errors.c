/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_errors.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.3 $
 * \brief   Routines for handling errors.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"

/**
 * fsal_is_retryable:
 * Indicates if an FSAL error is retryable,
 * i.e. if the caller has a chance of succeeding
 * if it tries to call again the function that returned
 * such an error code.
 *
 * \param status(input): The fsal status whom retryability is to be tested.
 *
 * \return - TRUE if the error is retryable.
 *         - FALSE if the error is NOT retryable.
 */
fsal_boolean_t fsal_is_retryable(fsal_status_t status)
{

  switch (status.major)
    {

    /** @todo : ERR_FSAL_DELAY : The only retryable error ? */
    case ERR_FSAL_DELAY:
      return TRUE;

    default:
      return FALSE;
    }

}
