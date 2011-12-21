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

/* filesystem operations */
extern struct ganefuse_operations *p_fs_ops;

/* filesystem opaque data */
extern void *fs_user_data;
extern void *fs_private_data;

#endif

struct ganefuse
{
  /* unused for now */
  void *reserved;
};

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

/**
 * This function sets the current context for a filesystem operation,
 * so it can be retrieved with fuse_get_context().
 * The structure pointed by p_ctx must stay allocated and kept unchanged
 * during the FS call.
 */
int fsal_set_thread_context(fsal_op_context_t * p_ctx);

/**
 * This function retrieves the last context associated to a thread.
 */
fsal_op_context_t *fsal_get_thread_context();

/* used for generating inode numbers for FS which don't have some */
static inline unsigned long hash_peer(ino_t parent_inode, char *name)
{
  unsigned long hash;
  char *curr;

  hash = 1;

  for(curr = name; *curr != '\0'; curr++)
    hash = ((hash << 5) - hash + (unsigned long)(*curr));

  return (hash ^ (unsigned long)parent_inode);
}

/* All the call to FSAL to be wrapped */
fsal_status_t FUSEFSAL_access(fsal_handle_t * p_object_handle,      /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessflags_t access_type,   /* IN */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t FUSEFSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_attrib_set,      /* IN */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options /* IN */ );

fsal_status_t FUSEFSAL_InitClientContext(fsal_op_context_t * p_thr_context);

fsal_status_t FUSEFSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                        fsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t FUSEFSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                             fsal_name_t * p_dirname,   /* IN */
                             fsal_op_context_t * p_context, /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_handle_t * p_object_handle,       /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                            fsal_handle_t * p_dir_handle,   /* IN */
                            fsal_name_t * p_link_name,  /* IN */
                            fsal_op_context_t * p_context,  /* IN */
                            fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_node_name,        /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_nodetype_t nodetype, /* IN */
                              fsal_dev_t * dev, /* IN */
                              fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                              fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_opendir(fsal_handle_t * p_dir_handle,        /* IN */
                               fsal_op_context_t * p_context,       /* IN */
                               fsal_dir_t * p_dir_descriptor,       /* OUT */
                               fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_readdir(fsal_dir_t * p_dir_descriptor,       /* IN */
                               fsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * p_pdirent,       /* OUT */
                               fsal_cookie_t * p_end_position,      /* OUT */
                               fsal_count_t * p_nb_entries,     /* OUT */
                               fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t FUSEFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t FUSEFSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    fsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    fsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_open(fsal_handle_t * p_filehandle,   /* IN */
                            fsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            fsal_file_t * p_file_descriptor,        /* OUT */
                            fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_read(fsal_file_t * p_file_descriptor,        /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * p_read_amount,        /* OUT */
                            fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t FUSEFSAL_write(fsal_file_t * p_file_descriptor,       /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t FUSEFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t FUSEFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                      fsal_op_context_t * p_context,        /* IN */
                                      fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t FUSEFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t FUSEFSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                                   fsal_accessflags_t access_type,      /* IN */
                                   fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t FUSEFSAL_lookup(fsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  fsal_op_context_t * p_context,    /* IN */
                                  fsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                      fsal_op_context_t * p_context,        /* IN */
                                      fsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t *
                                      p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                           fsal_op_context_t * p_context,   /* IN */
                           fsal_path_t * p_local_path,  /* IN */
                           fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t FUSEFSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
                              fsal_name_t * p_old_name, /* IN */
                              fsal_handle_t * p_new_parentdir_handle,       /* IN */
                              fsal_name_t * p_new_name, /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t * p_src_dir_attributes,        /* [ IN/OUT ] */
                              fsal_attrib_list_t *
                              p_tgt_dir_attributes /* [ IN/OUT ] */ );

void FUSEFSAL_get_stats(fsal_statistics_t * stats,      /* OUT */
                        fsal_boolean_t reset /* IN */ );

fsal_status_t FUSEFSAL_readlink(fsal_handle_t * p_linkhandle,       /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_path_t * p_link_content,   /* OUT */
                                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_symlink(fsal_handle_t * p_parent_directory_handle,   /* IN */
                               fsal_name_t * p_linkname,        /* IN */
                               fsal_path_t * p_linkcontent,     /* IN */
                               fsal_op_context_t * p_context,       /* IN */
                               fsal_accessmode_t accessmode,    /* IN (ignored) */
                               fsal_handle_t * p_link_handle,       /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int FUSEFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                       fsal_status_t * status);

unsigned int FUSEFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size);

unsigned int FUSEFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                         unsigned int cookie);

fsal_status_t FUSEFSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    fsal_handle_t * p_in_fsal_handle,       /* IN */
                                    caddr_t out_buff /* OUT */ );

fsal_status_t FUSEFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    fsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t FUSEFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t FUSEFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t FUSEFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t FUSEFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter);

fsal_status_t FUSEFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t *
                                                          out_parameter);

fsal_status_t FUSEFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter);

fsal_status_t FUSEFSAL_truncate(fsal_handle_t * p_filehandle,       /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_size_t length,     /* IN */
                                fsal_file_t * file_descriptor,      /* Unused in this FSAL */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t FUSEFSAL_unlink(fsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_object_name,      /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t *
                              p_parent_directory_attributes /* [IN/OUT ] */ );

char *FUSEFSAL_GetFSName();

fsal_status_t FUSEFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                     fsal_op_context_t * p_context, /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_attrib_list_t * p_attrs);

fsal_status_t FUSEFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,   /* IN */
                                  unsigned int cookie,  /* IN */
                                  fsal_op_context_t * p_context,    /* IN */
                                  fsal_xattrent_t * xattrs_tab, /* IN/OUT */
                                  unsigned int xattrs_tabsize,  /* IN */
                                  unsigned int *p_nb_returned,  /* OUT */
                                  int *end_of_list /* OUT */ );

fsal_status_t FUSEFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN/OUT */
                                         size_t buffer_size,    /* IN */
                                         size_t * p_output_size /* OUT */ );

fsal_status_t FUSEFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                        const fsal_name_t * xattr_name, /* IN */
                                        fsal_op_context_t * p_context,      /* IN */
                                        unsigned int *pxattr_id /* OUT */ );

fsal_status_t FUSEFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                           const fsal_name_t * xattr_name,      /* IN */
                                           fsal_op_context_t * p_context,   /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

fsal_status_t FUSEFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                     const fsal_name_t * xattr_name,    /* IN */
                                     fsal_op_context_t * p_context, /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size,        /* IN */
                                     int create /* IN */ );

fsal_status_t FUSEFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size /* IN */ );

fsal_status_t FUSEFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                       fsal_op_context_t * p_context,       /* IN */
                                       unsigned int xattr_id) /* IN */ ;

fsal_status_t FUSEFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                         fsal_op_context_t * p_context,     /* IN */
                                         const fsal_name_t * xattr_name) /* IN */ ;

unsigned int FUSEFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t FUSEFSAL_sync(fsal_file_t * p_file_descriptor     /* IN */);

