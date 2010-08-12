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
 * ---------------------------------------
 */

/**
 * \file    nfs3_Readdirplus.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.37 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Readdirplus.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <sys/file.h>           /* for having FNDELAY */
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
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Readdir: The NFS PROC2 and PROC3 READDIR
 *
 * Implements the NFS PROC CREATE function (for V2 and V3).
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs3_Readdirplus(nfs_arg_t * parg,
                     exportlist_t * pexport,
                     fsal_op_context_t * pcontext,
                     cache_inode_client_t * pclient,
                     hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs3_Readdirplus";

  typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN];
  typedef char fh3_buffer_item_t[NFS3_FHSIZE];

  unsigned int delta = 0;
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *pentry_dot_dot = NULL;
  unsigned long dircount;
  unsigned long maxcount;
  fsal_attrib_list_t dir_attr;
  fsal_attrib_list_t entry_attr;
  unsigned int begin_cookie;
  unsigned int end_cookie;
  unsigned int cache_inode_cookie;
  cache_inode_dir_entry_t *dirent_array = NULL;
  unsigned int *cookie_array = NULL;
  cookieverf3 cookie_verifier;
  int rc;
  unsigned int i = 0;
  unsigned int num_entries;
  unsigned long space_used;
  unsigned long estimated_num_entries;
  unsigned long asked_num_entries;
  cache_inode_file_type_t dir_filetype;
  cache_inode_endofdir_t eod_met = UNASSIGNED_EOD;
  cache_inode_status_t cache_status;
  cache_inode_status_t cache_status_gethandle;
  fsal_handle_t *pfsal_handle = NULL;
  entry_name_array_item_t *entry_name_array = NULL;
  fh3_buffer_item_t *fh3_array = NULL;

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdirplus3.dircount;
  maxcount = parg->arg_readdirplus3.maxcount;
  begin_cookie = (unsigned int)parg->arg_readdirplus3.cookie;
  space_used = sizeof(READDIRPLUS3resok);
  estimated_num_entries = dircount / sizeof(entryplus3);

  LogFullDebug(COMPONENT_NFS_READDIR,
      "---> nfs3_Readdirplus: dircount=%d  maxcount=%d  begin_cookie=%d  space_used=%d  estimated_num_entries=%d\n",
       dircount, maxcount, begin_cookie, space_used, estimated_num_entries);

  /* Is this a xattr FH ? */
  if(nfs3_Is_Fh_Xattr(&(parg->arg_readdirplus3.dir)))
    return nfs3_Readdirplus_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  /* Convert file handle into a vnode */
  /* BUGAZOMEU : rajouter acces direct au DIR_CONTINUE */
  if((dir_pentry = nfs_FhandleToCache(preq->rq_vers,
                                      NULL,
                                      &(parg->arg_readdirplus3.dir),
                                      NULL,
                                      NULL,
                                      &(pres->res_readdirplus3.status),
                                      NULL,
                                      &dir_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* return NFS_REQ_DROP ; */
      return rc;
    }

  /* Extract the filetype */
  dir_filetype = cache_inode_fsal_type_convert(dir_attr.type);

  /* Sanity checks -- must be a directory */

  if((dir_filetype != DIR_BEGINNING) && (dir_filetype != DIR_CONTINUE))
    {
      pres->res_readdirplus3.status = NFS3ERR_NOTDIR;
      return NFS_REQ_OK;
    }

  /* switch */
  memset(cookie_verifier, 0, sizeof(cookieverf3));

  /*
   * If cookie verifier is used, then an non-trivial value is
   * returned to the client         This value is the mtime of
   * the directory. If verifier is unused (as in many NFS
   * Servers) then only a set of zeros is returned (trivial
   * value) 
   */

  if(pexport->UseCookieVerifier)
    memcpy(cookie_verifier, &(dir_attr.mtime), sizeof(dir_attr.mtime));

  /*
   * nothing to do if != 0 because the area is already full of
   * zero 
   */

  if(pexport->UseCookieVerifier && (begin_cookie != 0))
    {
      /*
       * Not the first call, so we have to check the cookie
       * verifier 
       */
      if(memcmp(cookie_verifier, parg->arg_readdirplus3.cookieverf, NFS3_COOKIEVERFSIZE)
         != 0)
        {
          pres->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;

          return NFS_REQ_OK;
        }
    }
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("cache_inode_dir_entry_t in nfs3_Readdirplus");
#endif

  if((dirent_array =
      (cache_inode_dir_entry_t *) Mem_Alloc(estimated_num_entries *
                                            sizeof(cache_inode_dir_entry_t))) == NULL)
    {
      pres->res_readdirplus3.status = NFS3ERR_IO;
      return NFS_REQ_DROP;
    }
#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("cookie array in nfs3_Readdirplus");
#endif

  if((cookie_array =
      (unsigned int *)Mem_Alloc(estimated_num_entries * sizeof(unsigned int))) == NULL)
    {
      Mem_Free((char *)dirent_array);
      pres->res_readdirplus3.status = NFS3ERR_IO;
      return NFS_REQ_DROP;
    }

  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

  /* How many entries will we retry from cache_inode ? */
  if(begin_cookie > 1)
    {
      asked_num_entries = estimated_num_entries;
      cache_inode_cookie = begin_cookie - 2;
    }
  else
    {
      asked_num_entries = ((estimated_num_entries > 2) ? estimated_num_entries - 2 : 0);        /* Keep space for '.' and '..' */
      cache_inode_cookie = 0;
    }

  /* A definition that will be very useful to avoid very long names for variables */
#define RES_READDIRPLUS_REPLY pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply

  /* Call readdir */
  if(cache_inode_readdir(dir_pentry,
                         cache_inode_cookie,
                         asked_num_entries,
                         &num_entries,
                         &end_cookie,
                         &eod_met,
                         dirent_array,
                         cookie_array,
                         ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NFS_READDIR,
          "-- Readdirplus3 -> Call to cache_inode_readdir( cookie=%d, asked=%d ) -> num_entries = %d\n",
           cache_inode_cookie, asked_num_entries, num_entries);

      if(eod_met == END_OF_DIR)
        {
          LogFullDebug(COMPONENT_NFS_READDIR, "+++++++++++++++++++++++++++++++++++++++++> EOD MET \n");
        }

      /* If nothing was found, return nothing, but if cookie=0, we should return . and .. */
      if((num_entries == 0) && (asked_num_entries != 0) && (begin_cookie > 1))
        {
          pres->res_readdirplus3.status = NFS3_OK;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpAttr(pcontext, pexport,
                            dir_pentry,
                            NULL,
                            &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                              dir_attributes));

          memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
        }
      else
        {
#ifdef _DEBUG_MEMLEAKS
          /* For debugging memory leaks */
          BuddySetDebugLabel("entry_name_array in nfs3_Readdirplus");
#endif
          /* Allocation of the structure for reply */
          entry_name_array =
              (entry_name_array_item_t *) Mem_Alloc(estimated_num_entries *
                                                    (FSAL_MAX_NAME_LEN + 1));

          if(entry_name_array == NULL)
            {
              Mem_Free((char *)dirent_array);
              Mem_Free((char *)cookie_array);
              return NFS_REQ_DROP;
            }
#ifdef _DEBUG_MEMLEAKS
          /* For debugging memory leaks */
          BuddySetDebugLabel("READDIRPLUS3res_u.resok.reply.entries");
#endif
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
              (entryplus3 *) Mem_Alloc(estimated_num_entries * sizeof(entryplus3));

          if(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries == NULL)
            {
              Mem_Free((char *)dirent_array);
              Mem_Free((char *)cookie_array);
              Mem_Free((char *)entry_name_array);
              return NFS_REQ_DROP;
            }

          /* Allocation of the file handles */
#ifdef _DEBUG_MEMLEAKS
          /* For debugging memory leaks */
          BuddySetDebugLabel("Filehandle V3 in nfs3_Readdirplus");
#endif
          fh3_array =
              (fh3_buffer_item_t *) Mem_Alloc(estimated_num_entries * NFS3_FHSIZE);

#ifdef _DEBUG_MEMLEAKS
          /* For debugging memory leaks */
          BuddySetDebugLabel("N/A");
#endif

          if(fh3_array == NULL)
            {
              Mem_Free((char *)dirent_array);
              Mem_Free((char *)cookie_array);
              Mem_Free((char *)entry_name_array);

              return NFS_REQ_DROP;
            }

          delta = 0;

          /* manage . and .. */
          if(begin_cookie == 0)
            {
              /* Fill in '.' */
              if(estimated_num_entries > 0)
                {
                  if((pfsal_handle = cache_inode_get_fsal_handle(dir_pentry,
                                                                 &cache_status_gethandle))
                     == NULL)
                    {
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = nfs3_Errno(cache_status_gethandle);
                      return NFS_REQ_OK;
                    }

                  FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                    FSAL_DIGEST_FILEID3,
                                    pfsal_handle,
                                    (caddr_t) & (RES_READDIRPLUS_REPLY.entries[0].
                                                 fileid));

                  RES_READDIRPLUS_REPLY.entries[0].name = entry_name_array[0];
                  strcpy(RES_READDIRPLUS_REPLY.entries[0].name, ".");

                  RES_READDIRPLUS_REPLY.entries[0].cookie = 1;

                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[0].
                      name_handle.post_op_fh3_u.handle.data.data_val =
                      (char *)fh3_array[0];

                  if(nfs3_FSALToFhandle
                     (&pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[0].
                      name_handle.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
                    {
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = NFS3ERR_BADHANDLE;
                      return NFS_REQ_OK;
                    }

                  RES_READDIRPLUS_REPLY.entries[0].name_attributes.attributes_follow =
                      FALSE;
                  RES_READDIRPLUS_REPLY.entries[0].name_handle.handle_follows = FALSE;

                  cache_inode_get_attributes(dir_pentry, &entry_attr);

                  /* Set PostPoFh3 structure */
                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[0].
                      name_handle.handle_follows = TRUE;
                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[0].
                      name_handle.post_op_fh3_u.handle.data.data_len =
                      sizeof(file_handle_v3_t);

                  nfs_SetPostOpAttr(pcontext, pexport,
                                    dir_pentry,
                                    &entry_attr,
                                    &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                                      reply.entries[0].name_attributes));

                  delta += 1;
                }

            }

          /* Fill in '..' */
          if(begin_cookie <= 1)
            {
              if(estimated_num_entries > delta)
                {
                  if((pentry_dot_dot = cache_inode_lookupp(dir_pentry,
                                                           ht,
                                                           pclient,
                                                           pcontext,
                                                           &cache_status_gethandle)) ==
                     NULL)
                    {
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = nfs3_Errno(cache_status_gethandle);
                      return NFS_REQ_OK;
                    }

                  if((pfsal_handle = cache_inode_get_fsal_handle(pentry_dot_dot,
                                                                 &cache_status_gethandle))
                     == NULL)
                    {
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = nfs3_Errno(cache_status_gethandle);
                      return NFS_REQ_OK;
                    }

                  FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                    FSAL_DIGEST_FILEID3,
                                    pfsal_handle,
                                    (caddr_t) & (RES_READDIRPLUS_REPLY.entries[delta].
                                                 fileid));

                  RES_READDIRPLUS_REPLY.entries[delta].name = entry_name_array[delta];
                  strcpy(RES_READDIRPLUS_REPLY.entries[delta].name, "..");

                  /* Getting a file handle */
                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[delta].
                      name_handle.post_op_fh3_u.handle.data.data_val =
                      (char *)fh3_array[delta];

                  if(nfs3_FSALToFhandle
                     (&pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[0].
                      name_handle.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
                    {
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = NFS3ERR_BADHANDLE;
                      return NFS_REQ_OK;
                    }

                  RES_READDIRPLUS_REPLY.entries[delta].cookie = 2;

                  RES_READDIRPLUS_REPLY.entries[delta].name_attributes.attributes_follow =
                      FALSE;
                  RES_READDIRPLUS_REPLY.entries[delta].name_handle.handle_follows = FALSE;

                  cache_inode_get_attributes(pentry_dot_dot, &entry_attr);

                  /* Set PostPoFh3 structure */
                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[delta].
                      name_handle.handle_follows = TRUE;
                  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[delta].
                      name_handle.post_op_fh3_u.handle.data.data_len =
                      sizeof(file_handle_v3_t);

                  nfs_SetPostOpAttr(pcontext, pexport,
                                    pentry_dot_dot,
                                    &entry_attr,
                                    &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                                      reply.entries[delta].name_attributes));

                }
              RES_READDIRPLUS_REPLY.entries[0].nextentry =
                  &(RES_READDIRPLUS_REPLY.entries[delta]);

              if(num_entries > delta + 1)       /* not 0 ??? */
                RES_READDIRPLUS_REPLY.entries[delta].nextentry =
                    &(RES_READDIRPLUS_REPLY.entries[delta + 1]);
              else
                RES_READDIRPLUS_REPLY.entries[delta].nextentry = NULL;

              delta += 1;
            }
          /* if( begin_cookie == 0 ) */
          for(i = delta; i < num_entries + delta; i++)
            {
              unsigned long needed;

              /* dircount is the size without the FH and attributes overhead, so entry3 is used intead of entryplus3 */
              needed =
                  sizeof(entry3) + ((strlen(dirent_array[i - delta].name.name) + 3) & ~3);

              /* LogFullDebug(COMPONENT_NFS_READDIR, "==============> i=%d sizeof(entryplus3)=%d needed=%d space_used=%d maxcount=%d num_entries=%d asked_num_entries=%d\n",
                 i, sizeof( entryplus3 ), needed, space_used, maxcount, num_entries, asked_num_entries ) ; */
              if((space_used += needed) > maxcount)
                {
                  if(i == delta)
                    {
                      /*
                       * Not enough room to make even a single reply 
                       */
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)cookie_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = NFS3ERR_TOOSMALL;

                      return NFS_REQ_OK;
                    }
                  break;        /* Make post traitement */
                }

              /*
               * Get information specific to this entry
               */
              if((pfsal_handle =
                  cache_inode_get_fsal_handle(dirent_array[i - delta].pentry,
                                              &cache_status_gethandle)) == NULL)
                {
                  Mem_Free((char *)dirent_array);
                  Mem_Free((char *)cookie_array);
                  Mem_Free((char *)entry_name_array);
                  Mem_Free((char *)fh3_array);

                  pres->res_readdirplus3.status = nfs3_Errno(cache_status_gethandle);
                  return NFS_REQ_OK;
                }

              /* Now fill in the replyed entryplus3 list */
              FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                FSAL_DIGEST_FILEID3,
                                pfsal_handle,
                                (caddr_t) & (RES_READDIRPLUS_REPLY.entries[i].fileid));

              FSAL_name2str(&dirent_array[i - delta].name, entry_name_array[i],
                            FSAL_MAX_NAME_LEN);
              RES_READDIRPLUS_REPLY.entries[i].name = entry_name_array[i];

              if(i != num_entries + delta - 1)
                RES_READDIRPLUS_REPLY.entries[i].cookie = cookie_array[i + 1 - delta] + 2;
              else
                RES_READDIRPLUS_REPLY.entries[i].cookie = end_cookie + 2;

              RES_READDIRPLUS_REPLY.entries[i].name_attributes.attributes_follow = FALSE;
              RES_READDIRPLUS_REPLY.entries[i].name_handle.handle_follows = FALSE;

              cache_inode_get_attributes(dirent_array[i - delta].pentry, &entry_attr);

              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.
                  post_op_fh3_u.handle.data.data_val = (char *)fh3_array[i];

              /* Compute the NFSv3 file handle */
              if(nfs3_FSALToFhandle
                 (&pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].
                  name_handle.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
                {
                  Mem_Free((char *)dirent_array);
                  Mem_Free((char *)cookie_array);
                  Mem_Free((char *)entry_name_array);
                  Mem_Free((char *)fh3_array);

                  pres->res_readdirplus3.status = NFS3ERR_BADHANDLE;
                  return NFS_REQ_OK;
                }
              LogFullDebug(COMPONENT_NFS_READDIR,
                  "-- Readdirplus3 -> i=%d num_entries=%d needed=%d space_used=%lu maxcount=%lu Name=%s FileId=%llu Cookie=%llu\n",
                   i, num_entries, needed, space_used, maxcount,
                   dirent_array[i - delta].name.name,
                   RES_READDIRPLUS_REPLY.entries[i].fileid,
                   RES_READDIRPLUS_REPLY.entries[i].cookie);

              /* Set PostPoFh3 structure */
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.
                  handle_follows = TRUE;
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.
                  post_op_fh3_u.handle.data.data_len = sizeof(file_handle_v3_t);

              nfs_SetPostOpAttr(pcontext, pexport,
                                dirent_array[i - delta].pentry,
                                &entry_attr,
                                &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.
                                  entries[i].name_attributes));

              RES_READDIRPLUS_REPLY.entries[i].nextentry = NULL;
              if(i != 0)
                RES_READDIRPLUS_REPLY.entries[i - 1].nextentry =
                    &(RES_READDIRPLUS_REPLY.entries[i]);
            }

          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;
        }

      nfs_SetPostOpAttr(pcontext,
                        pexport,
                        dir_pentry,
                        &dir_attr,
                        &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.dir_attributes));

      memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
             cookie_verifier, sizeof(cookieverf3));

      pres->res_readdirplus3.status = NFS3_OK;

      if((eod_met == END_OF_DIR) && (i == num_entries + delta))
        {
          /* End of directory */
          LogFullDebug(COMPONENT_NFS_READDIR,
              "============================================================> EOD MET !!!!!!\n");
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;
        }
      else
        pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

      nfs_SetPostOpAttr(pcontext, pexport,
                        dir_pentry,
                        &dir_attr,
                        &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.dir_attributes));
      memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf, cookie_verifier,
             sizeof(cookieverf3));

      LogFullDebug(COMPONENT_NFS_READDIR,"============================================================\n");

      /* Free the memory */
      Mem_Free((char *)dirent_array);
      Mem_Free((char *)cookie_array);

      return NFS_REQ_OK;
    }

  /* If we are here, there was an error */

  /* Free the memory */
  Mem_Free((char *)dirent_array);
  Mem_Free((char *)cookie_array);
  Mem_Free((char *)entry_name_array);
  Mem_Free((char *)fh3_array);

  /* Is this a retryable error */
  if(nfs_RetryableError(cache_status))
    return NFS_REQ_DROP;

  /* Set failed status */
  nfs_SetFailedStatus(pcontext, pexport,
                      NFS_V3,
                      cache_status,
                      NULL,
                      &pres->res_readdirplus3.status,
                      dir_pentry,
                      &(pres->res_readdirplus3.READDIRPLUS3res_u.resfail.dir_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs3_Readdirplus */

/**
 * nfs3_Readdirplus_Free: Frees the result structure allocated for nfs3_Readdirplus.
 * 
 * Frees the result structure allocated for nfs3_Readdirplus.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Readdirplus_Free(nfs_res_t * resp)
{
#define PRESREADDIRPLUSREPLY resp->res_readdirplus3.READDIRPLUS3res_u.resok.reply
  if((resp->res_readdirplus3.status == NFS3_OK) && (PRESREADDIRPLUSREPLY.entries != NULL))
    {
      /* All is allocated as a single array */
      Mem_Free(PRESREADDIRPLUSREPLY.entries[0].name);
      Mem_Free(PRESREADDIRPLUSREPLY.entries[0].name_handle.post_op_fh3_u.handle.data.
               data_val);
      Mem_Free(PRESREADDIRPLUSREPLY.entries);
    }
}                               /*  nfs3_Readdirplus_Free */
