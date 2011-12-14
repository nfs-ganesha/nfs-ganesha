/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
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
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log_macros.h"
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
int nfs4_op_readdir(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *pentry = NULL;

  cache_inode_endofdir_t eod_met;
  fsal_attrib_list_t attrlookup;
  cache_inode_status_t cache_status;
  cache_inode_status_t cache_status_attr;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readdir";

  unsigned long dircount;
  unsigned long maxcount;
  entry4 *entry_nfs_array;
  cache_inode_dir_entry_t **dirent_array = NULL;
  verifier4 cookie_verifier;
  uint64_t cookie = 0;
  uint64_t end_cookie = 0;
  fsal_handle_t *entry_FSALhandle;
  nfs_fh4 entryFH;
  char val_fh[NFS4_FHSIZE];
  entry_name_array_item_t *entry_name_array = NULL;
  unsigned long space_used;
  unsigned int estimated_num_entries;
  unsigned int num_entries;
  int dir_pentry_unlock = FALSE;

  unsigned int i = 0;
  unsigned int outbuffsize = 0 ;
  unsigned int entrysize = 0 ;
 
  bitmap4 RdAttrErrorBitmap = { 1, (uint32_t *) "\0\0\0\b" };   /* 0xB = 11 = FATTR4_RDATTR_ERROR */
  attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  entryFH.nfs_fh4_len = 0;
  entryFH.nfs_fh4_val = val_fh;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_READDIR4.status = NFS4ERR_NOFILEHANDLE;
      return res_READDIR4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_READDIR4.status = NFS4ERR_BADHANDLE;
      return res_READDIR4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_READDIR4.status = NFS4ERR_FHEXPIRED;
      return res_READDIR4.status;
    }

  /* Pseudo Fs management */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_readdir_pseudo(op, data, resp);

  /* Xattrs management */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_readdir_xattr(op, data, resp);

  /* You can readdir only within a directory */
  dir_pentry = data->current_entry;
  if(data->current_filetype != DIRECTORY)
    {
      res_READDIR4.status = NFS4ERR_NOTDIR;
      return res_READDIR4.status;
    }

  /* get the characteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount*0.9;
  cookie = (unsigned int)arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI
   * one).  we use maxcount instead. */

  /* the Linux 3.0, 3.1.0 clients vs. TCP Ganesha comes out 10x slower
   * with 500 max entries */
#if 0
  /* takes 2s to return 2999 entries */
  estimated_num_entries = maxcount / sizeof(entry4);
#else
  /* takes 20s to return 2999 entries */
  estimated_num_entries = 50;
#endif

  LogFullDebug(COMPONENT_NFS_V4,
               "--- nfs4_op_readdir ---> dircount=%lu maxcount=%lu arg_cookie=%"
               PRIu64" cookie=%"PRIu64" estimated_num_entries=%u",
               dircount, maxcount, arg_READDIR4.cookie, cookie,
               estimated_num_entries);

  /* Do not use a cookie of 1 or 2 (reserved values) */
  if(cookie == 1 || cookie == 2)
    {
      res_READDIR4.status = NFS4ERR_BAD_COOKIE;
      return res_READDIR4.status;
    }

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access_Bitmap(&arg_READDIR4.attr_request,
                                     FATTR4_ATTR_READ))
    {
      res_READDIR4.status = NFS4ERR_INVAL;
      return res_READDIR4.status;
    }

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /*

   * If cookie verifier is used, then an non-trivial value is
   * returned to the client         This value is the mtime of
   * the pentry. If verifier is unused (as in many NFS
   * Servers) then only a set of zeros is returned (trivial
   * value) 
   */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
  if(data->pexport->UseCookieVerifier == 1)
    memcpy(cookie_verifier, &dir_pentry->internal_md.mod_time, sizeof(time_t));

  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and
   * HPSS cookie */

  if((cookie != 0) && (data->pexport->UseCookieVerifier == 1))
    {
      if(memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE) != 0)
        {

          res_READDIR4.status = NFS4ERR_BAD_COOKIE;
          return res_READDIR4.status;
        }
    }

  /* The default behaviour is to consider that eof is not reached, the
   * returned values by cache_inode_readdir will let us know if eod was
   * reached or not */
  res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;

  /* Get prepared for readdir */
  if((dirent_array =
      (cache_inode_dir_entry_t **) Mem_Alloc(
          estimated_num_entries * sizeof(cache_inode_dir_entry_t*))) == NULL)
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      goto out;
    }
  
  /* Perform the readdir operation */
  if(cache_inode_readdir(dir_pentry,
                         data->pexport->cache_inode_policy,
                         cookie,
                         estimated_num_entries,
                         &num_entries,
                         &end_cookie,
                         &eod_met,
                         dirent_array,
                         data->ht,
                         &dir_pentry_unlock,
                         data->pclient,
                         data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_READDIR4.status = nfs4_Errno(cache_status);
      goto out;
    }

  /* For an empty directory, we will find only . and .., so reply as if the
   * end is reached */
  if(num_entries == 0)
    {
      /* only . and .. */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }
  else
    {
      /* Start computing the outbuffsize */
      outbuffsize = sizeof( bool_t) /* eof */ 
                  + sizeof( nfsstat4 ) /* READDIR4res::status */
                  + NFS4_VERIFIER_SIZE /* cookie verifier */ ;

      /* Allocation of reply structures */
      if((entry_name_array =
          (entry_name_array_item_t *) Mem_Alloc(num_entries *
                                                (FSAL_MAX_NAME_LEN + 1)))
         == NULL)
        {
          LogError(COMPONENT_NFS_V4, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }
      memset((char *)entry_name_array, 0,
             num_entries * (FSAL_MAX_NAME_LEN + 1));

      if((entry_nfs_array = (entry4 *) Mem_Alloc(num_entries * sizeof(entry4))) 
         == NULL)
        {
          LogError(COMPONENT_NFS_V4, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      for(i = 0; i < num_entries; i++) 
        {
          entry_nfs_array[i].name.utf8string_val = entry_name_array[i];

          if(str2utf8(dirent_array[i]->name.name,
                      &entry_nfs_array[i].name) == -1)
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              goto out;
            }

          /* Set the cookie value */
          entry_nfs_array[i].cookie = dirent_array[i]->cookie;

          /* Get the pentry for the object's attributes and filehandle */
          if( ( pentry = cache_inode_lookup_no_mutex( dir_pentry,
                                                      &dirent_array[i]->name,
                                                      data->pexport->cache_inode_policy,
                                                      &attrlookup,
                                                      data->ht,
                                                      data->pclient,
                                                      data->pcontext,
                                                      &cache_status ) ) == NULL )
            {
              Mem_Free((char *)entry_nfs_array);
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              goto out;
            }

          /* If file handle is asked in the attributes, provide it */
          if(arg_READDIR4.attr_request.bitmap4_val != NULL
             && (arg_READDIR4.attr_request.bitmap4_val[0] & FATTR4_FILEHANDLE))
            {
              if((entry_FSALhandle =
                  cache_inode_get_fsal_handle(pentry,
                                              &cache_status_attr)) == NULL)
                {
                  /* Faulty Handle or pentry */
                  Mem_Free((char *)entry_nfs_array);
                  res_READDIR4.status = NFS4ERR_SERVERFAULT;
                  goto out;
                }

              if(!nfs4_FSALToFhandle(&entryFH, entry_FSALhandle, data))
                {
                  /* Faulty type */
                  Mem_Free((char *)entry_nfs_array);
                  res_READDIR4.status = NFS4ERR_SERVERFAULT;
                  goto out;
                }
            }

          if(nfs4_FSALattr_To_Fattr(data->pexport,
                                    &attrlookup,
                                    &(entry_nfs_array[i].attrs),
                                    data, &entryFH, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }

          /* Update the size of the output buffer */
          entrysize = sizeof( nfs_cookie4 ) ; /* nfs_cookie4 */
          entrysize += sizeof( u_int ) ; /* pathname4::utf8strings_len */
          entrysize +=  entry_nfs_array[i].name.utf8string_len ; 
          entrysize += sizeof( u_int ) ; /* bitmap4_len */
          entrysize +=  entry_nfs_array[i].attrs.attrmask.bitmap4_len ;
          entrysize += sizeof( u_int ) ; /* attrlist4_len */
          entrysize +=  entry_nfs_array[i].attrs.attr_vals.attrlist4_len ;
          entrysize += sizeof( caddr_t ) ;
          outbuffsize += entrysize;

          LogFullDebug(COMPONENT_NFS_V4,
                  " === nfs4_op_readdir ===>   i=%u name=%s cookie=%"PRIu64" "
                  "entrysize=%u buffsize=%u",
                  i, dirent_array[i]->name.name,
                  entry_nfs_array[i].cookie,
                  entrysize,
                  outbuffsize);

          /* Chain the entries together */
          entry_nfs_array[i].nextentry = NULL;
          if(i != 0)
           {
              if( outbuffsize < maxcount )
                entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);
              else
               {
                   LogFullDebug(COMPONENT_NFS_V4,
                           "=== nfs4_op_readdir ===> "
                           "maxcount reached at %u entries name=%s "
                           "cookie=%llu "
                           "buffsize=%u (return early)",
                           i+1, 
                           dirent_array[i]->name.name,
                           (unsigned long long)entry_nfs_array[i].cookie,
                           outbuffsize);
                 entry_nfs_array[i - 1].nextentry = NULL ;
                 break ;
               }
           }
        }                       /* for i */

      if((i == num_entries) && (eod_met == END_OF_DIR))
      {

          LogFullDebug(COMPONENT_NFS_V4,
                  "End of directory reached:  num_entries=%d i=%d",
                  num_entries,
                  i);

          /* This is the end of the directory */
          res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
          memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf,
                 cookie_verifier, NFS4_VERIFIER_SIZE);
      }

      /* Put the entry's list in the READDIR reply */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;
    }

  /* Do not forget to set the verifier */
  memcpy((char *)res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);

  res_READDIR4.status = NFS4_OK;

out:
  /* release read lock on dir_pentry, if requested */
  if (dir_pentry_unlock)
      V_r(&dir_pentry->lock);

  if (dirent_array)
   {
      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
        cache_inode_release_dirent( dirent_array, num_entries, data->pclient ) ;

      Mem_Free((char *)dirent_array);
   }

  return res_READDIR4.status;
}                               /* nfs4_op_readdir */
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
void nfs4_op_readdir_Free(READDIR4res * resp)
{
  entry4 *entries;

  if(resp->status == NFS4_OK)
    {
      for(entries = resp->READDIR4res_u.resok4.reply.entries; entries != NULL;
          entries = entries->nextentry)
        {
          Mem_Free((char *)entries->attrs.attrmask.bitmap4_val);
          /** @todo Fixeme , bad Free here Mem_Free( (char *)entries->attrs.attr_vals.attrlist4_val ) ; */
        }

      if(resp->READDIR4res_u.resok4.reply.entries != NULL)
        {
          Mem_Free((char *)resp->READDIR4res_u.resok4.reply.entries[0].name.
                   utf8string_val);
          Mem_Free((char *)resp->READDIR4res_u.resok4.reply.entries);
        }
    }

  return;
}                               /* nfs4_op_readdir_Free */
