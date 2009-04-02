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
 * MFSL_setattr_async_op: Callback for asynchronous setattr. 
 *
 * Callback for asynchronous setattr. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_setattrs.
 */
fsal_status_t  MFSL_setattr_async_op( mfsl_async_op_desc_t  * popasyncdesc )
{
  fsal_status_t fsal_status ;

  DisplayLogLevel( NIV_DEBUG, "Making asynchronous FSAL_setattrs for async op %p", popasyncdesc ) ;

  fsal_status = FSAL_setattrs( popasyncdesc->op_args.setattr.pfsal_handle,
                               popasyncdesc->fsal_op_context,
                               &popasyncdesc->op_args.setattr.attr,
                               &popasyncdesc->op_res.setattr.attr ) ;

  return fsal_status ; 
} /* MFSL_setattr_async_op */

/**
 *
 * MFSAL_setattrs_check_perms : Checks authorization to perform an asynchronous setattr.
 *
 * Checks authorization to perform an asynchronous setattr.
 *
 * @param filehandle        [IN]    mfsl object to be operated on.
 * @param pspecdata         [INOUT] mfsl object associated specific data
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param attrib_set        [IN]    attributes to be set 
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_setattrs_check_perms( mfsl_object_t                * filehandle,        /* IN */
                                          mfsl_object_specific_data_t  * pspecdata,         /* IN */ 
					  fsal_op_context_t            * p_context,         /* IN */
    					  mfsl_context_t               * p_mfsl_context,    /* IN */
    					  fsal_attrib_list_t           * attrib_set         /* IN */ )
{
  fsal_status_t fsal_status ;

  fsal_status = FSAL_setattr_access( p_context, attrib_set, &pspecdata->async_attr ) ;
  
  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ; 

  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_setattr_check_perms */

/**
 *
 * MFSL_setattrs : posts an asynchronous setattr and sets the cached attributes in return.
 *
 * Posts an asynchronous setattr and sets the cached attributes in return.
 * If the object is not asynchronous, then the content of object attributes will be used to populate it.
 *
 * @param filehandle        [IN]    mfsl object to be operated on.
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param attrib_set        [IN]    attributes to be set 
 * @param object_attributes [INOUT] resulting attributes
 *
 * @return the same as FSAL_setattrs
 */
fsal_status_t MFSL_setattrs(
    mfsl_object_t         * filehandle,        /* IN */
    fsal_op_context_t     * p_context,         /* IN */
    mfsl_context_t        * p_mfsl_context,    /* IN */
    fsal_attrib_list_t    * attrib_set,        /* IN */
    fsal_attrib_list_t    * object_attributes  /* [ IN/OUT ] */
)
{
  fsal_status_t fsal_status ;
  mfsl_async_op_desc_t        * pasyncopdesc = NULL ;
  mfsl_object_specific_data_t * pasyncdata   = NULL ;

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
      DisplayLog( "MFSL_setattrs: cannot get time of day... exiting" ) ;
      exit( 1 ) ;
   }

  /* Is the object asynchronous ? */
  if( !mfsl_async_get_specdata( filehandle, &pasyncdata ) )
   {
	/* Not yet asynchronous object */
        P( p_mfsl_context->lock ) ;

  	GET_PREALLOC( pasyncdata,
        	      p_mfsl_context->pool_spec_data,
                      mfsl_param.nb_pre_async_op_desc,
               	      mfsl_object_specific_data_t,
               	      next_alloc ) ;

  	V( p_mfsl_context->lock ) ;

	/* In this case use object_attributes parameter to initiate asynchronous object */
	pasyncdata->async_attr = *object_attributes ;
   }

  fsal_status = MFSAL_setattrs_check_perms( filehandle, pasyncdata, p_context, p_mfsl_context, attrib_set ) ;

  if( FSAL_IS_ERROR( fsal_status ) )
   return fsal_status ;

  DisplayLogJdLevel( p_mfsl_context->log_outputs, NIV_DEBUG, "Creating asyncop %p", pasyncopdesc ) ;
  
  pasyncopdesc->op_type    = MFSL_ASYNC_OP_SETATTR ;
  pasyncopdesc->op_mobject = filehandle ;
  pasyncopdesc->op_args.setattr.pfsal_handle = &filehandle->handle ;
  pasyncopdesc->op_args.setattr.attr = *attrib_set ;
  pasyncopdesc->op_res.setattr.attr = *attrib_set ;

  pasyncopdesc->op_func = MFSL_setattr_async_op ;
  pasyncopdesc->fsal_op_context = p_context ;

  pasyncopdesc->ptr_mfsl_context = (caddr_t)p_mfsl_context ;

  fsal_status = MFSL_async_post( pasyncopdesc ) ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    return fsal_status ;

 
  /* Update the associated times for this object */
  pasyncdata->async_attr.ctime.seconds  = pasyncopdesc->op_time.tv_sec ;
  pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec ; /** @todo: there may be a coefficient to be applied here */
  filehandle->health = MFSL_ASYNC_ASYNCHRONOUS ;

  /* merge the attributes to the asynchronous attributes */
  if( ( attrib_set->asked_attributes & FSAL_ATTR_SIZE ) ||
      ( attrib_set->asked_attributes & FSAL_ATTR_SPACEUSED) )
    {
       /* Operation on a non data cached file */
       pasyncdata->async_attr.filesize   = attrib_set->filesize ;
       pasyncdata->async_attr.spaceused  = attrib_set->spaceused ;
    }
  
      
  if( attrib_set->asked_attributes & (FSAL_ATTR_MODE|FSAL_ATTR_OWNER|FSAL_ATTR_GROUP) )
    {
      if( attrib_set->asked_attributes & FSAL_ATTR_MODE )
        pasyncdata->async_attr.mode = attrib_set->mode ;
      
      if( attrib_set->asked_attributes & FSAL_ATTR_OWNER )
        pasyncdata->async_attr.owner = attrib_set->owner ;
      
      if( attrib_set->asked_attributes & FSAL_ATTR_GROUP )
        pasyncdata->async_attr.group = attrib_set->group ;
    }
  
  if( attrib_set->asked_attributes & (FSAL_ATTR_ATIME|FSAL_ATTR_MTIME) )
    {
      if( attrib_set->asked_attributes & FSAL_ATTR_ATIME )
        pasyncdata->async_attr.atime = attrib_set->atime ;
      
      if( attrib_set->asked_attributes & FSAL_ATTR_MTIME )
        pasyncdata->async_attr.mtime =attrib_set->mtime ;
    }  
  
  /* Set output attributes */
  *object_attributes = pasyncdata->async_attr ;

  if( !mfsl_async_set_specdata( filehandle, pasyncdata ) )
    MFSL_return( ERR_FSAL_SERVERFAULT, 0 ) ;

  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_setattr */



#endif /* ! _USE_SWIG */


