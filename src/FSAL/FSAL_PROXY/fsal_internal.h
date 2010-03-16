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
#include "fsal_types.h"
#include "nfs4.h"

#ifndef FSAL_INTERNAL_H
#define FSAL_INTERNAL_H
/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

#define FSAL_PROXY_OWNER_LEN 256

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

/* log descriptor */
extern log_t fsal_log;

#endif

typedef struct fsal_proxy_internal_fattr__ {
  fattr4_type type;
  fattr4_change change_time;
  fattr4_size size;
  fattr4_fsid fsid;
  fattr4_filehandle filehandle;
  fattr4_fileid fileid;
  fattr4_mode mode;
  fattr4_numlinks numlinks;
  fattr4_owner owner;           /* Needs to points to a string */
  fattr4_owner_group owner_group;       /* Needs to points to a string */
  fattr4_space_used space_used;
  fattr4_time_access time_access;
  fattr4_time_metadata time_metadata;
  fattr4_time_modify time_modify;
  fattr4_rawdev rawdev;
  char padowner[MAXNAMLEN];
  char padgroup[MAXNAMLEN];
  char padfh[NFS4_FHSIZE];
} fsal_proxy_internal_fattr_t;

typedef struct fsal_proxy_internal_fattr_readdir__ {
  fattr4_type type;
  fattr4_change change_time;
  fattr4_size size;
  fattr4_fsid fsid;
  fattr4_filehandle filehandle;
  fattr4_fileid fileid;
  fattr4_mode mode;
  fattr4_numlinks numlinks;
  fattr4_owner owner;           /* Needs to points to a string */
  fattr4_owner_group owner_group;       /* Needs to points to a string */
  fattr4_space_used space_used;
  fattr4_time_access time_access;
  fattr4_time_metadata time_metadata;
  fattr4_time_modify time_modify;
  fattr4_rawdev rawdev;
  char padowner[MAXNAMLEN];
  char padgroup[MAXNAMLEN];
  char padfh[NFS4_FHSIZE];
} fsal_proxy_internal_fattr_readdir_t;

void fsal_internal_proxy_setup_fattr(fsal_proxy_internal_fattr_t * pfattr);
void fsal_internal_proxy_setup_readdir_fattr(fsal_proxy_internal_fattr_readdir_t *
                                             pfattr);

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

void fsal_internal_proxy_create_fattr_bitmap(bitmap4 * pbitmap);
void fsal_internal_proxy_create_fattr_readdir_bitmap(bitmap4 * pbitmap);
void fsal_internal_proxy_create_fattr_fsinfo_bitmap(bitmap4 * pbitmap);
void fsal_interval_proxy_fsalattr2bitmap4(fsal_attrib_list_t * pfsalattr,
                                          bitmap4 * pbitmap);

/*
 * A few functions dedicated in proxy related information management and conversion 
 */
fsal_status_t fsal_internal_set_auth_gss(fsal_op_context_t * p_thr_context);
fsal_status_t fsal_internal_proxy_error_convert(nfsstat4 nfsstatus, int indexfunc);
int fsal_internal_proxy_create_fh(nfs_fh4 * pnfs4_handle,
                                  fsal_nodetype_t type,
                                  fsal_u64_t fileid, fsal_handle_t * pfsal_handle);
int fsal_internal_proxy_extract_fh(nfs_fh4 * pnfs4_handle, fsal_handle_t * pfsal_handle);
int fsal_internal_proxy_fsal_name_2_utf8(fsal_name_t * pname, utf8string * utf8str);
int fsal_internal_proxy_fsal_path_2_utf8(fsal_path_t * ppath, utf8string * utf8str);
int fsal_internal_proxy_fsal_utf8_2_name(fsal_name_t * pname, utf8string * utf8str);
int fsal_internal_proxy_fsal_utf8_2_path(fsal_path_t * ppath, utf8string * utf8str);
int proxy_Fattr_To_FSAL_attr(fsal_attrib_list_t * pFSAL_attr,
                             fsal_handle_t * phandle, fattr4 * Fattr);
int proxy_Fattr_To_FSAL_dynamic_fsinfo(fsal_dynamicfsinfo_t * pdynamicinfo,
                                       fattr4 * Fattr);
fsal_status_t FSAL_proxy_setclientid(fsal_op_context_t * p_context);
int FSAL_proxy_set_hldir(fsal_op_context_t * p_thr_context, char *hl_path);
int fsal_internal_ClientReconnect(fsal_op_context_t * p_thr_context);
fsal_status_t FSAL_proxy_open_confirm(fsal_file_t * pfd);
void *FSAL_proxy_change_user(fsal_op_context_t * p_thr_context);

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

#endif
