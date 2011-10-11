/*
 *
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
 * \file    extended_types.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:26:30 $
 * \version $Revision: 1.3 $
 * \brief   Extended type, platform dependant.
 *
 * extended_types.h: defines some types, line longlong_t or u_longlong_t if not defined in the OS headers.
 *
 *
 */

#ifndef _EXTENDED_TYPES_H
#define _EXTENDED_TYPES_H

#include <sys/types.h>

/* Added extended types, often missing */
typedef long long longlong_t;
typedef unsigned long long u_longlong_t;

typedef unsigned int uint_t;
typedef unsigned int uint32_t;

# ifndef __int8_t_defined

#if SIZEOF_LONG == 8
typedef unsigned long int uint64_t;
typedef long int int64_t;
#else
typedef unsigned long long int uint64_t;
typedef long long int int64_t;
#endif
#endif

#endif                          /* _EXTENDED_TYPES_H */
