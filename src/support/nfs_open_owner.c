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
 * nfs_open_owner.c : The management of the open owner cache.
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
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
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
#include "cache_inode.h"

size_t strnlen(const char *s, size_t maxlen);

extern time_t ServerBootTime;
extern nfs_parameter_t nfs_param;

hash_table_t *ht_open_owner;

uint32_t open_owner_counter = 0;
pthread_mutex_t open_owner_counter_lock = PTHREAD_MUTEX_INITIALIZER;

int display_open_owner_key(hash_buffer_t * pbuff, char *str)
{
  char strtmp[MAXNAMLEN * 2];
  unsigned int i = 0;
  unsigned int len = 0;

  cache_inode_open_owner_name_t *pname = (cache_inode_open_owner_name_t *) pbuff->pdata;

  for(i = 0; i < pname->owner_len; i++)
    len += sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)pname->owner_val[i]);

  return len + sprintf(str, "clientid=%llu owner=(%u|%s)",
                       (unsigned long long)pname->clientid, pname->owner_len, strtmp);
}                               /* display_state_id_val */

int display_open_owner_val(hash_buffer_t * pbuff, char *str)
{
  char strtmp[MAXNAMLEN * 2];
  unsigned int i = 0;
  unsigned int len = 0;

  cache_inode_open_owner_t *powner = (cache_inode_open_owner_t *) (pbuff->pdata);

  for(i = 0; i < powner->owner_len; i++)
    len += sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)powner->owner_val[i]);

  return len + sprintf(str, "clientid=%llu owner=(%u|%s) confirmed=%u seqid=%u",
                       (unsigned long long)powner->clientid, powner->owner_len, strtmp,
                       powner->confirmed, powner->seqid);
}                               /* display_state_id_val */

int compare_open_owner(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  unsigned int rc;

#ifdef _DEBUG_OPEN_OWNER_HASH
  char str1[MAXPATHLEN];
  char str2[MAXPATHLEN];

  display_open_owner_key(buff1, str1);
  display_open_owner_key(buff2, str2);
  printf("compare_open_owner => {%s}|{%s}\n", str1, str2);
#endif

  cache_inode_open_owner_name_t *pname1 = (cache_inode_open_owner_name_t *) buff1->pdata;
  cache_inode_open_owner_name_t *pname2 = (cache_inode_open_owner_name_t *) buff2->pdata;

  if(pname1 == NULL || pname2 == NULL)
    return 1;

  if(pname1->clientid != pname2->clientid)
    return 1;

  if(pname1->owner_len != pname2->owner_len)
    return 1;

  return memcmp((char *)pname1->owner_val, (char *)pname2->owner_val, pname1->owner_len);
}                               /* compare_open_owner */

unsigned long open_owner_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  cache_inode_open_owner_name_t *pname =
      (cache_inode_open_owner_name_t *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

#ifdef _DEBUG_OPEN_OWNER_HASH
  printf("---> rbt_hash_val = %lu\n", res % p_hparam->index_size);
#endif

  return (unsigned long)(res % p_hparam->index_size);

}                               /* open_owner_value_hash_func */

unsigned long open_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  cache_inode_open_owner_name_t *pname =
      (cache_inode_open_owner_name_t *) buffclef->pdata;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

#ifdef _DEBUG_OPEN_OWNER_HASH
  printf("---> rbt_hash_func = %lu\n", res);
#endif

  return res;
}                               /* state_id_rbt_hash_func */

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
int nfs4_Init_open_owner(nfs_open_owner_parameter_t param)
{

  if((ht_open_owner = HashTable_Init(param.hash_param)) == NULL)
    {
      DisplayLog("NFS STATE_ID: Cannot init State Id cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_open_owner */

/**
 * nfs_open_owner_Set
 * 
 *
 * This routine sets a open owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Set(cache_inode_open_owner_name_t * pname,
                       cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
#ifdef _DEBUG_OPEN_OWNER_HASH
  char str[MAXPATHLEN];

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  display_open_owner_key(&buffkey, str);
  printf("nfs_open_owner_Set => KEY {%s}\n", str);
#endif

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(cache_inode_open_owner_t);

  P(open_owner_counter_lock);
  open_owner_counter += 1;
  powner->counter = open_owner_counter;
  V(open_owner_counter_lock);

  if(HashTable_Test_And_Set
     (ht_open_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs_open_owner_Set */

/**
 *
 * nfs_open_owner_Get
 *
 * This routine gets open owner from the openowner's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Get(cache_inode_open_owner_name_t * pname,
                       cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  memcpy((char *)powner, buffval.pdata, sizeof(cache_inode_open_owner_t));

  return 1;
}                               /* nfs_open_owner_Get */

/**
 *
 * nfs_open_owner_Get_Pointer
 *
 * This routine gets a pointer to an open owner from the open owners's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Get_Pointer(cache_inode_open_owner_name_t * pname,
                               cache_inode_open_owner_t * *powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
#ifdef _DEBUG_OPEN_OWNER_HASH
  char str[MAXPATHLEN];

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  display_open_owner_key(&buffkey, str);
  printf("nfs_open_owner_Get_Pointer => KEY {%s}\n", str);
#endif

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
#ifdef _DEBUG_OPEN_OWNER_HASH
      printf("nfs_open_owner_Get_Pointer => NOTFOUND\n");
#endif
      return 0;
    }

  *powner = (cache_inode_open_owner_t *) buffval.pdata;

#ifdef _DEBUG_OPEN_OWNER_HASH
  printf("nfs_open_owner_Get_Pointer => FOUND\n");
#endif

  return 1;
}                               /* nfs_open_owner_Get_Pointer */

/**
 * 
 * nfs_open_owner_Update
 *
 * This routine updates a open owner from the open owners's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [IN] new state
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nfs_open_owner_Update(cache_inode_open_owner_name_t * pname,
                          cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  memcpy(buffval.pdata, powner, sizeof(cache_inode_open_owner_t));

  return 1;
}                               /* nfs_open_owner_Update */

/**
 *
 * nfs_open_owner_Del
 *
 * This routine removes a open owner from the open_owner's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Del(cache_inode_open_owner_name_t * pname)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Del(ht_open_owner, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nfs_open_owner_Del */

/**
 * 
 *  nfs_open_owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the open owners. 
 * 
 * @return nothing (void function)
 */

void nfs_open_owner_PrintAll(void)
{
  HashTable_Print(ht_open_owner);
}                               /* nfs_open_owner_PrintAll */

int nfs_convert_open_owner(open_owner4 * pnfsowner,
                           cache_inode_open_owner_name_t * pname_owner)
{
  if(pnfsowner == NULL || pname_owner == NULL)
    return 0;

  pname_owner->clientid = pnfsowner->clientid;
  pname_owner->owner_len = pnfsowner->owner.owner_len;
  memcpy((char *)pname_owner->owner_val, (char *)pnfsowner->owner.owner_val,
         pnfsowner->owner.owner_len);

  return 1;
}                               /* nfs_convert_open_owner */
