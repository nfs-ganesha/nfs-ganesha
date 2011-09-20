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
#include "nfs4.h"

#ifndef FSAL_INTERNAL_H
#define FSAL_INTERNAL_H
/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

#define FSAL_PROXY_OWNER_LEN 256

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

#endif

typedef struct fsal_proxy_internal_fattr__
{
  fattr4_type type;
  fattr4_change change_time;
  fattr4_size size;
  fattr4_fsid fsid;
  fattr4_filehandle filehandle;
  fattr4_fileid fileid;
  fattr4_mode mode;
  fattr4_numlinks numlinks;
  fattr4_owner owner;           /* Needs to points to a string */
  fattr4_owner_group owner_group;       /* Needs to points to a string */
  fattr4_space_used space_used;
  fattr4_time_access time_access;
  fattr4_time_metadata time_metadata;
  fattr4_time_modify time_modify;
  fattr4_rawdev rawdev;
  char padowner[MAXNAMLEN];
  char padgroup[MAXNAMLEN];
  char padfh[NFS4_FHSIZE];
} fsal_proxy_internal_fattr_t;

typedef struct fsal_proxy_internal_fattr_readdir__
{
  fattr4_type type;
  fattr4_change change_time;
  fattr4_size size;
  fattr4_fsid fsid;
  fattr4_filehandle filehandle;
  fattr4_fileid fileid;
  fattr4_mode mode;
  fattr4_numlinks numlinks;
  fattr4_owner owner;           /* Needs to points to a string */
  fattr4_owner_group owner_group;       /* Needs to points to a string */
  fattr4_space_used space_used;
  fattr4_time_access time_access;
  fattr4_time_metadata time_metadata;
  fattr4_time_modify time_modify;
  fattr4_rawdev rawdev;
  char padowner[MAXNAMLEN];
  char padgroup[MAXNAMLEN];
  char padfh[NFS4_FHSIZE];
} fsal_proxy_internal_fattr_readdir_t;

void fsal_internal_proxy_setup_fattr(fsal_proxy_internal_fattr_t * pfattr);
void fsal_internal_proxy_setup_readdir_fattr(fsal_proxy_internal_fattr_readdir_t *
                                             pfattr);

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

void fsal_internal_proxy_create_fattr_bitmap(bitmap4 * pbitmap);
void fsal_internal_proxy_create_fattr_readdir_bitmap(bitmap4 * pbitmap);
void fsal_internal_proxy_create_fattr_fsinfo_bitmap(bitmap4 * pbitmap);
void fsal_interval_proxy_fsalattr2bitmap4(fsal_attrib_list_t * pfsalattr,
                                          bitmap4 * pbitmap);

/*
 * A few functions dedicated in proxy related information management and conversion 
 */
fsal_status_t fsal_internal_set_auth_gss(proxyfsal_op_context_t * p_thr_context);
fsal_status_t fsal_internal_proxy_error_convert(nfsstat4 nfsstatus, int indexfunc);
int fsal_internal_proxy_create_fh(nfs_fh4 * pnfs4_handle,
                                  fsal_nodetype_t type,
                                  fsal_u64_t fileid, fsal_handle_t * pfsal_handle);
int fsal_internal_proxy_extract_fh(nfs_fh4 * pnfs4_handle, fsal_handle_t * pfsal_handle);
int fsal_internal_proxy_fsal_name_2_utf8(fsal_name_t * pname, utf8string * utf8str);
int fsal_internal_proxy_fsal_path_2_utf8(fsal_path_t * ppath, utf8string * utf8str);
int fsal_internal_proxy_fsal_utf8_2_name(fsal_name_t * pname, utf8string * utf8str);
int fsal_internal_proxy_fsal_utf8_2_path(fsal_path_t * ppath, utf8string * utf8str);
int proxy_Fattr_To_FSAL_attr(fsal_attrib_list_t * pFSAL_attr,
                             proxyfsal_handle_t * phandle, fattr4 * Fattr);
int proxy_Fattr_To_FSAL_dynamic_fsinfo(fsal_dynamicfsinfo_t * pdynamicinfo,
                                       fattr4 * Fattr);

fsal_status_t FSAL_proxy_setclientid(proxyfsal_op_context_t * p_context);
fsal_status_t FSAL_proxy_setclientid_force(proxyfsal_op_context_t * p_context);
fsal_status_t FSAL_proxy_setclientid_renego(proxyfsal_op_context_t * p_context);

int FSAL_proxy_set_hldir(proxyfsal_op_context_t * p_thr_context, char *hl_path);
int fsal_internal_ClientReconnect(proxyfsal_op_context_t * p_thr_context);
fsal_status_t FSAL_proxy_open_confirm(proxyfsal_file_t * pfd);
void *FSAL_proxy_change_user(proxyfsal_op_context_t * p_thr_context);

/* All the call to FSAL to be wrapped */
fsal_status_t PROXYFSAL_access(fsal_handle_t * p_object_handle,    /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_getattrs(fsal_handle_t * p_filehandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t PROXYFSAL_setattrs(fsal_handle_t * p_filehandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_attrib_list_t * p_attrib_set,     /* IN */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_BuildExportContext(fsal_export_context_t * p_export_context,       /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options /* IN */ );

fsal_status_t PROXYFSAL_InitClientContext(fsal_op_context_t * p_thr_context);

fsal_status_t PROXYFSAL_create(fsal_handle_t * p_parent_directory_handle,  /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * p_object_handle,    /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,   /* IN */
                              fsal_name_t * p_dirname,  /* IN */
                              fsal_op_context_t * p_context,       /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_handle_t * p_object_handle,     /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_link(fsal_handle_t * p_target_handle,      /* IN */
                             fsal_handle_t * p_dir_handle, /* IN */
                             fsal_name_t * p_link_name, /* IN */
                             fsal_op_context_t * p_context,        /* IN */
                             fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_mknode(fsal_handle_t * parentdir_handle,   /* IN */
                               fsal_name_t * p_node_name,       /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_nodetype_t nodetype,        /* IN */
                               fsal_dev_t * dev,        /* IN */
                               fsal_handle_t * p_object_handle,    /* OUT (handle to the created node) */
                               fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_opendir(fsal_handle_t * p_dir_handle,      /* IN */
                                fsal_op_context_t * p_context,     /* IN */
                                fsal_dir_t * p_dir_descriptor,     /* OUT */
                                fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_readdir(fsal_dir_t * p_dir_descriptor,     /* IN */
                                fsal_cookie_t start_position,      /* IN */
                                fsal_attrib_mask_t get_attr_mask,       /* IN */
                                fsal_mdsize_t buffersize,       /* IN */
                                fsal_dirent_t * p_pdirent,      /* OUT */
                                fsal_cookie_t * p_end_position,    /* OUT */
                                fsal_count_t * p_nb_entries,    /* OUT */
                                fsal_boolean_t * p_end_of_dir /* OUT */ );

fsal_status_t PROXYFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t PROXYFSAL_open_by_name(fsal_handle_t * dirhandle,    /* IN */
                                     fsal_name_t * filename,    /* IN */
                                     fsal_op_context_t * p_context,        /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     fsal_file_t * file_descriptor,        /* OUT */
                                     fsal_attrib_list_t *
                                     file_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_open(fsal_handle_t * p_filehandle, /* IN */
                             fsal_op_context_t * p_context,        /* IN */
                             fsal_openflags_t openflags,        /* IN */
                             fsal_file_t * p_file_descriptor,      /* OUT */
                             fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_read(fsal_file_t * p_file_descriptor,      /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* [IN] */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* OUT */
                             fsal_size_t * p_read_amount,       /* OUT */
                             fsal_boolean_t * p_end_of_file /* OUT */ );

fsal_status_t PROXYFSAL_write(fsal_file_t * p_file_descriptor,     /* IN */
                              fsal_seek_t * p_seek_descriptor,  /* IN */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* IN */
                              fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t PROXYFSAL_sync(fsal_file_t * p_file_descriptor     /* IN */);

fsal_status_t PROXYFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t PROXYFSAL_open_by_fileid(fsal_handle_t * filehandle, /* IN */
                                       fsal_u64_t fileid,       /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_openflags_t openflags,      /* IN */
                                       fsal_file_t * file_descriptor,      /* OUT */
                                       fsal_attrib_list_t *
                                       file_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                        fsal_u64_t fileid);

fsal_status_t PROXYFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,       /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t PROXYFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t PROXYFSAL_terminate();

fsal_status_t PROXYFSAL_test_access(fsal_op_context_t * p_context, /* IN */
                                    fsal_accessflags_t access_type,     /* IN */
                                    fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t PROXYFSAL_setattr_access(fsal_op_context_t * p_context,      /* IN */
                                       fsal_attrib_list_t * candidate_attributes,       /* IN */
                                       fsal_attrib_list_t * object_attributes /* IN */ );

fsal_status_t PROXYFSAL_lookup(fsal_handle_t * p_parent_directory_handle,  /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_handle_t * p_object_handle,    /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_lookupPath(fsal_path_t * p_path,        /* IN */
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_handle_t * object_handle,  /* OUT */
                                   fsal_attrib_list_t *
                                   p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_lookupJunction(fsal_handle_t * p_junction_handle,  /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_handle_t * p_fsoot_handle,     /* OUT */
                                       fsal_attrib_list_t *
                                       p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_rcp(fsal_handle_t * filehandle,    /* IN */
                            fsal_op_context_t * p_context, /* IN */
                            fsal_path_t * p_local_path, /* IN */
                            fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t PROXYFSAL_rcp_by_fileid(fsal_handle_t * filehandle,  /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      fsal_path_t * p_local_path,       /* IN */
                                      fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t PROXYFSAL_rename(fsal_handle_t * p_old_parentdir_handle,     /* IN */
                               fsal_name_t * p_old_name,        /* IN */
                               fsal_handle_t * p_new_parentdir_handle,     /* IN */
                               fsal_name_t * p_new_name,        /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_attrib_list_t * p_src_dir_attributes,       /* [ IN/OUT ] */
                               fsal_attrib_list_t *
                               p_tgt_dir_attributes /* [ IN/OUT ] */ );

void PROXYFSAL_get_stats(fsal_statistics_t * stats,     /* OUT */
                         fsal_boolean_t reset /* IN */ );

fsal_status_t PROXYFSAL_readlink(fsal_handle_t * p_linkhandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_path_t * p_link_content,  /* OUT */
                                 fsal_attrib_list_t *
                                 p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_symlink(fsal_handle_t * p_parent_directory_handle, /* IN */
                                fsal_name_t * p_linkname,       /* IN */
                                fsal_path_t * p_linkcontent,    /* IN */
                                fsal_op_context_t * p_context,     /* IN */
                                fsal_accessmode_t accessmode,   /* IN (ignored) */
                                fsal_handle_t * p_link_handle,     /* OUT */
                                fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int PROXYFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                        fsal_status_t * status);

unsigned int PROXYFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                           unsigned int cookie,
                                           unsigned int alphabet_len,
                                           unsigned int index_size);

unsigned int PROXYFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle,
                                          unsigned int cookie);

fsal_status_t PROXYFSAL_DigestHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t output_type,     /* IN */
                                     fsal_handle_t * p_in_fsal_handle,     /* IN */
                                     caddr_t out_buff /* OUT */ );

fsal_status_t PROXYFSAL_ExpandHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t in_type, /* IN */
                                     caddr_t in_buff,   /* IN */
                                     fsal_handle_t * p_out_fsal_handle /* OUT */ );

fsal_status_t PROXYFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t PROXYFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t PROXYFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
                                                         out_parameter);

fsal_status_t PROXYFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                      fsal_parameter_t * out_parameter);

fsal_status_t PROXYFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t PROXYFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                             fsal_parameter_t *
                                                             out_parameter);

fsal_status_t PROXYFSAL_truncate(fsal_handle_t * p_filehandle,     /* IN */
                                 fsal_op_context_t * p_context,    /* IN */
                                 fsal_size_t length,    /* IN */
                                 fsal_file_t * file_descriptor,    /* Unused in this FSAL */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t PROXYFSAL_unlink(fsal_handle_t * p_parent_directory_handle,  /* IN */
                               fsal_name_t * p_object_name,     /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_attrib_list_t *
                               p_parent_directory_attributes /* [IN/OUT ] */ );

char *PROXYFSAL_GetFSName();

fsal_status_t PROXYFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,      /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      unsigned int xattr_id,    /* IN */
                                      fsal_attrib_list_t * p_attrs);

fsal_status_t PROXYFSAL_ListXAttrs(fsal_handle_t * p_objecthandle, /* IN */
                                   unsigned int cookie, /* IN */
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_xattrent_t * xattrs_tab,        /* IN/OUT */
                                   unsigned int xattrs_tabsize, /* IN */
                                   unsigned int *p_nb_returned, /* OUT */
                                   int *end_of_list /* OUT */ );

fsal_status_t PROXYFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t PROXYFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,   /* IN */
                                         const fsal_name_t * xattr_name,        /* IN */
                                         fsal_op_context_t * p_context,    /* IN */
                                         unsigned int *pxattr_id /* OUT */ );

fsal_status_t PROXYFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,        /* IN */
                                            const fsal_name_t * xattr_name,     /* IN */
                                            fsal_op_context_t * p_context, /* IN */
                                            caddr_t buffer_addr,        /* IN/OUT */
                                            size_t buffer_size, /* IN */
                                            size_t * p_output_size /* OUT */ );

fsal_status_t PROXYFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,      /* IN */
                                      const fsal_name_t * xattr_name,   /* IN */
                                      fsal_op_context_t * p_context,       /* IN */
                                      caddr_t buffer_addr,      /* IN */
                                      size_t buffer_size,       /* IN */
                                      int create /* IN */ );

fsal_status_t PROXYFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,  /* IN */
                                          unsigned int xattr_id,        /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          caddr_t buffer_addr,  /* IN */
                                          size_t buffer_size /* IN */ );

fsal_status_t PROXYFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,    /* IN */
                                        fsal_op_context_t * p_context,     /* IN */
                                        unsigned int xattr_id) /* IN */ ;

fsal_status_t PROXYFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,  /* IN */
                                          fsal_op_context_t * p_context,   /* IN */
                                          const fsal_name_t * xattr_name) /* IN */ ;

unsigned int PROXYFSAL_GetFileno(fsal_file_t * pfile);

#endif
