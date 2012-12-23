/**
 *
 * \file    fsal_internal.h
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.12 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 *
 */

#include  "fsal.h"
#include <libzfswrap.h>

/* libzfswrap handler, used only when the FSAL is created and destroyed */
extern libzfswrap_handle_t *p_zhd;

void ZFSFSAL_VFS_RDLock() ;
void ZFSFSAL_VFS_RDLock() ;
void ZFSFSAL_VFS_Unlock() ;

typedef struct zfs_file_handle
{
    inogen_t zfs_handle;
    char i_snap;
} zfs_file_handle_t;

#define ZFS_SNAP_DIR ".zfs"

#define ZFS_SNAP_DIR_INODE 2


typedef struct
{
  char *psz_name;
  libzfswrap_vfs_t *p_vfs;
  unsigned int index;
} snapshot_t;

/* defined the set of attributes supported with POSIX */
#define ZFS_SUPPORTED_ATTRIBUTES (                         \
          ATTR_SUPPATTR | ATTR_TYPE     | ATTR_SIZE      | \
          ATTR_FSID     | ATTR_FILEID   |                  \
          ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
          ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
          ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
          ATTR_CHGTIME  )

static inline size_t zfs_sizeof_handle(struct zfs_file_handle *hdl)
{
  return (size_t)sizeof( struct zfs_file_handle ) ;
}


/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern struct fsal_staticfsinfo_t global_fs_info;

#endif

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);

#if 0

/* All the call to FSAL to be wrapped */
fsal_status_t ZFSFSAL_access(fsal_handle_t * p_object_handle,        /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_accessflags_t access_type,    /* IN */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_getattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

fsal_status_t ZFSFSAL_setattrs(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_attrib_list_t * p_attrib_set,       /* IN */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_BuildExportContext(fsal_export_context_t * p_export_context,   /* OUT */
                                         fsal_path_t * p_export_path,   /* IN */
                                         char *fs_specific_options /* IN */ );

fsal_status_t ZFSFSAL_create(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_mkdir(fsal_handle_t * p_parent_directory_handle,       /* IN */
                            fsal_name_t * p_dirname,    /* IN */
                            fsal_op_context_t * p_context,   /* IN */
                            fsal_accessmode_t accessmode,       /* IN */
                            fsal_handle_t * p_object_handle, /* OUT */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_link(fsal_handle_t * p_target_handle,  /* IN */
                           fsal_handle_t * p_dir_handle,     /* IN */
                           fsal_name_t * p_link_name,   /* IN */
                           fsal_op_context_t * p_context,    /* IN */
                           fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_mknode(fsal_handle_t * parentdir_handle,       /* IN */
                             fsal_name_t * p_node_name, /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             fsal_nodetype_t nodetype,  /* IN */
                             fsal_dev_t * dev,  /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT (handle to the created node) */
                             fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_opendir(fsal_handle_t * p_dir_handle,  /* IN */
                              fsal_op_context_t * p_context, /* IN */
                              fsal_dir_t * p_dir_descriptor, /* OUT */
                              fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_readdir(fsal_dir_t * p_dir_descriptor, /* IN */
                              fsal_cookie_t start_position,  /* IN */
                              fsal_attrib_mask_t get_attr_mask, /* IN */
                              fsal_mdsize_t buffersize, /* IN */
                              fsal_dirent_t * p_pdirent,        /* OUT */
                              fsal_cookie_t * p_end_position,        /* OUT */
                              fsal_count_t * p_nb_entries,      /* OUT */
                              bool * p_end_of_dir /* OUT */ );

fsal_status_t ZFSFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */ );

fsal_status_t ZFSFSAL_open_by_name(fsal_handle_t * dirhandle,        /* IN */
                                   fsal_name_t * filename,      /* IN */
                                   fsal_op_context_t * p_context,    /* IN */
                                   fsal_openflags_t openflags,  /* IN */
                                   fsal_file_t * file_descriptor,    /* OUT */
                                   fsal_attrib_list_t *
                                   file_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_open(fsal_handle_t * p_filehandle,     /* IN */
                           fsal_op_context_t * p_context,    /* IN */
                           fsal_openflags_t openflags,  /* IN */
                           fsal_file_t * p_file_descriptor,  /* OUT */
                           fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_read(fsal_file_t * p_file_descriptor,  /* IN */
                           fsal_seek_t * p_seek_descriptor,     /* [IN] */
                           fsal_size_t buffer_size,     /* IN */
                           caddr_t buffer,      /* OUT */
                           fsal_size_t * p_read_amount, /* OUT */
                           bool * p_end_of_file /* OUT */ );

fsal_status_t ZFSFSAL_write(fsal_file_t * p_file_descriptor, /* IN */
                            fsal_op_context_t * p_context,   /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* IN */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* IN */
                            fsal_size_t * p_write_amount /* OUT */ );

fsal_status_t ZFSFSAL_close(fsal_file_t * p_file_descriptor /* IN */ );

fsal_status_t ZFSFSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle,   /* IN */
                                     fsal_op_context_t * p_context,  /* IN */
                                     fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

fsal_status_t ZFSFSAL_Init(fsal_parameter_t * init_info /* IN */ );

fsal_status_t ZFSFSAL_terminate();

fsal_status_t ZFSFSAL_test_access(fsal_op_context_t * p_context,     /* IN */
                                  fsal_accessflags_t access_type,       /* IN */
                                  fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t ZFSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_lookupPath(fsal_path_t * p_path,  /* IN */
                                 fsal_op_context_t * p_context,      /* IN */
                                 fsal_handle_t * object_handle,      /* OUT */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,      /* IN */
                                     fsal_op_context_t * p_context,  /* IN */
                                     fsal_handle_t * p_fsoot_handle, /* OUT */
                                     fsal_attrib_list_t *
                                     p_fsroot_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_rcp(fsal_handle_t * filehand:le,        /* IN */
                          fsal_op_context_t * p_context,     /* IN */
                          fsal_path_t * p_local_path,   /* IN */
                          fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t ZFSFSAL_rename(fsal_handle_t * p_old_parentdir_handle, /* IN */
                             fsal_name_t * p_old_name,  /* IN */
                             fsal_handle_t * p_new_parentdir_handle, /* IN */
                             fsal_name_t * p_new_name,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t * p_src_dir_attributes, /* [ IN/OUT ] */
                             fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ );

void ZFSFSAL_get_stats(fsal_statistics_t * stats,       /* OUT */
                       bool reset /* IN */ );

fsal_status_t ZFSFSAL_readlink(fsal_handle_t * p_linkhandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_path_t * p_link_content,    /* OUT */
                               fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_symlink(fsal_handle_t * p_parent_directory_handle,     /* IN */
                              fsal_name_t * p_linkname, /* IN */
                              fsal_path_t * p_linkcontent,      /* IN */
                              fsal_op_context_t * p_context, /* IN */
                              fsal_accessmode_t accessmode,     /* IN (ignored) */
                              fsal_handle_t * p_link_handle, /* OUT */
                              fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ );

int ZFSFSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                      fsal_status_t * status);

unsigned int ZFSFSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                         unsigned int cookie,
                                         unsigned int alphabet_len,
                                         unsigned int index_size);

unsigned int ZFSFSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, unsigned int cookie);

fsal_status_t ZFSFSAL_DigestHandle(fsal_export_context_t * exp_context,     /* IN */
                                   fsal_digesttype_t output_type,       /* IN */
                                   fsal_handle_t *in_fsal_handle, /* IN */
                                   struct fsal_handle_desc *fh_desc     /* IN/OUT */ ) ;

fsal_status_t ZFSFSAL_ExpandHandle(fsal_export_context_t * p_expcontext,     /* IN not used */
                                   fsal_digesttype_t in_type,   /* IN */
                                   struct fsal_handle_desc *fh_desc  /* IN/OUT */ ) ;

fsal_status_t ZFSFSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

fsal_status_t ZFSFSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                           fsal_parameter_t *
                                                           out_parameter);

fsal_status_t ZFSFSAL_truncate(fsal_handle_t * p_filehandle, /* IN */
                               fsal_op_context_t * p_context,        /* IN */
                               fsal_size_t length,      /* IN */
                               fsal_file_t * file_descriptor,        /* Unused in this FSAL */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

fsal_status_t ZFSFSAL_unlink(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_object_name,       /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_attrib_list_t *
                             p_parent_directory_attributes /* [IN/OUT ] */ );

char *ZFSFSAL_GetFSName();

fsal_status_t ZFSFSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,  /* IN */
                                    fsal_op_context_t * p_context,   /* IN */
                                    unsigned int xattr_id,      /* IN */
                                    fsal_attrib_list_t * p_attrs);

fsal_status_t ZFSFSAL_ListXAttrs(fsal_handle_t * p_objecthandle,     /* IN */
                                 unsigned int cookie,   /* IN */
                                 fsal_op_context_t * p_context,      /* IN */
                                 fsal_xattrent_t * xattrs_tab,  /* IN/OUT */
                                 unsigned int xattrs_tabsize,   /* IN */
                                 unsigned int *p_nb_returned,   /* OUT */
                                 int *end_of_list /* OUT */ );

fsal_status_t ZFSFSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN/OUT */
                                        size_t buffer_size,     /* IN */
                                        size_t * p_output_size /* OUT */ );

fsal_status_t ZFSFSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,       /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,        /* IN */
                                       unsigned int *pxattr_id /* OUT */ );

fsal_status_t ZFSFSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,    /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          fsal_op_context_t * p_context,     /* IN */
                                          caddr_t buffer_addr,  /* IN/OUT */
                                          size_t buffer_size,   /* IN */
                                          size_t * p_output_size /* OUT */ );

fsal_status_t ZFSFSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,  /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * p_context,   /* IN */
                                    caddr_t buffer_addr,        /* IN */
                                    size_t buffer_size, /* IN */
                                    int create /* IN */ );

fsal_status_t ZFSFSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,      /* IN */
                                        unsigned int xattr_id,  /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        caddr_t buffer_addr,    /* IN */
                                        size_t buffer_size /* IN */ );

fsal_status_t ZFSFSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,        /* IN */
                                      fsal_op_context_t * p_context, /* IN */
                                      unsigned int xattr_id) /* IN */ ;

fsal_status_t ZFSFSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,      /* IN */
                                        fsal_op_context_t * p_context,       /* IN */
                                        const fsal_name_t * xattr_name) /* IN */ ;

int ZFSFSAL_GetXattrOffsetSetable( void ) ;

unsigned int ZFSFSAL_GetFileno(fsal_file_t * pfile);

fsal_status_t ZFSFSAL_getextattrs(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_extattrib_list_t * p_object_attributes /* OUT */) ;

fsal_status_t ZFSFSAL_commit( fsal_file_t * p_file_descriptor,
                            fsal_off_t    offset,
                            fsal_size_t   size ) ;


#endif
