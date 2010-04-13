/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_lock.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:37:28 $
 * \version $Revision: 1.3 $
 * \brief   Locking operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

fsal_status_t FSAL_lock(fsal_handle_t * objecthandle,   /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_lockparam_t * lock_info,   /* IN */
                        fsal_lockdesc_t * lock_descriptor       /* OUT */
    )
{

  /* for logging */
  SetFuncID(INDEX_FSAL_lock);

  /* sanity checks. */
  if(!objecthandle || !p_context || !lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock);
}

fsal_status_t FSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                              fsal_lockparam_t * lock_info      /* IN */
    )
{
  /* for logging */
  SetFuncID(INDEX_FSAL_changelock);

  /* sanity checks. */
  if(!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_changelock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_changelock);

}

fsal_status_t FSAL_unlock(fsal_lockdesc_t * lock_descriptor     /* IN/OUT */
    )
{

  /* for logging */
  SetFuncID(INDEX_FSAL_unlock);

  /* sanity checks. */
  if(!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);

}
