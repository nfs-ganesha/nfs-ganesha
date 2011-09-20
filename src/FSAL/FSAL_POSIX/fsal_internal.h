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
          FSAL_ATTR_FSID     | FSAL_ATTR_ACL      | FSAL_ATTR_FILEID    | \
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
extern fsal_posixdb_conn_params_t global_posixdb_params;

#endif

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info,
                                        posixfs_specific_initinfo_t * fs_specific_info);

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

fsal_status_t fsal_internal_posix2posixdb_fileinfo(struct stat *buffstat,
                                                   fsal_posixdb_fileinfo_t * info);

/**
 *  Check if 2 fsal_posixdb_fileinfo_t are consistent
 */
fsal_status_t fsal_internal_posixdb_checkConsistency(fsal_posixdb_fileinfo_t * p_info_fs,
                                                     fsal_posixdb_fileinfo_t * p_info_db,
                                                     int *p_result);

/**
 * Add a new entry to the database. Remove an already existing handle if it's not consistent
 */
fsal_status_t fsal_internal_posixdb_add_entry(fsal_posixdb_conn * p_conn,
                                              fsal_name_t * p_filename,
                                              fsal_posixdb_fileinfo_t * p_info,
                                              posixfsal_handle_t * p_dir_handle,
                                              posixfsal_handle_t * p_new_handle);

/**
 * Append a fsal_name to an fsal_path to have the full path of a file from its name and its parent path
 */
fsal_status_t fsal_internal_appendFSALNameToFSALPath(fsal_path_t * p_path,
                                                     fsal_name_t * p_name);

/**
 * Get a valid path associated to an handle.
 * The function selects many paths from the DB and return the first valid one. If is_dir is set, then only 1 path will be constructed from the database.
 */
fsal_status_t fsal_internal_getPathFromHandle(posixfsal_op_context_t * p_context,    /* IN */
                                              posixfsal_handle_t * p_handle, /* IN */
                                              int is_dir,       /* IN */
                                              fsal_path_t * p_fsalpath, /* OUT */
                                              struct stat *p_buffstat /* OUT */ );

/** 
 * @brief Get the handle of a file, knowing its name and its parent dir
 * 
 * @param p_context 
 * @param p_parent_dir_handle 
 *    Handle of the parent directory
 * @param p_fsalname 
 *    Name of the object
 * @param p_infofs
 *    Information about the file (taken from the filesystem) to be compared to information stored in database
 * @param p_object_handle 
 *    Handle of the file.
 * 
 * @return
 *    ERR_FSAL_NOERR, if no error
 *    Anothere error code else.
 */
fsal_status_t fsal_internal_getInfoFromName(posixfsal_op_context_t * p_context,      /* IN */
                                            posixfsal_handle_t * p_parent_dir_handle,        /* IN */
                                            fsal_name_t * p_fsalname,   /* IN */
                                            fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                            posixfsal_handle_t * p_object_handle);   /* OUT */

fsal_status_t fsal_internal_getInfoFromChildrenList(posixfsal_op_context_t * p_context,      /* IN */
                                                    posixfsal_handle_t * p_parent_dir_handle,        /* IN */
                                                    fsal_name_t * p_fsalname,   /* IN */
                                                    fsal_posixdb_fileinfo_t * p_infofs, /* IN */
                                                    fsal_posixdb_child * p_children,    /* IN */
                                                    unsigned int children_count,        /* IN */
                                                    posixfsal_handle_t * p_object_handle);   /* OUT */

/**
 *  test the access to a file knowing its POSIX attributes (struct stat) OR its FSAL attributes (fsal_attrib_list_t).
 *
 */
fsal_status_t fsal_internal_testAccess(posixfsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat *p_buffstat, /* IN */
                                       fsal_attrib_list_t *
                                       p_object_attributes /* IN */ );

int fsal_internal_path2fsname(char *rpath, char *fs_spec);

/* All the call to FSAL to be wrapped */
fsal_status_t POSIXFSAL_access(fsal_handle_t * object_handle,    /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_getattrs(fsal_handle_t * filehandle,     /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t POSIXFSAL_setattrs(fsal_handle_t * filehandle,     /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_attrib_list_t * p_attrib_set,     /* IN */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_BuildExportContext(fsal_export_context_t * p_export_context,       /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options /* IN */ );

fsal_status_t POSIXFSAL_InitClientContext(fsal_op_context_t * p_thr_context);

fsal_status_t POSIXFSAL_create(fsal_handle_t * parent_directory_handle,  /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * object_handle,    /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_mkdir(fsal_handle_t * parent_directory_handle,   /* IN */
                              fsal_name_t * p_dirname,  /* IN */
                              fsal_op_context_t * context,       /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_handle_t * object_handle,     /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_link(fsal_handle_t * target_handle,      /* IN */
                             fsal_handle_t * dir_handle, /* IN */
                             fsal_name_t * p_link_name, /* IN */
                             fsal_op_context_t * context,        /* IN */
                             fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_mknode(fsal_handle_t * parentdir_handle,   /* IN */
                               fsal_name_t * p_node_name,       /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_nodetype_t nodetype,        /* IN */
                               fsal_dev_t * dev,        /* IN */
                               fsal_handle_t * object_handle,    /* OUT (handle to the created node) */
                               fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_opendir(fsal_handle_t * dir_handle,      /* IN */
                                fsal_op_context_t * context,     /* IN */
                                fsal_dir_t * dir_descriptor,     /* OUT */
                                fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_readdir(fsal_dir_t * dir_descriptor,     /* IN */
                                fsal_cookie_t start_pos,      /* IN */
                                fsal_attrib_mask_t get_attr_mask,       /* IN */
                                fsal_mdsize_t buffersize,       /* IN */
                                fsal_dirent_t * p_pdirent,      /* OUT */
                                fsal_cookie_t * end_position,    /* OUT */
                                fsal_count_t * p_nb_entries,    /* OUT */
                                fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t POSIXFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t POSIXFSAL_open_by_name(fsal_handle_t * dirhandle,    /* IN */
                                     fsal_name_t * filename,    /* IN */
                                     fsal_op_context_t * p_context,        /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     fsal_file_t * file_descriptor,        /* OUT */
                                     fsal_attrib_list_t *
                                     file_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_open(fsal_handle_t * p_filehandle, /* IN */
                             fsal_op_context_t * p_context,        /* IN */
                             fsal_openflags_t openflags,        /* IN */
                             fsal_file_t * p_file_descriptor,      /* OUT */
                             fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_read(fsal_file_t * p_file_descriptor,      /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* [IN] */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* OUT */
                             fsal_size_t * p_read_amount,       /* OUT */
                             fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t POSIXFSAL_write(fsal_file_t * p_file_descriptor,     /* IN */
                              fsal_seek_t * p_seek_descriptor,  /* IN */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* IN */
                              fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t POSIXFSAL_sync(fsal_file_t * p_file_descriptor     /* IN */);

fsal_status_t POSIXFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t POSIXFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,       /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t POSIXFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t POSIXFSAL_test_access(fsal_op_context_t * p_context, /* IN */
                                    fsal_accessflags_t access_type,     /* IN */
                                    fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t POSIXFSAL_setattr_access(fsal_op_context_t * p_context,      /* IN */
                                       fsal_attrib_list_t * candidate_attributes,       /* IN */
                                       fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t POSIXFSAL_rename_access(fsal_op_context_t * pcontext,        /* IN */
                                      fsal_attrib_list_t * pattrsrc,    /* IN */
                                      fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t POSIXFSAL_create_access(fsal_op_context_t * pcontext,        /* IN */
                                      fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t POSIXFSAL_unlink_access(fsal_op_context_t * pcontext,        /* IN */
                                      fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t POSIXFSAL_link_access(fsal_op_context_t * pcontext,  /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t POSIXFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                    fsal_attrib_list_t * pnew_attr,
                                    fsal_attrib_list_t * presult_attr);

fsal_status_t POSIXFSAL_lookup(fsal_handle_t * p_parent_directory_handle,  /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_handle_t * p_object_handle,    /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_lookupPath(fsal_path_t * p_path,        /* IN */
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_handle_t * object_handle,  /* OUT */
                                   fsal_attrib_list_t *
                                   p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_lookupJunction(fsal_handle_t * p_junction_handle,  /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_handle_t * p_fsoot_handle,     /* OUT */
                                       fsal_attrib_list_t *
                                       p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_set_quota(fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota,        /* IN */
                                  fsal_quota_t * presquota);    /* OUT */

fsal_status_t POSIXFSAL_get_quota(fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota);       /* OUT */

fsal_status_t POSIXFSAL_rcp(fsal_handle_t * filehandle,    /* IN */
                            fsal_op_context_t * p_context, /* IN */
                            fsal_path_t * p_local_path, /* IN */
                            fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t POSIXFSAL_rename(fsal_handle_t * p_old_parentdir_handle,     /* IN */
                               fsal_name_t * p_old_name,        /* IN */
                               fsal_handle_t * p_new_parentdir_handle,     /* IN */
                               fsal_name_t * p_new_name,        /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_attrib_list_t * p_src_dir_attributes,       /* [ IN/OUT ] */
                               fsal_attrib_list_t *
                               p_tgt_dir_attributes /* [ IN/OUT ] */ );

void POSIXFSAL_get_stats(fsal_statistics_t * stats,     /* OUT */
                         fsal_boolean_t reset /* IN */ );

fsal_status_t POSIXFSAL_readlink(fsal_handle_t * p_linkhandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_path_t * p_link_content,  /* OUT */
                                 fsal_attrib_list_t *
                                 p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_symlink(fsal_handle_t * p_parent_directory_handle, /* IN */
                                fsal_name_t * p_linkname,       /* IN */
                                fsal_path_t * p_linkcontent,    /* IN */
                                fsal_op_context_t * p_context,     /* IN */
                                fsal_accessmode_t accessmode,   /* IN (ignored) */
                                fsal_handle_t * p_link_handle,     /* OUT */
                                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int POSIXFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                        fsal_status_t * status);

unsigned int POSIXFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                           unsigned int cookie,
                                           unsigned int alphabet_len,
                                           unsigned int index_size);

unsigned int POSIXFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                          unsigned int cookie);

fsal_status_t POSIXFSAL_DigestHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t output_type,     /* IN */
                                     fsal_handle_t * p_in_fsal_handle,     /* IN */
                                     caddr_t out_buff /* OUT */ );

fsal_status_t POSIXFSAL_ExpandHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t in_type, /* IN */
                                     caddr_t in_buff,   /* IN */
                                     fsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t POSIXFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t POSIXFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t POSIXFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                         out_parameter);

fsal_status_t POSIXFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                      fsal_parameter_t * out_parameter);

fsal_status_t POSIXFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t POSIXFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                             fsal_parameter_t *
                                                             out_parameter);

fsal_status_t POSIXFSAL_truncate(fsal_handle_t * p_filehandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_size_t length,    /* IN */
                                 fsal_file_t * file_descriptor,    /* Unused in this FSAL */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t POSIXFSAL_unlink(fsal_handle_t * p_parent_directory_handle,  /* IN */
                               fsal_name_t * p_object_name,     /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_attrib_list_t *
                               p_parent_directory_attributes /* [IN/OUT ] */ );

char *POSIXFSAL_GetFSName();

fsal_status_t POSIXFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,      /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      unsigned int xattr_id,    /* IN */
                                      fsal_attrib_list_t * p_attrs);

fsal_status_t POSIXFSAL_ListXAttrs(fsal_handle_t * p_objecthandle, /* IN */
                                   unsigned int cookie, /* IN */
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_xattrent_t * xattrs_tab,        /* IN/OUT */
                                   unsigned int xattrs_tabsize, /* IN */
                                   unsigned int *p_nb_returned, /* OUT */
                                   int *end_of_list /* OUT */ );

fsal_status_t POSIXFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t POSIXFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,   /* IN */
                                         const fsal_name_t * xattr_name,        /* IN */
                                         fsal_op_context_t * p_context,    /* IN */
                                         unsigned int *pxattr_id /* OUT */ );

fsal_status_t POSIXFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                            const fsal_name_t * xattr_name,     /* IN */
                                            fsal_op_context_t * p_context, /* IN */
                                            caddr_t buffer_addr,        /* IN/OUT */
                                            size_t buffer_size, /* IN */
                                            size_t * p_output_size /* OUT */ );

fsal_status_t POSIXFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,      /* IN */
                                      const fsal_name_t * xattr_name,   /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      caddr_t buffer_addr,      /* IN */
                                      size_t buffer_size,       /* IN */
                                      int create /* IN */ );

fsal_status_t POSIXFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,  /* IN */
                                          size_t buffer_size /* IN */ );

fsal_status_t POSIXFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,    /* IN */
                                        fsal_op_context_t * p_context,     /* IN */
                                        unsigned int xattr_id) /* IN */ ;

fsal_status_t POSIXFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,  /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          const fsal_name_t * xattr_name) /* IN */ ;

unsigned int POSIXFSAL_GetFileno(fsal_file_t * pfile);
