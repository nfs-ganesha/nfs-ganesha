/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  
 * USA
 *
 * ---------------------------------------
 */

/**
 *
 * \file    fsal_convert.h
 * \version $Revision: 1.13 $
 * \brief   XFS to FSAL type converting function.
 *
 */
#ifndef _FSAL_CONVERTION_H
#define _FSAL_CONVERTION_H
#include "fsi_ipc_ccl.h"
#include "fsal.h"

/* convert error codes */
int posix2fsal_error(int posix_errorcode);

/** converts an fsal open flag to an hpss open flag. */
int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags);

/** converts an FSAL permission test to a Posix permission test. */
int fsal2posix_testperm(fsal_accessflags_t testperm);

/**
 * Converts POSIX attributes (struct stat) to FSAL attributes 
 * (fsal_attrib_list_t)
 */
fsal_status_t posix2fsal_attributes(struct stat *p_buffstat,
				    struct attrlist *p_fsalattr_out);
/** converts FSAL access mode to unix mode. */
mode_t fsal2unix_mode(mode_t fsal_mode);

/** converts unix access mode to fsal mode. */
mode_t unix2fsal_mode(mode_t unix_mode);

/** converts hpss object type to fsal object type. */
object_file_type_t posix2fsal_type(mode_t posix_type_in);

/** converts posix fsid to fsal FSid. */
fsal_fsid_t posix2fsal_fsid(dev_t posix_devid);

/**
 * fsal2posix_time:
 * Converts FSAL time structure (fsal_time_t)
 * to POSIX time type (time_t).
 */
#define fsal2posix_time(_time_) ((time_t)(_time_).seconds)

 /**
 * posix2fsal_time:
 * Convert POSIX time structure (time_t)
 * to FSAL time type (now struct timespec).
 *
 * XXX Remove me (the entire file), included in
 * The routine is in include/FSAL/fsal_convert.h 
 *
 */
static inline struct timespec posix2fsal_time(time_t tsec, time_t nsec)
{
	struct timespec ts = {.tv_sec = tsec,.tv_nsec = nsec };
	return (ts);
}

#define high32m( a ) ( (unsigned int)( a >> 32 ) )
#define low32m( a ) ( (unsigned int)a )

#endif
