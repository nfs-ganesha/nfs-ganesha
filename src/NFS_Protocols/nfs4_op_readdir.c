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
 * --------------------------------------- */
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
#include "nfs_file_handle.h"

#define arg_READDIR4 op->nfs_argop4_u.opreaddir
#define res_READDIR4 resp->nfs_resop4_u.opreaddir

extern time_t ServerBootTime ;

/**
 * nfs4_op_readdir: The NFS4_OP_READDIR.
 * 
 * Implements the NFS4_OP_READDIR. If fh is a pseudo FH, then call is routed to routine nfs4_op_readdir_pseudo
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
int nfs4_op_readdir(  struct nfs_argop4 * op ,   
                      compound_data_t   * data,
                      struct nfs_resop4 * resp)
{
  cache_entry_t    * dir_pentry = NULL ;
  cache_entry_t    * pentry = NULL ;
  
  cache_inode_endofdir_t eod_met ;
  fsal_attrib_list_t     attrlookup ;
  cache_inode_status_t   cache_status ;
  cache_inode_status_t   cache_status_attr ;
  
  char               __attribute__(( __unused__ )) funcname[] = "nfs4_op_readdir" ;
    
  unsigned long              dircount;
  unsigned long              maxcount ;
  entry4 *                   entry_nfs_array ;
  cache_inode_dir_entry_t  * dirent_array;
  verifier4                  cookie_verifier;
  unsigned int               cookie = 0  ;
  unsigned int               end_cookie = 0;
  unsigned int             * cookie_array ;
  fsal_handle_t            * entry_FSALhandle ;
  nfs_fh4                    entryFH ;
  char                       val_fh[NFS4_FHSIZE] ;
  entry_name_array_item_t  * entry_name_array;
  unsigned long             space_used;
  unsigned int              estimated_num_entries;
  unsigned int              num_entries ;

  unsigned int              i = 0 ;

  bitmap4   RdAttrErrorBitmap ;
  attrlist4 RdAttrErrorVals   ;
    
  resp->resop = NFS4_OP_READDIR ;
  res_READDIR4.status =  NFS4_OK  ;

  entryFH.nfs_fh4_len = 0 ;
  entryFH.nfs_fh4_val = val_fh ;
  
  /* If there is no FH */
  if( nfs4_Is_Fh_Empty( &(data->currentFH  ) ) )
    {
      res_READDIR4.status = NFS4ERR_NOFILEHANDLE ;
      return res_READDIR4.status ; 
    }
  
  /* If the filehandle is invalid */
  if( nfs4_Is_Fh_Invalid( &(data->currentFH ) ) )
    {
      res_READDIR4.status = NFS4ERR_BADHANDLE ;
      return res_READDIR4.status ;
    }
      
  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if( nfs4_Is_Fh_Expired( &(data->currentFH) ) )
    {
      res_READDIR4.status = NFS4ERR_FHEXPIRED ;
      return res_READDIR4.status ;
    }
  
  /* Pseudo Fs management */
  if( nfs4_Is_Fh_Pseudo( &(data->currentFH) ) )
    return nfs4_op_readdir_pseudo( op, data, resp ) ;
  
  /* Xattrs management */
  if( nfs4_Is_Fh_Xattr( &(data->currentFH) ) )
    return nfs4_op_readdir_xattr( op, data, resp ) ;
  
  /* You can readdir only within a directory */
  dir_pentry = data->current_entry ;
  if ( data->current_filetype != DIR_BEGINNING && data->current_filetype != DIR_CONTINUE )
    {
      res_READDIR4.status = NFS4ERR_NOTDIR ;
      return res_READDIR4.status ;
    }
  
  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount ;
  maxcount = arg_READDIR4.maxcount ;
  cookie   = (unsigned int)arg_READDIR4.cookie ; 
  space_used = sizeof( entry4 ) ;

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof( entry4 )  ; /* Estimated_num_entries is probably far too big */
  
#ifdef _DEBUG_NFS_V4
  printf( "--- nfs4_op_readdir ---> dircount=%u maxcount=%u arg_cookie=%llu cookie=%d estimated_num_entries=%u\n", 
          dircount, maxcount, arg_READDIR4.cookie, cookie, estimated_num_entries ) ;
#endif
  /* Do not use a cookie of 1 or 2 (reserved values) */
  if( cookie == 1 || cookie == 2 ) 
    {
      res_READDIR4.status = NFS4ERR_BAD_COOKIE ;
      return res_READDIR4.status ;
    }
 
  if( cookie != 0 ) cookie = cookie -2 ; /* 0,1 and 2 are reserved, there is a delta of '3' because of this */
   
   /* Get only attributes that are allowed to be read */
  if( !nfs4_Fattr_Check_Access_Bitmap( &arg_READDIR4.attr_request, FATTR4_ATTR_READ ) )
    {
      res_READDIR4.status = NFS4ERR_INVAL ;
      return res_READDIR4.status ;
    }
 

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if( maxcount < sizeof( entry4 ) || estimated_num_entries == 0  )
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL ;
      return res_READDIR4.status ;
    }
  
  /*
   * If cookie verifier is used, then an non-trivial value is
   * returned to the client         This value is the mtime of
   * the pentry. If verifier is unused (as in many NFS
   * Servers) then only a set of zeros is returned (trivial
   * value) 
   */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE ) ;
  if( data->pexport->UseCookieVerifier == 1 )
      memcpy(cookie_verifier, &dir_pentry->internal_md.mod_time, sizeof( time_t ) ) ;
      
  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and HPSS cookie */
  
  if( ( cookie !=0  ) && ( data->pexport->UseCookieVerifier == 1 ) )
    {
      if(  memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE  ) != 0 )
        {
          
          res_READDIR4.status = NFS4ERR_BAD_COOKIE ;
          return  res_READDIR4.status ;
        }
    }

  /* The default behaviour is to consider that eof is not reached, the returned values by cache_inode_readdir 
   * will let us know if eod was reached or not */
  res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE ;
  
  /* Get prepared for readdir */
  if( ( dirent_array = (cache_inode_dir_entry_t *)Mem_Alloc(estimated_num_entries * sizeof( cache_inode_dir_entry_t ) ) ) == NULL )
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT ;
      return res_READDIR4.status;
    }
  
  if( ( cookie_array = (unsigned int *)Mem_Alloc( estimated_num_entries * sizeof( unsigned int ) ) ) == NULL )
    {
      Mem_Free( (char *)dirent_array ) ;

      res_READDIR4.status = NFS4ERR_SERVERFAULT ;
      return res_READDIR4.status;
    }

  /* Perform the readdir operation */
  if( cache_inode_readdir(dir_pentry, 
                          cookie,
                          estimated_num_entries, 
                          &num_entries, 
                          &end_cookie,
                          &eod_met,
                          dirent_array, 
                          cookie_array,
                          data->ht, 
                          data->pclient,
                          data->pcontext, 
                          &cache_status) != CACHE_INODE_SUCCESS )
    {
      res_READDIR4.status = nfs4_Errno( cache_status ) ;
      return res_READDIR4.status;
    }
  
  if( eod_met == END_OF_DIR )
    {
      /* This is the end of the directory */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE ;
      memcpy( res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier, NFS4_VERIFIER_SIZE ) ;
    }
  
    

  /* For an empty directory, we will find only . and .., so reply af if the end if reached */
  if( num_entries == 0 )
    {
      /* only . and .. */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL ;
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE ;
      memcpy( res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier, NFS4_VERIFIER_SIZE ) ;
    }
  else
    {
      /* Allocation of reply structures */
      if( ( entry_name_array = (entry_name_array_item_t *)Mem_Alloc(num_entries * (FSAL_MAX_NAME_LEN + 1) ) ) == NULL )
        {
          DisplayErrorLog( ERR_SYS, ERR_MALLOC, errno ) ;
          res_READDIR4.status = NFS4ERR_SERVERFAULT ;
          return res_READDIR4.status;
        }
      memset( (char *)entry_name_array, 0, num_entries * (FSAL_MAX_NAME_LEN + 1) ) ;


      if( ( entry_nfs_array = (entry4 *)Mem_Alloc(num_entries * sizeof( entry4 ) ) ) == NULL )
        {
          DisplayErrorLog( ERR_SYS, ERR_MALLOC, errno ) ;
          res_READDIR4.status = NFS4ERR_SERVERFAULT ;
          return res_READDIR4.status;
        }


      for( i = 0 ; i < num_entries ; i ++ )
        {
          entry_nfs_array[i].name.utf8string_val =  entry_name_array[i] ;
 
          if( str2utf8( dirent_array[i].name.name, &entry_nfs_array[i].name ) == -1 )
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT ;
              return res_READDIR4.status;
            }
          
          /* Set the cookie value */
          if( i != num_entries -1 )
            entry_nfs_array[i].cookie = cookie_array[i+1] + 2 ; /* 0, 1 and 2 are reserved */
          else
            entry_nfs_array[i].cookie = end_cookie + 2 ;

#ifdef _DEBUG_NFS_V4
          printf( " === nfs4_op_readdir ===>   i=%d name=%s cookie=%llu\n", 
                  i, dirent_array[i].name.name, entry_nfs_array[i].cookie ) ;
#endif

          /* Get the pentry for the object's attributes and filehandle */
          if( ( pentry = cache_inode_lookup( dir_pentry, 
                                             &dirent_array[i].name,
                                             &attrlookup,
                                             data->ht, 
                                             data->pclient, 
                                             data->pcontext,
                                             &cache_status ) ) == NULL )
            {
              Mem_Free( (char *)entry_nfs_array ) ;
              Mem_Free( (char *)dirent_array ) ;
              Mem_Free( (char *)cookie_array ) ;
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask  = RdAttrErrorBitmap ;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals ;
              res_READDIR4.status = NFS4ERR_SERVERFAULT ;
              return res_READDIR4.status;
            }

           /* If file handle is asked in the attributes, provide it */
          if( arg_READDIR4.attr_request.bitmap4_val != NULL && (arg_READDIR4.attr_request.bitmap4_val[0] & FATTR4_FILEHANDLE) )
            {
             if( ( entry_FSALhandle = cache_inode_get_fsal_handle( pentry, &cache_status_attr ) ) == NULL )
               {
                 /* Faulty Handle or pentry */
                  Mem_Free( (char *)entry_nfs_array ) ;
                  Mem_Free( (char *)dirent_array ) ;
                  Mem_Free( (char *)cookie_array ) ;
                  res_READDIR4.status = NFS4ERR_SERVERFAULT ;
                  return res_READDIR4.status;
                }

             if( !nfs4_FSALToFhandle( &entryFH, entry_FSALhandle, data ) )
                {
                  /* Faulty type */
                  Mem_Free( (char *)entry_nfs_array ) ;
                  Mem_Free( (char *)dirent_array ) ;
                  Mem_Free( (char *)cookie_array ) ;
                  res_READDIR4.status = NFS4ERR_SERVERFAULT ;
                  return res_READDIR4.status;
                }
            }
          
          if( nfs4_FSALattr_To_Fattr( data->pexport,
                                      &attrlookup, 
                                      &(entry_nfs_array[i].attrs), 
                                      data,
                                      &entryFH,
                                      &(arg_READDIR4.attr_request) ) != 0 )
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask  = RdAttrErrorBitmap ;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals ;
            } 

          /* Chain the entries together */ 
          entry_nfs_array[i].nextentry = NULL;
          if( i != 0 ) 
            entry_nfs_array[i-1].nextentry = &(entry_nfs_array[i]); 

          /* This test is there to avoid going further than the buffer provided by the client 
           * the factor "9/10" is there for safety. Its value could be change as beta tests will be done */
          if( (caddr_t)((caddr_t)(&entry_nfs_array[i]) - (caddr_t)(&entry_nfs_array[0])) > (caddr_t)(maxcount*9/10) )
             break ;
        } /* for i */

      
      /* Put the entry's list in the READDIR reply */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array ;
    }    
  
  /* Do not forget to set the verifier */
  memcpy( (char *)res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier, NFS4_VERIFIER_SIZE ) ;

  Mem_Free( (char *)dirent_array ) ;
  Mem_Free( (char *)cookie_array ) ;
  res_READDIR4.status = NFS4_OK ;
  
  return res_READDIR4.status;
} /* nfs4_op_readdir */

    
/**
 * nfs4_op_readdir_Free: frees what was allocared to handle nfs4_op_readdir.
 * 
 * Frees what was allocared to handle nfs4_op_readdir.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_readdir_Free( READDIR4res * resp )
{
  entry4 * entries ;
  
  if( resp->status == NFS4_OK )
    {
      for( entries = resp->READDIR4res_u.resok4.reply.entries; entries != NULL ; entries = entries->nextentry )
        {
          Mem_Free( (char *)entries->attrs.attrmask.bitmap4_val ) ; 
          /** @todo Fixeme , bad Free here Mem_Free( (char *)entries->attrs.attr_vals.attrlist4_val ) ; */
        }

      if( resp->READDIR4res_u.resok4.reply.entries != NULL )
       {
         Mem_Free( (char *)resp->READDIR4res_u.resok4.reply.entries[0].name.utf8string_val ) ; 
         Mem_Free( (char *)resp->READDIR4res_u.resok4.reply.entries ) ;
       }
    }
  
  return ;
} /* nfs4_op_readdir_Free */
