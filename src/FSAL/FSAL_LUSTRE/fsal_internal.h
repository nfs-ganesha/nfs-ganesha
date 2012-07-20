/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 *
 * \file    fsal_internal.h
 * \version $Revision: 1.12 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 * 
 */

#include "fsal.h"
#include <sys/stat.h>
#include "FSAL/common_functions.h"
#include "fsal_pnfs.h"

/* defined the set of attributes supported with POSIX */
#define LUSTRE_SUPPORTED_ATTRIBUTES (                                       \
          ATTR_SUPPATTR | ATTR_TYPE     | ATTR_SIZE      | \
          ATTR_FSID     | ATTR_FILEID  | \
          ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
          ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
          ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
          ATTR_CHGTIME  )

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern struct fsal_staticfsinfo_t global_fs_info;

/* export_context_t is not given to every function, but
 * most functions need to use the open-by-handle funcionality.
 */
extern char open_by_handle_path[MAXPATHLEN];
extern int open_by_handle_fd;

#endif

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);


#if 0
/**
 * Gets a fd from a handle 
 */
fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_handle2fd_at(int dirfd,
                                         fsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_Path2Handle(fsal_op_context_t * p_context,       /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        fsal_handle_t * p_handle /* OUT */ ) ;

/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(fsal_op_context_t * p_context,
                                      fsal_handle_t * p_handle, char *__buf, int maxlen);

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_get_handle_at(int dfd, const char *name,    /* IN */
                                          fsal_handle_t * p_handle /* OUT */ );

fsal_status_t fsal_internal_fd2handle(  fsal_op_context_t * p_context,
					int fd,   /* IN */
                                        fsal_handle_t * p_handle  /* OUT */
    );

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

fsal_status_t fsal_stat_by_handle(fsal_op_context_t * p_context,
                                  fsal_handle_t * p_handle, struct stat64 *buf);

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

fsal_status_t LUSTREFSAL_DigestHandle(fsal_export_context_t * exp_context,     /* IN */
                                      fsal_digesttype_t output_type,       /* IN */
                                      fsal_handle_t *in_fsal_handle, /* IN */
                                      struct fsal_handle_desc *fh_desc     /* IN/OUT */ ) ;

fsal_status_t LUSTREFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,     /* IN not used */
                                   fsal_digesttype_t in_type,   /* IN */
                                   struct fsal_handle_desc *fh_desc  /* IN/OUT */ ) ;

fsal_status_t LUSTREFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t *
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

int LUSTREFSAL_GetXattrOffsetSetable( void ) ;

fsal_status_t LUSTREFSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                                  fsal_handle_t     * p_filehandle,        /* IN */
                                  fsal_op_context_t * p_context,           /* IN */
                                  void              * p_owner,             /* IN (opaque to FSAL) */
                                  fsal_lock_op_t      lock_op,             /* IN */
                                  fsal_lock_param_t   request_lock,        /* IN */
                                  fsal_lock_param_t * conflicting_lock)    /* OUT */ ;

fsal_status_t LUSTREFSAL_set_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota,  /* IN */
                                fsal_quota_t * presquota)  ;     /* OUT */

fsal_status_t LUSTREFSAL_get_quota(fsal_path_t * pfsal_path,       /* IN */
                                   int quota_type, /* IN */
                                   fsal_uid_t fsal_uid,    /* IN */
                                   fsal_quota_t * pquota) ;  /* OUT */

fsal_status_t LUSTREFSAL_check_quota( char *path,  /* IN */
                                      fsal_quota_type_t  quota_type,
                                      fsal_uid_t fsal_uid);      /* IN */

unsigned int LUSTREFSAL_GetFileno(fsal_file_t * pfile);
nfsstat4 LUSTREFSAL_MDS_init( ) ;
nfsstat4 LUSTREFSAL_MDS_terminate( ) ;

nfsstat4 LUSTREFSAL_layoutget(fsal_handle_t *exhandle,
                            fsal_op_context_t *excontext,
                            XDR *loc_body,
                            const struct fsal_layoutget_arg *arg,
                            struct fsal_layoutget_res *res);
nfsstat4 LUSTREFSAL_layoutreturn(fsal_handle_t* handle,
                               fsal_op_context_t* context,
                               XDR *lrf_body,
                               const struct fsal_layoutreturn_arg *arg);
nfsstat4 LUSTREFSAL_layoutcommit(fsal_handle_t *handle,
                               fsal_op_context_t *context,
                               XDR *lou_body,
                               const struct fsal_layoutcommit_arg *arg,
                               struct fsal_layoutcommit_res *res);
nfsstat4 LUSTREFSAL_getdeviceinfo(fsal_op_context_t *context,
                                XDR* da_addr_body,
                                layouttype4 type,
                                const struct pnfs_deviceid *deviceid);
nfsstat4 LUSTREFSAL_getdevicelist(fsal_handle_t *handle,
                                fsal_op_context_t *context,
                                const struct fsal_getdevicelist_arg *arg,
                                struct fsal_getdevicelist_res *res);

fsal_status_t LUSTREFSAL_load_pnfs_parameter_from_conf(config_file_t             in_config,
                                                       lustre_pnfs_parameter_t * out_parameter);

nfsstat4 LUSTREFSAL_DS_init( lustre_pnfs_parameter_t *pparam ) ;

nfsstat4 LUSTREFSAL_DS_terminate( void ) ;

nfsstat4 LUSTREFSAL_DS_read(fsal_handle_t *handle,
                          fsal_op_context_t *context,
                          const stateid4 *stateid,
                          offset4 offset,
                          count4 requested_length,
                          caddr_t buffer,
                          count4 *supplied_length,
                          fsal_boolean_t *end_of_file);

nfsstat4 LUSTREFSAL_DS_write(fsal_handle_t *handle,
                           fsal_op_context_t *context,
                           const stateid4 *stateid,
                           offset4 offset,
                           count4 write_length,
                           caddr_t buffer,
                           stable_how4 stability_wanted,
                           count4 *written_length,
                           verifier4 writeverf,
                           stable_how4 *stability_got);

nfsstat4 LUSTREFSAL_DS_commit(fsal_handle_t *handle,
                            fsal_op_context_t *context,
                            offset4 offset,
                            count4 count,
                            verifier4 writeverf);
