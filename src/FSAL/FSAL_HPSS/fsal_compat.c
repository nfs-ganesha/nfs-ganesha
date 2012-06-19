/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 * \file    fsal_compat.c
 * \brief   FSAL glue functions
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_internal.h"

fsal_status_t WRAP_HPSSFSAL_access(fsal_handle_t * object_handle,       /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_accessflags_t access_type,      /* IN */
                                   fsal_attrib_list_t *
                                   object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_access((hpssfsal_handle_t *) object_handle,
                         (hpssfsal_op_context_t *) p_context, access_type,
                         object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_getattrs(fsal_handle_t * p_filehandle,      /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     fsal_attrib_list_t *
                                     p_object_attributes /* IN/OUT */ )
{
  return HPSSFSAL_getattrs((hpssfsal_handle_t *) p_filehandle,
                           (hpssfsal_op_context_t *) p_context, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_setattrs(fsal_handle_t * p_filehandle,      /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     fsal_attrib_list_t * p_attrib_set, /* IN */
                                     fsal_attrib_list_t *
                                     p_object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_setattrs((hpssfsal_handle_t *) p_filehandle,
                           (hpssfsal_op_context_t *) p_context, p_attrib_set,
                           p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_BuildExportContext(fsal_export_context_t * p_export_context,        /* OUT */
                                               fsal_path_t * p_export_path,     /* IN */
                                               char *fs_specific_options /* IN */ )
{
  return HPSSFSAL_BuildExportContext((hpssfsal_export_context_t *) p_export_context,
                                     p_export_path, fs_specific_options);
}

fsal_status_t WRAP_HPSSFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context)
{
  return HPSSFSAL_CleanUpExportContext((hpssfsal_export_context_t *) p_export_context);
}


fsal_status_t WRAP_HPSSFSAL_InitClientContext(fsal_op_context_t * p_thr_context)
{
  return HPSSFSAL_InitClientContext((hpssfsal_op_context_t *) p_thr_context);
}

fsal_status_t WRAP_HPSSFSAL_GetClientContext(fsal_op_context_t * p_thr_context, /* IN/OUT  */
                                             fsal_export_context_t * p_export_context,  /* IN */
                                             fsal_uid_t uid,    /* IN */
                                             fsal_gid_t gid,    /* IN */
                                             fsal_gid_t * alt_groups,   /* IN */
                                             fsal_count_t nb_alt_groups /* IN */ )
{
  return HPSSFSAL_GetClientContext((hpssfsal_op_context_t *) p_thr_context,
                                   (hpssfsal_export_context_t *) p_export_context, uid,
                                   gid, alt_groups, nb_alt_groups);
}

fsal_status_t WRAP_HPSSFSAL_create(fsal_handle_t * p_parent_directory_handle,   /* IN */
                                   fsal_name_t * p_filename,    /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_accessmode_t accessmode,        /* IN */
                                   fsal_handle_t * p_object_handle,     /* OUT */
                                   fsal_attrib_list_t *
                                   p_object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_create((hpssfsal_handle_t *) p_parent_directory_handle, p_filename,
                         (hpssfsal_op_context_t *) p_context, accessmode,
                         (hpssfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,    /* IN */
                                  fsal_name_t * p_dirname,      /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_accessmode_t accessmode, /* IN */
                                  fsal_handle_t * p_object_handle,      /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_mkdir((hpssfsal_handle_t *) p_parent_directory_handle, p_dirname,
                        (hpssfsal_op_context_t *) p_context, accessmode,
                        (hpssfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_link(fsal_handle_t * p_target_handle,       /* IN */
                                 fsal_handle_t * p_dir_handle,  /* IN */
                                 fsal_name_t * p_link_name,     /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_link((hpssfsal_handle_t *) p_target_handle,
                       (hpssfsal_handle_t *) p_dir_handle, p_link_name,
                       (hpssfsal_op_context_t *) p_context, p_attributes);
}

fsal_status_t WRAP_HPSSFSAL_mknode(fsal_handle_t * parentdir_handle,    /* IN */
                                   fsal_name_t * p_node_name,   /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_accessmode_t accessmode,        /* IN */
                                   fsal_nodetype_t nodetype,    /* IN */
                                   fsal_dev_t * dev,    /* IN */
                                   fsal_handle_t * p_object_handle,     /* OUT (handle to the created node) */
                                   fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_mknode((hpssfsal_handle_t *) parentdir_handle, p_node_name,
                         (hpssfsal_op_context_t *) p_context, accessmode, nodetype, dev,
                         (hpssfsal_handle_t *) p_object_handle, node_attributes);
}

fsal_status_t WRAP_HPSSFSAL_opendir(fsal_handle_t * p_dir_handle,       /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_dir_t * p_dir_descriptor,      /* OUT */
                                    fsal_attrib_list_t *
                                    p_dir_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_opendir((hpssfsal_handle_t *) p_dir_handle,
                          (hpssfsal_op_context_t *) p_context,
                          (hpssfsal_dir_t *) p_dir_descriptor, p_dir_attributes);
}

fsal_status_t WRAP_HPSSFSAL_readdir(fsal_dir_t * p_dir_descriptor,      /* IN */
                                    fsal_cookie_t start_position,       /* IN */
                                    fsal_attrib_mask_t get_attr_mask,   /* IN */
                                    fsal_mdsize_t buffersize,   /* IN */
                                    fsal_dirent_t * p_pdirent,  /* OUT */
                                    fsal_cookie_t * p_end_position,     /* OUT */
                                    fsal_count_t * p_nb_entries,        /* OUT */
                                    fsal_boolean_t * p_end_of_dir /* OUT */ )
{
  hpssfsal_cookie_t hpsscookie;

  memcpy((char *)&hpsscookie, (char *)&start_position, sizeof(hpssfsal_cookie_t));

  return HPSSFSAL_readdir((hpssfsal_dir_t *) p_dir_descriptor,
                          start_position,
                          get_attr_mask,
                          buffersize, 
                          p_pdirent, 
                          (hpssfsal_cookie_t *) p_end_position,
                          p_nb_entries, 
                          p_end_of_dir);
}

fsal_status_t WRAP_HPSSFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ )
{
  return HPSSFSAL_closedir((hpssfsal_dir_t *) p_dir_descriptor);
}

fsal_status_t WRAP_HPSSFSAL_open_by_name(fsal_handle_t * dirhandle,     /* IN */
                                         fsal_name_t * filename,        /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         fsal_openflags_t openflags,    /* IN */
                                         fsal_file_t * file_descriptor, /* OUT */
                                         fsal_attrib_list_t *
                                         file_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_open_by_name((hpssfsal_handle_t *) dirhandle, filename,
                               (hpssfsal_op_context_t *) p_context, openflags,
                               (hpssfsal_file_t *) file_descriptor, file_attributes);
}

fsal_status_t WRAP_HPSSFSAL_open(fsal_handle_t * p_filehandle,  /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_openflags_t openflags,    /* IN */
                                 fsal_file_t * p_file_descriptor,       /* OUT */
                                 fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_open((hpssfsal_handle_t *) p_filehandle,
                       (hpssfsal_op_context_t *) p_context, openflags,
                       (hpssfsal_file_t *) p_file_descriptor, p_file_attributes);
}

fsal_status_t WRAP_HPSSFSAL_read(fsal_file_t * p_file_descriptor,       /* IN */
                                 fsal_seek_t * p_seek_descriptor,       /* [IN] */
                                 fsal_size_t buffer_size,       /* IN */
                                 caddr_t buffer,        /* OUT */
                                 fsal_size_t * p_read_amount,   /* OUT */
                                 fsal_boolean_t * p_end_of_file /* OUT */ )
{
  return HPSSFSAL_read((hpssfsal_file_t *) p_file_descriptor, p_seek_descriptor,
                       buffer_size, buffer, p_read_amount, p_end_of_file);
}

fsal_status_t WRAP_HPSSFSAL_write(fsal_file_t * p_file_descriptor,      /* IN */
                                  fsal_seek_t * p_seek_descriptor,      /* IN */
                                  fsal_size_t buffer_size,      /* IN */
                                  caddr_t buffer,       /* IN */
                                  fsal_size_t * p_write_amount /* OUT */ )
{
  return HPSSFSAL_write((hpssfsal_file_t *) p_file_descriptor, p_seek_descriptor,
                        buffer_size, buffer, p_write_amount);
}

fsal_status_t WRAP_HPSSFSAL_close(fsal_file_t * p_file_descriptor /* IN */ )
{
  return HPSSFSAL_close((hpssfsal_file_t *) p_file_descriptor);
}

fsal_status_t WRAP_HPSSFSAL_open_by_fileid(fsal_handle_t * filehandle,  /* IN */
                                           fsal_u64_t fileid,   /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           fsal_openflags_t openflags,  /* IN */
                                           fsal_file_t * file_descriptor,       /* OUT */
                                           fsal_attrib_list_t *
                                           file_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_open_by_fileid((hpssfsal_handle_t *) filehandle, fileid,
                                 (hpssfsal_op_context_t *) p_context, openflags,
                                 (hpssfsal_file_t *) file_descriptor, file_attributes);
}

fsal_status_t WRAP_HPSSFSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                            fsal_u64_t fileid)
{
  return HPSSFSAL_close_by_fileid((hpssfsal_file_t *) file_descriptor, fileid);
}

fsal_status_t WRAP_HPSSFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,        /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           fsal_dynamicfsinfo_t *
                                           p_dynamicinfo /* OUT */ )
{
  return HPSSFSAL_dynamic_fsinfo((hpssfsal_handle_t *) p_filehandle,
                                 (hpssfsal_op_context_t *) p_context, p_dynamicinfo);
}

fsal_status_t WRAP_HPSSFSAL_Init(fsal_parameter_t * init_info /* IN */ )
{
  return HPSSFSAL_Init(init_info);
}

fsal_status_t WRAP_HPSSFSAL_terminate()
{
  return HPSSFSAL_terminate();
}

fsal_status_t WRAP_HPSSFSAL_test_access(fsal_op_context_t * p_context,  /* IN */
                                        fsal_accessflags_t access_type, /* IN */
                                        fsal_attrib_list_t *
                                        p_object_attributes /* IN */ )
{
  return HPSSFSAL_test_access((hpssfsal_op_context_t *) p_context, access_type,
                              p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_setattr_access(fsal_op_context_t * p_context,       /* IN */
                                           fsal_attrib_list_t * candidate_attributes,   /* IN */
                                           fsal_attrib_list_t *
                                           object_attributes /* IN */ )
{
  return HPSSFSAL_setattr_access((hpssfsal_op_context_t *) p_context,
                                 candidate_attributes, object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_rename_access(fsal_op_context_t * pcontext, /* IN */
                                          fsal_attrib_list_t * pattrsrc,        /* IN */
                                          fsal_attrib_list_t * pattrdest)       /* IN */
{
  return HPSSFSAL_rename_access((hpssfsal_op_context_t *) pcontext, pattrsrc, pattrdest);
}

fsal_status_t WRAP_HPSSFSAL_create_access(fsal_op_context_t * pcontext, /* IN */
                                          fsal_attrib_list_t * pattr)   /* IN */
{
  return HPSSFSAL_create_access((hpssfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_HPSSFSAL_unlink_access(fsal_op_context_t * pcontext, /* IN */
                                          fsal_attrib_list_t * pattr)   /* IN */
{
  return HPSSFSAL_unlink_access((hpssfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_HPSSFSAL_link_access(fsal_op_context_t * pcontext,   /* IN */
                                        fsal_attrib_list_t * pattr)     /* IN */
{
  return HPSSFSAL_link_access((hpssfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_HPSSFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                        fsal_attrib_list_t * pnew_attr,
                                        fsal_attrib_list_t * presult_attr)
{
  return HPSSFSAL_merge_attrs(pinit_attr, pnew_attr, presult_attr);
}

fsal_status_t WRAP_HPSSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,   /* IN */
                                   fsal_name_t * p_filename,    /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_handle_t * p_object_handle,     /* OUT */
                                   fsal_attrib_list_t *
                                   p_object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_lookup((hpssfsal_handle_t *) p_parent_directory_handle, p_filename,
                         (hpssfsal_op_context_t *) p_context,
                         (hpssfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_lookupPath(fsal_path_t * p_path,    /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       fsal_handle_t * object_handle,   /* OUT */
                                       fsal_attrib_list_t *
                                       p_object_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_lookupPath(p_path, (hpssfsal_op_context_t *) p_context,
                             (hpssfsal_handle_t *) object_handle, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,   /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           fsal_handle_t * p_fsoot_handle,      /* OUT */
                                           fsal_attrib_list_t *
                                           p_fsroot_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_lookupJunction((hpssfsal_handle_t *) p_junction_handle,
                                 (hpssfsal_op_context_t *) p_context,
                                 (hpssfsal_handle_t *) p_fsoot_handle,
                                 p_fsroot_attributes);
}

fsal_status_t WRAP_HPSSFSAL_CleanObjectResources(fsal_handle_t * in_fsal_handle)
{
  return HPSSFSAL_CleanObjectResources((hpssfsal_handle_t *) in_fsal_handle);
}

fsal_status_t WRAP_HPSSFSAL_set_quota(fsal_path_t * pfsal_path, /* IN */
                                      int quota_type,   /* IN */
                                      fsal_uid_t fsal_uid,      /* IN */
                                      fsal_quota_t * pquota,    /* IN */
                                      fsal_quota_t * presquota) /* OUT */
{
  return HPSSFSAL_set_quota(pfsal_path, quota_type, fsal_uid, pquota, presquota);
}

fsal_status_t WRAP_HPSSFSAL_get_quota(fsal_path_t * pfsal_path, /* IN */
                                      int quota_type,   /* IN */
                                      fsal_uid_t fsal_uid,      /* IN */
                                      fsal_quota_t * pquota)    /* OUT */
{
  return HPSSFSAL_get_quota(pfsal_path, quota_type, fsal_uid, pquota);
}

fsal_status_t WRAP_HPSSFSAL_check_quota( char * path,  /* IN */
                                         fsal_quota_type_t  quota_type,
                                         fsal_uid_t fsal_uid)      /* IN */
{
  return HPSSFSAL_check_quota( path, quota_type, fsal_uid ) ;
}

fsal_status_t WRAP_HPSSFSAL_rcp(fsal_handle_t * filehandle,     /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_path_t * p_local_path,     /* IN */
                                fsal_rcpflag_t transfer_opt /* IN */ )
{
  return HPSSFSAL_rcp((hpssfsal_handle_t *) filehandle,
                      (hpssfsal_op_context_t *) p_context, p_local_path, transfer_opt);
}

fsal_status_t WRAP_HPSSFSAL_rename(fsal_handle_t * p_old_parentdir_handle,      /* IN */
                                   fsal_name_t * p_old_name,    /* IN */
                                   fsal_handle_t * p_new_parentdir_handle,      /* IN */
                                   fsal_name_t * p_new_name,    /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_attrib_list_t * p_src_dir_attributes,   /* [ IN/OUT ] */
                                   fsal_attrib_list_t *
                                   p_tgt_dir_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_rename((hpssfsal_handle_t *) p_old_parentdir_handle, p_old_name,
                         (hpssfsal_handle_t *) p_new_parentdir_handle, p_new_name,
                         (hpssfsal_op_context_t *) p_context, p_src_dir_attributes,
                         p_tgt_dir_attributes);
}

void WRAP_HPSSFSAL_get_stats(fsal_statistics_t * stats, /* OUT */
                             fsal_boolean_t reset /* IN */ )
{
  return HPSSFSAL_get_stats(stats, reset);
}

fsal_status_t WRAP_HPSSFSAL_readlink(fsal_handle_t * p_linkhandle,      /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     fsal_path_t * p_link_content,      /* OUT */
                                     fsal_attrib_list_t *
                                     p_link_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_readlink((hpssfsal_handle_t *) p_linkhandle,
                           (hpssfsal_op_context_t *) p_context, p_link_content,
                           p_link_attributes);
}

fsal_status_t WRAP_HPSSFSAL_symlink(fsal_handle_t * p_parent_directory_handle,  /* IN */
                                    fsal_name_t * p_linkname,   /* IN */
                                    fsal_path_t * p_linkcontent,        /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_accessmode_t accessmode,       /* IN (ignored) */
                                    fsal_handle_t * p_link_handle,      /* OUT */
                                    fsal_attrib_list_t *
                                    p_link_attributes /* [ IN/OUT ] */ )
{
  return HPSSFSAL_symlink((hpssfsal_handle_t *) p_parent_directory_handle, p_linkname,
                          p_linkcontent, (hpssfsal_op_context_t *) p_context, accessmode,
                          (hpssfsal_handle_t *) p_link_handle, p_link_attributes);
}

int WRAP_HPSSFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                            fsal_status_t * status)
{
  return HPSSFSAL_handlecmp((hpssfsal_handle_t *) handle1, (hpssfsal_handle_t *) handle2,
                            status);
}

unsigned int WRAP_HPSSFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                               unsigned int cookie,
                                               unsigned int alphabet_len,
                                               unsigned int index_size)
{
  return HPSSFSAL_Handle_to_HashIndex((hpssfsal_handle_t *) p_handle, cookie,
                                      alphabet_len, index_size);
}

unsigned int WRAP_HPSSFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                              unsigned int cookie)
{
  return HPSSFSAL_Handle_to_RBTIndex((hpssfsal_handle_t *) p_handle, cookie);
}

fsal_status_t WRAP_HPSSFSAL_DigestHandle(fsal_export_context_t * p_exportcontext,       /* IN */
                                         fsal_digesttype_t output_type, /* IN */
                                         fsal_handle_t * p_in_fsal_handle,      /* IN */
                                         caddr_t out_buff /* OUT */ )
{
  return HPSSFSAL_DigestHandle((hpssfsal_export_context_t *) p_exportcontext, output_type,
                               (hpssfsal_handle_t *) p_in_fsal_handle, out_buff);
}

fsal_status_t WRAP_HPSSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,  /* IN */
                                         fsal_digesttype_t in_type,     /* IN */
                                         caddr_t in_buff,       /* IN */
                                         fsal_handle_t * p_out_fsal_handle /* OUT */ )
{
  return HPSSFSAL_ExpandHandle((hpssfsal_export_context_t *) p_expcontext, in_type,
                               in_buff, (hpssfsal_handle_t *) p_out_fsal_handle);
}

fsal_status_t WRAP_HPSSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  return HPSSFSAL_SetDefault_FSAL_parameter(out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t *
                                                           out_parameter)
{
  return HPSSFSAL_SetDefault_FS_common_parameter(out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                             out_parameter)
{
  return HPSSFSAL_SetDefault_FS_specific_parameter(out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t *
                                                          out_parameter)
{
  return HPSSFSAL_load_FSAL_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                               fsal_parameter_t *
                                                               out_parameter)
{
  return HPSSFSAL_load_FS_common_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                                 fsal_parameter_t *
                                                                 out_parameter)
{
  return HPSSFSAL_load_FS_specific_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_HPSSFSAL_truncate(fsal_handle_t * p_filehandle,
                                     fsal_op_context_t * p_context,
                                     fsal_size_t length,
                                     fsal_file_t * file_descriptor,
                                     fsal_attrib_list_t * p_object_attributes)
{
  return HPSSFSAL_truncate((hpssfsal_handle_t *) p_filehandle,
                           (hpssfsal_op_context_t *) p_context, length,
                           (hpssfsal_file_t *) file_descriptor, p_object_attributes);
}

fsal_status_t WRAP_HPSSFSAL_unlink(fsal_handle_t * p_parent_directory_handle,   /* IN */
                                   fsal_name_t * p_object_name, /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_attrib_list_t *
                                   p_parent_directory_attributes /* [IN/OUT ] */ )
{
  return HPSSFSAL_unlink((hpssfsal_handle_t *) p_parent_directory_handle, p_object_name,
                         (hpssfsal_op_context_t *) p_context,
                         p_parent_directory_attributes);
}

fsal_status_t WRAP_HPSSFSAL_commit( fsal_file_t * p_file_descriptor, 
                                  fsal_off_t    offset, 
                                  fsal_size_t   length )

{
  return HPSSFSAL_commit((hpssfsal_file_t *) p_file_descriptor, offset, length );
}


char *WRAP_HPSSFSAL_GetFSName()
{
  return HPSSFSAL_GetFSName();
}

fsal_status_t WRAP_HPSSFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,       /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_attrib_list_t * p_attrs)
{
  return HPSSFSAL_GetXAttrAttrs((hpssfsal_handle_t *) p_objecthandle,
                                (hpssfsal_op_context_t *) p_context, xattr_id, p_attrs);
}

fsal_status_t WRAP_HPSSFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,  /* IN */
                                       unsigned int cookie,     /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       fsal_xattrent_t * xattrs_tab,    /* IN/OUT */
                                       unsigned int xattrs_tabsize,     /* IN */
                                       unsigned int *p_nb_returned,     /* OUT */
                                       int *end_of_list /* OUT */ )
{
  return HPSSFSAL_ListXAttrs((hpssfsal_handle_t *) p_objecthandle, cookie,
                             (hpssfsal_op_context_t *) p_context, xattrs_tab,
                             xattrs_tabsize, p_nb_returned, end_of_list);
}

fsal_status_t WRAP_HPSSFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,   /* IN */
                                              unsigned int xattr_id,    /* IN */
                                              fsal_op_context_t * p_context,    /* IN */
                                              caddr_t buffer_addr,      /* IN/OUT */
                                              size_t buffer_size,       /* IN */
                                              size_t * p_output_size /* OUT */ )
{
  return HPSSFSAL_GetXAttrValueById((hpssfsal_handle_t *) p_objecthandle, xattr_id,
                                    (hpssfsal_op_context_t *) p_context, buffer_addr,
                                    buffer_size, p_output_size);
}

fsal_status_t WRAP_HPSSFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,    /* IN */
                                             const fsal_name_t * xattr_name,    /* IN */
                                             fsal_op_context_t * p_context,     /* IN */
                                             unsigned int *pxattr_id /* OUT */ )
{
  return HPSSFSAL_GetXAttrIdByName((hpssfsal_handle_t *) p_objecthandle, xattr_name,
                                   (hpssfsal_op_context_t *) p_context, pxattr_id);
}

fsal_status_t WRAP_HPSSFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle, /* IN */
                                                const fsal_name_t * xattr_name, /* IN */
                                                fsal_op_context_t * p_context,  /* IN */
                                                caddr_t buffer_addr,    /* IN/OUT */
                                                size_t buffer_size,     /* IN */
                                                size_t * p_output_size /* OUT */ )
{
  return HPSSFSAL_GetXAttrValueByName((hpssfsal_handle_t *) p_objecthandle, xattr_name,
                                      (hpssfsal_op_context_t *) p_context, buffer_addr,
                                      buffer_size, p_output_size);
}

fsal_status_t WRAP_HPSSFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,       /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          caddr_t buffer_addr,  /* IN */
                                          size_t buffer_size,   /* IN */
                                          int create /* IN */ )
{
  return HPSSFSAL_SetXAttrValue((hpssfsal_handle_t *) p_objecthandle, xattr_name,
                                (hpssfsal_op_context_t *) p_context, buffer_addr,
                                buffer_size, create);
}

fsal_status_t WRAP_HPSSFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,   /* IN */
                                              unsigned int xattr_id,    /* IN */
                                              fsal_op_context_t * p_context,    /* IN */
                                              caddr_t buffer_addr,      /* IN */
                                              size_t buffer_size /* IN */ )
{
  return HPSSFSAL_SetXAttrValueById((hpssfsal_handle_t *) p_objecthandle, xattr_id,
                                    (hpssfsal_op_context_t *) p_context, buffer_addr,
                                    buffer_size);
}

fsal_status_t WRAP_HPSSFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,     /* IN */
                                            fsal_op_context_t * p_context,      /* IN */
                                            unsigned int xattr_id)      /* IN */
{
  return HPSSFSAL_RemoveXAttrById((hpssfsal_handle_t *) p_objecthandle,
                                  (hpssfsal_op_context_t *) p_context, xattr_id);
}

fsal_status_t WRAP_HPSSFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,   /* IN */
                                              fsal_op_context_t * p_context,    /* IN */
                                              const fsal_name_t * xattr_name)   /* IN */
{
  return HPSSFSAL_RemoveXAttrByName((hpssfsal_handle_t *) p_objecthandle,
                                    (hpssfsal_op_context_t *) p_context, xattr_name);
}

fsal_status_t WRAP_HPSSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                       fsal_op_context_t * p_context,        /* IN */
                                       fsal_extattrib_list_t * p_object_attributes /* OUT */)
{
  return HPSSFSAL_getextattrs( (hpssfsal_handle_t *)p_filehandle,
                               (hpssfsal_op_context_t *) p_context, p_object_attributes ) ;
}


fsal_functions_t fsal_hpss_functions = {
  .fsal_access = WRAP_HPSSFSAL_access,
  .fsal_getattrs = WRAP_HPSSFSAL_getattrs,
  .fsal_setattrs = WRAP_HPSSFSAL_setattrs,
  .fsal_buildexportcontext = WRAP_HPSSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = WRAP_HPSSFSAL_CleanUpExportContext,
  .fsal_initclientcontext = WRAP_HPSSFSAL_InitClientContext,
  .fsal_getclientcontext = WRAP_HPSSFSAL_GetClientContext,
  .fsal_create = WRAP_HPSSFSAL_create,
  .fsal_mkdir = WRAP_HPSSFSAL_mkdir,
  .fsal_link = WRAP_HPSSFSAL_link,
  .fsal_mknode = WRAP_HPSSFSAL_mknode,
  .fsal_opendir = WRAP_HPSSFSAL_opendir,
  .fsal_readdir = WRAP_HPSSFSAL_readdir,
  .fsal_closedir = WRAP_HPSSFSAL_closedir,
  .fsal_open_by_name = WRAP_HPSSFSAL_open_by_name,
  .fsal_open = WRAP_HPSSFSAL_open,
  .fsal_read = WRAP_HPSSFSAL_read,
  .fsal_write = WRAP_HPSSFSAL_write,
  .fsal_close = WRAP_HPSSFSAL_close,
  .fsal_open_by_fileid = WRAP_HPSSFSAL_open_by_fileid,
  .fsal_close_by_fileid = WRAP_HPSSFSAL_close_by_fileid,
  .fsal_dynamic_fsinfo = WRAP_HPSSFSAL_dynamic_fsinfo,
  .fsal_init = WRAP_HPSSFSAL_Init,
  .fsal_terminate = WRAP_HPSSFSAL_terminate,
  .fsal_test_access = WRAP_HPSSFSAL_test_access,
  .fsal_setattr_access = WRAP_HPSSFSAL_setattr_access,
  .fsal_rename_access = WRAP_HPSSFSAL_rename_access,
  .fsal_create_access = WRAP_HPSSFSAL_create_access,
  .fsal_unlink_access = WRAP_HPSSFSAL_unlink_access,
  .fsal_link_access = WRAP_HPSSFSAL_link_access,
  .fsal_merge_attrs = WRAP_HPSSFSAL_merge_attrs,
  .fsal_lookup = WRAP_HPSSFSAL_lookup,
  .fsal_lookuppath = WRAP_HPSSFSAL_lookupPath,
  .fsal_lookupjunction = WRAP_HPSSFSAL_lookupJunction,
  .fsal_cleanobjectresources = WRAP_HPSSFSAL_CleanObjectResources,
  .fsal_set_quota = WRAP_HPSSFSAL_set_quota,
  .fsal_get_quota = WRAP_HPSSFSAL_get_quota,
  .fsal_check_quota = WRAP_HPSSFSAL_check_quota,
  .fsal_rcp = WRAP_HPSSFSAL_rcp,
  .fsal_rename = WRAP_HPSSFSAL_rename,
  .fsal_get_stats = WRAP_HPSSFSAL_get_stats,
  .fsal_readlink = WRAP_HPSSFSAL_readlink,
  .fsal_symlink = WRAP_HPSSFSAL_symlink,
  .fsal_handlecmp = WRAP_HPSSFSAL_handlecmp,
  .fsal_handle_to_hashindex = WRAP_HPSSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = WRAP_HPSSFSAL_Handle_to_RBTIndex,
  .fsal_handle_to_hash_both = NULL,
  .fsal_digesthandle = WRAP_HPSSFSAL_DigestHandle,
  .fsal_expandhandle = WRAP_HPSSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = WRAP_HPSSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = WRAP_HPSSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = WRAP_HPSSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = WRAP_HPSSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      WRAP_HPSSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      WRAP_HPSSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = WRAP_HPSSFSAL_truncate,
  .fsal_unlink = WRAP_HPSSFSAL_unlink,
  .fsal_commit = WRAP_HPSSFSAL_commit,
  .fsal_getfsname = WRAP_HPSSFSAL_GetFSName,
  .fsal_getxattrattrs = WRAP_HPSSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = WRAP_HPSSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = WRAP_HPSSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = WRAP_HPSSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = WRAP_HPSSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = WRAP_HPSSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = WRAP_HPSSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = WRAP_HPSSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = WRAP_HPSSFSAL_RemoveXAttrByName,
  .fsal_getextattrs = WRAP_HPSSFSAL_getextattrs,
  .fsal_getfileno = HPSSFSAL_GetFileno,
  .fsal_share_op = COMMON_share_op_notsupp
};

fsal_const_t fsal_hpss_consts = {
  .fsal_handle_t_size = sizeof(hpssfsal_handle_t),
  .fsal_op_context_t_size = sizeof(hpssfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(hpssfsal_export_context_t),
  .fsal_file_t_size = sizeof(hpssfsal_file_t),
  .fsal_cookie_t_size = sizeof(hpssfsal_cookie_t),
  .fsal_cred_t_size = sizeof(hpssfsal_cred_t),
  .fs_specific_initinfo_t_size = sizeof(hpssfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(hpssfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_hpss_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_hpss_consts;
}                               /* FSAL_GetConsts */
