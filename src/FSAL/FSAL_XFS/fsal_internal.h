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
#define POSIX_SUPPORTED_ATTRIBUTES (                                       \
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

#endif

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        xfsfs_specific_initinfo_t * fs_specific_info);

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
fsal_status_t fsal_internal_handle2fd(xfsfsal_op_context_t * p_context,
                                      xfsfsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_fd2handle(xfsfsal_op_context_t * p_context,
                                      int fd, xfsfsal_handle_t * phandle);

/**
 * Build .lustre/fid path associated to a handle.
 */
fsal_status_t fsal_internal_Handle2FidPath(xfsfsal_op_context_t * p_context,    /* IN */
                                           xfsfsal_handle_t * p_handle, /* IN */
                                           fsal_path_t * p_fsalpath /* OUT */ );

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_Path2Handle(xfsfsal_op_context_t * p_context,       /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        xfsfsal_handle_t * p_handle /* OUT */ );

/**
 *  test the access to a file from its POSIX attributes (struct stat) OR its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t fsal_internal_testAccess(xfsfsal_op_context_t * p_context,        /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat *p_buffstat, /* IN, optional */
                                       fsal_attrib_list_t *
                                       p_object_attributes /* IN, optional */ );

fsal_status_t fsal_internal_inum2handle(xfsfsal_op_context_t * p_context,       /* IN */
                                        ino_t inum,     /* IN */
                                        xfsfsal_handle_t * phandle);    /* OUT */

fsal_status_t fsal_internal_setattrs_symlink(xfsfsal_handle_t * p_filehandle,   /* IN */
                                             xfsfsal_op_context_t * p_context,  /* IN */
                                             fsal_attrib_list_t * p_attrib_set, /* IN */
                                             fsal_attrib_list_t * p_object_attributes); /* [ IN/OUT ] */

int fsal_internal_path2fsname(char *rpath, char *fs_spec);

int fsal_internal_get_bulkstat_by_inode(int fd, xfs_ino_t * p_ino, xfs_bstat_t * pxfs_bstat) ;


/* All the call to FSAL to be wrapped */
fsal_status_t XFSFSAL_access(xfsfsal_handle_t * p_object_handle,        /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessflags_t access_type,    /* IN */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_getattrs(xfsfsal_handle_t * p_filehandle, /* IN */
                               xfsfsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t XFSFSAL_setattrs(xfsfsal_handle_t * p_filehandle, /* IN */
                               xfsfsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_attrib_set,       /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_BuildExportContext(xfsfsal_export_context_t * p_export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options /* IN */ );

fsal_status_t XFSFSAL_InitClientContext(xfsfsal_op_context_t * p_thr_context);

fsal_status_t XFSFSAL_CleanUpExportContext(xfsfsal_export_context_t * p_export_context) ;

fsal_status_t XFSFSAL_GetClientContext(xfsfsal_op_context_t * p_thr_context,    /* IN/OUT  */
                                       xfsfsal_export_context_t * p_export_context,     /* IN */
                                       fsal_uid_t uid,  /* IN */
                                       fsal_gid_t gid,  /* IN */
                                       fsal_gid_t * alt_groups, /* IN */
                                       fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t XFSFSAL_create(xfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             xfsfsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_mkdir(xfsfsal_handle_t * p_parent_directory_handle,       /* IN */
                            fsal_name_t * p_dirname,    /* IN */
                            xfsfsal_op_context_t * p_context,   /* IN */
                            fsal_accessmode_t accessmode,       /* IN */
                            xfsfsal_handle_t * p_object_handle, /* OUT */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_link(xfsfsal_handle_t * p_target_handle,  /* IN */
                           xfsfsal_handle_t * p_dir_handle,     /* IN */
                           fsal_name_t * p_link_name,   /* IN */
                           xfsfsal_op_context_t * p_context,    /* IN */
                           fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_mknode(xfsfsal_handle_t * parentdir_handle,       /* IN */
                             fsal_name_t * p_node_name, /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_nodetype_t nodetype,  /* IN */
                             fsal_dev_t * dev,  /* IN */
                             xfsfsal_handle_t * p_object_handle,        /* OUT (handle to the created node) */
                             fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_opendir(xfsfsal_handle_t * p_dir_handle,  /* IN */
                              xfsfsal_op_context_t * p_context, /* IN */
                              xfsfsal_dir_t * p_dir_descriptor, /* OUT */
                              fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_readdir(xfsfsal_dir_t * p_dir_descriptor, /* IN */
                              xfsfsal_cookie_t start_position,  /* IN */
                              fsal_attrib_mask_t get_attr_mask, /* IN */
                              fsal_mdsize_t buffersize, /* IN */
                              fsal_dirent_t * p_pdirent,        /* OUT */
                              xfsfsal_cookie_t * p_end_position,        /* OUT */
                              fsal_count_t * p_nb_entries,      /* OUT */
                              fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t XFSFSAL_closedir(xfsfsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t XFSFSAL_open_by_name(xfsfsal_handle_t * dirhandle,        /* IN */
                                   fsal_name_t * filename,      /* IN */
                                   xfsfsal_op_context_t * p_context,    /* IN */
                                   fsal_openflags_t openflags,  /* IN */
                                   xfsfsal_file_t * file_descriptor,    /* OUT */
                                   fsal_attrib_list_t *
                                   file_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_open(xfsfsal_handle_t * p_filehandle,     /* IN */
                           xfsfsal_op_context_t * p_context,    /* IN */
                           fsal_openflags_t openflags,  /* IN */
                           xfsfsal_file_t * p_file_descriptor,  /* OUT */
                           fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_read(xfsfsal_file_t * p_file_descriptor,  /* IN */
                           fsal_seek_t * p_seek_descriptor,     /* [IN] */
                           fsal_size_t buffer_size,     /* IN */
                           caddr_t buffer,      /* OUT */
                           fsal_size_t * p_read_amount, /* OUT */
                           fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t XFSFSAL_write(xfsfsal_file_t * p_file_descriptor, /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* IN */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* IN */
                            fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t XFSFSAL_close(xfsfsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t XFSFSAL_open_by_fileid(xfsfsal_handle_t * filehandle,     /* IN */
                                     fsal_u64_t fileid, /* IN */
                                     xfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     xfsfsal_file_t * file_descriptor,  /* OUT */
                                     fsal_attrib_list_t *
                                     file_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_close_by_fileid(xfsfsal_file_t * file_descriptor /* IN */ ,
                                      fsal_u64_t fileid);

fsal_status_t XFSFSAL_static_fsinfo(xfsfsal_handle_t * p_filehandle,    /* IN */
                                    xfsfsal_op_context_t * p_context,   /* IN */
                                    fsal_staticfsinfo_t * p_staticinfo /* OUT */ );

fsal_status_t XFSFSAL_dynamic_fsinfo(xfsfsal_handle_t * p_filehandle,   /* IN */
                                     xfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t XFSFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t XFSFSAL_terminate();

fsal_status_t XFSFSAL_test_access(xfsfsal_op_context_t * p_context,     /* IN */
                                  fsal_accessflags_t access_type,       /* IN */
                                  fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t XFSFSAL_setattr_access(xfsfsal_op_context_t * p_context,  /* IN */
                                     fsal_attrib_list_t * candidate_attributes, /* IN */
                                     fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t XFSFSAL_rename_access(xfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattrsrc,      /* IN */
                                    fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t XFSFSAL_create_access(xfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t XFSFSAL_unlink_access(xfsfsal_op_context_t * pcontext,    /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t XFSFSAL_link_access(xfsfsal_op_context_t * pcontext,      /* IN */
                                  fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t XFSFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                  fsal_attrib_list_t * pnew_attr,
                                  fsal_attrib_list_t * presult_attr);

fsal_status_t XFSFSAL_lookup(xfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             xfsfsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_lookupPath(fsal_path_t * p_path,  /* IN */
                                 xfsfsal_op_context_t * p_context,      /* IN */
                                 xfsfsal_handle_t * object_handle,      /* OUT */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_lookupJunction(xfsfsal_handle_t * p_junction_handle,      /* IN */
                                     xfsfsal_op_context_t * p_context,  /* IN */
                                     xfsfsal_handle_t * p_fsoot_handle, /* OUT */
                                     fsal_attrib_list_t *
                                     p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_lock(xfsfsal_file_t * obj_handle,
                           xfsfsal_lockdesc_t * ldesc, fsal_boolean_t blocking);

fsal_status_t XFSFSAL_changelock(xfsfsal_lockdesc_t * lock_descriptor,  /* IN / OUT */
                                 fsal_lockparam_t * lock_info /* IN */ );

fsal_status_t XFSFSAL_unlock(xfsfsal_file_t * obj_handle, xfsfsal_lockdesc_t * ldesc);

fsal_status_t XFSFSAL_getlock(xfsfsal_file_t * obj_handle, xfsfsal_lockdesc_t * ldesc);

fsal_status_t XFSFSAL_CleanObjectResources(xfsfsal_handle_t * in_fsal_handle);

fsal_status_t XFSFSAL_set_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota,  /* IN */
                                fsal_quota_t * presquota);      /* OUT */

fsal_status_t XFSFSAL_get_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota); /* OUT */

fsal_status_t XFSFSAL_rcp(xfsfsal_handle_t * filehandle,        /* IN */
                          xfsfsal_op_context_t * p_context,     /* IN */
                          fsal_path_t * p_local_path,   /* IN */
                          fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t XFSFSAL_rcp_by_fileid(xfsfsal_handle_t * filehandle,      /* IN */
                                    fsal_u64_t fileid,  /* IN */
                                    xfsfsal_op_context_t * p_context,   /* IN */
                                    fsal_path_t * p_local_path, /* IN */
                                    fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t XFSFSAL_rename(xfsfsal_handle_t * p_old_parentdir_handle, /* IN */
                             fsal_name_t * p_old_name,  /* IN */
                             xfsfsal_handle_t * p_new_parentdir_handle, /* IN */
                             fsal_name_t * p_new_name,  /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t * p_src_dir_attributes, /* [ IN/OUT ] */
                             fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ );

void XFSFSAL_get_stats(fsal_statistics_t * stats,       /* OUT */
                       fsal_boolean_t reset /* IN */ );

fsal_status_t XFSFSAL_readlink(xfsfsal_handle_t * p_linkhandle, /* IN */
                               xfsfsal_op_context_t * p_context,        /* IN */
                               fsal_path_t * p_link_content,    /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_symlink(xfsfsal_handle_t * p_parent_directory_handle,     /* IN */
                              fsal_name_t * p_linkname, /* IN */
                              fsal_path_t * p_linkcontent,      /* IN */
                              xfsfsal_op_context_t * p_context, /* IN */
                              fsal_accessmode_t accessmode,     /* IN (ignored) */
                              xfsfsal_handle_t * p_link_handle, /* OUT */
                              fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int XFSFSAL_handlecmp(xfsfsal_handle_t * handle1, xfsfsal_handle_t * handle2,
                      fsal_status_t * status);

unsigned int XFSFSAL_Handle_to_HashIndex(xfsfsal_handle_t * p_handle,
                                         unsigned int cookie,
                                         unsigned int alphabet_len,
                                         unsigned int index_size);

unsigned int XFSFSAL_Handle_to_RBTIndex(xfsfsal_handle_t * p_handle, unsigned int cookie);

fsal_status_t XFSFSAL_DigestHandle(xfsfsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t output_type,       /* IN */
                                   xfsfsal_handle_t * p_in_fsal_handle, /* IN */
                                   caddr_t out_buff /* OUT */ );

fsal_status_t XFSFSAL_ExpandHandle(xfsfsal_export_context_t * p_expcontext,     /* IN */
                                   fsal_digesttype_t in_type,   /* IN */
                                   caddr_t in_buff,     /* IN */
                                   xfsfsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t XFSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t XFSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t XFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t XFSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                    fsal_parameter_t * out_parameter);

fsal_status_t XFSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                         fsal_parameter_t *
                                                         out_parameter);

fsal_status_t XFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t XFSFSAL_truncate(xfsfsal_handle_t * p_filehandle, /* IN */
                               xfsfsal_op_context_t * p_context,        /* IN */
                               fsal_size_t length,      /* IN */
                               xfsfsal_file_t * file_descriptor,        /* Unused in this FSAL */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t XFSFSAL_unlink(xfsfsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_object_name,       /* IN */
                             xfsfsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t *
                             p_parent_directory_attributes /* [IN/OUT ] */ );

char *XFSFSAL_GetFSName();

fsal_status_t XFSFSAL_GetXAttrAttrs(xfsfsal_handle_t * p_objecthandle,  /* IN */
                                    xfsfsal_op_context_t * p_context,   /* IN */
                                    unsigned int xattr_id,      /* IN */
                                    fsal_attrib_list_t * p_attrs);

fsal_status_t XFSFSAL_ListXAttrs(xfsfsal_handle_t * p_objecthandle,     /* IN */
                                 unsigned int cookie,   /* IN */
                                 xfsfsal_op_context_t * p_context,      /* IN */
                                 fsal_xattrent_t * xattrs_tab,  /* IN/OUT */
                                 unsigned int xattrs_tabsize,   /* IN */
                                 unsigned int *p_nb_returned,   /* OUT */
                                 int *end_of_list /* OUT */ );

fsal_status_t XFSFSAL_GetXAttrValueById(xfsfsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        xfsfsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN/OUT */
                                        size_t buffer_size,     /* IN */
                                        size_t * p_output_size /* OUT */ );

fsal_status_t XFSFSAL_GetXAttrIdByName(xfsfsal_handle_t * p_objecthandle,       /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       xfsfsal_op_context_t * p_context,        /* IN */
                                       unsigned int *pxattr_id /* OUT */ );

fsal_status_t XFSFSAL_GetXAttrValueByName(xfsfsal_handle_t * p_objecthandle,    /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          xfsfsal_op_context_t * p_context,     /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t XFSFSAL_SetXAttrValue(xfsfsal_handle_t * p_objecthandle,  /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    xfsfsal_op_context_t * p_context,   /* IN */
                                    caddr_t buffer_addr,        /* IN */
                                    size_t buffer_size, /* IN */
                                    int create /* IN */ );

fsal_status_t XFSFSAL_SetXAttrValueById(xfsfsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        xfsfsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN */
                                        size_t buffer_size /* IN */ );

fsal_status_t XFSFSAL_RemoveXAttrById(xfsfsal_handle_t * p_objecthandle,        /* IN */
                                      xfsfsal_op_context_t * p_context, /* IN */
                                      unsigned int xattr_id) /* IN */ ;

fsal_status_t XFSFSAL_RemoveXAttrByName(xfsfsal_handle_t * p_objecthandle,      /* IN */
                                        xfsfsal_op_context_t * p_context,       /* IN */
                                        const fsal_name_t * xattr_name) /* IN */ ;

unsigned int XFSFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t XFSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_extattrib_list_t * p_object_attributes /* OUT */) ;


