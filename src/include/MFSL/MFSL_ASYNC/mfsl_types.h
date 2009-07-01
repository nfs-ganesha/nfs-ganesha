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
 * \file    mfsl_types.h
 */

#ifndef _MFSL_ASYNC_TYPES_H
#define _MFSL_ASYNC_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_MFSL_ASYNC          "MFSL_Async"

#define MFSL_ASYNC_DEFAULT_NB_SYNCLETS 10
#define MFSL_ASYNC_DEFAULT_SLEEP_TIME  60 
#define MFSL_ASYNC_DEFAULT_BEFORE_GC   10 
#define MFSL_ASYNC_DEFAULT_NB_PREALLOCATED_DIRS  10
#define MFSL_ASYNC_DEFAULT_NB_PREALLOCATED_FILES 100

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <dirent.h> /* for MAXNAMLEN */
#include "config_parsing.h"
#include "LRU_List.h"
#include "HashTable.h"
#include "err_fsal.h"
#include "err_mfsl.h"


typedef enum mfsl_async_health__ { MFSL_ASYNC_SYNCHRONOUS   = 0,
				   MFSL_ASYNC_ASYNCHRONOUS  = 1,
                                   MFSL_ASYNC_NEVER_SYNCED  = 2,
                                   MFSL_ASYNC_IS_SYMLINK    = 3  } mfsl_async_health_t ;

typedef struct mfsl_object_specific_data__{
  fsal_attrib_list_t     async_attr ;
  unsigned int           deleted ;
  struct  mfsl_object_specific_data__ * next_alloc ; 
} mfsl_object_specific_data_t ;

typedef struct mfsl_object__
{
  fsal_handle_t          handle ; 
  pthread_mutex_t     *  plock ;
  mfsl_async_health_t    health ;
} mfsl_object_t ;

typedef struct mfsl_precreated_object__
{
   mfsl_object_t                      mobject ;
   fsal_name_t                        name ;
   fsal_attrib_list_t                 attr ;
   unsigned int                       inited ;
   struct mfsl_precreated_object__  * next_alloc ;
} mfsl_precreated_object_t ;

typedef struct mfsl_synclet_context__ {
  pthread_mutex_t                lock ;
} mfsl_synclet_context_t ;


typedef enum mfsl_async_addr_type__ { MFSL_ASYNC_ADDR_DIRECT   = 1,
                                      MFSL_ASYNC_ADDR_INDIRECT = 2 } mfsl_async_addr_type_t ;

typedef struct mfsl_synclet_data__
{
  unsigned int             my_index ;
  pthread_cond_t           op_condvar ;
  pthread_mutex_t          mutex_op_condvar;
  fsal_op_context_t        root_fsal_context ;
  mfsl_synclet_context_t   synclet_context ;
  pthread_mutex_t          mutex_op_lru;
  unsigned int             passcounter ; 
  LRU_list_t            *  op_lru ;
} mfsl_synclet_data_t ;

typedef enum mfsl_async_op_type__
{
  MFSL_ASYNC_OP_CREATE     = 0,
  MFSL_ASYNC_OP_MKDIR      = 1,
  MFSL_ASYNC_OP_LINK       = 2,
  MFSL_ASYNC_OP_REMOVE     = 3,
  MFSL_ASYNC_OP_RENAME     = 4,
  MFSL_ASYNC_OP_SETATTR    = 5,
  MFSL_ASYNC_OP_TRUNCATE   = 6
} mfsl_async_op_type_t ;

static const char * mfsl_async_op_name [] = { "MFSL_ASYNC_OP_CREATE",
                                              "MFSL_ASYNC_OP_MKDIR",
                                              "MFSL_ASYNC_OP_LINK",
                                              "MFSL_ASYNC_OP_REMOVE",
                                              "MFSL_ASYNC_OP_RENAME",
                                              "MFSL_ASYNC_OP_SETATTR",
                                              "MFSL_ASYNC_OP_TRUNCATE" } ;

 		          
typedef struct mfsl_async_op_create_args__
{
  fsal_name_t           precreate_name ;
  mfsl_object_t      *  pmfsl_obj_dirdest ;
  fsal_name_t           filename ;
  fsal_accessmode_t     mode ;
  fsal_uid_t            owner ;
  fsal_gid_t            group ;
} mfsl_async_op_create_args_t ;

typedef struct mfsl_async_op_create_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_create_res_t ;

typedef struct mfsl_async_op_mkdir_args__
{
  fsal_name_t           precreate_name ;
  mfsl_object_t      *  pmfsl_obj_dirdest ;
  fsal_name_t           dirname ;
  fsal_accessmode_t     mode ;
  fsal_uid_t            owner ;
  fsal_gid_t            group ;
} mfsl_async_op_mkdir_args_t ;

typedef struct mfsl_async_op_mkdir_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_mkdir_res_t ;

typedef struct mfsl_async_op_link_args__
{
  mfsl_object_t      *  pmobject_src ;
  mfsl_object_t      *  pmobject_dirdest ;
  fsal_name_t           name_link ;
} mfsl_async_op_link_args_t ;

typedef struct mfsl_async_op_link_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_link_res_t ;

typedef struct mfsl_async_op_remove_args__
{
  mfsl_object_t      *  pmobject ;
  fsal_name_t           name ;
} mfsl_async_op_remove_args_t ;

typedef struct mfsl_async_op_remove_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_remove_res_t ;

typedef struct mfsl_async_op_rename_args__
{
  mfsl_object_t      *  pmobject_src ;
  fsal_name_t           name_src ;
  mfsl_object_t      *  pmobject_dirdest ;
  fsal_name_t           name_dest ;
} mfsl_async_op_rename_args_t ;

typedef struct mfsl_async_op_rename_res__
{
  fsal_attrib_list_t attrsrc ;
  fsal_attrib_list_t attrdest ;
} mfsl_async_op_rename_res_t ;

typedef struct mfsl_async_op_setattr_args__
{
  mfsl_object_t      *  pmobject ;
  fsal_attrib_list_t    attr         ;
} mfsl_async_op_setattr_args_t ;

typedef struct mfsl_async_op_setattr_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_setattr_res_t ;

typedef struct mfsl_async_op_truncate_args__
{
  mfsl_object_t      *  pmobject ;
  fsal_size_t           size ;
} mfsl_async_op_truncate_args_t ;

typedef struct mfsl_async_op_truncate_res__
{
  fsal_attrib_list_t attr ;
} mfsl_async_op_truncate_res_t ;

typedef union mfsl_async_op_args__
{
  mfsl_async_op_create_args_t   create ;
  mfsl_async_op_mkdir_args_t    mkdir ;
  mfsl_async_op_link_args_t     link ;
  mfsl_async_op_remove_args_t   remove ;
  mfsl_async_op_rename_args_t   rename ;
  mfsl_async_op_setattr_args_t  setattr ; 
  mfsl_async_op_truncate_args_t truncate ;
} mfsl_async_op_args_t ;

typedef union mfsl_async_op_res__
{
  mfsl_async_op_create_res_t   create ;
  mfsl_async_op_mkdir_res_t    mkdir ;
  mfsl_async_op_link_res_t     link ;
  mfsl_async_op_remove_res_t   remove ;
  mfsl_async_op_rename_res_t   rename ;
  mfsl_async_op_setattr_res_t  setattr ; 
  mfsl_async_op_truncate_res_t truncate ;
} mfsl_async_op_res_t ;


typedef struct mfsl_async_op_desc__ 
{
  struct timeval                  op_time ;
  mfsl_async_op_type_t            op_type ;
  mfsl_async_op_args_t            op_args ;
  mfsl_async_op_res_t             op_res ;
  mfsl_object_t                 * op_mobject ;
  fsal_status_t                   (*op_func)( struct mfsl_async_op_desc__ *) ;
  fsal_op_context_t             * fsal_op_context ;
  caddr_t                         ptr_mfsl_context ;
  unsigned int                    related_synclet_index ;
  struct  mfsl_async_op_desc__  * next_alloc ;
} mfsl_async_op_desc_t ;

void * mfsl_synclet_thread( void * Arg ) ;
void * mfsl_asynchronous_dispatcher_thread( void * Arg ) ;

/* Async Operations on FSAL */
fsal_status_t  mfsl_async_create(   mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_mkdir(    mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_link(     mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_remove(   mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_rename(   mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_setattr(  mfsl_async_op_desc_t  * popasyncdesc ) ;
fsal_status_t  mfsl_async_truncate( mfsl_async_op_desc_t  * popasyncdesc ) ;

typedef struct mfsl_parameter__{
  unsigned int        nb_pre_async_op_desc ;     /**< Number of preallocated Aync Op descriptors       */
  unsigned int        nb_synclet           ;     /**< Number of synclet to be used                     */
  unsigned int        async_window_sec     ;     /**< Asynchronos Task Dispatcher Window (seconds)     */
  unsigned int        async_window_usec    ;     /**< Asynchronos Task Dispatcher Window (useconds)    */
  unsigned int        nb_before_gc         ;     /**< Numbers of calls before LRU invalide GC          */
  LRU_parameter_t     lru_async_param      ;     /**< Asynchorous Synclet Tasks LRU parameters         */
  unsigned int        nb_pre_create_dirs   ;     /**< The size of pre-created directories per synclet  */
  unsigned int        nb_pre_create_files  ;     /**< The size of pre-created files per synclet        */
  char                pre_create_obj_dir[MAXPATHLEN] ; /**< Directory for pre-createed objects         */
  LRU_parameter_t     lru_param ;                      /**< Parameter to LRU for async op              */
} mfsl_parameter_t ;

typedef struct mfsl_context__ {
  mfsl_object_specific_data_t  * pool_spec_data ;
  mfsl_async_op_desc_t         * pool_async_op ;
  pthread_mutex_t                lock ;
  unsigned int                   synclet_index ;
  log_t                          log_outputs ;
  mfsl_precreated_object_t     * pool_dirs ; 
  mfsl_precreated_object_t     * pool_files ; 
  unsigned int                   avail_pool_dirs ;
  unsigned int                   avail_pool_files ;
} mfsl_context_t ;


int mfsl_async_hash_init( void ) ;
int mfsl_async_set_specdata( mfsl_object_t                * key,
               		     mfsl_object_specific_data_t  * value ) ;
int mfsl_async_get_specdata( mfsl_object_t                 * key,
               		     mfsl_object_specific_data_t  ** value ) ;
int mfsl_async_remove_specdata( mfsl_object_t   * key ) ;

void * mfsl_async_synclet_thread( void * Arg ) ;
void * mfsl_async_asynchronous_dispatcher_thread( void * Arg ) ;
fsal_status_t  mfsl_async_post_async_op( mfsl_async_op_desc_t  * popdes, 
					 mfsl_object_t         * pmobject )  ;
fsal_status_t  MFSL_async_post( mfsl_async_op_desc_t  * popdesc ) ;

fsal_status_t mfsl_async_init_precreated_directories( fsal_op_context_t         * pcontext,
						      mfsl_precreated_object_t  * pool_dirs ) ;

fsal_status_t mfsl_async_init_precreated_files( fsal_op_context_t         * pcontext,
						mfsl_precreated_object_t  * pool_dirs ) ;

fsal_status_t  mfsl_async_init_clean_precreated_objects( fsal_op_context_t * pcontext ) ; 

int mfsl_async_is_object_asynchronous( mfsl_object_t * object ) ;

void constructor_preacreated_entries( void * ptr ) ;

fsal_status_t MFSL_PrepareContext( fsal_op_context_t  * pcontext ) ;

fsal_status_t MFSL_RefreshContext( mfsl_context_t     * pcontext,
                                   fsal_op_context_t  * pfsal_context  ) ;

fsal_status_t MFSL_ASYNC_GetSyncletContext( mfsl_synclet_context_t  * pcontext,
                                            fsal_op_context_t       * pfsal_context  ) ;

fsal_status_t MFSL_ASYNC_RefreshSyncletContext( mfsl_synclet_context_t  * pcontext,
                                                 fsal_op_context_t       * pfsal_context  )  ;

int MFSL_ASYNC_is_synced( mfsl_object_t * mobject ) ;

#endif /* _MFSL_ASYNC_TYPES_H */
