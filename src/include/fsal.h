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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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

#include "nlm_list.h"
/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "common_utils.h"

/******************************************************
 *            Attribute mask management.
 ******************************************************/

/** this macro tests if an attribute is set
 *  example :
 *  FSAL_TEST_MASK( attrib_list.supported_attributes, FSAL_ATTR_CREATION )
 */
#define FSAL_TEST_MASK( _attrib_mask_ , _attr_const_ ) \
                      ( (_attrib_mask_) & (_attr_const_) )

/** this macro sets an attribute
 *  example :
 *  FSAL_SET_MASK( attrib_list.asked_attributes, FSAL_ATTR_CREATION )
 */
#define FSAL_SET_MASK( _attrib_mask_ , _attr_const_ ) \
                    ( (_attrib_mask_) |= (_attr_const_) )

#define FSAL_UNSET_MASK( _attrib_mask_ , _attr_const_ ) \
                    ( (_attrib_mask_) &= ~(_attr_const_) )

/** this macro clears the attribute mask
 *  example :
 *  FSAL_CLEAR_MASK( attrib_list.asked_attributes )
 */
#define FSAL_CLEAR_MASK( _attrib_mask_ ) \
                    ( (_attrib_mask_) = 0LL )

/** This macro sets the cookie to its initial value
 */
#define FSAL_SET_COOKIE_BEGINNING( cookie ) memset( (char *)&cookie, 0, sizeof( fsal_cookie_t ) )

/******************************************************
 *              FSAL Returns macros
 ******************************************************/

/**
 *  ReturnCode :
 *  Macro for returning a fsal_status_t without trace nor stats increment.
 */
static inline fsal_status_t
fsalstat(fsal_errors_t major, uint32_t minor)
{                                                                       \
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
#define FSAL_IS_ERROR( _status_ ) \
        ( ! ( ( _status_ ).major == ERR_FSAL_NO_ERROR ) )



/******************************************************
 *          FSAL extended attributes management.
 ******************************************************/

/** cookie for reading attrs from the first one */
#define XATTRS_READLIST_FROM_BEGINNING  (0)

/** An extented attribute entry */
typedef struct fsal_xattrent__
{
  uint64_t xattr_id; /*< xattr index */
  uint64_t xattr_cookie; /*< cookie for getting xattrs list from the next entry */
  struct attrlist attributes;/*< entry attributes (if supported) */
  char xattr_name[MAXNAMLEN]; /*< attribute name  */
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

/**
 * fsal2unix_mode:
 * Convert FSAL mode to posix mode.
 *
 * \param fsal_mode (input):
 *        The FSAL mode to be translated.
 *
 * \return The posix mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(uint32_t fsal_mode);

fsal_dev_t posix2fsal_devt(dev_t posix_devid);

/**
 * unix2fsal_mode:
 * Convert posix mode to FSAL mode.
 *
 * \param unix_mode (input):
 *        The posix mode to be translated.
 *
 * \return The FSAL mode associated to unix_mode.
 */
uint32_t unix2fsal_mode(mode_t unix_mode);

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

/** @TODO interfaces that have to be added to new api
 */
/*   /\* FSAL_UP functions *\/ */
/* #ifdef _USE_FSAL_UP */
/*   fsal_status_t(*fsal_up_init) (struct fsal_up_event_bus_parameter_t_ * pebparam,      /\* IN *\/ */
/*                                 struct fsal_up_event_bus_context_t_ * pupebcontext     /\* OUT *\/ ); */
/*   fsal_status_t(*fsal_up_addfilter)(struct fsal_up_event_bus_filter_t_ * pupebfilter,  /\* IN *\/ */
/*                                   struct fsal_up_event_bus_context_t_ * pupebcontext /\* INOUT *\/ ); */
/*   fsal_status_t(*fsal_up_getevents)(struct fsal_up_event_t_ ** pevents,                /\* OUT *\/ */
/*                                   fsal_count_t * event_nb,                   /\* IN *\/ */
/*                                   fsal_time_t timeout,                       /\* IN *\/ */
/*                                     fsal_count_t * peventfound,                 /\* OUT *\/ */
/*                                   struct fsal_up_event_bus_context_t_ * pupebcontext /\* IN *\/ ); */
/* #endif /\* _USE_FSAL_UP *\/ */

/* new api to replace above
 */

#include "fsal_api.h"
#include "FSAL/access_check.h" /* rethink where this should go */

void display_fsinfo(struct fsal_staticfsinfo_t *info);

#endif /* !FSAL_H */
/** @} */
