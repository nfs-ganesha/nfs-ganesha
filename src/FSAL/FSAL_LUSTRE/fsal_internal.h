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
 * Append a fsal_name to an fsal_path to have the full path of a file from its name and its parent path
 */
fsal_status_t fsal_internal_appendNameToPath(fsal_path_t * p_path,
                                             const fsal_name_t * p_name);

/**
 * Build .lustre/fid path associated to a handle.
 */
fsal_status_t fsal_internal_Handle2FidPath(fsal_op_context_t * p_context,       /* IN */
                                           fsal_handle_t * p_handle,    /* IN */
                                           fsal_path_t * p_fsalpath /* OUT */ );

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_Path2Handle(fsal_op_context_t * p_context,  /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        fsal_handle_t * p_handle /* OUT */ );

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
fsal_status_t LUSTREFSAL_access(lustrefsal_handle_t * p_object_handle,  /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                fsal_accessflags_t access_type, /* IN */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_getattrs(lustrefsal_handle_t * p_filehandle,   /* IN */
                                  lustrefsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t LUSTREFSAL_setattrs(lustrefsal_handle_t * p_filehandle,   /* IN */
                                  lustrefsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_attrib_set,    /* IN */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_BuildExportContext(lustrefsal_export_context_t * p_export_context,     /* OUT */
                                            fsal_path_t * p_export_path,        /* IN */
                                            char *fs_specific_options /* IN */ );

fsal_status_t LUSTREFSAL_CleanUpExportContext(lustrefsal_export_context_t * p_export_context);

fsal_status_t LUSTREFSAL_InitClientContext(lustrefsal_op_context_t * p_thr_context);

fsal_status_t LUSTREFSAL_GetClientContext(lustrefsal_op_context_t * p_thr_context,      /* IN/OUT  */
                                          lustrefsal_export_context_t * p_export_context,       /* IN */
                                          fsal_uid_t uid,       /* IN */
                                          fsal_gid_t gid,       /* IN */
                                          fsal_gid_t * alt_groups,      /* IN */
                                          fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t LUSTREFSAL_create(lustrefsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_filename,       /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                lustrefsal_handle_t * p_object_handle,  /* OUT */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_mkdir(lustrefsal_handle_t * p_parent_directory_handle, /* IN */
                               fsal_name_t * p_dirname, /* IN */
                               lustrefsal_op_context_t * p_context,     /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               lustrefsal_handle_t * p_object_handle,   /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_link(lustrefsal_handle_t * p_target_handle,    /* IN */
                              lustrefsal_handle_t * p_dir_handle,       /* IN */
                              fsal_name_t * p_link_name,        /* IN */
                              lustrefsal_op_context_t * p_context,      /* IN */
                              fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_mknode(lustrefsal_handle_t * parentdir_handle, /* IN */
                                fsal_name_t * p_node_name,      /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                fsal_nodetype_t nodetype,       /* IN */
                                fsal_dev_t * dev,       /* IN */
                                lustrefsal_handle_t * p_object_handle,  /* OUT (handle to the created node) */
                                fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_opendir(lustrefsal_handle_t * p_dir_handle,    /* IN */
                                 lustrefsal_op_context_t * p_context,   /* IN */
                                 lustrefsal_dir_t * p_dir_descriptor,   /* OUT */
                                 fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_readdir(lustrefsal_dir_t * p_dir_descriptor,   /* IN */
                                 lustrefsal_cookie_t start_position,    /* IN */
                                 fsal_attrib_mask_t get_attr_mask,      /* IN */
                                 fsal_mdsize_t buffersize,      /* IN */
                                 fsal_dirent_t * p_pdirent,     /* OUT */
                                 lustrefsal_cookie_t * p_end_position,  /* OUT */
                                 fsal_count_t * p_nb_entries,   /* OUT */
                                 fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t LUSTREFSAL_closedir(lustrefsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t LUSTREFSAL_open_by_name(lustrefsal_handle_t * dirhandle,  /* IN */
                                      fsal_name_t * filename,   /* IN */
                                      lustrefsal_op_context_t * p_context,      /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      lustrefsal_file_t * file_descriptor,      /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_open(lustrefsal_handle_t * p_filehandle,       /* IN */
                              lustrefsal_op_context_t * p_context,      /* IN */
                              fsal_openflags_t openflags,       /* IN */
                              lustrefsal_file_t * p_file_descriptor,    /* OUT */
                              fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_read(lustrefsal_file_t * p_file_descriptor,    /* IN */
                              fsal_seek_t * p_seek_descriptor,  /* [IN] */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* OUT */
                              fsal_size_t * p_read_amount,      /* OUT */
                              fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t LUSTREFSAL_write(lustrefsal_file_t * p_file_descriptor,   /* IN */
                               fsal_seek_t * p_seek_descriptor, /* IN */
                               fsal_size_t buffer_size, /* IN */
                               caddr_t buffer,  /* IN */
                               fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t LUSTREFSAL_close(lustrefsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t LUSTREFSAL_open_by_fileid(lustrefsal_handle_t * filehandle,       /* IN */
                                        fsal_u64_t fileid,      /* IN */
                                        lustrefsal_op_context_t * p_context,    /* IN */
                                        fsal_openflags_t openflags,     /* IN */
                                        lustrefsal_file_t * file_descriptor,    /* OUT */
                                        fsal_attrib_list_t *
                                        file_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_close_by_fileid(lustrefsal_file_t * file_descriptor /* IN */ ,
                                         fsal_u64_t fileid);

fsal_status_t LUSTREFSAL_static_fsinfo(lustrefsal_handle_t * p_filehandle,      /* IN */
                                       lustrefsal_op_context_t * p_context,     /* IN */
                                       fsal_staticfsinfo_t * p_staticinfo /* OUT */ );

fsal_status_t LUSTREFSAL_dynamic_fsinfo(lustrefsal_handle_t * p_filehandle,     /* IN */
                                        lustrefsal_op_context_t * p_context,    /* IN */
                                        fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t LUSTREFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t LUSTREFSAL_terminate();

fsal_status_t LUSTREFSAL_test_access(lustrefsal_op_context_t * p_context,       /* IN */
                                     fsal_accessflags_t access_type,    /* IN */
                                     fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t LUSTREFSAL_setattr_access(lustrefsal_op_context_t * p_context,    /* IN */
                                        fsal_attrib_list_t * candidate_attributes,      /* IN */
                                        fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t LUSTREFSAL_rename_access(lustrefsal_op_context_t * pcontext,      /* IN */
                                       fsal_attrib_list_t * pattrsrc,   /* IN */
                                       fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t LUSTREFSAL_create_access(lustrefsal_op_context_t * pcontext,      /* IN */
                                       fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t LUSTREFSAL_unlink_access(lustrefsal_op_context_t * pcontext,      /* IN */
                                       fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t LUSTREFSAL_link_access(lustrefsal_op_context_t * pcontext,        /* IN */
                                     fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t LUSTREFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                     fsal_attrib_list_t * pnew_attr,
                                     fsal_attrib_list_t * presult_attr);

fsal_status_t LUSTREFSAL_lookup(lustrefsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_filename,       /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                lustrefsal_handle_t * p_object_handle,  /* OUT */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_lookupPath(fsal_path_t * p_path,       /* IN */
                                    lustrefsal_op_context_t * p_context,        /* IN */
                                    lustrefsal_handle_t * object_handle,        /* OUT */
                                    fsal_attrib_list_t *
                                    p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_lookupJunction(lustrefsal_handle_t * p_junction_handle,        /* IN */
                                        lustrefsal_op_context_t * p_context,    /* IN */
                                        lustrefsal_handle_t * p_fsoot_handle,   /* OUT */
                                        fsal_attrib_list_t *
                                        p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_lock(lustrefsal_file_t * obj_handle,
                              lustrefsal_lockdesc_t * ldesc, fsal_boolean_t blocking);

fsal_status_t LUSTREFSAL_changelock(lustrefsal_lockdesc_t * lock_descriptor,    /* IN / OUT */
                                    fsal_lockparam_t * lock_info /* IN */ );

fsal_status_t LUSTREFSAL_unlock(lustrefsal_file_t * obj_handle,
                                lustrefsal_lockdesc_t * ldesc);

fsal_status_t LUSTREFSAL_getlock(lustrefsal_file_t * obj_handle,
                                 lustrefsal_lockdesc_t * ldesc);

fsal_status_t LUSTREFSAL_CleanObjectResources(lustrefsal_handle_t * in_fsal_handle);

fsal_status_t LUSTREFSAL_set_quota(fsal_path_t * pfsal_path,    /* IN */
                                   int quota_type,      /* IN */
                                   fsal_uid_t fsal_uid, /* IN */
                                   fsal_quota_t * pquota,       /* IN */
                                   fsal_quota_t * presquota);   /* OUT */

fsal_status_t LUSTREFSAL_get_quota(fsal_path_t * pfsal_path,    /* IN */
                                   int quota_type,      /* IN */
                                   fsal_uid_t fsal_uid, /* IN */
                                   fsal_quota_t * pquota);      /* OUT */

fsal_status_t LUSTREFSAL_rcp(lustrefsal_handle_t * filehandle,  /* IN */
                             lustrefsal_op_context_t * p_context,       /* IN */
                             fsal_path_t * p_local_path,        /* IN */
                             fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t LUSTREFSAL_rcp_by_fileid(lustrefsal_handle_t * filehandle,        /* IN */
                                       fsal_u64_t fileid,       /* IN */
                                       lustrefsal_op_context_t * p_context,     /* IN */
                                       fsal_path_t * p_local_path,      /* IN */
                                       fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t LUSTREFSAL_rename(lustrefsal_handle_t * p_old_parentdir_handle,   /* IN */
                                fsal_name_t * p_old_name,       /* IN */
                                lustrefsal_handle_t * p_new_parentdir_handle,   /* IN */
                                fsal_name_t * p_new_name,       /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t * p_src_dir_attributes,      /* [ IN/OUT ] */
                                fsal_attrib_list_t *
                                p_tgt_dir_attributes /* [ IN/OUT ] */ );

void LUSTREFSAL_get_stats(fsal_statistics_t * stats,    /* OUT */
                          fsal_boolean_t reset /* IN */ );

fsal_status_t LUSTREFSAL_readlink(lustrefsal_handle_t * p_linkhandle,   /* IN */
                                  lustrefsal_op_context_t * p_context,  /* IN */
                                  fsal_path_t * p_link_content, /* OUT */
                                  fsal_attrib_list_t *
                                  p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_symlink(lustrefsal_handle_t * p_parent_directory_handle,       /* IN */
                                 fsal_name_t * p_linkname,      /* IN */
                                 fsal_path_t * p_linkcontent,   /* IN */
                                 lustrefsal_op_context_t * p_context,   /* IN */
                                 fsal_accessmode_t accessmode,  /* IN (ignored) */
                                 lustrefsal_handle_t * p_link_handle,   /* OUT */
                                 fsal_attrib_list_t *
                                 p_link_attributes /* [ IN/OUT ] */ );

int LUSTREFSAL_handlecmp(lustrefsal_handle_t * handle1, lustrefsal_handle_t * handle2,
                         fsal_status_t * status);

unsigned int LUSTREFSAL_Handle_to_HashIndex(lustrefsal_handle_t * p_handle,
                                            unsigned int cookie,
                                            unsigned int alphabet_len,
                                            unsigned int index_size);

unsigned int LUSTREFSAL_Handle_to_RBTIndex(lustrefsal_handle_t * p_handle,
                                           unsigned int cookie);

fsal_status_t LUSTREFSAL_DigestHandle(lustrefsal_export_context_t * p_expcontext,       /* IN */
                                      fsal_digesttype_t output_type,    /* IN */
                                      lustrefsal_handle_t * p_in_fsal_handle,   /* IN */
                                      caddr_t out_buff /* OUT */ );

fsal_status_t LUSTREFSAL_ExpandHandle(lustrefsal_export_context_t * p_expcontext,       /* IN */
                                      fsal_digesttype_t in_type,        /* IN */
                                      caddr_t in_buff,  /* IN */
                                      lustrefsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t LUSTREFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t LUSTREFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t LUSTREFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                          out_parameter);

fsal_status_t LUSTREFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                       fsal_parameter_t * out_parameter);

fsal_status_t LUSTREFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter);

fsal_status_t LUSTREFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                              fsal_parameter_t *
                                                              out_parameter);

fsal_status_t LUSTREFSAL_truncate(lustrefsal_handle_t * p_filehandle,   /* IN */
                                  lustrefsal_op_context_t * p_context,  /* IN */
                                  fsal_size_t length,   /* IN */
                                  lustrefsal_file_t * file_descriptor,  /* Unused in this FSAL */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_unlink(lustrefsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_object_name,    /* IN */
                                lustrefsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t *
                                p_parent_directory_attributes /* [IN/OUT ] */ );

char *LUSTREFSAL_GetFSName();

fsal_status_t LUSTREFSAL_GetXAttrAttrs(lustrefsal_handle_t * p_objecthandle,    /* IN */
                                       lustrefsal_op_context_t * p_context,     /* IN */
                                       unsigned int xattr_id,   /* IN */
                                       fsal_attrib_list_t * p_attrs);

fsal_status_t LUSTREFSAL_ListXAttrs(lustrefsal_handle_t * p_objecthandle,       /* IN */
                                    unsigned int cookie,        /* IN */
                                    lustrefsal_op_context_t * p_context,        /* IN */
                                    fsal_xattrent_t * xattrs_tab,       /* IN/OUT */
                                    unsigned int xattrs_tabsize,        /* IN */
                                    unsigned int *p_nb_returned,        /* OUT */
                                    int *end_of_list /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrValueById(lustrefsal_handle_t * p_objecthandle,        /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           lustrefsal_op_context_t * p_context, /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrIdByName(lustrefsal_handle_t * p_objecthandle, /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          lustrefsal_op_context_t * p_context,  /* IN */
                                          unsigned int *pxattr_id /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrValueByName(lustrefsal_handle_t * p_objecthandle,      /* IN */
                                             const fsal_name_t * xattr_name,    /* IN */
                                             lustrefsal_op_context_t * p_context,       /* IN */
                                             caddr_t buffer_addr,       /* IN/OUT */
                                             size_t buffer_size,        /* IN */
                                             size_t * p_output_size /* OUT */ );

fsal_status_t LUSTREFSAL_SetXAttrValue(lustrefsal_handle_t * p_objecthandle,    /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       lustrefsal_op_context_t * p_context,     /* IN */
                                       caddr_t buffer_addr,     /* IN */
                                       size_t buffer_size,      /* IN */
                                       int create /* IN */ );

fsal_status_t LUSTREFSAL_SetXAttrValueById(lustrefsal_handle_t * p_objecthandle,        /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           lustrefsal_op_context_t * p_context, /* IN */
                                           caddr_t buffer_addr, /* IN */
                                           size_t buffer_size /* IN */ );

fsal_status_t LUSTREFSAL_RemoveXAttrById(lustrefsal_handle_t * p_objecthandle,  /* IN */
                                         lustrefsal_op_context_t * p_context,   /* IN */
                                         unsigned int xattr_id) /* IN */ ;

fsal_status_t LUSTREFSAL_RemoveXAttrByName(lustrefsal_handle_t * p_objecthandle,        /* IN */
                                           lustrefsal_op_context_t * p_context, /* IN */
                                           const fsal_name_t * xattr_name) /* IN */ ;

unsigned int LUSTREFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t LUSTREFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                     fsal_op_context_t * p_context,        /* IN */
                                     fsal_extattrib_list_t * p_object_attributes /* OUT */) ;

