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

extern mfsl_parameter_t  mfsl_param ;

/**
 *
 * MFSL_link_async_op: Callback for asynchronous link. 
 *
 * Callback for asynchronous link. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_setattrs.
 */
fsal_status_t  MFSL_link_async_op( mfsl_async_op_desc_t  * popasyncdesc )
{
  fsal_status_t fsal_status ;

  DisplayLogLevel( NIV_DEBUG, "Making asynchronous FSAL_link for async op %p", popasyncdesc ) ;

  fsal_status = FSAL_link( &popasyncdesc->op_args.link.pmobject_src->handle,
			   &popasyncdesc->op_args.link.pmobject_dirdest->handle,
			   &popasyncdesc->op_args.link.name_link,
                           popasyncdesc->fsal_op_context,
                           &popasyncdesc->op_res.link.attr ) ;
  return fsal_status ; 
} /* MFSL_link_async_op */

/**
 *
 * MFSAL_link_check_perms : Checks authorization to perform an asynchronous setattr.
 *
 * Checks authorization to perform an asynchronous link.
 *
 * @param target_handle     [IN]    mfsl object to be operated on.
 * @param dir_handle        [IN]    mfsl object to be operated on.
 * @param tgt_pspecdata     [INOUT] mfsl object associated specific data
 * @param dir_pspecdata     [INOUT] mfsl object associated specific data
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_link_check_perms( 	mfsl_object_t                * target_handle,     /* IN */
    				 	mfsl_object_t                * dir_handle,        /* IN */
    			                fsal_name_t                  * p_link_name,       /* IN */
				        mfsl_object_specific_data_t  * tgt_pspecdata,     /* IN */
				        mfsl_object_specific_data_t  * dir_pspecdata,     /* IN */
    					fsal_op_context_t            * p_context,         /* IN */
    					mfsl_context_t               * p_mfsl_context     /* IN */ )
{
  fsal_status_t fsal_status ;

  fsal_status = FSAL_link_access( p_context, &dir_pspecdata->async_attr ) ;

  if( FSAL_IS_ERROR( fsal_status ) ) 
   return fsal_status ;

  /** @todo : put some stuff in this function */
  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_link_check_perms */

/**
 *
 * MFSL_link : posts an asynchronous link and sets the cached attributes in return.
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
fsal_status_t MFSL_link(  mfsl_object_t         * target_handle,     /* IN */
    			  mfsl_object_t         * dir_handle,        /* IN */
    			  fsal_name_t           * p_link_name,       /* IN */
    			  fsal_op_context_t     * p_context,         /* IN */
    			  mfsl_context_t        * p_mfsl_context,    /* IN */
    			  fsal_attrib_list_t    * tgt_attributes,    /* [ IN/OUT ] */ 
    			  fsal_attrib_list_t    * dir_attributes     /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status ;
  mfsl_async_op_desc_t        * pasyncopdesc = NULL ;
  mfsl_object_specific_data_t * tgt_pasyncdata   = NULL ;
  mfsl_object_specific_data_t * dir_pasyncdata   = NULL ;

  P( p_mfsl_context->lock ) ;

  GET_PREALLOC( pasyncopdesc,
                p_mfsl_context->pool_async_op,
                mfsl_param.nb_pre_async_op_desc,
                mfsl_async_op_desc_t,
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

  if( !mfsl_async_get_specdata( target_handle, &tgt_pasyncdata ) )
   {
	/* Target is not yet asynchronous */
	P( p_mfsl_context->lock ) ;

  	GET_PREALLOC( tgt_pasyncdata,
        	      p_mfsl_context->pool_spec_data,
                      mfsl_param.nb_pre_async_op_desc,
               	      mfsl_object_specific_data_t,
               	      next_alloc ) ;

  	V( p_mfsl_context->lock ) ;

	/* In this case use object_attributes parameter to initiate asynchronous object */
	tgt_pasyncdata->async_attr = *tgt_attributes ;
   }

  if( !mfsl_async_get_specdata( dir_handle, &dir_pasyncdata ) )
   {
	/* Target is not yet asynchronous */
	P( p_mfsl_context->lock ) ;

 	
  	GET_PREALLOC( dir_pasyncdata,
        	      p_mfsl_context->pool_spec_data,
                      mfsl_param.nb_pre_async_op_desc,
               	      mfsl_object_specific_data_t,
               	      next_alloc ) ;

  	V( p_mfsl_context->lock ) ;

	/* In this case use object_attributes parameter to initiate asynchronous object */
	dir_pasyncdata->async_attr = *dir_attributes ;
   }
  
  fsal_status = MFSAL_link_check_perms( target_handle, 
					dir_handle, 
                                        p_link_name,
					tgt_pasyncdata, 
					dir_pasyncdata, 
					p_context, 
					p_mfsl_context ) ;

  if( FSAL_IS_ERROR( fsal_status ) )
   return fsal_status ;


  DisplayLogJdLevel( p_mfsl_context->log_outputs, NIV_DEBUG, "Creating asyncop %p", pasyncopdesc ) ;
  
  pasyncopdesc->op_type    = MFSL_ASYNC_OP_LINK ;
  pasyncopdesc->op_args.link.pmobject_src     = target_handle ;
  pasyncopdesc->op_args.link.pmobject_dirdest = dir_handle ;
  pasyncopdesc->op_args.link.name_link        = *p_link_name ;
  pasyncopdesc->op_res.link.attr              = *tgt_attributes ; 

  pasyncopdesc->op_func = MFSL_link_async_op ;
  pasyncopdesc->fsal_op_context = p_context ;

  pasyncopdesc->ptr_mfsl_context = (caddr_t)p_mfsl_context ;

  fsal_status = MFSL_async_post( pasyncopdesc ) ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ;

  /* Update the asynchronous metadata */
  tgt_pasyncdata->health = MFSL_ASYNC_ASYNCHRONOUS ;
  tgt_pasyncdata->async_attr.ctime.seconds  = pasyncopdesc->op_time.tv_sec ;
  tgt_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec ; /** @todo: there may be a coefficient to be applied here */
  tgt_pasyncdata->async_attr.numlinks += 1 ;

  dir_pasyncdata->health = MFSL_ASYNC_ASYNCHRONOUS ;
  dir_pasyncdata->async_attr.ctime.seconds  = pasyncopdesc->op_time.tv_sec ;
  dir_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec ; /** @todo: there may be a coefficient to be applied here */

  if( !mfsl_async_set_specdata( target_handle, tgt_pasyncdata ) )
    MFSL_return( ERR_FSAL_SERVERFAULT, 0 ) ;

  if( !mfsl_async_set_specdata( dir_handle, dir_pasyncdata ) )
    MFSL_return( ERR_FSAL_SERVERFAULT, 0 ) ;

 
  /* Return the correct attributes */ 
  *tgt_attributes = tgt_pasyncdata->async_attr ;
  *dir_attributes = dir_pasyncdata->async_attr ;

  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_link */



#endif /* ! _USE_SWIG */


