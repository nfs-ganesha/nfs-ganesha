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

#include  "fsal.h"
#include <sys/stat.h>

/* defined the set of attributes supported with POSIX */
#define GPFS_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     |  FSAL_ATTR_FILEID  | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME  )

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

/* export_context_t is not given to every function, but
 * most functions need to use the open-by-handle funcionality.
 */
extern char open_by_handle_path[MAXPATHLEN];
extern int open_by_handle_fd;

#endif

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

fsal_status_t fsal_internal_get_handle_at(int dfd, fsal_name_t * p_fsalname,    /* IN */
                                          fsal_handle_t * p_handle /* OUT */ );
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

fsal_status_t fsal_internal_get_handle_at(int dfd, fsal_name_t * p_fsalname,    /* IN */
                                          fsal_handle_t * p_handle /* OUT */ );

fsal_status_t fsal_internal_fd2handle(int fd,   /* IN */
                                      fsal_handle_t * p_handle  /* OUT */
    );

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

/**
 *  test the access to a file from its POSIX attributes (struct stat) OR its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t fsal_internal_testAccess(fsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat *p_buffstat, /* IN, optional */
                                       fsal_attrib_list_t *
                                       p_object_attributes /* IN, optional */ );



/* All the call to FSAL to be wrapped */
fsal_status_t GPFSFSAL_access(gpfsfsal_handle_t * p_object_handle,        /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessflags_t access_type,    /* IN */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_getattrs(gpfsfsal_handle_t * p_filehandle, /* IN */
                               gpfsfsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t GPFSFSAL_setattrs(gpfsfsal_handle_t * p_filehandle, /* IN */
                               gpfsfsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_attrib_set,       /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_BuildExportContext(gpfsfsal_export_context_t * p_export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options /* IN */ );

fsal_status_t GPFSFSAL_CleanUpExportContext(gpfsfsal_export_context_t * p_export_context);

fsal_status_t GPFSFSAL_InitClientContext(gpfsfsal_op_context_t * p_thr_context);

fsal_status_t GPFSFSAL_GetClientContext(gpfsfsal_op_context_t * p_thr_context,    /* IN/OUT  */
                                       gpfsfsal_export_context_t * p_export_context,     /* IN */
                                       fsal_uid_t uid,  /* IN */
                                       fsal_gid_t gid,  /* IN */
                                       fsal_gid_t * alt_groups, /* IN */
                                       fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t GPFSFSAL_create(gpfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             gpfsfsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_mkdir(gpfsfsal_handle_t * p_parent_directory_handle,       /* IN */
                            fsal_name_t * p_dirname,    /* IN */
                            gpfsfsal_op_context_t * p_context,   /* IN */
                            fsal_accessmode_t accessmode,       /* IN */
                            gpfsfsal_handle_t * p_object_handle, /* OUT */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_link(gpfsfsal_handle_t * p_target_handle,  /* IN */
                           gpfsfsal_handle_t * p_dir_handle,     /* IN */
                           fsal_name_t * p_link_name,   /* IN */
                           gpfsfsal_op_context_t * p_context,    /* IN */
                           fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_mknode(gpfsfsal_handle_t * parentdir_handle,       /* IN */
                             fsal_name_t * p_node_name, /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_nodetype_t nodetype,  /* IN */
                             fsal_dev_t * dev,  /* IN */
                             gpfsfsal_handle_t * p_object_handle,        /* OUT (handle to the created node) */
                             fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_opendir(gpfsfsal_handle_t * p_dir_handle,  /* IN */
                              gpfsfsal_op_context_t * p_context, /* IN */
                              gpfsfsal_dir_t * p_dir_descriptor, /* OUT */
                              fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_readdir(gpfsfsal_dir_t * p_dir_descriptor, /* IN */
                              gpfsfsal_cookie_t start_position,  /* IN */
                              fsal_attrib_mask_t get_attr_mask, /* IN */
                              fsal_mdsize_t buffersize, /* IN */
                              fsal_dirent_t * p_pdirent,        /* OUT */
                              gpfsfsal_cookie_t * p_end_position,        /* OUT */
                              fsal_count_t * p_nb_entries,      /* OUT */
                              fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t GPFSFSAL_closedir(gpfsfsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t GPFSFSAL_open_by_name(gpfsfsal_handle_t * dirhandle,        /* IN */
                                   fsal_name_t * filename,      /* IN */
                                   gpfsfsal_op_context_t * p_context,    /* IN */
                                   fsal_openflags_t openflags,  /* IN */
                                   gpfsfsal_file_t * file_descriptor,    /* OUT */
                                   fsal_attrib_list_t *
                                   file_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_open(gpfsfsal_handle_t * p_filehandle,     /* IN */
                           gpfsfsal_op_context_t * p_context,    /* IN */
                           fsal_openflags_t openflags,  /* IN */
                           gpfsfsal_file_t * p_file_descriptor,  /* OUT */
                           fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_read(gpfsfsal_file_t * p_file_descriptor,  /* IN */
                           fsal_seek_t * p_seek_descriptor,     /* [IN] */
                           fsal_size_t buffer_size,     /* IN */
                           caddr_t buffer,      /* OUT */
                           fsal_size_t * p_read_amount, /* OUT */
                           fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t GPFSFSAL_write(gpfsfsal_file_t * p_file_descriptor, /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* IN */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* IN */
                            fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t GPFSFSAL_close(gpfsfsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t GPFSFSAL_open_by_fileid(gpfsfsal_handle_t * filehandle,     /* IN */
                                     fsal_u64_t fileid, /* IN */
                                     gpfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     gpfsfsal_file_t * file_descriptor,  /* OUT */
                                     fsal_attrib_list_t *
                                     file_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_close_by_fileid(gpfsfsal_file_t * file_descriptor /* IN */ ,
                                      fsal_u64_t fileid);

fsal_status_t GPFSFSAL_static_fsinfo(gpfsfsal_handle_t * p_filehandle,    /* IN */
                                    gpfsfsal_op_context_t * p_context,   /* IN */
                                    fsal_staticfsinfo_t * p_staticinfo /* OUT */ );

fsal_status_t GPFSFSAL_dynamic_fsinfo(gpfsfsal_handle_t * p_filehandle,   /* IN */
                                     gpfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t GPFSFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t GPFSFSAL_terminate();

fsal_status_t GPFSFSAL_test_access(gpfsfsal_op_context_t * p_context,     /* IN */
                                  fsal_accessflags_t access_type,       /* IN */
                                  fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t GPFSFSAL_setattr_access(gpfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_attrib_list_t * candidate_attributes, /* IN */
                                     fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t GPFSFSAL_rename_access(gpfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattrsrc,      /* IN */
                                    fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t GPFSFSAL_create_access(gpfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t GPFSFSAL_unlink_access(gpfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t GPFSFSAL_link_access(gpfsfsal_op_context_t * pcontext,      /* IN */
                                  fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t GPFSFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                  fsal_attrib_list_t * pnew_attr,
                                  fsal_attrib_list_t * presult_attr);

fsal_status_t GPFSFSAL_lookup(gpfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             gpfsfsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lookupPath(fsal_path_t * p_path,  /* IN */
                                 gpfsfsal_op_context_t * p_context,      /* IN */
                                 gpfsfsal_handle_t * object_handle,      /* OUT */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lookupJunction(gpfsfsal_handle_t * p_junction_handle,      /* IN */
                                     gpfsfsal_op_context_t * p_context,  /* IN */
                                     gpfsfsal_handle_t * p_fsoot_handle, /* OUT */
                                     fsal_attrib_list_t *
                                     p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_lock(gpfsfsal_file_t * obj_handle,
                           gpfsfsal_lockdesc_t * ldesc, fsal_boolean_t blocking);

fsal_status_t GPFSFSAL_changelock(gpfsfsal_lockdesc_t * lock_descriptor,  /* IN / OUT */
                                 fsal_lockparam_t * lock_info /* IN */ );

fsal_status_t GPFSFSAL_unlock(gpfsfsal_file_t * obj_handle, gpfsfsal_lockdesc_t * ldesc);

fsal_status_t GPFSFSAL_getlock(gpfsfsal_file_t * obj_handle, gpfsfsal_lockdesc_t * ldesc);

fsal_status_t GPFSFSAL_CleanObjectResources(gpfsfsal_handle_t * in_fsal_handle);

fsal_status_t GPFSFSAL_set_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota,  /* IN */
                                fsal_quota_t * presquota);      /* OUT */

fsal_status_t GPFSFSAL_get_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota); /* OUT */

fsal_status_t GPFSFSAL_rcp(gpfsfsal_handle_t * filehandle,        /* IN */
                          gpfsfsal_op_context_t * p_context,     /* IN */
                          fsal_path_t * p_local_path,   /* IN */
                          fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t GPFSFSAL_rcp_by_fileid(gpfsfsal_handle_t * filehandle,      /* IN */
                                    fsal_u64_t fileid,  /* IN */
                                    gpfsfsal_op_context_t * p_context,   /* IN */
                                    fsal_path_t * p_local_path, /* IN */
                                    fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t GPFSFSAL_rename(gpfsfsal_handle_t * p_old_parentdir_handle, /* IN */
                             fsal_name_t * p_old_name,  /* IN */
                             gpfsfsal_handle_t * p_new_parentdir_handle, /* IN */
                             fsal_name_t * p_new_name,  /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t * p_src_dir_attributes, /* [ IN/OUT ] */
                             fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ );

void GPFSFSAL_get_stats(fsal_statistics_t * stats,       /* OUT */
                       fsal_boolean_t reset /* IN */ );

fsal_status_t GPFSFSAL_readlink(gpfsfsal_handle_t * p_linkhandle, /* IN */
                               gpfsfsal_op_context_t * p_context,        /* IN */
                               fsal_path_t * p_link_content,    /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_symlink(gpfsfsal_handle_t * p_parent_directory_handle,     /* IN */
                              fsal_name_t * p_linkname, /* IN */
                              fsal_path_t * p_linkcontent,      /* IN */
                              gpfsfsal_op_context_t * p_context, /* IN */
                              fsal_accessmode_t accessmode,     /* IN (ignored) */
                              gpfsfsal_handle_t * p_link_handle, /* OUT */
                              fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int GPFSFSAL_handlecmp(gpfsfsal_handle_t * handle1, gpfsfsal_handle_t * handle2,
                      fsal_status_t * status);

unsigned int GPFSFSAL_Handle_to_HashIndex(gpfsfsal_handle_t * p_handle,
                                         unsigned int cookie,
                                         unsigned int alphabet_len,
                                         unsigned int index_size);

unsigned int GPFSFSAL_Handle_to_RBTIndex(gpfsfsal_handle_t * p_handle, unsigned int cookie);

fsal_status_t GPFSFSAL_DigestHandle(gpfsfsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t output_type,       /* IN */
                                   gpfsfsal_handle_t * p_in_fsal_handle, /* IN */
                                   caddr_t out_buff /* OUT */ );

fsal_status_t GPFSFSAL_ExpandHandle(gpfsfsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t in_type,   /* IN */
                                   caddr_t in_buff,     /* IN */
                                   gpfsfsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t GPFSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t GPFSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t GPFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t GPFSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                    fsal_parameter_t * out_parameter);

fsal_status_t GPFSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                         fsal_parameter_t *
                                                         out_parameter);

fsal_status_t GPFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t GPFSFSAL_truncate(gpfsfsal_handle_t * p_filehandle, /* IN */
                               gpfsfsal_op_context_t * p_context,        /* IN */
                               fsal_size_t length,      /* IN */
                               gpfsfsal_file_t * file_descriptor,        /* Unused in this FSAL */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t GPFSFSAL_unlink(gpfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_object_name,       /* IN */
                             gpfsfsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t *
                             p_parent_directory_attributes /* [IN/OUT ] */ );

char *GPFSFSAL_GetFSName();

fsal_status_t GPFSFSAL_GetXAttrAttrs(gpfsfsal_handle_t * p_objecthandle,  /* IN */
                                    gpfsfsal_op_context_t * p_context,   /* IN */
                                    unsigned int xattr_id,      /* IN */
                                    fsal_attrib_list_t * p_attrs);

fsal_status_t GPFSFSAL_ListXAttrs(gpfsfsal_handle_t * p_objecthandle,     /* IN */
                                 unsigned int cookie,   /* IN */
                                 gpfsfsal_op_context_t * p_context,      /* IN */
                                 fsal_xattrent_t * xattrs_tab,  /* IN/OUT */
                                 unsigned int xattrs_tabsize,   /* IN */
                                 unsigned int *p_nb_returned,   /* OUT */
                                 int *end_of_list /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrValueById(gpfsfsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        gpfsfsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN/OUT */
                                        size_t buffer_size,     /* IN */
                                        size_t * p_output_size /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrIdByName(gpfsfsal_handle_t * p_objecthandle,       /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       gpfsfsal_op_context_t * p_context,        /* IN */
                                       unsigned int *pxattr_id /* OUT */ );

fsal_status_t GPFSFSAL_GetXAttrValueByName(gpfsfsal_handle_t * p_objecthandle,    /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          gpfsfsal_op_context_t * p_context,     /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t GPFSFSAL_SetXAttrValue(gpfsfsal_handle_t * p_objecthandle,  /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    gpfsfsal_op_context_t * p_context,   /* IN */
                                    caddr_t buffer_addr,        /* IN */
                                    size_t buffer_size, /* IN */
                                    int create /* IN */ );

fsal_status_t GPFSFSAL_SetXAttrValueById(gpfsfsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        gpfsfsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN */
                                        size_t buffer_size /* IN */ );

fsal_status_t GPFSFSAL_RemoveXAttrById(gpfsfsal_handle_t * p_objecthandle,        /* IN */
                                      gpfsfsal_op_context_t * p_context, /* IN */
                                      unsigned int xattr_id) /* IN */ ;

fsal_status_t GPFSFSAL_RemoveXAttrByName(gpfsfsal_handle_t * p_objecthandle,      /* IN */
                                        gpfsfsal_op_context_t * p_context,       /* IN */
                                        const fsal_name_t * xattr_name) /* IN */ ;

unsigned int GPFSFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t GPFSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                   fsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */) ;

