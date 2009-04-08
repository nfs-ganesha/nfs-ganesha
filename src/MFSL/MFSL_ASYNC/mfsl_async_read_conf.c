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
#include "stuff_alloc.h"
#include "log_functions.h"
#include "config_parsing.h"

#include "strings.h"
#include "string.h"

#ifndef _USE_SWIG

log_t              log_outputs ;              /**< Log descriptor                                   */


/******************************************************
 *            Attribute mask management.
 ******************************************************/

/**
 *
 * mfsl_async_clean_pending_request: cleans an entry in a nfs request LRU,
 *
 * cleans an entry in a nfs request LRU.
 *
 * @param pentry [INOUT] entry to be cleaned. 
 * @param addparam [IN] additional parameter used for cleaning.
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int mfsl_async_clean_pending_request( LRU_entry_t * pentry, void * addparam )
{
  //nfs_request_data_t ** preqnfspool = (nfs_request_data_t **)addparam ;
  //nfs_request_data_t *  preqnfs     = (nfs_request_data_t *)(pentry->buffdata.pdata) ;

  /* Send the entry back to the pool */
  //RELEASE_PREALLOC( preqnfs, *preqnfspool, next_alloc ) ;

  return 0 ;
} /* mfsl_async_clean_pending_request */

/**
 *
 * mfsl_async_print_pending_request: prints an entry related to a pending request in the LRU list.
 * 
 * prints an entry related to a pending request in the LRU list.
 *
 * @param data [IN] data stored in a LRU entry to be printed.
 * @param str [OUT] string used to store the result. 
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int mfsl_async_print_pending_request( LRU_data_t data, char *str )
{
  return snprintf( str, LRU_DISPLAY_STRLEN, "not implemented for now" ) ;
} /* print_pending_request */


/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */
fsal_status_t  MFSL_SetDefault_parameter(
                              mfsl_parameter_t *  out_parameter )     
{
   out_parameter->nb_pre_async_op_desc = 50 ;
   out_parameter->nb_synclet = 1 ;
   out_parameter->async_window_sec = 1 ;
   out_parameter->async_window_usec = 0 ;
   out_parameter->nb_before_gc = 500 ;
   out_parameter->nb_pre_create_dirs = 10 ;
   out_parameter->nb_pre_create_files = 10 ;
   strncpy( out_parameter->pre_create_obj_dir, "/tmp", MAXPATHLEN ) ;

   out_parameter->lru_param.nb_entry_prealloc  = 100 ;
   out_parameter->lru_param.nb_call_gc_invalid = 30 ;
   out_parameter->lru_param.clean_entry        = mfsl_async_clean_pending_request ;
   out_parameter->lru_param.entry_to_str       = mfsl_async_print_pending_request ;
   
   MFSL_return( ERR_FSAL_NO_ERROR, 0 )  ;
} /* MFSL_SetDefault_parameter */

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
fsal_status_t  MFSL_load_parameter_from_conf(
      config_file_t       in_config,
      mfsl_parameter_t *  pparam )
{
  int     err;
  int     var_max, var_index;
  char *  key_name;
  char *  key_value;
  config_item_t   block;

  int  DebugLevel   = -1;
  char * LogFile    = NULL;

   /* Is the config tree initialized ? */
  if( in_config == NULL || pparam == NULL )
      MFSL_return( ERR_FSAL_INVAL, 0 );

  /* Get the config BLOCK */
  if( ( block = config_FindItemByName( in_config, CONF_LABEL_MFSL_ASYNC ) ) == NULL )
    {
      DisplayLog( "/!\\ Cannot read item \"%s\" from configuration file\n", CONF_LABEL_MFSL_ASYNC ) ; 
      MFSL_return( ERR_FSAL_NOENT, 0 );
    }
  var_max = config_GetNbItems( block ) ;

  for( var_index = 0 ; var_index < var_max ; var_index++ )
    {
      config_item_t item;

      item = config_GetItemByIndex( block, var_index );

      if ( ( err = config_GetKeyValue( item,
                                       &key_name,
                                       &key_value ) ) > 0 )
	{
      	  DisplayLog( "MFSL ASYNC LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                      var_index, CONF_LABEL_MFSL_ASYNC );
          MFSL_return( ERR_FSAL_SERVERFAULT, err );
        }

      if( !strcasecmp( key_name, "Nb_Synclet" ) )
	{
		DisplayLog( "MFSL ASYNC LOAD PARAMETER: the asyncop scheduler is not yet implemented. Only one synclet managed" ) ;
		DisplayLog( "MFSL ASYNC LOAD PARAMETER: Parameter Nb_Synclet = %s will be ignored", key_value ) ;
		//pparam->nb_synclet = atoi( key_value ) ;
		pparam->nb_synclet = 1 ;
	}
      else if( !strcasecmp( key_name, "Async_Window_sec" ) )
	{
		pparam->async_window_sec = atoi( key_value ) ; /* Asynchronous Task Dispatcher sleeping time */
	}
      else if( !strcasecmp( key_name, "Async_Window_usec" ) )
	{
		pparam->async_window_usec = atoi( key_value ) ; /* Asynchronous Task Dispatcher sleeping time */
	}
      else if( !strcasecmp( key_name, "Nb_Sync_Before_GC" ) )
       {
          pparam->nb_before_gc = atoi( key_value ) ;
       }
      else if( !strcasecmp( key_name, "PreCreatedObject_Directory" ) )
       {
          strncpy( pparam->pre_create_obj_dir, key_value, MAXPATHLEN ) ;
       }
      else if( !strcasecmp( key_name, "Nb_PreCreated_Directories" ) ) 
       {
          pparam->nb_pre_create_dirs = atoi( key_value ) ;
       }
      else if( !strcasecmp( key_name, "Nb_PreCreated_Files" ) ) 
       {
          pparam->nb_pre_create_files = atoi( key_value ) ;
       }
      else if( !strcasecmp( key_name, "LRU_Prealloc_PoolSize" ) )
        {
          pparam->lru_param.nb_entry_prealloc = atoi( key_value ) ;
        }
      else if( !strcasecmp( key_name, "LRU_Nb_Call_Gc_invalid" ) )
        {
          pparam->lru_param.nb_call_gc_invalid = atoi( key_value ) ;
        }
      else if ( !strcasecmp( key_name, "DebugLevel" ) )
        {
          DebugLevel = ReturnLevelAscii( key_value );

          if ( DebugLevel == -1 )
          {
            DisplayLog("cache_content_read_conf: ERROR: Invalid debug level name: \"%s\".",
                  key_value );
            MFSL_return( ERR_FSAL_INVAL, 0 );
          }
        }
      else if ( !strcasecmp( key_name, "LogFile" ) )
        {
          LogFile = key_value ;
        }

      else 
       {
	  DisplayLog( "MFSL ASYNC LOAD PARAMETER: Unknown or unsettable key %s from section \"%s\" of configuration file.",
                      key_name, CONF_LABEL_MFSL_ASYNC );
          MFSL_return( ERR_FSAL_INVAL, 0 );
       }

    } /* for */

  if ( LogFile )
    {
      desc_log_stream_t  log_stream;

      strcpy( log_stream.path , LogFile );

      /* Default : NIV_CRIT */

      if ( DebugLevel == -1 )
        AddLogStreamJd( &log_outputs,
            V_FILE, log_stream, NIV_CRIT, SUP);
      else         
        AddLogStreamJd( &log_outputs,
            V_FILE, log_stream, DebugLevel, SUP);

    }

  MFSL_return( ERR_FSAL_NO_ERROR, 0 );
} /* MFSL_load_parameter_from_conf */

#endif /* ! _USE_SWIG */


