/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    cache_inode_rdwr.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Removes an entry of any type.
 *
 * cache_inode_rdwr.c : performs an IO on a REGULAR_FILE.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */


#include "fsal.h"

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_rdwr: Reads/Writes through the cache layer.
 *
 * Reads/Writes through the cache layer.
 *
 * @param pentry [IN] entry in cache inode layer whose content is to be accessed.
 * @param read_or_write [IN] a flag of type cache_content_io_direction_t to tell if a read or write is to be done. 
 * @param seek_descriptor [IN] absolute position (in the FSAL file) where the IO will be done.
 * @param buffer_size [IN] size of the buffer pointed by parameter 'buffer'. 
 * @param pio_size [OUT] the size of the io that was successfully made.
 * @param pfsal_attr [OUT] the FSAL attributes after the operation.
 * @param buffer write:[IN] read:[OUT] the buffer for the data.
 * @param ht [INOUT] the hashtable used for managing the cache. 
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext [IN] fsal context for the operation.
 * @param stable[IN] if FALSE, data will be written to unstable storage (for implementing write/commit)
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZEOMEU; gestion de la taille du fichier a prendre en compte.
 *
 */

cache_inode_status_t cache_inode_rdwr( cache_entry_t              * pentry, 
                                       cache_inode_io_direction_t   read_or_write,
                                       fsal_seek_t                * seek_descriptor,
                                       fsal_size_t                  buffer_size,  
                                       fsal_size_t                * pio_size,
                                       fsal_attrib_list_t         * pfsal_attr,
                                       caddr_t                      buffer,    
                                       fsal_boolean_t             * p_fsal_eof,
                                       hash_table_t               * ht,  
                                       cache_inode_client_t       * pclient, 
                                       fsal_op_context_t          * pcontext, 
                                       bool_t                       stable, 
                                       cache_inode_status_t       * pstatus )
{
  int                          statindex = 0 ;
  cache_content_io_direction_t io_direction ;
  cache_content_status_t       cache_content_status ;
  fsal_status_t                fsal_status ;
  fsal_openflags_t             openflags ;
  fsal_size_t                  io_size ;
  fsal_attrib_list_t           post_write_attr; 
  fsal_status_t                fsal_status_getattr ;
  struct stat                  buffstat ;
  bool_t                       stable_flag = stable ;
 
  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS ;

  /* For now, only FSAL_SEEK_SET is supported */
  if( seek_descriptor->whence != FSAL_SEEK_SET )
    {
      DisplayLogJd( pclient->log_outputs, 
                    "Implementation trouble: seek_descriptor was not a 'FSAL_SEEK_SET' cursor" ) ;
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
    }
  
                    
  io_size = buffer_size ;

#ifdef _DEBUG_CACHE_INODE
  DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG, 
                     "---> INODE : IO Size = %llu fdsize =%d seeksize=%d",
                     buffer_size , sizeof( fsal_file_t ), sizeof( fsal_seek_t ) ) ;
#endif

  /* stat */
  pclient->stat.nb_call_total += 1 ;
  if( read_or_write == CACHE_INODE_READ )
    {
      statindex    = CACHE_INODE_READ_DATA;
      io_direction = CACHE_CONTENT_READ ;
      openflags    = FSAL_O_RDONLY ;
      pclient->stat.func_stats.nb_call[CACHE_INODE_READ_DATA] += 1 ;
    }
  else
    {
      statindex    = CACHE_INODE_WRITE_DATA ;
      io_direction = CACHE_CONTENT_WRITE ;
      openflags    = FSAL_O_WRONLY ;
      pclient->stat.func_stats.nb_call[CACHE_INODE_WRITE_DATA] += 1 ;
    }

   P_w( &pentry->lock ) ;
  
   /* IO are done only on REGULAR_FILEs */
   if( pentry->internal_md.type != REGULAR_FILE )
    {
      *pstatus = CACHE_INODE_BAD_TYPE ;
      V_w( &pentry->lock ) ;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
      
      return *pstatus ;
    }

   /* Non absolute address within the file are not supported (we act only like pread/pwrite) */
   if( seek_descriptor->whence != FSAL_SEEK_SET )
     {
       *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
       V_w( &pentry->lock ) ;
       
       /* stats */
       pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
       
       return *pstatus ;
     }

   /* Do we use stable or unstable storage ? */
   if( stable_flag == FALSE )
    {
      /* Data will be stored in memory and not flush to FSAL */

      /* If the unstable_data buffer allocated ? */
      if( pentry->object.file.unstable_data.buffer == NULL )
       {
          if( ( pentry->object.file.unstable_data.buffer = Mem_Alloc( CACHE_INODE_UNSTABLE_BUFFERSIZE ) ) == NULL )
           {
             *pstatus = CACHE_INODE_MALLOC_ERROR ;
             V_w( &pentry->lock ) ;
       
             /* stats */
             pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
       
              return *pstatus ;
           }

          pentry->object.file.unstable_data.offset = seek_descriptor->offset ;
          pentry->object.file.unstable_data.length = buffer_size ;

          memcpy(  pentry->object.file.unstable_data.buffer, buffer, buffer_size ) ;

          /* Set mtime and ctime */
          pentry->object.file.attributes.mtime.seconds  = time( NULL ) ;
          pentry->object.file.attributes.mtime.nseconds = 0 ;
       
          /* BUGAZOMEU : write operation must NOT modify file's ctime */
          pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;
       
       } /* if( pentry->object.file.unstable_data.buffer == NULL ) */
      else
       {
          if( ( pentry->object.file.unstable_data.offset < seek_descriptor->offset ) && 
              ( buffer_size + seek_descriptor->offset < CACHE_INODE_UNSTABLE_BUFFERSIZE ) )
            {
		pentry->object.file.unstable_data.length = buffer_size + seek_descriptor->offset ;
                memcpy( (char *)( pentry->object.file.unstable_data.buffer + seek_descriptor->offset), buffer, buffer_size ) ;

                /* Set mtime and ctime */
                pentry->object.file.attributes.mtime.seconds  = time( NULL ) ;
                pentry->object.file.attributes.mtime.nseconds = 0 ;
       
                /* BUGAZOMEU : write operation must NOT modify file's ctime */
                pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;
            }
	  else
            {
		/* Go back to regular situation */
		stable_flag = TRUE ;
            }
       }

    } /* if( stable_flag == FALSE ) */

   if( stable_flag == TRUE )
    {
      /* Calls file content cache to operate on the cache */
      if( pentry->object.file.pentry_content != NULL )
        {
          /* Entry is data cached */
          cache_content_rdwr( pentry->object.file.pentry_content, 
                              io_direction,
                              seek_descriptor, 
                              &io_size,
                              pio_size,
                              buffer, 
                              p_fsal_eof,
  		              &buffstat,
                              (cache_content_client_t *)pclient->pcontent_client, 
                              pcontext, 
                              &cache_content_status ) ;

          /* If the entry under resync */
          if( cache_content_status == CACHE_CONTENT_LOCAL_CACHE_NOT_FOUND )
            {
	      /* Data cache gc has removed this entry */
             if(  cache_content_new_entry( pentry,
                                           NULL,
                                           (cache_content_client_t *)pclient->pcontent_client,
                                            RENEW_ENTRY,
                                            pcontext,
                                            &cache_content_status ) == NULL )
               { 
	          /* Entry could not be recoverd, cache_content_status contains an error, let it be managed by the next block */
	          DisplayLogJdLevel( pclient->log_outputs, NIV_CRIT,
                                    "Read/Write Operation through cache failed with status %d (renew process failed)",	
		       	       	     cache_content_status ) ;

	       } 
   	     else
	       {
	 	/* Entry was successfully renewed */
		DisplayLog( "----> File Content Entry %p was successfully renewed", pentry ) ;

                /* Try to access the content of the file again */
                cache_content_rdwr( pentry->object.file.pentry_content,
                                    io_direction,
                                    seek_descriptor,
                                    &io_size,
                                    pio_size,
                                    buffer,
                                    p_fsal_eof,
                                    &buffstat,
                                    (cache_content_client_t *)pclient->pcontent_client,
                                    pcontext,
                                    &cache_content_status ) ;

                /* No management of cache_content_status in case of failure, this will be done 
                 * within the next block */
	       }
		
             }


           if( cache_content_status != CACHE_CONTENT_SUCCESS ) 
             {
               *pstatus = cache_content_error_convert( cache_content_status ) ;

               V_w( &pentry->lock ) ;
              
               DisplayLogJdLevel( pclient->log_outputs, NIV_CRIT,
                                  "Read/Write Operation through cache failed with status %d",cache_content_status ) ;
         
           
               /* stats */
               pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
           
               return *pstatus ;
             }
#ifdef _DEBUG_CACHE_INODE    
           DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG,
                              "inode/dc: io_size=%llu, pio_size=%llu,  eof=%d, seek=%d.%llu", 
                              io_size, *pio_size, *p_fsal_eof, seek_descriptor->whence, seek_descriptor->offset ) ;
  
           DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG,
                              "---> INODE  AFTER : IO Size = %llu %llu", io_size, *pio_size ) ;
#endif
     
      	   /* Use information from the buffstat to update the file metadata */
	   pentry->object.file.attributes.filesize = buffstat.st_size ;
	   pentry->object.file.attributes.spaceused = buffstat.st_blksize * buffstat.st_blocks ;  

        }
      else
        {
          /* No data cache entry, we operated directly on FSAL */
          pentry->object.file.attributes.asked_attributes =  pclient->attrmask ;

          /* Open the file if needed */
          if( pentry->object.file.open_fd.fileno == -1 ) 
            {
              if( cache_inode_open( pentry, 
	      		           pclient, 
                                   openflags,
			           pcontext,
                                   pstatus ) != CACHE_INODE_SUCCESS ) 
               {
                 V_w( &pentry->lock ) ;
           
                 /* stats */
                 pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
           
                 return *pstatus ;
               } 
            }

          rw_lock_downgrade( &pentry->lock ) ;

          /* Call FSAL_read or FSAL_write */

          switch( read_or_write )
            {
            case CACHE_INODE_READ:
#ifdef _USE_MFSL
              fsal_status = MFSL_read( &(pentry->object.file.open_fd.fd), 
                                       seek_descriptor, 
                                       io_size, 
                                       buffer, 
                                       pio_size, 
                                       p_fsal_eof,
				       &pclient->mfsl_context ) ;
#else
              fsal_status = FSAL_read( &(pentry->object.file.open_fd.fd), 
                                       seek_descriptor, 
                                       io_size, 
                                       buffer, 
                                       pio_size, 
                                       p_fsal_eof ) ;
#endif
              break ;
           
            case CACHE_INODE_WRITE:
#ifdef _USE_MFSL
              fsal_status = MFSL_write( &(pentry->object.file.open_fd.fd), 
                                        seek_descriptor, 
                                        io_size, 
                                        buffer, 
                                        pio_size,
				        &pclient->mfsl_context ) ;
#else
              fsal_status = FSAL_write( &(pentry->object.file.open_fd.fd), 
                                        seek_descriptor, 
                                        io_size, 
                                        buffer, 
                                        pio_size ) ;
#endif
 
              break ;
            }


          V_r( &pentry->lock ) ;
#ifdef _DEBUG_FSAL 
          DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "FSAL IO operation returned %d, asked_size=%llu, effective_size=%llu", fsal_status.major, (unsigned long long)io_size, (unsigned long long)*pio_size ) ;
#endif
          P_w( &pentry->lock ) ;

          if( FSAL_IS_ERROR( fsal_status ) )
            {

              if( fsal_status.major == ERR_FSAL_DELAY )
                DisplayLogJd( pclient->log_outputs, "FSAL_write returned EBUSY" ) ;
              else
                DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "cache_inode_rdwr: fsal_status.major = %d", fsal_status.major ) ;

             if ( (fsal_status.major != ERR_FSAL_NOT_OPENED) && (pentry->object.file.open_fd.fileno != -1) )
             {
#ifdef _DEBUG_CACHE_INODE
                printf("cache_inode_rdwr: CLOSING pentry %p: fd=%d\n", pentry, pentry->object.file.open_fd.fileno);
#endif

#ifdef _USE_MFSL 
                MFSL_close( &(pentry->object.file.open_fd.fd), &pclient->mfsl_context ) ;
#else
                FSAL_close( &(pentry->object.file.open_fd.fd) ) ;
#endif

              *pstatus = cache_inode_error_convert( fsal_status ) ;
             }
             else
             {
                /* the fd has been close by another thread.
                 * return CACHE_INODE_FSAL_DELAY so the client will
                 * retry with a new fd.
                 */
                *pstatus = CACHE_INODE_FSAL_DELAY;
             }

             pentry->object.file.open_fd.last_op = 0 ;
             pentry->object.file.open_fd.fileno = -1 ;
           
             V_w( &pentry->lock ) ;
          
             /* stats */
             pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
          
             return *pstatus ;
           } 

                                     
#ifndef _DEBUG_CACHE_INODE
          DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG, 
                             "inode/direct: io_size=%llu, pio_size=%llu, eof=%d, seek=%d.%llu", 
                             io_size, *pio_size, *p_fsal_eof, seek_descriptor->whence, seek_descriptor->offset ) ;
#endif
        
          if( cache_inode_close( pentry, pclient, pstatus ) != CACHE_INODE_SUCCESS )  
            {
              DisplayLogJd( pclient->log_outputs, "cache_inode_rdwr: cache_inode_close = %d", *pstatus ) ;

              V_w( &pentry->lock ) ;
           
              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[statindex] += 1 ;
           
              return *pstatus ;
            } 

          if( read_or_write == CACHE_INODE_WRITE ) 
             {
                /* Do a getattr in order to have update information on filesize 
                 * This query is done directly on FSAL (object is not data cached), and result
                 * will be propagated to cache Inode */
              
                /* WARNING: This operation is to be done AFTER FSAL_close (some FSAL, like POSIX, 
                 * may not flush data until the file is closed */

   	        /*post_write_attr.asked_attributes =  pclient->attrmask ; */
	        post_write_attr.asked_attributes =  FSAL_ATTR_SIZE | FSAL_ATTR_SPACEUSED ;
	        fsal_status_getattr = FSAL_getattrs(  &(pentry->object.file.handle), pcontext, &post_write_attr ) ;

	        /* if failed, the next block will handle the error */
	        if( FSAL_IS_ERROR( fsal_status_getattr ) ) 
	          fsal_status = fsal_status_getattr ;
	       else
	          {
		    /* Update Cache Inode attributes */
		    pentry->object.file.attributes.filesize = post_write_attr.filesize ;
		    pentry->object.file.attributes.spaceused = post_write_attr.spaceused ;
	          }
	     }

        }
   
      /* IO was successfull (through cache content or not), we manually update the times in the attributes */

      switch( read_or_write ) 
       {
        case CACHE_INODE_READ:
          /* Set the atime */
          pentry->object.file.attributes.atime.seconds  = time( NULL ) ;
          pentry->object.file.attributes.atime.nseconds = 0 ;
          break ;
       
        case CACHE_INODE_WRITE:
          /* Set mtime and ctime */
          pentry->object.file.attributes.mtime.seconds  = time( NULL ) ;
          pentry->object.file.attributes.mtime.nseconds = 0 ;
       
          /* BUGAZOMEU : write operation must NOT modify file's ctime */
          pentry->object.file.attributes.ctime = pentry->object.file.attributes.mtime;
       
          break ;
        }
     }  /* if(stable_flag == TRUE ) */

   /* Return attributes to caller */ 
   if( pfsal_attr != NULL ) 
	*pfsal_attr = pentry->object.file.attributes ;

   *pstatus = CACHE_INODE_SUCCESS ;

   /* stat */
   if( read_or_write == CACHE_INODE_READ )
     {
       *pstatus =  cache_inode_valid( pentry, CACHE_INODE_OP_GET, pclient ) ;

       if( *pstatus != CACHE_INODE_SUCCESS )
         pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READ] += 1 ;
       else
         pclient->stat.func_stats.nb_success[CACHE_INODE_READ] += 1 ;
     }
   else
     {
       *pstatus =  cache_inode_valid( pentry, CACHE_INODE_OP_SET, pclient ) ;
       
       if( *pstatus != CACHE_INODE_SUCCESS )
         pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_WRITE] += 1 ;
       else
         pclient->stat.func_stats.nb_success[CACHE_INODE_WRITE] += 1 ;
     }
     
   V_w( &pentry->lock ) ;      
     
   return *pstatus ;
} /* cache_inode_rdwr */

/**
 *
 * cache_inode_commit: commits a write operation on unstable storage
 *
 * Reads/Writes through the cache layer.
 *
 * @param pentry [IN] entry in cache inode layer whose content is to be accessed.
 * @param read_or_write [IN] a flag of type cache_content_io_direction_t to tell if a read or write is to be done. 
 * @param seek_descriptor [IN] absolute position (in the FSAL file) where the IO will be done.
 * @param buffer_size [IN] size of the buffer pointed by parameter 'buffer'. 
 * @param pio_size [OUT] the size of the io that was successfully made.
 * @param pfsal_attr [OUT] the FSAL attributes after the operation.
 * @param buffer write:[IN] read:[OUT] the buffer for the data.
 * @param ht [INOUT] the hashtable used for managing the cache. 
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext [IN] fsal context for the operation.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZEOMEU; gestion de la taille du fichier a prendre en compte.
 *
 */

cache_inode_status_t cache_inode_commit( cache_entry_t              * pentry, 
                                         uint64_t                     offset,
                                         fsal_size_t                  count,  
                                         fsal_attrib_list_t         * pfsal_attr,
                                         hash_table_t               * ht,  
                                         cache_inode_client_t       * pclient, 
                                         fsal_op_context_t          * pcontext, 
                                         cache_inode_status_t       * pstatus )
{
  fsal_seek_t    seek_descriptor ;
  fsal_size_t    size_io_done ;  
  fsal_boolean_t eof ;


  if( pentry->object.file.unstable_data.buffer == NULL )
   {
     *pstatus = CACHE_INODE_SUCCESS ;
     return *pstatus ;
   }

  if( count == 0 )
   {
     /* Count = 0 means "flush all data to permanent storage */
     seek_descriptor.offset = pentry->object.file.unstable_data.offset ;
     seek_descriptor.whence = FSAL_SEEK_SET ;


    if( cache_inode_rdwr( pentry,
		          CACHE_INODE_WRITE,
			  &seek_descriptor,
			  pentry->object.file.unstable_data.length,
			  &size_io_done,
			  pfsal_attr,
			  pentry->object.file.unstable_data.buffer,
			  &eof,
			  ht,
			  pclient,
			  pcontext,
			  TRUE,
			  pstatus ) != CACHE_INODE_SUCCESS )
	return *pstatus ;
     
    P_w( &pentry->lock ) ; 

    Mem_Free( pentry->object.file.unstable_data.buffer ) ;
    pentry->object.file.unstable_data.buffer = NULL ;	      

    V_w( &pentry->lock ) ; 

   }
  else
   {
     if( offset < pentry->object.file.unstable_data.offset )
      {
         *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
         return *pstatus ;
      }

     seek_descriptor.offset = offset ;
     seek_descriptor.whence = FSAL_SEEK_SET ;


     return cache_inode_rdwr( pentry,
			      CACHE_INODE_WRITE,
			      &seek_descriptor,
			      count,
			      &size_io_done,
			      pfsal_attr,
			      (char *)(pentry->object.file.unstable_data.buffer + offset - pentry->object.file.unstable_data.offset ),
			      &eof,
			      ht,
			      pclient,
			      pcontext,
			      TRUE,
			      pstatus ) ;	  
   }

  /* Regulat exit */
  *pstatus = CACHE_INODE_SUCCESS ;
  return *pstatus ;
} /* cache_inode_commit */


 
