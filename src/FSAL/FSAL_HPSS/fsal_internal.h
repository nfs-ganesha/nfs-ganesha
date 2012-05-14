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

#if HPSS_MAJOR_VERSION >= 6
#include <hpss_mech.h>
#include <hpss_String.h>
#endif

/* defined the set of attributes supported with HPSS */
#define HPSS_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_FILEID    | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_CREATION  | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_MOUNTFILEID | FSAL_ATTR_CHGTIME  )

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

extern fsal_uint_t CredentialLifetime;

extern fsal_uint_t ReturnInconsistentDirent;


#endif

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
 * Set credential lifetime.
 */
void fsal_internal_SetCredentialLifetime(fsal_uint_t lifetime_in);

/**
 * Set behavior when detecting a MD inconsistency in readdir.
 */
void fsal_internal_SetReturnInconsistentDirent(fsal_uint_t bool_in);

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
fsal_status_t HPSSFSAL_access(hpssfsal_handle_t * p_object_handle,      /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessflags_t access_type,   /* IN */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_getattrs(hpssfsal_handle_t * p_filehandle,       /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t HPSSFSAL_setattrs(hpssfsal_handle_t * p_filehandle,       /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * p_attrib_set,      /* IN */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_BuildExportContext(hpssfsal_export_context_t * p_export_context, /* OUT */
                                          fsal_path_t * p_export_path,  /* IN */
                                          char *fs_specific_options /* IN */ );

fsal_status_t HPSSFSAL_InitClientContext(hpssfsal_op_context_t * p_thr_context);

fsal_status_t HPSSFSAL_CleanUpExportContext(hpssfsal_export_context_t * p_export_context) ;

fsal_status_t HPSSFSAL_GetClientContext(hpssfsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                        hpssfsal_export_context_t * p_export_context,   /* IN */
                                        fsal_uid_t uid, /* IN */
                                        fsal_gid_t gid, /* IN */
                                        fsal_gid_t * alt_groups,        /* IN */
                                        fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t HPSSFSAL_create(hpssfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              hpssfsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_mkdir(hpssfsal_handle_t * p_parent_directory_handle,     /* IN */
                             fsal_name_t * p_dirname,   /* IN */
                             hpssfsal_op_context_t * p_context, /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             hpssfsal_handle_t * p_object_handle,       /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_link(hpssfsal_handle_t * p_target_handle,        /* IN */
                            hpssfsal_handle_t * p_dir_handle,   /* IN */
                            fsal_name_t * p_link_name,  /* IN */
                            hpssfsal_op_context_t * p_context,  /* IN */
                            fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_mknode(hpssfsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_node_name,        /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_nodetype_t nodetype, /* IN */
                              fsal_dev_t * dev, /* IN */
                              hpssfsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                              fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_opendir(hpssfsal_handle_t * p_dir_handle,        /* IN */
                               hpssfsal_op_context_t * p_context,       /* IN */
                               hpssfsal_dir_t * p_dir_descriptor,       /* OUT */
                               fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_readdir(hpssfsal_dir_t * p_dir_descriptor,       /* IN */
                               hpssfsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * p_pdirent,       /* OUT */
                               hpssfsal_cookie_t * p_end_position,      /* OUT */
                               fsal_count_t * p_nb_entries,     /* OUT */
                               fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t HPSSFSAL_closedir(hpssfsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t HPSSFSAL_open_by_name(hpssfsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    hpssfsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    hpssfsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_open(hpssfsal_handle_t * p_filehandle,   /* IN */
                            hpssfsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            hpssfsal_file_t * p_file_descriptor,        /* OUT */
                            fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_read(hpssfsal_file_t * p_file_descriptor,        /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * p_read_amount,        /* OUT */
                            fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t HPSSFSAL_write(hpssfsal_file_t * p_file_descriptor,       /* IN */
                             fsal_op_context_t * p_context,             /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t HPSSFSAL_close(hpssfsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t HPSSFSAL_open_by_fileid(hpssfsal_handle_t * filehandle,   /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      hpssfsal_file_t * file_descriptor,        /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_close_by_fileid(hpssfsal_file_t * file_descriptor /* IN */ ,
                                       fsal_u64_t fileid);

fsal_status_t HPSSFSAL_dynamic_fsinfo(hpssfsal_handle_t * p_filehandle, /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t HPSSFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t HPSSFSAL_terminate();

fsal_status_t HPSSFSAL_test_access(hpssfsal_op_context_t * p_context,   /* IN */
                                   fsal_accessflags_t access_type,      /* IN */
                                   fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t HPSSFSAL_setattr_access(hpssfsal_op_context_t * p_context,        /* IN */
                                      fsal_attrib_list_t * candidate_attributes,        /* IN */
                                      fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t HPSSFSAL_rename_access(hpssfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattrsrc,     /* IN */
                                     fsal_attrib_list_t * pattrdest) /* IN */ ;

fsal_status_t HPSSFSAL_create_access(hpssfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t HPSSFSAL_unlink_access(hpssfsal_op_context_t * pcontext,  /* IN */
                                     fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t HPSSFSAL_link_access(hpssfsal_op_context_t * pcontext,    /* IN */
                                   fsal_attrib_list_t * pattr) /* IN */ ;

fsal_status_t HPSSFSAL_merge_attrs(fsal_attrib_list_t * pinit_attr,
                                   fsal_attrib_list_t * pnew_attr,
                                   fsal_attrib_list_t * presult_attr);

fsal_status_t HPSSFSAL_lookup(hpssfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_filename, /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              hpssfsal_handle_t * p_object_handle,      /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  hpssfsal_op_context_t * p_context,    /* IN */
                                  hpssfsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_lookupJunction(hpssfsal_handle_t * p_junction_handle,    /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      hpssfsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t *
                                      p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_CleanObjectResources(hpssfsal_handle_t * in_fsal_handle);

fsal_status_t HPSSFSAL_set_quota(fsal_path_t * pfsal_path,      /* IN */
                                 int quota_type,        /* IN */
                                 fsal_uid_t fsal_uid,   /* IN */
                                 fsal_quota_t * pquota, /* IN */
                                 fsal_quota_t * presquota);     /* OUT */

fsal_status_t HPSSFSAL_get_quota(fsal_path_t * pfsal_path,      /* IN */
                                 int quota_type,        /* IN */
                                 fsal_uid_t fsal_uid,   /* IN */
                                 fsal_quota_t * pquota);        /* OUT */

fsal_status_t HPSSFSAL_check_quota( char              * path,  /* IN */
                                    fsal_quota_type_t   quota_type,
                                    fsal_uid_t          fsal_uid) ;     /* IN */

fsal_status_t HPSSFSAL_rcp(hpssfsal_handle_t * filehandle,      /* IN */
                           hpssfsal_op_context_t * p_context,   /* IN */
                           fsal_path_t * p_local_path,  /* IN */
                           fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t HPSSFSAL_rename(hpssfsal_handle_t * p_old_parentdir_handle,       /* IN */
                              fsal_name_t * p_old_name, /* IN */
                              hpssfsal_handle_t * p_new_parentdir_handle,       /* IN */
                              fsal_name_t * p_new_name, /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t * p_src_dir_attributes,        /* [ IN/OUT ] */
                              fsal_attrib_list_t *
                              p_tgt_dir_attributes /* [ IN/OUT ] */ );

void HPSSFSAL_get_stats(fsal_statistics_t * stats,      /* OUT */
                        fsal_boolean_t reset /* IN */ );

fsal_status_t HPSSFSAL_readlink(hpssfsal_handle_t * p_linkhandle,       /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_path_t * p_link_content,   /* OUT */
                                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_symlink(hpssfsal_handle_t * p_parent_directory_handle,   /* IN */
                               fsal_name_t * p_linkname,        /* IN */
                               fsal_path_t * p_linkcontent,     /* IN */
                               hpssfsal_op_context_t * p_context,       /* IN */
                               fsal_accessmode_t accessmode,    /* IN (ignored) */
                               hpssfsal_handle_t * p_link_handle,       /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int HPSSFSAL_handlecmp(hpssfsal_handle_t * handle1, hpssfsal_handle_t * handle2,
                       fsal_status_t * status);

unsigned int HPSSFSAL_Handle_to_HashIndex(hpssfsal_handle_t * p_handle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size);

unsigned int HPSSFSAL_Handle_to_RBTIndex(hpssfsal_handle_t * p_handle,
                                         unsigned int cookie);

fsal_status_t HPSSFSAL_DigestHandle(hpssfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t output_type,      /* IN */
                                    hpssfsal_handle_t * p_in_fsal_handle,       /* IN */
                                    caddr_t out_buff /* OUT */ );

fsal_status_t HPSSFSAL_ExpandHandle(hpssfsal_export_context_t * p_expcontext,   /* IN */
                                    fsal_digesttype_t in_type,  /* IN */
                                    caddr_t in_buff,    /* IN */
                                    hpssfsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t HPSSFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t HPSSFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t HPSSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t HPSSFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter);

fsal_status_t HPSSFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t *
                                                          out_parameter);

fsal_status_t HPSSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter);

fsal_status_t HPSSFSAL_truncate(hpssfsal_handle_t * p_filehandle,       /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_size_t length,     /* IN */
                                hpssfsal_file_t * file_descriptor,      /* Unused in this FSAL */
                                fsal_attrib_list_t *
                                p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t HPSSFSAL_unlink(hpssfsal_handle_t * p_parent_directory_handle,    /* IN */
                              fsal_name_t * p_object_name,      /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_attrib_list_t *
                              p_parent_directory_attributes /* [IN/OUT ] */ );

char *HPSSFSAL_GetFSName();

fsal_status_t HPSSFSAL_GetXAttrAttrs(hpssfsal_handle_t * p_objecthandle,        /* IN */
                                     hpssfsal_op_context_t * p_context, /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_attrib_list_t * p_attrs);

fsal_status_t HPSSFSAL_ListXAttrs(hpssfsal_handle_t * p_objecthandle,   /* IN */
                                  unsigned int cookie,  /* IN */
                                  hpssfsal_op_context_t * p_context,    /* IN */
                                  fsal_xattrent_t * xattrs_tab, /* IN/OUT */
                                  unsigned int xattrs_tabsize,  /* IN */
                                  unsigned int *p_nb_returned,  /* OUT */
                                  int *end_of_list /* OUT */ );

fsal_status_t HPSSFSAL_GetXAttrValueById(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN/OUT */
                                         size_t buffer_size,    /* IN */
                                         size_t * p_output_size /* OUT */ );

fsal_status_t HPSSFSAL_GetXAttrIdByName(hpssfsal_handle_t * p_objecthandle,     /* IN */
                                        const fsal_name_t * xattr_name, /* IN */
                                        hpssfsal_op_context_t * p_context,      /* IN */
                                        unsigned int *pxattr_id /* OUT */ );

fsal_status_t HPSSFSAL_GetXAttrValueByName(hpssfsal_handle_t * p_objecthandle,  /* IN */
                                           const fsal_name_t * xattr_name,      /* IN */
                                           hpssfsal_op_context_t * p_context,   /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

fsal_status_t HPSSFSAL_SetXAttrValue(hpssfsal_handle_t * p_objecthandle,        /* IN */
                                     const fsal_name_t * xattr_name,    /* IN */
                                     hpssfsal_op_context_t * p_context, /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size,        /* IN */
                                     int create /* IN */ );

fsal_status_t HPSSFSAL_SetXAttrValueById(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         unsigned int xattr_id, /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         caddr_t buffer_addr,   /* IN */
                                         size_t buffer_size /* IN */ );

fsal_status_t HPSSFSAL_RemoveXAttrById(hpssfsal_handle_t * p_objecthandle,      /* IN */
                                       hpssfsal_op_context_t * p_context,       /* IN */
                                       unsigned int xattr_id) /* IN */ ;

fsal_status_t HPSSFSAL_RemoveXAttrByName(hpssfsal_handle_t * p_objecthandle,    /* IN */
                                         hpssfsal_op_context_t * p_context,     /* IN */
                                         const fsal_name_t * xattr_name) /* IN */ ;

unsigned int HPSSFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t HPSSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                   fsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */) ;

fsal_status_t HPSSFSAL_commit( fsal_file_t * p_file_descriptor,
                             fsal_off_t    offset,
                             fsal_size_t   size ) ;


