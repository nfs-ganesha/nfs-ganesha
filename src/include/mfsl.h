/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#include "fsal_types.h"
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
fsal_status_t  MFSL_SetDefault_parameter(
                              mfsl_parameter_t *  out_parameter );     

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
fsal_status_t  MFSL_load_parameter_from_conf(
      config_file_t       in_config,
      mfsl_parameter_t *  out_parameter
    );

/** 
 *  FSAL_Init:
 *  Initializes Filesystem abstraction layer.
 */
fsal_status_t  MFSL_Init(
    mfsl_parameter_t        * init_info         /* IN */
);

fsal_status_t MFSL_GetContext( mfsl_context_t     * pcontext,
                               fsal_op_context_t  * pfsal_context  ) ;


#endif /* ! _USE_SWIG */



/******************************************************
 *              Common Filesystem calls.
 ******************************************************/


fsal_status_t MFSL_lookup (
    mfsl_object_t         * parent_directory_handle,        /* IN */
    fsal_name_t           * p_filename,                     /* IN */
    fsal_op_context_t     * p_context,                      /* IN */
    mfsl_context_t        * p_mfsl_context,                 /* IN */
    mfsl_object_t         * object_handle,                  /* OUT */
    fsal_attrib_list_t    * object_attributes               /* [ IN/OUT ] */
);


fsal_status_t MFSL_lookupPath (
    fsal_path_t           * p_path,            /* IN */
    fsal_op_context_t     * p_context,         /* IN */
    mfsl_context_t        * p_mfsl_context,    /* IN */
    mfsl_object_t         * object_handle,     /* OUT */
    fsal_attrib_list_t    * object_attributes  /* [ IN/OUT ] */
);


fsal_status_t MFSL_lookupJunction (
    mfsl_object_t         * p_junction_handle,   /* IN */
    fsal_op_context_t     * p_context,           /* IN */
    mfsl_context_t        * p_mfsl_context,      /* IN */
    mfsl_object_t         * p_fsoot_handle,      /* OUT */
    fsal_attrib_list_t    * p_fsroot_attributes  /* [ IN/OUT ] */
);


fsal_status_t MFSL_access(
    mfsl_object_t              * object_handle,      /* IN */
    fsal_op_context_t          * p_context,          /* IN */
    mfsl_context_t             * p_mfsl_context,     /* IN */
    fsal_accessflags_t           access_type,        /* IN */
    fsal_attrib_list_t         * object_attributes   /* [ IN/OUT ] */
);

fsal_status_t MFSL_create(
    mfsl_object_t         * parent_directory_handle, /* IN */
    fsal_name_t           * p_filename,              /* IN */
    fsal_op_context_t     * p_context,               /* IN */
    mfsl_context_t        * p_mfsl_context,          /* IN */
    fsal_accessmode_t       accessmode,              /* IN */
    mfsl_object_t         * object_handle,           /* OUT */
    fsal_attrib_list_t    * object_attributes        /* [ IN/OUT ] */
);

fsal_status_t MFSL_mkdir(
    mfsl_object_t         * parent_directory_handle, /* IN */
    fsal_name_t           * p_dirname,               /* IN */
    fsal_op_context_t     * p_context,               /* IN */
    mfsl_context_t        * p_mfsl_context,          /* IN */
    fsal_accessmode_t       accessmode,              /* IN */
    mfsl_object_t         * object_handle,           /* OUT */
    fsal_attrib_list_t    * object_attributes        /* [ IN/OUT ] */
);

fsal_status_t MFSL_truncate(
    mfsl_object_t         * filehandle,              /* IN */
    fsal_op_context_t     * p_context,               /* IN */
    mfsl_context_t        * p_mfsl_context,          /* IN */
    fsal_size_t             length,                  /* IN */
    fsal_file_t           * file_descriptor,         /* INOUT */
    fsal_attrib_list_t    * object_attributes   /* [ IN/OUT ] */
);

fsal_status_t MFSL_getattrs(
    mfsl_object_t         * filehandle,              /* IN */
    fsal_op_context_t     * p_context,               /* IN */
    mfsl_context_t        * p_mfsl_context,          /* IN */
    fsal_attrib_list_t    * object_attributes        /* IN/OUT */
);

fsal_status_t MFSL_setattrs(
    mfsl_object_t         * filehandle,        /* IN */
    fsal_op_context_t     * p_context,         /* IN */
    mfsl_context_t        * p_mfsl_context,    /* IN */
    fsal_attrib_list_t    * attrib_set,        /* IN */
    fsal_attrib_list_t    * object_attributes  /* [ IN/OUT ] */
);

fsal_status_t MFSL_link(
    mfsl_object_t         * target_handle,     /* IN */
    mfsl_object_t         * dir_handle,        /* IN */
    fsal_name_t           * p_link_name,       /* IN */
    fsal_op_context_t     * p_context,         /* IN */
    mfsl_context_t        * p_mfsl_context,    /* IN */
    fsal_attrib_list_t    * tgt_attributes,         /* [ IN/OUT ] */
    fsal_attrib_list_t    * dir_attributes         /* [ IN/OUT ] */
);

fsal_status_t MFSL_opendir(
    mfsl_object_t             * dir_handle,           /* IN */
    fsal_op_context_t         * p_context,            /* IN */
    mfsl_context_t            * p_mfsl_context,       /* IN */
    fsal_dir_t                * dir_descriptor,       /* OUT */
    fsal_attrib_list_t        * dir_attributes        /* [ IN/OUT ] */
);

fsal_status_t MFSL_readdir(
    fsal_dir_t            * dir_descriptor,     /* IN */
    fsal_cookie_t           start_position ,    /* IN */
    fsal_attrib_mask_t      get_attr_mask,      /* IN */
    fsal_mdsize_t           buffersize,         /* IN */
    fsal_dirent_t         * pdirent,            /* OUT */
    fsal_cookie_t         * end_position,       /* OUT */
    fsal_count_t          * nb_entries,         /* OUT */
    fsal_boolean_t        * end_of_dir,         /* OUT */
    mfsl_context_t        * p_mfsl_context      /* IN */
);


fsal_status_t MFSL_closedir(
    fsal_dir_t      * dir_descriptor,    /* IN */
    mfsl_context_t  * p_mfsl_context     /* IN */
);

fsal_status_t MFSL_open(
    mfsl_object_t         * filehandle,             /* IN */
    fsal_op_context_t     * p_context,              /* IN */
    mfsl_context_t        * p_mfsl_context,         /* IN */
    fsal_openflags_t      openflags,                /* IN */
    fsal_file_t           * file_descriptor,        /* OUT */
    fsal_attrib_list_t    * file_attributes         /* [ IN/OUT ] */
);

fsal_status_t MFSL_open_by_name(
    mfsl_object_t         * dirhandle,             /* IN */
    fsal_name_t           * filename,              /* IN */
    fsal_op_context_t     * p_context,             /* IN */
    mfsl_context_t        * p_mfsl_context,        /* IN */
    fsal_openflags_t        openflags,             /* IN */
    fsal_file_t           * file_descriptor,       /* OUT */
    fsal_attrib_list_t    * file_attributes        /* [ IN/OUT ] */) ;

fsal_status_t MFSL_open_by_fileid(
    mfsl_object_t         * filehandle,             /* IN */
    fsal_u64_t              fileid,                 /* IN */
    fsal_op_context_t     * p_context,              /* IN */
    mfsl_context_t        * p_mfsl_context,         /* IN */
    fsal_openflags_t        openflags,              /* IN */
    fsal_file_t           * file_descriptor,        /* OUT */
    fsal_attrib_list_t    * file_attributes         /* [ IN/OUT ] */ ) ;

fsal_status_t MFSL_read(
    fsal_file_t           * file_descriptor,         /*  IN  */
    fsal_seek_t           * seek_descriptor,         /* [IN] */
    fsal_size_t             buffer_size,             /*  IN  */
    caddr_t                 buffer,                  /* OUT  */
    fsal_size_t           * read_amount,             /* OUT  */
    fsal_boolean_t        * end_of_file,             /* OUT  */
    mfsl_context_t        * p_mfsl_context           /* IN */
);

fsal_status_t MFSL_write(
    fsal_file_t        * file_descriptor,     /* IN */
    fsal_seek_t        * seek_descriptor,     /* IN */
    fsal_size_t         buffer_size,          /* IN */
    caddr_t             buffer,               /* IN */
    fsal_size_t        * write_amount,        /* OUT */
    mfsl_context_t     * p_mfsl_context       /* IN */
);

fsal_status_t MFSL_close(
    fsal_file_t        * file_descriptor, /* IN */
    mfsl_context_t        * p_mfsl_context                 /* IN */
);

fsal_status_t MFSL_close_by_fileid(
    fsal_file_t        * file_descriptor /* IN */,
    fsal_u64_t           fileid, 
    mfsl_context_t     * p_mfsl_context );                /* IN */


fsal_status_t MFSL_readlink(
    mfsl_object_t         * linkhandle,     /* IN */
    fsal_op_context_t     * p_context,      /* IN */
    mfsl_context_t        * p_mfsl_context, /* IN */
    fsal_path_t           * p_link_content, /* OUT */
    fsal_attrib_list_t    * link_attributes /* [ IN/OUT ] */
);

fsal_status_t MFSL_symlink(
    mfsl_object_t         * parent_directory_handle,   /* IN */
    fsal_name_t           * p_linkname,                /* IN */
    fsal_path_t           * p_linkcontent,             /* IN */
    fsal_op_context_t     * p_context,                 /* IN */
    mfsl_context_t        * p_mfsl_context,            /* IN */
    fsal_accessmode_t       accessmode,                /* IN (ignored); */
    mfsl_object_t         * link_handle,               /* OUT */
    fsal_attrib_list_t    * link_attributes            /* [ IN/OUT ] */
);

fsal_status_t MFSL_rename(
    mfsl_object_t         * old_parentdir_handle, /* IN */
    fsal_name_t           * p_old_name,           /* IN */
    mfsl_object_t         * new_parentdir_handle, /* IN */
    fsal_name_t           * p_new_name,           /* IN */
    fsal_op_context_t     * p_context,            /* IN */
    mfsl_context_t        * p_mfsl_context,       /* IN */
    fsal_attrib_list_t    * src_dir_attributes,   /* [ IN/OUT ] */
    fsal_attrib_list_t    * tgt_dir_attributes    /* [ IN/OUT ] */
);

fsal_status_t MFSL_unlink(
    mfsl_object_t           * parentdir_handle,     /* IN */
    fsal_name_t             * p_object_name,        /* IN */
    fsal_op_context_t       * p_context,            /* IN */
    mfsl_context_t          * p_mfsl_context,       /* IN */
    fsal_attrib_list_t      * parentdir_attributes  /* [IN/OUT ] */
);

fsal_status_t MFSL_mknode(
    mfsl_object_t             * parentdir_handle,       /* IN */
    fsal_name_t               * p_node_name,            /* IN */
    fsal_op_context_t         * p_context,              /* IN */
    mfsl_context_t            * p_mfsl_context,         /* IN */
    fsal_accessmode_t           accessmode,             /* IN */
    fsal_nodetype_t             nodetype,               /* IN */
    fsal_dev_t                * dev,                    /* IN */
    mfsl_object_t             * p_object_handle,        /* OUT */
    fsal_attrib_list_t        * node_attributes         /* [ IN/OUT ] */
);

fsal_status_t  MFSL_rcp(
    mfsl_object_t           *   filehandle,     /* IN */
    fsal_op_context_t       *   p_context,      /* IN */
    mfsl_context_t          *   p_mfsl_context, /* IN */
    fsal_path_t             *   p_local_path,   /* IN */
    fsal_rcpflag_t              transfer_opt    /* IN */
);

fsal_status_t  MFSL_rcp_by_name(
    mfsl_object_t             * filehandle,     /* IN */
    fsal_name_t               * pfilename,      /* IN */
    fsal_op_context_t         * p_context,      /* IN */
    mfsl_context_t            * p_mfsl_context, /* IN */
    fsal_path_t               * p_local_path,   /* IN */
    fsal_rcpflag_t            transfer_opt      /* IN */
);

fsal_status_t  MFSL_rcp_by_fileid(
    mfsl_object_t             * filehandle,     /* IN */
    fsal_u64_t                  fileid,         /* IN */
    fsal_op_context_t         * p_context,      /* IN */
    mfsl_context_t            * p_mfsl_context, /* IN */
    fsal_path_t               * p_local_path,   /* IN */
    fsal_rcpflag_t            transfer_opt      /* IN */
);


/* To be called before exiting */
fsal_status_t  MFSL_terminate( ) ;


#ifndef _USE_SWIG

/******************************************************
 *                FSAL locks management.
 ******************************************************/

fsal_status_t  MFSL_lock(
    mfsl_object_t           * objecthandle,     /* IN */
    fsal_op_context_t       * p_context,        /* IN */
    mfsl_context_t          * p_mfsl_context,   /* IN */
    fsal_lockparam_t        * lock_info,        /* IN */
    fsal_lockdesc_t         * lock_descriptor   /* OUT */
);

fsal_status_t  MFSL_changelock(
    fsal_lockdesc_t         * lock_descriptor,      /* IN / OUT */
    fsal_lockparam_t        * lock_info,            /* IN */
    mfsl_context_t        * p_mfsl_context          /* IN */
);

fsal_status_t  MFSL_unlock(
    fsal_lockdesc_t * lock_descriptor,        /* IN/OUT */
    mfsl_context_t  * p_mfsl_context          /* IN */
);



#endif /* ! _USE_SWIG */

#endif /* _MFSL_H */

