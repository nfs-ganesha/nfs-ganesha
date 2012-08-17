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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  
 * USA
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

#include "nfs_core.h"
#include "nfs_exports.h"
#include  "fsal.h"
#include <sys/stat.h>
#include "fsal_up.h"
#include "FSAL/common_functions.h"

/* defined the set of attributes supported with POSIX */
#define PTFS_SUPPORTED_ATTRIBUTES (                                       \
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

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t 
fsal_internal_init_global(fsal_init_info_t       * fsal_info,
                          fs_common_initinfo_t   * fs_common_info,
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
 * Gets a fd from a handle 
 */
fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t     * phandle, 
                                      int               * pfd, 
                                      int                 oflags);

fsal_status_t fsal_internal_handle2fd_at(fsal_op_context_t * p_context, 
                                         int                 dirfd,
                                         fsal_handle_t     * phandle, 
                                         int               * pfd, 
                                         int                 oflags);

/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(fsal_op_context_t * p_context,
                                      fsal_handle_t     * p_handle, 
                                      char              * __buf, 
                                      int                 maxlen);

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_get_handle(fsal_op_context_t * p_context, /* IN */
                                       fsal_path_t       * p_fsalpath,/* IN */
                                       fsal_handle_t     * p_handle /* OUT */);

fsal_status_t 
fsal_internal_get_handle_at(fsal_op_context_t    * p_context,
                            int dfd, fsal_name_t * p_fsalname, /* IN */
                            fsal_handle_t        * p_handle    /* OUT */ );

fsal_status_t 
fsal_internal_fd2handle(fsal_op_context_t * p_context, 
                        int                 fd,       /* IN */
                        fsal_handle_t     * p_handle  /* OUT */);

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

/**
 *  test the access to a file from its POSIX attributes (struct stat) OR 
 * its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t 
fsal_internal_testAccess(fsal_op_context_t  * p_context,   /*IN*/
                         fsal_accessflags_t   access_type, /*IN*/
                         struct stat        * p_buffstat,  /*IN, optional*/
                         fsal_attrib_list_t * 
                         p_object_attributes /*IN, optional*/ );

fsal_status_t 
fsal_internal_access(fsal_op_context_t  * p_context,    /* IN */
                     fsal_handle_t      * p_handle,     /* IN */
                     fsal_accessflags_t   access_type,  /* IN */
                     fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t fsal_stat_by_handle(fsal_op_context_t * p_context,
                                  fsal_handle_t     * p_handle, 
                                  struct stat64     * buf);

fsal_status_t fsal_get_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t     * p_handle, 
                                       ptfsal_xstat_t    * p_buffxstat);

fsal_status_t fsal_set_xstat_by_handle(fsal_op_context_t * p_context,
                                       fsal_handle_t     * p_handle, 
                                       int                 attr_valid,
                                       int                 attr_changed, 
                                       ptfsal_xstat_t    * p_buffxstat);

fsal_status_t fsal_check_access_by_mode(fsal_op_context_t * p_context,  /*IN*/
                                        fsal_accessflags_t  access_type,/*IN*/
                                        struct stat64     * p_buffstat  /*IN*/);


/* All the call to FSAL to be wrapped */
fsal_status_t PTFSAL_access(fsal_handle_t       * p_object_handle, /* IN */
                             fsal_op_context_t  * p_context,       /* IN */
                             fsal_accessflags_t   access_type,     /* IN */
                             fsal_attrib_list_t * 
                             p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PTFSAL_getattrs(fsal_handle_t      * p_filehandle, /* IN */
                              fsal_op_context_t  * p_context,    /* IN */
                              fsal_attrib_list_t * 
                              p_object_attributes /* IN/OUT */ );

fsal_status_t 
PTFSAL_getattrs_descriptor(fsal_file_t        * p_file_descriptor,     /* IN */
                           fsal_handle_t      * p_filehandle,          /* IN */
                           fsal_op_context_t  * p_context,             /* IN */
                           fsal_attrib_list_t * 
                           p_object_attributes /* IN/OUT */ );

fsal_status_t PTFSAL_setattrs(fsal_handle_t       * p_filehandle, /* IN */
                               fsal_op_context_t  * p_context,    /* IN */
                               fsal_attrib_list_t * p_attrib_set, /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_BuildExportContext(fsal_export_context_t * p_export_context,  /* OUT */
                          fsal_path_t           * p_export_path,     /* IN */
                          char *                  fs_specific_options/* IN */);

fsal_status_t 
PTFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context);

fsal_status_t 
PTFSAL_create(fsal_handle_t      * p_parent_directory_handle,/*IN*/
              fsal_name_t        * p_filename,         /* IN */
              fsal_op_context_t  * p_context,          /* IN */
              fsal_accessmode_t    accessmode,         /* IN */
              fsal_handle_t      * p_object_handle,    /* OUT */
              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PTFSAL_mkdir(fsal_handle_t      * p_parent_directory_handle,/*IN*/
                           fsal_name_t        * p_dirname,        /* IN */
                           fsal_op_context_t  * p_context,        /* IN */
                           fsal_accessmode_t    accessmode,       /* IN */
                           fsal_handle_t      * p_object_handle,  /* OUT */
                           fsal_attrib_list_t * 
                           p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PTFSAL_link(fsal_handle_t      * p_target_handle,  /* IN */
                          fsal_handle_t      * p_dir_handle,     /* IN */
                          fsal_name_t        * p_link_name, /* IN */
                          fsal_op_context_t  * p_context,   /* IN */
                          fsal_attrib_list_t * p_attributes /*[ IN/OUT ]*/ );

fsal_status_t PTFSAL_mknode(fsal_handle_t      * parentdir_handle, /* IN */
                            fsal_name_t        * p_node_name,      /* IN */
                            fsal_op_context_t  * p_context,        /* IN */
                            fsal_accessmode_t    accessmode,       /* IN */
                            fsal_nodetype_t      nodetype,         /* IN */
                            fsal_dev_t         * dev,              /* IN */
                            fsal_handle_t      * 
                            p_object_handle,       /* OUT (created node) */
                            fsal_attrib_list_t * 
                            node_attributes        /* [ IN/OUT ] */ );

fsal_status_t PTFSAL_opendir(fsal_handle_t      * p_dir_handle,     /* IN */
                             fsal_op_context_t  * p_context,        /* IN */
                             fsal_dir_t         * p_dir_descriptor, /* OUT */
                             fsal_attrib_list_t * 
                             p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t PTFSAL_readdir(fsal_dir_t        * p_dir_descriptor,/* IN */
                             fsal_cookie_t       start_position,  /* IN */
                             fsal_attrib_mask_t  get_attr_mask,   /* IN */
                             fsal_mdsize_t       buffersize,      /* IN */
                             fsal_dirent_t     * p_pdirent,       /* OUT */
                             fsal_cookie_t     * p_end_position,  /* OUT */
                             fsal_count_t      * p_nb_entries,    /* OUT */
                             fsal_boolean_t    * p_end_of_dir     /* OUT */ );

fsal_status_t PTFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t 
PTFSAL_open_by_name(fsal_handle_t      * dirhandle,      /* IN */
                    fsal_name_t        * filename,       /* IN */
                    fsal_op_context_t  * p_context,      /* IN */
                    fsal_openflags_t     openflags,      /* IN */
                    fsal_file_t        * file_descriptor,/* OUT */
                    fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_open(fsal_handle_t      * p_filehandle,       /* IN */
            fsal_op_context_t  * p_context,          /* IN */
            fsal_openflags_t     openflags,          /* IN */
            fsal_file_t        * p_file_descriptor,  /* OUT */
            fsal_attrib_list_t * 
            p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_read(fsal_file_t    * p_file_descriptor,  /* IN */
            fsal_seek_t    * p_seek_descriptor,  /* [IN] */
            fsal_size_t      buffer_size,        /* IN */
            caddr_t          buffer,             /* OUT */
            fsal_size_t    * p_read_amount,      /* OUT */
            fsal_boolean_t * p_end_of_file       /* OUT */ );

fsal_status_t PTFSAL_write(fsal_file_t * p_file_descriptor, /* IN */
                           fsal_op_context_t * p_context,   /* IN */
                           fsal_seek_t * p_seek_descriptor, /* IN */
                           fsal_size_t   buffer_size,     /* IN */
                           caddr_t       buffer,          /* IN */
                           fsal_size_t * p_write_amount   /* OUT */ );

fsal_status_t PTFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t 
PTFSAL_dynamic_fsinfo(fsal_handle_t        * p_filehandle, /* IN */
                      fsal_op_context_t    * p_context,    /* IN */
                      fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t PTFSAL_Init(fsal_parameter_t * init_info /* IN */ );
fsal_status_t PTFSAL_terminate();

fsal_status_t 
PTFSAL_test_access(fsal_op_context_t  * p_context,          /* IN */
                   fsal_accessflags_t   access_type,        /* IN */
                   fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t 
PTFSAL_lookup(fsal_handle_t      * p_parent_directory_handle,/* IN */
              fsal_name_t        * p_filename,               /* IN */
              fsal_op_context_t  * p_context,                /* IN */
              fsal_handle_t      * p_object_handle,          /* OUT */
              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_lookupPath(fsal_path_t        * p_path,             /* IN */
                  fsal_op_context_t  * p_context,          /* IN */
                  fsal_handle_t      * object_handle,      /* OUT */
                  fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_lookupJunction(fsal_handle_t      * p_junction_handle,  /*IN*/
                      fsal_op_context_t  * p_context,          /*IN*/
                      fsal_handle_t      * p_fsoot_handle,     /*OUT*/
                      fsal_attrib_list_t * p_fsroot_attributes /*[ IN/OUT ]*/ );

fsal_status_t 
PTFSAL_lock_op(fsal_file_t       * p_file_descriptor, /*IN*/
               fsal_handle_t     * p_filehandle,      /*IN*/
               fsal_op_context_t * p_context,         /*IN*/
               void              * p_owner,           /*IN*/
               fsal_lock_op_t      lock_op,           /*IN*/
               fsal_lock_param_t   request_lock,      /*IN*/
               fsal_lock_param_t * conflicting_lock   /* OUT */ );

fsal_status_t PTFSAL_rcp(fsal_handle_t     * filehandle,    /* IN */
                         fsal_op_context_t * p_context,     /* IN */
                         fsal_path_t       * p_local_path,  /* IN */
                         fsal_rcpflag_t      transfer_opt   /* IN */ );

fsal_status_t 
PTFSAL_rename(fsal_handle_t      * p_old_parentdir_handle, /* IN */
              fsal_name_t        * p_old_name,             /* IN */
              fsal_handle_t      * p_new_parentdir_handle, /* IN */
              fsal_name_t        * p_new_name,           /* IN */
              fsal_op_context_t  * p_context,            /* IN */
              fsal_attrib_list_t * p_src_dir_attributes, /* [ IN/OUT ] */
              fsal_attrib_list_t * p_tgt_dir_attributes  /* [ IN/OUT ] */ );

void PTFSAL_get_stats(fsal_statistics_t * stats, /* OUT */
                      fsal_boolean_t reset       /* IN */ );

fsal_status_t 
PTFSAL_readlink(fsal_handle_t      * p_linkhandle,     /* IN */
                fsal_op_context_t  * p_context,        /* IN */
                fsal_path_t        * p_link_content,   /* OUT */
                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_symlink(fsal_handle_t      * p_parent_directory_handle, /*IN*/
               fsal_name_t        * p_linkname,       /* IN */
               fsal_path_t        * p_linkcontent,    /* IN */
               fsal_op_context_t  * p_context,        /* IN */
               fsal_accessmode_t    accessmode,       /* IN (ignored)*/
               fsal_handle_t      * p_link_handle,    /* OUT */
               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int PTFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                     fsal_status_t * status);

unsigned int PTFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                        unsigned int    cookie,
                                        unsigned int    alphabet_len,
                                        unsigned int    index_size);

unsigned int PTFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, 
                                       unsigned int cookie);

fsal_status_t 
PTFSAL_DigestHandle(fsal_export_context_t   * p_expcontext,     /* IN */
                    fsal_digesttype_t         output_type,      /* IN */
                    fsal_handle_t           * p_in_fsal_handle, /* IN */
                    struct fsal_handle_desc * fh_desc           /* OUT */ );

fsal_status_t 
PTFSAL_ExpandHandle(fsal_export_context_t   * p_expcontext, /* IN */
                    fsal_digesttype_t         in_type,      /* IN */
                    struct fsal_handle_desc * fh_desc);

fsal_status_t 
PTFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t 
PTFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                            fsal_parameter_t * out_parameter);

fsal_status_t 
PTFSAL_truncate(fsal_handle_t      * p_filehandle,   /* IN */
                fsal_op_context_t  * p_context,      /* IN */
                fsal_size_t          length,         /* IN */
                fsal_file_t        * file_descriptor,/* Unused in this FSAL */
                fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t 
PTFSAL_unlink(fsal_handle_t * p_parent_directory_handle,      /* IN */
              fsal_name_t * p_object_name,    /* IN */
              fsal_op_context_t * p_context,  /* IN */
              fsal_attrib_list_t *
              p_parent_directory_attributes   /* [IN/OUT ] */ );

char *PTFSAL_GetFSName();

fsal_status_t 
PTFSAL_GetXAttrAttrs(fsal_handle_t      * p_objecthandle,  /* IN */
                     fsal_op_context_t  * p_context,       /* IN */
                     unsigned int         xattr_id,        /* IN */
                     fsal_attrib_list_t * p_attrs);

fsal_status_t 
PTFSAL_ListXAttrs(fsal_handle_t     * p_objecthandle,  /* IN */
                  unsigned int        cookie,          /* IN */
                  fsal_op_context_t * p_context,       /* IN */
                  fsal_xattrent_t   * xattrs_tab,      /* IN/OUT */
                  unsigned int        xattrs_tabsize,  /* IN */
                  unsigned int      * p_nb_returned,   /* OUT */
                  int               * end_of_list      /* OUT */ );

fsal_status_t 
PTFSAL_GetXAttrValueById(fsal_handle_t     * p_objecthandle, /* IN */
                         unsigned int        xattr_id,       /* IN */
                         fsal_op_context_t * p_context,      /* IN */
                         caddr_t             buffer_addr,    /* IN/OUT */
                         size_t              buffer_size,    /* IN */
                         size_t            * p_output_size   /* OUT */ );

fsal_status_t 
PTFSAL_GetXAttrIdByName(fsal_handle_t     * p_objecthandle, /* IN */
                        const fsal_name_t * xattr_name,/* IN */
                        fsal_op_context_t * p_context, /* IN */
                        unsigned int      * pxattr_id  /* OUT */ );

fsal_status_t 
PTFSAL_GetXAttrValueByName(fsal_handle_t     * p_objecthandle,/*IN*/
                           const fsal_name_t * xattr_name,    /*IN*/
                           fsal_op_context_t * p_context,     /* IN */
                           caddr_t             buffer_addr,   /* IN/OUT */
                           size_t              buffer_size,   /* IN */
                           size_t            * p_output_size  /* OUT */ );

fsal_status_t 
PTFSAL_SetXAttrValue(fsal_handle_t     * p_objecthandle,  /* IN */
                     const fsal_name_t * xattr_name,  /* IN */
                     fsal_op_context_t * p_context,   /* IN */
                     caddr_t             buffer_addr, /* IN */
                     size_t              buffer_size, /* IN */
                     int                 create       /* IN */ );

fsal_status_t 
PTFSAL_SetXAttrValueById(fsal_handle_t     * p_objecthandle, /* IN */
                         unsigned int        xattr_id,       /* IN */
                         fsal_op_context_t * p_context,      /* IN */
                         caddr_t             buffer_addr,    /* IN */
                         size_t              buffer_size     /* IN */ );

fsal_status_t 
PTFSAL_RemoveXAttrById(fsal_handle_t     * p_objecthandle, /* IN */
                       fsal_op_context_t * p_context,      /* IN */
                       unsigned int        xattr_id)       /* IN */ ;

fsal_status_t 
PTFSAL_RemoveXAttrByName(fsal_handle_t     * p_objecthandle, /*IN*/
                         fsal_op_context_t * p_context,   /* IN */
                         const fsal_name_t * xattr_name)  /* IN */;

unsigned int PTFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t PTFSAL_commit( fsal_file_t * p_file_descriptor,
                             fsal_off_t    offset,
                             fsal_size_t   size ) ;

fsal_status_t PTFSAL_GetExportEntry(char * path,                 /* IN */ 
                                    exportlist_t ** p_exportlist) /* OUT */ ;

fsal_status_t PTFSAL_GetMountRootFD(fsal_op_context_t * p_context);
