/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2012)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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
 *
 * \file    fsal_thrcreds.c
 * \brief   FSAL's set creds functions.
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/fsuid.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/quota.h>
#include "log.h"
#include "fsal.h"
#include "FSAL/common_methods.h"


fsal_status_t COMMON_SetThrCred( fsal_uid_t uid, fsal_gid_t gid )
{
  fsal_status_t status ;

  status.major = ERR_FSAL_NO_ERROR ;
  status.minor = 0 ;

  /* Do not check setfsuid return code, 
   * it's the old fsuid, never an error code (see manpage) */
  setfsuid( uid ) ;
  setfsgid( gid ) ;

  /* Always a succes, if something fails next call to FSAL will
   * produce a EPERM error */
 
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
} /* COMMON_SetThrCred */
