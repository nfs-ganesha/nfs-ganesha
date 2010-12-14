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

/* information for SNMP sessions establishment */
extern fs_specific_initinfo_t snmp_glob_config;

#endif

#define FSALSNMP_MAX_FILESIZE   4096

/**
 * for saving global SNMP configuration 
 */
void set_snmp_global_config(fs_specific_initinfo_t * p_config);

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

/* All the call to FSAL to be wrapped */
fsal_status_t SNMPFSAL_access(snmpfsal_handle_t * p_object_handle,      /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              fsal_accessflags_t access_type,   /* IN */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_getattrs(snmpfsal_handle_t * p_filehandle,       /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t SNMPFSAL_setattrs(snmpfsal_handle_t * p_filehandle,       /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_attrib_set,      /* IN */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_BuildExportContext(snmpfsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options /* IN */ );

fsal_status_t SNMPFSAL_CleanUpExportContext(snmpfsal_export_context_t * p_export_context);

fsal_status_t SNMPFSAL_InitClientContext(snmpfsal_op_context_t * p_thr_context);

fsal_status_t SNMPFSAL_GetClientContext(snmpfsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                        snmpfsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t SNMPFSAL_create(snmpfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              snmpfsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_mkdir(snmpfsal_handle_t * p_parent_directory_handle,     /* IN */
                             fsal_name_t * p_dirname,   /* IN */
                             snmpfsal_op_context_t * p_context, /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             snmpfsal_handle_t * p_object_handle,       /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_link(snmpfsal_handle_t * p_target_handle,        /* IN */
                            snmpfsal_handle_t * p_dir_handle,   /* IN */
                            fsal_name_t * p_link_name,  /* IN */
                            snmpfsal_op_context_t * p_context,  /* IN */
                            fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_mknode(snmpfsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_node_name,        /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_nodetype_t nodetype, /* IN */
                              fsal_dev_t * dev, /* IN */
                              snmpfsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                              fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_opendir(snmpfsal_handle_t * p_dir_handle,        /* IN */
                               snmpfsal_op_context_t * p_context,       /* IN */
                               snmpfsal_dir_t * p_dir_descriptor,       /* OUT */
                               fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_readdir(snmpfsal_dir_t * p_dir_descriptor,       /* IN */
                               snmpfsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * p_pdirent,       /* OUT */
                               snmpfsal_cookie_t * p_end_position,      /* OUT */
                               fsal_count_t * p_nb_entries,     /* OUT */
                               fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t SNMPFSAL_closedir(snmpfsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t SNMPFSAL_open_by_name(snmpfsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    snmpfsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    snmpfsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_open(snmpfsal_handle_t * p_filehandle,   /* IN */
                            snmpfsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            snmpfsal_file_t * p_file_descriptor,        /* OUT */
                            fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_read(snmpfsal_file_t * p_file_descriptor,        /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * p_read_amount,        /* OUT */
                            fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t SNMPFSAL_write(snmpfsal_file_t * p_file_descriptor,       /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t SNMPFSAL_close(snmpfsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t SNMPFSAL_open_by_fileid(snmpfsal_handle_t * filehandle,   /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      snmpfsal_op_context_t * p_context,        /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      snmpfsal_file_t * file_descriptor,        /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_close_by_fileid(snmpfsal_file_t * file_descriptor /* IN */ ,
                                       fsal_u64_t fileid);

fsal_status_t SNMPFSAL_static_fsinfo(snmpfsal_handle_t * p_filehandle,  /* IN */
                                     snmpfsal_op_context_t * p_context, /* IN */
                                     fsal_staticfsinfo_t * p_staticinfo /* OUT */ );

fsal_status_t SNMPFSAL_dynamic_fsinfo(snmpfsal_handle_t * p_filehandle, /* IN */
                                      snmpfsal_op_context_t * p_context,        /* IN */
                                      fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t SNMPFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t SNMPFSAL_terminate();

fsal_status_t SNMPFSAL_test_access(snmpfsal_op_context_t * p_context,   /* IN */
                                   fsal_accessflags_t access_type,      /* IN */
                                   fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t SNMPFSAL_setattr_access(snmpfsal_op_context_t * p_context,        /* IN */
                                      fsal_attrib_list_t * candidate_attributes,        /* IN */
                                      fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t SNMPFSAL_rename_access(snmpfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattrsrc,     /* IN */
                                     fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t SNMPFSAL_create_access(snmpfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t SNMPFSAL_unlink_access(snmpfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t SNMPFSAL_link_access(snmpfsal_op_context_t * pcontext,    /* IN */
                                   fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t SNMPFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                   fsal_attrib_list_t * pnew_attr,
                                   fsal_attrib_list_t * presult_attr);

fsal_status_t SNMPFSAL_lookup(snmpfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              snmpfsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  snmpfsal_op_context_t * p_context,    /* IN */
                                  snmpfsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_lookupJunction(snmpfsal_handle_t * p_junction_handle,    /* IN */
                                      snmpfsal_op_context_t * p_context,        /* IN */
                                      snmpfsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t *
                                      p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_lock(snmpfsal_file_t * obj_handle,
                            snmpfsal_lockdesc_t * ldesc, fsal_boolean_t blocking);

fsal_status_t SNMPFSAL_changelock(snmpfsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                                  fsal_lockparam_t * lock_info /* IN */ );

fsal_status_t SNMPFSAL_unlock(snmpfsal_file_t * obj_handle, snmpfsal_lockdesc_t * ldesc);

fsal_status_t SNMPFSAL_getlock(snmpfsal_file_t * obj_handle, snmpfsal_lockdesc_t * ldesc);

fsal_status_t SNMPFSAL_CleanObjectResources(snmpfsal_handle_t * in_fsal_handle);

fsal_status_t SNMPFSAL_set_quota(fsal_path_t * pfsal_path,      /* IN */
                                 int quota_type,        /* IN */
                                 fsal_uid_t fsal_uid,   /* IN */
                                 fsal_quota_t * pquota, /* IN */
                                 fsal_quota_t * presquota);     /* OUT */

fsal_status_t SNMPFSAL_get_quota(fsal_path_t * pfsal_path,      /* IN */
                                 int quota_type,        /* IN */
                                 fsal_uid_t fsal_uid,   /* IN */
                                 fsal_quota_t * pquota);        /* OUT */

fsal_status_t SNMPFSAL_rcp(snmpfsal_handle_t * filehandle,      /* IN */
                           snmpfsal_op_context_t * p_context,   /* IN */
                           fsal_path_t * p_local_path,  /* IN */
                           fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t SNMPFSAL_rcp_by_fileid(snmpfsal_handle_t * filehandle,    /* IN */
                                     fsal_u64_t fileid, /* IN */
                                     snmpfsal_op_context_t * p_context, /* IN */
                                     fsal_path_t * p_local_path,        /* IN */
                                     fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t SNMPFSAL_rename(snmpfsal_handle_t * p_old_parentdir_handle,       /* IN */
                              fsal_name_t * p_old_name, /* IN */
                              snmpfsal_handle_t * p_new_parentdir_handle,       /* IN */
                              fsal_name_t * p_new_name, /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t * p_src_dir_attributes,        /* [ IN/OUT ] */
                              fsal_attrib_list_t *
                              p_tgt_dir_attributes /* [ IN/OUT ] */ );

void SNMPFSAL_get_stats(fsal_statistics_t * stats,      /* OUT */
                        fsal_boolean_t reset /* IN */ );

fsal_status_t SNMPFSAL_readlink(snmpfsal_handle_t * p_linkhandle,       /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_path_t * p_link_content,   /* OUT */
                                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_symlink(snmpfsal_handle_t * p_parent_directory_handle,   /* IN */
                               fsal_name_t * p_linkname,        /* IN */
                               fsal_path_t * p_linkcontent,     /* IN */
                               snmpfsal_op_context_t * p_context,       /* IN */
                               fsal_accessmode_t accessmode,    /* IN (ignored) */
                               snmpfsal_handle_t * p_link_handle,       /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int SNMPFSAL_handlecmp(snmpfsal_handle_t * handle1, snmpfsal_handle_t * handle2,
                       fsal_status_t * status);

unsigned int SNMPFSAL_Handle_to_HashIndex(snmpfsal_handle_t * p_handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size);

unsigned int SNMPFSAL_Handle_to_RBTIndex(snmpfsal_handle_t * p_handle,
                                         unsigned int cookie);

fsal_status_t SNMPFSAL_DigestHandle(snmpfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    snmpfsal_handle_t * p_in_fsal_handle,       /* IN */
                                    caddr_t out_buff /* OUT */ );

fsal_status_t SNMPFSAL_ExpandHandle(snmpfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    snmpfsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t SNMPFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t SNMPFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t SNMPFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t SNMPFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter);

fsal_status_t SNMPFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t *
                                                          out_parameter);

fsal_status_t SNMPFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter);

fsal_status_t SNMPFSAL_truncate(snmpfsal_handle_t * p_filehandle,       /* IN */
                                snmpfsal_op_context_t * p_context,      /* IN */
                                fsal_size_t length,     /* IN */
                                snmpfsal_file_t * file_descriptor,      /* Unused in this FSAL */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t SNMPFSAL_unlink(snmpfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_object_name,      /* IN */
                              snmpfsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t *
                              p_parent_directory_attributes /* [IN/OUT ] */ );

char *SNMPFSAL_GetFSName();

fsal_status_t SNMPFSAL_GetXAttrAttrs(snmpfsal_handle_t * p_objecthandle,        /* IN */
                                     snmpfsal_op_context_t * p_context, /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_attrib_list_t * p_attrs);

fsal_status_t SNMPFSAL_ListXAttrs(snmpfsal_handle_t * p_objecthandle,   /* IN */
                                  unsigned int cookie,  /* IN */
                                  snmpfsal_op_context_t * p_context,    /* IN */
                                  fsal_xattrent_t * xattrs_tab, /* IN/OUT */
                                  unsigned int xattrs_tabsize,  /* IN */
                                  unsigned int *p_nb_returned,  /* OUT */
                                  int *end_of_list /* OUT */ );

fsal_status_t SNMPFSAL_GetXAttrValueById(snmpfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         snmpfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN/OUT */
                                         size_t buffer_size,    /* IN */
                                         size_t * p_output_size /* OUT */ );

fsal_status_t SNMPFSAL_GetXAttrIdByName(snmpfsal_handle_t * p_objecthandle,     /* IN */
                                        const fsal_name_t * xattr_name, /* IN */
                                        snmpfsal_op_context_t * p_context,      /* IN */
                                        unsigned int *pxattr_id /* OUT */ );

fsal_status_t SNMPFSAL_GetXAttrValueByName(snmpfsal_handle_t * p_objecthandle,  /* IN */
                                           const fsal_name_t * xattr_name,      /* IN */
                                           snmpfsal_op_context_t * p_context,   /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

fsal_status_t SNMPFSAL_SetXAttrValue(snmpfsal_handle_t * p_objecthandle,        /* IN */
                                     const fsal_name_t * xattr_name,    /* IN */
                                     snmpfsal_op_context_t * p_context, /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size,        /* IN */
                                     int create /* IN */ );

fsal_status_t SNMPFSAL_SetXAttrValueById(snmpfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         snmpfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size /* IN */ );

fsal_status_t SNMPFSAL_RemoveXAttrById(snmpfsal_handle_t * p_objecthandle,      /* IN */
                                       snmpfsal_op_context_t * p_context,       /* IN */
                                       unsigned int xattr_id) /* IN */ ;

fsal_status_t SNMPFSAL_RemoveXAttrByName(snmpfsal_handle_t * p_objecthandle,    /* IN */
                                         snmpfsal_op_context_t * p_context,     /* IN */
                                         const fsal_name_t * xattr_name) /* IN */ ;

unsigned int SNMPFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t SNMPFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                   fsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */) ;

