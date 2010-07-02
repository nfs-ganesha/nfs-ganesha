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

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_glue.h"

extern fsal_functions_t fsal_xfs_functions ;

fsal_status_t FSAL_access(fsal_handle_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */)
{
  return fsal_xfs_functions.fsal_access( object_handle, p_context, access_type, object_attributes ) ;
} 

fsal_status_t FSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* IN/OUT */) 
{
  return fsal_xfs_functions.fsal_getattrs( p_filehandle, p_context, p_object_attributes ) ;
}

fsal_status_t FSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes    /* [ IN/OUT ] */)
{
  return fsal_xfs_functions.fsal_setattrs( p_filehandle, p_context, p_attrib_set, p_object_attributes ) ;
}

fsal_status_t FSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */ ) 
{
  return fsal_xfs_functions.fsal_buildexportcontext( p_export_context, p_export_path, fs_specific_options ) ;
}

fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_thr_context) 
{
  return fsal_xfs_functions.fsal_initclientcontext( p_thr_context ) ;
}

fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups  /* IN */ ) 
{
  return fsal_xfs_functions.fsal_getclientcontext( p_thr_context, p_export_context, uid, gid, alt_groups, nb_alt_groups ) ;
}

fsal_status_t FSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */ ) 
{
  return fsal_xfs_functions.fsal_create( p_parent_directory_handle, p_filename, p_context,
					 accessmode, p_object_handle, p_object_attributes ) ;
}

fsal_status_t FSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * p_object_handle,       /* OUT */
                         fsal_attrib_list_t * p_object_attributes       /* [ IN/OUT ] */)
{
  return fsal_xfs_functions.fsal_mkdir( p_parent_directory_handle, p_dirname, p_context,
					accessmode, p_object_handle, p_object_attributes ) ;
}


fsal_status_t FSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                        fsal_handle_t * p_dir_handle,   /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * p_attributes       /* [ IN/OUT ] */ )
{
  return fsal_xfs_functions.fsal_link( p_target_handle, p_dir_handle, p_link_name, p_context, p_attributes ) ;
}

fsal_status_t FSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                          fsal_attrib_list_t * node_attributes  /* [ IN/OUT ] */ ) 
{
  return fsal_xfs_functions.fsal_mknode( parentdir_handle, p_node_name, p_context, accessmode,
					 nodetype, dev,   p_object_handle, node_attributes ) ;
}


fsal_status_t FSAL_opendir(fsal_handle_t * p_dir_handle,        /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * p_dir_descriptor,       /* OUT */
                           fsal_attrib_list_t * p_dir_attributes        /* [ IN/OUT ] */) 
{
  return fsal_xfs_functions.fsal_opendir( p_dir_handle, p_context, p_dir_descriptor, p_dir_attributes ) ;
} 

fsal_status_t FSAL_readdir(fsal_dir_t * p_dir_descriptor,       /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * p_pdirent,   /* OUT */
                           fsal_cookie_t * p_end_position,      /* OUT */
                           fsal_count_t * p_nb_entries, /* OUT */
                           fsal_boolean_t * p_end_of_dir        /* OUT */ ) 
{
  return fsal_xfs_functions.fsal_readdir( p_dir_descriptor, start_position, get_attr_mask,
			     		  buffersize, p_pdirent, p_end_position, p_nb_entries, p_end_of_dir ) ;
}


fsal_status_t FSAL_closedir(fsal_dir_t * p_dir_descriptor       /* IN */ )
{
  return fsal_xfs_functions.fsal_closedir( p_dir_descriptor ) ;
}

fsal_status_t FSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  return fsal_xfs_functions.fsal_open_by_name( dirhandle, filename, p_context, openflags, file_descriptor, file_attributes ) ;
}

fsal_status_t FSAL_open(fsal_handle_t * p_filehandle,   /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        fsal_file_t * p_file_descriptor,        /* OUT */
                        fsal_attrib_list_t * p_file_attributes  /* [ IN/OUT ] */ )
{
  return fsal_xfs_functions.fsal_open( p_filehandle, p_context, openflags, p_file_descriptor, p_file_attributes ) ;
}

fsal_status_t FSAL_read(fsal_file_t * p_file_descriptor,        /* IN */
                        fsal_seek_t * p_seek_descriptor,        /* [IN] */
                        fsal_size_t buffer_size,        /* IN */
                        caddr_t buffer, /* OUT */
                        fsal_size_t * p_read_amount,    /* OUT */
                        fsal_boolean_t * p_end_of_file  /* OUT */)
{
  return fsal_xfs_functions.fsal_read( p_file_descriptor, p_seek_descriptor, buffer_size, buffer, p_read_amount, p_end_of_file ) ;
}
  

fsal_status_t FSAL_write(fsal_file_t * p_file_descriptor,       /* IN */
                         fsal_seek_t * p_seek_descriptor,       /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * p_write_amount   /* OUT */ )
{
  return fsal_xfs_functions.fsal_write( p_file_descriptor, p_seek_descriptor, buffer_size, buffer, p_write_amount ) ;
}

fsal_status_t FSAL_close(fsal_file_t * p_file_descriptor        /* IN */ )
{
  return fsal_xfs_functions.fsal_close( p_file_descriptor ) ;
}

fsal_status_t FSAL_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
   return fsal_xfs_functions.fsal_open_by_fileid( filehandle, fileid, p_context, openflags, file_descriptor, file_attributes ) ;
}

fsal_status_t FSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid)
{
   return fsal_xfs_functions.fsal_close_by_fileid( file_descriptor, fileid ) ;
}

fsal_status_t FSAL_static_fsinfo(fsal_handle_t * p_filehandle,  /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_staticfsinfo_t * p_staticinfo     /* OUT */)
{
  return fsal_xfs_functions.fsal_static_fsinfo( p_filehandle, p_context, p_staticinfo ) ;
}

fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * p_dynamicinfo  /* OUT */ )
{
  return fsal_xfs_functions.fsal_dynamic_fsinfo( p_filehandle, p_context, p_dynamicinfo ) ;
}

fsal_status_t FSAL_Init(fsal_parameter_t * init_info    /* IN */)
{
  return fsal_xfs_functions.fsal_init( init_info ) ;
}

fsal_status_t FSAL_terminate()
{
  return fsal_xfs_functions.fsal_terminate() ;
}

