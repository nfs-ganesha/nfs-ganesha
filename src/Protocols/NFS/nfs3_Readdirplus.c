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
  uint64_t begin_cookie;
  uint64_t end_cookie;
  uint64_t cache_inode_cookie;
  cache_inode_dir_entry_t **dirent_array = NULL;
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
  entryplus3 reference_entry;
  READDIRPLUS3resok reference_reply;
  int dir_pentry_unlock = FALSE;

  if(isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR))
    {
      char str[LEN_FH_STR];
      log_components_t component;
      sprint_fhandle3(str, &(parg->arg_readdirplus3.dir));
      if(isDebug(COMPONENT_NFSPROTO))
        component = COMPONENT_NFSPROTO;
      else
        component = COMPONENT_NFS_READDIR;
      LogDebug(component,
               "REQUEST PROCESSING: Calling nfs3_Readdirplus handle: %s", str);
    }

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdirplus3.dircount;
  maxcount = parg->arg_readdirplus3.maxcount;
  begin_cookie = (unsigned int)parg->arg_readdirplus3.cookie;

  /* FIXME: This calculation over estimates the number of bytes that 
   * READDIRPLUS3resok will use on the wire by 4 bytes on x86_64. */
  space_used = sizeof(reference_reply.dir_attributes.attributes_follow) +
    sizeof(reference_reply.dir_attributes.post_op_attr_u.attributes) +
    sizeof(reference_reply.cookieverf) +
    sizeof(reference_reply.reply.eof);

  estimated_num_entries =
    (dircount - space_used + sizeof(entry3 *))
    / (sizeof(entry3) - sizeof(char *)*2);
  //  estimated_num_entries *= 4;
  LogFullDebug(COMPONENT_NFS_READDIR,
               "nfs3_Readdirplus: dircount=%lu  maxcount=%lu  begin_cookie=%"
               PRIu64" space_used=%lu  estimated_num_entries=%lu",
               dircount, maxcount, begin_cookie,
               space_used, estimated_num_entries);

  /* Is this a xattr FH ? */
  if(nfs3_Is_Fh_Xattr(&(parg->arg_readdirplus3.dir)))
    return nfs3_Readdirplus_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  /* Convert file handle into a vnode */
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

  if(dir_filetype != DIRECTORY)
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
      if(memcmp(cookie_verifier, parg->arg_readdirplus3.cookieverf,
                NFS3_COOKIEVERFSIZE)
         != 0)
        {
          pres->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;

          return NFS_REQ_OK;
        }
    }

  if((dirent_array =
      (cache_inode_dir_entry_t **) Mem_Alloc_Label(
          estimated_num_entries * sizeof(cache_inode_dir_entry_t*),
          "cache_inode_dir_entry_t in nfs3_Readdirplus")) == NULL)
    {
      pres->res_readdirplus3.status = NFS3ERR_IO;
      return NFS_REQ_DROP;
    }

  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

#warning XXXX fix this--compare nfs4_op_readdir

  /* How many entries will we retry from cache_inode ? */
  if(begin_cookie > 1)
    {
      asked_num_entries = estimated_num_entries;
      cache_inode_cookie = begin_cookie;
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
                   "Readdirplus3 -> Call to cache_inode_readdir( cookie=%"
                   PRIu64", asked=%lu ) -> num_entries = %u",
                   cache_inode_cookie, asked_num_entries, num_entries);

      if(eod_met == END_OF_DIR)
        {
          LogFullDebug(COMPONENT_NFS_READDIR,
                       "+++++++++++++++++++++++++++++++++++++++++> EOD MET ");
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
          /* Allocation of the structure for reply */
          entry_name_array =
              (entry_name_array_item_t *) Mem_Alloc_Label(estimated_num_entries *
                                                          (FSAL_MAX_NAME_LEN + 1),
                                                          "entry_name_array in nfs3_Readdirplus");

          if(entry_name_array == NULL)
            {
                /* after successful cache_inode_readdir, dir_pentry may be
                 * read locked */
                if (dir_pentry_unlock)
                    V_r(&dir_pentry->lock);
   
              if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
              Mem_Free((char *)dirent_array);
              return NFS_REQ_DROP;
            }

          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
              (entryplus3 *) Mem_Alloc_Label(estimated_num_entries * sizeof(entryplus3),
                                             "READDIRPLUS3res_u.resok.reply.entries");

          if(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries == NULL)
            {
                /* after successful cache_inode_readdir, dir_pentry may be
                 * read locked */
                if (dir_pentry_unlock)
                    V_r(&dir_pentry->lock);

              if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
               cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
              Mem_Free((char *)dirent_array);
              Mem_Free((char *)entry_name_array);
              return NFS_REQ_DROP;
            }

          /* Allocation of the file handles */
          fh3_array =
              (fh3_buffer_item_t *) Mem_Alloc_Label(estimated_num_entries * NFS3_FHSIZE,
                                                    "Filehandle V3 in nfs3_Readdirplus");

          if(fh3_array == NULL)
            {
                /* after successful cache_inode_readdir, dir_pentry may be
                 * read locked */
                if (dir_pentry_unlock)
                    V_r(&dir_pentry->lock);

              if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
              Mem_Free((char *)dirent_array);
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
                        /* after successful cache_inode_readdir, dir_pentry
                         * may be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
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
                        /* after successful cache_inode_readdir, dir_pentry may
                         * be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
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

                  LogFullDebug(COMPONENT_NFS_READDIR,
                               "Readdirplus3 -> i=0 num_entries=%d space_used=%lu maxcount=%lu Name=. FileId=%016llx Cookie=%llu",
                               num_entries, space_used, maxcount,
                               RES_READDIRPLUS_REPLY.entries[0].fileid,
                               RES_READDIRPLUS_REPLY.entries[0].cookie);

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
                        /* after successful cache_inode_readdir, dir_pentry may
                         * be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);

                      pres->res_readdirplus3.status = nfs3_Errno(cache_status_gethandle);
                      return NFS_REQ_OK;
                    }

                  if((pfsal_handle = cache_inode_get_fsal_handle(pentry_dot_dot,
                                                                 &cache_status_gethandle))
                     == NULL)
                    {
                        /* after successful cache_inode_readdir, dir_pentry may
                         * be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
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
                     (&pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[delta].
                      name_handle.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
                    {
                        /* after successful cache_inode_readdir, dir_pentry may
                         * be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
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

                  LogFullDebug(COMPONENT_NFS_READDIR,
                               "Readdirplus3 -> i=%d num_entries=%d space_used=%lu maxcount=%lu Name=.. FileId=%016llx Cookie=%llu",
                               delta, num_entries, space_used, maxcount,
                               RES_READDIRPLUS_REPLY.entries[delta].fileid,
                               RES_READDIRPLUS_REPLY.entries[delta].cookie);
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

              /* maxcount is the size with the FH and attributes overhead,
	       * so entryplus3 is used instead of entry3. The data structures
	       * in nfs23.h have funny padding depending on the arch (32 or 64).
	       * We can't get an accurate estimate by simply using
	       * sizeof(entryplus3). */
	      /* FIXME: There is still a 4 byte over estimate here on x86_64. */

#warning Remove cookie offset calculation in readdir and readdirplus (obsoleted)

              needed =
		sizeof(reference_entry)
		+ NFS3_FHSIZE
		+ ((strlen(dirent_array[i - delta]->name.name) + 3) & ~3);

	      /* if delta == 1 or 2, then "." and ".." have already been added
	       * to the readdirplus reply. */
	      if (i == delta) {
		needed += needed*delta /* size of a dir entry in reply */
		  - ((strlen(dirent_array[i - delta]->name.name) + 3) & ~3)*delta /* size of filename for current entry */
		  + 4*delta; /* size of "." and ".." filenames in reply */
	      }

              if((space_used += needed) > maxcount)
                {
		  /* If delta != 0, then we already added "." or ".." to the reply. */
                  if(i == delta && delta == 0)
                    {
                      /* Not enough room to make even a single reply */

                        /* after successful cache_inode_readdir, dir_pentry may
                         * be read locked */
                        if (dir_pentry_unlock)
                            V_r(&dir_pentry->lock);

                      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                      Mem_Free((char *)dirent_array);
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
                  cache_inode_get_fsal_handle(dirent_array[i - delta]->pentry,
                                              &cache_status_gethandle)) == NULL)
                {
                    /* after successful cache_inode_readdir, dir_pentry may be
                     * read locked */
                    if (dir_pentry_unlock)
                        V_r(&dir_pentry->lock);

                  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                    cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                  Mem_Free((char *)dirent_array);
                  Mem_Free((char *)entry_name_array);
                  Mem_Free((char *)fh3_array);

                  pres->res_readdirplus3.status =
                      nfs3_Errno(cache_status_gethandle);
                  return NFS_REQ_OK;
                }

              /* Now fill in the replyed entryplus3 list */
              FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                FSAL_DIGEST_FILEID3,
                                pfsal_handle,
                                (caddr_t) & (RES_READDIRPLUS_REPLY.entries[i].fileid));

              FSAL_name2str(&dirent_array[i - delta]->name, entry_name_array[i],
                            FSAL_MAX_NAME_LEN);
              RES_READDIRPLUS_REPLY.entries[i].name = entry_name_array[i];

              LogFullDebug(COMPONENT_NFS_READDIR,
                           "Readdirplus3 -> i=%u num_entries=%u delta=%u "
                           "num_entries + delta - 1=%u end_cookie=%"PRIu64,
                           i, num_entries, delta, num_entries + delta - 1,
                           end_cookie);
              if(i != num_entries + delta - 1)
                RES_READDIRPLUS_REPLY.entries[i].cookie =
                    dirent_array[i - delta]->cookie;
              else
                RES_READDIRPLUS_REPLY.entries[i].cookie = end_cookie;

              RES_READDIRPLUS_REPLY.entries[i].name_attributes.attributes_follow = FALSE;
              RES_READDIRPLUS_REPLY.entries[i].name_handle.handle_follows = FALSE;

              cache_inode_get_attributes(dirent_array[i - delta]->pentry,
                                         &entry_attr);

              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.post_op_fh3_u.handle.data.data_val = (char *)fh3_array[i];

              /* Compute the NFSv3 file handle */
              if(nfs3_FSALToFhandle
                 (&pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].
                  name_handle.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
                {
                    /* after successful cache_inode_readdir, dir_pentry may be
                     * read locked */
                    if (dir_pentry_unlock)
                        V_r(&dir_pentry->lock);

                  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
                    cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
                  Mem_Free((char *)dirent_array);
                  Mem_Free((char *)entry_name_array);
                  Mem_Free((char *)fh3_array);

                  pres->res_readdirplus3.status = NFS3ERR_BADHANDLE;
                  return NFS_REQ_OK;
                }

              /* Set PostPoFh3 structure */
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.handle_follows = TRUE;
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_handle.post_op_fh3_u.handle.data.data_len = sizeof(file_handle_v3_t);

              nfs_SetPostOpAttr(pcontext, pexport,
                                dirent_array[i - delta]->pentry,
                                &entry_attr,
                                &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries[i].name_attributes));

              LogFullDebug(COMPONENT_NFS_READDIR,
                           "Readdirplus3 -> i=%d, num_entries=%d needed=%lu space_used=%lu maxcount=%lu Name=%s FileId=%016llx Cookie=%llu",
                           i, num_entries, needed, space_used, maxcount,
                           dirent_array[i - delta]->name.name,
                           RES_READDIRPLUS_REPLY.entries[i].fileid,
                           RES_READDIRPLUS_REPLY.entries[i].cookie);

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
                       "============================================================> EOD MET !!!!!!");
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

      LogFullDebug(COMPONENT_NFS_READDIR,
                   "============================================================");

      /* after successful cache_inode_readdir, dir_pentry may be
       * read locked */
      if (dir_pentry_unlock)
          V_r(&dir_pentry->lock);

      /* Free the memory */
      if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) ) 
        cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
      Mem_Free((char *)dirent_array);

      return NFS_REQ_OK;
    }

  /* If we are here, there was an error */

  /* after successful cache_inode_readdir, dir_pentry may be
   * read locked */
  if (dir_pentry_unlock)
      V_r(&dir_pentry->lock);

  /* Free the memory */
  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
    cache_inode_release_dirent( dirent_array, num_entries, pclient ) ;
  Mem_Free((char *)dirent_array);
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
