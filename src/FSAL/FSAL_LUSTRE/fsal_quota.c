/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * ------------- 
 */

/**
 *
 * \file    fsal_quota.c
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/* For llapi_quotactl */
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>

/**
 * FSAL_check_quota :
 * checks if quotas allow a user to do an operation
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param  quota_type
 *        type of quota to be checked (inodes or blocks       
 * \param  fsal_uid
 *        uid for the user whose quota are requested
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */


fsal_status_t LUSTREFSAL_check_quota( char              * path,  /* IN */
                                      fsal_uid_t          fsal_uid)      /* IN */
{
  struct if_quotactl dataquota ;
  char fs_spec[MAXPATHLEN];

  if(!path )
    ReturnCode(ERR_FSAL_FAULT, 0);

  if( fsal_uid == 0 ) /* No quota for root */
    ReturnCode(ERR_FSAL_NO_ERROR, 0) ;

  memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

  dataquota.qc_cmd  = LUSTRE_Q_GETQUOTA ;
  dataquota.qc_type = USRQUOTA ; // UGQUOTA ??
  dataquota.qc_id = fsal_uid ;

  if(llapi_quotactl( path, &dataquota) < 0 )
    ReturnCode(posix2fsal_error(errno), errno);

  /* If dqb_bhardlimit is no-zero, then quota are set for this user */
  if(  dataquota.qc_dqblk.dqb_bhardlimit != 0 )
    if( dataquota.qc_dqblk.dqb_curspace > dataquota.qc_dqblk.dqb_bhardlimit  )
        ReturnCode( ERR_FSAL_DQUOT, EDQUOT ) ;


  ReturnCode(ERR_FSAL_NO_ERROR, 0) ;
} /* LUSTREFSAL_check_quota */
               
