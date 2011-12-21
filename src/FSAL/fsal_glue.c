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

#include <string.h> /* For strncpy */

#define fsal_increment_nbcall( _f_,_struct_status_ )

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_up.h"

int __thread my_fsalid = -1 ;

fsal_functions_t fsal_functions_array[NB_AVAILABLE_FSAL];
fsal_const_t fsal_consts_array[NB_AVAILABLE_FSAL];

#ifdef _USE_SHARED_FSAL
#define fsal_functions fsal_functions_array[my_fsalid]
#define fsal_consts fsal_consts_array[my_fsalid]
#else
#define fsal_functions fsal_functions_array[0]
#define fsal_consts fsal_consts_array[0]
#endif


int FSAL_name2fsalid( char * fsname )
{
   if(      !strncasecmp( fsname, "CEPH",     MAXNAMLEN ) ) return FSAL_CEPH_ID ;
   else if( !strncasecmp( fsname, "HPSS",     MAXNAMLEN ) ) return FSAL_HPSS_ID ;
   else if( !strncasecmp( fsname, "SNMP",     MAXNAMLEN ) ) return FSAL_SNMP_ID ;
   else if( !strncasecmp( fsname, "ZFS",      MAXNAMLEN ) ) return FSAL_ZFS_ID  ;
   else if( !strncasecmp( fsname, "FUSELIKE", MAXNAMLEN ) ) return FSAL_FUSELIKE_ID ; 
   else if( !strncasecmp( fsname, "LUSTRE",   MAXNAMLEN ) ) return FSAL_LUSTRE_ID ;
   else if( !strncasecmp( fsname, "POSIX",    MAXNAMLEN ) ) return FSAL_POSIX_ID ;
   else if( !strncasecmp( fsname, "VFS",      MAXNAMLEN ) ) return FSAL_VFS_ID ;
   else if( !strncasecmp( fsname, "GPFS",     MAXNAMLEN ) ) return FSAL_GPFS_ID ;
   else if( !strncasecmp( fsname, "PROXY",    MAXNAMLEN ) ) return FSAL_PROXY_ID ;
   else if( !strncasecmp( fsname, "XFS",      MAXNAMLEN ) ) return FSAL_XFS_ID ;
   else return -1 ;
} /* FSAL_name2fsalid */

char * FSAL_fsalid2name( int fsalid )
{
  switch( fsalid )
   {
	case FSAL_CEPH_ID:
		return "CEPH" ;
		break ;

	case FSAL_HPSS_ID:
		return "HPSS" ;
		break ;

	case FSAL_SNMP_ID:
		return "SNMP" ;
		break ;

	case FSAL_ZFS_ID:
		return "ZFS" ;
		break ;

	case FSAL_FUSELIKE_ID: 
		return "FUSELIKE" ;
		break ;

	case FSAL_LUSTRE_ID:
		return "LUSTRE" ;
		break ;

	case FSAL_POSIX_ID:
		return "POSIX" ;
		break ;

	case FSAL_VFS_ID:
		return "VFS" ;
		break ;

	case FSAL_GPFS_ID:
		return "GPFS" ;
		break ;

	case FSAL_PROXY_ID:
		return "PROXY" ;
		break ;

	case FSAL_XFS_ID:
		return "XFS" ;
		break ;

	default:
		return "not found" ;
		break ;

   }
  return "You should not see this message... Implementation error !!!!" ;
} /* FSAL_fsalid2name */

/* Split the fsal_param into a fsalid and a path for a fsal lib 
 * for example "XFS:/usr/lib/libfsalxfs.so.1.1.0" will produce FSAL_XFS_ID and "/usr/lib/libfsalxfs.so.1.1.0"  */
int FSAL_param_load_fsal_split( char * param, int * pfsalid, char * pathlib )
{
  char *p1 = NULL ;
  int foundcolon = 0 ;

  char strwork[MAXPATHLEN] ;

  if( !param || !pfsalid || !pathlib )
    return -1 ;

  strncpy( strwork, param, MAXPATHLEN ) ;
  for( p1 = strwork; *p1 != '\0' ; p1++ )
   {
      if( *p1 == ':' )
       {
         foundcolon = 1 ;
         *p1 = '\0';
	 
         break ; 
       }
   }

  if( foundcolon != 1 )
    return -1 ;

  p1+= 1 ;
  strncpy( pathlib, p1, MAXPATHLEN ) ;
  *pfsalid = FSAL_name2fsalid( strwork ) ;
  return 0 ;
} /* FSAL_param_load_fsal_split */


#ifdef _USE_SHARED_FSAL
fsal_functions_t(*getfunctions) (void);
fsal_const_t(*getconsts) (void);

int FSAL_Is_Loaded( int fsalid )
{
  return (fsal_functions_array[fsalid].fsal_access == NULL)?FALSE:TRUE ;
}

void FSAL_SetId( int fsalid )
{
  my_fsalid = fsalid ;
}

int FSAL_GetId( void )
{
  return my_fsalid ;
} /* FSAL_GetId */
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

fsal_status_t FSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,         /* IN */
                                       fsal_handle_t * p_filehandle,            /* IN */
                                       fsal_op_context_t * p_context,           /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN/OUT */ )
{
   if(fsal_functions.fsal_getattrs_descriptor != NULL && p_file_descriptor != NULL)
    {
      LogFullDebug(COMPONENT_FSAL,
                   "FSAL_getattrs_descriptor calling fsal_getattrs_descriptor");
      return fsal_functions.fsal_getattrs_descriptor(p_file_descriptor, p_filehandle, p_context, p_object_attributes);
    }
  else
    {
      LogFullDebug(COMPONENT_FSAL,
                   "FSAL_getattrs_descriptor calling fsal_getattrs");
      return fsal_functions.fsal_getattrs(p_filehandle, p_context, p_object_attributes);
    }
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

fsal_status_t FSAL_cookie_to_uint64(fsal_handle_t * handle,
                                    fsal_op_context_t * context,
                                    fsal_cookie_t * cookie,
                                    uint64_t *data)
{
  if (fsal_functions.fsal_cookie_to_uint64)
    {
      return fsal_functions.fsal_cookie_to_uint64(handle,
                                                  cookie,
                                                  data);
    }
  else
    {
      *data = 0LL ;
      memcpy(data, cookie, sizeof(uint64_t));
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }
}

fsal_status_t FSAL_uint64_to_cookie(fsal_handle_t *handle,
                                    fsal_op_context_t *context,
                                    uint64_t *data,
                                    fsal_cookie_t *cookie)
{
  if (fsal_functions.fsal_uint64_to_cookie)
    {
      return fsal_functions.fsal_uint64_to_cookie(handle,
                                                  data,
                                                  cookie);
    }
  else
    {
      memset(cookie, 0, sizeof(fsal_cookie_t));
      memcpy(cookie, data, sizeof(uint64_t));
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }
}

fsal_status_t FSAL_get_cookieverf(fsal_handle_t * handle,
                                  fsal_op_context_t * context,
                                  uint64_t * verf)
{
  if (fsal_functions.fsal_get_cookieverf)
    {
      return fsal_functions.fsal_get_cookieverf(handle,
                                                verf);
    }
  else
    {
      fsal_attrib_list_t attributes;
      fsal_status_t status;

      memset(&attributes, 0, sizeof(fsal_attrib_list_t));
      attributes.asked_attributes = FSAL_ATTR_MTIME;
      status = FSAL_getattrs(handle,
                             context,
                             &attributes);
      if (FSAL_IS_ERROR(status))
        {
          return status;
        }
      else
        {
          memcpy(verf, &attributes.mtime, sizeof(uint64_t));
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
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
  return fsal_functions.fsal_getclientcontext(p_thr_context, p_export_context,
					      uid, gid,
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

fsal_status_t FSAL_sync(fsal_file_t * p_file_descriptor)
{
  return fsal_functions.fsal_sync(p_file_descriptor);
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
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_handle_t do not match: %u |%lu !!!!",
               fsal_consts.fsal_handle_t_size, sizeof(fsal_handle_t));
      exit(1);
    }
  if(fsal_consts.fsal_cookie_t_size != sizeof(fsal_cookie_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_cookie_t do not match: %u |%lu !!!!",
               fsal_consts.fsal_cookie_t_size, sizeof(fsal_cookie_t));
      exit(1);
    }

#if 0
  if(fsal_consts.fsal_op_context_t_size != sizeof(fsal_op_context_t) - sizeof(void *))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_op_context_t do not match: %u |%u !!!!",
               fsal_consts.fsal_op_context_t_size,
               sizeof(fsal_op_context_t) - sizeof(void *));
    }

  if(fsal_consts.fsal_export_context_t_size != sizeof(fsal_export_context_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_export_context_t do not match: %u |%u !!!!",
               fsal_consts.fsal_export_context_t_size, sizeof(fsal_export_context_t));
    }

  if(fsal_consts.fsal_file_t_size != sizeof(fsal_file_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_file_context_t do not match: %u |%u !!!!",
               fsal_consts.fsal_file_t_size, sizeof(fsal_file_t));
    }

  if(fsal_consts.fsal_cred_t_size != sizeof(fsal_cred_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_cred_t do not match: %u |%u !!!!",
               fsal_consts.fsal_cred_t_size, sizeof(fsal_cred_t));
    }

  if(fsal_consts.fs_specific_initinfo_t_size != sizeof(fs_specific_initinfo_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fs_specific_initinfo_t do not match: %u |%u !!!!",
               fsal_consts.fs_specific_initinfo_t_size, sizeof(fs_specific_initinfo_t));
    }

  if(fsal_consts.fsal_dir_t_size != sizeof(fsal_dir_t))
    {
      LogFatal(COMPONENT_FSAL,
               "Implementation Error, local and specific fsal_dir_t do not match: %u |%u !!!!",
               fsal_consts.fsal_dir_t_size, sizeof(fsal_dir_t));
    }
#endif /* 0 */

  return fsal_functions.fsal_init(init_info);
#else                          /* USE_SHARED_FSAL */

  return fsal_functions.fsal_init(init_info);
#endif                          /* USE_SHARED_FSAL */
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

unsigned int FSAL_Handle_to_Hash_both(fsal_handle_t * p_handle, unsigned int cookie, unsigned int alphabet_len,
                                      unsigned int index_size, unsigned int * phashval, unsigned int *prbtval ) 
{
  if( fsal_functions.fsal_handle_to_hash_both != NULL ) 
    return fsal_functions.fsal_handle_to_hash_both( p_handle, cookie, alphabet_len, index_size, phashval, prbtval) ;
  else
    {
        if( phashval == NULL || prbtval == NULL )
           return 0 ;

        *phashval = fsal_functions.fsal_handle_to_hashindex( p_handle, cookie, alphabet_len, index_size ) ;
        *prbtval = fsal_functions.fsal_handle_to_rbtindex( p_handle, cookie);

        return 1 ;
    }
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
#ifdef _USE_SHARED_FSAL
  return "Multiple Dynamic FSAL" ;
#else
  return fsal_functions.fsal_getfsname();
#endif
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

fsal_status_t FSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                            fsal_handle_t     * p_filehandle,        /* IN */
                            fsal_op_context_t * p_context,           /* IN */
                            void              * p_owner,             /* IN (opaque to FSAL) */
                            fsal_lock_op_t      lock_op,             /* IN */
                            fsal_lock_param_t   request_lock,        /* IN */
                            fsal_lock_param_t * conflicting_lock)    /* OUT */
{
  if(fsal_functions.fsal_lock_op != NULL)
    return fsal_functions.fsal_lock_op(p_file_descriptor,
                                       p_filehandle,
                                       p_context,
                                       p_owner,
                                       lock_op,
                                       request_lock,
                                       conflicting_lock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);
}

/* FSAL_UP functions */
#ifdef _USE_FSAL_UP
fsal_status_t FSAL_UP_Init( fsal_up_event_bus_parameter_t * pebparam,      /* IN */
                               fsal_up_event_bus_context_t * pupebcontext     /* OUT */)
{
  if (fsal_functions.fsal_up_init == NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_UP_init);
  else
    return fsal_functions.fsal_up_init(pebparam, pupebcontext);
}

fsal_status_t FSAL_UP_AddFilter( fsal_up_event_bus_filter_t * pupebfilter,  /* IN */
                                    fsal_up_event_bus_context_t * pupebcontext /* INOUT */ )
{
  if (fsal_functions.fsal_up_addfilter == NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_UP_addfilter);
  else
    return fsal_functions.fsal_up_addfilter(pupebfilter, pupebcontext);
}

fsal_status_t FSAL_UP_GetEvents( fsal_up_event_t ** pevents,            /* OUT */
				 fsal_count_t * event_nb,          /* IN */
				 fsal_time_t timeout,                       /* IN */
				 fsal_count_t * peventfound,                /* OUT */
				 fsal_up_event_bus_context_t * pupebcontext /* IN */ )
{
  if (fsal_functions.fsal_up_getevents == NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_UP_getevents);
  else
    return fsal_functions.fsal_up_getevents(pevents, event_nb, timeout,
					    peventfound, pupebcontext);
}
#endif /* _USE_FSAL_UP */

#ifdef _USE_SHARED_FSAL
int FSAL_LoadLibrary(char *path)
{
  void *handle;
  char *error;

  LogEvent(COMPONENT_FSAL, "Load shared FSAL : %s", path);

  if((handle = dlopen(path, RTLD_LAZY)) == NULL)
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL_LoadLibrary: could not load fsal: %s",
               dlerror());
      return 0;
    }

  /* Clear any existing error : dlerror will be used to check if dlsym succeeded or not */
  dlerror();

  /* Map FSAL_GetFunctions */
  *(void **)(&getfunctions) = dlsym(handle, "FSAL_GetFunctions");
  if((error = dlerror()) != NULL)
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL_LoadLibrary: Could not map symbol FSAL_GetFunctions:%s",
               error);
      return 0;
    }
  /* Map FSAL_GetConsts */
  *(void **)(&getconsts) = dlsym(handle, "FSAL_GetConsts");
  if((error = dlerror()) != NULL)
    {
      LogMajor(COMPONENT_FSAL,
               "FSAL_LoadLibrary: Could not map symbol FSAL_GetConsts:%s",
               error);
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
