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
 *
 * \param objecthandle (input):
 *        Handle of the object to be locked.
 * \param p_context    (input):
 *        Authentication context for the operation (user, export...).
 * \param typelock    (input):
 *        Type of lock to be taken
 * \param lock_descriptor (output):
 *        The returned lock descriptor
 */
fsal_status_t HPSSFSAL_lock(hpssfsal_file_t * obj_handle,       /* IN */
                            hpssfsal_lockdesc_t * ldesc, fsal_boolean_t callback)       /*IN/OUT */
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
fsal_status_t HPSSFSAL_changelock(hpssfsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                                  fsal_lockparam_t * lock_info  /* IN */
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
fsal_status_t HPSSFSAL_unlock(hpssfsal_file_t * obj_handle,     /* IN */
                              hpssfsal_lockdesc_t * ldesc       /*IN/OUT */
    )
{

  /* sanity checks. */
  if(!obj_handle || !ldesc)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);

}

/**
 * FSAL_unlock:
 * Not implemented.
 */
fsal_status_t HPSSFSAL_getlock(hpssfsal_file_t * obj_handle, hpssfsal_lockdesc_t * ldesc)
{

  /* sanity checks. */
  if(!obj_handle || !ldesc)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);
}
