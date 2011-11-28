/*
 *
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

#ifndef _FSAL_H
#define _FSAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "common_utils.h"

#ifndef _USE_SWIG

#ifdef _USE_SHARED_FSAL
/******************************************************
 *    FSAL ID management for multiple FSAL support 
 ******************************************************/
void FSAL_SetId( int fsalid ) ;
int FSAL_GetId( void ) ;
int FSAL_Is_Loaded( int fsalid ) ;
#endif
int FSAL_param_load_fsal_split( char * param, int * fsalid, char * pathlib ) ;
int FSAL_name2fsalid( char * fsname ) ;
char * FSAL_fsalid2name( int fsalid ) ;


/******************************************************
 *            Attribute mask management.
 ******************************************************/

/** this macro tests if an attribute is set
 *  example :
 *  FSAL_TEST_MASK( attrib_list.supported_attributes, FSAL_ATTR_CREATION )
 */
#define FSAL_TEST_MASK( _attrib_mask_ , _attr_const_ ) \
                      ( (_attrib_mask_) & (_attr_const_) )

/** this macro sets an attribute
 *  example :
 *  FSAL_SET_MASK( attrib_list.asked_attributes, FSAL_ATTR_CREATION )
 */
#define FSAL_SET_MASK( _attrib_mask_ , _attr_const_ ) \
                    ( (_attrib_mask_) |= (_attr_const_) )

/** this macro clears the attribute mask
 *  example :
 *  FSAL_CLEAR_MASK( attrib_list.asked_attributes )
 */
#define FSAL_CLEAR_MASK( _attrib_mask_ ) \
                    ( (_attrib_mask_) = 0LL )

/******************************************************
 *              Initialization tools.
 ******************************************************/

/** This macro initializes init info behaviors and values.
 *  Examples :
 *  FSAL_SET_INIT_INFO( parameter.fs_common_info ,
 *                      maxfilesize              ,
 *                      FSAL_INIT_MAX_LIMIT      ,
 *                      0x00000000FFFFFFFFLL    );
 *
 *  FSAL_SET_INIT_INFO( parameter.fs_common_info ,
 *                      linksupport              ,
 *                      FSAL_INIT_FORCEVALUE     ,
 *                      FALSE                   );
 *
 */

#define FSAL_SET_INIT_INFO( _common_info_struct_ , _field_name_ ,   \
                                    _field_behavior_ , _value_ ) do \
           {                                                        \
             _common_info_struct_.behaviors._field_name_ = _field_behavior_ ;\
             if ( _field_behavior_ != FSAL_INIT_FS_DEFAULT )        \
               _common_info_struct_.values._field_name_ = _value_ ; \
           } while (0)

/** This macro initializes the behavior for one parameter
 *  to default filesystem value.
 *  Examples :
 *  FSAL_SET_INIT_DEFAULT( parameter.fs_common_info , case_insensitive );
 */
#define FSAL_SET_INIT_DEFAULT( _common_info_struct_ , _field_name_ ) \
        do {                                                         \
             _common_info_struct_.behaviors._field_name_             \
                = FSAL_INIT_FS_DEFAULT ;                             \
           } while (0)

/** This macro sets the cookie to its initial value
 */
#define FSAL_SET_COOKIE_BEGINNING( cookie ) memset( (char *)&cookie, 0, sizeof( fsal_cookie_t ) )

/** This macros manage conversion between directory offset and cookies
 *  BEWARE : this will probably bug with FSAL_SNMP and FSAL_CEPH 
 */
#define FSAL_SET_COOKIE_BY_OFFSET( __cookie, __offset )  \
   memcpy( (char *)&(__cookie.data), (char *)&__offset, sizeof( uint64_t ) ) 
    

#define FSAL_SET_POFFSET_BY_COOKIE( __cookie, __poffset )  \
    memcpy( (char *)__poffset, (char *)&(__cookie.data), sizeof( uint64_t ) ) 

#define FSAL_GET_EXP_CTX( popctx ) (fsal_export_context_t *)(popctx->export_context)

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t FSAL_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter);

fsal_status_t FSAL_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter);

fsal_status_t FSAL_SetDefault_FS_specific_parameter(fsal_parameter_t * out_parameter);

/**
 * FSAL_load_FSAL_parameter_from_conf,
 * FSAL_load_FS_common_parameter_from_conf,
 * FSAL_load_FS_specific_parameter_from_conf:
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
fsal_status_t FSAL_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                 fsal_parameter_t * out_parameter);

fsal_status_t FSAL_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                      fsal_parameter_t * out_parameter);
fsal_status_t FSAL_load_FS_specific_parameter_from_conf(config_file_t in_config,
                                                        fsal_parameter_t * out_parameter);

/** 
 *  FSAL_Init:
 *  Initializes Filesystem abstraction layer.
 */
fsal_status_t FSAL_Init(fsal_parameter_t * init_info    /* IN */
    );

/******************************************************
 *              FSAL Returns macros
 ******************************************************/

/**
 * Return :
 * Macro for returning from functions
 * with trace and function call increment.
 */

#define Return( _code_, _minor_ , _f_ ) do {                              \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;     \
               (_struct_status_).major = (_code_) ;                       \
               (_struct_status_).minor = (_minor_) ;                      \
               fsal_increment_nbcall( _f_,_struct_status_ );              \
               if(isDebug(COMPONENT_FSAL))                                \
                 {                                                        \
                   char _str_[256];                                       \
                   log_snprintf( _str_, 256, "%J%r",ERR_FSAL, _code_ );   \
                   if((_struct_status_).major != ERR_FSAL_NO_ERROR)       \
                     LogDebug(COMPONENT_FSAL,                             \
                       "%s returns ( %s, %d )",fsal_function_names[_f_],  \
                       _str_, _minor_);                                   \
                   else                                                   \
                     LogFullDebug(COMPONENT_FSAL,                         \
                       "%s returns ( %s, %d )",fsal_function_names[_f_],  \
                       _str_, _minor_);                                   \
                 }                                                        \
               return (_struct_status_);                                  \
              } while(0)

#define ReturnStatus( _st_, _f_ )	Return( (_st_).major, (_st_).minor, _f_ )

/**
 *  ReturnCode :
 *  Macro for returning a fsal_status_t without trace nor stats increment.
 */
#define ReturnCode( _code_, _minor_ ) do {                               \
               fsal_status_t _struct_status_ = FSAL_STATUS_NO_ERROR ;\
               (_struct_status_).major = (_code_) ;          \
               (_struct_status_).minor = (_minor_) ;         \
               return (_struct_status_);                     \
              } while(0)

/******************************************************
 *              FSAL Errors handling.
 ******************************************************/

/** Tests whether the returned status is errorneous.
 *  Example :
 *  if ( FSAL_IS_ERROR( status = FSAL_call(...) )){
 *     printf("ERROR status = %d, %d\n", status.major,status.minor);
 *  }
 */
#define FSAL_IS_ERROR( _status_ ) \
        ( ! ( ( _status_ ).major == ERR_FSAL_NO_ERROR ) )

/**
 *  Tests whether an error code is retryable.
 */
fsal_boolean_t fsal_is_retryable(fsal_status_t status);

#endif                          /* ! _USE_SWIG */

/******************************************************
 *              FSAL Strings handling.
 ******************************************************/

fsal_status_t FSAL_str2name(const char *string, /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_name_t * name  /* OUT */
    );

fsal_status_t FSAL_name2str(fsal_name_t * p_name,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    );

int FSAL_namecmp(fsal_name_t * p_name1, fsal_name_t * p_name2);

fsal_status_t FSAL_namecpy(fsal_name_t * p_tgt_name, fsal_name_t * p_src_name);

fsal_status_t FSAL_str2path(char *string,       /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_path_t * p_path        /* OUT */
    );

fsal_status_t FSAL_path2str(fsal_path_t * p_path,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    );

int FSAL_pathcmp(fsal_path_t * p_path1, fsal_path_t * p_path2);

fsal_status_t FSAL_pathcpy(fsal_path_t * p_tgt_path, fsal_path_t * p_src_path);

#ifndef _USE_SWIG
/** utf8 management functions. */

fsal_status_t FSAL_buffdesc2name(fsal_buffdesc_t * in_buf, fsal_name_t * out_name);

fsal_status_t FSAL_buffdesc2path(fsal_buffdesc_t * in_buf, fsal_path_t * out_path);

fsal_status_t FSAL_path2buffdesc(fsal_path_t * in_path, fsal_buffdesc_t * out_buff);

fsal_status_t FSAL_name2buffdesc(fsal_name_t * in_name, fsal_buffdesc_t * out_buff);

#endif                          /* ! _USE_SWIG */

/* snprintmem and sscanmem are defined into common_utils */

#define snprintHandle(target, tgt_size, p_handle) \
  snprintmem(target,tgt_size,(caddr_t)p_handle,sizeof(fsal_handle_t))

#define snprintCookie(target, tgt_size, p_cookie) \
  snprintmem(target,tgt_size,(caddr_t)p_cookie,sizeof(fsal_cookie_t))

#define snprintAttrs(target, tgt_size, p_attrs) \
  snprintmem(target,tgt_size,(caddr_t)p_attrs,sizeof(fsal_attrib_list_t))

#define sscanHandle(p_handle,str_source) \
  sscanmem( (caddr_t)p_handle,sizeof(fsal_handle_t),str_source )

#define sscanCookie(p_cookie,str_source) \
  sscanmem( (caddr_t)p_cookie,sizeof(fsal_cookie_t),str_source )

#define sscanAttrs(p_attrs,str_source) \
  sscanmem( (caddr_t)p_attrs,sizeof(fsal_attrib_list_t),str_source )

/******************************************************
 *              FSAL handles management.
 ******************************************************/

/** Compare 2 handles.
 *  \return - 0 if handle are the same
 *          - A non null value else.
 */
int FSAL_handlecmp(fsal_handle_t * handle1, fsal_handle_t * handle2,
                   fsal_status_t * status);

#ifndef _USE_SWIG

/**
 * FSAL_Handle_to_HashIndex
 * This function is used for hashing a FSAL handle
 * in order to dispatch entries into the hash table array.
 *
 * \param p_handle	The handle to be hashed
 * \param cookie 	Makes it possible to have different hash value for the
 *			same handle, when cookie changes.
 * \param alphabet_len	Parameter for polynomial hashing algorithm
 * \param index_size	The range of hash value will be [0..index_size-1]
 *
 * \return The hash value
 */

unsigned int FSAL_Handle_to_HashIndex(fsal_handle_t * p_handle,
                                      unsigned int cookie,
                                      unsigned int alphabet_len, unsigned int index_size);

/*
 * FSAL_Handle_to_RBTIndex 
 * This function is used for generating a RBT node ID 
 * in order to identify entries into the RBT.
 *
 * \param p_handle	The handle to be hashed
 * \param cookie 	Makes it possible to have different hash value for the
 *			same handle, when cookie changes.
 *
 * \return The hash value
 */

unsigned int FSAL_Handle_to_RBTIndex(fsal_handle_t * p_handle, unsigned int cookie);

/*
 * FSAL_Handle_to_Hash_Both 
 * This function is used for generating both a RBT node ID and a hash index in one pass
 * in order to identify entries into the RBT.
 *
 * \param p_handle	The handle to be hashed
 * \param cookie 	Makes it possible to have different hash value for the
 *			same handle, when cookie changes.
 * \param alphabet_len	Parameter for polynomial hashing algorithm
 * \param index_size	The range of hash value will be [0..index_size-1]
 * \param phashval      First computed value : hash value
 * \param prbtval       Second computed value : rbt value
 *
 *
 * \return 1 if successful and 0 otherwise
 */

unsigned int FSAL_Handle_to_Hash_both(fsal_handle_t * p_handle, unsigned int cookie, unsigned int alphabet_len, 
                                      unsigned int index_size, unsigned int * phashval, unsigned int *prbtval ) ;

/** FSAL_DigestHandle :
 *  convert an fsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 */
fsal_status_t FSAL_DigestHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t output_type,  /* IN */
                                fsal_handle_t * in_fsal_handle, /* IN */
                                caddr_t out_buff        /* OUT */
    );

/** FSAL_ExpandHandle :
 *  convert a buffer extracted from NFS handles
 *  to an FSAL handle.
 */
fsal_status_t FSAL_ExpandHandle(fsal_export_context_t * p_expcontext,   /* IN */
                                fsal_digesttype_t in_type,      /* IN */
                                caddr_t in_buff,        /* IN */
                                fsal_handle_t * out_fsal_handle /* OUT */
    );

/**
 *  FSAL_CleanObjectResources:
 *  Garbages the resources relative to an fsal_handle_t.
 */
fsal_status_t FSAL_CleanObjectResources(fsal_handle_t * in_fsal_handle);        /* IN */

/******************************************************
 *              FSAL directory cookies
 ******************************************************/

/**
 * FSAL_cookie_to_uint64
 *
 * This function converts an fsal_cookie_t to a uint64_t value that
 * can be supplied to NFS readdir routines.
 *
 * \param [in]  handle	 The handle of the directory to which the
 *                       cookie pertains
 * \param [in]  context  The FSAL operation context
 * \param [in]  cookie	 The directory entry cookie
 * \param [out] data     The uint64_t value corresponding to the
 *                       cookie
 */

fsal_status_t FSAL_cookie_to_uint64(fsal_handle_t * handle,
                                    fsal_op_context_t * context,
                                    fsal_cookie_t * cookie,
                                    uint64_t *data);
/**
 * FSAL_uint64_to_cookie
 *
 * This function converts a uint64_t value supplied by the NFS caller
 * to the FSAL's cookie type.
 *
 * \param [in]  handle	 The handle of the directory to which the
 *                       cookie pertains
 * \param [in]  context  The FSAL operation context
 * \param [in]  uint64   The uint64_t value corresponding to the
 *                       cookie
 * \param [out] cookie	 The directory entry cookie
 */

fsal_status_t FSAL_uint64_to_cookie(fsal_handle_t * handle,
                                    fsal_op_context_t * context,
                                    uint64_t * uint64,
                                    fsal_cookie_t * cookie);

/**
 * FSAL_get_cookieverf
 *
 * This function retrieves a cookie verifier from the FSAL for a given
 * directory.
 *
 * \param [in]  handle	 The handle of the directory to which the
 *                       verifier pertains
 * \param [in]  context  The FSAL operation context
 * \param [out] verf     The directory entry cookie
 */

fsal_status_t FSAL_get_cookieverf(fsal_handle_t * handle,
                                  fsal_op_context_t * context,
                                  uint64_t * verf);

/******************************************************
 *              FSAL context management.
 ******************************************************/

fsal_status_t FSAL_BuildExportContext(fsal_export_context_t * p_export_context, /* OUT */
                                      fsal_path_t * p_export_path,      /* IN */
                                      char *fs_specific_options /* IN */
    );


fsal_status_t FSAL_CleanUpExportContext(fsal_export_context_t * p_export_context);

fsal_status_t FSAL_InitClientContext(fsal_op_context_t * p_thr_context  /* OUT  */
    );

fsal_status_t FSAL_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups  /* IN */
    );

#endif                          /* ! _USE_SWIG */

/******************************************************
 *              Common Filesystem calls.
 ******************************************************/

fsal_status_t FSAL_lookup(fsal_handle_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_handle_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    );

fsal_status_t FSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    );

fsal_status_t FSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    );

fsal_status_t FSAL_access(fsal_handle_t * object_handle,        /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessflags_t access_type,       /* IN */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    );

/**
 * FSAL_test_access :
 * test if a client identified by cred can access the object
 * whom the attributes are object_attributes.
 * The following fields of the object_attributes structure MUST be filled :
 * acls (if supported), mode, owner, group.
 * This doesn't make any call to the filesystem,
 * as a result, this doesn't ensure that the file exists, nor that
 * the permissions given as parameters are the actual file permissions :
 * this must be ensured by the cache_inode layer, using FSAL_getattrs,
 * for example.
 */
fsal_status_t FSAL_test_access(fsal_op_context_t * p_context,   /* IN */
                               fsal_accessflags_t access_type,  /* IN */
                               fsal_attrib_list_t * object_attributes   /* IN */
    );

fsal_status_t FSAL_create(fsal_handle_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    );

fsal_status_t FSAL_mkdir(fsal_handle_t * parent_directory_handle,       /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * object_handle, /* OUT */
                         fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */
    );

fsal_status_t FSAL_truncate(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_size_t length, /* IN */
                            fsal_file_t * file_descriptor,      /* INOUT */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    );

fsal_status_t FSAL_getattrs(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * object_attributes      /* IN/OUT */
    );

fsal_status_t FSAL_getattrs_descriptor(fsal_file_t * p_file_descriptor,         /* IN */
                                       fsal_handle_t * p_filehandle,            /* IN */
                                       fsal_op_context_t * p_context,           /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN/OUT */
    );

fsal_status_t FSAL_setattrs(fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_attrib_list_t * attrib_set,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    );

fsal_status_t FSAL_getextattrs(fsal_handle_t * filehandle, /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_extattrib_list_t * object_attributes      /* IN/OUT */
    );

fsal_status_t FSAL_link(fsal_handle_t * target_handle,  /* IN */
                        fsal_handle_t * dir_handle,     /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * attributes /* [ IN/OUT ] */
    );

fsal_status_t FSAL_opendir(fsal_handle_t * dir_handle,  /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * dir_descriptor, /* OUT */
                           fsal_attrib_list_t * dir_attributes  /* [ IN/OUT ] */
    );

fsal_status_t FSAL_readdir(fsal_dir_t * dir_descriptor, /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * pdirent,     /* OUT */
                           fsal_cookie_t * end_position,        /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir  /* OUT */
    );

fsal_status_t FSAL_closedir(fsal_dir_t * dir_descriptor /* IN */
    );

fsal_status_t FSAL_open(fsal_handle_t * filehandle,     /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_openflags_t openflags,     /* IN */
                        fsal_file_t * file_descriptor,  /* OUT */
                        fsal_attrib_list_t * file_attributes    /* [ IN/OUT ] */
    );

fsal_status_t FSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ );

fsal_status_t FSAL_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ );

fsal_status_t FSAL_read(fsal_file_t * file_descriptor,  /*  IN  */
                        fsal_seek_t * seek_descriptor,  /* [IN] */
                        fsal_size_t buffer_size,        /*  IN  */
                        caddr_t buffer, /* OUT  */
                        fsal_size_t * read_amount,      /* OUT  */
                        fsal_boolean_t * end_of_file    /* OUT  */
    );

fsal_status_t FSAL_write(fsal_file_t * file_descriptor, /* IN */
                         fsal_seek_t * seek_descriptor, /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * write_amount     /* OUT */
    );

fsal_status_t FSAL_sync(fsal_file_t * file_descriptor /* IN */);

fsal_status_t FSAL_close(fsal_file_t * file_descriptor  /* IN */
    );

fsal_status_t FSAL_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid);

fsal_status_t FSAL_readlink(fsal_handle_t * linkhandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_path_t * p_link_content,       /* OUT */
                            fsal_attrib_list_t * link_attributes        /* [ IN/OUT ] */
    );

fsal_status_t FSAL_symlink(fsal_handle_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored); */
                           fsal_handle_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes /* [ IN/OUT ] */
    );

fsal_status_t FSAL_rename(fsal_handle_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes       /* [ IN/OUT ] */
    );

fsal_status_t FSAL_unlink(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * parentdir_attributes     /* [IN/OUT ] */
    );

fsal_status_t FSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT */
                          fsal_attrib_list_t * node_attributes  /* [ IN/OUT ] */
    );

fsal_status_t FSAL_dynamic_fsinfo(fsal_handle_t * filehandle,   /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_dynamicfsinfo_t * dynamicinfo    /* OUT */
    );

fsal_status_t FSAL_rcp(fsal_handle_t * filehandle,      /* IN */
                       fsal_op_context_t * p_context,   /* IN */
                       fsal_path_t * p_local_path,      /* IN */
                       fsal_rcpflag_t transfer_opt      /* IN */
    );

fsal_status_t FSAL_rcp_by_name(fsal_handle_t * filehandle,      /* IN */
                               fsal_name_t * pfilename, /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_path_t * p_local_path,      /* IN */
                               fsal_rcpflag_t transfer_opt      /* IN */
    );

fsal_status_t FSAL_rcp_by_fileid(fsal_handle_t * filehandle,    /* IN */
                                 fsal_u64_t fileid,     /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_path_t * p_local_path,    /* IN */
                                 fsal_rcpflag_t transfer_opt    /* IN */
    );

fsal_status_t FSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                            fsal_handle_t     * p_filehandle,        /* IN */
                            fsal_op_context_t * p_context,           /* IN */
                            void              * p_owner,             /* IN (opaque to FSAL) */
                            fsal_lock_op_t      lock_op,             /* IN */
                            fsal_lock_param_t   request_lock,        /* IN */
                            fsal_lock_param_t * conflicting_lock     /* OUT */
                            );

/* FSAL_UP functions */
/* These structs are defined here because including fsal_up.h causes
 * preprocessor issues. */
#ifdef _USE_FSAL_UP
struct fsal_up_event_bus_filter_t_;
struct fsal_up_event_t_;
struct fsal_up_event_bus_parameter_t_;
struct fsal_up_event_bus_context_t_;
fsal_status_t FSAL_UP_Init(struct fsal_up_event_bus_parameter_t_ * pebparam,      /* IN */
                           struct fsal_up_event_bus_context_t_ * pupebcontext     /* OUT */
                           );

fsal_status_t FSAL_UP_AddFilter(struct fsal_up_event_bus_filter_t_ * pupebfilter,  /* IN */
                                struct fsal_up_event_bus_context_t_ * pupebcontext /* INOUT */
                                   );
fsal_status_t FSAL_UP_GetEvents(struct fsal_up_event_t_ ** pevents,                /* OUT */
                                fsal_count_t * event_nb,                   /* IN */
                                fsal_time_t timeout,                       /* IN */
                                fsal_count_t * peventfound,                /* OUT */
                                struct fsal_up_event_bus_context_t_ * pupebcontext /* IN */
                                );
#endif /* _USE_FSAL_UP */

/* To be called before exiting */
fsal_status_t FSAL_terminate();

#ifndef _USE_SWIG

/******************************************************
 *          FSAL extended attributes management.
 ******************************************************/

/** cookie for reading attrs from the first one */
#define XATTRS_READLIST_FROM_BEGINNING  (0)

/** An extented attribute entry */
typedef struct fsal_xattrent__
{
  unsigned int xattr_id;                 /**< xattr index */
  fsal_name_t xattr_name;                /**< attribute name  */
  unsigned int xattr_cookie;             /**< cookie for getting xattrs list from the next entry */
  fsal_attrib_list_t attributes;         /**< entry attributes (if supported) */

} fsal_xattrent_t;

/**
 * Retrieves the list of extended attributes for an object in the filesystem.
 * 
 * \param p_objecthandle Handle of the object we want to get extended attributes.
 * \param cookie index of the next entry to be returned.
 * \param p_context pointer to the current security context.
 * \param xattrs_tab a table for storing extended attributes list to.
 * \param xattrs_tabsize the maximum number of xattr entries that xattrs_tab
 *            can contain.
 * \param p_nb_returned the number of xattr entries actually stored in xattrs_tab.
 * \param end_of_list this boolean indicates that the end of xattrs list has been reached.
 */
fsal_status_t FSAL_ListXAttrs(fsal_handle_t * p_objecthandle,   /* IN */
                              unsigned int cookie,      /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_xattrent_t * xattrs_tab,     /* IN/OUT */
                              unsigned int xattrs_tabsize,      /* IN */
                              unsigned int *p_nb_returned,      /* OUT */
                              int *end_of_list  /* OUT */
    );

/**
 * Get the index of an xattr based on its name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param pxattr_id found xattr_id
 *   
 * \return ERR_FSAL_NO_ERROR if xattr_name exists, ERR_FSAL_NOENT otherwise
 */
fsal_status_t FSAL_GetXAttrIdByName(fsal_handle_t * p_objecthandle,     /* IN */
                                    const fsal_name_t * xattr_name,     /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    unsigned int *pxattr_id     /* OUT */
    );

/**
 * Get the value of an extended attribute from its name.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t FSAL_GetXAttrValueByName(fsal_handle_t * p_objecthandle,  /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       caddr_t buffer_addr,     /* IN/OUT */
                                       size_t buffer_size,      /* IN */
                                       size_t * p_output_size   /* OUT */
    );

/**
 * Get the value of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t FSAL_GetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN/OUT */
                                     size_t buffer_size,        /* IN */
                                     size_t * p_output_size     /* OUT */
    );

/**
 * Set the value of an extended attribute.
 *
 * \param p_objecthandle Handle of the object you want to set attribute for.
 * \param xattr_name the name of the attribute to be written.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is stored.
 * \param buffer_size size of the buffer where the xattr value is stored.
 * \param create this boolean specifies if the attribute is created
 *               if it does not exist.
 */
fsal_status_t FSAL_SetXAttrValue(fsal_handle_t * p_objecthandle,        /* IN */
                                 const fsal_name_t * xattr_name,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 caddr_t buffer_addr,   /* IN */
                                 size_t buffer_size,    /* IN */
                                 int create     /* IN */
    );

/**
 * Set the value of an extended attribute by its Id.
 *
 * \param p_objecthandle Handle of the object you want to set attribute for.
 * \param xattr_id index of the attribute to be written.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is stored.
 * \param buffer_size size of the buffer where the xattr value is stored.
 */
fsal_status_t FSAL_SetXAttrValueById(fsal_handle_t * p_objecthandle,    /* IN */
                                     unsigned int xattr_id,     /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     caddr_t buffer_addr,       /* IN */
                                     size_t buffer_size);       /* IN */

/**
 * Get the attributes of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_cookie xattr's cookie (as returned by listxattrs).
 * \param p_attrs xattr's attributes.
 */
fsal_status_t FSAL_GetXAttrAttrs(fsal_handle_t * p_objecthandle,        /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 unsigned int xattr_id, /* IN */
                                 fsal_attrib_list_t * p_attrs
                                          /**< IN/OUT xattr attributes (if supported) */
    );

/**
 *  Removes a xattr by Id
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_id xattr's id
 */
fsal_status_t FSAL_RemoveXAttrById(fsal_handle_t * p_objecthandle,      /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   unsigned int xattr_id);      /* IN */

/**
 *  Removes a xattr by Name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_name xattr's name
 */
fsal_status_t FSAL_RemoveXAttrByName(fsal_handle_t * p_objecthandle,    /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     const fsal_name_t * xattr_name);   /* IN */

/******************************************************
 *                FSAL miscelaneous tools.
 ******************************************************/

/* Note : one per client thread */
void FSAL_get_stats(fsal_statistics_t * stats,  /* OUT */
                    fsal_boolean_t reset        /* IN */
    );

/* Return the name of the underlying file system (used for traces) */
char *FSAL_GetFSName();

/******************************************************
 *                FSAL quota related functions.
 ******************************************************/
#define FSAL_QCMD(cmd, type)  (((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

fsal_status_t FSAL_get_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type,    /* IN */
                             fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquota);    /* OUT */

fsal_status_t FSAL_set_quota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type,    /* IN */
                             fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquot,      /* IN */
                             fsal_quota_t * presquot);  /* OUT */

/******************************************************
 *                Standard convertion routines.
 ******************************************************/

unsigned int FSAL_GetFileno(fsal_file_t * pfile);
#define FSAL_FILENO( pfile ) FSAL_GetFileno( pfile )

/**
 * fsal2unix_mode:
 * Convert FSAL mode to posix mode.
 *
 * \param fsal_mode (input):
 *        The FSAL mode to be translated.
 *
 * \return The posix mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(fsal_accessmode_t fsal_mode);

fsal_dev_t posix2fsal_devt(dev_t posix_devid);

/**
 * unix2fsal_mode:
 * Convert posix mode to FSAL mode.
 *
 * \param unix_mode (input):
 *        The posix mode to be translated.
 *
 * \return The FSAL mode associated to unix_mode.
 */
fsal_accessmode_t unix2fsal_mode(mode_t unix_mode);

/* The following functions are used in Cache_Inode_Asynch mode */

fsal_status_t FSAL_setattr_access(fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t * candidate_attributes,    /* IN */
                                  fsal_attrib_list_t * object_attributes        /* IN */
    );

fsal_status_t FSAL_merge_attrs(fsal_attrib_list_t * pinit_attr, /* IN */
                               fsal_attrib_list_t * pnew_attr,  /* IN */
                               fsal_attrib_list_t * presult_attr);      /* OUT */

fsal_status_t FSAL_rename_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattrsrc, /* IN */
                                 fsal_attrib_list_t * pattrdest);       /* IN */

fsal_status_t FSAL_unlink_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr);   /* IN */

fsal_status_t FSAL_create_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr);   /* IN */

fsal_status_t FSAL_link_access(fsal_op_context_t * pcontext,    /* IN */
                               fsal_attrib_list_t * pattr);     /* IN */

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

typedef struct fsal_functions__
{
  /* FSAL_access */
  fsal_status_t(*fsal_access) (fsal_handle_t * p_object_handle,
                               fsal_op_context_t * p_context,
                               fsal_accessflags_t access_type,
                               fsal_attrib_list_t * p_object_attribute);

  /* FSAL_getattrs */
  fsal_status_t(*fsal_getattrs) (fsal_handle_t * p_filehandle,  /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

  /* FSAL_getattrs_descriptor */
  fsal_status_t(*fsal_getattrs_descriptor) (fsal_file_t * p_file_descriptor,         /* IN */
                                            fsal_handle_t * p_filehandle,            /* IN */
                                            fsal_op_context_t * p_context,           /* IN */
                                            fsal_attrib_list_t * p_object_attributes /* IN/OUT */ );

  /* FSAL_setattrs */
  fsal_status_t(*fsal_setattrs) (fsal_handle_t * p_filehandle,  /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_attrib_list_t * p_attrib_set,     /* IN */
                                 fsal_attrib_list_t *
                                 p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL__BuildExportContext */
  fsal_status_t(*fsal_buildexportcontext) (fsal_export_context_t * p_export_context,    /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options /* IN */ );

  /* FSAL_CleanUpExportContent */
  fsal_status_t(*fsal_cleanupexportcontext) (fsal_export_context_t * p_export_context);


  /* FSAL_InitClientContext */
  fsal_status_t(*fsal_initclientcontext) (fsal_op_context_t * p_thr_context);

  /* FSAL_GetClientContext */
  fsal_status_t(*fsal_getclientcontext) (fsal_op_context_t * p_thr_context,     /* IN/OUT  */
                                         fsal_export_context_t * p_export_context,      /* IN */
                                         fsal_uid_t uid,        /* IN */
                                         fsal_gid_t gid,        /* IN */
                                         fsal_gid_t * alt_groups,       /* IN */
                                         fsal_count_t nb_alt_groups /* IN */ );

  /* FSAL_create */
  fsal_status_t(*fsal_create) (fsal_handle_t * p_parent_directory_handle,       /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * p_object_handle, /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL_mkdir */
  fsal_status_t(*fsal_mkdir) (fsal_handle_t * p_parent_directory_handle,        /* IN */
                              fsal_name_t * p_dirname,  /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_handle_t * p_object_handle,  /* OUT */
                              fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL_link */
  fsal_status_t(*fsal_link) (fsal_handle_t * p_target_handle,   /* IN */
                             fsal_handle_t * p_dir_handle,      /* IN */
                             fsal_name_t * p_link_name, /* IN */
                             fsal_op_context_t * p_context,     /* IN */
                             fsal_attrib_list_t * p_attributes /* [ IN/OUT ] */ );

  /* FSAL_mknode */
  fsal_status_t(*fsal_mknode) (fsal_handle_t * parentdir_handle,        /* IN */
                               fsal_name_t * p_node_name,       /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_nodetype_t nodetype,        /* IN */
                               fsal_dev_t * dev,        /* IN */
                               fsal_handle_t * p_object_handle, /* OUT (handle to the created node) */
                               fsal_attrib_list_t * node_attributes /* [ IN/OUT ] */ );

  /* FSAL_opendir */
  fsal_status_t(*fsal_opendir) (fsal_handle_t * p_dir_handle,   /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_dir_t * p_dir_descriptor,  /* OUT */
                                fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */ );

  /* FSAL_readdir */
  fsal_status_t(*fsal_readdir) (fsal_dir_t * p_dir_descriptor,  /* IN */
                                fsal_cookie_t start_position,   /* IN */
                                fsal_attrib_mask_t get_attr_mask,       /* IN */
                                fsal_mdsize_t buffersize,       /* IN */
                                fsal_dirent_t * p_pdirent,      /* OUT */
                                fsal_cookie_t * p_end_position, /* OUT */
                                fsal_count_t * p_nb_entries,    /* OUT */
                                fsal_boolean_t * p_end_of_dir /* OUT */ );

  /* FSAL_closedir */
  fsal_status_t(*fsal_closedir) (fsal_dir_t * p_dir_descriptor /* IN */ );

  /* FSAL_open_by_name */
  fsal_status_t(*fsal_open_by_name) (fsal_handle_t * dirhandle, /* IN */
                                     fsal_name_t * filename,    /* IN */
                                     fsal_op_context_t * p_context,     /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     fsal_file_t * file_descriptor,     /* OUT */
                                     fsal_attrib_list_t *
                                     file_attributes /* [ IN/OUT ] */ );

  /* FSAL_open */
  fsal_status_t(*fsal_open) (fsal_handle_t * p_filehandle,      /* IN */
                             fsal_op_context_t * p_context,     /* IN */
                             fsal_openflags_t openflags,        /* IN */
                             fsal_file_t * p_file_descriptor,   /* OUT */
                             fsal_attrib_list_t * p_file_attributes /* [ IN/OUT ] */ );

  /* FSAL_read */
  fsal_status_t(*fsal_read) (fsal_file_t * p_file_descriptor,   /* IN */
                             fsal_seek_t * p_seek_descriptor,   /* [IN] */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* OUT */
                             fsal_size_t * p_read_amount,       /* OUT */
                             fsal_boolean_t * p_end_of_file /* OUT */ );

  /* FSAL_write */
  fsal_status_t(*fsal_write) (fsal_file_t * p_file_descriptor,  /* IN */
                              fsal_seek_t * p_seek_descriptor,  /* IN */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* IN */
                              fsal_size_t * p_write_amount /* OUT */ );

  /* FSAL_close */
  fsal_status_t(*fsal_close) (fsal_file_t * p_file_descriptor /* IN */ );

  /* FSAL_open_by_fileid */
  fsal_status_t(*fsal_open_by_fileid) (fsal_handle_t * filehandle,      /* IN */
                                       fsal_u64_t fileid,       /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       fsal_openflags_t openflags,      /* IN */
                                       fsal_file_t * file_descriptor,   /* OUT */
                                       fsal_attrib_list_t *
                                       file_attributes /* [ IN/OUT ] */ );

  /* FSAL_close_by_fileid */
  fsal_status_t(*fsal_close_by_fileid) (fsal_file_t * file_descriptor /* IN */ ,
                                        fsal_u64_t fileid);

  /* FSAL_dynamic_fsinfo */
  fsal_status_t(*fsal_dynamic_fsinfo) (fsal_handle_t * p_filehandle,    /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       fsal_dynamicfsinfo_t * p_dynamicinfo /* OUT */ );

  /* FSAL_Init */
  fsal_status_t(*fsal_init) (fsal_parameter_t * init_info /* IN */ );

  /* FSAL_terminate */
  fsal_status_t(*fsal_terminate) ();

  /* FSAL_test_access */
  fsal_status_t(*fsal_test_access) (fsal_op_context_t * p_context,      /* IN */
                                    fsal_accessflags_t access_type,     /* IN */
                                    fsal_attrib_list_t * p_object_attributes /* IN */ );

  /* FSAL_setattr_access */
  fsal_status_t(*fsal_setattr_access) (fsal_op_context_t * p_context,   /* IN */
                                       fsal_attrib_list_t * candidate_attributes,       /* IN */
                                       fsal_attrib_list_t * object_attributes /* IN */ );

  /* FSAL_rename_access */
  fsal_status_t(*fsal_rename_access) (fsal_op_context_t * pcontext,     /* IN */
                                      fsal_attrib_list_t * pattrsrc,    /* IN */
                                      fsal_attrib_list_t * pattrdest) /* IN */ ;

  /* FSAL_create_access */
  fsal_status_t(*fsal_create_access) (fsal_op_context_t * pcontext,     /* IN */
                                      fsal_attrib_list_t * pattr) /* IN */ ;

  /* FSAL_unlink_access */
  fsal_status_t(*fsal_unlink_access) (fsal_op_context_t * pcontext,     /* IN */
                                      fsal_attrib_list_t * pattr) /* IN */ ;

  /* FSAL_link_access */
  fsal_status_t(*fsal_link_access) (fsal_op_context_t * pcontext,       /* IN */
                                    fsal_attrib_list_t * pattr) /* IN */ ;

  /* FSAL_merge_attrs */
  fsal_status_t(*fsal_merge_attrs) (fsal_attrib_list_t * pinit_attr,
                                    fsal_attrib_list_t * pnew_attr,
                                    fsal_attrib_list_t * presult_attr);

  /* FSAL_lookup */
  fsal_status_t(*fsal_lookup) (fsal_handle_t * p_parent_directory_handle,       /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_handle_t * p_object_handle, /* OUT */
                               fsal_attrib_list_t *
                               p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL_lookupPath */
  fsal_status_t(*fsal_lookuppath) (fsal_path_t * p_path,        /* IN */
                                   fsal_op_context_t * p_context,       /* IN */
                                   fsal_handle_t * object_handle,       /* OUT */
                                   fsal_attrib_list_t *
                                   p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL_lookupJunction */
  fsal_status_t(*fsal_lookupjunction) (fsal_handle_t * p_junction_handle,       /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       fsal_handle_t * p_fsoot_handle,  /* OUT */
                                       fsal_attrib_list_t *
                                       p_fsroot_attributes /* [ IN/OUT ] */ );
  /* FSAL_CleanObjectResources */
  fsal_status_t(*fsal_cleanobjectresources) (fsal_handle_t * in_fsal_handle);

  /* FSAL_cookie_to_uint64 */
  fsal_status_t(*fsal_cookie_to_uint64) (fsal_handle_t * handle,
                                         fsal_cookie_t * cookie,
                                         uint64_t * uint64);

  /* FSAL_uint64_to_cookie */
  fsal_status_t(*fsal_uint64_to_cookie) (fsal_handle_t * handle,
                                         uint64_t * uint64,
                                         fsal_cookie_t * cookie);

  /* FSAL_get_cookieverf */
  fsal_status_t(*fsal_get_cookieverf)(fsal_handle_t * handle,
                                      uint64_t * cookie);
  /* FSAL_set_quota */
  fsal_status_t(*fsal_set_quota) (fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota,        /* IN */
                                  fsal_quota_t * presquota);    /* OUT */

  /* FSAL_get_quota */
  fsal_status_t(*fsal_get_quota) (fsal_path_t * pfsal_path,     /* IN */
                                  int quota_type,       /* IN */
                                  fsal_uid_t fsal_uid,  /* IN */
                                  fsal_quota_t * pquota);       /* OUT */

  /* FSAL_rcp */
  fsal_status_t(*fsal_rcp) (fsal_handle_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_path_t * p_local_path, /* IN */
                            fsal_rcpflag_t transfer_opt);       /* IN */

  /* FSAL_rcp_by_fileid */
  fsal_status_t(*fsal_rcp_by_fileid) (fsal_handle_t * filehandle,       /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      fsal_op_context_t * p_context,    /* IN */
                                      fsal_path_t * p_local_path,       /* IN */
                                      fsal_rcpflag_t transfer_opt /* IN */ );

  /* FSAL_rename */
  fsal_status_t(*fsal_rename) (fsal_handle_t * p_old_parentdir_handle,  /* IN */
                               fsal_name_t * p_old_name,        /* IN */
                               fsal_handle_t * p_new_parentdir_handle,  /* IN */
                               fsal_name_t * p_new_name,        /* IN */
                               fsal_op_context_t * p_context,   /* IN */
                               fsal_attrib_list_t * p_src_dir_attributes,       /* [ IN/OUT ] */
                               fsal_attrib_list_t *
                               p_tgt_dir_attributes /* [ IN/OUT ] */ );

  /* FSAL_get_stats */
  void (*fsal_get_stats) (fsal_statistics_t * stats,    /* OUT */
                          fsal_boolean_t reset);        /* IN */

  /* FSAL_readlink */
   fsal_status_t(*fsal_readlink) (fsal_handle_t * p_linkhandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_path_t * p_link_content, /* OUT */
                                  fsal_attrib_list_t *
                                  p_link_attributes /* [ IN/OUT ] */ );

  /* FSAL_symlink */
   fsal_status_t(*fsal_symlink) (fsal_handle_t * p_parent_directory_handle,     /* IN */
                                 fsal_name_t * p_linkname,      /* IN */
                                 fsal_path_t * p_linkcontent,   /* IN */
                                 fsal_op_context_t * p_context, /* IN */
                                 fsal_accessmode_t accessmode,  /* IN (ignored) */
                                 fsal_handle_t * p_link_handle, /* OUT */
                                 fsal_attrib_list_t *
                                 p_link_attributes /* [ IN/OUT ] */ );

  /* FSAL_handlecmp */
  int (*fsal_handlecmp) (fsal_handle_t * handle1, fsal_handle_t * handle2,
                         fsal_status_t * status);

  /* FSAL_Handle_to_HashIndex */
  unsigned int (*fsal_handle_to_hashindex) (fsal_handle_t * p_handle,
                                            unsigned int cookie,
                                            unsigned int alphabet_len,
                                            unsigned int index_size);

  /* FSAL_Handle_to_RBTIndex */
  unsigned int (*fsal_handle_to_rbtindex) (fsal_handle_t * p_handle, unsigned int cookie);

  /* FSAL_Handle_to_Hash_both */
  unsigned int (*fsal_handle_to_hash_both) (fsal_handle_t * p_handle, unsigned int cookie, unsigned int alphabet_len, 
                                      unsigned int index_size, unsigned int * phashval, unsigned int *prbtval ) ;

  /* FSAL_DigestHandle */
   fsal_status_t(*fsal_digesthandle) (fsal_export_context_t * p_expcontext,     /* IN */
                                      fsal_digesttype_t output_type,    /* IN */
                                      fsal_handle_t * p_in_fsal_handle, /* IN */
                                      caddr_t out_buff /* OUT */ );

  /* FSAL_ExpandHandle */
   fsal_status_t(*fsal_expandhandle) (fsal_export_context_t * p_expcontext,     /* IN */
                                      fsal_digesttype_t in_type,        /* IN */
                                      caddr_t in_buff,  /* IN */
                                      fsal_handle_t * p_out_fsal_handle /* OUT */ );

  /* FSAL_SetDefault_FSAL_parameter */
   fsal_status_t(*fsal_setdefault_fsal_parameter) (fsal_parameter_t * out_parameter);

  /* FSAL_SetDefault_FS_common_parameter */
   fsal_status_t(*fsal_setdefault_fs_common_parameter) (fsal_parameter_t * out_parameter);

  /* FSAL_SetDefault_FS_specific_parameter */
   fsal_status_t(*fsal_setdefault_fs_specific_parameter) (fsal_parameter_t *
                                                          out_parameter);

  /* FSAL_load_FSAL_parameter_from_conf */
   fsal_status_t(*fsal_load_fsal_parameter_from_conf) (config_file_t in_config,
                                                       fsal_parameter_t * out_parameter);

  /* FSAL_load_FS_common_parameter_from_conf */
   fsal_status_t(*fsal_load_fs_common_parameter_from_conf) (config_file_t in_config,
                                                            fsal_parameter_t *
                                                            out_parameter);

  /* FSAL_load_FS_specific_parameter_from_conf */
   fsal_status_t(*fsal_load_fs_specific_parameter_from_conf) (config_file_t in_config,
                                                              fsal_parameter_t *
                                                              out_parameter);

  /* FSAL_truncate */
   fsal_status_t(*fsal_truncate) (fsal_handle_t * p_filehandle, /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_size_t length,   /* IN */
                                  fsal_file_t * file_descriptor,        /* Unused in this FSAL */
                                  fsal_attrib_list_t *
                                  p_object_attributes /* [ IN/OUT ] */ );

  /* FSAL_unlink */
   fsal_status_t(*fsal_unlink) (fsal_handle_t * p_parent_directory_handle,      /* IN */
                                fsal_name_t * p_object_name,    /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_attrib_list_t *
                                p_parent_directory_attributes /* [IN/OUT ] */ );

  /* FSAL_GetFSName */
  char *(*fsal_getfsname) ();

  /* FSAL_GetXAttrAttrs */
   fsal_status_t(*fsal_getxattrattrs) (fsal_handle_t * p_objecthandle,  /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       unsigned int xattr_id,   /* IN */
                                       fsal_attrib_list_t * p_attrs);

  /* FSAL_ListXAttrs */
   fsal_status_t(*fsal_listxattrs) (fsal_handle_t * p_objecthandle,     /* IN */
                                    unsigned int cookie,        /* IN */
                                    fsal_op_context_t * p_context,      /* IN */
                                    fsal_xattrent_t * xattrs_tab,       /* IN/OUT */
                                    unsigned int xattrs_tabsize,        /* IN */
                                    unsigned int *p_nb_returned,        /* OUT */
                                    int *end_of_list /* OUT */ );

  /* FSAL_GetXAttrValueById */
   fsal_status_t(*fsal_getxattrvaluebyid) (fsal_handle_t * p_objecthandle,      /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           caddr_t buffer_addr, /* IN/OUT */
                                           size_t buffer_size,  /* IN */
                                           size_t * p_output_size /* OUT */ );

  /* FSAL_GetXAttrIdByName */
   fsal_status_t(*fsal_getxattridbyname) (fsal_handle_t * p_objecthandle,       /* IN */
                                          const fsal_name_t * xattr_name,       /* IN */
                                          fsal_op_context_t * p_context,        /* IN */
                                          unsigned int *pxattr_id /* OUT */ );

  /* FSAL_GetXAttrValueByName */
   fsal_status_t(*fsal_getxattrvaluebyname) (fsal_handle_t * p_objecthandle,    /* IN */
                                             const fsal_name_t * xattr_name,    /* IN */
                                             fsal_op_context_t * p_context,     /* IN */
                                             caddr_t buffer_addr,       /* IN/OUT */
                                             size_t buffer_size,        /* IN */
                                             size_t * p_output_size /* OUT */ );

  /* FSAL_SetXAttrValue */
   fsal_status_t(*fsal_setxattrvalue) (fsal_handle_t * p_objecthandle,  /* IN */
                                       const fsal_name_t * xattr_name,  /* IN */
                                       fsal_op_context_t * p_context,   /* IN */
                                       caddr_t buffer_addr,     /* IN */
                                       size_t buffer_size,      /* IN */
                                       int create /* IN */ );

  /* FSAL_SetXAttrValueById */
   fsal_status_t(*fsal_setxattrvaluebyid) (fsal_handle_t * p_objecthandle,      /* IN */
                                           unsigned int xattr_id,       /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           caddr_t buffer_addr, /* IN */
                                           size_t buffer_size /* IN */ );

  /* FSAL_RemoveXAttrById */
   fsal_status_t(*fsal_removexattrbyid) (fsal_handle_t * p_objecthandle,        /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         unsigned int xattr_id) /* IN */ ;

  /* FSAL_RemoveXAttrByName */
   fsal_status_t(*fsal_removexattrbyname) (fsal_handle_t * p_objecthandle,      /* IN */
                                           fsal_op_context_t * p_context,       /* IN */
                                           const fsal_name_t * xattr_name) /* IN */ ;

  /* FSAL_getextattrs */
  fsal_status_t (*fsal_getextattrs)( fsal_handle_t * p_filehandle, /* IN */
                                     fsal_op_context_t * p_context,        /* IN */
                                     fsal_extattrib_list_t * p_object_attributes /* OUT */ ) ;

  fsal_status_t (*fsal_lock_op)( fsal_file_t             * p_file_descriptor,   /* IN */
                                 fsal_handle_t           * p_filehandle,        /* IN */
                                 fsal_op_context_t       * p_context,           /* IN */
                                 void                    * p_owner,             /* IN (opaque to FSAL) */
                                 fsal_lock_op_t            lock_op,             /* IN */
                                 fsal_lock_param_t         request_lock,        /* IN */
                                 fsal_lock_param_t       * conflicting_lock     /* OUT */ );

  /* get fileno */
  unsigned int (*fsal_getfileno) (fsal_file_t *);

  fsal_status_t(*fsal_sync) (fsal_file_t * p_file_descriptor  /* IN */);

  /* FSAL_UP functions */
#ifdef _USE_FSAL_UP
  fsal_status_t(*fsal_up_init) (struct fsal_up_event_bus_parameter_t_ * pebparam,      /* IN */
				struct fsal_up_event_bus_context_t_ * pupebcontext     /* OUT */ );
  fsal_status_t(*fsal_up_addfilter)(struct fsal_up_event_bus_filter_t_ * pupebfilter,  /* IN */
                                  struct fsal_up_event_bus_context_t_ * pupebcontext /* INOUT */ );
  fsal_status_t(*fsal_up_getevents)(struct fsal_up_event_t_ ** pevents,                /* OUT */
                                  fsal_count_t * event_nb,                   /* IN */
                                  fsal_time_t timeout,                       /* IN */
				    fsal_count_t * peventfound,                 /* OUT */
                                  struct fsal_up_event_bus_context_t_ * pupebcontext /* IN */ );
#endif /* _USE_FSAL_UP */
} fsal_functions_t;

/* Structure allow assignement, char[<n>] do not */
typedef struct fsal_const__
{
  unsigned int fsal_handle_t_size;
  unsigned int fsal_op_context_t_size;
  unsigned int fsal_export_context_t_size;
  unsigned int fsal_file_t_size;
  unsigned int fsal_cookie_t_size;
  unsigned int fsal_cred_t_size;
  unsigned int fs_specific_initinfo_t_size;
  unsigned int fsal_dir_t_size;
} fsal_const_t;

int FSAL_LoadLibrary(char *path);

fsal_functions_t FSAL_GetFunctions(void);
void FSAL_LoadFunctions(void);

fsal_const_t FSAL_GetConsts(void);
void FSAL_LoadConsts(void);

#endif                          /* ! _USE_SWIG */

#endif                          /* _FSAL_H */
