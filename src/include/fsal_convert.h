/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file fsal_convert.h
 * @brief FSAL conversion function.
 */

#ifndef FSAL_CONVERT_H
#define FSAL_CONVERT_H

#include "fsal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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
fsal_status_t posix2fsal_attributes(const struct stat *buffstat,
				    struct attrlist *fsalattr_out);

/** converts FSAL access mode to unix mode. */
mode_t fsal2unix_mode(uint32_t fsal_mode);

/** converts unix access mode to fsal mode. */
uint32_t unix2fsal_mode(mode_t unix_mode);

/** converts hpss object type to fsal object type. */
object_file_type_t posix2fsal_type(mode_t posix_type_in);

/** converts posix fsid to fsal FSid. */
fsal_fsid_t posix2fsal_fsid(dev_t posix_devid);

 /**
 * posix2fsal_time:
 * Convert POSIX time structure (time_t)
 * to FSAL time type (now struct timespec).
 */
static inline struct timespec posix2fsal_time(time_t tsec, time_t nsec)
{
	struct timespec ts = {.tv_sec = tsec, .tv_nsec = nsec};
	return ts;
}

const char *object_file_type_to_str(object_file_type_t type);

#define my_high32m(a) ((unsigned int)(a >> 32))
#define my_low32m(a) ((unsigned int)a)

extern size_t open_fd_count;

fsal_dev_t posix2fsal_devt(dev_t posix_devid);

#endif				/* !FSAL_CONVERT_H */
/** @} */
