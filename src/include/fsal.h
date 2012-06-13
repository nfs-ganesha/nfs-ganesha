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
 * \file    fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:41:01 $
 * \version $Revision: 1.72 $
 * \brief   File System Abstraction Layer interface.
 *
 *
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

#ifndef _USE_SWIG

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
 * Return :
 * Macro for returning from functions
 * with trace and function call increment.
 */

#define Return( _code_, _minor_ , _f_ ) do {                                   \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;          \
               (_struct_status_).major = (_code_) ;                            \
               (_struct_status_).minor = (_minor_) ;                           \
               fsal_increment_nbcall( _f_,_struct_status_ );                   \
               if(isDebug(COMPONENT_FSAL))                                     \
                 {                                                             \
                   if((_struct_status_).major != ERR_FSAL_NO_ERROR)            \
                     LogDebug(COMPONENT_FSAL,                                  \
                       "%s returns (%s, %s, %d)",fsal_function_names[_f_],     \
                       label_fsal_err(_code_), msg_fsal_err(_code_), _minor_); \
                   else                                                        \
                     LogFullDebug(COMPONENT_FSAL,                              \
                       "%s returns (%s, %s, %d)",fsal_function_names[_f_],     \
                       label_fsal_err(_code_), msg_fsal_err(_code_), _minor_); \
                 }                                                             \
               return (_struct_status_);                                       \
              } while(0)

#define ReturnStatus( _st_, _f_ )	Return( (_st_).major, (_st_).minor, _f_ )

/**
 *  ReturnCode :
 *  Macro for returning a fsal_status_t without trace nor stats increment.
 */
#define ReturnCode( _code_, _minor_ ) do {                               \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;\
               (_struct_status_).major = (_code_) ;          \
               (_struct_status_).minor = (_minor_) ;         \
               return (_struct_status_);                     \
              } while(0)

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

/**
 *  Tests whether an error code is retryable.
 */
fsal_boolean_t fsal_is_retryable(fsal_status_t status);

#endif                          /* ! _USE_SWIG */

/******************************************************
 *              FSAL Strings handling.
 ******************************************************/

fsal_status_t FSAL_str2name(const char *string, /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_name_t * name  /* OUT */
    );

fsal_status_t FSAL_name2str(fsal_name_t * p_name,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    );

int FSAL_namecmp(const fsal_name_t *p_name1,
                 const fsal_name_t *p_name2);

fsal_status_t FSAL_namecpy(fsal_name_t * p_tgt_name, fsal_name_t * p_src_name);

fsal_status_t FSAL_str2path(char *string,       /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_path_t * p_path        /* OUT */
    );

fsal_status_t FSAL_path2str(fsal_path_t * p_path,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    );

int FSAL_pathcmp(fsal_path_t * p_path1, fsal_path_t * p_path2);

fsal_status_t FSAL_pathcpy(fsal_path_t * p_tgt_path, fsal_path_t * p_src_path);

#ifndef _USE_SWIG
/** utf8 management functions. */

fsal_status_t FSAL_buffdesc2name(fsal_buffdesc_t * in_buf, fsal_name_t * out_name);

fsal_status_t FSAL_buffdesc2path(fsal_buffdesc_t * in_buf, fsal_path_t * out_path);

fsal_status_t FSAL_path2buffdesc(fsal_path_t * in_path, fsal_buffdesc_t * out_buff);

fsal_status_t FSAL_name2buffdesc(fsal_name_t * in_name, fsal_buffdesc_t * out_buff);

#endif                          /* ! _USE_SWIG */

/* snprintmem and sscanmem are defined into common_utils */

#define snprintHandle(target, tgt_size, p_handle) \
  do {                                                           \
        struct fsal_handle_desc fd = {                           \
                .start = (char *)p_handle                        \
        };                                                       \
        FSAL_ExpandHandle(NULL, FSAL_DIGEST_SIZEOF, &fd);        \
        snprintmem(target,tgt_size,(caddr_t)p_handle,fd.len);    \
  } while(0)

#define snprintCookie(target, tgt_size, p_cookie) \
  snprintmem(target,tgt_size,(caddr_t)p_cookie,sizeof(fsal_cookie_t))

#define snprintAttrs(target, tgt_size, p_attrs) \
  snprintmem(target,tgt_size,(caddr_t)p_attrs,sizeof(fsal_attrib_list_t))

#define sscanAttrs(p_attrs,str_source) \
  sscanmem( (caddr_t)p_attrs,sizeof(fsal_attrib_list_t),str_source )

/******************************************************
 *              FSAL handles management.
 ******************************************************/

/** @TODO new fsal call to be added to new api
 */
/* fsal_status_t FSAL_share_op( fsal_file_t       * p_file_descriptor,   /\* IN *\/ */
/*                              fsal_handle_t     * p_filehandle,        /\* IN *\/ */
/*                              fsal_op_context_t * p_context,           /\* IN *\/ */
/*                              void              * p_owner,             /\* IN (opaque to FSAL) *\/ */
/*                              fsal_share_param_t  request_share        /\* IN *\/ */
/*                              ); */

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
fsal_status_t FSAL_UP_GetEvents(struct fsal_up_event_t_ ** pevents,                /* OUT */
                                fsal_count_t * event_nb,                   /* IN */
                                fsal_time_t timeout,                       /* IN */
                                fsal_count_t * peventfound,                /* OUT */
                                struct fsal_up_event_bus_context_t_ * pupebcontext /* IN */
                                );
#endif /* _USE_FSAL_UP */

/* To be called before exiting */
fsal_status_t FSAL_terminate();

#ifndef _USE_SWIG

/******************************************************
 *          FSAL extended attributes management.
 ******************************************************/

/** cookie for reading attrs from the first one */
#define XATTRS_READLIST_FROM_BEGINNING  (0)

/** An extented attribute entry */
typedef struct fsal_xattrent__
{
  unsigned int xattr_id;                 /**< xattr index */
  fsal_name_t xattr_name;                /**< attribute name  */
  unsigned int xattr_cookie;             /**< cookie for getting xattrs list from the next entry */
  fsal_attrib_list_t attributes;         /**< entry attributes (if supported) */

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
mode_t fsal2unix_mode(fsal_accessmode_t fsal_mode);

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
fsal_accessmode_t unix2fsal_mode(mode_t unix_mode);

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

/*   fsal_status_t (*fsal_share_op)( fsal_file_t            * p_file_descriptor,   /\* IN *\/ */
/*                                   fsal_handle_t          * p_filehandle,        /\* IN *\/ */
/*                                   fsal_op_context_t      * p_context,           /\* IN *\/ */
/*                                   void                   * p_owner,             /\* IN (opaque to FSAL) *\/ */
/*                                   fsal_share_param_t       request_share        /\* IN *\/ ); */

/* new api to replace above
 */

#include "fsal_api.h"
#include "FSAL/access_check.h" /* rethink where this should go */

#endif                          /* ! _USE_SWIG */

#endif                          /* _FSAL_H */
