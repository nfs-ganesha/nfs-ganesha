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
#include <sys/stat.h>

/* defined the set of attributes supported with POSIX */
#define POSIX_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_ACL      | FSAL_ATTR_FILEID    | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME  )


/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t   global_fs_info;
extern fsal_posixdb_conn_params_t  global_posixdb_params;

/* log descriptor */
extern log_t   fsal_log;

#endif

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global( fsal_init_info_t       * fsal_info ,
                                         fs_common_initinfo_t   * fs_common_info,
                                         fs_specific_initinfo_t * fs_specific_info);

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall( int function_index , fsal_status_t  status );

/**
 * Retrieves current thread statistics.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats);


/**
 *  Used to limit the number of simultaneous calls to Filesystem.
 */
void  TakeTokenFSCall();
void  ReleaseTokenFSCall();



/**
 * Return :
 * Macro for returning from functions
 * with trace and function call increment.
 */
 
#define Return( _code_, _minor_ , _f_ ) do {                          \
               char _str_[256];                                       \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ; \
               (_struct_status_).major = (_code_) ;                   \
               (_struct_status_).minor = (_minor_) ;                  \
               fsal_increment_nbcall( _f_,_struct_status_ );          \
               log_snprintf( _str_, 256, "%J%r",ERR_FSAL, _code_ );   \
               DisplayLogJdLevel( fsal_log, NIV_FULL_DEBUG,           \
                  "%s returns ( %s, %d )",fsal_function_names[_f_],   \
                  _str_, _minor_);                                    \
               return (_struct_status_);                              \
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


fsal_status_t fsal_internal_posix2posixdb_fileinfo(struct stat *buffstat, fsal_posixdb_fileinfo_t *info);

/**
 *  Check if 2 fsal_posixdb_fileinfo_t are consistent
 */
fsal_status_t fsal_internal_posixdb_checkConsistency( fsal_posixdb_fileinfo_t * p_info_fs,
                                        fsal_posixdb_fileinfo_t * p_info_db,
                                        int * p_result);

/**
 * Add a new entry to the database. Remove an already existing handle if it's not consistent
 */
fsal_status_t fsal_internal_posixdb_add_entry( fsal_posixdb_conn *p_conn,
                                 fsal_name_t *p_filename,
                                 fsal_posixdb_fileinfo_t *p_info,
                                 fsal_handle_t *p_dir_handle,
                                 fsal_handle_t *p_new_handle);

/**
 * Append a fsal_name to an fsal_path to have the full path of a file from its name and its parent path
 */
fsal_status_t fsal_internal_appendFSALNameToFSALPath(fsal_path_t *p_path, fsal_name_t * p_name);

/**
 * Get a valid path associated to an handle.
 * The function selects many paths from the DB and return the first valid one. If is_dir is set, then only 1 path will be constructed from the database.
 */
fsal_status_t fsal_internal_getPathFromHandle( fsal_op_context_t * p_context,  /* IN */
                                               fsal_handle_t     * p_handle,   /* IN */
                                               int                 is_dir,     /* IN */
                                               fsal_path_t       * p_fsalpath, /* OUT */
                                               struct stat        * p_buffstat /* OUT */);

/** 
 * @brief Get the handle of a file, knowing its name and its parent dir
 * 
 * @param p_context 
 * @param p_parent_dir_handle 
 *    Handle of the parent directory
 * @param p_fsalname 
 *    Name of the object
 * @param p_infofs
 *    Information about the file (taken from the filesystem) to be compared to information stored in database
 * @param p_object_handle 
 *    Handle of the file.
 * 
 * @return
 *    ERR_FSAL_NOERR, if no error
 *    Anothere error code else.
 */
fsal_status_t fsal_internal_getInfoFromName( fsal_op_context_t       * p_context,  /* IN */
                                             fsal_handle_t           * p_parent_dir_handle,   /* IN */
                                             fsal_name_t             * p_fsalname, /* IN */
                                             fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                             fsal_handle_t           * p_object_handle);   /* OUT */

fsal_status_t fsal_internal_getInfoFromChildrenList( fsal_op_context_t       * p_context,  /* IN */
                                                     fsal_handle_t           * p_parent_dir_handle,   /* IN */
                                                     fsal_name_t             * p_fsalname, /* IN */
                                                     fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                                     fsal_posixdb_child      * p_children, /* IN */
                                                     unsigned int              children_count, /* IN */
                                                     fsal_handle_t           * p_object_handle);   /* OUT */

/**
 *  test the access to a file knowing its POSIX attributes (struct stat) OR its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t fsal_internal_testAccess( fsal_op_context_t   * p_context,          /* IN */
                                        fsal_accessflags_t  access_type,          /* IN */
                                        struct stat         * p_buffstat,          /* IN */
                                        fsal_attrib_list_t  * p_object_attributes   /* IN */);
