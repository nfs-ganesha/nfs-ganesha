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
 * nfs_open_owner.c : The management of the open_owner cache.
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


extern nfs_parameter_t nfs_param ;

hash_table_t * ht_open_owner ;

int display_open_owner_key( hash_buffer_t * pbuff, char * str )
{
  unsigned int i   = 0 ;
  unsigned int len = 0 ;

  strcpy( str, (char *)pbuff->pdata ) ;

  return 1 ;
} /* display_state_id_val */

int display_open_owner_val( hash_buffer_t * pbuff, char * str )
{
  cache_inode_state_t * pstate = (cache_inode_state_t *)(pbuff->pdata) ;

  return sprintf( str, "state %p is associated with pentry=%p and clientid=%llx type=%u my_id=%u seqid=%u prev=%p next=%p\n", 
                  pstate, pstate->pentry, (unsigned long long)pstate->state_owner.clientid, pstate->state_type, pstate->my_id, pstate->seqid,
                  pstate->prev, pstate->next ) ;
} /* display_state_id_val */

int compare_open_owner( hash_buffer_t * buff1, hash_buffer_t * buff2 )
{
   open_owner4 * powner1 = (open_owner4 * )buff1->pdata ;
   open_owner4 * powner2 = (open_owner4 * )buff2->pdata ;

   if( powner1 == NULL || powner2 == NULL ) 
     return 1 ;

   if( powner1->clientid != powner2->clientid )
     return 1 ;

   if( powner1->owner.owner_len != powner2->owner.owner_len )
      return 1 ;

   return strncmp( powner1->owner.owner_val, powner2->owner.owner_val, powner1->owner.owner_len ) ;
} /* compare_state_id */

unsigned long open_owner_value_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef )
{
  unsigned int  sum = 0 ;
  unsigned int  i   = 0 ;
  unsigned char c   = 0 ;
  unsigned long res = 0 ;

  open_owner4 * powner = (open_owner4 *)buffclef->pdata ;

  /* Compute the sum of all the characters */
  for( i = 0 ; i < powner->owner.owner_len ; i++ ) 
   { 
       c=((char *)powner->owner.owner_val)[i] ;
       sum += c ;
   }

#ifdef _DEBUG_STATES
  printf( "---> state_id_value_hash_func=%lu\n",(unsigned long)( sum % p_hparam->index_size ) ) ; 
#endif
  
  res = (unsigned long)(powner->clientid) + (unsigned long)sum ;

  return (unsigned long)( res % p_hparam->index_size ) ;
} /*  client_id_reverse_value_hash_func */

unsigned long open_owner_rbt_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef )
{
  open_owner4 * powner = (open_owner4 *)buffclef->pdata ;

  u_int32_t i1 = 0 ;
  u_int32_t i2 = 0 ;
  u_int32_t i3 = 0 ;

  memcpy( &i1, &(powner->clientid) , sizeof( u_int32_t ) ) ;
  memcpy( &i2, &(powner->owner.owner_val), (powner->owner.owner_len < sizeof( u_int32_t ))?powner->owner.owner_len:sizeof( u_int32_t ) ) ;
  if( powner->owner.owner_len > sizeof( u_int32_t ) )
    memcpy( &i3, &(powner->owner.owner_val) +sizeof( u_int32_t), 
	    (powner->owner.owner_len < 2*sizeof( u_int32_t ))?powner->owner.owner_len-sizeof(u_int32_t):sizeof( u_int32_t ) ) ;
  else
    i3 = 0xF0F0F0F0 ;
 
#ifdef _DEBUG_STATES 
  printf( "--->  state_id_rbt_hash_func=%lu\n",  (unsigned long)(i1 ^ i2 ^ i3) );
#endif
  return (unsigned long)(i1 ^ i2 ^ i3);
} /* state_id_rbt_hash_func */


/**
 *
 * nfs4_Init_open_owner: Init the hashtable for Open Owner cache.
 *
 * Perform all the required initialization for hashtable Open Owner cache
 * 
 * @param param [IN] parameter used to init open owner cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs4_Init_open_owner( nfs_open_owner_parameter_t  param ) 
{
  /* I reused nfs_state_id_parameter, they are for the same kind of purpose */ 
  if( ( ht_open_owner = HashTable_Init( param.hash_param  ) ) == NULL )
    {
      DisplayLog( "NFS STATE_ID: Cannot init State Id cache" ) ;
      return -1 ;
    }

  return 0 ;
} /* nfs_Init_open_owner */



/**
 *
 * nfs4_Open_Owner_Set
 *
 * This routine sets a open_owner into the open_owner's hashtable.
 *
 * @param popen_owner [IN] pointer to the open_owner to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Open_Owner_Set( open_owner4 * popen_owner, cache_inode_state_t * pstate_data ) 
{
  hash_buffer_t buffkey ;
  hash_buffer_t buffval ;
  open_owner4 * powner_key = NULL ;


  if( (  powner_key = (open_owner4 *)Mem_Alloc( sizeof( open_owner4 ) ) ) == NULL )
        return 0 ;

  if( ( powner_key->owner.owner_val = (char *)Mem_Alloc( popen_owner->owner.owner_len ) ) == NULL )
	return 0 ;

  powner_key->clientid = popen_owner->clientid ;
  powner_key->owner.owner_len = popen_owner->owner.owner_len ;
  strncpy( powner_key->owner.owner_val, popen_owner->owner.owner_val, popen_owner->owner.owner_len ) ; 

  buffkey.pdata = (caddr_t)powner_key ;
  buffkey.len = sizeof( open_owner4 ) + popen_owner->owner.owner_len ; 

  buffval.pdata = (caddr_t)pstate_data ;
  buffval.len = sizeof( cache_inode_state_t ) ;

  if( HashTable_Test_And_Set( ht_open_owner, &buffkey, &buffval, HASHTABLE_SET_HOW_SET_NO_OVERWRITE ) != HASHTABLE_SUCCESS )
    return 0 ;

  return 1 ;
} /* nfs4_Open_Owner_Set */

/**
 *
 * nfs4_Open_Owner_Get
 *
 * This routine gets a open_owner from the open_owners hashtable.
 *
 * @param pstate      [IN] pointer to the open_owner to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Open_Owner_Get( open_owner4 * popen_owner, cache_inode_state_t * pstate_data ) 
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

   buffkey.pdata = (char *)popen_owner ;
   buffkey.len   = sizeof( open_owner4 ) + popen_owner->owner.owner_len ;

   if( HashTable_Get( ht_open_owner, &buffkey, &buffval ) != HASHTABLE_SUCCESS ) 
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_Open_Owner_Get  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   memcpy( pstate_data, buffval.pdata, sizeof( cache_inode_state_t ) ) ;
#ifdef _DEBUG_STATES
   printf( "---> nfs4_Open_Owner_Get Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_Open_Owner_Get */

/**
 *
 * nfs4_Open_Owner_Get_Pointer
 *
 * This routine gets a pointer to a state from the open_owner's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Open_Owner_Get_Pointer( open_owner4 * popen_owner, cache_inode_state_t *  *pstate_data ) 
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

   buffkey.pdata = (char *)popen_owner ;
   buffkey.len   = sizeof( open_owner4 ) + popen_owner->owner.owner_len ;

   if( HashTable_Get( ht_open_owner, &buffkey, &buffval ) != HASHTABLE_SUCCESS )
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_Open_Owner_Get_Pointer  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   *pstate_data = (cache_inode_state_t *)buffval.pdata ;

#ifdef _DEBUG_STATES
   printf( "---> nfs4_Open_Owner_Get_Pointer Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_Open_Owner_Get_Pointer */

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
int nfs4_Open_Owner_Update( open_owner4 * popen_owner, cache_inode_state_t * pstate_data ) 
{
   hash_buffer_t buffkey ;
   hash_buffer_t buffval ;

   buffkey.pdata = (char *)popen_owner ;
   buffkey.len   = sizeof( open_owner4 ) + popen_owner->owner.owner_len ;

   if( HashTable_Get( ht_open_owner, &buffkey, &buffval ) != HASHTABLE_SUCCESS )
    {
#ifdef _DEBUG_STATES
        printf( "---> nfs4_Open_Owner_Update  NOT FOUND !!!!!!\n" ) ;
#endif
        return 0 ;
     }

   memcpy( buffval.pdata, pstate_data, sizeof( cache_inode_state_t ) ) ;
#ifdef _DEBUG_STATES
   printf( "---> nfs4_Open_Owner_Update Found :-)\n" ) ;
#endif
   return 1 ;
} /* nfs4_Open_Owner_Update */

/**
 *
 * nfs4_Open_Owner_Del
 *
 * This routine removes a open_owner from the states's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Open_Owner_Del( open_owner4 * popen_owner )
{
  hash_buffer_t     buffkey , old_key, old_value ;

  buffkey.pdata = (char *)popen_owner ;
  buffkey.len   = sizeof( open_owner4 ) + popen_owner->owner.owner_len ;

  if( HashTable_Del( ht_open_owner, &buffkey, &old_key, &old_value ) == HASHTABLE_SUCCESS )
     {
        /* free the key that was stored in hash table */
        Mem_Free( (void *)old_key.pdata ) ;
       
        /* State is managed in stuff alloc, no fre is needed for old_value.pdata */
 
        return 1 ;
     }
  else 
        return 0 ;
} /* nfs4_Open_Owner_Del */

/**
 * 
 *  nfs4_Open_Owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the open_owners. 
 * 
 * @return nothing (void function)
 */

void nfs_Open_Owner_PrintAll( void )
{
   HashTable_Print( ht_open_owner ) ;
} /* nfs_State_PrintAll */
