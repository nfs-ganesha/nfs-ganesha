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
 * \file    nfs_tools.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/20 07:39:22 $
 * \version $Revision: 1.43 $
 * \brief   Some tools very usefull in the nfs protocol implementation.
 *
 * nfs_tools.c : Some tools very usefull in the nfs protocol implementation
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif


#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef _FREEBSD
#include <netinet/tcp.h>
#endif /* _FREEBSD */
#include <sys/types.h>
#include <ctype.h>  /* for having isalnum */
#include <stdlib.h> /* for having atoi */
#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>  /* for having FNDELAY */
#include <pwd.h>

#include <grp.h>
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs_core.h" 
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"

struct tcp_conn {  /* kept in xprt->xp_p1 */
        enum xprt_stat strm_stat;
        u_long x_id;
        XDR xdrs;
        char verf_body[MAX_AUTH_BYTES];
};



unsigned long decimal_simple_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  printf( "ATTENTION: APPEL D'UNE DUMMY FUNCTION\n" ) ;
  return 0 ;
}

unsigned long decimal_rbt_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  printf( "ATTENTION: APPEL D'UNE DUMMY FUNCTION\n" ) ;
  return 0 ;
}

int display_cache( hash_buffer_t * pbuff, char * str ) 
{
  return 0 ;
}

int compare_cache(  hash_buffer_t * buff1, hash_buffer_t * buff2 ) 
{
  return 0 ;
}

int print_cache( LRU_data_t data, char *str ) 
{
  return 0 ;
}

int clean_cache(  LRU_entry_t * pentry, void * addparam ) 
{
  return 0 ;
} /* clean_cache */



/**
 * 
 * lru_inode_entry_to_str: printing function for internal worker's LRU.
 *
 * printing function for internal worker's LRU.
 *
 * @param data [IN]  the LRU data to be printed.
 * @param str  [OUT] the string result.
 * 
 * @return the length of the computed string of -1 if failed.
 *
 */
int lru_inode_entry_to_str( LRU_data_t data, char * str)
{
  return sprintf( str, "N/A " ) ;
} /* lru_inode_entry_to_str */

/**
 *
 * lru_data_entry_to_str: printing function for internal worker's LRU.
 *
 * printing function for internal worker's LRU.
 *
 * @param data [IN]  the LRU data to be printed.
 * @param str  [OUT] the string result.
 *
 * @return the length of the computed string of -1 if failed.
 *
 */
int lru_data_entry_to_str( LRU_data_t data, char * str)
{
  return sprintf( str, "addr=%p,len=%u ", data.pdata, data.len ) ;
} /* lru_data_entry_to_str */


/**
 *
 * lru_inode_clean_entry: a function used to clean up a LRU entry during cache inode gc.
 *
 * a function used to clean up a LRU entry during cache inode gc.
 *
 * @param entry   [INOUT] the entry to be cleaned up.
 * @param adddata [IN]    a buffer with additional input parameters.
 *
 * @return 0 if successful, other values show an error.
 *
 */
int lru_inode_clean_entry( LRU_entry_t * entry, void * adddata)
{
  return 0 ;
} /* lru_inode_clean_entry */

/**
 *
 * lru_data_clean_entry: a function used to clean up a LRU entry during cache inode gc.
 *
 * a function used to clean up a LRU entry during cache inode gc.
 *
 * @param entry   [INOUT] the entry to be cleaned up.
 * @param adddata [IN]    a buffer with additional input parameters.
 *
 * @return 0 if successful, other values show an error.
 *
 */
int lru_data_clean_entry( LRU_entry_t * entry, void * adddata)
{
  return 0 ;
} /* lru_data_clean_entry */


void socket_setoptions(int socketFd )
{
  unsigned int SbMax = (1<<30) ;  /* 1GB*/


  while (SbMax > 1048576 )
    {
      if ((setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, (char *)&SbMax, sizeof( SbMax ) ) < 0 ) ||
          (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, (char *)&SbMax, sizeof( SbMax ) ) < 0 ) )
        {
          SbMax >>= 1; /* SbMax = SbMax/2 */
          continue;
        }

      break;
    }

  return;
} /* socket_setoptions_ctrl */


