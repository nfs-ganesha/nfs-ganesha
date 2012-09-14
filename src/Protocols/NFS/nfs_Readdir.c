 /*
 * vimf:expandtab:shiftwidth=8:tabstop=8:
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include <assert.h>

/* This has a tremendous amount of code duplication, but it's very
   difficult to refactor since the differences between NFSv2 and
   NFSv3 are more a matter of data types than functionality. */

static bool_t nfs2_readdir_callback(void* opaque,
                                    char *name,
                                    fsal_handle_t *handle,
                                    fsal_attrib_list_t *attrs,
                                    uint64_t cookie);
static bool_t nfs3_readdir_callback(void* opaque,
                                    char *name,
                                    fsal_handle_t *handle,
                                    fsal_attrib_list_t *attrs,
                                    uint64_t cookie);
static void free_entry2s(entry2 *entry2s);
static void free_entry3s(entry3 *entry3s);

/**
 * @brief Opaque bookkeeping structure for NFSv2 readdir
 *
 * This structure keeps track of the process of writing out an NFSv2
 * READDIR response between calls to nfs2_readdir_callback.
 */

struct nfs2_readdir_cb_data
{
     entry2 *entries; /*< The array holding individual entries */
     size_t mem_left; /*< The amount of memory remaining before we
                          hit maxcount */
     size_t count; /*< The count of complete entries stored in the
                       buffer */
     size_t total_entries; /*< The total number of entries in the
                              array */
     fsal_op_context_t *context; /*< FSAL operation context */
     nfsstat2 error; /*< Set to a value other than NFS_OK if the
                         callback function finds a fatal error. */
};

/**
 * @brief Opaque bookkeeping structure for NFSv3 readdir
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIR response between calls to nfs3_readdir_callback.
 */

struct nfs3_readdir_cb_data
{
     entry3 *entries; /*< The array holding individual entries */
     size_t mem_left; /*< The amount of memory remaining before we
                          hit maxcount */
     size_t count; /*< The count of complete entries stored in the
                       buffer */
     size_t total_entries; /*< The total number of entries in the
                              array */
     fsal_op_context_t *context; /*< FSAL operation context */
     nfsstat3 error; /*< Set to a value other than NFS_OK if the
                         callback function finds a fatal error. */
};


/**
 *
 * @brief The NFS PROC2 and PROC3 READDIR
 *
 * Implements the NFS PROC READDIR function (for V2 and V3).
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  context Credentials to be used for this request
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int
nfs_Readdir(nfs_arg_t *arg,
            exportlist_t *export,
            fsal_op_context_t *context,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
     cache_entry_t *dir_entry = NULL;
     unsigned long count = 0;
     fsal_attrib_list_t dir_attr;
     uint64_t cookie = 0;
     uint64_t cache_inode_cookie = 0;
     cookieverf3 cookie_verifier;
     unsigned int num_entries;
     unsigned long estimated_num_entries = 0;
     cache_inode_file_type_t dir_filetype = 0;
     bool_t eod_met = FALSE;
     cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
     cache_inode_status_t cache_status_gethandle = CACHE_INODE_SUCCESS;
     int rc = NFS_REQ_OK;
     struct nfs2_readdir_cb_data cb2 = {NULL};
     struct nfs3_readdir_cb_data cb3 = {NULL};
     cache_inode_readdir_cb_t cbfunc;
     void *cbdata;
     cache_entry_t *parent_dir_entry = NULL;

     if (isDebug(COMPONENT_NFSPROTO) || isDebug(COMPONENT_NFS_READDIR)) {
          char str[LEN_FH_STR];
          log_components_t component;
          nfs_FhandleToStr(req->rq_vers,
                           &(arg->arg_readdir2.dir),
                           &(arg->arg_readdir3.dir),
                           NULL,
                           str);
          if(isDebug(COMPONENT_NFSPROTO)) {
               component = COMPONENT_NFSPROTO;
          } else {
               component = COMPONENT_NFS_READDIR;
          }
          LogDebug(component,
                   "REQUEST PROCESSING: Calling nfs_Readdir handle: %s", str);
     }

     if (req->rq_vers == NFS_V3) {
          /* to avoid setting it on each error case */
          res->res_readdir3.READDIR3res_u.resfail.dir_attributes
               .attributes_follow = FALSE;
     }

     /* Look up cache entry for filehandle */
     if ((dir_entry
          = nfs_FhandleToCache(req->rq_vers,
                               &(arg->arg_readdir2.dir),
                               &(arg->arg_readdir3.dir),
                               NULL,
                               &(res->res_readdir2.status),
                               &(res->res_readdir3.status),
                               NULL,
                               &dir_attr,
                               context,
                               &rc)) == NULL) {
          /* Stale NFS FH? */
          goto out;
     }

     if ((req->rq_vers == NFS_V3) &&
         (nfs3_Is_Fh_Xattr(&(arg->arg_readdir3.dir)))) {
          rc = nfs3_Readdir_Xattr(arg, export, context, req, res);
          goto out;
     }

     /* Extract the filetype */
     dir_filetype = cache_inode_fsal_type_convert(dir_attr.type);
     /* Sanity checks -- must be a directory */
     if (dir_filetype != DIRECTORY) {
          if (req->rq_vers == NFS_V2) {
               /* In the RFC tell it not good but it does not tell
                  what to do ... */
               res->res_readdir2.status = NFSERR_NOTDIR;
          } else if (req->rq_vers == NFS_V3) {
               res->res_readdir3.status = NFS3ERR_NOTDIR;
          }
          rc = NFS_REQ_OK;
          goto out;
     }

     /* Parse out request arguments and decide how many entries we
        want.  For NFSv3, deal with the cookie verifier. */

     if (req->rq_vers == NFS_V2) {
          count = (arg->arg_readdir2.count * 9) / 10;
          memcpy(&cookie, arg->arg_readdir2.cookie, NFS2_COOKIESIZE);
          estimated_num_entries = MIN(50, count / sizeof(entry2));
          LogFullDebug(COMPONENT_NFS_READDIR,
                       "-- Readdir2 -> count=%lu  cookie = %"PRIu64"  "
                       "estimated_num_entries=%lu", count, cookie,
                       estimated_num_entries);
          if (estimated_num_entries == 0) {
               res->res_readdir2.status = NFSERR_IO;
               rc = NFS_REQ_OK;
               goto out;
          }

          cb2.entries = gsh_calloc(estimated_num_entries,
                                   sizeof(entry2));
          if (cb2.entries == NULL) {
               rc = NFS_REQ_DROP;
               goto out;
          }

          cb2.total_entries = estimated_num_entries;
          cb2.mem_left = count - sizeof(READDIR2resok);
          cb2.count = 0;
          cb2.context = context;
          cb2.error = NFS_OK;
          cbfunc = nfs2_readdir_callback;
          cbdata = &cb2;
     } else {
          count = (arg->arg_readdir3.count * 9 / 10);
          cookie = arg->arg_readdir3.cookie;
          estimated_num_entries = MIN(count / sizeof(entry3), 50);
          LogFullDebug(COMPONENT_NFS_READDIR,
                       "---> nfs3_Readdir: count=%lu  cookie=%"PRIu64"  "
                       "estimated_num_entries=%lu",
                       count, cookie, estimated_num_entries);
          if (estimated_num_entries == 0) {
               res->res_readdir3.status = NFS3ERR_TOOSMALL;
               rc = NFS_REQ_OK;
               goto out;
          }

          /* To make or check the cookie verifier */
          memset(cookie_verifier, 0, sizeof(cookieverf3));
          /* If cookie verifier is used, then a
             non-trivial value is returned to the
             client.

             This value is the mtime of the directory. If verifier is
             unused (as in many NFS Servers) then only a set of zeros
             is returned (trivial value). */
          if (export->UseCookieVerifier)
               memcpy(cookie_verifier,
                      &(dir_attr.mtime),
                      sizeof(dir_attr.mtime));
          /* Nothing to do if != 0 because the area is already full of
             zero */
          if ((cookie != 0) &&
              (export->UseCookieVerifier)) {
               /* Not the first call, so we have to check the cookie
                  verifier */
               if (memcmp(cookie_verifier,
                          arg->arg_readdir3.cookieverf,
                          NFS3_COOKIEVERFSIZE) != 0) {
                    res->res_readdir3.status = NFS3ERR_BAD_COOKIE;
                    rc = NFS_REQ_OK;
                    goto out;
               }
          }

          cb3.entries = gsh_calloc(estimated_num_entries,
                                   sizeof(entry3));
          if (cb3.entries == NULL) {
               rc = NFS_REQ_DROP;
               goto out;
          }
          cb3.total_entries = estimated_num_entries;
          cb3.mem_left = count - sizeof(READDIR3resok);
          cb3.count = 0;
          cb3.context = context;
          cb3.error = NFS_OK;
          cbfunc = nfs3_readdir_callback;
          cbdata = &cb3;
     }

     /* Adjust the cookie we supply to cache_inode */
     if (cookie > 2)  /* it is not the cookie for "." nor ".." */ {
          cache_inode_cookie = cookie;
     } else {
          cache_inode_cookie = 0;
     }

     /* Fills "."  */
     if (cookie == 0) {
          if (!cbfunc(cbdata, ".", &dir_entry->handle, &dir_attr, 1))
                goto outerr;
     }

     /* Fills ".." */
     if ((cookie <= 1) && (estimated_num_entries > 1)) {
          fsal_attrib_list_t parent_dir_attr;
          /* Get parent pentry */
          parent_dir_entry = cache_inode_lookupp(dir_entry,
                                                 context,
                                                 &cache_status_gethandle);
          if (parent_dir_entry == NULL) {
               if (req->rq_vers == NFS_V2) {
                    res->res_readdir2.status
                         = nfs2_Errno(cache_status_gethandle);
               } else if (req->rq_vers == NFS_V3) {
                    res->res_readdir3.status
                         = nfs3_Errno(cache_status_gethandle);
               }
               rc = NFS_REQ_OK;
               goto out;
          }
          if ((cache_inode_getattr(parent_dir_entry,
                                   &parent_dir_attr,
                                   context,
                                   &cache_status_gethandle))
              != CACHE_INODE_SUCCESS) {
               if (req->rq_vers == NFS_V2) {
                    res->res_readdir2.status
                         = nfs2_Errno(cache_status_gethandle);
               } else if (req->rq_vers == NFS_V3) {
                    res->res_readdir3.status
                         = nfs3_Errno(cache_status_gethandle);
               }
               rc = NFS_REQ_OK;
               goto out;
          }
          if (!cbfunc(cbdata, "..", &parent_dir_entry->handle,
                      &parent_dir_attr, 2))
                goto outerr;
          cache_inode_put(parent_dir_entry);
          parent_dir_entry = NULL;
     }

/* Some definitions that will be very useful to avoid very long names
   for variables */
#define RES_READDIR2_OK   res->res_readdir2.READDIR2res_u.readdirok
#define RES_READDIR3_OK   res->res_readdir3.READDIR3res_u.resok
#define RES_READDIR3_FAIL res->res_readdir3.READDIR3res_u.resfail

     /* Call readdir */
     if (cache_inode_readdir(dir_entry,
                             cache_inode_cookie,
                             &num_entries,
                             &eod_met,
                             context,
                             cbfunc,
                             cbdata,
                             &cache_status) != CACHE_INODE_SUCCESS) {
          rc = nfs_SetFailedStatus(export, req->rq_vers, cache_status,
                                   &res->res_readdir2.status,
                                   &res->res_readdir3.status,
                                   &(res->res_readdir3.READDIR3res_u.resfail.dir_attributes),
                                   NULL, NULL, NULL, NULL);
          goto out;
     }

     LogFullDebug(COMPONENT_NFS_READDIR,
                  "-- Readdir -> Call to "
                  "cache_inode_readdir(cookie=%"PRIu64
                  " -> num_entries = %u",
                  cache_inode_cookie,
                  num_entries);

     if (req->rq_vers == NFS_V2) {
          RES_READDIR2_OK.entries = cb2.entries;
          RES_READDIR2_OK.eof = eod_met;
     } else if (req->rq_vers == NFS_V3) {
          RES_READDIR3_OK.reply.entries = cb3.entries;
          RES_READDIR3_OK.reply.eof = eod_met;
          nfs_SetPostOpAttr(export,
                            &dir_attr,
                            &(RES_READDIR3_OK.dir_attributes));
          memcpy(RES_READDIR3_OK.cookieverf,
                 cookie_verifier,
                 sizeof(cookieverf3));
          res->res_readdir3.status = NFS3_OK;
     }

     rc = NFS_REQ_OK;

out:
     /* return references */
     if (dir_entry) {
          cache_inode_put(dir_entry);
     }

     if (parent_dir_entry) {
          cache_inode_put(parent_dir_entry);
     }

     /* Deallocate memory in the event of an error */
     if (req->rq_vers == NFS_V2) {
          if (((res->res_readdir2.status != NFS_OK) ||
               (rc != NFS_REQ_OK)) &&
              (cb2.entries != NULL)) {
               free_entry2s(cb2.entries);
               RES_READDIR2_OK.entries = NULL;
          }
     } else if (req->rq_vers == NFS_V3) {
          if (((res->res_readdir3.status != NFS3_OK) ||
               (rc != NFS_REQ_OK)) &&
              (cb3.entries != NULL)) {
               free_entry3s(cb3.entries);
               RES_READDIR3_OK.reply.entries = NULL;
          }
     }

     return rc;

outerr:
     if (req->rq_vers == NFS_V2) {
          assert(cbdata == &cb2);
          res->res_readdir2.status = cb2.error;
     } else if (req->rq_vers == NFS_V3) {
          assert(cbdata == &cb3);
          res->res_readdir3.status = cb3.error;
     }

     goto out;
} /* nfs_Readdir */

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
     if ((resp->res_readdir2.status == NFS_OK) &&
         (resp->res_readdir2.READDIR2res_u.readdirok.entries != NULL)) {
          free_entry2s(resp->res_readdir2.READDIR2res_u.readdirok.entries);
     }
} /* nfs2_Readdir_Free */

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
     if ((resp->res_readdir3.status == NFS3_OK) &&
         (resp->res_readdir3.READDIR3res_u.resok.reply.entries != NULL)) {
          free_entry3s(resp->res_readdir3.READDIR3res_u.resok.reply.entries);
     }
} /* nfs3_Readdir_Free */

/**
 * @brief Populate entry2s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entry2 structures and allocates
 * space for the name of each entry.
 *
 * @param opaque [in] Pointer to a struct nfs2_readdir_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

static bool_t
nfs2_readdir_callback(void* opaque,
                      char *name,
                      fsal_handle_t *handle,
                      fsal_attrib_list_t *attrs,
                      uint64_t cookie)
{
     /* Not-so-opaque pointer to callback data`*/
     struct nfs2_readdir_cb_data *tracker =
          (struct nfs2_readdir_cb_data *) opaque;
     /* Space needed for this entry's filename */
     unsigned long namelen = strlen(name);
     /* A big-endian representation of the least significant
        thirty-to bits of the cookie. */
     uint32_t truncookie = htonl((uint32_t) cookie);
     entry2 *e2 = tracker->entries + tracker->count;
     /* Fileid descriptor */
     struct fsal_handle_desc id_descriptor
          = {sizeof(e2->fileid), (caddr_t) &e2->fileid};
     size_t need = sizeof(entry2) + ((namelen + 3) & ~3) + 4;

     if (tracker->count == tracker->total_entries) {
          return FALSE;
     }
     if (tracker->mem_left < (sizeof(entry2) + namelen)) {
          if (tracker->count == 0) {
               tracker->error = NFSERR_IO;
          }
          return FALSE;
     }
     FSAL_DigestHandle(FSAL_GET_EXP_CTX(tracker->context),
                       FSAL_DIGEST_FILEID2,
                       handle,
                       &id_descriptor);
     e2->name = gsh_malloc(namelen + 1);
     if (e2->name == NULL) {
          tracker->error = NFSERR_IO;
          return FALSE;
     }
     strcpy(e2->name, name);
     memcpy(e2->cookie, &truncookie, NFS2_COOKIESIZE);
     if (tracker->count != 0) {
          tracker->entries[tracker->count - 1].nextentry = e2;
     }
     tracker->mem_left -= need;
     ++(tracker->count);
     return TRUE;
} /* nfs2_readdir_callback */

/**
 * @brief Populate entry3s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entry3 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdir_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

static bool_t
nfs3_readdir_callback(void* opaque,
                      char *name,
                      fsal_handle_t *handle,
                      fsal_attrib_list_t *attrs,
                      uint64_t cookie)
{
     /* Not-so-opaque pointer to callback data`*/
     struct nfs3_readdir_cb_data *tracker =
          (struct nfs3_readdir_cb_data *) opaque;
     /* Length of the current filename */
     size_t namelen = strlen(name);
     entry3 *e3 = tracker->entries + tracker->count;
     /* Fileid descriptor */
     struct fsal_handle_desc id_descriptor
          = {sizeof(e3->fileid), (caddr_t) &e3->fileid};
     size_t need = sizeof(entry3) + ((namelen + 3) & ~3) + 4;

     if (tracker->count == tracker->total_entries) {
          return FALSE;
     }
     if ((tracker->mem_left < need)) {
          if (tracker->count == 0) {
               tracker->error = NFS3ERR_TOOSMALL;
          }
          return FALSE;
     }
     FSAL_DigestHandle(FSAL_GET_EXP_CTX(tracker->context),
                       FSAL_DIGEST_FILEID3,
                       handle,
                       &id_descriptor);

     e3->name = gsh_malloc(namelen + 1);
     if (e3->name == NULL) {
          tracker->error = NFS3ERR_IO;
          return FALSE;
     }
     strcpy(e3->name, name);
     e3->cookie = cookie;

     if (tracker->count > 0) {
          tracker->entries[tracker->count - 1].nextentry = e3;
     }
     tracker->mem_left -= need;
     ++(tracker->count);
     return TRUE;
} /* */

/**
 * @brief Clean up memory allocated to serve NFSv2 READDIR
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entry2s [in] Pointer to first entry
 */

static void
free_entry2s(entry2 *entry2s)
{
     entry2 *entry = NULL;

     for (entry = entry2s;
          entry != NULL;
          entry = entry->nextentry) {
          gsh_free(entry->name);
     }
     gsh_free(entry2s);

     return;
} /* free_entry2s */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIR
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entry3s [in] Pointer to first entry
 */

static void
free_entry3s(entry3 *entry3s)
{
     entry3 *entry = NULL;

     for (entry = entry3s;
          entry != NULL;
          entry = entry->nextentry) {
          gsh_free(entry->name);
     }
     gsh_free(entry3s);

     return;
} /* free_entry3s */
