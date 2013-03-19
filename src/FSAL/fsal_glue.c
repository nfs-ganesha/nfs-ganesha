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


#include <string.h> /* For strncpy */

#define fsal_increment_nbcall( _f_,_struct_status_ )

#include "fsal.h"
#include "fsal_glue.h"
#include "fsal_up.h"
#include "timers.h"

int __thread my_fsalid = -1 ;

fsal_functions_t fsal_functions_array[NB_AVAILABLE_FSAL];
fsal_const_t fsal_consts_array[NB_AVAILABLE_FSAL];

#define fsal_functions fsal_functions_array[0]
#define fsal_consts fsal_consts_array[0]

#ifdef _USE_PNFS_MDS
fsal_mdsfunctions_t fsal_mdsfunctions;
#endif /* _USE_PNFS_MDS */
#ifdef _USE_PNFS_DS
fsal_dsfunctions_t fsal_dsfunctions;
#endif /* _USE_PNFS_DS */

fsal_status_t FSAL_getattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_object_attributes /* IN/OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getattrs(p_filehandle, p_context, p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,         /* IN */
                                       fsal_handle_t * p_filehandle,            /* IN */
                                       fsal_op_context_t * p_context,           /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN/OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

   if(fsal_functions.fsal_getattrs_descriptor != NULL && p_file_descriptor != NULL)
    {
      LogFullDebug(COMPONENT_FSAL,
                   "FSAL_getattrs_descriptor calling fsal_getattrs_descriptor");
      rc = fsal_functions.fsal_getattrs_descriptor(p_file_descriptor, p_filehandle, p_context, p_object_attributes);
    }
  else
    {
      LogFullDebug(COMPONENT_FSAL,
                   "FSAL_getattrs_descriptor calling fsal_getattrs");
      rc = fsal_functions.fsal_getattrs(p_filehandle, p_context, p_object_attributes);
    }

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_setattrs(fsal_handle_t * p_filehandle,       /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * p_attrib_set,  /* IN */
                            fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_setattrs(p_filehandle, p_context, p_attrib_set,
                                    p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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
                                    fsal_op_context_t * p_context,
                                    fsal_cookie_t * cookie,
                                    uint64_t *data)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  if (fsal_functions.fsal_cookie_to_uint64)
    {
      rc = fsal_functions.fsal_cookie_to_uint64(handle,
                                                  cookie,
                                                  data);
    }
  else
    {
      *data = 0LL ;
      memcpy(data, cookie, sizeof(uint64_t));
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_uint64_to_cookie(fsal_handle_t *handle,
                                    fsal_op_context_t *p_context,
                                    uint64_t *data,
                                    fsal_cookie_t *cookie)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  if (fsal_functions.fsal_uint64_to_cookie)
    {
      rc = fsal_functions.fsal_uint64_to_cookie(handle,
                                                data,
                                                cookie);
    }
  else
    {
      memset(cookie, 0, sizeof(fsal_cookie_t));
      memcpy(cookie, data, sizeof(uint64_t));
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_get_cookieverf(fsal_handle_t * handle,
                                  fsal_op_context_t * p_context,
                                  uint64_t * verf)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  if (fsal_functions.fsal_get_cookieverf)
    {
      rc = fsal_functions.fsal_get_cookieverf(handle,
                                                verf);
    }
  else
    {
      fsal_attrib_list_t attributes;

      memset(&attributes, 0, sizeof(fsal_attrib_list_t));
      attributes.asked_attributes = FSAL_ATTR_MTIME;
      rc = FSAL_getattrs(handle,
                         p_context,
                         &attributes);
      if (!FSAL_IS_ERROR(rc))
        {
          memcpy(verf, &attributes.mtime, sizeof(uint64_t));
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}


fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_context)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_initclientcontext(p_context);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getclientcontext(p_context, p_export_context,
					    uid, gid,
					    alt_groups, nb_alt_groups);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_create(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_create(p_parent_directory_handle, p_filename, p_context,
                                  accessmode, p_object_handle, p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_mkdir(fsal_handle_t * p_parent_directory_handle,     /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * p_object_handle,       /* OUT */
                         fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_mkdir(p_parent_directory_handle, p_dirname, p_context,
                                 accessmode, p_object_handle, p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_link(fsal_handle_t * p_target_handle,        /* IN */
                        fsal_handle_t * p_dir_handle,   /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_link(p_target_handle, p_dir_handle, p_link_name, p_context,
                                p_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_mknode(parentdir_handle, p_node_name, p_context, accessmode,
                                  nodetype, dev, p_object_handle, node_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_opendir(fsal_handle_t * p_dir_handle,        /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * p_dir_descriptor,       /* OUT */
                           fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_opendir(p_dir_handle, p_context, p_dir_descriptor,
                                   p_dir_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_readdir(fsal_dir_t * p_dir_descriptor,       /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * p_pdirent,   /* OUT */
                           fsal_cookie_t * p_end_position,      /* OUT */
                           fsal_count_t * p_nb_entries, /* OUT */
                           fsal_boolean_t * p_end_of_dir /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_readdir(p_dir_descriptor, p_context, start_position, get_attr_mask,
                                   buffersize, p_pdirent, p_end_position, p_nb_entries,
                                   p_end_of_dir);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_closedir(fsal_dir_t * p_dir_descriptor, /* IN */ 
                            fsal_op_context_t * p_context  /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_closedir(p_dir_descriptor, p_context);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_open_by_name(dirhandle, filename, p_context, openflags,
                                        file_descriptor, file_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_open(fsal_handle_t * p_filehandle,   /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        fsal_file_t * p_file_descriptor,        /* OUT */
                        fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_open(p_filehandle, p_context, openflags, p_file_descriptor,
                                p_file_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_read(fsal_file_t * p_file_descriptor,        /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_seek_t * p_seek_descriptor,        /* [IN] */
                        fsal_size_t buffer_size,        /* IN */
                        caddr_t buffer, /* OUT */
                        fsal_size_t * p_read_amount,    /* OUT */
                        fsal_boolean_t * p_end_of_file /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_read(p_file_descriptor, p_context, p_seek_descriptor, buffer_size,
                                buffer, p_read_amount, p_end_of_file);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_write(fsal_file_t * p_file_descriptor,       /* IN */
                         fsal_op_context_t * p_context,         /* IN */
                         fsal_seek_t * p_seek_descriptor,       /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * p_write_amount /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_write(p_file_descriptor, p_context,
                                 p_seek_descriptor, buffer_size,
                                 buffer, p_write_amount);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_commit( fsal_file_t * p_file_descriptor, 
                         fsal_op_context_t * p_context,        /* IN */
                         fsal_off_t    offset,
                         fsal_size_t   length )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_commit(p_file_descriptor, p_context, offset, length );

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_close(fsal_file_t * p_file_descriptor, /* IN */
                         fsal_op_context_t * p_context  /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  if(p_context != NULL)
    timer_start = timer_get();

  rc = fsal_functions.fsal_close(p_file_descriptor, p_context);

  if(p_context != NULL)
    {
      timer_end = timer_get();

      p_context->latency += timer_end - timer_start;
      p_context->count++;
    }

  return rc;
}

fsal_status_t FSAL_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_open_by_fileid(filehandle, fileid, p_context, openflags,
                                          file_descriptor, file_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_u64_t fileid)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_close_by_fileid(file_descriptor, p_context, fileid);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_dynamic_fsinfo(p_filehandle, p_context, p_dynamicinfo);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_Init(fsal_parameter_t * init_info /* IN */ )
{
  return fsal_functions.fsal_init(init_info);
}

fsal_status_t FSAL_terminate()
{
  return fsal_functions.fsal_terminate();
}

fsal_status_t FSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_accessflags_t * allowed,  /* OUT */
                               fsal_accessflags_t * denied,  /* OUT */
                               fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_test_access(p_context,
                                       access_type,
                                       allowed,
                                       denied,
                                       p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_lookup(p_parent_directory_handle, p_filename, p_context,
                                  p_object_handle, p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_lookuppath(p_path, p_context, object_handle,
                                      p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t *
                                  p_fsroot_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_lookupjunction(p_junction_handle, p_context, p_fsoot_handle,
                                          p_fsroot_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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

fsal_status_t FSAL_check_quota( char *path,  /* IN */
                                fsal_quota_type_t   quota_type,
                                fsal_uid_t          fsal_uid)      /* IN */
{
  return fsal_functions.fsal_check_quota( path, quota_type, fsal_uid ) ;
}

fsal_status_t FSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_rcp(filehandle, p_context, p_local_path, transfer_opt);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * p_new_parentdir_handle,       /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * p_src_dir_attributes,    /* [ IN/OUT ] */
                          fsal_attrib_list_t * p_tgt_dir_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_rename(p_old_parentdir_handle, p_old_name,
                                  p_new_parentdir_handle, p_new_name, p_context,
                                  p_src_dir_attributes, p_tgt_dir_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_readlink(p_linkhandle, p_context, p_link_content,
                                    p_link_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_symlink(fsal_handle_t * p_parent_directory_handle,   /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored) */
                           fsal_handle_t * p_link_handle,       /* OUT */
                           fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_symlink(p_parent_directory_handle, p_linkname, p_linkcontent,
                                   p_context, accessmode, p_link_handle,
                                   p_link_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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
                                struct fsal_handle_desc *fh_desc /* OUT */ )
{
  return fsal_functions.fsal_digesthandle(p_expcontext, output_type,
                                          p_in_fsal_handle, fh_desc);
}

fsal_status_t FSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t in_type,      /* IN */
                                struct fsal_handle_desc *fh_desc        /* IN OUT */ )
{
  return fsal_functions.fsal_expandhandle(p_expcontext, in_type, fh_desc);
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
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_truncate(p_filehandle, p_context, length, file_descriptor,
                                    p_object_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_unlink(fsal_handle_t * p_parent_directory_handle,    /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t *
                          p_parent_directory_attributes /* [IN/OUT ] */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_unlink(p_parent_directory_handle, p_object_name, p_context,
                                  p_parent_directory_attributes);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

char *FSAL_GetFSName()
{
  return fsal_functions.fsal_getfsname();
}

fsal_status_t FSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 unsigned int xattr_id, /* IN */
                                 fsal_attrib_list_t * p_attrs)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getxattrattrs(p_objecthandle, p_context, xattr_id, p_attrs);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_ListXAttrs(fsal_handle_t * p_objecthandle,   /* IN */
                              unsigned int cookie,      /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_xattrent_t * xattrs_tab,     /* IN/OUT */
                              unsigned int xattrs_tabsize,      /* IN */
                              unsigned int *p_nb_returned,      /* OUT */
                              int *end_of_list /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_listxattrs(p_objecthandle, cookie, p_context,
                                      xattrs_tab, xattrs_tabsize, p_nb_returned,
                                      end_of_list);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN/OUT */
                                     size_t buffer_size,        /* IN */
                                     size_t * p_output_size /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getxattrvaluebyid(p_objecthandle, xattr_id, p_context,
                                             buffer_addr, buffer_size, p_output_size);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    unsigned int *pxattr_id /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getxattridbyname(p_objecthandle, xattr_name, p_context,
                                            pxattr_id);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       caddr_t buffer_addr,     /* IN/OUT */
                                       size_t buffer_size,      /* IN */
                                       size_t * p_output_size /* OUT */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getxattrvaluebyname(p_objecthandle, xattr_name, p_context,
                                               buffer_addr, buffer_size, p_output_size);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                 const fsal_name_t * xattr_name,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 caddr_t buffer_addr,   /* IN */
                                 size_t buffer_size,    /* IN */
                                 int create /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_setxattrvalue(p_objecthandle, xattr_name, p_context,
                                         buffer_addr, buffer_size, create);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size /* IN */ )
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_setxattrvaluebyid(p_objecthandle, xattr_id, p_context,
                                             buffer_addr, buffer_size);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   unsigned int xattr_id)       /* IN */
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_removexattrbyid(p_objecthandle, p_context, xattr_id);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     const fsal_name_t * xattr_name)    /* IN */
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_removexattrbyname(p_objecthandle, p_context, xattr_name);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

unsigned int FSAL_GetFileno(fsal_file_t * pfile)
{
  return fsal_functions.fsal_getfileno(pfile);
}

fsal_status_t FSAL_getextattrs( fsal_handle_t * p_filehandle, /* IN */
                                fsal_op_context_t * p_context,        /* IN */
                                fsal_extattrib_list_t * p_object_attributes /* OUT */)
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  rc = fsal_functions.fsal_getextattrs( p_filehandle, p_context, p_object_attributes ) ;

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                            fsal_handle_t     * p_filehandle,        /* IN */
                            fsal_op_context_t * p_context,           /* IN */
                            void              * p_owner,             /* IN (opaque to FSAL) */
                            fsal_lock_op_t      lock_op,             /* IN */
                            fsal_lock_param_t   request_lock,        /* IN */
                            fsal_lock_param_t * conflicting_lock)    /* OUT */
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  if(fsal_functions.fsal_lock_op != NULL)
    rc = fsal_functions.fsal_lock_op(p_file_descriptor,
                                       p_filehandle,
                                       p_context,
                                       p_owner,
                                       lock_op,
                                       request_lock,
                                       conflicting_lock);
  else
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
}

fsal_status_t FSAL_share_op( fsal_file_t       * p_file_descriptor,   /* IN */
                             fsal_handle_t     * p_filehandle,        /* IN */
                             fsal_op_context_t * p_context,           /* IN */
                             void              * p_owner,             /* IN (opaque to FSAL) */
                             fsal_share_param_t  request_share)       /* IN */
{
  fsal_status_t  rc;
  msectimer_t    timer_start, timer_end;

  timer_start = timer_get();

  if(fsal_functions.fsal_share_op != NULL)
    rc = fsal_functions.fsal_share_op(p_file_descriptor,
                                      p_filehandle,
                                      p_context,
                                      p_owner,
                                      request_share);
  else
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_share_op);

  timer_end = timer_get();

  p_context->latency += timer_end - timer_start;
  p_context->count++;

  return rc;
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

fsal_status_t FSAL_UP_GetEvents( struct glist_head * pevent_head,            /* OUT */
                                 fsal_count_t * event_nb,          /* IN */
                                 fsal_time_t timeout,                       /* IN */
                                 fsal_count_t * peventfound,                /* OUT */
                                 fsal_up_event_bus_context_t * pupebcontext /* IN */ )
{
  if (fsal_functions.fsal_up_getevents == NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_UP_getevents);
  else
    return fsal_functions.fsal_up_getevents(pevent_head, event_nb, timeout,
                                            peventfound, pupebcontext);
}
#endif /* _USE_FSAL_UP */

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

#ifdef _USE_PNFS_MDS
void FSAL_LoadMDSFunctions(void)
{
  fsal_mdsfunctions = FSAL_GetMDSFunctions();
}
#endif /* _USE_PNFS_MDS */

#ifdef _USE_PNFS_DS
void FSAL_LoadDSFunctions(void)
{
  fsal_dsfunctions = FSAL_GetDSFunctions();
}
#endif /* _USE_PNFS_DS */
