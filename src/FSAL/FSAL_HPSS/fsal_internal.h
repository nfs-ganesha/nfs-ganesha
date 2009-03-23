/**
 *
 * \file    fsal_internal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.12 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 * 
 */
 
#include  "fsal.h"

#if defined( _USE_HPSS_62 ) || defined ( _USE_HPSS_622 )
#include <hpss_mech.h>
#include <hpss_String.h>
#endif


/* defined the set of attributes supported with HPSS */
#define HPSS_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_ACL      | FSAL_ATTR_FILEID    | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_CREATION  | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_MOUNTFILEID | FSAL_ATTR_CHGTIME  )


/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t   global_fs_info;

extern fsal_uint_t           CredentialLifetime;

extern fsal_uint_t           ReturnInconsistentDirent;

/* log descriptor */
extern log_t   fsal_log;


#endif

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global( fsal_init_info_t    *    fsal_info ,
                                         fs_common_initinfo_t  *  fs_common_info );

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall( int function_index , fsal_status_t  status );

/**
 * Retrieves current thread statistics.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats);


/**
 * Set credential lifetime.
 */
void fsal_internal_SetCredentialLifetime(fsal_uint_t lifetime_in);

/**
 * Set behavior when detecting a MD inconsistency in readdir.
 */
void fsal_internal_SetReturnInconsistentDirent(fsal_uint_t bool_in);


/**
 *  Used to limit the number of simultaneous calls to Filesystem.
 */
void  TakeTokenFSCall();
void  ReleaseTokenFSCall();

/**
 * fsal_do_log:
 * Indicates if an FSAL error has to be traced
 * into its log file in the NIV_EVENT level.
 * (in the other cases, return codes are only logged
 * in the NIV_FULL_DEBUG logging lovel).
 */
fsal_boolean_t  fsal_do_log( fsal_status_t  status );


/**
 * Return :
 * Macro for returning from functions
 * with trace and function call increment.
 */
 
#define Return( _code_, _minor_ , _f_ ) do {                          \
                                                                      \
               char _str_[256];                                       \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ; \
               (_struct_status_).major = (_code_) ;                   \
               (_struct_status_).minor = (_minor_) ;                  \
               fsal_increment_nbcall( _f_,_struct_status_ );          \
               log_snprintf( _str_, 256, "%J%r",ERR_FSAL, _code_ );   \
                                                                      \
               if ( fsal_do_log( _struct_status_ ) )                  \
                   DisplayLogJdLevel( fsal_log, NIV_EVENT,            \
                        "%s returns ( %s, %d )",fsal_function_names[_f_], \
                        _str_, _minor_);                              \
               else                                                   \
                   DisplayLogJdLevel( fsal_log, NIV_FULL_DEBUG,       \
                        "%s returns ( %s, %d )",fsal_function_names[_f_], \
                        _str_, _minor_);                              \
                                                                      \
               return (_struct_status_);                              \
                                                                      \
              } while(0)
 
 
 
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

 
 
