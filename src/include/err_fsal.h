/*
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    err_fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:22:57 $
 * \version $Revision: 1.30 $
 * \brief   FSAL error codes.
 *
 *
 */

#ifndef _ERR_FSAL_H
#define _ERR_FSAL_H

#include "log.h"

typedef enum fsal_errors_t
{
  ERR_FSAL_NO_ERROR      = 0,
  ERR_FSAL_PERM          = 1,
  ERR_FSAL_NOENT         = 2,
  ERR_FSAL_IO            = 5,
  ERR_FSAL_NXIO          = 6,
  ERR_FSAL_NOMEM         = 12,
  ERR_FSAL_ACCESS        = 13,
  ERR_FSAL_FAULT         = 14,
  ERR_FSAL_EXIST         = 17,
  ERR_FSAL_XDEV          = 18,
  ERR_FSAL_NOTDIR        = 20,
  ERR_FSAL_ISDIR         = 21,
  ERR_FSAL_INVAL         = 22,
  ERR_FSAL_FBIG          = 27,
  ERR_FSAL_NOSPC         = 28,
  ERR_FSAL_ROFS          = 30,
  ERR_FSAL_MLINK         = 31,
  ERR_FSAL_DQUOT         = 49,
  ERR_FSAL_NAMETOOLONG   = 78,
  ERR_FSAL_NOTEMPTY      = 93,
  ERR_FSAL_STALE         = 151,
  ERR_FSAL_BADHANDLE     = 10001,
  ERR_FSAL_BADCOOKIE     = 10003,
  ERR_FSAL_NOTSUPP       = 10004,
  ERR_FSAL_TOOSMALL      = 10005,
  ERR_FSAL_SERVERFAULT   = 10006,
  ERR_FSAL_BADTYPE       = 10007,
  ERR_FSAL_DELAY         = 10008,
  ERR_FSAL_FHEXPIRED     = 10014,
  ERR_FSAL_SHARE_DENIED  = 10015,
  ERR_FSAL_SYMLINK       = 10029,
  ERR_FSAL_ATTRNOTSUPP   = 10032,
  ERR_FSAL_NOT_INIT      = 20001,
  ERR_FSAL_ALREADY_INIT  = 20002,
  ERR_FSAL_BAD_INIT      = 20003,
  ERR_FSAL_SEC           = 20004,
  ERR_FSAL_NO_QUOTA      = 20005,
  ERR_FSAL_NOT_OPENED    = 20010,
  ERR_FSAL_DEADLOCK      = 20011,
  ERR_FSAL_OVERFLOW      = 20012,
  ERR_FSAL_INTERRUPT     = 20013,
  ERR_FSAL_BLOCKED       = 20014,
  ERR_FSAL_TIMEOUT       = 20015,
  ERR_FSAL_FILE_OPEN     = 10046
} fsal_errors_t;

extern family_error_t __attribute__ ((__unused__)) tab_errstatus_FSAL[];

const char * msg_fsal_err(fsal_errors_t fsal_err);
const char * label_fsal_err(fsal_errors_t fsal_err);

#endif /*_ERR_FSAL_H*/
