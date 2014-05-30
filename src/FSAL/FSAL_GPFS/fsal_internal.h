/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 *
 * \file    fsal_internal.h
 * \version $Revision: 1.12 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 * 
 */

#include <sys/stat.h>
#include "fsal.h"
#include "fsal_up.h"
#include "FSAL/common_functions.h"
#include "nlm_list.h"

/*
 * Tests whether an error code should be raised as an event.
 */
fsal_boolean_t fsal_error_is_event(fsal_status_t status);
/*
 * Tests whether an error code should be raised as an info debug.
 */
fsal_boolean_t fsal_error_is_info(fsal_status_t status);

#ifdef _USE_FSAL_UP
struct gpfs_fsal_up_ctx_t
{
  /* There is one GPFS FSAL UP Context per GPFS file system */
  struct glist_head   gf_list;    /* List of GPFS FSAL UP Contexts */
  struct glist_head   gf_exports; /* List of GPFS Export Contexts on this FSAL UP context */
  char              * gf_fs;      /* GPFS File System Directory */
  unsigned int        gf_fsid[2];
  pthread_t           gf_thread;
};
#else
#error FSAL_GPFS requires --enable-fsal-up
#endif

#undef Return
#define Return( _code_, _minor_ , _f_ ) do {                                   \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;          \
               (_struct_status_).major = (_code_) ;                            \
               (_struct_status_).minor = (_minor_) ;                           \
               fsal_increment_nbcall( _f_,_struct_status_ );                   \
               if(fsal_error_is_event(_struct_status_))                        \
                 {                                                             \
                   LogEvent(COMPONENT_FSAL,                                    \
                     "%s returns (%s, %s, %d)",fsal_function_names[_f_],       \
                     label_fsal_err(_code_), msg_fsal_err(_code_), _minor_);   \
                   return (_struct_status_);                                   \
                 }                                                             \
               if(fsal_error_is_info(_struct_status_))                        \
                 {                                                             \
                   LogInfo(COMPONENT_FSAL,                                    \
                     "%s returns (%s, %s, %d)",fsal_function_names[_f_],       \
                     label_fsal_err(_code_), msg_fsal_err(_code_), _minor_);   \
                   return (_struct_status_);                                   \
                 }                                                             \
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

/* defined the set of attributes supported with POSIX */
#define GPFS_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     |  FSAL_ATTR_FILEID  | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME | FSAL_ATTR_ACL  )

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

/* export_context_t is not given to every function, but
 * most functions need to use the open-by-handle funcionality.
 */

#endif

static inline void gpfs_healthcheck(int fd, int errsv)
{
  struct grace_period_arg gpa;

  if (errsv != ESTALE)
    return;
  gpa.mountdirfd = fd;
  if ((gpfs_ganesha(OPENHANDLE_GET_NODEID, &gpa) < 0) && errno == EUNATCH)
     LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
}

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        fs_specific_initinfo_t * fs_specific_info);

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
 * Gets a fd from a handle 
 */
fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_handle2fd_at(int dirfd,
                                         fsal_handle_t * phandle, int *pfd, int oflags);

/**
 * Gets a file handle from a parent handle and name
 */
fsal_status_t fsal_internal_get_fh(fsal_op_context_t * p_context,  /* IN */
                                   fsal_handle_t * p_dir_handle,   /* IN */
                                   fsal_name_t * p_fsalname,       /* IN */
                                   fsal_handle_t * p_handle);      /* OUT */
/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(fsal_op_context_t * p_context,
                                      fsal_handle_t * p_handle, char *__buf, int maxlen);

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_get_handle(fsal_op_context_t * p_context,   /* IN */
                                       fsal_path_t * p_fsalpath,        /* IN */
                                       fsal_handle_t * p_handle /* OUT */ );

fsal_status_t fsal_internal_get_handle_at(int dfd, fsal_name_t * p_fsalname, 
                                          fsal_handle_t * p_handle,
                                          fsal_op_context_t * p_context);


fsal_status_t fsal_internal_fd2handle(int fd,   /* IN */
                                      fsal_handle_t * p_handle  /* OUT */
    );

fsal_status_t fsal_internal_link_fh(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_target_handle,
                                    fsal_handle_t * p_dir_handle,
                                    fsal_name_t * p_link_name);

fsal_status_t fsal_internal_stat_name(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_dir_handle,
                                    fsal_name_t * p_stat_name,
                                    struct stat *buf);

fsal_status_t fsal_internal_unlink(fsal_op_context_t * p_context,
                                   fsal_handle_t * p_dir_handle,
                                   fsal_name_t * p_stat_name,
                                   struct stat *buf);

fsal_status_t fsal_internal_create(fsal_op_context_t * p_context,
                                   fsal_handle_t * p_dir_handle,
                                   fsal_name_t * p_stat_name,
                                   mode_t mode, dev_t dev,
                                   fsal_handle_t * p_new_handle,
                                   struct stat *buf);

fsal_status_t fsal_internal_rename_fh(fsal_op_context_t * p_context,
                                    fsal_handle_t * p_old_handle,
                                    fsal_handle_t * p_new_handle,
                                    fsal_name_t * p_old_name,
                                    fsal_name_t * p_new_name);

int fsal_internal_version(); 

fsal_status_t fsal_get_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t * p_handle, gpfsfsal_xstat_t *p_buffxstat);

fsal_status_t fsal_set_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t * p_handle, int attr_valid,
                                       int attr_changed, gpfsfsal_xstat_t *p_buffxstat);

fsal_status_t fsal_trucate_by_handle(fsal_op_context_t * p_context,
                                     fsal_handle_t * p_handle,
                                     u_int64_t size);

/* All the call to FSAL to be wrapped */

fsal_status_t GPFSFSAL_getattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t GPFSFSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,     /* IN */
                                           fsal_handle_t * p_filehandle,        /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t GPFSFSAL_setattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_attrib_set,       /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_BuildExportContext(fsal_export_context_t * p_export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options /* IN */ );

fsal_status_t GPFSFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context);

fsal_status_t GPFSFSAL_create(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,       /* IN */
                            fsal_name_t * p_dirname,    /* IN */
                            fsal_op_context_t * p_context,   /* IN */
                            fsal_accessmode_t accessmode,       /* IN */
                            fsal_handle_t * p_object_handle, /* OUT */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_link(fsal_handle_t * p_target_handle,  /* IN */
                           fsal_handle_t * p_dir_handle,     /* IN */
                           fsal_name_t * p_link_name,   /* IN */
                           fsal_op_context_t * p_context,    /* IN */
                           fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_mknode(fsal_handle_t * parentdir_handle,       /* IN */
                             fsal_name_t * p_node_name, /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_nodetype_t nodetype,  /* IN */
                             fsal_dev_t * dev,  /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT (handle to the created node) */
                             fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_opendir(fsal_handle_t * p_dir_handle,  /* IN */
                              fsal_op_context_t * p_context, /* IN */
                              fsal_dir_t * p_dir_descriptor, /* OUT */
                              fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_readdir(fsal_dir_t * p_dir_descriptor, /* IN */
                              fsal_op_context_t * p_context,       /* IN */
                              fsal_cookie_t start_position,  /* IN */
                              fsal_attrib_mask_t get_attr_mask, /* IN */
                              fsal_mdsize_t buffersize, /* IN */
                              fsal_dirent_t * p_pdirent,        /* OUT */
                              fsal_cookie_t * p_end_position,        /* OUT */
                              fsal_count_t * p_nb_entries,      /* OUT */
                              fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t GPFSFSAL_closedir(fsal_dir_t * p_dir_descriptor, /* IN */
                                fsal_op_context_t * p_context  /* IN */ );

fsal_status_t GPFSFSAL_open_by_name(fsal_handle_t * dirhandle,        /* IN */
                                   fsal_name_t * filename,      /* IN */
                                   fsal_op_context_t * p_context,    /* IN */
                                   fsal_openflags_t openflags,  /* IN */
                                   fsal_file_t * file_descriptor,    /* OUT */
                                   fsal_attrib_list_t *
                                   file_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_open(fsal_handle_t * p_filehandle,     /* IN */
                           fsal_op_context_t * p_context,    /* IN */
                           fsal_openflags_t openflags,  /* IN */
                           fsal_file_t * p_file_descriptor,  /* OUT */
                           fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_read(fsal_file_t * p_file_descriptor,  /* IN */
                           fsal_op_context_t * p_context,     /* IN */
                           fsal_seek_t * p_seek_descriptor,     /* [IN] */
                           fsal_size_t buffer_size,     /* IN */
                           caddr_t buffer,      /* OUT */
                           fsal_size_t * p_read_amount, /* OUT */
                           fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t GPFSFSAL_write(fsal_file_t * p_file_descriptor, /* IN */
                            fsal_op_context_t * p_context,     /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* IN */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* IN */
                            fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t GPFSFSAL_close(fsal_file_t * p_file_descriptor, /* IN */
                             fsal_op_context_t * p_context    /* IN */ );

fsal_status_t GPFSFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,   /* IN */
                                     fsal_op_context_t * p_context,  /* IN */
                                     fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t GPFSFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t GPFSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lookupPath(fsal_path_t * p_path,  /* IN */
                                 fsal_op_context_t * p_context,      /* IN */
                                 fsal_handle_t * object_handle,      /* OUT */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,      /* IN */
                                     fsal_op_context_t * p_context,  /* IN */
                                     fsal_handle_t * p_fsoot_handle, /* OUT */
                                     fsal_attrib_list_t *
                                     p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lock_op( fsal_file_t           * p_file_descriptor,   /* IN */
                                fsal_handle_t         * p_filehandle,        /* IN */
                                fsal_op_context_t     * p_context,           /* IN */
                                void                  * p_owner,             /* IN */
                                fsal_lock_op_t          lock_op,             /* IN */
                                fsal_lock_param_t       request_lock,        /* IN */
                                fsal_lock_param_t     * conflicting_lock     /* OUT */ );

fsal_status_t GPFSFSAL_share_op( fsal_file_t          * p_file_descriptor,   /* IN */
                                 fsal_handle_t        * p_filehandle,        /* IN */
                                 fsal_op_context_t    * p_context,           /* IN */
                                 void                 * p_owner,             /* IN */
                                 fsal_share_param_t     request_share        /* IN */ );

fsal_status_t
GPFSFSAL_start_grace(fsal_op_context_t *p_context,              /* IN */
                                   int  grace_period);          /* IN */

fsal_status_t GPFSFSAL_rcp(fsal_handle_t * filehandle,        /* IN */
                          fsal_op_context_t * p_context,     /* IN */
                          fsal_path_t * p_local_path,   /* IN */
                          fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t GPFSFSAL_rename(fsal_handle_t * p_old_parentdir_handle, /* IN */
                             fsal_name_t * p_old_name,  /* IN */
                             fsal_handle_t * p_new_parentdir_handle, /* IN */
                             fsal_name_t * p_new_name,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t * p_src_dir_attributes, /* [ IN/OUT ] */
                             fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ );

void GPFSFSAL_get_stats(fsal_statistics_t * stats,       /* OUT */
                       fsal_boolean_t reset /* IN */ );

fsal_status_t GPFSFSAL_readlink(fsal_handle_t * p_linkhandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_path_t * p_link_content,    /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_symlink(fsal_handle_t * p_parent_directory_handle,     /* IN */
                              fsal_name_t * p_linkname, /* IN */
                              fsal_path_t * p_linkcontent,      /* IN */
                              fsal_op_context_t * p_context, /* IN */
                              fsal_accessmode_t accessmode,     /* IN (ignored) */
                              fsal_handle_t * p_link_handle, /* OUT */
                              fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int GPFSFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                      fsal_status_t * status);

unsigned int GPFSFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                         unsigned int cookie,
                                         unsigned int alphabet_len,
                                         unsigned int index_size);

unsigned int GPFSFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, unsigned int cookie);

fsal_status_t GPFSFSAL_DigestHandle(fsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t output_type,       /* IN */
                                   fsal_handle_t * p_in_fsal_handle, /* IN */
                                   struct fsal_handle_desc *fh_desc /* OUT */ );

fsal_status_t GPFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext, /*IN*/
                                   fsal_digesttype_t in_type,   /* IN */
                                   struct fsal_handle_desc *fh_desc /*IN OUT*/);

fsal_status_t GPFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t GPFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t GPFSFSAL_truncate(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_size_t length,      /* IN */
                               fsal_file_t * file_descriptor,        /* Unused in this FSAL */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_unlink(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_object_name,       /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t *
                             p_parent_directory_attributes /* [IN/OUT ] */ );

char *GPFSFSAL_GetFSName();

fsal_status_t GPFSFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,  /* IN */
                                    fsal_op_context_t * p_context,   /* IN */
                                    unsigned int xattr_id,      /* IN */
                                    fsal_attrib_list_t * p_attrs);

fsal_status_t GPFSFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,     /* IN */
                                 unsigned int cookie,   /* IN */
                                 fsal_op_context_t * p_context,      /* IN */
                                 fsal_xattrent_t * xattrs_tab,  /* IN/OUT */
                                 unsigned int xattrs_tabsize,   /* IN */
                                 unsigned int *p_nb_returned,   /* OUT */
                                 int *end_of_list /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN/OUT */
                                        size_t buffer_size,     /* IN */
                                        size_t * p_output_size /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,       /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,        /* IN */
                                       unsigned int *pxattr_id /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,    /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          fsal_op_context_t * p_context,     /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t GPFSFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,  /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * p_context,   /* IN */
                                    caddr_t buffer_addr,        /* IN */
                                    size_t buffer_size, /* IN */
                                    int create /* IN */ );

fsal_status_t GPFSFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN */
                                        size_t buffer_size /* IN */ );

fsal_status_t GPFSFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,        /* IN */
                                      fsal_op_context_t * p_context, /* IN */
                                      unsigned int xattr_id) /* IN */ ;

fsal_status_t GPFSFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,      /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        const fsal_name_t * xattr_name) /* IN */ ;

int GPFSFSAL_GetXattrOffsetSetable( void ) ;

unsigned int GPFSFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t GPFSFSAL_commit( fsal_file_t * p_file_descriptor,
                             fsal_op_context_t * p_context,        /* IN */
                             fsal_off_t    offset,
                             fsal_size_t   size ) ;


#ifdef _USE_FSAL_UP
fsal_status_t GPFSFSAL_UP_Init( fsal_up_event_bus_parameter_t * pebparam,      /* IN */
				fsal_up_event_bus_context_t * pupebcontext     /* OUT */);
fsal_status_t GPFSFSAL_UP_AddFilter( fsal_up_event_bus_filter_t * pupebfilter,  /* IN */
				     fsal_up_event_bus_context_t * pupebcontext /* INOUT */ );
fsal_status_t GPFSFSAL_UP_GetEvents( struct glist_head * pevent_head,
				     fsal_count_t * event_nb,                     /* IN */
				     fsal_time_t timeout,                         /* IN */
				     fsal_count_t * peventfound,                  /* OUT */
				     fsal_up_event_bus_context_t * pupebcontext   /* IN */ );

void *GPFSFSAL_UP_Thread(void *Arg);

struct glist_head gpfs_fsal_up_ctx_list;

gpfs_fsal_up_ctx_t * gpfsfsal_find_fsal_up_context(gpfsfsal_export_context_t * export_ctx);

#endif /* _USE_FSAL_UP */
