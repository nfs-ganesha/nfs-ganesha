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
 * \file    nfs_ip_stats.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:49:12 $
 * \version $Revision: 1.4 $
 * \brief   The management of the IP stats per machines.
 *
 * nfs_ip_stats.c : The management of the IP stats per machines.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_ip_stats.c,v 1.4 2006/01/24 13:49:12 leibovic Exp $
 *
 * $Log: nfs_ip_stats.c,v $
 * Revision 1.4  2006/01/24 13:49:12  leibovic
 * Adding missing includes.
 *
 * Revision 1.3  2006/01/20 07:39:23  leibovic
 * Back to the previous version.
 *
 * Revision 1.1  2005/11/30 09:28:42  deniel
 * IP/stats cache per thread (no bottleneck) is complete
 *
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "HashData.h"
#include "HashTable.h"
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include "nfs_ip_stats.h"
#include <stdlib.h>
#include <string.h>

/* Hashtable used to cache stats, accessed by their IP addess */
extern nfs_parameter_t    nfs_param ;



/**
 *
 *  ip_stats_rbt_hash_func: computes the hash value for the entry in IP stats cache.
 * 
 * Computes the hash value for the entry in IP stats cache. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long int ip_stats_value_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  return (unsigned long int)(buffclef->pdata) % p_hparam->index_size ;
} /*  ip_stats_value_hash_func */


/**
 *
 *  ip_stats_rbt_hash_func: computes the rbt value for the entry in IP stats cache.
 * 
 * Computes the rbt value for the entry in IP stats cache. In fact, it just use the address value
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long int ip_stats_rbt_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  /* We use the Xid as the rbt value */
  return (unsigned long int)(buffclef->pdata) ;
} /* ip_stats_rbt_hash_func */

/**
 *
 * compare_ip_stats: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_ip_stats(  hash_buffer_t * buff1, hash_buffer_t * buff2 )
{
  unsigned long int xid1 = (unsigned long int)(buff1->pdata) ;
  unsigned long int xid2 = (unsigned long int)(buff2->pdata) ;
  return (xid1 == xid2)?0:1 ;
} /* compare_xid */


/**
 *
 * display_ip_stats: displays the ip_stats stored in the buffer.
 *
 * displays the ip_stats stored in the buffer. This function is to be used as 'key_to_str' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_ip_stats( hash_buffer_t * pbuff, char * str ) 
{
  unsigned long int ip_stats  = (unsigned long int)(pbuff->pdata) ;
  
  return sprintf( str, "%x : %u.%u.%u.%u", 
                  (unsigned int)ip_stats, 
                  ( (unsigned int)ip_stats & 0xFF000000 ) >> 24, 
                  ( (unsigned int)ip_stats & 0x00FF0000 ) >> 16,
                  ( (unsigned int)ip_stats & 0x0000FF00 ) >> 8,
                  ( (unsigned int)ip_stats & 0x000000FF ) ) ;
} /* display_ip_stats */


/**
 *
 * nfs_ip_stats_add: adds an entry in the duplicate requests cache.
 *
 * Adds an entry in the duplicate requests cache.
 *
 * @param ipaddr           [IN]    the ipaddr to be used as key
 * @param nfs_ip_stats_pool [INOUT] values pool for hash table
 *
 * @return IP_STATS_SUCCESS if successfull\n.
 * @return IP_STATS_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return IP_STATS_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */

int nfs_ip_stats_add(  hash_table_t   * ht_ip_stats,
                       unsigned         int ipaddr,
                       nfs_ip_stats_t * nfs_ip_stats_pool )

{
  hash_buffer_t       buffkey  ;
  hash_buffer_t       buffdata ;
  nfs_ip_stats_t     * pnfs_ip_stats  = NULL ;
  unsigned long int    local_ipaddr = ipaddr ;

  /* Do nothing if configuration disables IP_Stats */ 
  if( nfs_param.core_param.dump_stats_per_client == 0 ) 
	return IP_STATS_SUCCESS ;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "nfs_ip_stats_t" ) ;
#endif

  /* Entry to be cached */
  GET_PREALLOC( pnfs_ip_stats, 
                nfs_ip_stats_pool, 
                nfs_param.worker_param.nb_ip_stats_prealloc, 
                nfs_ip_stats_t, 
                next_alloc ) ;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel( "N/A" ) ;
#endif

  if( pnfs_ip_stats == NULL ) 
    return IP_STATS_INSERT_MALLOC_ERROR ;
  
  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t)local_ipaddr ; 
  buffkey.len = 0 ;


  /* I build the data with the request pointer that should be in state 'IN USE' */
  pnfs_ip_stats->nb_call     = 0 ;
  pnfs_ip_stats->nb_req_nfs2 = 0 ;
  pnfs_ip_stats->nb_req_nfs3 = 0 ;
  pnfs_ip_stats->nb_req_nfs4 = 0 ;
  pnfs_ip_stats->nb_req_mnt1 = 0 ;
  pnfs_ip_stats->nb_req_mnt3 = 0 ;

  memset( pnfs_ip_stats->req_mnt1, 0, MNT_V1_NB_COMMAND*sizeof( int ) ) ;
  memset( pnfs_ip_stats->req_mnt3, 0, MNT_V3_NB_COMMAND*sizeof( int ) ) ;
  memset( pnfs_ip_stats->req_nfs2, 0, NFS_V2_NB_COMMAND*sizeof( int ) ) ;
  memset( pnfs_ip_stats->req_nfs3, 0, NFS_V3_NB_COMMAND*sizeof( int ) ) ;

  buffdata.pdata = (caddr_t)pnfs_ip_stats ;
  buffdata.len = sizeof( nfs_ip_stats_t ) ;
  
  if( HashTable_Set( ht_ip_stats, &buffkey, &buffdata ) != HASHTABLE_SUCCESS )
    return IP_STATS_INSERT_MALLOC_ERROR ;
  
  return IP_STATS_SUCCESS ; 
} /* nfs_ip_stats_add */

/**
 *
 * nfs_ip_stats_incr: increments the stats value.
 *
 * increments the stats value.
 * 
 * @param ipaddr   [IN]  the ip address requested
 *
 * @return the computed number of call for this ipaddr. Negative value is a failure
 *
 */
int  nfs_ip_stats_incr( hash_table_t   * ht_ip_stats, 
                        unsigned int     ipaddr, 
                        unsigned int     nfs_prog,
                        unsigned int     mnt_prog,
                        struct svc_req * ptr_req )
{
  hash_buffer_t         buffkey ;
  hash_buffer_t         buffval ;
  int                   status ;
  unsigned long int     local_ipaddr = ipaddr ; 
  nfs_ip_stats_t      * pnfs_ip_stats ;
  
  buffkey.pdata = (caddr_t)local_ipaddr ;
  buffkey.len = 0 ;
  
  /* Do nothing if configuration disables IP_Stats */ 
  if( nfs_param.core_param.dump_stats_per_client == 0 ) 
	return IP_STATS_SUCCESS ;

  if( HashTable_Get( ht_ip_stats, &buffkey, &buffval ) == HASHTABLE_SUCCESS )
    {
      pnfs_ip_stats = (nfs_ip_stats_t *)buffval.pdata ;
      pnfs_ip_stats->nb_call += 1 ;

      status = IP_STATS_SUCCESS ;

      if( ptr_req->rq_prog == nfs_prog )
        {
          switch( ptr_req->rq_vers )
            {
            case NFS_V2:
              pnfs_ip_stats->nb_req_nfs2 +=1 ;
              pnfs_ip_stats->req_nfs2[ptr_req->rq_proc] += 1 ;
              break ;
              
             case NFS_V3:
              pnfs_ip_stats->nb_req_nfs3 +=1 ;
              pnfs_ip_stats->req_nfs3[ptr_req->rq_proc] += 1 ;
              break ;
              
            case NFS_V4:
              pnfs_ip_stats->nb_req_nfs4 += 1 ;
              break ;
            }
        }
      else if(  ptr_req->rq_prog == mnt_prog )
        {
           switch( ptr_req->rq_vers )
            {
            case MOUNT_V1:
              pnfs_ip_stats->nb_req_mnt1 +=1 ;
              pnfs_ip_stats->req_mnt1[ptr_req->rq_proc] += 1 ;
              break ;
              
            case MOUNT_V3:
              pnfs_ip_stats->nb_req_mnt3 +=1 ;
              pnfs_ip_stats->req_mnt3[ptr_req->rq_proc] += 1 ;
              break ;
            }
        }
    }
  else
    {
      status = IP_STATS_NOT_FOUND ;
    }
  return status ;
} /* nfs_ip_stats_incr */

/**
 *
 * nfs_ip_stats_get: gets the stats value.
 *
 * gets the stats value.
 * 
 * @param ipaddr   [IN]  the ip address requested
 *
 * @return the computed number of call for this ipaddr. Negative value is a failure
 *
 */
int  nfs_ip_stats_get( hash_table_t     * ht_ip_stats, 
                       unsigned int       ipaddr, 
                       nfs_ip_stats_t  ** pnfs_ip_stats )
{
  hash_buffer_t        buffkey ;
  hash_buffer_t        buffval ;
  unsigned long int    local_ipaddr = ipaddr ;
  int                  status ;
  
  buffkey.pdata = (caddr_t)local_ipaddr ;
  buffkey.len = 0 ;
  
  /* Do nothing if configuration disables IP_Stats */ 
  if( nfs_param.core_param.dump_stats_per_client == 0 ) 
	return IP_STATS_SUCCESS ;

  if( HashTable_Get( ht_ip_stats, &buffkey, &buffval ) == HASHTABLE_SUCCESS )
    {
      *pnfs_ip_stats = (nfs_ip_stats_t *)buffval.pdata ;

      status =  IP_STATS_SUCCESS ;
    }
  else
    {
      status = IP_STATS_NOT_FOUND ;
    }
  return status ;
} /* nfs_ip_stats_get */



/**
 *
 * nfs_ip_stats_remove: Tries to remove an entry for ip_stats cache
 *
 * Tries to remove an entry for ip_stats cache.
 * 
 * @param ipaddr           [IN]    the ip address to be uncached.
 * @param nfs_ip_stats_pool [INOUT] values pool for hash table
 *
 * @return the result previously set if *pstatus == IP_STATS_SUCCESS
 *
 */
int nfs_ip_stats_remove( hash_table_t   * ht_ip_stats, 
                         int              ipaddr, 
                         nfs_ip_stats_t * nfs_ip_stats_pool )
{
  hash_buffer_t        buffkey , old_value ;
  int                  status = IP_STATS_SUCCESS;
  nfs_ip_stats_t     * pnfs_ip_stats  = NULL ;
  unsigned long int   long_ipaddr ; 
 
  long_ipaddr = (unsigned long int)ipaddr ;

  buffkey.pdata = (caddr_t)long_ipaddr ; 
  buffkey.len = 0 ;

  /* Do nothing if configuration disables IP_Stats */ 
  if( nfs_param.core_param.dump_stats_per_client == 0 ) 
	return IP_STATS_SUCCESS ;

   if( HashTable_Del( ht_ip_stats, &buffkey, NULL, &old_value ) == HASHTABLE_SUCCESS )
    {
      pnfs_ip_stats = (nfs_ip_stats_t *)old_value.pdata ;
      RELEASE_PREALLOC( pnfs_ip_stats, nfs_ip_stats_pool, next_alloc ) ;
    }
  else
    {
      status = IP_STATS_NOT_FOUND ;
    }

   return status ;
} /* nfs_ip_stats_remove */



/**
 *
 * nfs_Init_ip_stats: Init the hashtable for IP stats cache.
 *
 * Perform all the required initialization for hashtable IP stats cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
hash_table_t * nfs_Init_ip_stats( nfs_ip_stats_parameter_t  param ) 
{ 
  hash_table_t * ht_ip_stats ;
  
  if( ( ht_ip_stats = HashTable_Init( param.hash_param  ) ) == NULL )
    {
      DisplayLog( "NFS IP_STATS: Cannot init IP stats cache" ) ;
      return NULL ;
    }

  return  ht_ip_stats ;
} /* nfs_Init_ip_stats */


/**
 *
 * nfs_ip_stats_dump: Dumps the IP Stats for each client to a file per client
 *
 * @param ht_ip_stats [IN] hash table to be dumped
 * @param path_stat   [IN] pattern used to build path used for dumping stats
 *
 * @return nothing (void function).
 *
 */
void nfs_ip_stats_dump( hash_table_t   ** ht_ip_stats,
		        unsigned int      nb_worker, 
			char            * path_stat)
{
  struct rbt_node    * it;
  struct rbt_head    * tete_rbt ;
  hash_data_t        * pdata = NULL ;
  unsigned int         i = 0 ;
  unsigned int         j = 0 ;
  unsigned int         k = 0 ;
  nfs_ip_stats_t     * pnfs_ip_stats[NB_MAX_WORKER_THREAD] ;
  nfs_ip_stats_t       ip_stats_aggreg ;
  char                 ifpathdump[MAXPATHLEN] ;
  unsigned long int    ipaddr ; 
  time_t               current_time ;
  struct tm            current_time_struct ;
  char                 strdate[1024] ;
  FILE               * flushipstat = NULL ; 

  /* Do nothing if configuration disables IP_Stats */ 
  if( nfs_param.core_param.dump_stats_per_client == 0 ) 
	return  ;

  /* Compute the current time */
  current_time = time( NULL ) ;
  memcpy( &current_time_struct, localtime( &current_time ), sizeof( current_time_struct ) ) ;
  snprintf( strdate, 1024, "%u, %.2d/%.2d/%.4d %.2d:%.2d:%.2d ",
            (unsigned int)current_time,
            current_time_struct.tm_mday,
            current_time_struct.tm_mon+1,
            1900 + current_time_struct.tm_year,
            current_time_struct.tm_hour,
            current_time_struct.tm_min,
            current_time_struct.tm_sec ) ;

  /* All clients are supposed to have call at least one time worker #0 
   * we loop on every client in the HashTable */
  for( i = 0 ; i < ht_ip_stats[0]->parameter.index_size ; i++ )
   {
      tete_rbt = &((ht_ip_stats[0]->array_rbt)[i]) ;
      RBT_LOOP( tete_rbt, it )
        {
          pdata = (hash_data_t *)it->rbt_opaq ;

          ipaddr =  (unsigned long int)pdata->buffkey.pdata ;

          snprintf( ifpathdump, 
		    MAXPATHLEN, 
		    "%s/stats_nfs-0x%x=%u.%u.%u.%u", 
		    path_stat, 
	            ntohl( ipaddr ),
                    ( ntohl( ipaddr ) & 0xFF000000 ) >> 24,
                    ( ntohl( ipaddr ) & 0x00FF0000 ) >> 16,
                    ( ntohl( ipaddr ) & 0x0000FF00 ) >> 8,
                    ( ntohl( ipaddr ) & 0x000000FF ) ) ;

	 if( ( flushipstat = fopen( ifpathdump, "a" ) ) == NULL )
	   return ;

	 /* Collect stats for each worker and aggregate them*/
         memset( &ip_stats_aggreg, 0, sizeof( ip_stats_aggreg ) ) ;
         for( j = 0 ; j < nb_worker ; j++ )  
	   {
             if( nfs_ip_stats_get( ht_ip_stats[j], 
                                   ipaddr, 
                                   &pnfs_ip_stats[j] ) != IP_STATS_SUCCESS )
             {
                fclose( flushipstat ) ;
        	return ;
             }
             ip_stats_aggreg.nb_call += (pnfs_ip_stats[j])->nb_call ;

	     ip_stats_aggreg.nb_req_nfs2 += (pnfs_ip_stats[j])->nb_req_nfs2 ;
	     ip_stats_aggreg.nb_req_nfs3 += (pnfs_ip_stats[j])->nb_req_nfs3 ;
	     ip_stats_aggreg.nb_req_nfs4 += (pnfs_ip_stats[j])->nb_req_nfs4 ;
             ip_stats_aggreg.nb_req_mnt1 += (pnfs_ip_stats[j])->nb_req_mnt1 ;
             ip_stats_aggreg.nb_req_mnt3 += (pnfs_ip_stats[j])->nb_req_mnt3  ;

	     for( k = 0 ; k < MNT_V1_NB_COMMAND ; k++ ) 
		ip_stats_aggreg.req_mnt1[k] += (pnfs_ip_stats[j])->req_mnt1[k] ;

	     for( k = 0 ; k < MNT_V3_NB_COMMAND ; k++ )
		ip_stats_aggreg.req_mnt3[k] += (pnfs_ip_stats[j])->req_mnt3[k] ;

	      for( k = 0 ; k < NFS_V2_NB_COMMAND ; k++ )
		ip_stats_aggreg.req_nfs2[k] += (pnfs_ip_stats[j])->req_nfs2[k] ;

	      for( k = 0 ; k < NFS_V3_NB_COMMAND ; k++ )
		ip_stats_aggreg.req_nfs3[k] += (pnfs_ip_stats[j])->req_nfs3[k] ;
	   }

 	  /* Write stats to file */
          fprintf( flushipstat, "NFS/MOUNT STATISTICS,%s;%u|%u,%u,%u,%u,%u\n",
		   strdate,
		   ip_stats_aggreg.nb_call,
                   ip_stats_aggreg.nb_req_mnt1,
                   ip_stats_aggreg.nb_req_mnt3,
                   ip_stats_aggreg.nb_req_nfs2,
                   ip_stats_aggreg.nb_req_nfs3,
                   ip_stats_aggreg.nb_req_nfs4 ) ;
                   
	 fprintf( flushipstat, "MNT V1 REQUEST,%s;%u|", strdate, ip_stats_aggreg.nb_req_mnt1 ) ;
      	 for( k = 0 ; k < MNT_V1_NB_COMMAND-1 ; k++ )
	   fprintf( flushipstat, "%u," , ip_stats_aggreg.req_mnt1[k] ) ;
	 fprintf( flushipstat, "%u\n" , ip_stats_aggreg.req_mnt1[MNT_V1_NB_COMMAND-1] );
                   
	 fprintf( flushipstat, "MNT V3 REQUEST,%s;%u|", strdate, ip_stats_aggreg.nb_req_mnt3 ) ;
      	 for( k = 0 ; k < MNT_V3_NB_COMMAND-1 ; k++ )
	   fprintf( flushipstat, "%u," , ip_stats_aggreg.req_mnt3[k] );
	 fprintf( flushipstat, "%u\n" , ip_stats_aggreg.req_mnt3[MNT_V3_NB_COMMAND-1] );
          
	 fprintf( flushipstat, "NFS V2 REQUEST,%s;%u|", strdate, ip_stats_aggreg.nb_req_nfs2 ) ;
      	 for( k = 0 ; k < NFS_V2_NB_COMMAND-1 ; k++ )
	   fprintf( flushipstat, "%u," , ip_stats_aggreg.req_nfs2[k] ) ;
	 fprintf( flushipstat, "%u\n" , ip_stats_aggreg.req_nfs2[NFS_V2_NB_COMMAND-1] );
                  
	 fprintf( flushipstat, "NFS V3 REQUEST,%s;%u|", strdate, ip_stats_aggreg.nb_req_nfs3 ) ;
      	 for( k = 0 ; k < NFS_V3_NB_COMMAND-1 ; k++ )
	   fprintf( flushipstat, "%u," , ip_stats_aggreg.req_nfs3[k] ) ;
	 fprintf( flushipstat, "%u\n" , ip_stats_aggreg.req_nfs3[NFS_V3_NB_COMMAND-1] );

	 fprintf( flushipstat, "END, ----- NO MORE STATS FOR THIS PASS ----\n" ) ;

	 fflush( flushipstat ) ;

	 /* Check next client */
         RBT_INCREMENT(it);
       }

       fclose( flushipstat ) ;
    }
} /* nfs_ip_stats_dump */
