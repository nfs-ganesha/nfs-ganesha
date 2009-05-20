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
 * \file    nfs4_referral.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/02/08 12:49:32 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing NFSv4 referrals.
 *
 * nfs4_pseudo.c: Routines used for managing NFSv4 referrals.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>  /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
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
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "cache_content.h"


int nfs4_Set_Fh_Referral( nfs_fh4 * pfh )
{
  file_handle_v4_t * pfhandle4;

  if( pfh == NULL )
    return 0 ;

  pfhandle4 = (file_handle_v4_t *)(pfh->nfs_fh4_val) ;

  pfhandle4->refid = 1 ;
  
  return 1 ;
}

int nfs4_referral_str_To_Fattr_fs_location( char * input_str, char * buff, u_int * plen )
{
   char str[MAXPATHLEN] ;
   char local_part[MAXPATHLEN] ;
   char * local_comp[MAXNAMLEN] ;
   char remote_part[MAXPATHLEN] ;
   char * remote_comp[MAXNAMLEN] ;
   char server_part[MAXPATHLEN] ;

   u_int nb_comp_local  = 1 ;  
   u_int nb_comp_remote = 1 ;  
   u_int lastoff = 0 ;
   u_int tmp_int = 0 ;
   u_int i = 0 ;
   u_int delta_xdr = 0 ;

   char * ptr = NULL ;
 
   if( !str || !buff ) 
	return 0 ;

   strncpy( str, input_str, MAXPATHLEN ) ;

   /* Find the ":" in the string */
   for( ptr = str ; *ptr != ':' ; ptr ++ ) ;
   *ptr = '\0' ;
   ptr += 1 ;

   strncpy( local_part, str, MAXPATHLEN ) ; 
   strncpy( remote_part, ptr, MAXPATHLEN ) ;

   /* Each part should not start with a leading slash */
   if( local_part[0] == '/' )
     strncpy( local_part, str+1, MAXPATHLEN ) ; 

   if( remote_part[0] == '/' )
     strncpy( remote_part, ptr+1, MAXPATHLEN ) ;

   /* Find the "@" in the remote_part */
   for( ptr = remote_part; *ptr != '@' ; ptr ++ ) ;
   *ptr = '\0' ;
   ptr += 1 ;
 
   strncpy( server_part, ptr, MAXPATHLEN ) ;
 
   local_comp[0] = local_part ;
   for( ptr = local_part ; *ptr != '\0' ; ptr ++ )
     if( *ptr == '/' )
      {
        local_comp[nb_comp_local] = ptr+1 ;
        nb_comp_local += 1 ;
      }
   for( tmp_int = 0 ; tmp_int < nb_comp_local ; tmp_int ++ )
    {
      ptr = local_comp[tmp_int] - 1 ;
      *ptr = '\0' ;
    }

   remote_comp[0] = remote_part ;
   for( ptr = remote_part ; *ptr != '\0' ; ptr ++ )
     if( *ptr == '/' ) 
      {
        remote_comp[nb_comp_remote] = ptr+1 ;
        nb_comp_remote += 1 ;
      }
   for( tmp_int = 0 ; tmp_int < nb_comp_remote ; tmp_int ++ )
    {
      ptr = remote_comp[tmp_int] - 1 ;
      *ptr = '\0' ;
    }

   /* This attributes is equivalent to a "mount" command line,
    * To understand what's follow, imagine that you do kind of "mount refer@server nfs_ref" */

#ifdef _DEBUG_REFERRAL
   printf( "--> %s\n", input_str ) ;

   printf( "   %u comp local\n", nb_comp_local ) ;
   for( tmp_int = 0 ; tmp_int < nb_comp_local ; tmp_int ++ )
    printf( "     #%s#\n", local_comp[tmp_int] ) ;  

   printf( "   %u comp remote\n", nb_comp_remote ) ;
   for( tmp_int = 0 ; tmp_int < nb_comp_remote ; tmp_int ++ )
    printf( "     #%s#\n", remote_comp[tmp_int] ) ;  

   printf( "   server = #%s#\n", server_part ) ;
#endif

   /* 1- Number of component in local path */
   tmp_int = htonl( nb_comp_local ) ;
   memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
   lastoff += sizeof( u_int ) ;

   /* 2- each component in local path */
   for( i = 0 ; i < nb_comp_local ; i ++ ) 
    {
       /* The length for the string */
       tmp_int = htonl( strlen( local_comp[i] ) ) ;
       memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
       lastoff += sizeof( u_int ) ;
       
       /* the string itself */
       memcpy( (char *)(buff + lastoff) , local_comp[i], strlen( local_comp[i] ) ) ;
       lastoff += strlen( local_comp[i] ) ;

       /* The XDR padding  : strings must be aligned to 32bits fields */
       if( ( strlen( local_comp[i] ) % 4 ) == 0 )
	 delta_xdr = 0 ;
       else
        {
         delta_xdr =  4 - (  strlen( local_comp[i] ) % 4 ) ;
         memset( (char *)(buff + lastoff), 0, delta_xdr ) ;
         lastoff += delta_xdr ;
        }
    }

  /* 3- there is only one fs_location in the fs_locations array */
  tmp_int = htonl( 1 ) ;
  memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
  lastoff += sizeof( u_int ) ;

  /* 4- Only ine server in fs_location entry */
  tmp_int = htonl( 1 ) ;
  memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
  lastoff += sizeof( u_int ) ;

  /* 5- the len for the server's adress */
  tmp_int = htonl( strlen( server_part ) ) ;
  memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
  lastoff += sizeof( u_int ) ;

  /* 6- the server's string */
  memcpy( (char *)(buff + lastoff) , server_part, strlen( server_part ) ) ;
  lastoff += strlen( server_part ) ;

  /* 7- XDR padding for server's string */
  if( ( strlen( server_part ) % 4 ) == 0 )
   delta_xdr = 0 ;
  else
   {
      delta_xdr =  4 - (  strlen( server_part ) % 4 ) ;
      memset( (char *)(buff + lastoff), 0, delta_xdr ) ;
      lastoff += delta_xdr ;
   }

  /* 8- Number of component in remote path */
  tmp_int = htonl( nb_comp_remote ) ;
  memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
  lastoff += sizeof( u_int ) ;

  /* 9- each component in local path */
  for( i = 0 ; i < nb_comp_remote ; i ++ ) 
    {
       /* The length for the string */
       tmp_int = htonl( strlen( remote_comp[i] ) ) ;
       memcpy( (char *)(buff + lastoff), &tmp_int, sizeof( u_int ) );
       lastoff += sizeof( u_int ) ;
       
       /* the string itself */
       memcpy( (char *)(buff + lastoff) , remote_comp[i], strlen( remote_comp[i] ) ) ;
       lastoff += strlen( remote_comp[i] ) ;

       /* The XDR padding  : strings must be aligned to 32bits fields */
       if( ( strlen( remote_comp[i] ) % 4 ) == 0 )
	 delta_xdr = 0 ;
       else
        {
         delta_xdr =  4 - (  strlen( remote_comp[i] ) % 4 ) ;
         memset( (char *)(buff + lastoff), 0, delta_xdr ) ;
         lastoff += delta_xdr ;
        }
    }

 /* Set the len then return */
 *plen = lastoff ;
 
 return 1 ; 
} /* nfs4_referral_str_To_Fattr_fs_location */

