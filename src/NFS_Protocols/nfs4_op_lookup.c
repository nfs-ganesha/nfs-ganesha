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
 * \file    nfs4_op_lookup.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.18 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lookup.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h" 
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 * nfs4_op_lookup: looks up into theFSAL.
 * 
 * looks up into the FSAL. If a junction is crossed, does what is necessary.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup(  struct nfs_argop4 * op ,   
                     compound_data_t   * data,
                     struct nfs_resop4 * resp)
{
  fsal_name_t          name ;
  char                 strname[MAXNAMLEN] ;
  char                 objname[MAXNAMLEN] ;
  unsigned int         xattr_found = FALSE ;
  cache_entry_t      * dir_pentry = NULL ;
  cache_entry_t      * file_pentry = NULL ;
  fsal_attrib_list_t   attrlookup ;
  cache_inode_status_t cache_status ;

  fsal_handle_t      * pfsal_handle = NULL ;

  char                 __attribute__(( __unused__ )) funcname[] = "nfs4_op_lookup" ;
  
  resp->resop = NFS4_OP_LOOKUP ;
  res_LOOKUP4.status =  NFS4_OK  ;
  
  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_LOOKUP4.status = NFS4ERR_NOFILEHANDLE ;
      return res_LOOKUP4.status ;
    }
  
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_LOOKUP4.status = NFS4ERR_BADHANDLE ;
      return res_LOOKUP4.status ;
    }
  
  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_LOOKUP4.status = NFS4ERR_FHEXPIRED ;
      return res_LOOKUP4.status ;
    }

  /* Check for empty name */
  if( op->nfs_argop4_u.oplookup.objname.utf8string_len == 0 || 
      op->nfs_argop4_u.oplookup.objname.utf8string_val == NULL )
    {
      res_LOOKUP4.status = NFS4ERR_INVAL ;
      return res_LOOKUP4.status ;
    }

  /* Check for name to long */
  if( op->nfs_argop4_u.oplookup.objname.utf8string_len > FSAL_MAX_NAME_LEN )
    {
      res_LOOKUP4.status = NFS4ERR_NAMETOOLONG ;
      return res_LOOKUP4.status ;
    }
 
  /* If Filehandle points to a pseudo fs entry, manage it via pseudofs specific functions */
  if( nfs4_Is_Fh_Pseudo( &(data->currentFH) ) )
    return nfs4_op_lookup_pseudo( op, data, resp ) ;

#ifndef _NO_XATTRD
  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if( nfs4_Is_Fh_Xattr( &(data->currentFH ) ) ) 
    return nfs4_op_lookup_xattr( op, data, resp ) ;
#endif
  
  /* UTF8 strings may not end with \0, but they carry their length */
  strncpy( strname, arg_LOOKUP4.objname.utf8string_val, arg_LOOKUP4.objname.utf8string_len ) ;
  strname[arg_LOOKUP4.objname.utf8string_len] = '\0' ;

#ifndef _NO_XATTRD
  /* Is this a .xattr.d.<object> name ? */
  if( nfs_XattrD_Name( strname, objname ) )
   {
    strcpy( strname, objname ) ;
    xattr_found = TRUE ;
   }
#endif

  if( ( cache_status = cache_inode_error_convert( FSAL_str2name( strname,
                                                                 MAXNAMLEN,
                                                                 &name ) ) ) != CACHE_INODE_SUCCESS )
    {
      res_LOOKUP4.status = nfs4_Errno( cache_status ) ;
      return res_LOOKUP4.status ;
    }

  /* No 'cd .' is allowed return NFS4ERR_BADNAME in this case */ 
  /* No 'cd .. is allowed, return EINVAL in this case. NFS4_OP_LOOKUPP should be use instead */
  if( !FSAL_namecmp( &name, &FSAL_DOT ) || !FSAL_namecmp( &name, &FSAL_DOT_DOT )  )
    {
      res_LOOKUP4.status = NFS4ERR_BADNAME ;
      return res_LOOKUP4.status ;
    }
  
  /* Do the lookup in the HPSS Namespace */
  file_pentry = NULL;
  dir_pentry = data->current_entry ;
  
  /* Sanity check: dir_pentry should be ACTUALLY a directory */
  if( dir_pentry->internal_md.type != DIR_BEGINNING && dir_pentry->internal_md.type != DIR_CONTINUE )
    {
       /* This is not a directory */
       if( dir_pentry->internal_md.type == SYMBOLIC_LINK )
	  res_LOOKUP4.status = NFS4ERR_SYMLINK ;
       else
	  res_LOOKUP4.status = NFS4ERR_NOTDIR ; 

       /* Return failed status */
       return res_LOOKUP4.status ;
    }
 
  /* BUGAZOMEU: Faire la gestion des cross junction traverse */
  if( ( file_pentry = cache_inode_lookup( dir_pentry, 
                                          &name, 
                                          &attrlookup, 
                                          data->ht,
                                          data->pclient,
                                          data->pcontext, 
                                          &cache_status ) ) != NULL )
    {
      /* Extract the fsal attributes from the cache inode pentry */
      pfsal_handle = cache_inode_get_fsal_handle( file_pentry, &cache_status )  ;
    
      if( cache_status != CACHE_INODE_SUCCESS )
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT ;
          return res_LOOKUP4.status ;
        }

      /* Convert it to a file handle */
      if( !nfs4_FSALToFhandle( &data->currentFH, pfsal_handle, data ) )
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT ;
          return res_LOOKUP4.status ;
        }

      /* Copy this to the mounted on FH (if no junction is traversed */
      memcpy( (char *)(data->mounted_on_FH.nfs_fh4_val), 
              (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len ) ;
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len ;
      
      
#ifdef _DEBUG_NFS_V4
      printf( "----> nfs4_op_lookup: name=%s  dir_pentry=%p  looked up pentry=%p\n",
              strname, dir_pentry, file_pentry ) ;
      printf( "----> FSAL handle parent puis fils dans nfs4_op_lookup\n" ) ;
      print_buff( ( char *)cache_inode_get_fsal_handle( file_pentry, &cache_status ), sizeof( fsal_handle_t ) ) ;
      print_buff( ( char *)cache_inode_get_fsal_handle( dir_pentry, &cache_status ), sizeof( fsal_handle_t ) ) ;
#endif      

      /* Keep the pointer within the compound data */
      data->current_entry = file_pentry ;
      data->current_filetype = file_pentry->internal_md.type ;
    
      /* Return successfully */ 
      res_LOOKUP4.status = NFS4_OK ;

#ifndef _NO_XATTRD
      /* If this is a xattr ghost directory name, update the FH */
      if( xattr_found == TRUE )
        res_LOOKUP4.status = nfs4_fh_to_xattrfh( &(data->currentFH), &(data->currentFH) )  ;
#endif

      return NFS4_OK ;
      
    }
  
  /* If the part of the code is reached, then something wrong occured in the lookup process, status is not HPSS_E_NOERROR 
   * and contains the code for the error */
  
  res_LOOKUP4.status = nfs4_Errno( cache_status ) ;

  return res_LOOKUP4.status;
} /* nfs4_op_lookup */


/**
 * nfs4_op_lookup_Free: frees what was allocared to handle nfs4_op_lookup.
 * 
 * Frees what was allocared to handle nfs4_op_lookup.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lookup_Free( LOOKUP4res * resp )
{
  /* Nothing to be done */
  return ;
} /* nfs4_op_lookup_Free */
