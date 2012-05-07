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

#ifndef QUOTABLOCK_SIZE
#define QUOTABLOCK_SIZE (1 << 10)
#endif

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
              
/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param quota_type
 *         the kind of requested quota (user or group)
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        pointer to the structure containing the wanted quotas
 * \param presquot (output)
 *        pointer to the structure containing the resulting quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t LUSTREFSAL_set_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota,  /* IN */
                                fsal_quota_t * presquota)       /* OUT */
{
  fsal_status_t fsal_status;
  struct if_quotactl dataquota ;

  memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

  dataquota.qc_cmd  = LUSTRE_Q_GETQUOTA ;
  dataquota.qc_type = quota_type ; 
  dataquota.qc_id = fsal_uid ;

  if(!pfsal_path || !pfsal_path->path ||!pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

  /* Convert FSAL structure to XFS one */
  if(pquota->bhardlimit != 0)
    {
      dataquota.qc_dqblk.dqb_bhardlimit = pquota->bhardlimit;
      dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->bsoftlimit != 0)
    {
      dataquota.qc_dqblk.dqb_bsoftlimit = pquota->bsoftlimit;
      dataquota.qc_dqblk.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->fhardlimit != 0)
    {
      dataquota.qc_dqblk.dqb_ihardlimit = pquota->fhardlimit;
      dataquota.qc_dqblk.dqb_valid |= QIF_ILIMITS;
    }

  if(pquota->btimeleft != 0)
    {
      dataquota.qc_dqblk.dqb_btime = pquota->btimeleft;
      dataquota.qc_dqblk.dqb_valid |= QIF_BTIME;
    }

  if(pquota->ftimeleft != 0)
    {
      dataquota.qc_dqblk.dqb_itime = pquota->ftimeleft;
      dataquota.qc_dqblk.dqb_valid |= QIF_ITIME;
    }

  if(llapi_quotactl( pfsal_path->path, &dataquota) < 0 )
    ReturnCode(posix2fsal_error(errno), errno);

  if(presquota != NULL)
    {
      fsal_status = FSAL_get_quota(pfsal_path, quota_type, fsal_uid, presquota);

      if(FSAL_IS_ERROR(fsal_status))
        return fsal_status;
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_set_quota */ 

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param quota_type
 *         the kind of requested quota (user or group)
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        Parameter to the structure containing the requested quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t LUSTREFSAL_get_quota(fsal_path_t * pfsal_path,       /* IN */
                                   int quota_type, /* IN */
                                   fsal_uid_t fsal_uid,    /* IN */
                                   fsal_quota_t * pquota)  /* OUT */
{
  fsal_status_t fsal_status;
  struct if_quotactl dataquota ;

  memset((char *)&dataquota, 0, sizeof(struct if_quotactl));

  if(!pfsal_path || !pfsal_path->path ||!pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  dataquota.qc_cmd  = LUSTRE_Q_GETQUOTA ;
  dataquota.qc_type = quota_type ; 
  dataquota.qc_id = fsal_uid ;

  if(llapi_quotactl( pfsal_path->path, &dataquota) < 0 )
    ReturnCode(posix2fsal_error(errno), errno);

  /* Convert XFS structure to FSAL one */
  pquota->bsize      = 1024; // LUSTRE has block of 1024 bytes

  pquota->bhardlimit = dataquota.qc_dqblk.dqb_bhardlimit ;
  pquota->bsoftlimit = dataquota.qc_dqblk.dqb_bsoftlimit ;
  pquota->curblocks  = dataquota.qc_dqblk.dqb_curspace /  pquota->bsize  ;

  pquota->fhardlimit = dataquota.qc_dqblk.dqb_ihardlimit;
  pquota->fsoftlimit = dataquota.qc_dqblk.dqb_isoftlimit;
  pquota->curfiles   = dataquota.qc_dqblk.dqb_curinodes;

  /* Times left are set only if used resource is in-between soft and hard limits */
  if( ( pquota->curfiles >  pquota->fsoftlimit ) && ( pquota->curfiles <  pquota->fhardlimit ) )
     pquota->ftimeleft  = dataquota.qc_dqblk.dqb_itime;
  else
     pquota->ftimeleft = 0 ;

  if( ( pquota->curblocks >  pquota->bsoftlimit ) && ( pquota->curblocks <  pquota->bhardlimit ) )
     pquota->btimeleft  = dataquota.qc_dqblk.dqb_btime;
  else
     pquota->btimeleft = 0 ;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_get_quota */


