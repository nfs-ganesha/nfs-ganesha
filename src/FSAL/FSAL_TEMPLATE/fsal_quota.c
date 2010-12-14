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

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        Parameter to the structure containing the requested quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t FSAL_get_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type, fsal_uid_t fsal_uid, fsal_quota_t * pquota)        /* OUT */
{
  ReturnCode(ERR_FSAL_NO_QUOTA, 0);
}                               /*  FSAL_get_quota */

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
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

fsal_status_t FSAL_set_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type, fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquot,      /* IN */
                             fsal_quota_t * presquot)   /* OUT */
{
  ReturnCode(ERR_FSAL_NO_QUOTA, 0);
}                               /*  FSAL_set_quota */
