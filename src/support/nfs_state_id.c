/**
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
 *
 * nfs_state_id.c : The management of the state id cache.
 *
 * $Header$
 *
 * $Log$
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef _SOLARIS
#include "solaris_port.h"
#endif


#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>  /* for having isalnum */
#include <stdlib.h> /* for having atoi */
#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>  /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_functions.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h" 
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"


size_t strnlen(const char *s, size_t maxlen);


extern time_t   ServerBootTime ;
extern nfs_parameter_t nfs_param ;

hash_table_t * ht_state_id ;

char all_zero[] = "\0\0\0\0\0\0\0\0\0\0\0\0" ;
char all_one[12] ;

int display_state_id_key( hash_buffer_t * pbuff, char * str )
{
  unsigned int i   = 0 ;
  unsigned int len = 0 ;

  for( i = 0 ; i < 12 ; i++ )
     len += sprintf( &(str[i*2]), "%02x", (unsigned char)pbuff->pdata[i] ) ;
  return len ;
} /* display_state_id_val */

int display_state_id_val( hash_buffer_t * pbuff, char * str )
{
  cache_inode_state_t * pstate = (cache_inode_state_t *)(pbuff->pdata) ;

  return sprintf( str, "state %p is associated with pentry=%p type=%u my_id=%u seqid=%u prev=%p next=%p\n", 
                  pstate, pstate->pentry, pstate->state_type, pstate->my_id, pstate->seqid,
                  pstate->prev, pstate->next ) ;
} /* display_state_id_val */

int compare_state_id( hash_buffer_t * buff1, hash_buffer_t * buff2 )
{
   return memcmp( buff1->pdata, buff2->pdata, 12 ) ; /* The value 12 is fixed by RFC3530 */
} /* compare_state_id */

unsigned long state_id_value_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef )
{
  unsigned int  sum = 0 ;
  unsigned int  i = 0 ;
  unsigned char c ;

  /* Compute the sum of all the characters */
  for( i = 0 ; i < 12 ; i++ ) 
   { 
       c=((char *)buffclef->pdata)[i] ;
       sum += c ;
   }

#ifdef _DEBUG_STATES
  printf( "---> state_id_value_hash_func=%lu\n",(unsigned long)( sum % p_hparam->index_size ) ) ; 
#endif
  return (unsigned long)( sum % p_hparam->index_size ) ;
} /*  client_id_reverse_value_hash_func */

unsigned long state_id_rbt_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef )
{

  u_int32_t i1 = 0 ;
  u_int32_t i2 = 0 ;
  u_int32_t i3 = 0 ;
#ifdef _DEBUG_STATES
  unsigned int i  = 0 ;

   printf( "         ----- state_id_rbt_hash_func : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)buffclef->pdata[i] ) ;
   printf( "\n" ) ;
#endif

  memcpy( &i1, &(buffclef->pdata[0]), sizeof( u_int32_t ) ) ;
  memcpy( &i2, &(buffclef->pdata[4]), sizeof( u_int32_t ) ) ;
  memcpy( &i3, &(buffclef->pdata[8]), sizeof( u_int32_t ) ) ;
 
#ifdef _DEBUG_STATES 
  printf( "--->  state_id_rbt_hash_func=%lu\n",  (unsigned long)(i1 ^ i2 ^ i3) );
#endif
  return (unsigned long)(i1 ^ i2 ^ i3);
} /* state_id_rbt_hash_func */


/**
 *
 * nfs4_Init_state_id: Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs4_Init_state_id( nfs_state_id_parameter_t  param ) 
{ 
  /* Init  all_one */
  memset( all_one, 0xFF, 12 ) ;

  if( ( ht_state_id = HashTable_Init( param.hash_param  ) ) == NULL )
    {
      DisplayLog( "NFS STATE_ID: Cannot init State Id cache" ) ;
      return -1 ;
    }

  return 0 ;
} /* nfs_Init_client_id */


/**
 *
 * nfs4_BuildStateId_Other_Raw
 *
 * This routine fills in the pcontext field in the compound data.
 * pentry is supposed to be locked when this function is called.
 *
 * @param fileid   [IN]   file's id
 * @param id       [IN]   open/owner's pair id
 * @param other    [OUT]  the stateid.other object (a char[12] string)
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_BuildStateId_Other_Raw( uint64_t fileid, u_int16_t id, char * other ) 
{
  u_int16_t    srvboot_digest = 0 ;
  uint64_t     fileid_digest  = 0 ;
  u_int16_t    counter_digest = 0 ;
#ifdef _DEBUG_STATES
  unsigned int i              = 0 ;
#endif
  
  if( other == 0 )
    return 0 ;

  srvboot_digest = (u_int16_t)(ServerBootTime & 0x0000FFFF ) ;  ;
  fileid_digest  = fileid ;
  counter_digest = id ;

  /* Now, let's do the time's warp again.... Well, in fact we'll just build the stateid.other field */
  memcpy( (char *)other,     &srvboot_digest, 2 ) ;
  memcpy( (char *)(other+2), &fileid_digest,  8 ) ;
  memcpy( (char *)(other+10), &counter_digest, 2 ) ;

#ifdef _DEBUG_STATES
   printf( "         ----- BuildStateId_Other_Raw %x|%llx|%x : ", srvboot_digest, fileid_digest, counter_digest  ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;
   printf( "\n" ) ;
#endif


  return 1 ;
} /* nfs_BuildStateId_Other_Raw */

/**
 *
 * nfs4_BuildStateId_Other
 *
 * This routine fills in the pcontext field in the compound data.
 * pentry is supposed to be locked when this function is called.
 *
 * @param pentry   [INOUT] related pentry (should be a REGULAR FILE)
 * @param pcontext [IN]    FSAL's operation context
 * @param other    [OUT]   the stateid.other object (a char[12] string)
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_BuildStateId_Other( cache_entry_t * pentry, fsal_op_context_t * pcontext, char * other )
{
  
  u_int16_t counter        = 0 ;
  uint64_t  fileid_digest  = 0 ;

  if( pcontext == NULL )
   return 0 ;

  if( pentry == NULL )
   return 0 ;

  if( pentry->internal_md.type != REGULAR_FILE ) 
   return 0 ;

  if( other == NULL ) 
   return 0 ;

  /* Get 3x32 bits digest: the server boot time, the fileid and a monotonic counter */  
  if( FSAL_IS_ERROR( FSAL_DigestHandle( pcontext->export_context,
                                        FSAL_DIGEST_FILEID3,
                                        &(pentry->object.file.handle),
                                        (caddr_t)&fileid_digest ) ) )
        return 0 ;
  
  counter = pentry->object.file.state_current_counter  ;

  if( !nfs_BuildStateId_Other_Raw( fileid_digest, counter, other ) )
    return 0 ;

  return 1;
} /* nfs4_BuildStateId_Other */

/**
 *
 * nfs4_State_Set
 *
 * This routine sets a state into the states's hashtable.
 *
 * @param pstate [IN] pointer to the stateid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Set( char other[12], cache_inode_state_t * pstate_data ) 
{
  hash_buffer_t buffkey ;
  hash_buffer_t buffval ;

#ifdef _DEBUG_STATES
  int i = 0 ;

  printf( "         -----  SetStateid : " ) ;
  for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;

  printf( "\n" ) ;
#endif


  if( ( buffkey.pdata = (caddr_t)Mem_Alloc( 12 ) ) == NULL )
        return 0 ;
  memcpy( buffkey.pdata, other, 12 ) ;
  buffkey.len = 12 ; 

  buffval.pdata = (caddr_t)pstate_data ;
  buffval.len = sizeof( cache_inode_state_t ) ;

  if( HashTable_Test_And_Set( ht_state_id, &buffkey, &buffval, HASHTABLE_SET_HOW_SET_NO_OVERWRITE ) != HASHTABLE_SUCCESS )
    return 0 ;

  return 1 ;
} /* nfs4_State_Set */

/**
 *
 * nfs4_State_Get
 *
 * This routine gets a state from the states's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Get( char other[12], cache_inode_state_t * pstate_data )
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

#ifdef _DEBUG_STATES
   int i = 0 ;

   printf( "         -----  GetStateid : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;
   printf( "\n" ) ;
#endif

   buffkey.pdata = (caddr_t)other ;
   buffkey.len   = 12 ;

   if( HashTable_Get( ht_state_id, &buffkey, &buffval ) != HASHTABLE_SUCCESS ) 
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_State_Get  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   memcpy( pstate_data, buffval.pdata, sizeof( cache_inode_state_t ) ) ;
#ifdef _DEBUG_STATES
   printf( "---> nfs4_State_Get Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_State_Get */

/**
 *
 * nfs4_State_Get_Pointer
 *
 * This routine gets a pointer to a state from the states's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Get_Pointer( char other[12], cache_inode_state_t * * pstate_data )
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

#ifdef _DEBUG_STATES
   int i = 0 ;

   printf( "         -----  Get_PointerStateid : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;
   printf( "\n" ) ;
#endif

   buffkey.pdata = (caddr_t)other ;
   buffkey.len   = 12 ;

   if( HashTable_Get( ht_state_id, &buffkey, &buffval ) != HASHTABLE_SUCCESS )
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_State_Get_Pointer  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   *pstate_data = (cache_inode_state_t *)buffval.pdata ;

#ifdef _DEBUG_STATES
   printf( "---> nfs4_State_Get_Pointer Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_State_Get_Pointer */

/**
 * 
 * nfs4_State_Update
 *
 * This routine updates a state from the states's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [IN] new state
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nfs4_State_Update( char other[12], cache_inode_state_t * pstate_data )
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

#ifdef _DEBUG_STATES
   int i = 0 ;

   printf( "         -----  UpdateStateid : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;
   printf( "\n" ) ;
#endif

   buffkey.pdata = (caddr_t)other ;
   buffkey.len   = 12 ;

   if( HashTable_Get( ht_state_id, &buffkey, &buffval ) != HASHTABLE_SUCCESS )
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_State_Update  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   memcpy( buffval.pdata, pstate_data, sizeof( cache_inode_state_t ) ) ;
#ifdef _DEBUG_STATES
   printf( "---> nfs4_State_Update Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_State_Update */

/**
 *
 * nfs4_State_Del
 *
 * This routine removes a state from the states's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Del( char other[12] )
{
  hash_buffer_t     buffkey , old_key, old_value ;

#ifdef _DEBUG_STATES
   int i = 0 ;

   printf( "         -----  DelStateid : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char)other[i] ) ;
   printf( "\n" ) ;
#endif

  buffkey.pdata = (caddr_t)other ;
  buffkey.len   = 12 ;

  if( HashTable_Del( ht_state_id, &buffkey, &old_key, &old_value ) == HASHTABLE_SUCCESS )
     {
        /* free the key that was stored in hash table */
        Mem_Free( (void *)old_key.pdata ) ;
       
        /* State is managed in stuff alloc, no fre is needed for old_value.pdata */
 
        return 1 ;
     }
  else 
        return 0 ;
} /* nfs4_State_Del */

/**
 *
 * nfs4_Check_Stateid
 *
 * This routine checks the availability of the stateid
 *
 * @param pstate [IN] pointer to the stateid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Check_Stateid( struct stateid4 * pstate, cache_entry_t * pentry )
{
   u_int16_t             time_digest    = 0 ;
   u_int16_t             counter_digest = 0 ;
   cache_inode_state_t   state ;
   nfs_client_id_t       nfs_clientid ;


#ifdef _DEBUG_STATES 
   int i = 0 ;
 
   printf( "         -----  CheckStateid : " ) ;
   for( i = 0 ; i < 12 ; i++ )
     printf( "%02x", (unsigned char )pstate->other[i] ) ;
   printf( "\n" ) ;
#endif

   if( pstate == NULL ) 
    return NFS4ERR_SERVERFAULT ;

  if( pentry == NULL ) 
    return NFS4ERR_SERVERFAULT;
  
  if( pentry->internal_md.type != REGULAR_FILE )
    return NFS4ERR_SERVERFAULT ;

  /* Try to get the related state */
  if( !nfs4_State_Get( pstate->other, &state ) )
   {
     /* State not found : return NFS4ERR_BAD_STATEID, RFC3530 page 129 */
     return NFS4ERR_BAD_STATEID ;
   }

  /* Get the related clientid */
  if( nfs_client_id_get( state.powner->clientid, &nfs_clientid ) != CLIENT_ID_SUCCESS )
     return NFS4ERR_BAD_STATEID ; /* Refers to a non-existing client... */

  /* Check for state availability */
  memcpy( (char *)&counter_digest, (char *)(state.stateid_other + 10), 2 ) ;

  if( counter_digest < pentry->object.file.state_head_counter ) 
     return NFS4ERR_BAD_STATEID ; /* Old state id ? */

  /* Check if stateid was made from this server instance */
  memcpy( (char *)&time_digest, pstate->other, 2 ) ;

  if( (u_int16_t)(ServerBootTime & 0x0000FFFF ) != time_digest ) 
    return NFS4ERR_STALE_STATEID ;

   return NFS4_OK ; 
} /* nfs4_Check_Stateid */

/**
 * 
 *  nfs4_State_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the states. 
 * 
 * @return nothing (void function)
 */

void nfs_State_PrintAll( void )
{
   HashTable_Print( ht_state_id ) ;
} /* nfs_State_PrintAll */
