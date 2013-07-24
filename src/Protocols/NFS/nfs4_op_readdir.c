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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

#define arg_READDIR4 op->nfs_argop4_u.opreaddir
#define res_READDIR4 resp->nfs_resop4_u.opreaddir

static bool_t nfs4_readdir_callback(void* opaque,
                                    char *name,
                                    cache_entry_t *entry,
                                    bool_t attr_allowed,
                                    fsal_op_context_t *context,
                                    uint64_t cookie);
static void free_entries(entry4 *entries);

/**
 * @brief Opaque bookkeeping structure for NFSv4 readdir
 *
 * This structure keeps track of the process of writing out an NFSv4
 * READDIR response between calls to nfs4_readdir_callback.
 */

struct nfs4_readdir_cb_data
{
     entry4 *entries; /*< The array holding individual entries */
     size_t mem_left; /*< The amount of memory remaining before we
                          hit maxcount */
     size_t count; /*< The count of complete entries stored in the
                       buffer */
     size_t total_entries; /*< The total number of entries available
                               in the array*/
     nfsstat4 error; /*< Set to a value other than NFS4_OK if the
                         callback function finds a fatal error. */
     bitmap4 req_attr; /*< The requested attributes */
     fsal_attrib_mask_t attrmask; /*< The requested attributes */
     compound_data_t *data; /*< The compound data, so we can produce
                                nfs_fh4s. */
};

/**
 * @brief NFS4_OP_READDIR
 *
 * Implements the NFS4_OP_READDIR opeartion. If fh is a pseudo FH,
 * then call is routed to routine nfs4_op_readdir_pseudo
 *
 * @param op [IN] pointer to nfs4_op arguments
 * @param data [INOUT] Pointer to the compound request's data
 * @param resp [IN] Pointer to nfs4_op results
 *
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
int
nfs4_op_readdir(struct nfs_argop4 *op,
                compound_data_t * data,
                struct nfs_resop4 *resp)
{
     cache_entry_t *dir_entry = NULL;
     bool_t eod_met = FALSE;
     char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readdir";
     unsigned long dircount = 0;
     unsigned long maxcount = 0;
     entry4 *entries = NULL;
     verifier4 cookie_verifier;
     uint64_t cookie = 0;
     unsigned int estimated_num_entries = 0;
     unsigned int num_entries = 0;
     struct nfs4_readdir_cb_data cb_data;
     cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

     resp->resop = NFS4_OP_READDIR;
     res_READDIR4.status = NFS4_OK;

     if ((res_READDIR4.status
          = nfs4_sanity_check_FH(data, DIRECTORY)) != NFS4_OK) {
          goto out;
     }

     /* Pseudo Fs management */
     if(nfs4_Is_Fh_Pseudo(&(data->currentFH))) {
          res_READDIR4.status = nfs4_op_readdir_pseudo(op, data, resp);
          goto out;
     }

     /* Xattrs management */
     if(nfs4_Is_Fh_Xattr(&(data->currentFH))) {
          res_READDIR4.status = nfs4_op_readdir_xattr(op, data, resp);
          goto out;
     }

     dir_entry = data->current_entry;

     /* get the characteristic value for readdir operation */
     dircount = arg_READDIR4.dircount;
     maxcount = (arg_READDIR4.maxcount * 9) / 10;
     cookie = arg_READDIR4.cookie;

     /* Dircount is considered meaningless by many nfsv4 client (like the CITI
        one).  we use maxcount instead. */

     /* The Linux 3.0, 3.1.0 clients vs. TCP Ganesha comes out 10x slower
        with 500 max entries */

     estimated_num_entries = 50;
     cb_data.total_entries = estimated_num_entries;

     LogFullDebug(COMPONENT_NFS_V4,
                  "--- nfs4_op_readdir ---> dircount=%lu maxcount=%lu "
                  "cookie=%"PRIu64" estimated_num_entries=%u",
                  dircount, maxcount, cookie, estimated_num_entries);

     /* Since we never send a cookie of 1 or 2, we shouldn't ever get
        them back. */
     if (cookie == 1 || cookie == 2) {
          res_READDIR4.status = NFS4ERR_BAD_COOKIE;
          goto out;
     }

     /* Get only attributes that are allowed to be read */
     if (!nfs4_Fattr_Check_Access_Bitmap(&arg_READDIR4.attr_request,
                                        FATTR4_ATTR_READ)) {
          res_READDIR4.status = NFS4ERR_INVAL;
          goto out;
     }

     /* If maxcount is too short (14 should be enough for an empty directory)
          return NFS4ERR_TOOSMALL */
     if (maxcount < 14 || estimated_num_entries == 0) {
          res_READDIR4.status = NFS4ERR_TOOSMALL;
          goto out;
     }

     /* If a cookie verifier is used, then a non-trivial value is
        returned to the client.  This value is the mtime of the
        directory.  If verifier is unused (as in many NFS Servers)
        then only a set of zeros is returned (trivial value) */
     memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);

     /* Cookie delivered by the server and used by the client SHOULD
        not be 0, 1 or 2 because these values are reserved (see RFC
        3530, p. 192/RFC 5661, p468).

             0 - cookie for first READDIR
             1 - reserved for . on client
             2 - reserved for .. on client

        '.' and '..' are not returned, so all cookies will be offset
        by 2 */

     if ((cookie != 0) && (data->pexport->UseCookieVerifier == 1)) {
          if(memcmp(cookie_verifier, arg_READDIR4.cookieverf,
                    NFS4_VERIFIER_SIZE) != 0) {
               res_READDIR4.status = NFS4ERR_BAD_COOKIE;
               goto out;
          }
     }

     /* Prepare to read the entries */

     entries = gsh_calloc(estimated_num_entries,
                          sizeof(entry4));
     cb_data.entries = entries;
     cb_data.mem_left = maxcount - sizeof(READDIR4resok);
     cb_data.count = 0;
     cb_data.error = NFS4_OK;
     cb_data.req_attr = arg_READDIR4.attr_request;
     cb_data.data = data;
     cb_data.attrmask = 0;

     nfs4_attrmap_to_FSAL_attrmask(&cb_data.req_attr, &cb_data.attrmask);

     /* Perform the readdir operation */
     if (cache_inode_readdir(dir_entry,
                             cookie,
                             &num_entries,
                             &eod_met,
                             data->pcontext,
                             cb_data.attrmask,
                             nfs4_readdir_callback,
                             &cb_data,
                             &cache_status) != CACHE_INODE_SUCCESS) {
          res_READDIR4.status = nfs4_Errno(cache_status);
          goto out;
     }

     if ((res_READDIR4.status = cb_data.error) != NFS4_OK) {
          goto out;
     }

     if (cb_data.count != 0) {
          /* Put the entry's list in the READDIR reply if there were any. */
          res_READDIR4.READDIR4res_u.resok4.reply.entries = entries;
     } else {
          gsh_free(entries);
          res_READDIR4.READDIR4res_u.resok4.reply.entries
               = entries = NULL;
     }

     res_READDIR4.READDIR4res_u.resok4.reply.eof = eod_met;

     /* Do not forget to set the verifier */
     memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf,
            cookie_verifier, NFS4_VERIFIER_SIZE);


     res_READDIR4.status = NFS4_OK;

out:
     if (res_READDIR4.status != NFS4_OK) {
          if (entries) {
               free_entries(entries);
          }
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
void nfs4_op_readdir_Free(READDIR4res *resp)
{
     free_entries(resp->READDIR4res_u.resok4.reply.entries);
} /* nfs4_op_readdir_Free */

static inline bool attribute_is_set(bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= bits->bitmap4_len)
		return FALSE;
	return !!(bits->bitmap4_val[offset] & (1 << (attr % 32)));
}

/**
 * @brief Populate entry4s when called from cache_inode_readdir
 *
 * This function is a callback passed to cache_inode_readdir.  It
 * fills in a pre-allocated array of entry4 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param opaque [in] Pointer to a struct nfs4_readdir_cb_data that is
 *                    gives the location of the array and other
 *                    bookeeping information
 * @param name [in] The filename for the current entry
 * @param handle [in] The current entry's filehandle
 * @param attrs [in] The current entry's attributes
 * @param cookie [in] The readdir cookie for the current entry
 */

static bool_t
nfs4_readdir_callback(void* opaque,
                      char *name,
                      cache_entry_t *entry,
                      bool_t attr_allowed,
                      fsal_op_context_t *context,
                      uint64_t cookie)
{
     struct nfs4_readdir_cb_data *tracker =
          (struct nfs4_readdir_cb_data *) opaque;
     size_t namelen = 0;
     char val_fh[NFS4_FHSIZE];
     nfs_fh4 entryFH = {
          .nfs_fh4_len = 0,
          .nfs_fh4_val = val_fh
     };

     if (tracker->total_entries == tracker->count) {
          return FALSE;
     }
     memset(val_fh, 0, NFS4_FHSIZE);
     /* Bits that don't require allocation */
     if (tracker->mem_left < sizeof(entry4)) {
          if (tracker->count == 0) {
               tracker->error = NFS4ERR_TOOSMALL;
          }
          return FALSE;
     }
     tracker->mem_left -= sizeof(entry4);
     tracker->entries[tracker->count].cookie = cookie;
     tracker->entries[tracker->count].nextentry = NULL;

     /* The filename.  We don't use str2utf8 because that has an
        additional copy into a buffer before copying into the
        destination. */

     namelen = strlen(name);
     if (tracker->mem_left < (namelen + 1)) {
          if (tracker->count == 0) {
               tracker->error = NFS4ERR_TOOSMALL;
          }
          return FALSE;
     }
     tracker->mem_left -= (namelen + 1);
     tracker->entries[tracker->count].name.utf8string_len = namelen;
     tracker->entries[tracker->count].name.utf8string_val = gsh_strdup(name);
     if(tracker->entries[tracker->count].name.utf8string_val == NULL) {
          return FALSE;
     }

     if(attr_allowed) {
          if ((tracker->req_attr.bitmap4_val != NULL) &&
              (tracker->req_attr.bitmap4_val[0] & FATTR4_FILEHANDLE)) {
               if (!nfs4_FSALToFhandle(&entryFH, &entry->handle, tracker->data)) {
                    tracker->error = NFS4ERR_SERVERFAULT;
                    gsh_free(tracker->entries[tracker->count].name.utf8string_val);
                    tracker->entries[tracker->count].name.utf8string_val = NULL;
                    return FALSE;
               }
          }
     }

     if(!attr_allowed) {
          /* cache_inode_readdir is signaling us that client didn't have
           * search permission in this directory, so we can't return any
           * attributes, but must indicate NFS4ERR_ACCESS.
           */
          if(nfs4_Fattr_Fill_Error(&tracker->entries[tracker->count].attrs,
                                   NFS4ERR_ACCESS) == -1) {
               tracker->error = NFS4ERR_SERVERFAULT;
               gsh_free(tracker->entries[tracker->count].name.utf8string_val);
               tracker->entries[tracker->count].name.utf8string_val = NULL;
               return FALSE;
          }
          
     } else {
          cache_inode_status_t attr_status;
          fsal_accessflags_t access_mask_attr = 0;

          /* Adjust access mask if ACL is asked for.
           * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
           */
          if(attribute_is_set(&tracker->req_attr, FATTR4_ACL)) {
               access_mask_attr |=
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
          }

          /* cache_inode_readdir holds attr_lock while making callback, so we
           * need to do access check with no mutex.
           */
          if (cache_inode_access_no_mutex(entry,
                                          access_mask_attr,
                                          context,
                                          &attr_status)
              != CACHE_INODE_SUCCESS) {
               LogFullDebug(COMPONENT_NFS_V4,
                            "permission check for attributes status=%s",
                            cache_inode_err_str(attr_status));

               if(nfs4_Fattr_Fill_Error(&tracker->entries[tracker->count].attrs,
                                        nfs4_Errno(attr_status)) == -1) {
                    tracker->error = NFS4ERR_SERVERFAULT;
                    gsh_free(tracker->entries[tracker->count].name.utf8string_val);
                    tracker->entries[tracker->count].name.utf8string_val = NULL;
                    return FALSE;
               }
          } else {
               if (nfs4_FSALattr_To_Fattr(tracker->data->pexport,
                                          &entry->attributes,
                                          &tracker->entries[tracker->count].attrs,
                                          tracker->data,
                                          &entryFH,
                                          &tracker->req_attr) != 0) {
                    LogFatal(COMPONENT_NFS_V4,
                             "nfs4_FSALattr_To_Fattr failed to convert attr");
               }
          }
     }

     if (tracker->mem_left <
         ((tracker->entries[tracker->count].attrs.attrmask.bitmap4_len *
           sizeof(uint32_t)) +
          (tracker->entries[tracker->count]
           .attrs.attr_vals.attrlist4_len))) {
          gsh_free(tracker->entries[tracker->count]
                   .attrs.attrmask.bitmap4_val);
          tracker->entries[tracker->count].attrs.attrmask.bitmap4_val = NULL;
          gsh_free(tracker->entries[tracker->count]
                   .attrs.attr_vals.attrlist4_val);
          tracker->entries[tracker->count].attrs.attr_vals.attrlist4_val = NULL;
          gsh_free(tracker->entries[tracker->count].name.utf8string_val);
          tracker->entries[tracker->count].name.utf8string_val = NULL;
          if (tracker->count == 0) {
               tracker->error = NFS4ERR_TOOSMALL;
          }
          return FALSE;
     }
     tracker->mem_left -=
          (tracker->entries[tracker->count].attrs.attrmask.bitmap4_len *
           sizeof(uint32_t));
     tracker->mem_left -=
          (tracker->entries[tracker->count].attrs.attr_vals.attrlist4_len);

     if (tracker->count != 0) {
          tracker->entries[tracker->count - 1].nextentry =
               &tracker->entries[tracker->count];
     }

     ++(tracker->count);
     return TRUE;
}

static void
free_entries(entry4 *entries)
{
     entry4 *entry = NULL;

     for (entry = entries;
          entry != NULL;
          entry = entry->nextentry) {
          if (entry->attrs.attrmask.bitmap4_val != NULL) {
               gsh_free(entry->attrs.attrmask.bitmap4_val);
          }
          if (entry->attrs.attr_vals.attrlist4_val != NULL) {
               gsh_free(entry->attrs.attr_vals.attrlist4_val);
          }
          if(entry->name.utf8string_val != NULL) {
               gsh_free(entry->name.utf8string_val);
          }
     }
     gsh_free(entries);

     return;
} /* free_entries */
