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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
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
#include "log.h"
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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include <assert.h>

static bool_t nfs3_readdirplus_callback(void* opaque,
                                        char *name,
                                        fsal_handle_t *handle,
                                        fsal_attrib_list_t *attrs,
                                        uint64_t cookie);
static void free_entryplus3s(entryplus3 *entryplus3s);

/**
 * @brief Opaque bookkeeping structure for NFSv3 READDIRPLUS
 *
 * This structure keeps track of the process of writing out an NFSv3
 * READDIRPLUS response between calls to nfs3_readdirplus_callback.
 */

struct nfs3_readdirplus_cb_data
{
     entryplus3 *entries; /*< The array holding individual entries */
     size_t mem_left; /*< The amount of memory remaining before we
                          hit maxcount */
     size_t count; /*< The count of complete entries stored in the
                       buffer */
     size_t total_entries; /*< The number of entires we allocated for
                               the array. */
     exportlist_t *export; /*< Pointer to the entry for the supplied
                               handle's export */
     fsal_op_context_t *context; /*< FSAL operation context */
     nfsstat3 error; /*< Set to a value other than NFS_OK if the
                         callback function finds a fatal error. */
};

/**
 * @brief The NFS PROC3 READDIRPLUS
 *
 * Implements the NFSv3 PROC READDIRPLUS function
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  context Credentials to be used for this request
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int
nfs3_Readdirplus(nfs_arg_t *arg,
                 exportlist_t *export,
                 fsal_op_context_t *context,
                 nfs_worker_data_t *pworker,
                 struct svc_req *req,
                 nfs_res_t *res)
{
     cache_entry_t *dir_entry = NULL;
     fsal_attrib_list_t dir_attr;
     uint64_t begin_cookie = 0;
     uint64_t cache_inode_cookie = 0;
     cookieverf3 cookie_verifier;
     unsigned int num_entries = 0;
     unsigned long estimated_num_entries = 0;
     cache_inode_file_type_t dir_filetype = 0;
     bool_t eod_met = FALSE;
     cache_inode_status_t cache_status = 0;
     cache_inode_status_t cache_status_gethandle = 0;
     int rc = NFS_REQ_OK;
     struct nfs3_readdirplus_cb_data cb_opaque = {.entries = NULL,
                                                  .mem_left = 0,
                                                  .count = 0,
                                                  .export = export,
                                                  .context = context,
                                                  .error = NFS3_OK};

     if (isDebug(COMPONENT_NFSPROTO) ||
         isDebug(COMPONENT_NFS_READDIR)) {
          char str[LEN_FH_STR];
          log_components_t component;
          sprint_fhandle3(str, &(arg->arg_readdirplus3.dir));
          if (isDebug(COMPONENT_NFSPROTO)) {
               component = COMPONENT_NFSPROTO;
          } else {
               component = COMPONENT_NFS_READDIR;
               LogDebug(component,
                        "REQUEST PROCESSING: Calling nfs3_Readdirplus "
                        " handle: %s", str);
          }
     }

     /* to avoid setting it on each error case */
     res->res_readdir3.READDIR3res_u.resfail
          .dir_attributes.attributes_follow = FALSE;

     cb_opaque.mem_left = (arg->arg_readdirplus3.maxcount * 9) / 10;
     begin_cookie = arg->arg_readdirplus3.cookie;

     cb_opaque.mem_left -= sizeof(READDIRPLUS3resok);

     /* Estimate assuming that we're going to send no names and no handles.
      * Don't count space for pointers for nextentry or 
      * name_handle.data.data_val in entryplus3 */
     estimated_num_entries =
          MIN((cb_opaque.mem_left + sizeof(entryplus3 *))
              / (sizeof(entryplus3) - sizeof(char *)*2), 50);

     cb_opaque.total_entries = estimated_num_entries;
     LogFullDebug(COMPONENT_NFS_READDIR,
                  "nfs3_Readdirplus: dircount=%u "
                  "begin_cookie=%"PRIu64" "
                  "estimated_num_entries=%lu, mem_left=%zd",
                  arg->arg_readdirplus3.dircount,
                  begin_cookie,
                  estimated_num_entries, cb_opaque.mem_left);

     /* Is this a xattr FH ? */
     if (nfs3_Is_Fh_Xattr(&(arg->arg_readdirplus3.dir))) {
          rc = nfs3_Readdirplus_Xattr(arg, export, context,
                                      req, res);
          goto out;
     }

     /* Convert file handle into a vnode */
     if ((dir_entry
          = nfs_FhandleToCache(req->rq_vers,
                               NULL,
                               &(arg->arg_readdirplus3.dir),
                               NULL,
                               NULL,
                               &(res->res_readdirplus3.status),
                               NULL,
                               &dir_attr, context, &rc)) == NULL) {
          rc = NFS_REQ_DROP;
          goto out;
     }

     /* Extract the filetype */
     dir_filetype = cache_inode_fsal_type_convert(dir_attr.type);

     /* Sanity checks -- must be a directory */

     if (dir_filetype != DIRECTORY) {
          res->res_readdirplus3.status = NFS3ERR_NOTDIR;
          rc = NFS_REQ_OK;
          goto out;
    }

     memset(cookie_verifier, 0, sizeof(cookieverf3));

     /* If cookie verifier is used, then an non-trivial value is
        returned to the client This value is the mtime of the
        directory. If verifier is unused (as in many NFS Servers) then
        only a set of zeros is returned (trivial value) */

     if (export->UseCookieVerifier) {
          memcpy(cookie_verifier, &(dir_attr.mtime), sizeof(dir_attr.mtime));
     }

     if (export->UseCookieVerifier && (begin_cookie != 0)) {
          /* Not the first call, so we have to check the cookie
             verifier */
          if (memcmp(cookie_verifier, arg->arg_readdirplus3.cookieverf,
                     NFS3_COOKIEVERFSIZE) != 0) {
               res->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;
               rc = NFS_REQ_OK;
               goto out;
          }
     }

     res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
     res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

     /* Fudge cookie for "." and "..", if necessary */
     if (begin_cookie > 1) {
          cache_inode_cookie = begin_cookie;
     } else {
          cache_inode_cookie = 0;
     }

     /* A definition that will be very useful to avoid very long names
        for variables */
#define RES_READDIRPLUS_REPLY pres->res_readdirplus3.READDIRPLUS3res_u  \
          .resok.reply

     /* Allocate space for entries */
     cb_opaque.entries = gsh_calloc(estimated_num_entries, sizeof(entryplus3));
     if (cb_opaque.entries == NULL) {
          rc = NFS_REQ_DROP;
          goto out;
     }

     if (begin_cookie == 0) {
          /* Fill in "." */
          if (!(nfs3_readdirplus_callback(&cb_opaque,
                                          ".",
                                          &dir_entry->handle,
                                          &dir_attr,
                                          1))) {
               res->res_readdirplus3.status = cb_opaque.error;
               rc = NFS_REQ_OK;
               goto out;
          }
     }

     /* Fill in ".." */
     if (begin_cookie <= 1) {
          fsal_attrib_list_t parent_dir_attr;
          cache_entry_t *parent_dir_entry
               = cache_inode_lookupp(dir_entry,
                                     context,
                                     &cache_status_gethandle);
          if (parent_dir_entry == NULL) {
               res->res_readdirplus3.status
                    = nfs3_Errno(cache_status_gethandle);
               rc = NFS_REQ_OK;
               goto out;
          }

          if ((cache_inode_getattr(parent_dir_entry,
                                   &parent_dir_attr,
                                   context,
                                   &cache_status_gethandle))
              != CACHE_INODE_SUCCESS) {
               res->res_readdirplus3.status
                    = nfs3_Errno(cache_status_gethandle);
               cache_inode_lru_unref(parent_dir_entry, 0);
               rc = NFS_REQ_OK;
               goto out;
          }
          if (!(nfs3_readdirplus_callback(&cb_opaque,
                                          "..",
                                          &parent_dir_entry->handle,
                                          &parent_dir_attr,
                                          2))) {
               res->res_readdirplus3.status = cb_opaque.error;
               cache_inode_lru_unref(parent_dir_entry, 0);
               rc = NFS_REQ_OK;
               goto out;
          }
          cache_inode_lru_unref(parent_dir_entry, 0);
     }

     /* Call readdir */
     if (cache_inode_readdir(dir_entry,
                             cache_inode_cookie,
                             &num_entries,
                             &eod_met,
                             context,
                             nfs3_readdirplus_callback,
                             &cb_opaque,
                             &cache_status) != CACHE_INODE_SUCCESS) {
          /* Is this a retryable error */
          if (nfs_RetryableError(cache_status)) {
               rc = NFS_REQ_DROP;
               goto out;
          }

          /* Set failed status */
          nfs_SetFailedStatus(context,
                              export,
                              NFS_V3,
                              cache_status,
                              NULL,
                              &res->res_readdirplus3.status,
                              dir_entry,
                              &(res->res_readdirplus3.READDIRPLUS3res_u
                                .resfail.dir_attributes),
                              NULL, NULL, NULL, NULL, NULL, NULL);
          goto out;
     }
     LogFullDebug(COMPONENT_NFS_READDIR,
                  "Readdirplus3 -> Call to cache_inode_readdir( cookie=%"
                  PRIu64") -> num_entries = %u",
                  cache_inode_cookie, num_entries);

     if ((num_entries == 0) && (begin_cookie > 1)) {
          res->res_readdirplus3.status = NFS3_OK;
          res->res_readdirplus3.READDIRPLUS3res_u
               .resok.reply.entries = NULL;
          res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpAttr(export,
                            NULL,
                            &(res->res_readdirplus3.READDIRPLUS3res_u
                              .resok.dir_attributes));

          memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
     } else {
          res->res_readdirplus3.READDIRPLUS3res_u
               .resok.reply.entries = cb_opaque.entries;
          res->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof
               = eod_met;
     }

     nfs_SetPostOpAttr(export,
                       &dir_attr,
                       &(res->res_readdirplus3.READDIRPLUS3res_u
                         .resok.dir_attributes));

     memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
            cookie_verifier, sizeof(cookieverf3));

     res->res_readdirplus3.status = NFS3_OK;

     memcpy(res->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
            cookie_verifier, sizeof(cookieverf3));

     rc = NFS_REQ_OK;

out:
     if (dir_entry)
          cache_inode_put(dir_entry);

     if (((res->res_readdir3.status != NFS3_OK) ||
          (rc != NFS_REQ_OK)) &&
         (cb_opaque.entries != NULL)) {
          free_entryplus3s(cb_opaque.entries);
     }

     return rc;
}                               /* nfs3_Readdirplus */

/**
 * @brief Frees the result structure allocated for nfs3_Readdirplus.
 *
 * Frees the result structure allocated for nfs3_Readdirplus.
 *
 * @param resp [in,out] Pointer to the result structure
 *
 */
void nfs3_Readdirplus_Free(nfs_res_t *resp)
{
#define RESREADDIRPLUSREPLY resp->res_readdirplus3.READDIRPLUS3res_u.resok.reply
     if ((resp->res_readdirplus3.status == NFS3_OK) &&
         (RESREADDIRPLUSREPLY.entries != NULL)) {
          free_entryplus3s(RESREADDIRPLUSREPLY.entries);
     }
} /*  nfs3_Readdirplus_Free */

/**
 * @brief Populate entryplus3s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entryplys3 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs3_readdirplus_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

static bool_t
nfs3_readdirplus_callback(void* opaque,
                          char *name,
                          fsal_handle_t *handle,
                          fsal_attrib_list_t *attrs,
                          uint64_t cookie)
{
     /* Not-so-opaque pointer to callback data`*/
     struct nfs3_readdirplus_cb_data *tracker =
          (struct nfs3_readdirplus_cb_data *) opaque;
     /* Length of the current filename */
     size_t namelen = strlen(name);
     /* Fileid buffer descriptor */
     entryplus3 *ep3 = tracker->entries + tracker->count;
     struct fsal_handle_desc id_descriptor
          = {sizeof(ep3->fileid), (caddr_t) &ep3->fileid};

     if (tracker->count == tracker->total_entries) {
          return FALSE;
     }
     /* This is a pessimistic check, which assumes that we're going
      * to send attributes and full size handle - if it fails then 
      * we're close enough to the buffer size limit and t's time to 
      * stop anyway */
     if ((tracker->mem_left < (sizeof(entryplus3) + namelen + NFS3_FHSIZE))) {
          if (tracker->count == 0) {
               tracker->error = NFS3ERR_TOOSMALL;
          }
          return FALSE;
     }

     FSAL_DigestHandle(FSAL_GET_EXP_CTX(tracker->context),
                       FSAL_DIGEST_FILEID3,
                       handle,
                       &id_descriptor);

     ep3->name = gsh_malloc(namelen + 1);
     if (ep3->name == NULL) {
          tracker->error = NFS3ERR_IO;
          return FALSE;
     }
     strcpy(ep3->name, name);
     ep3->cookie = cookie;

     /* Account for file name + length + cookie */
     tracker->mem_left -= sizeof(ep3->cookie) + ((namelen + 3) & ~3) + 4;

     ep3->name_handle.handle_follows = TRUE;
     ep3->name_handle.post_op_fh3_u.handle.data.data_val
          = gsh_malloc(NFS3_FHSIZE);
     if (ep3->name_handle.post_op_fh3_u .handle.data.data_val == NULL) {
          LogEvent(COMPONENT_NFS_READDIR,
                  "nfs3_readdirplus_callback FAILED to allocate FH");
          tracker->error = NFS3ERR_SERVERFAULT;
          gsh_free(ep3->name);
          return FALSE;
     }

     if (nfs3_FSALToFhandle(&ep3->name_handle.post_op_fh3_u.handle,
                            handle,
                            tracker->export) == 0) {
          tracker->error = NFS3ERR_BADHANDLE;
          gsh_free(ep3->name);
          gsh_free(ep3->name_handle.post_op_fh3_u.handle.data.data_val);
          return FALSE;
     }

     /* Account for filehande + length + follows + nextentry */
     tracker->mem_left -= ep3->name_handle.post_op_fh3_u.handle.data.data_len + 12;
     if (tracker->count > 0) {
          tracker->entries[tracker->count - 1].nextentry = ep3;
     }
     ep3->name_attributes.attributes_follow = FALSE;

     nfs_SetPostOpAttr(tracker->export,
                       attrs,
                       &ep3->name_attributes);
     if (ep3->name_attributes.attributes_follow) {
	  tracker->mem_left -= sizeof(ep3->name_attributes);
     } else {
	  tracker->mem_left -= sizeof(ep3->name_attributes.attributes_follow);
     }
     ++(tracker->count);
     return TRUE;
} /* nfs3_readdirplus_callback */

/**
 * @brief Clean up memory allocated to serve NFSv3 READDIRPLUS
 *
 * This function traverses the list of entries, freeing all names
 * allocated in the callback function, then frees the list itself.
 *
 * @param entryplus3s [in] Pointer to first entry
 */

static void
free_entryplus3s(entryplus3 *entryplus3s)
{
     entryplus3 *entry = NULL;

     for (entry = entryplus3s;
          entry != NULL;
          entry = entry->nextentry) {
          gsh_free(entry->name);
          gsh_free(entry->name_handle.post_op_fh3_u.handle.data.data_val);
     }
     gsh_free(entryplus3s);

     return;
} /* free_entryplus3s */
