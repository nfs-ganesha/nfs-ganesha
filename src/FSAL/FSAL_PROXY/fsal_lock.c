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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif				/* _SOLARIS */

#include <string.h>
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/xdr.h>
#else
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#endif
#include "nfs4.h"

#include "stuff_alloc.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

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
fsal_status_t FSAL_lock(fsal_handle_t * objecthandle,	/* IN */
			fsal_op_context_t * p_context,	/* IN */
			fsal_lockparam_t * lock_info,	/* IN */
			fsal_lockdesc_t * lock_descriptor	/* OUT */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;

#define FSAL_LOCK_NB_OP_ALLOC 7
#define FSAL_OPEN_VAL_BUFFER  1024

  nfs_argop4 argoparray[FSAL_LOCK_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_LOCK_NB_OP_ALLOC];

  /* sanity checks. */
  if (!objecthandle || !p_context || !lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;

  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lock" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */
  if (fsal_internal_proxy_extract_fh(&nfs4fh, objecthandle) == FALSE)
    {
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock);
    }
#define FSAL_LOCK_IDX_OP_PUTFH       0
#define FSAL_LOCK_IDX_OP_LOCK        1
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock);
}				/* FSAL_lock */

/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t FSAL_changelock(fsal_lockdesc_t * lock_descriptor,	/* IN / OUT */
			      fsal_lockparam_t * lock_info	/* IN */
    )
{

  /* sanity checks. */
  if (!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_changelock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_changelock);

}

/**
 * FSAL_unlock:
 * Not implemented.
 */
fsal_status_t FSAL_unlock(fsal_lockdesc_t * lock_descriptor	/* IN/OUT */
    )
{

  /* sanity checks. */
  if (!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_unlock);

}
