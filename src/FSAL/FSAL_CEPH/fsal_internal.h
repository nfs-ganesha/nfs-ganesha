/*
 * Copyright (C) 2010 The Linx Box Corporation
 * Contributor : Adam C. Emerson
 *
 * Some Portions Copyright CEA/DAM/DIF  (2008)
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
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <cephfs/libcephfs.h>
#include <string.h>
#include "fsal.h"
#include "fsal_pnfs.h"

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

#define ReturnStatus( _st_, _f_ ) Return( (_st_).major, (_st_).minor, _f_ )

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

/* Everybody gets to know the server. */
extern cephfs_specific_initinfo_t global_spec_info;

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

fsal_status_t CEPHFSAL_access(fsal_handle_t * exthandle,
                              fsal_op_context_t * extcontext,
                              fsal_accessflags_t access_type,
                              fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_getattrs(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_setattrs(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_attrib_list_t * attrib_set,
                                fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_getextattrs(
     fsal_handle_t * p_filehandle,
     fsal_op_context_t * p_context,
     fsal_extattrib_list_t * p_object_attributes);

fsal_status_t CEPHFSAL_BuildExportContext(
     fsal_export_context_t * extexport_context,
     fsal_path_t * export_path,
     char *fs_specific_options);

fsal_status_t CEPHFSAL_CleanUpExportContext(
     fsal_export_context_t * export_context);

fsal_status_t CEPHFSAL_InitClientContext(
     fsal_op_context_t* extcontext);

fsal_status_t CEPHFSAL_GetClientContext(
     fsal_op_context_t * extcontext,
     fsal_export_context_t * extexport_context,
     fsal_uid_t uid,
     fsal_gid_t gid,
     fsal_gid_t * alt_groups,
     fsal_count_t nb_alt_groups);

fsal_status_t CEPHFSAL_create(fsal_handle_t * extparent,
                              fsal_name_t * filename,
                              fsal_op_context_t * extcontext,
                              fsal_accessmode_t accessmode,
                              fsal_handle_t * object_handle,
                              fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_mkdir(fsal_handle_t * extparent,
                             fsal_name_t * dirname,
                             fsal_op_context_t * extcontext,
                             fsal_accessmode_t accessmode,
                             fsal_handle_t * object_handle,
                             fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_link(fsal_handle_t * exttarget,
                            fsal_handle_t * extdir,
                            fsal_name_t * link_name,
                            fsal_op_context_t * extcontext,
                            fsal_attrib_list_t * attributes);


fsal_status_t CEPHFSAL_mknode(fsal_handle_t * parent,
                              fsal_name_t * node_name,
                              fsal_op_context_t * p_context,
                              fsal_accessmode_t accessmode,
                              fsal_nodetype_t nodetype,
                              fsal_dev_t * dev,
                              fsal_handle_t * p_object_handle,
                              fsal_attrib_list_t * node_attributes);

fsal_status_t CEPHFSAL_opendir(fsal_handle_t * exthandle,
                               fsal_op_context_t * extcontext,
                               fsal_dir_t * extdescriptor,
                               fsal_attrib_list_t * dir_attributes);

fsal_status_t CEPHFSAL_readdir(fsal_dir_t * extdescriptor,
                               fsal_cookie_t extstart,
                               fsal_attrib_mask_t attrmask,
                               fsal_mdsize_t buffersize,
                               fsal_dirent_t * dirents,
                               fsal_cookie_t * extend,
                               fsal_count_t * count,
                               fsal_boolean_t * end_of_dir);

fsal_status_t CEPHFSAL_closedir(fsal_dir_t * extdescriptor);

fsal_boolean_t fsal_is_retryable(fsal_status_t status);

fsal_status_t CEPHFSAL_open(fsal_handle_t * exthandle,
                            fsal_op_context_t * extcontext,
                            fsal_openflags_t openflags,
                            fsal_file_t * extdescriptor,
                            fsal_attrib_list_t * file_attributes);

fsal_status_t CEPHFSAL_open_by_name(fsal_handle_t * exthandle,
                                    fsal_name_t * filename,
                                    fsal_op_context_t * extcontext,
                                    fsal_openflags_t openflags,
                                    fsal_file_t * extdescriptor,
                                    fsal_attrib_list_t * file_attributes);

fsal_status_t CEPHFSAL_read(fsal_file_t * extdescriptor,
                            fsal_seek_t * seek_descriptor,
                            fsal_size_t buffer_size,
                            caddr_t buffer,
                            fsal_size_t * read_amount,
                            fsal_boolean_t * end_of_file);

fsal_status_t CEPHFSAL_write(fsal_file_t * extdescriptor,
                             fsal_op_context_t * p_context,
                             fsal_seek_t * seek_descriptor,
                             fsal_size_t buffer_size,
                             caddr_t buffer,
                             fsal_size_t * write_amount);

fsal_status_t CEPHFSAL_close(fsal_file_t * extdescriptor);

fsal_status_t CEPHFSAL_open_by_fileid(fsal_handle_t * filehandle,
                                      fsal_u64_t fileid,
                                      fsal_op_context_t * p_context,
                                      fsal_openflags_t openflags,
                                      fsal_file_t * file_descriptor,
                                      fsal_attrib_list_t * file_attributes);

fsal_status_t CEPHFSAL_close_by_fileid(fsal_file_t * file_descriptor,
                                       fsal_u64_t fileid);

unsigned int CEPHFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t CEPHFSAL_commit(fsal_file_t * extdescriptor,
                            fsal_off_t    offset, 
                            fsal_size_t   size ) ;

fsal_status_t CEPHFSAL_static_fsinfo(fsal_handle_t * exthandle,
                                     fsal_op_context_t * context,
                                     fsal_staticfsinfo_t * staticinfo);

fsal_status_t CEPHFSAL_dynamic_fsinfo(fsal_handle_t * exthandle,
                                      fsal_op_context_t * extcontext,
                                      fsal_dynamicfsinfo_t * dynamicinfo);

fsal_status_t CEPHFSAL_Init(fsal_parameter_t * init_info);

fsal_status_t CEPHFSAL_terminate();

fsal_status_t CEPHFSAL_test_access(fsal_op_context_t * extcontext,
                                   fsal_accessflags_t access_type,
                                   fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_lookup(fsal_handle_t * extparent,
                              fsal_name_t * filename,
                              fsal_op_context_t * extcontext,
                              fsal_handle_t * exthandle,
                              fsal_attrib_list_t * object_attributes);


fsal_status_t CEPHFSAL_lookupJunction(fsal_handle_t * extjunction,
                                      fsal_op_context_t * extcontext,
                                      fsal_handle_t * extfsroot,
                                      fsal_attrib_list_t * fsroot_attributes);

fsal_status_t CEPHFSAL_lookupPath(fsal_path_t * path,
                                  fsal_op_context_t * extcontext,
                                  fsal_handle_t * exthandle,
                                  fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_rcp(fsal_handle_t * filehandle,
                           fsal_op_context_t * p_context,
                           fsal_path_t * p_local_path,
                           fsal_rcpflag_t transfer_opt);

fsal_status_t CEPHFSAL_rename(fsal_handle_t * extold_parent,
                              fsal_name_t * old_name,
                              fsal_handle_t * extnew_parent,
                              fsal_name_t * new_name,
                              fsal_op_context_t * extcontext,
                              fsal_attrib_list_t * src_dir_attributes,
                              fsal_attrib_list_t * tgt_dir_attributes);

void CEPHFSAL_get_stats(fsal_statistics_t * stats,
                        fsal_boolean_t reset);

fsal_status_t CEPHFSAL_readlink(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_path_t * link_content,
                                fsal_attrib_list_t * link_attributes);

fsal_status_t CEPHFSAL_symlink(fsal_handle_t * extparent,
                               fsal_name_t * linkname,
                               fsal_path_t * linkcontent,
                               fsal_op_context_t * extcontext,
                               fsal_accessmode_t accessmode,
                               fsal_handle_t * extlink,
                               fsal_attrib_list_t * link_attributes);

int CEPHFSAL_handlecmp(fsal_handle_t * exthandle1,
                       fsal_handle_t * exthandle2,
                       fsal_status_t * status);

unsigned int CEPHFSAL_Handle_to_HashIndex(fsal_handle_t * exthandle,
                                          unsigned int cookie,
                                          unsigned int alphabet_len,
                                          unsigned int index_size);

unsigned int CEPHFSAL_Handle_to_RBTIndex(fsal_handle_t * exthandle,
                                         unsigned int cookie);
fsal_status_t CEPHFSAL_DigestHandle(fsal_export_context_t *extexport,
                                    fsal_digesttype_t output_type,
                                    fsal_handle_t *exthandle,
                                    struct fsal_handle_desc *fh_desc);
fsal_status_t CEPHFSAL_ExpandHandle(fsal_export_context_t *extexport,
                                    fsal_digesttype_t in_type,
                                    struct fsal_handle_desc *fh_desc);

fsal_status_t CEPHFSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                     fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                          fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                            fsal_parameter_t * out_parameter);

fsal_status_t CEPHFSAL_truncate(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_size_t length,
                                fsal_file_t * file_descriptor,
                                fsal_attrib_list_t * object_attributes);

fsal_status_t CEPHFSAL_unlink(fsal_handle_t * extparent,
                              fsal_name_t * name,
                              fsal_op_context_t * extcontext,
                              fsal_attrib_list_t * parentdir_attributes);

fsal_status_t CEPHFSAL_GetXAttrAttrs(fsal_handle_t * exthandle,
                                     fsal_op_context_t * extcontext,
                                     unsigned int xattr_id,
                                     fsal_attrib_list_t * attrs);

fsal_status_t CEPHFSAL_ListXAttrs(fsal_handle_t * exthandle,
                                  unsigned int cookie,
                                  fsal_op_context_t * extcontext,
                                  fsal_xattrent_t * xattrs_tab,
                                  unsigned int xattrs_tabsize,
                                  unsigned int *p_nb_returned,
                                  int *end_of_list);

fsal_status_t CEPHFSAL_GetXAttrValueById(fsal_handle_t * exthandle,
                                         unsigned int xattr_id,
                                         fsal_op_context_t * extcontext,
                                         caddr_t buffer_addr,
                                         size_t buffer_size,
                                         size_t * p_output_size);

fsal_status_t CEPHFSAL_GetXAttrIdByName(fsal_handle_t * exthandle,
                                        const fsal_name_t * xattr_name,
                                        fsal_op_context_t * extcontext,
                                        unsigned int *pxattr_id);

fsal_status_t CEPHFSAL_GetXAttrValueByName(fsal_handle_t * exthandle,
                                           const fsal_name_t * xattr_name,
                                           fsal_op_context_t * extcontext,
                                           caddr_t buffer_addr,
                                           size_t buffer_size,
                                           size_t * p_output_size);

fsal_status_t CEPHFSAL_SetXAttrValue(fsal_handle_t * exthandle,
                                     const fsal_name_t * xattr_name,
                                     fsal_op_context_t * extcontext,
                                     caddr_t buffer_addr,
                                     size_t buffer_size,
                                     int create);

fsal_status_t CEPHFSAL_SetXAttrValueById(fsal_handle_t * exthandle,
                                         unsigned int xattr_id,
                                         fsal_op_context_t * extcontext,
                                         caddr_t buffer_addr,
                                         size_t buffer_size);

fsal_status_t CEPHFSAL_RemoveXAttrById(fsal_handle_t * exthandle,
                                       fsal_op_context_t * extcontext,
                                       unsigned int xattr_id);

fsal_status_t CEPHFSAL_RemoveXAttrByName(fsal_handle_t * exthandle,
                                         fsal_op_context_t * extcontext,
                                         const fsal_name_t * xattr_name);


char *CEPHFSAL_GetFSName();

fsal_status_t fsal_internal_testAccess(cephfsal_op_context_t* context,
                                       fsal_accessflags_t access_type,
                                       struct stat * st,
                                       fsal_attrib_list_t * object_attributes);
#ifdef _PNFS_MDS
nfsstat4 CEPHFSAL_layoutget(fsal_handle_t *exhandle,
                            fsal_op_context_t *excontext,
                            XDR *loc_body,
                            const struct fsal_layoutget_arg *arg,
                            struct fsal_layoutget_res *res);
nfsstat4 CEPHFSAL_layoutreturn(fsal_handle_t* handle,
                               fsal_op_context_t* context,
                               XDR *lrf_body,
                               const struct fsal_layoutreturn_arg *arg);
nfsstat4 CEPHFSAL_layoutcommit(fsal_handle_t *handle,
                               fsal_op_context_t *context,
                               XDR *lou_body,
                               const struct fsal_layoutcommit_arg *arg,
                               struct fsal_layoutcommit_res *res);
nfsstat4 CEPHFSAL_getdeviceinfo(fsal_op_context_t *context,
                                XDR* da_addr_body,
                                layouttype4 type,
                                const struct pnfs_deviceid *deviceid);
nfsstat4 CEPHFSAL_getdevicelist(fsal_handle_t *handle,
                                fsal_op_context_t *context,
                                const struct fsal_getdevicelist_arg *arg,
                                struct fsal_getdevicelist_res *res);
#endif /* _PNFS_MDS */

#ifdef _PNFS_DS
nfsstat4 CEPHFSAL_DS_read(fsal_handle_t *handle,
                          fsal_op_context_t *context,
                          const stateid4 *stateid,
                          offset4 offset,
                          count4 requested_length,
                          caddr_t buffer,
                          count4 *supplied_length,
                          fsal_boolean_t *end_of_file);

nfsstat4 CEPHFSAL_DS_write(fsal_handle_t *handle,
                           fsal_op_context_t *context,
                           const stateid4 *stateid,
                           offset4 offset,
                           count4 write_length,
                           caddr_t buffer,
                           stable_how4 stability_wanted,
                           count4 *written_length,
                           verifier4 *writeverf,
                           stable_how4 *stability_got);

nfsstat4 CEPHFSAL_DS_commit(fsal_handle_t *handle,
                            fsal_op_context_t *context,
                            offset4 offset,
                            count4 count,
                            verifier4 *writeverf);
#endif /* _PNFS_DS */
