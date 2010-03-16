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
 * nfs_session_id.c : The management of the session id cache.
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
#include <ctype.h>		/* for having isalnum */
#include <stdlib.h>		/* for having atoi */
#include <dirent.h>		/* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
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

extern time_t ServerBootTime;
extern nfs_parameter_t nfs_param;

hash_table_t *ht_session_id;
uint32_t global_sequence = 0;
pthread_mutex_t mutex_sequence = PTHREAD_MUTEX_INITIALIZER;

int display_session_id_key(hash_buffer_t * pbuff, char *str)
{
  unsigned int i = 0;
  unsigned int len = 0;

  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    len += sprintf(&(str[i * 2]), "%02x", (unsigned char)pbuff->pdata[i]);
  return len;
}				/* display_session_id_val */

int display_session_id_val(hash_buffer_t * pbuff, char *str)
{
  nfs41_session_t *psession = (nfs41_session_t *) (pbuff->pdata);

  return sprintf(str, "not implemented");
}				/* display_session_id_val */

int compare_session_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return memcmp(buff1->pdata, buff2->pdata, NFS4_SESSIONID_SIZE);
}				/* compare_session_id */

unsigned long session_id_value_hash_func(hash_parameter_t * p_hparam,
					 hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    {
      c = ((char *)buffclef->pdata)[i];
      sum += c;
    }

#ifdef _DEBUG_SESSIONS
  printf("---> session_id_value_hash_func=%lu\n",
	 (unsigned long)(sum % p_hparam->index_size));
#endif
  return (unsigned long)(sum % p_hparam->index_size);
}				/*  client_id_reverse_value_hash_func */

unsigned long session_id_rbt_hash_func(hash_parameter_t * p_hparam,
				       hash_buffer_t * buffclef)
{

  u_int32_t i1 = 0;
  u_int32_t i2 = 0;
  u_int32_t i3 = 0;
  u_int32_t i4 = 0;
#ifdef _DEBUG_SESSIONS
  unsigned int i = 0;

  printf("         ----- session_id_rbt_hash_func : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)buffclef->pdata[i]);
  printf("\n");
#endif

  memcpy(&i1, &(buffclef->pdata[0]), sizeof(u_int32_t));
  memcpy(&i2, &(buffclef->pdata[4]), sizeof(u_int32_t));
  memcpy(&i3, &(buffclef->pdata[8]), sizeof(u_int32_t));
  memcpy(&i4, &(buffclef->pdata[12]), sizeof(u_int32_t));

#ifdef _DEBUG_SESSIONS
  printf("--->  session_id_rbt_hash_func=%lu\n", (unsigned long)(i1 ^ i2 ^ i3));
#endif
  return (unsigned long)(i1 ^ i2 ^ i3 | i4);
}				/* session_id_rbt_hash_func */

/**
 *
 * nfs41_Init_session_id: Init the hashtable for Session Id cache.
 *
 * Perform all the required initialization for hashtable Session Id cache
 * 
 * @param param [IN] parameter used to init the session id cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs41_Init_session_id(nfs_session_id_parameter_t param)
{
  if ((ht_session_id = HashTable_Init(param.hash_param)) == NULL)
    {
      DisplayLog("NFS SESSION_ID: Cannot init Session Id cache");
      return -1;
    }

  return 0;
}				/* nfs_Init_sesion_id */

/**
 *
 * nfs41_Build_sessionid
 *
 * This routine fills in the pcontext field in the compound data.
 * pentry is supposed to be locked when this function is called.
 *
 * @param pclientid     [IN]    pointer to the related clientid
 * @param sessionid    [OUT]   the sessionid
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Build_sessionid(clientid4 * pclientid, char sessionid[NFS4_SESSIONID_SIZE])
{
  uint32_t seq;

  P(mutex_sequence);
  global_sequence += 1;
  seq = global_sequence;
  V(mutex_sequence);

  memset((char *)sessionid, 0, NFS4_SESSIONID_SIZE);
  memcpy((char *)sessionid, (char *)pclientid, sizeof(clientid4));
  memcpy((char *)(sessionid + sizeof(clientid4)), (char *)&seq, sizeof(seq));

  return 1;
}				/* nfs41_Build_sessionid */

/**
 *
 * nfs41_Session_Set
 *
 * This routine sets a session into the sessions's hashtable.
 *
 * @param psession [IN] pointer to the sessionid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Set(char sessionid[NFS4_SESSIONID_SIZE],
		      nfs41_session_t * psession_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

#ifdef _DEBUG_SESSIONS
  int i = 0;

  printf("         -----  SetSSession : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)sessionid[i]);

  printf("\n");
#endif

  if ((buffkey.pdata = (caddr_t) Mem_Alloc(NFS4_SESSIONID_SIZE)) == NULL)
    return 0;
  memcpy(buffkey.pdata, sessionid, NFS4_SESSIONID_SIZE);
  buffkey.len = NFS4_SESSIONID_SIZE;

  buffval.pdata = (caddr_t) psession_data;
  buffval.len = sizeof(nfs41_session_t);

  if (HashTable_Test_And_Set
      (ht_session_id, &buffkey, &buffval,
       HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}				/* nfs41_Session_Set */

/**
 *
 * nfs41_Session_Get
 *
 * This routine gets a session from the sessions's hashtable.
 *
 * @param psession      [IN] pointer to the sessionid to be checked.
 * @param psession_data [OUT] found session
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Get(char sessionid[NFS4_SESSIONID_SIZE],
		      nfs41_session_t * psession_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

#ifdef _DEBUG_SESSIONS
  int i = 0;

  printf("         -----  GetSessionId : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)sessionid[i]);
  printf("\n");
#endif

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if (HashTable_Get(ht_session_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
#ifdef _DEBUG_SESSIONS
      printf("---> nfs41_Session_Get  NOT FOUND !!!!!!\n");
#endif
      return 0;
    }

  memcpy(psession_data, buffval.pdata, sizeof(nfs41_session_t));
#ifdef _DEBUG_SESSIONS
  printf("---> nfs41_Session_Get Found :-)\n");
#endif
  return 1;
}				/* nfs41_Session_Get */

/**
 *
 * nfs41_Session_Get_Pointer
 *
 * This routine gets a pointer to a session from the sessions's hashtable.
 *
 * @param psession       [IN] pointer to the sessionid to be checked.
 * @param ppsession_data [OUT] pointer's session found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
			      nfs41_session_t * *psession_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

#ifdef _DEBUG_SESSIONS
  int i = 0;

  printf("         -----  Get_PointerSession : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)sessionid[i]);
  printf("\n");
#endif

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if (HashTable_Get(ht_session_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
#ifdef _DEBUG_SESSIONS
      printf("---> nfs41_Session_Get_Pointer  NOT FOUND !!!!!!\n");
#endif
      return 0;
    }

  *psession_data = (nfs41_session_t *) buffval.pdata;

#ifdef _DEBUG_SESSIONS
  printf("---> nfs41_Session_Get_Pointer Found :-)\n");
#endif
  return 1;
}				/* nfs41_Session_Get_Pointer */

/**
 * 
 * nfs41_Session_Update
 *
 * This routine updates a session from the sessions's hashtable.
 *
 * @param psession      [IN] pointer to the sessionid to be checked.
 * @param psession_data [IN] new session
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nfs41_Session_Update(char sessionid[NFS4_SESSIONID_SIZE],
			 nfs41_session_t * psession_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

#ifdef _DEBUG_SESSIONS
  int i = 0;

  printf("         -----  UpdateSession : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)sessionid[i]);
  printf("\n");
#endif

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if (HashTable_Get(ht_session_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
#ifdef _DEBUG_SESSIONS
      printf("---> nfs41_Session_Update  NOT FOUND !!!!!!\n");
#endif
      return 0;
    }

  memcpy(buffval.pdata, psession_data, sizeof(nfs41_session_t));
#ifdef _DEBUG_SESSIONS
  printf("---> nfs41_Session_Update Found :-)\n");
#endif
  return 1;
}				/* nfs41_Session_Update */

/**
 *
 * nfs41_Session_Del
 *
 * This routine removes a session from the sessions's hashtable.
 *
 * @param sessionid [IN] sessionid, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE])
{
  hash_buffer_t buffkey, old_key, old_value;

#ifdef _DEBUG_SESSIONS
  int i = 0;

  printf("         -----  DelSession : ");
  for (i = 0; i < NFS4_SESSIONID_SIZE; i++)
    printf("%02x", (unsigned char)sessionid[i]);
  printf("\n");
#endif

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if (HashTable_Del(ht_session_id, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    } else
    return 0;
}				/* nfs41_Session_Del */

/**
 * 
 *  nfs41_Session_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the sessions. 
 * 
 * @return nothing (void function)
 */

void nfs41_Session_PrintAll(void)
{
  HashTable_Print(ht_session_id);
}				/* nfs41_Session_PrintAll */
