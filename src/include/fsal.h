/*
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal.h
 * @brief Miscelaneous FSAL definitions
 */

#ifndef FSAL_H
#define FSAL_H

#include <stddef.h>

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "common_utils.h"

/******************************************************
 *            Attribute mask management.
 ******************************************************/

/** this macro tests if an attribute is set
 *  example :
 *  FSAL_TEST_MASK( attrib_list.mask, FSAL_ATTR_CREATION )
 */
#define FSAL_TEST_MASK(_attrib_mask_ , _attr_const_) \
	((_attrib_mask_) & (_attr_const_))

/** this macro sets an attribute
 *  example :
 *  FSAL_SET_MASK( attrib_list.mask, FSAL_ATTR_CREATION )
 */
#define FSAL_SET_MASK(_attrib_mask_ , _attr_const_) \
	((_attrib_mask_) |= (_attr_const_))

#define FSAL_UNSET_MASK(_attrib_mask_ , _attr_const_) \
	((_attrib_mask_) &= ~(_attr_const_))

/** this macro clears the attribute mask
 *  example :
 *  FSAL_CLEAR_MASK( attrib_list.asked_attributes )
 */
#define FSAL_CLEAR_MASK(_attrib_mask_) \
	((_attrib_mask_) = 0LL)

/******************************************************
 *              FSAL Returns macros
 ******************************************************/

/**
 *  ReturnCode :
 *  Macro for returning a fsal_status_t without trace nor stats increment.
 */
static inline fsal_status_t fsalstat(fsal_errors_t major, uint32_t minor)
{
	fsal_status_t status = {major, minor};
	return status;
}

/******************************************************
 *              FSAL Errors handling.
 ******************************************************/

/** Tests whether the returned status is errorneous.
 *  Example :
 *  if ( FSAL_IS_ERROR( status = FSAL_call(...) )){
 *     printf("ERROR status = %d, %d\n", status.major,status.minor);
 *  }
 */
#define FSAL_IS_ERROR(_status_) \
	(!((_status_).major == ERR_FSAL_NO_ERROR))

/******************************************************
 *          FSAL extended attributes management.
 ******************************************************/

/** An extented attribute entry */
typedef struct fsal_xattrent {
	uint64_t xattr_id;	/*< xattr index */
	uint64_t xattr_cookie;	/*< cookie for the next entry */
	struct attrlist attributes;	/*< entry attributes (if supported) */
	char xattr_name[MAXNAMLEN + 1];	/*< attribute name  */
} fsal_xattrent_t;

/* generic definitions for extended attributes */

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200
/* function for getting an attribute value */
#define XATTR_RW_COOKIE ~0

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

#include "fsal_api.h"
#include "FSAL/access_check.h"	/* rethink where this should go */

void display_fsinfo(struct fsal_staticfsinfo_t *info);

/**
 * @brief If we don't know how big a buffer we want for a link, use
 * this value.
 */

static const size_t fsal_default_linksize = 4096;

void destroy_fsals(void);
void emergency_cleanup_fsals(void);

const char *msg_fsal_err(fsal_errors_t fsal_err);

#endif				/* !FSAL_H */
/** @} */
