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
#include "fsal_types.h"
#include "fsal_glue.h"
#include "fsal_internal.h"

fsal_status_t WRAP_XFSFSAL_access(fsal_handle_t * object_handle,        /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_accessflags_t access_type,       /* IN */
                                  fsal_attrib_list_t *
                                  object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_access((xfsfsal_handle_t *) object_handle,
                        (xfsfsal_op_context_t *) p_context, access_type,
                        object_attributes);
}

fsal_status_t WRAP_XFSFSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_attrib_list_t *
                                    p_object_attributes /* IN/OUT */ )
{
  return XFSFSAL_getattrs((xfsfsal_handle_t *) p_filehandle,
                          (xfsfsal_op_context_t *) p_context, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_attrib_list_t * p_attrib_set,  /* IN */
                                    fsal_attrib_list_t *
                                    p_object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_setattrs((xfsfsal_handle_t *) p_filehandle,
                          (xfsfsal_op_context_t *) p_context, p_attrib_set,
                          p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                              fsal_path_t * p_export_path,      /* IN */
                                              char *fs_specific_options /* IN */ )
{
  return XFSFSAL_BuildExportContext((xfsfsal_export_context_t *) p_export_context,
                                    p_export_path, fs_specific_options);
}

fsal_status_t WRAP_XFSFSAL_CleanUpExportContext(fsal_export_context_t * p_export_context)
{
  return XFSFSAL_CleanUpExportContext((xfsfsal_export_context_t *) p_export_context);
}

fsal_status_t WRAP_XFSFSAL_InitClientContext(fsal_op_context_t * p_thr_context)
{
  return XFSFSAL_InitClientContext((xfsfsal_op_context_t *) p_thr_context);
}

fsal_status_t WRAP_XFSFSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                            fsal_export_context_t * p_export_context,   /* IN */
                                            fsal_uid_t uid,     /* IN */
                                            fsal_gid_t gid,     /* IN */
                                            fsal_gid_t * alt_groups,    /* IN */
                                            fsal_count_t nb_alt_groups /* IN */ )
{
  return XFSFSAL_GetClientContext((xfsfsal_op_context_t *) p_thr_context,
                                  (xfsfsal_export_context_t *) p_export_context, uid, gid,
                                  alt_groups, nb_alt_groups);
}

fsal_status_t WRAP_XFSFSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                                  fsal_name_t * p_filename,     /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_accessmode_t accessmode, /* IN */
                                  fsal_handle_t * p_object_handle,      /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_create((xfsfsal_handle_t *) p_parent_directory_handle, p_filename,
                        (xfsfsal_op_context_t *) p_context, accessmode,
                        (xfsfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                                 fsal_name_t * p_dirname,       /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_accessmode_t accessmode,  /* IN */
                                 fsal_handle_t * p_object_handle,       /* OUT */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_mkdir((xfsfsal_handle_t *) p_parent_directory_handle, p_dirname,
                       (xfsfsal_op_context_t *) p_context, accessmode,
                       (xfsfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                                fsal_handle_t * p_dir_handle,   /* IN */
                                fsal_name_t * p_link_name,      /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_link((xfsfsal_handle_t *) p_target_handle,
                      (xfsfsal_handle_t *) p_dir_handle, p_link_name,
                      (xfsfsal_op_context_t *) p_context, p_attributes);
}

fsal_status_t WRAP_XFSFSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                                  fsal_name_t * p_node_name,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_accessmode_t accessmode, /* IN */
                                  fsal_nodetype_t nodetype,     /* IN */
                                  fsal_dev_t * dev,     /* IN */
                                  fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                                  fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_mknode((xfsfsal_handle_t *) parentdir_handle, p_node_name,
                        (xfsfsal_op_context_t *) p_context, accessmode, nodetype, dev,
                        (xfsfsal_handle_t *) p_object_handle, node_attributes);
}

fsal_status_t WRAP_XFSFSAL_opendir(fsal_handle_t * p_dir_handle,        /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_dir_t * p_dir_descriptor,       /* OUT */
                                   fsal_attrib_list_t *
                                   p_dir_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_opendir((xfsfsal_handle_t *) p_dir_handle,
                         (xfsfsal_op_context_t *) p_context,
                         (xfsfsal_dir_t *) p_dir_descriptor, p_dir_attributes);
}

fsal_status_t WRAP_XFSFSAL_readdir(fsal_dir_t * p_dir_descriptor,       /* IN */
                                   fsal_cookie_t start_position,        /* IN */
                                   fsal_attrib_mask_t get_attr_mask,    /* IN */
                                   fsal_mdsize_t buffersize,    /* IN */
                                   fsal_dirent_t * p_pdirent,   /* OUT */
                                   fsal_cookie_t * p_end_position,      /* OUT */
                                   fsal_count_t * p_nb_entries, /* OUT */
                                   fsal_boolean_t * p_end_of_dir /* OUT */ )
{
  xfsfsal_cookie_t xfscookie;

  memcpy((char *)&xfscookie, (char *)&start_position, sizeof(xfsfsal_cookie_t));

  return XFSFSAL_readdir((xfsfsal_dir_t *) p_dir_descriptor, xfscookie, get_attr_mask,
                         buffersize, p_pdirent, (xfsfsal_cookie_t *) p_end_position,
                         p_nb_entries, p_end_of_dir);
}

fsal_status_t WRAP_XFSFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ )
{
  return XFSFSAL_closedir((xfsfsal_dir_t *) p_dir_descriptor);
}

fsal_status_t WRAP_XFSFSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                        fsal_name_t * filename, /* IN */
                                        fsal_op_context_t * p_context,  /* IN */
                                        fsal_openflags_t openflags,     /* IN */
                                        fsal_file_t * file_descriptor,  /* OUT */
                                        fsal_attrib_list_t *
                                        file_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_open_by_name((xfsfsal_handle_t *) dirhandle, filename,
                              (xfsfsal_op_context_t *) p_context, openflags,
                              (xfsfsal_file_t *) file_descriptor, file_attributes);
}

fsal_status_t WRAP_XFSFSAL_open(fsal_handle_t * p_filehandle,   /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * p_file_descriptor,        /* OUT */
                                fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_open((xfsfsal_handle_t *) p_filehandle,
                      (xfsfsal_op_context_t *) p_context, openflags,
                      (xfsfsal_file_t *) p_file_descriptor, p_file_attributes);
}

fsal_status_t WRAP_XFSFSAL_read(fsal_file_t * p_file_descriptor,        /* IN */
                                fsal_seek_t * p_seek_descriptor,        /* [IN] */
                                fsal_size_t buffer_size,        /* IN */
                                caddr_t buffer, /* OUT */
                                fsal_size_t * p_read_amount,    /* OUT */
                                fsal_boolean_t * p_end_of_file /* OUT */ )
{
  return XFSFSAL_read((xfsfsal_file_t *) p_file_descriptor, p_seek_descriptor,
                      buffer_size, buffer, p_read_amount, p_end_of_file);
}

fsal_status_t WRAP_XFSFSAL_write(fsal_file_t * p_file_descriptor,       /* IN */
                                 fsal_seek_t * p_seek_descriptor,       /* IN */
                                 fsal_size_t buffer_size,       /* IN */
                                 caddr_t buffer,        /* IN */
                                 fsal_size_t * p_write_amount /* OUT */ )
{
  return XFSFSAL_write((xfsfsal_file_t *) p_file_descriptor, p_seek_descriptor,
                       buffer_size, buffer, p_write_amount);
}

fsal_status_t WRAP_XFSFSAL_close(fsal_file_t * p_file_descriptor /* IN */ )
{
  return XFSFSAL_close((xfsfsal_file_t *) p_file_descriptor);
}

fsal_status_t WRAP_XFSFSAL_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                          fsal_u64_t fileid,    /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          fsal_openflags_t openflags,   /* IN */
                                          fsal_file_t * file_descriptor,        /* OUT */
                                          fsal_attrib_list_t *
                                          file_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_open_by_fileid((xfsfsal_handle_t *) filehandle, fileid,
                                (xfsfsal_op_context_t *) p_context, openflags,
                                (xfsfsal_file_t *) file_descriptor, file_attributes);
}

fsal_status_t WRAP_XFSFSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                           fsal_u64_t fileid)
{
  return XFSFSAL_close_by_fileid((xfsfsal_file_t *) file_descriptor, fileid);
}

fsal_status_t WRAP_XFSFSAL_static_fsinfo(fsal_handle_t * p_filehandle,  /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         fsal_staticfsinfo_t * p_staticinfo /* OUT */ )
{
  return XFSFSAL_static_fsinfo((xfsfsal_handle_t *) p_filehandle,
                               (xfsfsal_op_context_t *) p_context, p_staticinfo);
}

fsal_status_t WRAP_XFSFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ )
{
  return XFSFSAL_dynamic_fsinfo((xfsfsal_handle_t *) p_filehandle,
                                (xfsfsal_op_context_t *) p_context, p_dynamicinfo);
}

fsal_status_t WRAP_XFSFSAL_Init(fsal_parameter_t * init_info /* IN */ )
{
  return XFSFSAL_Init(init_info);
}

fsal_status_t WRAP_XFSFSAL_terminate()
{
  return XFSFSAL_terminate();
}

fsal_status_t WRAP_XFSFSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  return XFSFSAL_test_access((xfsfsal_op_context_t *) p_context, access_type,
                             p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_setattr_access(fsal_op_context_t * p_context,        /* IN */
                                          fsal_attrib_list_t * candidate_attributes,    /* IN */
                                          fsal_attrib_list_t *
                                          object_attributes /* IN */ )
{
  return XFSFSAL_setattr_access((xfsfsal_op_context_t *) p_context, candidate_attributes,
                                object_attributes);
}

fsal_status_t WRAP_XFSFSAL_rename_access(fsal_op_context_t * pcontext,  /* IN */
                                         fsal_attrib_list_t * pattrsrc, /* IN */
                                         fsal_attrib_list_t * pattrdest)        /* IN */
{
  return XFSFSAL_rename_access((xfsfsal_op_context_t *) pcontext, pattrsrc, pattrdest);
}

fsal_status_t WRAP_XFSFSAL_create_access(fsal_op_context_t * pcontext,  /* IN */
                                         fsal_attrib_list_t * pattr)    /* IN */
{
  return XFSFSAL_create_access((xfsfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_XFSFSAL_unlink_access(fsal_op_context_t * pcontext,  /* IN */
                                         fsal_attrib_list_t * pattr)    /* IN */
{
  return XFSFSAL_unlink_access((xfsfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_XFSFSAL_link_access(fsal_op_context_t * pcontext,    /* IN */
                                       fsal_attrib_list_t * pattr)      /* IN */
{
  return XFSFSAL_link_access((xfsfsal_op_context_t *) pcontext, pattr);
}

fsal_status_t WRAP_XFSFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                       fsal_attrib_list_t * pnew_attr,
                                       fsal_attrib_list_t * presult_attr)
{
  return XFSFSAL_merge_attrs(pinit_attr, pnew_attr, presult_attr);
}

fsal_status_t WRAP_XFSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,    /* IN */
                                  fsal_name_t * p_filename,     /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_object_handle,      /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_lookup((xfsfsal_handle_t *) p_parent_directory_handle, p_filename,
                        (xfsfsal_op_context_t *) p_context,
                        (xfsfsal_handle_t *) p_object_handle, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                                      fsal_op_context_t * p_context,    /* IN */
                                      fsal_handle_t * object_handle,    /* OUT */
                                      fsal_attrib_list_t *
                                      p_object_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_lookupPath(p_path, (xfsfsal_op_context_t *) p_context,
                            (xfsfsal_handle_t *) object_handle, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          fsal_handle_t * p_fsoot_handle,       /* OUT */
                                          fsal_attrib_list_t *
                                          p_fsroot_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_lookupJunction((xfsfsal_handle_t *) p_junction_handle,
                                (xfsfsal_op_context_t *) p_context,
                                (xfsfsal_handle_t *) p_fsoot_handle, p_fsroot_attributes);
}

fsal_status_t WRAP_XFSFSAL_lock(fsal_file_t * obj_handle,
                                fsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  return XFSFSAL_lock((xfsfsal_file_t *) obj_handle, (xfsfsal_lockdesc_t *) ldesc,
                      blocking);
}

fsal_status_t WRAP_XFSFSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                                      fsal_lockparam_t * lock_info /* IN */ )
{
  return XFSFSAL_changelock((xfsfsal_lockdesc_t *) lock_descriptor, lock_info);
}

fsal_status_t WRAP_XFSFSAL_unlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  return XFSFSAL_unlock((xfsfsal_file_t *) obj_handle, (xfsfsal_lockdesc_t *) ldesc);
}

fsal_status_t WRAP_XFSFSAL_getlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  return XFSFSAL_getlock((xfsfsal_file_t *) obj_handle, (xfsfsal_lockdesc_t *) ldesc);
}

fsal_status_t WRAP_XFSFSAL_CleanObjectResources(fsal_handle_t * in_fsal_handle)
{
  return XFSFSAL_CleanObjectResources((xfsfsal_handle_t *) in_fsal_handle);
}

fsal_status_t WRAP_XFSFSAL_set_quota(fsal_path_t * pfsal_path,  /* IN */
                                     int quota_type,    /* IN */
                                     fsal_uid_t fsal_uid,       /* IN */
                                     fsal_quota_t * pquota,     /* IN */
                                     fsal_quota_t * presquota)  /* OUT */
{
  return XFSFSAL_set_quota(pfsal_path, quota_type, fsal_uid, pquota, presquota);
}

fsal_status_t WRAP_XFSFSAL_get_quota(fsal_path_t * pfsal_path,  /* IN */
                                     int quota_type,    /* IN */
                                     fsal_uid_t fsal_uid,       /* IN */
                                     fsal_quota_t * pquota)     /* OUT */
{
  return XFSFSAL_get_quota(pfsal_path, quota_type, fsal_uid, pquota);
}

fsal_status_t WRAP_XFSFSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_path_t * p_local_path,      /* IN */
                               fsal_rcpflag_t transfer_opt /* IN */ )
{
  return XFSFSAL_rcp((xfsfsal_handle_t *) filehandle, (xfsfsal_op_context_t *) p_context,
                     p_local_path, transfer_opt);
}

fsal_status_t WRAP_XFSFSAL_rcp_by_fileid(fsal_handle_t * filehandle,    /* IN */
                                         fsal_u64_t fileid,     /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         fsal_path_t * p_local_path,    /* IN */
                                         fsal_rcpflag_t transfer_opt /* IN */ )
{
  return XFSFSAL_rcp_by_fileid((xfsfsal_handle_t *) filehandle, fileid,
                               (xfsfsal_op_context_t *) p_context, p_local_path,
                               transfer_opt);
}

fsal_status_t WRAP_XFSFSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
                                  fsal_name_t * p_old_name,     /* IN */
                                  fsal_handle_t * p_new_parentdir_handle,       /* IN */
                                  fsal_name_t * p_new_name,     /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t * p_src_dir_attributes,    /* [ IN/OUT ] */
                                  fsal_attrib_list_t *
                                  p_tgt_dir_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_rename((xfsfsal_handle_t *) p_old_parentdir_handle, p_old_name,
                        (xfsfsal_handle_t *) p_new_parentdir_handle, p_new_name,
                        (xfsfsal_op_context_t *) p_context, p_src_dir_attributes,
                        p_tgt_dir_attributes);
}

void WRAP_XFSFSAL_get_stats(fsal_statistics_t * stats,  /* OUT */
                            fsal_boolean_t reset /* IN */ )
{
  return XFSFSAL_get_stats(stats, reset);
}

fsal_status_t WRAP_XFSFSAL_readlink(fsal_handle_t * p_linkhandle,       /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_path_t * p_link_content,       /* OUT */
                                    fsal_attrib_list_t *
                                    p_link_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_readlink((xfsfsal_handle_t *) p_linkhandle,
                          (xfsfsal_op_context_t *) p_context, p_link_content,
                          p_link_attributes);
}

fsal_status_t WRAP_XFSFSAL_symlink(fsal_handle_t * p_parent_directory_handle,   /* IN */
                                   fsal_name_t * p_linkname,    /* IN */
                                   fsal_path_t * p_linkcontent, /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_accessmode_t accessmode,        /* IN (ignored) */
                                   fsal_handle_t * p_link_handle,       /* OUT */
                                   fsal_attrib_list_t *
                                   p_link_attributes /* [ IN/OUT ] */ )
{
  return XFSFSAL_symlink((xfsfsal_handle_t *) p_parent_directory_handle, p_linkname,
                         p_linkcontent, (xfsfsal_op_context_t *) p_context, accessmode,
                         (xfsfsal_handle_t *) p_link_handle, p_link_attributes);
}

int WRAP_XFSFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                           fsal_status_t * status)
{
  return XFSFSAL_handlecmp((xfsfsal_handle_t *) handle1, (xfsfsal_handle_t *) handle2,
                           status);
}

unsigned int WRAP_XFSFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                              unsigned int cookie,
                                              unsigned int alphabet_len,
                                              unsigned int index_size)
{
  return XFSFSAL_Handle_to_HashIndex((xfsfsal_handle_t *) p_handle, cookie, alphabet_len,
                                     index_size);
}

unsigned int WRAP_XFSFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                             unsigned int cookie)
{
  return XFSFSAL_Handle_to_RBTIndex((xfsfsal_handle_t *) p_handle, cookie);
}

fsal_status_t WRAP_XFSFSAL_DigestHandle(fsal_export_context_t * p_exportcontext,        /* IN */
                                        fsal_digesttype_t output_type,  /* IN */
                                        fsal_handle_t * p_in_fsal_handle,       /* IN */
                                        caddr_t out_buff /* OUT */ )
{
  return XFSFSAL_DigestHandle((xfsfsal_export_context_t *) p_exportcontext, output_type,
                              (xfsfsal_handle_t *) p_in_fsal_handle, out_buff);
}

fsal_status_t WRAP_XFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                        fsal_digesttype_t in_type,      /* IN */
                                        caddr_t in_buff,        /* IN */
                                        fsal_handle_t * p_out_fsal_handle /* OUT */ )
{
  return XFSFSAL_ExpandHandle((xfsfsal_export_context_t *) p_expcontext, in_type, in_buff,
                              (xfsfsal_handle_t *) p_out_fsal_handle);
}

fsal_status_t WRAP_XFSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  return XFSFSAL_SetDefault_FSAL_parameter(out_parameter);
}

fsal_status_t WRAP_XFSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t *
                                                          out_parameter)
{
  return XFSFSAL_SetDefault_FS_common_parameter(out_parameter);
}

fsal_status_t WRAP_XFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                            out_parameter)
{
  return XFSFSAL_SetDefault_FS_specific_parameter(out_parameter);
}

fsal_status_t WRAP_XFSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                         fsal_parameter_t * out_parameter)
{
  return XFSFSAL_load_FSAL_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_XFSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                              fsal_parameter_t *
                                                              out_parameter)
{
  return XFSFSAL_load_FS_common_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_XFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                                fsal_parameter_t *
                                                                out_parameter)
{
  return XFSFSAL_load_FS_specific_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t WRAP_XFSFSAL_truncate(fsal_handle_t * p_filehandle,
                                    fsal_op_context_t * p_context,
                                    fsal_size_t length,
                                    fsal_file_t * file_descriptor,
                                    fsal_attrib_list_t * p_object_attributes)
{
  return XFSFSAL_truncate((xfsfsal_handle_t *) p_filehandle,
                          (xfsfsal_op_context_t *) p_context, length,
                          (xfsfsal_file_t *) file_descriptor, p_object_attributes);
}

fsal_status_t WRAP_XFSFSAL_unlink(fsal_handle_t * p_parent_directory_handle,    /* IN */
                                  fsal_name_t * p_object_name,  /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t *
                                  p_parent_directory_attributes /* [IN/OUT ] */ )
{
  return XFSFSAL_unlink((xfsfsal_handle_t *) p_parent_directory_handle, p_object_name,
                        (xfsfsal_op_context_t *) p_context,
                        p_parent_directory_attributes);
}

char *WRAP_XFSFSAL_GetFSName()
{
  return XFSFSAL_GetFSName();
}

fsal_status_t WRAP_XFSFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         unsigned int xattr_id, /* IN */
                                         fsal_attrib_list_t * p_attrs)
{
  return XFSFSAL_GetXAttrAttrs((xfsfsal_handle_t *) p_objecthandle,
                               (xfsfsal_op_context_t *) p_context, xattr_id, p_attrs);
}

fsal_status_t WRAP_XFSFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,   /* IN */
                                      unsigned int cookie,      /* IN */
                                      fsal_op_context_t * p_context,    /* IN */
                                      fsal_xattrent_t * xattrs_tab,     /* IN/OUT */
                                      unsigned int xattrs_tabsize,      /* IN */
                                      unsigned int *p_nb_returned,      /* OUT */
                                      int *end_of_list /* OUT */ )
{
  return XFSFSAL_ListXAttrs((xfsfsal_handle_t *) p_objecthandle, cookie,
                            (xfsfsal_op_context_t *) p_context, xattrs_tab,
                            xattrs_tabsize, p_nb_returned, end_of_list);
}

fsal_status_t WRAP_XFSFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                             unsigned int xattr_id,     /* IN */
                                             fsal_op_context_t * p_context,     /* IN */
                                             caddr_t buffer_addr,       /* IN/OUT */
                                             size_t buffer_size,        /* IN */
                                             size_t * p_output_size /* OUT */ )
{
  return XFSFSAL_GetXAttrValueById((xfsfsal_handle_t *) p_objecthandle, xattr_id,
                                   (xfsfsal_op_context_t *) p_context, buffer_addr,
                                   buffer_size, p_output_size);
}

fsal_status_t WRAP_XFSFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                            const fsal_name_t * xattr_name,     /* IN */
                                            fsal_op_context_t * p_context,      /* IN */
                                            unsigned int *pxattr_id /* OUT */ )
{
  return XFSFSAL_GetXAttrIdByName((xfsfsal_handle_t *) p_objecthandle, xattr_name,
                                  (xfsfsal_op_context_t *) p_context, pxattr_id);
}

fsal_status_t WRAP_XFSFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                               const fsal_name_t * xattr_name,  /* IN */
                                               fsal_op_context_t * p_context,   /* IN */
                                               caddr_t buffer_addr,     /* IN/OUT */
                                               size_t buffer_size,      /* IN */
                                               size_t * p_output_size /* OUT */ )
{
  return XFSFSAL_GetXAttrValueByName((xfsfsal_handle_t *) p_objecthandle, xattr_name,
                                     (xfsfsal_op_context_t *) p_context, buffer_addr,
                                     buffer_size, p_output_size);
}

fsal_status_t WRAP_XFSFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                         const fsal_name_t * xattr_name,        /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size,    /* IN */
                                         int create /* IN */ )
{
  return XFSFSAL_SetXAttrValue((xfsfsal_handle_t *) p_objecthandle, xattr_name,
                               (xfsfsal_op_context_t *) p_context, buffer_addr,
                               buffer_size, create);
}

fsal_status_t WRAP_XFSFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                             unsigned int xattr_id,     /* IN */
                                             fsal_op_context_t * p_context,     /* IN */
                                             caddr_t buffer_addr,       /* IN */
                                             size_t buffer_size /* IN */ )
{
  return XFSFSAL_SetXAttrValueById((xfsfsal_handle_t *) p_objecthandle, xattr_id,
                                   (xfsfsal_op_context_t *) p_context, buffer_addr,
                                   buffer_size);
}

fsal_status_t WRAP_XFSFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           unsigned int xattr_id)       /* IN */
{
  return XFSFSAL_RemoveXAttrById((xfsfsal_handle_t *) p_objecthandle,
                                 (xfsfsal_op_context_t *) p_context, xattr_id);
}

fsal_status_t WRAP_XFSFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                             fsal_op_context_t * p_context,     /* IN */
                                             const fsal_name_t * xattr_name)    /* IN */
{
  return XFSFSAL_RemoveXAttrByName((xfsfsal_handle_t *) p_objecthandle,
                                   (xfsfsal_op_context_t *) p_context, xattr_name);
}

fsal_status_t WRAP_XFSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                       fsal_op_context_t * p_context,        /* IN */
                                       fsal_extattrib_list_t * p_object_attributes /* OUT */)
{
  return XFSFSAL_getextattrs( (xfsfsal_handle_t *)p_filehandle,
                              (xfsfsal_op_context_t *) p_context, p_object_attributes ) ;
}

fsal_functions_t fsal_xfs_functions = {
  .fsal_access = WRAP_XFSFSAL_access,
  .fsal_getattrs = WRAP_XFSFSAL_getattrs,
  .fsal_setattrs = WRAP_XFSFSAL_setattrs,
  .fsal_buildexportcontext = WRAP_XFSFSAL_BuildExportContext,
  .fsal_cleanupexportcontext = WRAP_XFSFSAL_CleanUpExportContext,
  .fsal_initclientcontext = WRAP_XFSFSAL_InitClientContext,
  .fsal_getclientcontext = WRAP_XFSFSAL_GetClientContext,
  .fsal_create = WRAP_XFSFSAL_create,
  .fsal_mkdir = WRAP_XFSFSAL_mkdir,
  .fsal_link = WRAP_XFSFSAL_link,
  .fsal_mknode = WRAP_XFSFSAL_mknode,
  .fsal_opendir = WRAP_XFSFSAL_opendir,
  .fsal_readdir = WRAP_XFSFSAL_readdir,
  .fsal_closedir = WRAP_XFSFSAL_closedir,
  .fsal_open_by_name = WRAP_XFSFSAL_open_by_name,
  .fsal_open = WRAP_XFSFSAL_open,
  .fsal_read = WRAP_XFSFSAL_read,
  .fsal_write = WRAP_XFSFSAL_write,
  .fsal_close = WRAP_XFSFSAL_close,
  .fsal_open_by_fileid = WRAP_XFSFSAL_open_by_fileid,
  .fsal_close_by_fileid = WRAP_XFSFSAL_close_by_fileid,
  .fsal_static_fsinfo = WRAP_XFSFSAL_static_fsinfo,
  .fsal_dynamic_fsinfo = WRAP_XFSFSAL_dynamic_fsinfo,
  .fsal_init = WRAP_XFSFSAL_Init,
  .fsal_terminate = WRAP_XFSFSAL_terminate,
  .fsal_test_access = WRAP_XFSFSAL_test_access,
  .fsal_setattr_access = WRAP_XFSFSAL_setattr_access,
  .fsal_rename_access = WRAP_XFSFSAL_rename_access,
  .fsal_create_access = WRAP_XFSFSAL_create_access,
  .fsal_unlink_access = WRAP_XFSFSAL_unlink_access,
  .fsal_link_access = WRAP_XFSFSAL_link_access,
  .fsal_merge_attrs = WRAP_XFSFSAL_merge_attrs,
  .fsal_lookup = WRAP_XFSFSAL_lookup,
  .fsal_lookuppath = WRAP_XFSFSAL_lookupPath,
  .fsal_lookupjunction = WRAP_XFSFSAL_lookupJunction,
  .fsal_lock = WRAP_XFSFSAL_lock,
  .fsal_changelock = WRAP_XFSFSAL_changelock,
  .fsal_unlock = WRAP_XFSFSAL_unlock,
  .fsal_getlock = WRAP_XFSFSAL_getlock,
  .fsal_cleanobjectresources = WRAP_XFSFSAL_CleanObjectResources,
  .fsal_set_quota = WRAP_XFSFSAL_set_quota,
  .fsal_get_quota = WRAP_XFSFSAL_get_quota,
  .fsal_rcp = WRAP_XFSFSAL_rcp,
  .fsal_rcp_by_fileid = WRAP_XFSFSAL_rcp_by_fileid,
  .fsal_rename = WRAP_XFSFSAL_rename,
  .fsal_get_stats = WRAP_XFSFSAL_get_stats,
  .fsal_readlink = WRAP_XFSFSAL_readlink,
  .fsal_symlink = WRAP_XFSFSAL_symlink,
  .fsal_handlecmp = WRAP_XFSFSAL_handlecmp,
  .fsal_handle_to_hashindex = WRAP_XFSFSAL_Handle_to_HashIndex,
  .fsal_handle_to_rbtindex = WRAP_XFSFSAL_Handle_to_RBTIndex,
  .fsal_digesthandle = WRAP_XFSFSAL_DigestHandle,
  .fsal_expandhandle = WRAP_XFSFSAL_ExpandHandle,
  .fsal_setdefault_fsal_parameter = WRAP_XFSFSAL_SetDefault_FSAL_parameter,
  .fsal_setdefault_fs_common_parameter = WRAP_XFSFSAL_SetDefault_FS_common_parameter,
  .fsal_setdefault_fs_specific_parameter = WRAP_XFSFSAL_SetDefault_FS_specific_parameter,
  .fsal_load_fsal_parameter_from_conf = WRAP_XFSFSAL_load_FSAL_parameter_from_conf,
  .fsal_load_fs_common_parameter_from_conf =
      WRAP_XFSFSAL_load_FS_common_parameter_from_conf,
  .fsal_load_fs_specific_parameter_from_conf =
      WRAP_XFSFSAL_load_FS_specific_parameter_from_conf,
  .fsal_truncate = WRAP_XFSFSAL_truncate,
  .fsal_unlink = WRAP_XFSFSAL_unlink,
  .fsal_getfsname = WRAP_XFSFSAL_GetFSName,
  .fsal_getxattrattrs = WRAP_XFSFSAL_GetXAttrAttrs,
  .fsal_listxattrs = WRAP_XFSFSAL_ListXAttrs,
  .fsal_getxattrvaluebyid = WRAP_XFSFSAL_GetXAttrValueById,
  .fsal_getxattridbyname = WRAP_XFSFSAL_GetXAttrIdByName,
  .fsal_getxattrvaluebyname = WRAP_XFSFSAL_GetXAttrValueByName,
  .fsal_setxattrvalue = WRAP_XFSFSAL_SetXAttrValue,
  .fsal_setxattrvaluebyid = WRAP_XFSFSAL_SetXAttrValueById,
  .fsal_removexattrbyid = WRAP_XFSFSAL_RemoveXAttrById,
  .fsal_removexattrbyname = WRAP_XFSFSAL_RemoveXAttrByName,
  .fsal_getextattrs = WRAP_XFSFSAL_getextattrs,
  .fsal_getfileno = XFSFSAL_GetFileno
};

fsal_const_t fsal_xfs_consts = {
  .fsal_handle_t_size = sizeof(xfsfsal_handle_t),
  .fsal_op_context_t_size = sizeof(xfsfsal_op_context_t),
  .fsal_export_context_t_size = sizeof(xfsfsal_export_context_t),
  .fsal_file_t_size = sizeof(xfsfsal_file_t),
  .fsal_cookie_t_size = sizeof(xfsfsal_cookie_t),
  .fsal_lockdesc_t_size = sizeof(xfsfsal_lockdesc_t),
  .fsal_cred_t_size = sizeof(xfsfsal_cred_t),
  .fs_specific_initinfo_t_size = sizeof(xfsfs_specific_initinfo_t),
  .fsal_dir_t_size = sizeof(xfsfsal_dir_t)
};

fsal_functions_t FSAL_GetFunctions(void)
{
  return fsal_xfs_functions;
}                               /* FSAL_GetFunctions */

fsal_const_t FSAL_GetConsts(void)
{
  return fsal_xfs_consts;
}                               /* FSAL_GetConsts */
