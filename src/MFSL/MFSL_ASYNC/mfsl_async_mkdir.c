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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"
#include "stuff_alloc.h"

#ifndef _USE_SWIG

extern mfsl_parameter_t       mfsl_param ;
extern fsal_handle_t          dir_handle_precreate ;
extern mfsl_synclet_data_t  * synclet_data ;

/**
 *
 * MFSL_mkdir_async_op: Callback for asynchronous link. 
 *
 * Callback for asynchronous link. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_setattrs.
 */
fsal_status_t  MFSL_mkdir_async_op( mfsl_async_op_desc_t  * popasyncdesc )
{
  fsal_status_t fsal_status ;
  fsal_attrib_list_t attrsrc, attrdest, chown_attr ;
  fsal_handle_t      handle ;

  attrsrc = attrdest = popasyncdesc->op_res.mkdir.attr ;

  DisplayLogLevel( NIV_DEBUG, "Renaming directory to complete asynchronous FSAL_mkdir for async op %p", popasyncdesc ) ;

  fsal_status = FSAL_rename( &dir_handle_precreate,
                             &popasyncdesc->op_args.mkdir.precreate_name,
                             &(popasyncdesc->op_args.mkdir.pmfsl_obj_dirdest->handle),
                             &popasyncdesc->op_args.mkdir.dirname,
                             popasyncdesc->fsal_op_context,
                             &attrsrc,
                             &attrdest )  ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ;

  /* Lookup to get the right attributes for the object */
  fsal_status = FSAL_lookup( &(popasyncdesc->op_args.mkdir.pmfsl_obj_dirdest->handle),
			     &popasyncdesc->op_args.mkdir.dirname,
 			     popasyncdesc->fsal_op_context,
			     &handle,
                             &popasyncdesc->op_res.mkdir.attr );

  if( FSAL_IS_ERROR( fsal_status ) )
    return fsal_status ;

  /* If user is not root, setattr to chown the entry */
  if( popasyncdesc->op_args.mkdir.owner != 0 )
   {
     chown_attr.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP ; 
     chown_attr.mode  = popasyncdesc->op_args.mkdir.mode  ; 
     chown_attr.owner = popasyncdesc->op_args.mkdir.owner ; 
     chown_attr.group = popasyncdesc->op_args.mkdir.group ;
 
     fsal_status = FSAL_setattrs( &handle, popasyncdesc->fsal_op_context, &chown_attr,  &popasyncdesc->op_res.mkdir.attr ) ;
   }
  return fsal_status ; 
} /* MFSL_mkdir_async_op */

/**
 *
 * MFSAL_mkdir_check_perms : Checks authorization to perform an asynchronous mkdir.
 *
 * Checks authorization to perform an asynchronous mkdir.
 *
 * @param target_handle     [IN]    mfsl object to be operated on.
 * @param p_dirname         [IN]    name of the object to be created
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param object_attributes [IN]    object-attributes
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_mkdir_check_perms( 	mfsl_object_t                * target_handle,     
    			                fsal_name_t                  * p_dirname,        
    					fsal_op_context_t            * p_context,        
    					mfsl_context_t               * p_mfsl_context,
					fsal_attrib_list_t           * object_attributes )
{
  fsal_status_t fsal_status ;

  fsal_status = FSAL_create_access( p_context, object_attributes ) ;

  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ;

  /** @todo : put some stuff in this function */
  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_mkdir_check_perms */

/**
 *
 * MFSL_mkdir : posts an asynchronous mkdir and sets the cached attributes in return.
 *
 * Posts an asynchronous setattr and sets the cached attributes in return.
 * If an object is not asynchronous, then the content of object attributes structure for result will be used to populate it.
 *
 * @param target_handle     [IN]    mfsl object to be operated on (object to be hard linked).
 * @param dir_handle        [IN]    mfsl object to be operated on (destination directory for the link).
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param attrib_set        [IN]    attributes to be set 
 * @param tgt_attributes    [INOUT] resulting attributes for target
 * @param dir_attributes    [INOUT] resulting attributes for directory 
 *
 * @return the same as FSAL_link
 */
fsal_status_t MFSL_mkdir(  mfsl_object_t         * parent_directory_handle, /* IN */
    			   fsal_name_t           * p_dirname,               /* IN */
   			   fsal_op_context_t     * p_context,               /* IN */
  			   mfsl_context_t        * p_mfsl_context,          /* IN */
    			   fsal_accessmode_t       accessmode,              /* IN */
    			   mfsl_object_t         * object_handle,           /* OUT */
    			   fsal_attrib_list_t    * object_attributes,       /* [ IN/OUT ] */ 
			   fsal_attrib_list_t    * parent_attributes        /* IN */)
{
  fsal_status_t                 fsal_status ;
  mfsl_async_op_desc_t        * pasyncopdesc = NULL ;
  mfsl_object_specific_data_t * newdir_pasyncdata   = NULL ;
  mfsl_object_t               * pnewdir_handle = NULL ;
  mfsl_precreated_object_t    * pprecreated = NULL ;

  fsal_status = MFSAL_mkdir_check_perms( parent_directory_handle,
                                         p_dirname,
					 p_context, 
					 p_mfsl_context,
                                         parent_attributes ) ;

  if( FSAL_IS_ERROR( fsal_status ) )
   return fsal_status ;

  P( p_mfsl_context->lock ) ;

  GET_PREALLOC( pasyncopdesc,
                p_mfsl_context->pool_async_op,
                mfsl_param.nb_pre_async_op_desc,
                mfsl_async_op_desc_t,
                next_alloc ) ;

  GET_PREALLOC( newdir_pasyncdata,
                p_mfsl_context->pool_spec_data,
                mfsl_param.nb_pre_async_op_desc,
                mfsl_object_specific_data_t,
                next_alloc ) ;

  V( p_mfsl_context->lock ) ;

  if( pasyncopdesc == NULL )
    MFSL_return( ERR_FSAL_INVAL, 0 ) ;
   
  if( gettimeofday( &pasyncopdesc->op_time, NULL ) != 0 )
   {
      /* Could'not get time of day... Stopping, this may need a major failure */
      DisplayLog( "MFSL_link: cannot get time of day... exiting" ) ;
      exit( 1 ) ;
   }

 
  /* Now get a pre-allocated directory from the synclet data */
  P( p_mfsl_context->lock ) ;
  GET_PREALLOC_CONSTRUCT( pprecreated,
			  p_mfsl_context->pool_dirs,
                          mfsl_param.nb_pre_create_dirs,
			  mfsl_precreated_object_t,
			  next_alloc,
			  constructor_preacreated_entries ) ;
  p_mfsl_context->avail_pool_dirs -= 1 ;
  V( p_mfsl_context->lock ) ;

  pnewdir_handle = &(pprecreated->mobject ) ;

  DisplayLogJdLevel( p_mfsl_context->log_outputs, NIV_DEBUG, "Creating asyncop %p", pasyncopdesc ) ;
  
  pasyncopdesc->op_type    = MFSL_ASYNC_OP_MKDIR ;
  pasyncopdesc->op_args.mkdir.pmfsl_obj_dirdest         = parent_directory_handle ;
  pasyncopdesc->op_args.mkdir.precreate_name            = pprecreated->name ;
  pasyncopdesc->op_args.mkdir.dirname                   = *p_dirname ;
  pasyncopdesc->op_args.mkdir.mode                      = accessmode ;
  pasyncopdesc->op_args.mkdir.owner                     = FSAL_OP_CONTEXT_TO_UID( p_context ) ;
  pasyncopdesc->op_args.mkdir.group                     = FSAL_OP_CONTEXT_TO_GID( p_context ) ;
  pasyncopdesc->op_res.mkdir.attr.asked_attributes      = object_attributes->asked_attributes ;
  pasyncopdesc->op_res.mkdir.attr.supported_attributes  = object_attributes->supported_attributes ;

  if( FSAL_IS_ERROR( fsal_status ) )
    return fsal_status ;

  pasyncopdesc->op_func = MFSL_mkdir_async_op ;
  //pasyncopdesc->fsal_op_context = p_context ;
  pasyncopdesc->fsal_op_context = &synclet_data[pasyncopdesc->related_synclet_index].root_fsal_context ;

  pasyncopdesc->ptr_mfsl_context = (caddr_t)p_mfsl_context ;

  fsal_status = MFSL_async_post( pasyncopdesc ) ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ;

  /* Update the asynchronous metadata */
  newdir_pasyncdata->async_attr = pprecreated->attr ;

  newdir_pasyncdata->async_attr.type = FSAL_TYPE_DIR ;
  newdir_pasyncdata->async_attr.filesize = DEV_BSIZE ;
  newdir_pasyncdata->async_attr.spaceused = DEV_BSIZE ;
  newdir_pasyncdata->async_attr.numlinks = 2 ;

  newdir_pasyncdata->async_attr.owner = pasyncopdesc->op_args.mkdir.owner ; 
  newdir_pasyncdata->async_attr.group = pasyncopdesc->op_args.mkdir.group ;
  
  newdir_pasyncdata->async_attr.ctime.seconds  = pasyncopdesc->op_time.tv_sec ;
  newdir_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec ; /** @todo: there may be a coefficient to be applied here */

  newdir_pasyncdata->deleted = FALSE ;

  if( !mfsl_async_set_specdata( pnewdir_handle, newdir_pasyncdata ) )
    MFSL_return( ERR_FSAL_SERVERFAULT, 0 ) ;

  /* Return the correct attributes */ 
  *object_attributes = newdir_pasyncdata->async_attr ;
  *object_handle = pprecreated->mobject ;
  object_handle->health = MFSL_ASYNC_NEVER_SYNCED ;

  /* Do not forget that the parent directory becomes asynchronous too */
  parent_directory_handle->health = MFSL_ASYNC_ASYNCHRONOUS ;

  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_mkdir */



#endif /* ! _USE_SWIG */


