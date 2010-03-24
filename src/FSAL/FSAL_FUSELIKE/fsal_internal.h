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

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

/* log descriptor */
extern log_t fsal_log;

/* filesystem operations */
extern struct ganefuse_operations *p_fs_ops;

/* filesystem opaque data */
extern void *fs_user_data;
extern void *fs_private_data;

#endif

struct ganefuse
{
  /* unused for now */
  void *reserved;
};

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info);

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);

/**
 * Retrieves current thread statistics.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats);

/**
 *  Used to limit the number of simultaneous calls to Filesystem.
 */
void TakeTokenFSCall();
void ReleaseTokenFSCall();

/**
 * fsal_do_log:
 * Indicates if an FSAL error has to be traced
 * into its log file in the NIV_EVENT level.
 * (in the other cases, return codes are only logged
 * in the NIV_FULL_DEBUG logging lovel).
 */
fsal_boolean_t fsal_do_log(fsal_status_t status);

/**
 * This function sets the current context for a filesystem operation,
 * so it can be retrieved with fuse_get_context().
 * The structure pointed by p_ctx must stay allocated and kept unchanged
 * during the FS call. 
 */
int fsal_set_thread_context(fsal_op_context_t * p_ctx);

/**
 * This function retrieves the last context associated to a thread.
 */
fsal_op_context_t *fsal_get_thread_context();

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

/* used for generating inode numbers for FS which don't have some */
static inline unsigned long hash_peer(ino_t parent_inode, char *name)
{
  unsigned int i;
  unsigned long hash;
  char *curr;

  hash = 1;

  for (curr = name; *curr != '\0'; curr++)
    hash = ((hash << 5) - hash + (unsigned long)(*curr));

  return (hash ^ (unsigned long)parent_inode);
}
