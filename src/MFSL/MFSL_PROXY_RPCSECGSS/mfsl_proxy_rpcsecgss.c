/*
 *
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
 * \file    fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:41:01 $
 * \version $Revision: 1.72 $
 * \brief   File System Abstraction Layer interface.
 *
 *
 */

#include "config.h"

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"

#ifndef _USE_SWIG
/******************************************************
 *            Attribute mask management.
 ******************************************************/

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t MFSL_SetDefault_parameter(mfsl_parameter_t * out_parameter)
{
  fsal_status_t status;

  status.major = ERR_FSAL_NO_ERROR;
  status.minor = 0;

  return status;
}                               /* MFSL_SetDefault_parameter */

/**
 * MFSL_load_FSAL_parameter_from_conf,
 *
 * Those functions initialize the FSAL init parameter
 * structure from a configuration structure.
 *
 * \param in_config (input):
 *        Structure that represents the parsed configuration file.
 * \param out_parameter (ouput)
 *        FSAL initialization structure filled according
 *        to the configuration file given as parameter.
 *
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_NOENT (missing a mandatory stanza in config file),
 *         ERR_FSAL_INVAL (invalid parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 */
fsal_status_t MFSL_load_parameter_from_conf(config_file_t in_config,
                                            mfsl_parameter_t * out_parameter)
{
  fsal_status_t status;

  status.major = ERR_FSAL_NO_ERROR;
  status.minor = 0;

  return status;
}

/** 
 *  FSAL_Init:
 *  Initializes Filesystem abstraction layer.
 */
fsal_status_t MFSL_Init(mfsl_parameter_t * init_info    /* IN */
    )
{
  fsal_status_t status;

  status.major = ERR_FSAL_NO_ERROR;
  status.minor = 0;

  return status;
}

fsal_status_t MFSL_GetContext(mfsl_context_t * pcontext)
{
  fsal_status_t status;

  status.major = ERR_FSAL_NO_ERROR;
  status.minor = 0;

  return status;
}

#endif                          /* ! _USE_SWIG */

/******************************************************
 *              Common Filesystem calls.
 ******************************************************/

fsal_status_t MFSL_lookup(mfsl_object_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          mfsl_object_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  return FSAL_lookup(&parent_directory_handle->handle,
                     p_filename, p_context, &object_handle->handle, object_attributes);
}                               /* MFSL_lookup */

fsal_status_t MFSL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              mfsl_context_t * p_mfsl_context,  /* IN */
                              mfsl_object_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{
  return FSAL_lookupPath(p_path, p_context, &object_handle->handle, object_attributes);
}                               /* MFSL_lookupPath */

fsal_status_t MFSL_lookupJunction(mfsl_object_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  mfsl_context_t * p_mfsl_context,      /* IN */
                                  mfsl_object_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    )
{
  return FSAL_lookupJunction(&p_junction_handle->handle,
                             p_context, &p_fsoot_handle->handle, p_fsroot_attributes);
}                               /* MFSL_lookupJunction */

fsal_status_t MFSL_access(mfsl_object_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  return FSAL_access(&object_handle->handle, p_context, access_type, object_attributes);
}                               /* MFSL_access */

fsal_status_t MFSL_create(mfsl_object_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          mfsl_object_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  return FSAL_create(&parent_directory_handle->handle,
                     p_filename,
                     p_context, accessmode, &object_handle->handle, object_attributes);
}                               /* MFSL_create */

fsal_status_t MFSL_mkdir(mfsl_object_t * parent_directory_handle,       /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         mfsl_context_t * p_mfsl_context,       /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         mfsl_object_t * object_handle, /* OUT */
                         fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */
    )
{
  return FSAL_mkdir(&parent_directory_handle->handle,
                    p_dirname,
                    p_context, accessmode, &object_handle->handle, object_attributes);
}                               /* MFSL_mkdir */

fsal_status_t MFSL_truncate(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_size_t length, /* IN */
                            fsal_file_t * file_descriptor,      /* INOUT */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  return FSAL_truncate(&filehandle->handle,
                       p_context, length, file_descriptor, object_attributes);
}                               /* MFSL_truncate */

fsal_status_t MFSL_getattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* IN/OUT */
    )
{
  return FSAL_getattrs(&filehandle->handle, p_context, object_attributes);
}                               /* MFSL_getattrs */

fsal_status_t MFSL_setattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * attrib_set,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  return FSAL_setattrs(&filehandle->handle, p_context, attrib_set, object_attributes);
}                               /* MFSL_setattrs */

fsal_status_t MFSL_link(mfsl_object_t * target_handle,  /* IN */
                        mfsl_object_t * dir_handle,     /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        mfsl_context_t * p_mfsl_context,        /* IN */
                        fsal_attrib_list_t * attributes /* [ IN/OUT ] */
    )
{
  return FSAL_link(&target_handle->handle,
                   &dir_handle->handle, p_link_name, p_context, attributes);
}                               /* MFSL_link */

fsal_status_t MFSL_opendir(mfsl_object_t * dir_handle,  /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_dir_t * dir_descriptor, /* OUT */
                           fsal_attrib_list_t * dir_attributes  /* [ IN/OUT ] */
    )
{
  return FSAL_opendir(&dir_handle->handle, p_context, dir_descriptor, dir_attributes);
}                               /* MFSL_opendir */

fsal_status_t MFSL_readdir(fsal_dir_t * dir_descriptor, /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * pdirent,     /* OUT */
                           fsal_cookie_t * end_position,        /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir, /* OUT */
                           mfsl_context_t * p_mfsl_context      /* IN */
    )
{
  return FSAL_readdir(dir_descriptor,
                      start_position,
                      get_attr_mask,
                      buffersize, pdirent, end_position, nb_entries, end_of_dir);

}                               /* MFSL_readdir */

fsal_status_t MFSL_closedir(fsal_dir_t * dir_descriptor,        /* IN */
                            mfsl_context_t * p_mfsl_context     /* IN */
    )
{
  return FSAL_closedir(dir_descriptor);
}                               /* FSAL_closedir */

fsal_status_t MFSL_open(mfsl_object_t * filehandle,     /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        mfsl_context_t * p_mfsl_context,        /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        fsal_file_t * file_descriptor,  /* OUT */
                        fsal_attrib_list_t * file_attributes    /* [ IN/OUT ] */
    )
{
  return FSAL_open(&filehandle->handle,
                   p_context, openflags, file_descriptor, file_attributes);
}                               /* MFSL_open */

fsal_status_t MFSL_open_by_name(mfsl_object_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                mfsl_context_t * p_mfsl_context,        /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  return FSAL_open_by_name(&dirhandle->handle,
                           filename,
                           p_context, openflags, file_descriptor, file_attributes);
}                               /* MFSL_open_by_name */

fsal_status_t MFSL_open_by_fileid(mfsl_object_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  mfsl_context_t * p_mfsl_context,      /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  return FSAL_open_by_fileid(&filehandle->handle,
                             fileid,
                             p_context, openflags, file_descriptor, file_attributes);
}                               /* MFSL_open_by_fileid */

fsal_status_t MFSL_read(fsal_file_t * file_descriptor,  /*  IN  */
                        fsal_seek_t * seek_descriptor,  /* [IN] */
                        fsal_size_t buffer_size,        /*  IN  */
                        caddr_t buffer, /* OUT  */
                        fsal_size_t * read_amount,      /* OUT  */
                        fsal_boolean_t * end_of_file,   /* OUT  */
                        mfsl_context_t * p_mfsl_context /* IN */
    )
{
  return FSAL_read(file_descriptor,
                   seek_descriptor, buffer_size, buffer, read_amount, end_of_file);
}                               /* MFSL_read */

fsal_status_t MFSL_write(fsal_file_t * file_descriptor, /* IN */
                         fsal_seek_t * seek_descriptor, /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * write_amount,    /* OUT */
                         mfsl_context_t * p_mfsl_context        /* IN */
    )
{
  return FSAL_write(file_descriptor, seek_descriptor, buffer_size, buffer, write_amount);
}                               /* MFSL_write */

fsal_status_t MFSL_close(fsal_file_t * file_descriptor, /* IN */
                         mfsl_context_t * p_mfsl_context        /* IN */
    )
{
  return FSAL_close(file_descriptor);
}                               /* MFSL_close */

fsal_status_t MFSL_sync(mfsl_file_t * file_descriptor /* IN */,
			 void * pextra)
{
   return FSAL_sync( &file_descriptor->fsal_file ) ;
}

fsal_status_t MFSL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid, mfsl_context_t * p_mfsl_context)  /* IN */
{
  return FSAL_close_by_fileid(file_descriptor, fileid);
}                               /* MFSL_close_by_fileid */

fsal_status_t MFSL_readlink(mfsl_object_t * linkhandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_path_t * p_link_content,       /* OUT */
                            fsal_attrib_list_t * link_attributes        /* [ IN/OUT ] */
    )
{
  return FSAL_readlink(&linkhandle->handle, p_context, p_link_content, link_attributes);
}                               /* MFSL_readlink */

fsal_status_t MFSL_symlink(mfsl_object_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored); */
                           mfsl_object_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes /* [ IN/OUT ] */
    )
{
  return FSAL_symlink(&parent_directory_handle->handle,
                      p_linkname,
                      p_linkcontent,
                      p_context, accessmode, &link_handle->handle, link_attributes);
}                               /* MFSL_symlink */

fsal_status_t MFSL_rename(mfsl_object_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          mfsl_object_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes       /* [ IN/OUT ] */
    )
{
  return FSAL_rename(&old_parentdir_handle->handle,
                     p_old_name,
                     &new_parentdir_handle->handle,
                     p_new_name, p_context, src_dir_attributes, tgt_dir_attributes);
}                               /* MFSL_rename */

fsal_status_t MFSL_unlink(mfsl_object_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * parentdir_attributes     /* [IN/OUT ] */
    )
{
  return FSAL_unlink(&parentdir_handle->handle,
                     p_object_name, p_context, parentdir_attributes);
}                               /* MFSL_unlink */

fsal_status_t MFSL_mknode(mfsl_object_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          mfsl_object_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * node_attributes  /* [ IN/OUT ] */
    )
{
  return FSAL_mknode(&parentdir_handle->handle,
                     p_node_name,
                     p_context,
                     accessmode,
                     nodetype, dev, &p_object_handle->handle, node_attributes);
}                               /* MFSL_mknode */

fsal_status_t MFSL_rcp(mfsl_object_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       mfsl_context_t * p_mfsl_context, /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt      /* IN */
    )
{
  return FSAL_rcp(&filehandle->handle, p_context, p_local_path, transfer_opt);
}                               /* MFSL_rcp */

fsal_status_t MFSL_rcp_by_fileid(mfsl_object_t * filehandle,    /* IN */
                                 fsal_u64_t fileid,     /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 mfsl_context_t * p_mfsl_context,       /* IN */
                                 fsal_path_t * p_local_path,    /* IN */
                                 fsal_rcpflag_t transfer_opt    /* IN */
    )
{
  return FSAL_rcp_by_fileid(&filehandle->handle,
                            fileid, p_context, p_local_path, transfer_opt);
}                               /* MFSL_rcp_by_fileid */

/* To be called before exiting */
fsal_status_t MFSL_terminate(void)
{
  fsal_status_t status;

  status.major = ERR_FSAL_NO_ERROR;
  status.minor = 0;

  return status;

}                               /* MFSL_terminate */
