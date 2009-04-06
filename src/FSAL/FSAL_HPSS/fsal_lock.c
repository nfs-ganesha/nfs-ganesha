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
fsal_status_t  FSAL_lock(
    fsal_handle_t           * objecthandle,         /* IN */
    fsal_op_context_t       * p_context,            /* IN */
    fsal_lockparam_t        * lock_info,            /* IN */
    fsal_lockdesc_t         * lock_descriptor       /* OUT */
)
{

  /* sanity checks. */
  if ( !objecthandle || !p_context || !lock_descriptor )
    Return(ERR_FSAL_FAULT ,0 , INDEX_FSAL_lock);
  
  Return(ERR_FSAL_NOTSUPP ,0 , INDEX_FSAL_lock);
}


/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t  FSAL_changelock(
    fsal_lockdesc_t         * lock_descriptor,         /* IN / OUT */
    fsal_lockparam_t        * lock_info             /* IN */
){
  
  /* sanity checks. */
  if ( !lock_descriptor )
    Return(ERR_FSAL_FAULT ,0 , INDEX_FSAL_changelock);
  
  Return(ERR_FSAL_NOTSUPP ,0 , INDEX_FSAL_changelock);
    
}


/**
 * FSAL_unlock:
 * Not implemented.
 */
fsal_status_t  FSAL_unlock(
    fsal_lockdesc_t * lock_descriptor         /* IN/OUT */
){
  
  /* sanity checks. */
  if ( !lock_descriptor )
    Return(ERR_FSAL_FAULT ,0 , INDEX_FSAL_unlock);
  
  Return(ERR_FSAL_NOTSUPP ,0 , INDEX_FSAL_unlock);
    
}
