/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_errors.c
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.3 $
 * \brief   Routines for handling errors.
 *
 */

#include "config.h"

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
 * \return - true if the error is retryable.
 *         - false if the error is NOT retryable.
 */
bool fsal_is_retryable(fsal_status_t status)
{

  switch (status.major)
    {

    /** @todo : ERR_FSAL_DELAY : The only retryable error ? */
    case ERR_FSAL_DELAY:
      return true;

    default:
      return false;
    }

}
