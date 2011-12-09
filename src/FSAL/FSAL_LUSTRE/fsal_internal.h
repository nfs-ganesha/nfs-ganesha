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
fsal_status_t LUSTREFSAL_access(fsal_handle_t * p_object_handle,  /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_accessflags_t access_type, /* IN */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_getattrs(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t LUSTREFSAL_setattrs(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_attrib_set,    /* IN */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_BuildExportContext(fsal_export_context_t * p_export_context,     /* OUT */
                                            fsal_path_t * p_export_path,        /* IN */
                                            char *fs_specific_options /* IN */ );

fsal_status_t LUSTREFSAL_create(fsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_filename,       /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                fsal_handle_t * p_object_handle,  /* OUT */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_mkdir(fsal_handle_t * p_parent_directory_handle, /* IN */
                               fsal_name_t * p_dirname, /* IN */
                               fsal_op_context_t * p_context,     /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * p_object_handle,   /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_link(fsal_handle_t * p_target_handle,    /* IN */
                              fsal_handle_t * p_dir_handle,       /* IN */
                              fsal_name_t * p_link_name,        /* IN */
                              fsal_op_context_t * p_context,      /* IN */
                              fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_mknode(fsal_handle_t * parentdir_handle, /* IN */
                                fsal_name_t * p_node_name,      /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_accessmode_t accessmode,   /* IN */
                                fsal_nodetype_t nodetype,       /* IN */
                                fsal_dev_t * dev,       /* IN */
                                fsal_handle_t * p_object_handle,  /* OUT (handle to the created node) */
                                fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_opendir(fsal_handle_t * p_dir_handle,    /* IN */
                                 fsal_op_context_t * p_context,   /* IN */
                                 fsal_dir_t * p_dir_descriptor,   /* OUT */
                                 fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_readdir(fsal_dir_t * p_dir_descriptor,   /* IN */
                                 fsal_cookie_t start_position,    /* IN */
                                 fsal_attrib_mask_t get_attr_mask,      /* IN */
                                 fsal_mdsize_t buffersize,      /* IN */
                                 fsal_dirent_t * p_pdirent,     /* OUT */
                                 fsal_cookie_t * p_end_position,  /* OUT */
                                 fsal_count_t * p_nb_entries,   /* OUT */
                                 fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t LUSTREFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t LUSTREFSAL_open_by_name(fsal_handle_t * dirhandle,  /* IN */
                                      fsal_name_t * filename,   /* IN */
                                      fsal_op_context_t * p_context,      /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      fsal_file_t * file_descriptor,      /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_open(fsal_handle_t * p_filehandle,       /* IN */
                              fsal_op_context_t * p_context,      /* IN */
                              fsal_openflags_t openflags,       /* IN */
                              fsal_file_t * p_file_descriptor,    /* OUT */
                              fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_read(fsal_file_t * p_file_descriptor,    /* IN */
                              fsal_seek_t * p_seek_descriptor,  /* [IN] */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* OUT */
                              fsal_size_t * p_read_amount,      /* OUT */
                              fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t LUSTREFSAL_write(fsal_file_t * p_file_descriptor,   /* IN */
                               fsal_seek_t * p_seek_descriptor, /* IN */
                               fsal_size_t buffer_size, /* IN */
                               caddr_t buffer,  /* IN */
                               fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t LUSTREFSAL_sync(fsal_file_t * p_file_descriptor  /* IN */);

fsal_status_t LUSTREFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t LUSTREFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,     /* IN */
                                        fsal_op_context_t * p_context,    /* IN */
                                        fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t LUSTREFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t LUSTREFSAL_test_access(fsal_op_context_t * p_context,       /* IN */
                                     fsal_accessflags_t access_type,    /* IN */
                                     fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t LUSTREFSAL_lookup(fsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_filename,       /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_handle_t * p_object_handle,  /* OUT */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_lookupPath(fsal_path_t * p_path,       /* IN */
                                    fsal_op_context_t * p_context,        /* IN */
                                    fsal_handle_t * object_handle,        /* OUT */
                                    fsal_attrib_list_t *
                                    p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_lookupJunction(fsal_handle_t * p_junction_handle,        /* IN */
                                        fsal_op_context_t * p_context,    /* IN */
                                        fsal_handle_t * p_fsoot_handle,   /* OUT */
                                        fsal_attrib_list_t *
                                        p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_rcp(fsal_handle_t * filehandle,  /* IN */
                             fsal_op_context_t * p_context,       /* IN */
                             fsal_path_t * p_local_path,        /* IN */
                             fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t LUSTREFSAL_rename(fsal_handle_t * p_old_parentdir_handle,   /* IN */
                                fsal_name_t * p_old_name,       /* IN */
                                fsal_handle_t * p_new_parentdir_handle,   /* IN */
                                fsal_name_t * p_new_name,       /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t * p_src_dir_attributes,      /* [ IN/OUT ] */
                                fsal_attrib_list_t *
                                p_tgt_dir_attributes /* [ IN/OUT ] */ );

void LUSTREFSAL_get_stats(fsal_statistics_t * stats,    /* OUT */
                          fsal_boolean_t reset /* IN */ );

fsal_status_t LUSTREFSAL_readlink(fsal_handle_t * p_linkhandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_path_t * p_link_content, /* OUT */
                                  fsal_attrib_list_t *
                                  p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_symlink(fsal_handle_t * p_parent_directory_handle,       /* IN */
                                 fsal_name_t * p_linkname,      /* IN */
                                 fsal_path_t * p_linkcontent,   /* IN */
                                 fsal_op_context_t * p_context,   /* IN */
                                 fsal_accessmode_t accessmode,  /* IN (ignored) */
                                 fsal_handle_t * p_link_handle,   /* OUT */
                                 fsal_attrib_list_t *
                                 p_link_attributes /* [ IN/OUT ] */ );

int LUSTREFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                         fsal_status_t * status);

unsigned int LUSTREFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                            unsigned int cookie,
                                            unsigned int alphabet_len,
                                            unsigned int index_size);

unsigned int LUSTREFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                           unsigned int cookie);

fsal_status_t LUSTREFSAL_DigestHandle(fsal_export_context_t * p_expcontext,       /* IN */
                                      fsal_digesttype_t output_type,    /* IN */
                                      fsal_handle_t * p_in_fsal_handle,   /* IN */
                                      caddr_t out_buff /* OUT */ );

fsal_status_t LUSTREFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,       /* IN */
                                      fsal_digesttype_t in_type,        /* IN */
                                      caddr_t in_buff,  /* IN */
                                      fsal_handle_t * p_out_fsal_handle /* OUT */ );

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

fsal_status_t LUSTREFSAL_truncate(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_size_t length,   /* IN */
                                  fsal_file_t * file_descriptor,  /* Unused in this FSAL */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t LUSTREFSAL_unlink(fsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_object_name,    /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t *
                                p_parent_directory_attributes /* [IN/OUT ] */ );

char *LUSTREFSAL_GetFSName();

fsal_status_t LUSTREFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,    /* IN */
                                       fsal_op_context_t * p_context,     /* IN */
                                       unsigned int xattr_id,   /* IN */
                                       fsal_attrib_list_t * p_attrs);

fsal_status_t LUSTREFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,       /* IN */
                                    unsigned int cookie,        /* IN */
                                    fsal_op_context_t * p_context,        /* IN */
                                    fsal_xattrent_t * xattrs_tab,       /* IN/OUT */
                                    unsigned int xattrs_tabsize,        /* IN */
                                    unsigned int *p_nb_returned,        /* OUT */
                                    int *end_of_list /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,        /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           fsal_op_context_t * p_context, /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle, /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          fsal_op_context_t * p_context,  /* IN */
                                          unsigned int *pxattr_id /* OUT */ );

fsal_status_t LUSTREFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,      /* IN */
                                             const fsal_name_t * xattr_name,    /* IN */
                                             fsal_op_context_t * p_context,       /* IN */
                                             caddr_t buffer_addr,       /* IN/OUT */
                                             size_t buffer_size,        /* IN */
                                             size_t * p_output_size /* OUT */ );

fsal_status_t LUSTREFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,    /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,     /* IN */
                                       caddr_t buffer_addr,     /* IN */
                                       size_t buffer_size,      /* IN */
                                       int create /* IN */ );

fsal_status_t LUSTREFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,        /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           fsal_op_context_t * p_context, /* IN */
                                           caddr_t buffer_addr, /* IN */
                                           size_t buffer_size /* IN */ );

fsal_status_t LUSTREFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,  /* IN */
                                         fsal_op_context_t * p_context,   /* IN */
                                         unsigned int xattr_id) /* IN */ ;

fsal_status_t LUSTREFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,        /* IN */
                                           fsal_op_context_t * p_context, /* IN */
                                           const fsal_name_t * xattr_name) /* IN */ ;

fsal_status_t LUSTREFSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                                  fsal_handle_t     * p_filehandle,        /* IN */
                                  fsal_op_context_t * p_context,           /* IN */
                                  void              * p_owner,             /* IN (opaque to FSAL) */
                                  fsal_lock_op_t      lock_op,             /* IN */
                                  fsal_lock_param_t   request_lock,        /* IN */
                                  fsal_lock_param_t * conflicting_lock)    /* OUT */ ;

unsigned int LUSTREFSAL_GetFileno(fsal_file_t * pfile);
