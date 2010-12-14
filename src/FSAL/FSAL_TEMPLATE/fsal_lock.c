/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_lock.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.2 $
 * \brief   Locking operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

/**
 * FSAL_lock:
 * Lock an entry in the filesystem.
 */
fsal_status_t FSAL_lock(fsal_file_t * obj_handle,
                        fsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock);
}

/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t FSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                              fsal_lockparam_t * lock_info      /* IN */
    )
{

  /* sanity checks. */
  if(!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_changelock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_changelock);

}

/**
 * FSAL_unlock:
 * Not implemented.
 */
fsal_status_t FSAL_unlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);
}
