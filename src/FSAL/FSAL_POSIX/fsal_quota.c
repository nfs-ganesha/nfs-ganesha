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

/* For quotactl */
#include <sys/quota.h>
#include <sys/types.h>
#include <string.h>

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
fsal_status_t POSIXFSAL_get_quota(fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota)        /* OUT */
{
  struct dqblk fs_quota;
  char fs_spec[MAXPATHLEN];

  if(!pfsal_path || !pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(fsal_internal_path2fsname(pfsal_path->path, fs_spec) == -1)
    ReturnCode(ERR_FSAL_INVAL, 0);

  memset((char *)&fs_quota, 0, sizeof(struct dqblk));

  if(quotactl(FSAL_QCMD(Q_GETQUOTA, quota_type), fs_spec, fsal_uid, (caddr_t) & fs_quota)
     < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  /* Convert XFS structure to FSAL one */
  pquota->bhardlimit = fs_quota.dqb_bhardlimit;
  pquota->bsoftlimit = fs_quota.dqb_bsoftlimit;
  pquota->curblocks = fs_quota.dqb_curspace;
  pquota->fhardlimit = fs_quota.dqb_ihardlimit;
  pquota->curfiles = fs_quota.dqb_curinodes;
  pquota->btimeleft = fs_quota.dqb_btime;
  pquota->ftimeleft = fs_quota.dqb_itime;
  pquota->bsize = DEV_BSIZE;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_get_quota */

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

fsal_status_t POSIXFSAL_set_quota(fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota,        /* IN */
                                  fsal_quota_t * presquota)     /* OUT */
{
  struct dqblk fs_quota;
  fsal_status_t fsal_status;
  char fs_spec[MAXPATHLEN];

  if(!pfsal_path || !pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(fsal_internal_path2fsname(pfsal_path->path, fs_spec) == -1)
    ReturnCode(ERR_FSAL_INVAL, 0);

  memset((char *)&fs_quota, 0, sizeof(struct dqblk));

  /* Convert FSAL structure to XFS one */
  if(pquota->bhardlimit != 0)
    {
      fs_quota.dqb_bhardlimit = pquota->bhardlimit;
      fs_quota.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->bsoftlimit != 0)
    {
      fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
      fs_quota.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->fhardlimit != 0)
    {
      fs_quota.dqb_ihardlimit = pquota->fhardlimit;
      fs_quota.dqb_valid |= QIF_ILIMITS;
    }

  if(pquota->btimeleft != 0)
    {
      fs_quota.dqb_btime = pquota->btimeleft;
      fs_quota.dqb_valid |= QIF_BTIME;
    }

  if(pquota->ftimeleft != 0)
    {
      fs_quota.dqb_itime = pquota->ftimeleft;
      fs_quota.dqb_valid |= QIF_ITIME;
    }

  if(quotactl(FSAL_QCMD(Q_SETQUOTA, quota_type), fs_spec, fsal_uid, (caddr_t) & fs_quota)
     < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  if(presquota != NULL)
    {
      fsal_status = POSIXFSAL_get_quota(pfsal_path, quota_type, fsal_uid, presquota);

      if(FSAL_IS_ERROR(fsal_status))
        return fsal_status;
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_set_quota */
