/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 * \file    fsal_glue.c
 * \brief   FSAL glue functions
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _USE_SHARED_FSAL
#include <stdlib.h>
#include <dlfcn.h>              /* For dlopen */
#endif

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_glue.h"

fsal_functions_t fsal_functions;
fsal_const_t fsal_consts;

#ifdef _USE_SHARED_FSAL
fsal_functions_t(*getfunctions) (void);
fsal_const_t(*getconsts) (void);
#endif                          /* _USE_SHARED_FSAL */

fsal_status_t FSAL_access(fsal_handle_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_access(object_handle, p_context, access_type,
                                    object_attributes);
}

fsal_status_t FSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_object_attributes /* IN/OUT */ )
{
  return fsal_functions.fsal_getattrs(p_filehandle, p_context, p_object_attributes);
}

fsal_status_t FSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_setattrs(p_filehandle, p_context, p_attrib_set,
                                      p_object_attributes);
}

fsal_status_t FSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */ )
{
  return fsal_functions.fsal_buildexportcontext(p_export_context, p_export_path,
                                                fs_specific_options);
}

fsal_status_t FSAL_CleanUpExportContext(fsal_export_context_t * p_export_context) /* IN */
{
  return fsal_functions.fsal_cleanupexportcontext(p_export_context);
}


fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_thr_context)
{
  return fsal_functions.fsal_initclientcontext(p_thr_context);
}

fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups /* IN */ )
{
  return fsal_functions.fsal_getclientcontext(p_thr_context, p_export_context, uid, gid,
                                              alt_groups, nb_alt_groups);
}

fsal_status_t FSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_create(p_parent_directory_handle, p_filename, p_context,
                                    accessmode, p_object_handle, p_object_attributes);
}

fsal_status_t FSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * p_object_handle,       /* OUT */
                         fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_mkdir(p_parent_directory_handle, p_dirname, p_context,
                                   accessmode, p_object_handle, p_object_attributes);
}

fsal_status_t FSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                        fsal_handle_t * p_dir_handle,   /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_link(p_target_handle, p_dir_handle, p_link_name, p_context,
                                  p_attributes);
}

fsal_status_t FSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                          fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_mknode(parentdir_handle, p_node_name, p_context, accessmode,
                                    nodetype, dev, p_object_handle, node_attributes);
}

fsal_status_t FSAL_opendir(fsal_handle_t * p_dir_handle,        /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * p_dir_descriptor,       /* OUT */
                           fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_opendir(p_dir_handle, p_context, p_dir_descriptor,
                                     p_dir_attributes);
}

fsal_status_t FSAL_readdir(fsal_dir_t * p_dir_descriptor,       /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * p_pdirent,   /* OUT */
                           fsal_cookie_t * p_end_position,      /* OUT */
                           fsal_count_t * p_nb_entries, /* OUT */
                           fsal_boolean_t * p_end_of_dir /* OUT */ )
{
  return fsal_functions.fsal_readdir(p_dir_descriptor, start_position, get_attr_mask,
                                     buffersize, p_pdirent, p_end_position, p_nb_entries,
                                     p_end_of_dir);
}

fsal_status_t FSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ )
{
  return fsal_functions.fsal_closedir(p_dir_descriptor);
}

fsal_status_t FSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_open_by_name(dirhandle, filename, p_context, openflags,
                                          file_descriptor, file_attributes);
}

fsal_status_t FSAL_open(fsal_handle_t * p_filehandle,   /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        fsal_file_t * p_file_descriptor,        /* OUT */
                        fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_open(p_filehandle, p_context, openflags, p_file_descriptor,
                                  p_file_attributes);
}

fsal_status_t FSAL_read(fsal_file_t * p_file_descriptor,        /* IN */
                        fsal_seek_t * p_seek_descriptor,        /* [IN] */
                        fsal_size_t buffer_size,        /* IN */
                        caddr_t buffer, /* OUT */
                        fsal_size_t * p_read_amount,    /* OUT */
                        fsal_boolean_t * p_end_of_file /* OUT */ )
{
  return fsal_functions.fsal_read(p_file_descriptor, p_seek_descriptor, buffer_size,
                                  buffer, p_read_amount, p_end_of_file);
}

fsal_status_t FSAL_write(fsal_file_t * p_file_descriptor,       /* IN */
                         fsal_seek_t * p_seek_descriptor,       /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * p_write_amount /* OUT */ )
{
  return fsal_functions.fsal_write(p_file_descriptor, p_seek_descriptor, buffer_size,
                                   buffer, p_write_amount);
}

fsal_status_t FSAL_close(fsal_file_t * p_file_descriptor /* IN */ )
{
  return fsal_functions.fsal_close(p_file_descriptor);
}

fsal_status_t FSAL_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_open_by_fileid(filehandle, fileid, p_context, openflags,
                                            file_descriptor, file_attributes);
}

fsal_status_t FSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid)
{
  return fsal_functions.fsal_close_by_fileid(file_descriptor, fileid);
}

fsal_status_t FSAL_static_fsinfo(fsal_handle_t * p_filehandle,  /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_staticfsinfo_t * p_staticinfo /* OUT */ )
{
  return fsal_functions.fsal_static_fsinfo(p_filehandle, p_context, p_staticinfo);
}

fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ )
{
  return fsal_functions.fsal_dynamic_fsinfo(p_filehandle, p_context, p_dynamicinfo);
}

fsal_status_t FSAL_Init(fsal_parameter_t * init_info /* IN */ )
{
#ifdef _USE_SHARED_FSAL
  /* Sanity check (only useful when dlopen is used, otherwise type are macros to FSAL specific types */
  if(fsal_consts.fsal_handle_t_size != sizeof(fsal_handle_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_handle_t do not match: %u |%u !!!!",
           fsal_consts.fsal_handle_t_size, sizeof(fsal_handle_t));
      exit(1);
    }
  if(fsal_consts.fsal_cookie_t_size != sizeof(fsal_cookie_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_cookie_t do not match: %u |%u !!!!",
           fsal_consts.fsal_cookie_t_size, sizeof(fsal_cookie_t));
      exit(1);
    }

#if 0
  if(fsal_consts.fsal_op_context_t_size != sizeof(fsal_op_context_t) - sizeof(void *))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_op_context_t do not match: %u |%u !!!!",
           fsal_consts.fsal_op_context_t_size,
           sizeof(fsal_op_context_t) - sizeof(void *));
      exit(1);
    }

  if(fsal_consts.fsal_export_context_t_size != sizeof(fsal_export_context_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_export_context_t do not match: %u |%u !!!!",
           fsal_consts.fsal_export_context_t_size, sizeof(fsal_export_context_t));
      exit(1);
    }

  if(fsal_consts.fsal_file_t_size != sizeof(fsal_file_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_file_context_t do not match: %u |%u !!!!",
           fsal_consts.fsal_file_t_size, sizeof(fsal_file_t));
      exit(1);
    }

  if(fsal_consts.fsal_lockdesc_t_size != sizeof(fsal_lockdesc_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_lockdesc_t do not match: %u |%u !!!!",
           fsal_consts.fsal_lockdesc_t_size, sizeof(fsal_lockdesc_t));
      exit(1);
    }

  if(fsal_consts.fsal_cred_t_size != sizeof(fsal_cred_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_cred_t do not match: %u |%u !!!!",
           fsal_consts.fsal_cred_t_size, sizeof(fsal_cred_t));
      exit(1);
    }

  if(fsal_consts.fs_specific_initinfo_t_size != sizeof(fs_specific_initinfo_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fs_specific_initinfo_t do not match: %u |%u !!!!",
           fsal_consts.fs_specific_initinfo_t_size, sizeof(fs_specific_initinfo_t));
      exit(1);
    }

  if(fsal_consts.fsal_dir_t_size != sizeof(fsal_dir_t))
    {
      LogMajor(COMPONENT_FSAL,
           "/!\\ MAJOR ERROR : Implementation Error, local and specific fsal_dir_t do not match: %u |%u !!!!",
           fsal_consts.fsal_dir_t_size, sizeof(fsal_dir_t));
      exit(1);
    }
#endif /* 0 */

#endif                          /* USE_SHARED_FSAL */

  return fsal_functions.fsal_init(init_info);
}

fsal_status_t FSAL_terminate()
{
  return fsal_functions.fsal_terminate();
}

fsal_status_t FSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  return fsal_functions.fsal_test_access(p_context, access_type, p_object_attributes);
}

fsal_status_t FSAL_setattr_access(fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t * candidate_attributes,    /* IN */
                                  fsal_attrib_list_t * object_attributes /* IN */ )
{
  return fsal_functions.fsal_setattr_access(p_context, candidate_attributes,
                                            object_attributes);
}

fsal_status_t FSAL_rename_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattrsrc, /* IN */
                                 fsal_attrib_list_t * pattrdest)        /* IN */
{
  return fsal_functions.fsal_rename_access(pcontext, pattrsrc, pattrdest);
}

fsal_status_t FSAL_create_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  return fsal_functions.fsal_create_access(pcontext, pattr);
}

fsal_status_t FSAL_unlink_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  return fsal_functions.fsal_unlink_access(pcontext, pattr);
}

fsal_status_t FSAL_link_access(fsal_op_context_t * pcontext,    /* IN */
                               fsal_attrib_list_t * pattr)      /* IN */
{
  return fsal_functions.fsal_link_access(pcontext, pattr);
}

fsal_status_t FSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                               fsal_attrib_list_t * pnew_attr,
                               fsal_attrib_list_t * presult_attr)
{
  return fsal_functions.fsal_merge_attrs(pinit_attr, pnew_attr, presult_attr);
}

fsal_status_t FSAL_lookup(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_lookup(p_parent_directory_handle, p_filename, p_context,
                                    p_object_handle, p_object_attributes);
}

fsal_status_t FSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_lookuppath(p_path, p_context, object_handle,
                                        p_object_attributes);
}

fsal_status_t FSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t *
                                  p_fsroot_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_lookupjunction(p_junction_handle, p_context, p_fsoot_handle,
                                            p_fsroot_attributes);
}

fsal_status_t FSAL_lock(fsal_file_t * obj_handle,
                        fsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  return fsal_functions.fsal_lock(obj_handle, ldesc, blocking);
}

fsal_status_t FSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                              fsal_lockparam_t * lock_info /* IN */ )
{
  return fsal_functions.fsal_changelock(lock_descriptor, lock_info);
}

fsal_status_t FSAL_unlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  return fsal_functions.fsal_unlock(obj_handle, ldesc);
}

fsal_status_t FSAL_getlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  return fsal_functions.fsal_getlock(obj_handle, ldesc);
}

fsal_status_t FSAL_CleanObjectResources(fsal_handle_t * in_fsal_handle)
{
  return fsal_functions.fsal_cleanobjectresources(in_fsal_handle);
}

fsal_status_t FSAL_set_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type,    /* IN */
                             fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquota,     /* IN */
                             fsal_quota_t * presquota)  /* OUT */
{
  return fsal_functions.fsal_set_quota(pfsal_path, quota_type, fsal_uid, pquota,
                                       presquota);
}

fsal_status_t FSAL_get_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type,    /* IN */
                             fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquota)     /* OUT */
{
  return fsal_functions.fsal_get_quota(pfsal_path, quota_type, fsal_uid, pquota);
}

fsal_status_t FSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt /* IN */ )
{
  return fsal_functions.fsal_rcp(filehandle, p_context, p_local_path, transfer_opt);
}

fsal_status_t FSAL_rcp_by_fileid(fsal_handle_t * filehandle,    /* IN */
                                 fsal_u64_t fileid,     /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_path_t * p_local_path,    /* IN */
                                 fsal_rcpflag_t transfer_opt /* IN */ )
{
  return fsal_functions.fsal_rcp_by_fileid(filehandle, fileid, p_context, p_local_path,
                                           transfer_opt);
}

fsal_status_t FSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * p_new_parentdir_handle,       /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * p_src_dir_attributes,    /* [ IN/OUT ] */
                          fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_rename(p_old_parentdir_handle, p_old_name,
                                    p_new_parentdir_handle, p_new_name, p_context,
                                    p_src_dir_attributes, p_tgt_dir_attributes);
}

void FSAL_get_stats(fsal_statistics_t * stats,  /* OUT */
                    fsal_boolean_t reset /* IN */ )
{
  return fsal_functions.fsal_get_stats(stats, reset);
}

fsal_status_t FSAL_readlink(fsal_handle_t * p_linkhandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_path_t * p_link_content,       /* OUT */
                            fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_readlink(p_linkhandle, p_context, p_link_content,
                                      p_link_attributes);
}

fsal_status_t FSAL_symlink(fsal_handle_t * p_parent_directory_handle,   /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored) */
                           fsal_handle_t * p_link_handle,       /* OUT */
                           fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ )
{
  return fsal_functions.fsal_symlink(p_parent_directory_handle, p_linkname, p_linkcontent,
                                     p_context, accessmode, p_link_handle,
                                     p_link_attributes);
}

int FSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                   fsal_status_t * status)
{
  return fsal_functions.fsal_handlecmp(handle1, handle2, status);
}

unsigned int FSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size)
{
  return fsal_functions.fsal_handle_to_hashindex(p_handle, cookie, alphabet_len,
                                                 index_size);
}

unsigned int FSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, unsigned int cookie)
{
  return fsal_functions.fsal_handle_to_rbtindex(p_handle, cookie);
}

fsal_status_t FSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t output_type,  /* IN */
                                fsal_handle_t * p_in_fsal_handle,       /* IN */
                                caddr_t out_buff /* OUT */ )
{
  return fsal_functions.fsal_digesthandle(p_expcontext, output_type, p_in_fsal_handle,
                                          out_buff);
}

fsal_status_t FSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t in_type,      /* IN */
                                caddr_t in_buff,        /* IN */
                                fsal_handle_t * p_out_fsal_handle /* OUT */ )
{
  return fsal_functions.fsal_expandhandle(p_expcontext, in_type, in_buff,
                                          p_out_fsal_handle);
}

fsal_status_t FSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_setdefault_fsal_parameter(out_parameter);
}

fsal_status_t FSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_setdefault_fs_common_parameter(out_parameter);
}

fsal_status_t FSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_setdefault_fs_specific_parameter(out_parameter);
}

fsal_status_t FSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                 fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_load_fsal_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t FSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                      fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_load_fs_common_parameter_from_conf(in_config, out_parameter);
}

fsal_status_t FSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                        fsal_parameter_t * out_parameter)
{
  return fsal_functions.fsal_load_fs_specific_parameter_from_conf(in_config,
                                                                  out_parameter);
}

fsal_status_t FSAL_truncate(fsal_handle_t * p_filehandle,
                            fsal_op_context_t * p_context,
                            fsal_size_t length,
                            fsal_file_t * file_descriptor,
                            fsal_attrib_list_t * p_object_attributes)
{
  return fsal_functions.fsal_truncate(p_filehandle, p_context, length, file_descriptor,
                                      p_object_attributes);
}

fsal_status_t FSAL_unlink(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t *
                          p_parent_directory_attributes /* [IN/OUT ] */ )
{
  return fsal_functions.fsal_unlink(p_parent_directory_handle, p_object_name, p_context,
                                    p_parent_directory_attributes);
}

char *FSAL_GetFSName()
{
  return fsal_functions.fsal_getfsname();
}

fsal_status_t FSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 unsigned int xattr_id, /* IN */
                                 fsal_attrib_list_t * p_attrs)
{
  return fsal_functions.fsal_getxattrattrs(p_objecthandle, p_context, xattr_id, p_attrs);
}

fsal_status_t FSAL_ListXAttrs(fsal_handle_t * p_objecthandle,   /* IN */
                              unsigned int cookie,      /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_xattrent_t * xattrs_tab,     /* IN/OUT */
                              unsigned int xattrs_tabsize,      /* IN */
                              unsigned int *p_nb_returned,      /* OUT */
                              int *end_of_list /* OUT */ )
{
  return fsal_functions.fsal_listxattrs(p_objecthandle, cookie, p_context,
                                        xattrs_tab, xattrs_tabsize, p_nb_returned,
                                        end_of_list);
}

fsal_status_t FSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN/OUT */
                                     size_t buffer_size,        /* IN */
                                     size_t * p_output_size /* OUT */ )
{
  return fsal_functions.fsal_getxattrvaluebyid(p_objecthandle, xattr_id, p_context,
                                               buffer_addr, buffer_size, p_output_size);
}

fsal_status_t FSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    unsigned int *pxattr_id /* OUT */ )
{
  return fsal_functions.fsal_getxattridbyname(p_objecthandle, xattr_name, p_context,
                                              pxattr_id);
}

fsal_status_t FSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       caddr_t buffer_addr,     /* IN/OUT */
                                       size_t buffer_size,      /* IN */
                                       size_t * p_output_size /* OUT */ )
{
  return fsal_functions.fsal_getxattrvaluebyname(p_objecthandle, xattr_name, p_context,
                                                 buffer_addr, buffer_size, p_output_size);
}

fsal_status_t FSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                 const fsal_name_t * xattr_name,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 caddr_t buffer_addr,   /* IN */
                                 size_t buffer_size,    /* IN */
                                 int create /* IN */ )
{
  return fsal_functions.fsal_setxattrvalue(p_objecthandle, xattr_name, p_context,
                                           buffer_addr, buffer_size, create);
}

fsal_status_t FSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size /* IN */ )
{
  return fsal_functions.fsal_setxattrvaluebyid(p_objecthandle, xattr_id, p_context,
                                               buffer_addr, buffer_size);
}

fsal_status_t FSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   unsigned int xattr_id)       /* IN */
{
  return fsal_functions.fsal_removexattrbyid(p_objecthandle, p_context, xattr_id);
}

fsal_status_t FSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     const fsal_name_t * xattr_name)    /* IN */
{
  return fsal_functions.fsal_removexattrbyname(p_objecthandle, p_context, xattr_name);
}

unsigned int FSAL_GetFileno(fsal_file_t * pfile)
{
  return fsal_functions.fsal_getfileno(pfile);
}

fsal_status_t FSAL_getextattrs( fsal_handle_t * p_filehandle, /* IN */
                                fsal_op_context_t * p_context,        /* IN */
                                fsal_extattrib_list_t * p_object_attributes /* OUT */)
{
   return fsal_functions.fsal_getextattrs( p_filehandle, p_context, p_object_attributes ) ;
}

#ifdef _USE_SHARED_FSAL
int FSAL_LoadLibrary(char *path)
{
  void *handle;
  char *error;

  LogEvent(COMPONENT_FSAL, "Load shared FSAL : %s", path);

  if((handle = dlopen(path, RTLD_LAZY)) == NULL)
    {
      LogMajor(COMPONENT_FSAL, "FSAL_LoadLibrary: could not load fsal: %s", dlerror());
      return 0;
    }

  /* Clear any existing error : dlerror will be used to check if dlsym succeeded or not */
  dlerror();

  /* Map FSAL_GetFunctions */
  *(void **)(&getfunctions) = dlsym(handle, "FSAL_GetFunctions");
  if((error = dlerror()) != NULL)
    {
      LogMajor(COMPONENT_FSAL, "FSAL_LoadLibrary: Could not map symbol FSAL_GetFunctions:%s", error);
      return 0;
    }
  /* Map FSAL_GetConsts */
  *(void **)(&getconsts) = dlsym(handle, "FSAL_GetConsts");
  if((error = dlerror()) != NULL)
    {
      LogMajor(COMPONENT_FSAL, "FSAL_LoadLibrary: Could not map symbol FSAL_GetConsts:%s", error);
      return 0;
    }

  return 1;
}                               /* FSAL_LoadLibrary */

void FSAL_LoadFunctions(void)
{
  fsal_functions = (*getfunctions) ();
}

void FSAL_LoadConsts(void)
{
  fsal_consts = (*getconsts) ();
}

#else
int FSAL_LoadLibrary(char *path)
{
  return 1;                     /* Does nothing, this is the "static" case */
}

void FSAL_LoadFunctions(void)
{
  fsal_functions = FSAL_GetFunctions();
}

void FSAL_LoadConsts(void)
{
  fsal_consts = FSAL_GetConsts();
}

#endif
