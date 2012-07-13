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
 * @file    fsal.h
 * @brief   File System Abstraction Layer interface.
 */

#ifndef _FSAL_H
#define _FSAL_H

#include <stddef.h>

/**
 * BUILD_BUG_ON - break compile if a condition is true.
 * @condition: the condition which the compiler should know is false.
 *
 * If you have some code which relies on certain constants being equal, or
 * other compile-time-evaluated condition, you should use BUILD_BUG_ON to
 * detect if someone changes it.
 *
 * The implementation uses gcc's reluctance to create a negative array, but
 * gcc (as of 4.4) only emits that error for obvious cases (eg. not arguments
 * to inline functions).  So as a fallback we use the optimizer; if it can't
 * prove the condition is false, it will cause a link error on the undefined
 * "__build_bug_on_failed".  This error message can be harder to track down
 * though, hence the two different methods.
 *
 * Blatantly stolen from kernel source, include/linux/kernel.h:651
 */
#ifndef __OPTIMIZE__
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#else
extern int __build_bug_on_failed;
#define BUILD_BUG_ON(condition)                                 \
        do {                                                    \
                ((void)sizeof(char[1 - 2*!!(condition)]));      \
                if (condition) __build_bug_on_failed = 1;       \
        } while(0)
#endif

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

/** This macros manage conversion between directory offset and cookies
 *  BEWARE : this will probably bug with FSAL_SNMP
 */
#define FSAL_SET_COOKIE_BY_OFFSET( __cookie, __offset )  \
   memcpy( (char *)&(__cookie.data), (char *)&__offset, sizeof( uint64_t ) ) 

#define FSAL_SET_POFFSET_BY_COOKIE( __cookie, __poffset )  \
    memcpy( (char *)__poffset, (char *)&(__cookie.data), sizeof( uint64_t ) ) 

#define FSAL_GET_EXP_CTX( popctx ) (fsal_export_context_t *)(popctx->export_context)

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


/* FSAL_UP functions */
/* These structs are defined here because including fsal_up.h causes
 * preprocessor issues. */
#ifdef _USE_FSAL_UP
struct fsal_up_event_bus_filter_t_;
struct fsal_up_event_t_;
struct fsal_up_event_bus_parameter_t_;
struct fsal_up_event_bus_context_t_;
fsal_status_t FSAL_UP_Init(struct fsal_up_event_bus_parameter_t_ * pebparam,      /* IN */
                           struct fsal_up_event_bus_context_t_ * pupebcontext     /* OUT */
                           );

fsal_status_t FSAL_UP_AddFilter(struct fsal_up_event_bus_filter_t_ * pupebfilter,  /* IN */
                                struct fsal_up_event_bus_context_t_ * pupebcontext /* INOUT */
                                   );
fsal_status_t FSAL_UP_GetEvents(struct glist_head * pevent_head,           /* OUT */
                                fsal_count_t * event_nb,                   /* IN */
                                fsal_time_t timeout,                       /* IN */
                                fsal_count_t * peventfound,                /* OUT */
                                struct fsal_up_event_bus_context_t_ * pupebcontext /* IN */
                                );
#endif /* _USE_FSAL_UP */

/* To be called before exiting */
fsal_status_t FSAL_terminate();

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
  char xattr_name[]; /*< attribute name  */
} fsal_xattrent_t;

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

#endif                          /* _FSAL_H */
