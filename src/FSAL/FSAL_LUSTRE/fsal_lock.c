/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_lock.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.2 $
 * \brief   Locking operations.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

/**
 * FSAL_lock:
 * Not implemented.
 */
fsal_status_t LUSTREFSAL_lock(lustrefsal_file_t * obj_handle,   /* IN */
                              lustrefsal_lockdesc_t * ldesc,    /*IN/OUT */
                              fsal_boolean_t callback   /* IN */
    )
{

  /* sanity checks. */
  if(!obj_handle || !ldesc)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock);
}

/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t LUSTREFSAL_changelock(lustrefsal_lockdesc_t * lock_descriptor,    /* IN / OUT */
                                    fsal_lockparam_t * lock_info        /* IN */
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
fsal_status_t LUSTREFSAL_unlock(lustrefsal_file_t * obj_handle, /* IN */
                                lustrefsal_lockdesc_t * ldesc   /*IN/OUT */
    )
{

  /* sanity checks. */
  if(!ldesc)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);

}

fsal_status_t LUSTREFSAL_getlock(lustrefsal_file_t * obj_handle,
                                 lustrefsal_lockdesc_t * ldesc)
{

  /* sanity checks. */
  if(!ldesc)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);
}
