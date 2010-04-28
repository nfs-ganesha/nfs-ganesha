/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
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

#if __WORDSIZE == 64
typedef unsigned long int uint64_t;
typedef long int int64_t;
#else
typedef unsigned long long int uint64_t;
typedef long long int int64_t;
#endif

#endif                          /* _EXTENDED_TYPES_H */
