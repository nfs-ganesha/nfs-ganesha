/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_errors.c
 * \author  $Author: leibovic $
 * \date    $Date: 2004/10/29 14:04:27 $
 * \version $Revision: 1.1 $
 * \brief   Routines for handling errors.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"

fsal_boolean_t fsal_is_retryable(fsal_status_t status)
{

  switch (status.major)
    {

    case ERR_FSAL_DELAY:
      return TRUE;

      /*
         case ERR_FSAL_PERM:
         case ERR_FSAL_NOENT:
         case ERR_FSAL_IO:
         case ERR_FSAL_NXIO:
         case ERR_FSAL_ACCESS:
         case ERR_FSAL_FAULT:
         case ERR_FSAL_EXIST:
         case ERR_FSAL_XDEV:
         case ERR_FSAL_NOTDIR:
         case ERR_FSAL_ISDIR:
         case ERR_FSAL_INVAL:
         case ERR_FSAL_FBIG:
         case ERR_FSAL_NOSPC:
         case ERR_FSAL_ROFS:
         case ERR_FSAL_MLINK:
         case ERR_FSAL_DQUOT:
         case ERR_FSAL_NAMETOOLONG:
         case ERR_FSAL_NOTEMPTY:
         case ERR_FSAL_STALE:
         case ERR_FSAL_BADHANDLE:
         case ERR_FSAL_BADCOOKIE:
         case ERR_FSAL_NOTSUPP:
         case ERR_FSAL_TOOSMALL:
         case ERR_FSAL_SERVERFAULT:
         case ERR_FSAL_BADTYPE:
         case ERR_FSAL_FHEXPIRED:
         case ERR_FSAL_SYMLINK:
         case ERR_FSAL_ATTRNOTSUPP:
         case ERR_FSAL_NOT_INIT:
         case ERR_FSAL_BAD_INIT:
         case ERR_FSAL_NOT_OPENED:
         case ERR_NULL:
       */

    default:
      return FALSE;
    }

}
