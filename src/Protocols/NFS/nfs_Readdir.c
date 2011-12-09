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
 * \file    nfs_Readdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Readdir.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_tools.h"
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

int nfs_Readdir(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Readdir";

  cache_entry_t *dir_pentry;
  cache_entry_t *pentry_dot_dot = NULL;
  unsigned long count = 0;
  fsal_attrib_list_t dir_attr;
  uint64_t cookie = 0;
  uint64_t cache_inode_cookie;
  uint64_t end_cookie;
  cache_inode_dir_entry_t **dirent_array;
  cookieverf3 cookie_verifier;
  int rc;
  unsigned int num_entries;
  unsigned long space_used = 0;
  unsigned long estimated_num_entries = 0;
  unsigned long asked_num_entries;
  cache_inode_file_type_t dir_filetype;
  cache_inode_endofdir_t eod_met;
  cache_inode_status_t cache_status;
  cache_inode_status_t cache_status_gethandle;
  fsal_handle_t *pfsal_handle;
  int dir_pentry_unlock = FALSE;
  unsigned int delta = 0;
  unsigned int i = 0;

  if(isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR))
    {
      char str[LEN_FH_STR];
      log_components_t component;
      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_readdir2.dir),
                       &(parg->arg_readdir3.dir),
                       NULL,
                       str);
      if(isDebug(COMPONENT_NFSPROTO))
        component = COMPONENT_NFSPROTO;
      else
        component = COMPONENT_NFS_READDIR;
      LogDebug(component,
               "REQUEST PROCESSING: Calling nfs_Readdir handle: %s", str);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;
    }

  /* Convert file handle into a vnode */
  /* BUGAZOMEU : rajouter acces direct au DIR_CONTINUE */
  if((dir_pentry = nfs_FhandleToCache(preq->rq_vers,
                                      &(parg->arg_readdir2.dir),
                                      &(parg->arg_readdir3.dir),
                                      NULL,
                                      &(pres->res_readdir2.status),
                                      &(pres->res_readdir3.status),
                                      NULL,
                                      &dir_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_readdir3.dir))))
    return nfs3_Readdir_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  switch (preq->rq_vers)
    {
    case NFS_V2:
      count = parg->arg_readdir2.count;
      memcpy((char *)&cookie, (char *)parg->arg_readdir2.cookie, NFS2_COOKIESIZE);
      space_used = sizeof(READDIR2resok);
      estimated_num_entries = count / sizeof(entry2);

      LogFullDebug(COMPONENT_NFS_READDIR,
                   "-- Readdir2 -> count=%lu  cookie = %"PRIu64"  "
                   "estimated_num_entries=%lu", count, cookie,
                   estimated_num_entries);

      if(estimated_num_entries == 0)
        {
          pres->res_readdir2.status = NFSERR_IO;
          return NFS_REQ_OK;
        }

      break;

    case NFS_V3:
      {
        count = parg->arg_readdir3.count;
        cookie = parg->arg_readdir3.cookie;
        space_used = sizeof(READDIR3resok);
        estimated_num_entries = count / sizeof(entry3);

        LogFullDebug(COMPONENT_NFS_READDIR,
                     "---> nfs3_Readdir: count=%lu  cookie=%"PRIu64"  "
                     "space_used=%lu  estimated_num_entries=%lu",
                     count, cookie, space_used, estimated_num_entries);

        if(estimated_num_entries == 0)
          {
            pres->res_readdir3.status = NFS3ERR_TOOSMALL;
            return NFS_REQ_OK;
          }

        /* To make or check the cookie verifier */
        memset(cookie_verifier, 0, sizeof(cookieverf3));

        /*
         * If cookie verifier is used, then an
         * non-trivial value is returned to the
         * client         This value is the mtime of
         * the directory. If verifier is unused (as
         * in many NFS Servers) then only a set of
         * zeros is returned (trivial value)
         */

        if(pexport->UseCookieVerifier)
          memcpy(cookie_verifier, &(dir_attr.mtime), sizeof(dir_attr.mtime));
        /*
         * nothing to do if != 0 because the area is
         * already full of zero
         */

        if((cookie != 0) && (pexport->UseCookieVerifier))
          {
            /*
             * Not the first call, so we have to
             * check the cookie verifier
             */
            if(memcmp
               (cookie_verifier, parg->arg_readdir3.cookieverf, NFS3_COOKIEVERFSIZE) != 0)
              {
                pres->res_readdir3.status = NFS3ERR_BAD_COOKIE;
                return NFS_REQ_OK;
              }
          }
        else
          {
            /*
             * This cookie verifier will always produce
             * errors if it is used by the client
             */
            memset(cookie_verifier, 0xFF, sizeof(cookieverf3));
          }
        /*
         * At thist point we ignore errors, the next vnode
         * call will fail and we will return the error
         */
      }

      break;
    }                           /* switch(preq->pq_vers ) */

  /* Extract the filetype */
  dir_filetype = cache_inode_fsal_type_convert(dir_attr.type);

  /* Sanity checks -- must be a directory */

  if(dir_filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /*
           * In the RFC tell it not good but it does
           * not tell what to do ...
           */
          pres->res_readdir2.status = NFSERR_NOTDIR;
          break;

        case NFS_V3:
          pres->res_readdir3.status = NFS3ERR_NOTDIR;
          break;
        }                       /* switch */

      return NFS_REQ_OK;
    }

  dirent_array =
      (cache_inode_dir_entry_t **) Mem_Alloc_Label(
          estimated_num_entries * sizeof(cache_inode_dir_entry_t*),
          "cache_inode_dir_entry_t in nfs_Readdir");

  if(dirent_array == NULL)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /*
           * In the RFC tell it not good but it does
           * not tell what to do ...
           */
          pres->res_readdir2.status = NFSERR_IO;
          break;

        case NFS_V3:
          pres->res_readdir3.status = NFS3ERR_IO;
          break;
        }                       /* switch */
      return NFS_REQ_DROP;
    }

  /* How many entries will we retry from cache_inode ? */

  if(cookie > 1)                /* it is not the cookie for "." nor ".." */
    {
      asked_num_entries = estimated_num_entries;
      cache_inode_cookie = cookie;
    }
  else
    {

      /* keep space for "." and ".." */

      /* cookie 0 must return "." as first entry (and keep 2 slots for . and ..)
       * cookie 1 must return ".." as first entry (and keep 1 slot for ..)
       */

      asked_num_entries =
          (estimated_num_entries > 2 - cookie ? estimated_num_entries + cookie - 2 : 0);
      cache_inode_cookie = 0;
    }

/* Some definitions that will be very useful to avoid very long names for variables */
#define RES_READDIR2_OK   pres->res_readdir2.READDIR2res_u.readdirok
#define RES_READDIR3_OK   pres->res_readdir3.READDIR3res_u.resok
#define RES_READDIR3_FAIL pres->res_readdir3.READDIR3res_u.resfail

  /* Call readdir */
  if(cache_inode_readdir(dir_pentry,
                         pexport->cache_inode_policy,
                         cache_inode_cookie,
                         asked_num_entries,
                         &num_entries,
                         &end_cookie,
                         &eod_met,
                         dirent_array,
                         ht,
                         &dir_pentry_unlock,
                         pclient,
                         pcontext,
                         &cache_status) == CACHE_INODE_SUCCESS)
    {

      LogFullDebug(COMPONENT_NFS_READDIR,
                   "-- Readdir -> Call to cache_inode_readdir( cookie=%"PRIu64
                   ", asked=%lu ) -> num_entries = %u",
                   cache_inode_cookie, asked_num_entries, num_entries);

      if(eod_met == END_OF_DIR)
        {
          LogFullDebug(COMPONENT_NFS_READDIR,
                       "+++++++++++++++++++++++++++++++++++++++++> EOD MET ");
        }

      /* If nothing was found, return nothing, but if cookie <= 1, we should return . and .. */
      if((num_entries == 0) && (asked_num_entries != 0) && (cookie > 1))
        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_readdir2.status = NFS_OK;
              RES_READDIR2_OK.entries = NULL;
              RES_READDIR2_OK.eof = TRUE;
              break;

            case NFS_V3:
              pres->res_readdir3.status = NFS3_OK;
              RES_READDIR3_OK.reply.entries = NULL;
              RES_READDIR3_OK.reply.eof = TRUE;
              nfs_SetPostOpAttr(pcontext, pexport,
                                dir_pentry, &dir_attr, &(RES_READDIR3_OK.dir_attributes));
              memcpy(RES_READDIR3_OK.cookieverf, cookie_verifier, sizeof(cookieverf3));
              break;
            }
        }
      else
        {
          typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN];
          entry_name_array_item_t *entry_name_array;

          entry_name_array =
              (entry_name_array_item_t *) Mem_Alloc_Label(estimated_num_entries *
                                                          (FSAL_MAX_NAME_LEN + 1),
                                                          "entry_name_array in nfs_Readdir");
          if(entry_name_array == NULL)
            {
                /* after successful cache_inode_readdir, dir_pentry may be
                 * read locked */
                if (dir_pentry_unlock)
                    V_r(&dir_pentry->lock);

              if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
                cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
              Mem_Free(dirent_array);
              return NFS_REQ_DROP;
            }

          switch (preq->rq_vers)
            {
            case NFS_V2:

              RES_READDIR2_OK.entries =
                  (entry2 *) Mem_Alloc_Label(
                      estimated_num_entries * sizeof(entry2),
                      "RES_READDIR2_OK.entries");

              if(RES_READDIR2_OK.entries == NULL)
                {
                    /* after successful cache_inode_readdir, dir_pentry may be
                     * read locked */
                    if (dir_pentry_unlock)
                        V_r(&dir_pentry->lock);

                  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                    cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                  Mem_Free(dirent_array);
                  Mem_Free(entry_name_array);
                  return NFS_REQ_DROP;
                }

              delta = 0;

              /* fills "."  */

              if(cookie == 0)
                {
                  if(estimated_num_entries > 0)
                    {
                      if((pfsal_handle = cache_inode_get_fsal_handle(dir_pentry,
                                                                     &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir2.status = nfs2_Errno(cache_status_gethandle);
                          return NFS_REQ_OK;
                        }

                      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                        FSAL_DIGEST_FILEID2,
                                        pfsal_handle,
                                        (caddr_t) & (RES_READDIR2_OK.entries[0].fileid));

                      RES_READDIR2_OK.entries[0].name = entry_name_array[0];
                      strcpy(RES_READDIR2_OK.entries[0].name, ".");

                      *(RES_READDIR2_OK.entries[0].cookie) = 1;

                      /* pointer to next entry ( if any ) */

                      if(estimated_num_entries > 1)
                        RES_READDIR2_OK.entries[0].nextentry =
                            &(RES_READDIR2_OK.entries[1]);
                      else
                        RES_READDIR2_OK.entries[0].nextentry = NULL;

                      delta++;
                    }

                }

              /* fills .. */

              if(cookie <= 1)
                {
                  /* take care of the place taken by "." entry */
                  if(estimated_num_entries > delta)
                    {
                      /* get parent pentry */

                      if((pentry_dot_dot = cache_inode_lookupp(dir_pentry,
                                                               ht,
                                                               pclient,
                                                               pcontext,
                                                               &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir2.status = nfs2_Errno(cache_status_gethandle);
                          return NFS_REQ_OK;
                        }

                      /* get parent handle */

                      if((pfsal_handle = cache_inode_get_fsal_handle(pentry_dot_dot,
                                                                     &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir2.status = nfs2_Errno(cache_status_gethandle);
                          return NFS_REQ_OK;
                        }

                      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                        FSAL_DIGEST_FILEID2,
                                        pfsal_handle,
                                        (caddr_t) & (RES_READDIR2_OK.entries[delta].
                                                     fileid));

                      RES_READDIR2_OK.entries[delta].name = entry_name_array[delta];
                      strcpy(RES_READDIR2_OK.entries[delta].name, "..");

                      *(RES_READDIR2_OK.entries[delta].cookie) = 2;

                      /* pointer to next entry ( if any ) */

                      if((num_entries > 0) && (estimated_num_entries > delta + 1))
                        RES_READDIR2_OK.entries[delta].nextentry =
                            &(RES_READDIR2_OK.entries[delta + 1]);
                      else
                        RES_READDIR2_OK.entries[delta].nextentry = NULL;

                      delta++;
                    }
                }

              /* fill dir entries */

              for(i = delta; i < num_entries + delta; i++)
                {
                  unsigned long needed;

                  needed =
                      sizeof(entry2) +
                      ((strlen(dirent_array[i - delta]->name.name) + 3) & ~3);

                  if((space_used += needed) > count)
                    {
                      if(i == delta)
                        {
                          /*
                           * Not enough room to make even 1 reply
                           */
                          pres->res_readdir2.status = NFSERR_IO;

                          /* after successful cache_inode_readdir, dir_pentry
                           * may be read locked */
                          if (dir_pentry_unlock)
                              V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);
                          return NFS_REQ_OK;
                        }
                      break;
                    }
                  FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                    FSAL_DIGEST_FILEID2,
                                    cache_inode_get_fsal_handle(
                                        dirent_array[i - delta]->pentry,
                                        &cache_status_gethandle),
                                    (caddr_t) & (RES_READDIR2_OK.entries[i].fileid));

                  FSAL_name2str(&dirent_array[i - delta]->name,
                                entry_name_array[i],
                                FSAL_MAX_NAME_LEN);
                  RES_READDIR2_OK.entries[i].name = entry_name_array[i];

                  /* Set cookie :
                   * If we are not at last returned dirent, the cookie is the
                   * index of the next p_entry.
                   * Else, the cookie is the end_cookie.
                   */

                  if(i != num_entries + delta - 1)
                    *(RES_READDIR2_OK.entries[i].cookie) =
                        dirent_array[i - delta]->cookie;
                  else
                    *(RES_READDIR2_OK.entries[i].cookie) = end_cookie;

                  RES_READDIR2_OK.entries[i].nextentry = NULL;
                  if(i != 0)
                    RES_READDIR2_OK.entries[i - 1].nextentry =
                        &(RES_READDIR2_OK.entries[i]);

                }               /* for */

              RES_READDIR2_OK.eof = FALSE;      /* the actual value will be set in post treatments */
              pres->res_readdir2.status = NFS_OK;
              break;

            case NFS_V3:

              RES_READDIR3_OK.reply.entries =
                  (entry3 *) Mem_Alloc_Label(estimated_num_entries * sizeof(entry3),
                                             "RES_READDIR3_OK.reply.entries");

              if(RES_READDIR3_OK.reply.entries == NULL)
                {
                    /* after successful cache_inode_readdir, dir_pentry
                     * may be read locked */
                    if (dir_pentry_unlock)
                        V_r(&dir_pentry->lock);

                  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
                   cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                  Mem_Free(dirent_array);
                  Mem_Free(entry_name_array);
                  return NFS_REQ_DROP;
                }

              delta = 0;

              /* fill "." */

              if(cookie == 0)
                {
                  if(estimated_num_entries > 0)
                    {
                      if((pfsal_handle = cache_inode_get_fsal_handle(dir_pentry,
                                                                     &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
                              cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir3.status = nfs3_Errno(cache_status_gethandle);

                          /* could not retrieve dir pentry, so we cannot return its attributes */
                          RES_READDIR3_FAIL.dir_attributes.attributes_follow = 0;

                          return NFS_REQ_OK;
                        }

                      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                        FSAL_DIGEST_FILEID3,
                                        pfsal_handle,
                                        (caddr_t) & (RES_READDIR3_OK.reply.entries[0].
                                                     fileid));

                      RES_READDIR3_OK.reply.entries[0].name = entry_name_array[0];
                      strcpy(RES_READDIR3_OK.reply.entries[0].name, ".");

                      RES_READDIR3_OK.reply.entries[0].cookie = 1;

                      /* pointer to next entry ( if any ) */

                      if(estimated_num_entries > 1)
                        RES_READDIR3_OK.reply.entries[0].nextentry =
                            &(RES_READDIR3_OK.reply.entries[1]);
                      else
                        RES_READDIR3_OK.reply.entries[0].nextentry = NULL;

                      delta++;
                    }

                }

              /* fills .. */

              if(cookie <= 1)
                {
                  /* take care of the place taken by "." entry */
                  if(estimated_num_entries > delta)
                    {
                      /* get parent pentry */

                      if((pentry_dot_dot = cache_inode_lookupp(dir_pentry,
                                                               ht,
                                                               pclient,
                                                               pcontext,
                                                               &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir3.status = nfs3_Errno(cache_status_gethandle);

                          /* unexpected error, we don't return attributes */
                          RES_READDIR3_FAIL.dir_attributes.attributes_follow = 0;
                          return NFS_REQ_OK;
                        }

                      /* get parent handle */

                      if((pfsal_handle = cache_inode_get_fsal_handle(pentry_dot_dot,
                                                                     &cache_status_gethandle))
                         == NULL)
                        {
                            /* after successful cache_inode_readdir, dir_pentry
                             * may be read locked */
                            if (dir_pentry_unlock)
                                V_r(&dir_pentry->lock);

                          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                            cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);

                          pres->res_readdir3.status = nfs3_Errno(cache_status_gethandle);

                          /* unexpected error, we don't return attributes */
                          RES_READDIR3_FAIL.dir_attributes.attributes_follow = 0;
                          return NFS_REQ_OK;
                        }

                      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                        FSAL_DIGEST_FILEID3,
                                        pfsal_handle,
                                        (caddr_t) & (RES_READDIR3_OK.reply.entries[delta].
                                                     fileid));

                      RES_READDIR3_OK.reply.entries[delta].name = entry_name_array[delta];
                      strcpy(RES_READDIR3_OK.reply.entries[delta].name, "..");

                      RES_READDIR3_OK.reply.entries[delta].cookie = 2;

                      /* pointer to next entry ( if any ) */

                      if((num_entries > 0) && (estimated_num_entries > delta + 1))
                        RES_READDIR3_OK.reply.entries[delta].nextentry =
                            &(RES_READDIR3_OK.reply.entries[delta + 1]);
                      else
                        RES_READDIR3_OK.reply.entries[delta].nextentry = NULL;

                      delta++;
                    }
                }

              for(i = delta; i < num_entries + delta; i++)
                {
                  unsigned long needed;
                  needed =
                      sizeof(entry3) +
                      ((strlen(dirent_array[i - delta]->name.name) + 3) & ~3);
                  if((space_used += needed) > count)
                    {
                      if(i == delta)
                        {
                          /*
                           * Not enough
                           * room to
                           * make even
                           * 1 reply
                           */
                          pres->res_readdir3.status = NFS3ERR_TOOSMALL;

                          /* after successful cache_inode_readdir, dir_pentry
                           * may be read locked */
                          if (dir_pentry_unlock)
                              V_r(&dir_pentry->lock);

                          Mem_Free(dirent_array);
                          Mem_Free(entry_name_array);
                          return NFS_REQ_OK;
                        }
                      break;
                    }
                  FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                    FSAL_DIGEST_FILEID3,
                                    cache_inode_get_fsal_handle(
                                        dirent_array[i - delta]->pentry,
                                        &cache_status_gethandle),
                                    (caddr_t) & (RES_READDIR3_OK.reply.entries[i].
                                                 fileid));

                  FSAL_name2str(&dirent_array[i - delta]->name,
                                entry_name_array[i],
                                FSAL_MAX_NAME_LEN);
                  RES_READDIR3_OK.reply.entries[i].name = entry_name_array[i];

                  /* Set cookie :
                   * If we are not at last returned dirent, the cookie is the
                   * index of the next p_entry.
                   * Else, the cookie is the end_cookie.
                   */

                  if(i != num_entries + delta - 1)
                    RES_READDIR3_OK.reply.entries[i].cookie =
                        dirent_array[i - delta]->cookie;
                  else
                    RES_READDIR3_OK.reply.entries[i].cookie = end_cookie;

                  RES_READDIR3_OK.reply.entries[i].nextentry = NULL;

                  if(i != 0)
                    RES_READDIR3_OK.reply.entries[i - 1].nextentry =
                        &(RES_READDIR3_OK.reply.entries[i]);

                }

              RES_READDIR3_OK.reply.eof = FALSE;        /* the actual value will be set in post treatments */

              nfs_SetPostOpAttr(pcontext, pexport,
                                dir_pentry, &dir_attr, &(RES_READDIR3_OK.dir_attributes));

              memcpy(RES_READDIR3_OK.cookieverf, cookie_verifier, sizeof(cookieverf3));
              pres->res_readdir3.status = NFS3_OK;
              break;

            }                   /* switch rq_vers */

          /* after successful cache_inode_readdir, dir_pentry
           * may be read locked */
          if (dir_pentry_unlock)
              V_r(&dir_pentry->lock);

          if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
           cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
          Mem_Free(dirent_array);

          if((eod_met == END_OF_DIR) && (i == num_entries + delta))
            {

              /* End of directory */
              switch (preq->rq_vers)
                {
                case NFS_V2:
                  pres->res_readdir2.status = NFS_OK;
                  RES_READDIR2_OK.eof = TRUE;
                  break;

                case NFS_V3:
                  pres->res_readdir3.status = NFS3_OK;
                  RES_READDIR3_OK.reply.eof = TRUE;
                  nfs_SetPostOpAttr(pcontext, pexport,
                                    dir_pentry,
                                    &dir_attr, &(RES_READDIR3_OK.dir_attributes));
                  memcpy(RES_READDIR3_OK.cookieverf, cookie_verifier,
                         sizeof(cookieverf3));
                  break;
                }
            }
          return NFS_REQ_OK;

        }                       /* if num_entries > 0 */

    }

  /* if cachine_inode_readdir */

  /* after successful cache_inode_readdir, dir_pentry
   * may be read locked */
  if (dir_pentry_unlock)
      V_r(&dir_pentry->lock);

  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
   cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
  Mem_Free(dirent_array);

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_readdir2.status,
                      &pres->res_readdir3.status,
                      dir_pentry,
                      &(pres->res_readdir3.READDIR3res_u.resfail.dir_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;

}                               /* nfs_Readdir */

/**
 * nfs2_Readdir_Free: Frees the result structure allocated for nfs2_Readdir.
 *
 * Frees the result structure allocated for nfs2_Readdir.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Readdir_Free(nfs_res_t * resp)
{
  if((resp->res_readdir2.status == NFS_OK) &&
     (resp->res_readdir2.READDIR2res_u.readdirok.entries != NULL))
    {
      Mem_Free(resp->res_readdir2.READDIR2res_u.readdirok.entries[0].name);
      Mem_Free(resp->res_readdir2.READDIR2res_u.readdirok.entries);
    }
}                               /* nfs2_Readdir_Free */

/**
 * nfs3_Readdir_Free: Frees the result structure allocated for nfs3_Readdir.
 *
 * Frees the result structure allocated for nfs3_Readdir.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Readdir_Free(nfs_res_t * resp)
{
  if((resp->res_readdir3.status == NFS3_OK) &&
     (resp->res_readdir3.READDIR3res_u.resok.reply.entries != NULL))
    {
      Mem_Free(resp->res_readdir3.READDIR3res_u.resok.reply.entries[0].name);
      Mem_Free(resp->res_readdir3.READDIR3res_u.resok.reply.entries);
    }
}                               /* nfs3_Readdir_Free */
