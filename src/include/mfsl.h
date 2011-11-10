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

#ifndef _MFSL_H
#define _MFSL_H

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal.h"
#include "mfsl_types.h"
#include "common_utils.h"

#ifndef _USE_SWIG

#define MFSL_return( _code_, _minor_ ) do {                               \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;\
               (_struct_status_).major = (_code_) ;          \
               (_struct_status_).minor = (_minor_) ;         \
               return (_struct_status_);                     \
              } while(0)

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t MFSL_SetDefault_parameter(mfsl_parameter_t * out_parameter);

/**
 * MFSL_load_parameter_from_conf,
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
                                            mfsl_parameter_t * out_parameter);

/** 
 *  FSAL_Init:
 *  Initializes Filesystem abstraction layer.
 */
fsal_status_t MFSL_Init(mfsl_parameter_t * init_info    /* IN */
    );

fsal_status_t MFSL_GetContext(mfsl_context_t * pcontext,
                              fsal_op_context_t * pfsal_context);

fsal_status_t MFSL_RefreshContext(mfsl_context_t * pcontext,
                                  fsal_op_context_t * pfsal_context);

#endif                          /* ! _USE_SWIG */

/******************************************************
 *              Common Filesystem calls.
 ******************************************************/

fsal_status_t MFSL_lookup(mfsl_object_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          mfsl_object_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes,        /* [ IN/OUT ] */
			  void * pextra
    );

fsal_status_t MFSL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              mfsl_context_t * p_mfsl_context,  /* IN */
                              mfsl_object_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    );

fsal_status_t MFSL_lookupJunction(mfsl_object_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  mfsl_context_t * p_mfsl_context,      /* IN */
                                  mfsl_object_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    );

fsal_status_t MFSL_access(mfsl_object_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes,        /* [ IN/OUT ] */
                          void * pextra
    );

fsal_status_t MFSL_create(mfsl_object_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          mfsl_object_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes,       /* [ IN/OUT ] */
                          fsal_attrib_list_t * parent_attributes,        /* IN */
                          void * pextra
    );

fsal_status_t MFSL_mkdir(mfsl_object_t * parent_directory_handle,       /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         mfsl_context_t * p_mfsl_context,       /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         mfsl_object_t * object_handle, /* OUT */
                         fsal_attrib_list_t * object_attributes,        /* [ IN/OUT ] */
                         fsal_attrib_list_t * parent_attributes, /* IN */
			 void * pextra 
    );

fsal_status_t MFSL_truncate(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_size_t length, /* IN */
                            mfsl_file_t * file_descriptor,      /* INOUT */
                            fsal_attrib_list_t * object_attributes,      /* [ IN/OUT ] */
			    void * pextra
    );

fsal_status_t MFSL_getattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * object_attributes,      /* IN/OUT */
			    void * pextra
    );

fsal_status_t MFSL_setattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * attrib_set,    /* IN */
                            fsal_attrib_list_t * object_attributes,      /* [ IN/OUT ] */
			    void * pextra
    );

fsal_status_t MFSL_link(mfsl_object_t * target_handle,  /* IN */
                        mfsl_object_t * dir_handle,     /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        mfsl_context_t * p_mfsl_context,        /* IN */
                        fsal_attrib_list_t * attributes,     /* [ IN/OUT ] */
			void * pextra
    );

fsal_status_t MFSL_opendir(mfsl_object_t * dir_handle,  /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_dir_t * dir_descriptor, /* OUT */
                           fsal_attrib_list_t * dir_attributes,  /* [ IN/OUT ] */
			   void * pextra
    );

fsal_status_t MFSL_readdir(fsal_dir_t * dir_descriptor, /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * pdirent,     /* OUT */
                           fsal_cookie_t * end_position,        /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir, /* OUT */
                           mfsl_context_t * p_mfsl_context,      /* IN */
			   void * pextra
    );

fsal_status_t MFSL_closedir(fsal_dir_t * dir_descriptor,        /* IN */
                            mfsl_context_t * p_mfsl_context,     /* IN */
			    void * pextra
    );

fsal_status_t MFSL_open(mfsl_object_t * filehandle,     /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        mfsl_context_t * p_mfsl_context,        /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        mfsl_file_t * file_descriptor,  /* OUT */
                        fsal_attrib_list_t * file_attributes,    /* [ IN/OUT ] */
			void * pextra
    );

fsal_status_t MFSL_open_by_name(mfsl_object_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                mfsl_context_t * p_mfsl_context,        /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                mfsl_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes, /* [ IN/OUT ] */ 
				void * pextra);

fsal_status_t MFSL_open_by_fileid(mfsl_object_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  mfsl_context_t * p_mfsl_context,      /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  mfsl_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes, /* [ IN/OUT ] */ 
				  void * pextra );

fsal_status_t MFSL_read(mfsl_file_t * file_descriptor,  /*  IN  */
                        fsal_seek_t * seek_descriptor,  /* [IN] */
                        fsal_size_t buffer_size,        /*  IN  */
                        caddr_t buffer, /* OUT  */
                        fsal_size_t * read_amount,      /* OUT  */
                        fsal_boolean_t * end_of_file,   /* OUT  */
                        mfsl_context_t * p_mfsl_context, /* IN */
			void * pextra
    );

fsal_status_t MFSL_write(mfsl_file_t * file_descriptor, /* IN */
                         fsal_seek_t * seek_descriptor, /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * write_amount,    /* OUT */
                         mfsl_context_t * p_mfsl_context,        /* IN */
			 void * pextra
    );

fsal_status_t MFSL_close(mfsl_file_t * file_descriptor, /* IN */
                         mfsl_context_t * p_mfsl_context,        /* IN */
			 void * pextra
    );

fsal_status_t MFSL_sync(mfsl_file_t * file_descriptor /* IN */,
			void * pextra);

fsal_status_t MFSL_close_by_fileid(mfsl_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid,
                                   mfsl_context_t * p_mfsl_context, /* IN */
				   void * pextra ) ;

fsal_status_t MFSL_readlink(mfsl_object_t * linkhandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_path_t * p_link_content,       /* OUT */
                            fsal_attrib_list_t * link_attributes,        /* [ IN/OUT ] */
			    void * pextra 
    );

fsal_status_t MFSL_symlink(mfsl_object_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored); */
                           mfsl_object_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes, /* [ IN/OUT ] */
			   void * pextra
    );

fsal_status_t MFSL_rename(mfsl_object_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          mfsl_object_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes,       /* [ IN/OUT ] */
			  void * pextra 
    );

fsal_status_t MFSL_unlink(mfsl_object_t * parentdir_handle,     /* INOUT */
                          fsal_name_t * p_object_name,  /* IN */
                          mfsl_object_t * object_handle,        /* INOUT */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * parentdir_attributes,     /* [IN/OUT ] */
			  void * pextra
    );

fsal_status_t MFSL_mknode(mfsl_object_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          mfsl_object_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * node_attributes,  /* [ IN/OUT ] */
			  void * pextra
    );

fsal_status_t MFSL_rcp(mfsl_object_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       mfsl_context_t * p_mfsl_context, /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt,     /* IN */
		       void * pextra
    );

fsal_status_t MFSL_rcp_by_name(mfsl_object_t * filehandle,      /* IN */
                               fsal_name_t * pfilename, /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               mfsl_context_t * p_mfsl_context, /* IN */
                               fsal_path_t * p_local_path,      /* IN */
                               fsal_rcpflag_t transfer_opt,     /* IN */
			       void * pextra 
    );

fsal_status_t MFSL_rcp_by_fileid(mfsl_object_t * filehandle,    /* IN */
                                 fsal_u64_t fileid,     /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 mfsl_context_t * p_mfsl_context,       /* IN */
                                 fsal_path_t * p_local_path,    /* IN */
                                 fsal_rcpflag_t transfer_opt,    /* IN */
				 void * pextra
    );

/* To be called before exiting */
fsal_status_t MFSL_terminate();

#endif                          /* _MFSL_H */
