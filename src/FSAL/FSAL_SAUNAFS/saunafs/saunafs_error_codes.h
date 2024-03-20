/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
   Copyright 2017 Skytechnology sp. z o.o.
   Copyright 2023 Leil Storage OÜ

   This file is part of SaunaFS.

   SaunaFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   SaunaFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SaunaFS  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SAUNAFS_ERROR_CODES_H
#define __SAUNAFS_ERROR_CODES_H

#include <stdint.h>

enum saunafs_error_code {
	SAUNAFS_STATUS_OK                     =  0,
	SAUNAFS_ERROR_EPERM                   =  1,
	SAUNAFS_ERROR_ENOTDIR                 =  2,
	SAUNAFS_ERROR_ENOENT                  =  3,
	SAUNAFS_ERROR_EACCES                  =  4,
	SAUNAFS_ERROR_EEXIST                  =  5,
	SAUNAFS_ERROR_EINVAL                  =  6,
	SAUNAFS_ERROR_ENOTEMPTY               =  7,
	SAUNAFS_ERROR_CHUNKLOST               =  8,
	SAUNAFS_ERROR_OUTOFMEMORY             =  9,
	SAUNAFS_ERROR_INDEXTOOBIG             = 10,
	SAUNAFS_ERROR_LOCKED                  = 11,
	SAUNAFS_ERROR_NOCHUNKSERVERS          = 12,
	SAUNAFS_ERROR_NOCHUNK                 = 13,
	SAUNAFS_ERROR_CHUNKBUSY               = 14,
	SAUNAFS_ERROR_REGISTER                = 15,
	SAUNAFS_ERROR_NOTDONE                 = 16,
	SAUNAFS_ERROR_GROUPNOTREGISTERED      = 17,
	SAUNAFS_ERROR_NOTSTARTED              = 18,
	SAUNAFS_ERROR_WRONGVERSION            = 19,
	SAUNAFS_ERROR_CHUNKEXIST              = 20,
	SAUNAFS_ERROR_NOSPACE                 = 21,
	SAUNAFS_ERROR_IO                      = 22,
	SAUNAFS_ERROR_BNUMTOOBIG              = 23,
	SAUNAFS_ERROR_WRONGSIZE               = 24,
	SAUNAFS_ERROR_WRONGOFFSET             = 25,
	SAUNAFS_ERROR_CANTCONNECT             = 26,
	SAUNAFS_ERROR_WRONGCHUNKID            = 27,
	SAUNAFS_ERROR_DISCONNECTED            = 28,
	SAUNAFS_ERROR_CRC                     = 29,
	SAUNAFS_ERROR_DELAYED                 = 30,
	SAUNAFS_ERROR_CANTCREATEPATH          = 31,
	SAUNAFS_ERROR_MISMATCH                = 32,
	SAUNAFS_ERROR_EROFS                   = 33,
	SAUNAFS_ERROR_QUOTA                   = 34,
	SAUNAFS_ERROR_BADSESSIONID            = 35,
	SAUNAFS_ERROR_NOPASSWORD              = 36,
	SAUNAFS_ERROR_BADPASSWORD             = 37,
	SAUNAFS_ERROR_ENOATTR                 = 38,
	SAUNAFS_ERROR_ENOTSUP                 = 39,
	SAUNAFS_ERROR_ERANGE                  = 40,
	SAUNAFS_ERROR_TIMEOUT                 = 41,
	SAUNAFS_ERROR_BADMETADATACHECKSUM     = 42,
	SAUNAFS_ERROR_CHANGELOGINCONSISTENT   = 43,
	SAUNAFS_ERROR_PARSE                   = 44,
	SAUNAFS_ERROR_METADATAVERSIONMISMATCH = 45,
	SAUNAFS_ERROR_NOTLOCKED               = 46,
	SAUNAFS_ERROR_WRONGLOCKID             = 47,
	SAUNAFS_ERROR_NOTPOSSIBLE             = 48,
	SAUNAFS_ERROR_TEMP_NOTPOSSIBLE        = 49,
	SAUNAFS_ERROR_WAITING                 = 50,
	SAUNAFS_ERROR_UNKNOWN                 = 51,
	SAUNAFS_ERROR_ENAMETOOLONG            = 52,
	SAUNAFS_ERROR_EFBIG                   = 53,
	SAUNAFS_ERROR_EBADF                   = 54,
	SAUNAFS_ERROR_ENODATA                 = 55,
	SAUNAFS_ERROR_E2BIG                   = 56,
	SAUNAFS_ERROR_MAX                     = 57
};

const char *saunafs_error_string(uint8_t status);

#endif
