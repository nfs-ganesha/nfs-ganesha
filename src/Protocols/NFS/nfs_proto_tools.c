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
 * @file    nfs_proto_tools.c
 * @brief   A set of functions used to managed NFS.
 *
 * A set of functions used to managed NFS.
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
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <assert.h>
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
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "nfs4_acls.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "fsal.h"
#include "fsal_pnfs.h"
#include "pnfs_common.h"

#ifdef _USE_NFS4_ACL
/* Define mapping of NFS4 who name and type. */
static struct {
  char *string;
  int   stringlen;
  int type;
} whostr_2_type_map[] = {
  {
    .string    = "OWNER@",
    .stringlen = sizeof("OWNER@") - 1,
    .type      = FSAL_ACE_SPECIAL_OWNER,
  },
  {
    .string    = "GROUP@",
    .stringlen = sizeof("GROUP@") - 1,
    .type      = FSAL_ACE_SPECIAL_GROUP,
  },
  {
    .string    = "EVERYONE@",
    .stringlen = sizeof("EVERYONE@") - 1,
    .type      = FSAL_ACE_SPECIAL_EVERYONE,
  },
};
#endif                          /* _USE_NFS4_ACL */

/*
 * String representations of NFS protocol operations.
 */
char *nfsv2_function_names[] = {
  "NFSv2_null", "NFSv2_getattr", "NFSv2_setattr", "NFSv2_root",
  "NFSv2_lookup", "NFSv2_readlink", "NFSv2_read", "NFSv2_writecache",
  "NFSv2_write", "NFSv2_create", "NFSv2_remove", "NFSv2_rename",
  "NFSv2_link", "NFSv2_symlink", "NFSv2_mkdir", "NFSv2_rmdir",
  "NFSv2_readdir", "NFSv2_statfs"
};

char *nfsv3_function_names[] = {
  "NFSv3_null", "NFSv3_getattr", "NFSv3_setattr", "NFSv3_lookup",
  "NFSv3_access", "NFSv3_readlink", "NFSv3_read", "NFSv3_write",
  "NFSv3_create", "NFSv3_mkdir", "NFSv3_symlink", "NFSv3_mknod",
  "NFSv3_remove", "NFSv3_rmdir", "NFSv3_rename", "NFSv3_link",
  "NFSv3_readdir", "NFSv3_readdirplus", "NFSv3_fsstat",
  "NFSv3_fsinfo", "NFSv3_pathconf", "NFSv3_commit"
};

char *nfsv4_function_names[] = {
  "NFSv4_null", "NFSv4_compound"
};

char *mnt_function_names[] = {
  "MNT_null", "MNT_mount", "MNT_dump", "MNT_umount", "MNT_umountall", "MNT_export"
};

char *rquota_functions_names[] = {
  "rquota_Null", "rquota_getquota", "rquota_getquotaspecific", "rquota_setquota",
  "rquota_setquotaspecific"
};

/**
 *
 * Attribute bitmap decoders
 */

/* bitmap is up to 3 x uint32_t.
 *
 * Structure of the bitmap is as follows
 *
 *                  0         1          2
 *    +-------+---------+----------+----------+
 *    | count | 31 .. 0 | 63 .. 32 | 64 .. 95 |
 *    +-------+---------+----------+----------+
 *
 * One bit is set for every possible attributes. The bits are packed together
 * in a uint32_T (XDR alignment reason probably)
 * As said in the RFC3530, the n-th bit is with the uint32_t #(n/32),
 * and its position with the uint32_t is n % 32
 *
 * Example
 *     1st bit = FATTR4_TYPE            = 1
 *     2nd bit = FATTR4_LINK_SUPPORT    = 5
 *     3rd bit = FATTR4_SYMLINK_SUPPORT = 6
 *
 *     Juste one uint32_t is necessay: 2**1 + 2**5 + 2**6 = 2 + 32 + 64 = 98
 *   +---+----+
 *   | 1 | 98 |
 *   +---+----+
 *
 * Other Example
 *
 *     1st bit = FATTR4_LINK_SUPPORT    = 5
 *     2nd bit = FATTR4_SYMLINK_SUPPORT = 6
 *     3rd bit = FATTR4_MODE            = 33
 *     4th bit = FATTR4_OWNER           = 36
 *
 *     Two uint32_t will be necessary there:
 *            #1 = 2**5 + 2**6 = 32 + 64 = 96
 #            #2 = 2**(33-32) + 2**(36-32) = 2**1 + 2**4 = 2 + 16 = 18
 *   +---+----+----+
 *   | 2 | 98 | 18 |
 *   +---+----+----+
 *
 */

static inline int next_attr_from_bitmap(bitmap4 *bits, int last_attr)
{
	int offset, bit;

	for(offset = (last_attr + 1) / 32;
	    offset >= 0 && offset < bits->bitmap4_len;
	    offset++) {
		if((bits->bitmap4_val[offset] &
		    (-1 << ((last_attr +1) % 32))) != 0) {
			for(bit = (last_attr +1) % 32; bit < 32; bit++) {
				if(bits->bitmap4_val[offset] & (1 << bit))
					return offset * 32 + bit;
			}
		}
		last_attr = -1;
	}
	return -1;
}

static inline bool attribute_is_set(bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= bits->bitmap4_len)
		return FALSE;
	return !!(bits->bitmap4_val[offset] & (1 << (attr % 32)));
}

static inline bool set_attribute_in_bitmap(bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= 3)
		return FALSE; /* over upper bound */
	if(offset >= bits->bitmap4_len)
		bits->bitmap4_len = offset + 1; /* roll into the next word */
	bits->bitmap4_val[offset] |= (1 << (attr % 32));
	return TRUE;
}

static inline bool clear_attribute_in_bitmap(bitmap4 *bits, int attr)
{
	int offset = attr / 32;

	if(offset >= bits->bitmap4_len)
		return FALSE;
	bits->bitmap4_val[offset] &= ~(1 << (attr % 32));
	return TRUE;
}

/**
 *
 * nfs_FhandleToStr: Converts a file handle to a string representation.
 *
 * Converts a file handle to a string representation.
 *
 * @param rq_vers  [IN]    version of the NFS protocol to be used
 * @param pfh2     [IN]    NFSv2 file handle or NULL
 * @param pfh3     [IN]    NFSv3 file handle or NULL
 * @param pfh4     [IN]    NFSv4 file handle or NULL
 * @param str      [OUT]   string version of handle
 *
 */
void nfs_FhandleToStr(u_long     rq_vers,
                      fhandle2  *pfh2,
                      nfs_fh3   *pfh3,
                      nfs_fh4   *pfh4,
                      char      *str)
{

  switch (rq_vers)
    {
    case NFS_V4:
      sprint_fhandle4(str, pfh4);
      break;

    case NFS_V3:
      sprint_fhandle3(str, pfh3);
      break;

    case NFS_V2:
      sprint_fhandle2(str, pfh2);
      break;
    }
}                               /* nfs_FhandleToStr */

/**
 *
 * nfs_FhandleToCache: Gets a cache entry using a file handle (v2/3/4) as input.
 *
 * Gets a cache entry using a file handle (v2/3/4) as input.
 *
 * If a cache entry is returned, its refcount is +1.
 *
 * @param rq_vers  [IN]    version of the NFS protocol to be used
 * @param pfh2     [IN]    NFSv2 file handle or NULL
 * @param pfh3     [IN]    NFSv3 file handle or NULL
 * @param pfh4     [IN]    NFSv4 file handle or NULL
 * @param pstatus2 [OUT]   pointer to NFSv2 status or NULL
 * @param pstatus3 [OUT]   pointer to NFSv3 status or NULL
 * @param pstatus4 [OUT]   pointer to NFSv4 status or NULL
 * @param pattr    [OUT]   FSAL attributes related to this cache entry
 * @param pexport  [IN]    client's export
 * @param pclient  [IN]    client's ressources to be used for accessing the Cache Inode
 * @param prc      [OUT]   internal status for the request (NFS_REQ_DROP or NFS_REQ_OK)
 *
 * @return a pointer to the related pentry if successful, NULL is returned in case of a failure.
 *
 */

cache_entry_t *nfs_FhandleToCache(const struct req_op_context *req_ctx,
                                  u_long rq_vers,
                                  fhandle2 * pfh2,
                                  nfs_fh3 * pfh3,
                                  nfs_fh4 * pfh4,
                                  nfsstat2 * pstatus2,
                                  nfsstat3 * pstatus3,
                                  nfsstat4 * pstatus4,
                                  struct attrlist * pattr,
                                  exportlist_t *pexport,
                                  int *prc)
{
  cache_inode_fsal_data_t fsal_data;
  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  struct attrlist attr;
  short exportid = 0;
  char fkey_data[NFS4_FHSIZE];
  struct netbuf fkey = {.maxlen = sizeof(fkey_data), .buf = fkey_data};

  /* Default behaviour */
  *prc = NFS_REQ_OK;

  memset(&fsal_data, 0, sizeof(fsal_data));
  fsal_data.export = pexport->export_hdl;
  switch (rq_vers)
    {
    case NFS_V4:
      if(!nfs4_FhandleToFSAL(pfh4, &fkey, pexport->export_hdl))
        {
          *prc = NFS_REQ_OK;
          *pstatus4 = NFS4ERR_BADHANDLE;
          return NULL;
        }
      exportid = nfs4_FhandleToExportId(pfh4);
      break;

    case NFS_V3:
      if(!nfs3_FhandleToFSAL(pfh3, &fkey, pexport->export_hdl))
        {
          *prc = NFS_REQ_OK;
          *pstatus3 = NFS3ERR_BADHANDLE;
          return NULL;
        }
      exportid = nfs3_FhandleToExportId(pfh3);
      break;

    case NFS_V2:
      if(!nfs2_FhandleToFSAL(pfh2, &fkey, pexport->export_hdl))
        {
          *prc = NFS_REQ_OK;
          *pstatus2 = NFSERR_STALE;
          return NULL;
        }
      exportid = nfs2_FhandleToExportId(pfh2);
      break;
    }

  fsal_data.fh_desc.addr = fkey.buf;
  fsal_data.fh_desc.len = fkey.len;

  print_buff(COMPONENT_FILEHANDLE,
             fsal_data.fh_desc.addr,
             fsal_data.fh_desc.len);

  if((pexport = nfs_Get_export_by_id(nfs_param.pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      switch (rq_vers)
        {
        case NFS_V4:
          *pstatus4 = NFS4ERR_STALE;
          break;

        case NFS_V3:
          *pstatus3 = NFS3ERR_STALE;
          break;

        case NFS_V2:
          *pstatus2 = NFSERR_STALE;
          break;
        }
      *prc = NFS_REQ_DROP;

      LogFullDebug(COMPONENT_NFSPROTO,
                   "Invalid file handle passed to nfsFhandleToCache ");
      return NULL;
    }

  if((pentry = cache_inode_get(&fsal_data, &attr,
                               NULL, req_ctx, &cache_status)) == NULL)
    {
      switch (rq_vers)
        {
        case NFS_V4:
          *pstatus4 = NFS4ERR_STALE;
          break;

        case NFS_V3:
          *pstatus3 = NFS3ERR_STALE;
          break;

        case NFS_V2:
          *pstatus2 = NFSERR_STALE;
          break;
        }
      *prc = NFS_REQ_OK;
      return NULL;
    }

  if(pattr != NULL)
    *pattr = attr;

  return pentry;
}                               /* nfs_FhandleToCache */

/**
 *
 * nfs_SetPostOpAttr: Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * @param pexport    [IN]  the related export entry
 * @param pfsal_attr [IN]  FSAL attributes
 * @param pattr      [OUT] NFSv3 PostOp structure attributes.
 *
 */
void nfs_SetPostOpAttr(exportlist_t *pexport,
                       const struct attrlist *pfsal_attr,
                       post_op_attr *presult)
{
  presult->attributes_follow
           = nfs3_FSALattr_To_Fattr(pexport,
                                    pfsal_attr,
                                    &(presult->post_op_attr_u.attributes));
} /* nfs_SetPostOpAttr */

/**
 *
 * nfs_SetPreOpAttr: Converts FSAL Attributes to NFSv3 PreOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PreOp Attributes structure.
 *
 * @param pfsal_attr [IN]  FSAL attributes.
 * @param pattr      [OUT] NFSv3 PreOp structure attributes.
 *
 * @return nothing (void function)
 *
 */
void nfs_SetPreOpAttr(const struct attrlist *pfsal_attr,
                      pre_op_attr *pattr)
{
  if(pfsal_attr == NULL)
    {
      pattr->attributes_follow = FALSE;
    }
  else
    {
      pattr->pre_op_attr_u.attributes.size = pfsal_attr->filesize;
      pattr->pre_op_attr_u.attributes.mtime.seconds = pfsal_attr->mtime.seconds;
      pattr->pre_op_attr_u.attributes.mtime.nseconds = 0 ;

      pattr->pre_op_attr_u.attributes.ctime.seconds = pfsal_attr->ctime.seconds;
      pattr->pre_op_attr_u.attributes.ctime.nseconds = 0;

      pattr->attributes_follow = TRUE;
    }
}                               /* nfs_SetPreOpAttr */

/**
 *
 * nfs_SetWccData: Sets NFSv3 Weak Cache Coherency structure.
 *
 * Sets NFSv3 Weak Cache Coherency structure.
 *
 * @param pexport      [IN]  export entry
 * @param pentry       [IN]  related pentry
 * @param pbefore_attr [IN]  the attributes before the operation.
 * @param pafter_attr  [IN]  the attributes after the operation
 * @param pwcc_data    [OUT] the Weak Cache Coherency structure
 *
 * @return nothing (void function).
 *
 */
void nfs_SetWccData(exportlist_t *pexport,
                    const struct attrlist *pbefore_attr,
                    const struct attrlist *pafter_attr,
                    wcc_data *pwcc_data)
{
  /* Build directory pre operation attributes */
  nfs_SetPreOpAttr(pbefore_attr, &(pwcc_data->before));

  /* Build directory post operation attributes */
  nfs_SetPostOpAttr(pexport, pafter_attr, &(pwcc_data->after));
}                               /* nfs_SetWccData */

/**
 *
 * nfs_RetryableError: Indicates if an error is retryable or not.
 *
 * Indicates if an error is retryable or not.
 *
 * @param cache_status [IN] input Cache Inode Status value, to be tested.
 *
 * @return true if retryable, false otherwise.
 *
 * @todo: Not implemented for NOW BUGAZEOMEU
 *
 */
int nfs_RetryableError(cache_inode_status_t cache_status)
{
  switch (cache_status)
    {
    case CACHE_INODE_IO_ERROR:
      if(nfs_param.core_param.drop_io_errors)
        {
          /* Drop the request */
          return true;
        }
      else
        {
          /* Propagate error to the client */
          return false;
        }
      break;

    case CACHE_INODE_INVALID_ARGUMENT:
      if(nfs_param.core_param.drop_inval_errors)
        {
          /* Drop the request */
          return true;
        }
      else
        {
          /* Propagate error to the client */
          return false;
        }
      break;

    case CACHE_INODE_DELAY:
      if(nfs_param.core_param.drop_delay_errors)
        {
          /* Drop the request */
          return true;
        }
      else
        {
          /* Propagate error to the client */
          return false;
        }
      break;

    case CACHE_INODE_SUCCESS:
      LogCrit(COMPONENT_NFSPROTO,
              "Possible implementation error: CACHE_INODE_SUCCESS managed as an error");
      return false;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_INSERT_ERROR:
      /* Internal error, should be dropped and retryed */
      return true;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
    case CACHE_INODE_BAD_TYPE:
    case CACHE_INODE_ENTRY_EXISTS:
    case CACHE_INODE_DIR_NOT_EMPTY:
    case CACHE_INODE_NOT_FOUND:
    case CACHE_INODE_FSAL_EACCESS:
    case CACHE_INODE_IS_A_DIRECTORY:
    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_NO_SPACE_LEFT:
    case CACHE_INODE_READ_ONLY_FS:
    case CACHE_INODE_KILLED:
    case CACHE_INODE_FSAL_ESTALE:
    case CACHE_INODE_FSAL_ERR_SEC:
    case CACHE_INODE_QUOTA_EXCEEDED:
    case CACHE_INODE_NOT_SUPPORTED:
    case CACHE_INODE_NAME_TOO_LONG:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
    case CACHE_INODE_BAD_COOKIE:
    case CACHE_INODE_FILE_BIG:
    case CACHE_INODE_FILE_OPEN:
      /* Non retryable error, return error to client */
      return false;
      break;
    }

  /* Should never reach this */
  LogDebug(COMPONENT_NFSPROTO,
           "cache_inode_status=%u not managed properly in nfs_RetryableError, line %u should never be reached",
           cache_status, __LINE__);
  return false;
}

void nfs_SetFailedStatus(exportlist_t *pexport,
                         int version,
                         cache_inode_status_t status,
                         nfsstat2 *pstatus2,
                         nfsstat3 *pstatus3,
                         cache_entry_t *pentry0,
                         post_op_attr *ppost_op_attr,
                         cache_entry_t *pentry1,
                         const struct attrlist *ppre_vattr1,
                         wcc_data *pwcc_data1,
                         cache_entry_t * pentry2,
                         const struct attrlist *ppre_vattr2,
                         wcc_data *pwcc_data2)
{
  switch (version)
    {
    case NFS_V2:
      if(status != CACHE_INODE_SUCCESS) /* Should not use success to address a failed status */
        *pstatus2 = nfs2_Errno(status);
      break;

    case NFS_V3:
      if(status != CACHE_INODE_SUCCESS) /* Should not use success to address a failed status */
        *pstatus3 = nfs3_Errno(status);

      if(ppost_op_attr != NULL)
        nfs_SetPostOpAttr(pexport, NULL, ppost_op_attr);

      if(pwcc_data1 != NULL)
        nfs_SetWccData(pexport, ppre_vattr1, NULL, pwcc_data1);

      if(pwcc_data2 != NULL)
        nfs_SetWccData(pexport, ppre_vattr2, NULL, pwcc_data2);
      break;

    }
}

/* NFSv4.0+ Attribute management
 * XDR encode/decode/compare functions for FSAL <-> Fattr4 translations
 * There is a set of functions for each and every attribute in the tables
 * on page 39-46 of RFC3530.  The translate between internal and the wire.
 * Ordered by attribute number
 */

/*
 * FATTR4_SUPPORTED_ATTRS
 */

/** supported attributes
 *  this drives off of the table but it really should drive off of the
 *  fs_supports in the export
 */

static fattr_xdr_result encode_supported_attrs(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t bitmap[3];
	bitmap4 bits;
	int attr, offset;

	bits.bitmap4_len = 0;  /* start empty */
	memset(bitmap, 0, sizeof(bitmap));
	bits.bitmap4_val = bitmap;
	for(attr = FATTR4_SUPPORTED_ATTRS;
	    attr <= FATTR4_FS_CHARSET_CAP;
	    attr++) {
		if(fattr4tab[attr].supported)
			assert(set_attribute_in_bitmap(&bits, attr));
	}
	if( !xdr_u_int32_t(xdr, &bits.bitmap4_len))
		return FATTR_XDR_FAILED;
	for(offset = 0; offset < bits.bitmap4_len; offset++) {
		if( !xdr_u_int32_t(xdr, &bits.bitmap4_val[offset]))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_supported_attrs(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TYPE
 */

static fattr_xdr_result encode_type(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_type;

	switch (args->attrs->type) {
	case REGULAR_FILE:
	case EXTENDED_ATTR:
		file_type = NF4REG;        /* Regular file */
		break;
	case DIRECTORY:
		file_type = NF4DIR;        /* Directory */
		break;
	case BLOCK_FILE:
		file_type = NF4BLK;        /* Special File - block device */
		break;
	case CHARACTER_FILE:
		file_type = NF4CHR;        /* Special File - character device */
		break;
	case SYMBOLIC_LINK:
		file_type = NF4LNK;        /* Symbolic Link */
		break;
	case SOCKET_FILE:
		file_type = NF4SOCK;       /* Special File - socket */
		break;
	case FIFO_FILE:
		file_type = NF4FIFO;       /* Special File - fifo */
		break;
	default:  /* includes NO_FILE_TYPE & FS_JUNCTION: */
		return FATTR_XDR_FAILED; /* silently skip bogus? */
	}                   /* switch( pattr->type ) */
	if( !xdr_u_int32_t(xdr, &file_type))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_type(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t t;

	if( !xdr_u_int32_t(xdr, &t))
		return FATTR_XDR_FAILED;
	switch (t) {
	case NF4REG:
		args->attrs->type = REGULAR_FILE;
		break;
	case NF4DIR:
		args->attrs->type = DIRECTORY;
		break;
	case NF4BLK:
		args->attrs->type = BLOCK_FILE;
		break;
	case NF4CHR:
		args->attrs->type = CHARACTER_FILE;
		break;
	case NF4LNK:
		args->attrs->type = SYMBOLIC_LINK;
		break;
	case NF4SOCK:
		args->attrs->type = SOCKET_FILE;
		break;
	case NF4FIFO:
		args->attrs->type = FIFO_FILE;
		break;
	default:
		/* For wanting of a better solution */
		return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_FH_EXPIRE_TYPE
 */

static fattr_xdr_result encode_expiretype(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t expire_type;

	/* For the moment, we handle only the persistent filehandle */
	if(nfs_param.nfsv4_param.fh_expire == TRUE)
		expire_type = FH4_VOLATILE_ANY;
	else
		expire_type = FH4_PERSISTENT;
	if( !xdr_u_int32_t(xdr, &expire_type))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_expiretype(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHANGE
 */

static fattr_xdr_result encode_change(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int64_t(xdr, &args->attrs->change))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_change(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t change;

	if( !xdr_u_int64_t(xdr, &change))
		return FATTR_XDR_FAILED;
	args->attrs->chgtime.seconds = (uint32_t)change;
	args->attrs->chgtime.nseconds = 0;
	args->attrs->change =  change;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_SIZE
 */

static fattr_xdr_result encode_filesize(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int64_t(xdr, &args->attrs->filesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_filesize(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int64_t(xdr, &args->attrs->filesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_LINK_SUPPORT
 */

static fattr_xdr_result encode_linksupport(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int linksupport;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		linksupport = export->ops->fs_supports(export, link_support);
	} else {
		linksupport = TRUE;
	}
	if( !xdr_bool(xdr, &linksupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_linksupport(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SYMLINK_SUPPORT
 */

static fattr_xdr_result encode_symlinksupport(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int symlinksupport;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		symlinksupport = export->ops->fs_supports(export, symlink_support);
	} else {
		symlinksupport = TRUE;
	}
	if( !xdr_bool(xdr, &symlinksupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_symlinksupport(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_NAMED_ATTR
 */

/* For this version of the binary, named attributes is not supported */

static fattr_xdr_result encode_namedattrsupport(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int namedattrsupport;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		namedattrsupport = export->ops->fs_supports(export, named_attr);
	} else {
		namedattrsupport = FALSE;
	}
	if( !xdr_bool(xdr, &namedattrsupport))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_namedattrsupport(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FSID
 */

static fattr_xdr_result encode_fsid(XDR *xdr, struct xdr_attrs_args *args)
{
	fsid4 fsid;

	/* The file system id (taken from the configuration file)
	 * If object is a directory attached to a referral,
	 * then a different fsid is to be returned
	 * to tell the client that a different fs is being crossed */

	if(args->data != NULL && args->data->pexport != NULL) {
		if(nfs4_Is_Fh_Referral(args->hdl4)) {
			fsid.major = ~args->data->pexport->filesystem_id.major;
			fsid.minor = ~args->data->pexport->filesystem_id.minor;
		} else {
			fsid.major = args->data->pexport->filesystem_id.major;
			fsid.minor = args->data->pexport->filesystem_id.minor;
		}
	} else {
		fsid.major = 152LL; /* 153,153 for junctions in pseudos */
		fsid.minor = 152LL;
	}
	if( !xdr_u_int64_t(xdr, &fsid.major))
		return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &fsid.minor))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fsid(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int64_t(xdr, &args->attrs->fsid.major))
		return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->attrs->fsid.minor))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_UNIQUE_HANDLES
 */

static fattr_xdr_result encode_uniquehandles(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	int uniquehandles;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		uniquehandles = export->ops->fs_supports(export, unique_handles);
	} else {
		uniquehandles = TRUE;
	}
	if( !xdr_bool(xdr, &uniquehandles))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_uniquehandles(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LEASE_TIME
 */

static fattr_xdr_result encode_leaselife(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int32_t(xdr, &nfs_param.nfsv4_param.lease_lifetime))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_leaselife(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RDATTR_ERROR
 */

/** @TODO we don't really do anything with rdattr_error.  It is needed for full readdir
 * error handling.  Check this to be correct when we do...
 */

static fattr_xdr_result encode_rdattr_error(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int32_t(xdr, &args->rdattr_error))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_rdattr_error(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int32_t(xdr, &args->rdattr_error))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_ACL
 */

static fattr_xdr_result encode_acl(XDR *xdr, struct xdr_attrs_args *args)
{
#ifdef _USE_NFS4_ACL
	fsal_ace_t *pace;

      LogFullDebug(COMPONENT_NFS_V4,
                   "Number of ACEs = %u",
		   args->attrs->acl->naces);
	if(args->attrs->acl) {
		int rc = 0, i;
		char buff[MAXNAMLEN];
		char *name;

		if( !xdr_u_int32_t(xdr, &args->attrs->acl->naces))
			return FATTR_XDR_FAILED;
		for(pace = args->attrs->acl->aces;
		    pace < args->attrs->acl->aces + args->attrs->acl->naces;
		    pace++) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "type=0X%x, flag=0X%x, perm=0X%x",
				     pace->type, pace->flag, pace->perm);
			if( !xdr_u_int32_t(xdr, &pace->type))
				return FATTR_XDR_FAILED;
			if( !xdr_u_int32_t(xdr, &pace->flag))
				return FATTR_XDR_FAILED;
			if( !xdr_u_int32_t(xdr, &pace->perm))
				return FATTR_XDR_FAILED;
			if(IS_FSAL_ACE_GROUP_ID(*pace)) { /* Encode group name. */
				rc = gid2name(buff, &pace->who.gid);
				if(rc == 0) { /* Failure. */
					      /* Encode gid itself without @. */
					sprintf(buff, "%u", pace->who.gid);
				}
				name = buff;
			} else {
				if(IS_FSAL_ACE_SPECIAL_ID(*pace)) {
					for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++) {
						if (whostr_2_type_map[i].type == pace->who.uid) {
							name = whostr_2_type_map[i].string;
							break;
						}
					}
				} else {
					rc = uid2name(buff, &pace->who.uid);
					if(rc == 0) { /* Failure. */
						/* Encode uid itself without @. */
						sprintf(buff, "%u", pace->who.uid);
					}
					name = buff;
				}

			}
			LogFullDebug(COMPONENT_NFS_V4,
				     "special = %u, %s = %u, name = %s",
				     IS_FSAL_ACE_SPECIAL_ID(*pace),
				     IS_FSAL_ACE_GROUP_ID(*pace) ? "gid" : "uid",
				     IS_FSAL_ACE_GROUP_ID(*pace) ?
				     pace->who.gid : pace->who.uid,
				     name);
			if( !xdr_string(xdr, &name, MAXNAMLEN))
			    return FATTR_XDR_FAILED;
		} /* for pace... */
	} else {
		uint32_t noacls = 0;
		if( !xdr_u_int32_t(xdr, &noacls))
			return FATTR_XDR_FAILED;
	}
#else
	{
		uint32_t noacls = 0;
		if( !xdr_u_int32_t(xdr, &noacls))
			return FATTR_XDR_FAILED;
	}		
#endif                          /* _USE_NFS4_ACL */
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_acl(XDR *xdr, struct xdr_attrs_args *args)
{
#ifdef _USE_NFS4_ACL
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace;
	char buffer[MAXNAMLEN];
	utf8string utf8buffer;
	int who;

	if( !xdr_u_int32_t(xdr, &acldata.naces))
		return FATTR_XDR_FAILED;
	if(acldata.naces == 0)
		return FATTR_XDR_SUCCESS; /* no acls is not a crime */
	acldata.aces = (fsal_ace_t *)nfs4_ace_alloc(acldata.naces);
	if(acldata.aces == NULL) {
		LogCrit(COMPONENT_NFS_V4,
			"Failed to allocate ACEs");
		args->nfs_status = NFS4ERR_SERVERFAULT;
		return FATTR_XDR_FAILED;
	}
	memset(acldata.aces, 0, acldata.naces * sizeof(fsal_ace_t));
	for(pace = acldata.aces; pace < acldata.aces + acldata.naces; pace++) {
		if( !xdr_u_uint32_t(xdr, &pace->type))
			goto baderr;
		if( !xdr_u_uint32_t(xdr, &pace->flag))
			goto baderr;
		if( !xdr_u_uint32_t(xdr, &pace->perm))
			goto baderr;
		if( !xdr_string(xdr, buffer, MAXNAMLEN))
			goto baderr;
		utf8buffer.utf8string_val = buffer;
		utf8buffer.utf8string_len = strlen(buffer);
		if(nfs4_decode_acl_special_user(&utf8buffer, &who) == 0) {
			/* Clear group flag for special users */
			pace->flag &= ~(FSAL_ACE_FLAG_GROUP_ID);
			pace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
			pace->who.uid = who;
			LogFullDebug(COMPONENT_NFS_V4,
				     "ACE special who.uid = 0x%x",
				     pace->who.uid);
		} else {
			if(pace->flag == FSAL_ACE_FLAG_GROUP_ID) { /* Decode group. */
				utf82gid(&utf8buffer, &(pace->who.gid));
				LogFullDebug(COMPONENT_NFS_V4,
					     "ACE who.gid = 0x%x",
					     pace->who.gid);
			} else {  /* Decode user. */
				utf82uid(&utf8buffer, &(pace->who.uid));
				LogFullDebug(COMPONENT_NFS_V4,
					     "ACE who.uid = 0x%x",
					     pace->who.uid);
			}
		}

		/* Check if we can map a name string to uid or gid. If we can't, do cleanup
		 * and bubble up NFS4ERR_BADOWNER. */
		if((pace->flag == FSAL_ACE_FLAG_GROUP_ID ? pace->who.gid : pace->who.uid) == -1) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "ACE bad owner");
			args->nfs_status =  NFS4ERR_BADOWNER;
			goto baderr;
		}
	}
	args->attrs->acl = nfs4_acl_new_entry(&acldata, &status);
	if(args->attrs->acl == NULL) {
		LogCrit(COMPONENT_NFS_V4,
			"Failed to create a new entry for ACL");
		args->nfs_status =  NFS4ERR_SERVERFAULT;
		return FATTR_XDR_FAILED;  /* acldata has already been freed */
	} else {
		LogFullDebug(COMPONENT_NFS_V4,
			     "Successfully created a new entry for ACL, status = %u",
			     status);
	}
	/* Set new ACL */
	LogFullDebug(COMPONENT_NFS_V4, "new acl = %p", args->attrs->acl);
	return FATTR_XDR_SUCCESS;

baderr:
	nfs4_ace_free(acldata.aces);
#endif                          /* _USE_NFS4_ACL */
	return FATTR_XDR_FAILED;
}

/*
 * FATTR4_ACLSUPPORT
 */

static fattr_xdr_result encode_aclsupport(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t aclsupport;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		aclsupport = export->ops->fs_acl_support(export);
	} else {
		aclsupport = FALSE;
	}
	if( !xdr_u_int32_t(xdr, &aclsupport))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}


static fattr_xdr_result decode_aclsupport(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_ARCHIVE
 */

static fattr_xdr_result encode_archive(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t archive;

	archive = FALSE;
	if( !xdr_bool(xdr, &archive))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}


static fattr_xdr_result decode_archive(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CANSETTIME
 */

static fattr_xdr_result encode_cansettime(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t cansettime;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		cansettime = export->ops->fs_supports(export, cansettime);;
	} else {
		cansettime = TRUE;
	}
	if( !xdr_bool(xdr, &cansettime))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_cansettime(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CASE_INSENSITIVE
 */

static fattr_xdr_result encode_case_insensitive(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t caseinsensitive;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		caseinsensitive = export->ops->fs_supports(export,
							    case_insensitive);
	} else {
		caseinsensitive = FALSE;
	}
	if( !xdr_bool(xdr, &caseinsensitive))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_case_insensitive(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CASE_PRESERVING
 */

static fattr_xdr_result encode_case_preserving(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t casepreserving;

	
	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		casepreserving = export->ops->fs_supports(export, case_preserving);
	} else {
		casepreserving = TRUE;
	}
	if( !xdr_bool(xdr, &casepreserving))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_case_preserving(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHOWN_RESTRICTED
 */

static fattr_xdr_result encode_chown_restricted(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t chownrestricted;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		chownrestricted = export->ops->fs_supports(export, chown_restricted);
	} else {
		chownrestricted = TRUE;
	}
	if( !xdr_bool(xdr, &chownrestricted))
		return FATTR_XDR_FAILED;

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_chown_restricted(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FILEHANDLE
 */

static fattr_xdr_result encode_filehandle(XDR *xdr, struct xdr_attrs_args *args)
{
	if(args->hdl4 == NULL || args->hdl4->nfs_fh4_val == NULL)
		return FATTR_XDR_FAILED;
	if( !xdr_bytes(xdr,
		       &args->hdl4->nfs_fh4_val,
		       &args->hdl4->nfs_fh4_len,
		       NFS4_FHSIZE))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/* zero copy file handle reference dropped as potentially unsafe XDR */

static fattr_xdr_result decode_filehandle(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t fhlen, pos;

	if(args->hdl4 == NULL || args->hdl4->nfs_fh4_val == NULL) {
		if( !xdr_u_int32_t(xdr, &fhlen))
			return FATTR_XDR_FAILED;
		pos = xdr_getpos(xdr);
		if( !xdr_setpos(xdr, pos+fhlen))
			return FATTR_XDR_FAILED;
	} else {
		if( !xdr_bytes(xdr,
			       &args->hdl4->nfs_fh4_val,
			       &args->hdl4->nfs_fh4_len,
			       NFS4_FHSIZE))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_FILEID
 */

static fattr_xdr_result encode_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int64_t(xdr, &args->attrs->fileid))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	/* The analog to the inode number.
	 * RFC3530 says "a number uniquely identifying the file within the filesystem"
	 * I use hpss_GetObjId to extract this information from the Name Server's handle
	 */
	if( !xdr_u_int64_t(xdr, &args->attrs->fileid))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * Dynamic file system info
 */

static fattr_xdr_result encode_fetch_fsinfo(struct xdr_attrs_args *args)
{
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

	if(args->data != NULL && args->data->current_entry != NULL) {
		   cache_status = cache_inode_statfs(args->data->current_entry,
						     args->dynamicinfo,
						     args->data->req_ctx);
	} else {
		args->dynamicinfo->avail_files = 512;
		args->dynamicinfo->free_files = 512;
		args->dynamicinfo->total_files = 512;
		args->dynamicinfo->total_bytes = 1024000;
		args->dynamicinfo->free_bytes = 512000;
		args->dynamicinfo->avail_bytes = 512000;
	}
	if(cache_status == CACHE_INODE_SUCCESS) {
                args->statfscalled = 1;
		return TRUE;
	} else {
		return FATTR_XDR_FAILED;
	}
}

/*
 * FATTR4_FILES_AVAIL
 */

static fattr_xdr_result encode_files_avail(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->avail_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_avail(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FILES_FREE
 */

static fattr_xdr_result encode_files_free(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->free_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_free(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FILES_TOTAL
 */

static fattr_xdr_result encode_files_total(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->total_files))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_files_total(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_LOCATIONS
 */

static fattr_xdr_result encode_fs_locations(XDR *xdr, struct xdr_attrs_args *args)
{
/** @TODO the parse part should be done at export time to a simple struct
 *  the parse is memory and memcpy hungry! NOOP it for now.
 */
/*           if(data->current_entry->type != DIRECTORY) */
/*             { */
/* 	      continue; */
/*             } */

/*           if(!nfs4_referral_str_To_Fattr_fs_location */
/*              (data->current_entry->object.dir.referral, tmp_buff, &tmp_int)) */
/*             { */
/* 	      continue; */
/*             } */

/*           memcpy((char *)(current_pos), tmp_buff, tmp_int); */
/*           LastOffset += tmp_int; */
/*           break; */

	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fs_locations(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_HIDDEN
 */

static fattr_xdr_result encode_hidden(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t hidden = FALSE;

	if( !xdr_bool(xdr, &hidden))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_hidden(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_HOMOGENEOUS
 */
/* Unix semantic is homogeneous (all objects have the same kind of attributes) */

static fattr_xdr_result encode_homogeneous(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t homogeneous;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		homogeneous = export->ops->fs_supports(export, homogenous);
	} else {
		homogeneous = TRUE;
	}
	if( !xdr_bool(xdr, &homogeneous))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_homogeneous(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXFILESIZE
 */

static fattr_xdr_result encode_maxfilesize(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint64_t maxfilesize;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		maxfilesize = export->ops->fs_maxfilesize(export);
	} else {
		maxfilesize = FSINFO_MAX_FILESIZE;
	}
	if( !xdr_u_int64_t(xdr, &maxfilesize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxfilesize(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXLINK
 */

static fattr_xdr_result encode_maxlink(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t maxlink;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		maxlink = export->ops->fs_maxlink(export);
	} else {
		maxlink = MAX_HARD_LINK_VALUE;
	}
	if( !xdr_u_int32_t(xdr, &maxlink))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxlink(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXNAME
 */

static fattr_xdr_result encode_maxname(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t maxname;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		maxname = export->ops->fs_maxnamelen(export);
	} else {
		maxname = MAXNAMLEN;
	}
	if( !xdr_u_int32_t(xdr, &maxname))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxname(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MAXREAD
 */

          /* The exports.c MAXREAD-MAXWRITE code establishes these semantics: 
           *  a. If you set the MaxWrite and MaxRead defaults in an export file
           *  they apply. 
           *  b. If you set the MaxWrite and MaxRead defaults in the main.conf
           *  file they apply unless overwritten by an export file setting. 
           *  c. If no settings are present in the export file or the main.conf
           *  file then the defaults values in the FSAL apply. 
           */
/** @TODO make these conditionals go away.  The old code was the 'else' part of this.
 * this is a fast path. Do both read and write conditionals.
 */

static fattr_xdr_result encode_maxread(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint64_t maxread;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		if((args->data->pexport->options & EXPORT_OPTION_MAXREAD) == EXPORT_OPTION_MAXREAD)
			maxread = args->data->pexport->MaxRead;
		else
			maxread = export->ops->fs_maxread(export);
	} else {
		maxread = NFS4_PSEUDOFS_MAX_READ_SIZE;
	}
	if( !xdr_u_int64_t(xdr, &maxread))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxread(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_MAXWRITE
 */

static fattr_xdr_result encode_maxwrite(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint64_t maxwrite;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		if((args->data->pexport->options & EXPORT_OPTION_MAXWRITE) == EXPORT_OPTION_MAXWRITE)
			maxwrite = args->data->pexport->MaxWrite;
		else
			maxwrite = export->ops->fs_maxwrite(export);
	} else {
		maxwrite = NFS4_PSEUDOFS_MAX_WRITE_SIZE;
	}
	if( !xdr_u_int64_t(xdr, &maxwrite))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_maxwrite(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_MIMETYPE
 */

static fattr_xdr_result encode_mimetype(XDR *xdr, struct xdr_attrs_args *args)
{
	int mimetype = FALSE;

	if( !xdr_bool(xdr, &mimetype))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mimetype(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MODE
 */

static fattr_xdr_result encode_mode(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_mode = fsal2unix_mode(args->attrs->mode);

	if( !xdr_u_int32_t(xdr, &file_mode))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mode(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t file_mode;

	if( !xdr_u_int32_t(xdr, &file_mode))
		return FATTR_XDR_FAILED;
	args->attrs->mode = unix2fsal_mode(file_mode);
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_NO_TRUNC
 */

static fattr_xdr_result encode_no_trunc(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	uint32_t no_trunc;

	if(args->data != NULL && args->data->pexport != NULL) {
		export = args->data->pexport->export_hdl;
		no_trunc = export->ops->fs_supports(export, no_trunc);
	} else {
		no_trunc = TRUE;
	}
	if( !xdr_bool(xdr, &no_trunc))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_no_trunc(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_NUMLINKS
 */

static fattr_xdr_result encode_numlinks(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int32_t(xdr, &args->attrs->numlinks))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_numlinks(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !xdr_u_int32_t(xdr, &args->attrs->numlinks))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_OWNER
 */

static fattr_xdr_result encode_owner(XDR *xdr, struct xdr_attrs_args *args)
{
	int rc;
	char buff[MAXNAMLEN];
	char *owner = buff;

	rc = uid2str(args->attrs->owner, owner);
	if(rc < 0)
		return FATTR_XDR_FAILED;
	if( !xdr_string(xdr, &owner, strlen(owner)))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_owner(XDR *xdr, struct xdr_attrs_args *args)
{
	char buff[MAXNAMLEN];
	char *owner = buff;
	uid_t uid;

	if( !xdr_string(xdr, &owner, MAXNAMLEN))
		return FATTR_XDR_FAILED;
	if(name2uid(owner, &uid) == 0)
		return FATTR_XDR_FAILED;
	args->attrs->owner = uid; /* uint64_t = uid_t ??? */
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_OWNER_GROUP
 */

static fattr_xdr_result encode_group(XDR *xdr, struct xdr_attrs_args *args)
{
	int rc;
	char buff[MAXNAMLEN];
	char *group = buff;

	rc = gid2str(args->attrs->group, group);
	if(rc < 0)
		return FATTR_XDR_FAILED;
	if( !xdr_string(xdr, &group, strlen(group)))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_group(XDR *xdr, struct xdr_attrs_args *args)
{
	char buff[MAXNAMLEN];
	char *group = buff;
	gid_t gid;

	if( !xdr_string(xdr, &group, MAXNAMLEN))
		return FATTR_XDR_FAILED;
	if(name2gid(group, &gid) == 0)
		return FATTR_XDR_FAILED;
	args->attrs->group = gid;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_QUOTA_AVAIL_HARD
 */

static fattr_xdr_result encode_quota_avail_hard(XDR *xdr, struct xdr_attrs_args *args)
{
/** @todo: not the right answer, actual quotas should be implemented */
	uint64_t quota = NFS_V4_MAX_QUOTA_HARD;

	if( !xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_avail_hard(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_QUOTA_AVAIL_SOFT
 */

static fattr_xdr_result encode_quota_avail_soft(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t quota = NFS_V4_MAX_QUOTA_SOFT;

	if( !xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_avail_soft(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_QUOTA_USED
 */

static fattr_xdr_result encode_quota_used(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t quota = args->attrs->filesize;

	if( !xdr_u_int64_t(xdr, &quota))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_quota_used(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_RAWDEV
 */

static fattr_xdr_result encode_rawdev(XDR *xdr, struct xdr_attrs_args *args)
{
	struct specdata4 specdata4;

	specdata4.specdata1 = args->attrs->rawdev.major;
	specdata4.specdata2 = args->attrs->rawdev.minor;
	if( !xdr_u_int64_t(xdr, (uint64_t *)&specdata4))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_rawdev(XDR *xdr, struct xdr_attrs_args *args)
{
	struct specdata4 specdata4;
	
	if( !xdr_u_int64_t(xdr, (uint64_t *)&specdata4))
		return FATTR_XDR_FAILED;
	args->attrs->rawdev.major = specdata4.specdata1;
	args->attrs->rawdev.minor = specdata4.specdata2;
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_SPACE_AVAIL
 */

static fattr_xdr_result encode_space_avail(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->avail_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_space_avail(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SPACE_FREE
 */

static fattr_xdr_result encode_space_free(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->free_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_space_free(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SPACE_TOTAL
 */

static fattr_xdr_result encode_space_total(XDR *xdr, struct xdr_attrs_args *args)
{
	if( !args->statfscalled)
		if( !encode_fetch_fsinfo(args))
			return FATTR_XDR_FAILED;
	if( !xdr_u_int64_t(xdr, &args->dynamicinfo->total_bytes))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_space_total(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SPACE_USED
 */

/* the number of bytes on the filesystem used by the object
 * which is slightly different 
 * from the file's size (there can be hole in the file)
 */

static fattr_xdr_result encode_spaceused(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t space = args->attrs->spaceused;

	if( !xdr_u_int64_t(xdr, &space))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_spaceused(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t space;

	if( !xdr_u_int64_t(xdr, &space))
		return FATTR_XDR_FAILED;
	args->attrs->spaceused = space;
	return TRUE;
}

/*
 * FATTR4_SYSTEM
 */

/* This is not a windows system File-System with respect to the regarding API */

static fattr_xdr_result encode_system(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t system = FALSE;

	if( !xdr_bool(xdr, &system))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_system(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * Time conversions
 */

static inline fattr_xdr_result encode_time(XDR *xdr, gsh_time_t *ts)
{
	uint64_t seconds = ts->seconds;
	uint32_t nseconds = ts->nseconds;
	if( !xdr_u_int64_t(xdr, &seconds))
		return FATTR_XDR_FAILED;
	if( !xdr_u_int32_t(xdr, &nseconds))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static inline fattr_xdr_result decode_time(XDR *xdr,
					   struct xdr_attrs_args *args,
					   gsh_time_t *ts)
{
	uint64_t seconds;
	uint32_t nseconds;

	if( !xdr_u_int64_t(xdr, &seconds))
		return FATTR_XDR_FAILED;
	if( !xdr_u_int32_t(xdr, &nseconds))
		return FATTR_XDR_FAILED;
	ts->seconds = (uint32_t)seconds;  /* !!! is this correct?? */
	ts->nseconds = nseconds;
	if(nseconds >= 1000000000) { /* overflow */
		args->nfs_status = NFS4ERR_INVAL;
		return  FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static inline fattr_xdr_result encode_timeset(XDR *xdr, gsh_time_t *ts)
{
	uint32_t how = SET_TO_CLIENT_TIME4;

	if( !xdr_u_int32_t(xdr, &how))
		return FATTR_XDR_FAILED;
	return encode_time(xdr, ts);
}

static inline fattr_xdr_result decode_timeset(XDR *xdr,
					      struct xdr_attrs_args *args,
					      gsh_time_t *ts)
{
	uint32_t how;

	if( !xdr_u_int32_t(xdr, &how))
		return FATTR_XDR_FAILED;
	if(how == SET_TO_SERVER_TIME4) {
		struct timespec sys_ts;

		if(clock_gettime(CLOCK_REALTIME, &sys_ts) != 0) {
			args->nfs_status = NFS4ERR_SERVERFAULT;
			return FATTR_XDR_FAILED;
		}
		ts->seconds = sys_ts.tv_sec;
		ts->nseconds = sys_ts.tv_nsec;
	} else {
		return decode_time(xdr, args, ts);
	}
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_TIME_ACCESS
 */

static fattr_xdr_result encode_accesstime(XDR *xdr, struct xdr_attrs_args *args)
{
	
	return encode_time(xdr, &args->attrs->atime);
}

static fattr_xdr_result decode_accesstime(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->atime);
}

/*
 * FATTR4_TIME_ACCESS_SET
 */

static fattr_xdr_result encode_accesstimeset(XDR *xdr, struct xdr_attrs_args *args)
{
	return encode_timeset(xdr, &args->attrs->atime);
}

static fattr_xdr_result decode_accesstimeset(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_timeset(xdr, args, &args->attrs->atime);
}

/*
 * FATTR4_TIME_BACKUP
 */

/* No time backup, return unix's beginning of time */

static fattr_xdr_result encode_backuptime(XDR *xdr, struct xdr_attrs_args *args)
{
	gsh_time_t ts;
	
	ts.seconds = 0LL;
	ts.nseconds = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_backuptime(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TIME_CREATE
 */

/* No time create, return unix's beginning of time */

static fattr_xdr_result encode_createtime(XDR *xdr, struct xdr_attrs_args *args)
{
	gsh_time_t ts;
	
	ts.seconds = 0LL;
	ts.nseconds = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_createtime(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_SUCCESS;
}

/*
 * FATTR4_TIME_DELTA
 */

/* According to RFC3530, this is "the smallest usefull server time granularity".
 * I set this to 1s.  note: dynamicfsinfo has this value.  use it???
 */

static fattr_xdr_result encode_deltatime(XDR *xdr, struct xdr_attrs_args *args)
{
	gsh_time_t ts;
	
	ts.seconds = 1LL;
	ts.nseconds = 0;
	return encode_time(xdr, &ts);
}

static fattr_xdr_result decode_deltatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_TIME_METADATA:
 */

static fattr_xdr_result encode_metatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return encode_time(xdr, &args->attrs->ctime);
}

static fattr_xdr_result decode_metatime(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->ctime);
}

/*
 * FATTR4_TIME_MODIFY
 */

static fattr_xdr_result encode_modifytime(XDR *xdr, struct xdr_attrs_args *args)
{
	return encode_time(xdr, &args->attrs->mtime);
}

static fattr_xdr_result decode_modifytime(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_time(xdr, args, &args->attrs->mtime);
}

/*
 * FATTR4_TIME_MODIFY_SET
 */

static fattr_xdr_result encode_modifytimeset(XDR *xdr, struct xdr_attrs_args *args)
{
	return encode_timeset(xdr, &args->attrs->mtime);
}

static fattr_xdr_result decode_modifytimeset(XDR *xdr, struct xdr_attrs_args *args)
{
	return decode_timeset(xdr, args, &args->attrs->mtime);
}

/*
 * FATTR4_MOUNTED_ON_FILEID
 */

static fattr_xdr_result encode_mounted_on_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	uint64_t file_id = args->attrs->fileid;

	if( !xdr_u_int64_t(xdr, &file_id))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_mounted_on_fileid(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DIR_NOTIF_DELAY
 */

static fattr_xdr_result encode_dir_notif_delay(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dir_notif_delay(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DIRENT_NOTIF_DELAY
 */

static fattr_xdr_result encode_dirent_notif_delay(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dirent_notif_delay(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_DACL
 */

static fattr_xdr_result encode_dacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_dacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SACL
 */

static fattr_xdr_result encode_sacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_sacl(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_CHANGE_POLICY
 */

static fattr_xdr_result encode_change_policy(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_change_policy(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_STATUS
 */

static fattr_xdr_result encode_fs_status(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_status(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_FS_LAYOUT_TYPES:
 */

static fattr_xdr_result encode_fs_layout_types(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export;
	const layouttype4 *layouttypes = NULL;
	layouttype4 layout_type;
	size_t typecount = 0;
	size_t index = 0;

	if(args->data == NULL || args->data->pexport == NULL)
		return FATTR_XDR_NOOP;
	export = args->data->pexport->export_hdl;
	export->ops->fs_layouttypes(export,
				    &typecount,
				    &layouttypes);
	if( !xdr_u_int32_t(xdr, (uint32_t *)&typecount))
		return FATTR_XDR_FAILED;
	for(index = 0; index < typecount; index++) {
		layout_type = layouttypes[index];
		if( !xdr_u_int32_t(xdr, &layout_type))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_fs_layout_types(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_HINT
 */

static fattr_xdr_result encode_layout_hint(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_hint(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_TYPES
 */

static fattr_xdr_result encode_layout_types(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_types(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_LAYOUT_BLKSIZE
 */

static fattr_xdr_result encode_layout_blocksize(XDR *xdr, struct xdr_attrs_args *args)
{
	struct fsal_export *export = args->data->pexport->export_hdl;
	uint32_t blocksize = export->ops->fs_layout_blocksize(export);

	if(args->data == NULL || args->data->pexport == NULL)
		return FATTR_XDR_NOOP;
	export = args->data->pexport->export_hdl;
	if( !xdr_u_int32_t(xdr, &blocksize))
		return FATTR_XDR_FAILED;
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_layout_blocksize(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_LAYOUT_ALIGNMENT
 */

static fattr_xdr_result encode_layout_alignment(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_layout_alignment(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 *  FATTR4_FS_LOCATIONS_INFO
 */

static fattr_xdr_result encode_fs_locations_info(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_locations_info(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MDSTHRESHOLD
 */

static fattr_xdr_result encode_mdsthreshold(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_mdsthreshold(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_GET
 */

static fattr_xdr_result encode_retention_get(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_get(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_SET
 */

static fattr_xdr_result encode_retention_set(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_set(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTEVT_GET
 */

static fattr_xdr_result encode_retentevt_get(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retentevt_get(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTEVT_SET
 */

static fattr_xdr_result encode_retentevt_set(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retentevt_set(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_RETENTION_HOLD
 */

static fattr_xdr_result encode_retention_hold(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_retention_hold(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_MODE_SET_MASKED
 */

static fattr_xdr_result encode_mode_set_masked(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_mode_set_masked(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/*
 * FATTR4_SUPPATTR_EXCLCREAT
 */

static fattr_xdr_result encode_support_exclusive_create(XDR *xdr, struct xdr_attrs_args *args)
{
	uint32_t bitmap[3];
	bitmap4 bits;
	int attr, offset;

	bits.bitmap4_len = 0;  /* start empty */
	memset(bitmap, 0, sizeof(bitmap));
	bits.bitmap4_val = bitmap;
	for(attr = FATTR4_SUPPORTED_ATTRS;
	    attr <= FATTR4_FS_CHARSET_CAP;
	    attr++) {
		if(fattr4tab[attr].supported)
			assert(set_attribute_in_bitmap(&bits, attr));
	}
	assert(clear_attribute_in_bitmap(&bits, FATTR4_TIME_ACCESS_SET));
	assert(clear_attribute_in_bitmap(&bits, FATTR4_TIME_MODIFY_SET));
	if( !xdr_u_int32_t(xdr, &bits.bitmap4_len))
		return FATTR_XDR_FAILED;
	for(offset = 0; offset < bits.bitmap4_len; offset++) {
		if( !xdr_u_int32_t(xdr, &bits.bitmap4_val[offset]))
			return FATTR_XDR_FAILED;
	}
	return FATTR_XDR_SUCCESS;
}

static fattr_xdr_result decode_support_exclusive_create(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}


/*
 * FATTR4_FS_CHARSET_CAP
 */

static fattr_xdr_result encode_fs_charset_cap(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

static fattr_xdr_result decode_fs_charset_cap(XDR *xdr, struct xdr_attrs_args *args)
{
	return FATTR_XDR_NOOP;
}

/* NFS V4.0+ attributes
 * This array reflects the tables on page 39-46 of RFC3530
 * indexed by attribute number
 */

const struct fattr4_dent fattr4tab[FATTR4_FS_CHARSET_CAP + 1] = {
	[FATTR4_SUPPORTED_ATTRS] = {
		.name = "FATTR4_SUPPORTED_ATTRS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_supported_attrs),
		.encode = encode_supported_attrs,
		.decode = decode_supported_attrs,
		.access = FATTR4_ATTR_READ},
	[FATTR4_TYPE] = {
		.name = "FATTR4_TYPE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_type),
		.attrmask = ATTR_TYPE,
		.encode = encode_type,
		.decode = decode_type,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FH_EXPIRE_TYPE] = {
		.name = "FATTR4_FH_EXPIRE_TYPE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fh_expire_type),
		.encode = encode_expiretype,
		.decode = decode_expiretype,
		.access = FATTR4_ATTR_READ},
	[FATTR4_CHANGE] = {
		.name = "FATTR4_CHANGE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_change),
		.attrmask = (ATTR_CHGTIME|ATTR_CHANGE),
		.encode = encode_change,
		.decode = decode_change,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SIZE] = {
		.name = "FATTR4_SIZE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_size),
		.attrmask = ATTR_SIZE,
		.encode = encode_filesize,
		.decode = decode_filesize,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_LINK_SUPPORT] = {
		.name = "FATTR4_LINK_SUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_link_support),
		.encode = encode_linksupport,
		.decode = decode_linksupport,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SYMLINK_SUPPORT] = {
		.name = "FATTR4_SYMLINK_SUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_symlink_support),
		.encode = encode_symlinksupport,
		.decode = decode_symlinksupport,
		.access = FATTR4_ATTR_READ},
	[FATTR4_NAMED_ATTR] = {
		.name = "FATTR4_NAMED_ATTR",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_named_attr),
		.encode = encode_namedattrsupport,
		.decode = decode_namedattrsupport,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FSID] = {
		.name = "FATTR4_FSID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fsid),
		.encode = encode_fsid,
		.decode = decode_fsid,
		.attrmask = ATTR_FSID,
		.access = FATTR4_ATTR_READ},
	[FATTR4_UNIQUE_HANDLES] = {
		.name = "FATTR4_UNIQUE_HANDLES",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_unique_handles),
		.encode = encode_uniquehandles,
		.decode = decode_uniquehandles,
		.access = FATTR4_ATTR_READ},
	[FATTR4_LEASE_TIME] = {
		.name = "FATTR4_LEASE_TIME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_lease_time),
		.encode = encode_leaselife,
		.decode = decode_leaselife,
		.access = FATTR4_ATTR_READ},
	[FATTR4_RDATTR_ERROR] = {
		.name = "FATTR4_RDATTR_ERROR",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_rdattr_error),
		.encode = encode_rdattr_error,
		.decode = decode_rdattr_error,
		.access = FATTR4_ATTR_READ},
	[FATTR4_ACL] = {
		.name = "FATTR4_ACL",
#ifdef _USE_NFS4_ACL
		.supported = 1,
#else
		.supported = 0,
#endif
		.size_fattr4 = sizeof(fattr4_acl),
		.encode = encode_acl,
		.decode = decode_acl,
		.attrmask = ATTR_ACL,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_ACLSUPPORT] = {
		.name = "FATTR4_ACLSUPPORT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_aclsupport),
		.encode = encode_aclsupport,
		.decode = decode_aclsupport,
		.access = FATTR4_ATTR_READ},
	[FATTR4_ARCHIVE] = {
		.name = "FATTR4_ARCHIVE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_archive),
		.encode = encode_archive,
		.decode = decode_archive,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_CANSETTIME] = {
		.name = "FATTR4_CANSETTIME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_cansettime),
		.encode = encode_cansettime,
		.decode = decode_cansettime,
		.access = FATTR4_ATTR_READ},
	[FATTR4_CASE_INSENSITIVE] = {
		.name = "FATTR4_CASE_INSENSITIVE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_case_insensitive),
		.encode = encode_case_insensitive,
		.decode = decode_case_insensitive,
		.access = FATTR4_ATTR_READ},
	[FATTR4_CASE_PRESERVING] = {
		.name = "FATTR4_CASE_PRESERVING",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_case_preserving),
		.encode = encode_case_preserving,
		.decode = decode_case_preserving,
		.access = FATTR4_ATTR_READ},
	[FATTR4_CHOWN_RESTRICTED] = {
		.name = "FATTR4_CHOWN_RESTRICTED",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_chown_restricted),
		.encode = encode_chown_restricted,
		.decode = decode_chown_restricted,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FILEHANDLE] = {
		.name = "FATTR4_FILEHANDLE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_filehandle),
		.encode = encode_filehandle,
		.decode = decode_filehandle,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FILEID] = {
		.name = "FATTR4_FILEID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_fileid),
		.encode = encode_fileid,
		.decode = decode_fileid,
		.attrmask = ATTR_FILEID,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FILES_AVAIL] = {
		.name = "FATTR4_FILES_AVAIL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_avail),
		.encode = encode_files_avail,
		.decode = decode_files_avail,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FILES_FREE] = {
		.name = "FATTR4_FILES_FREE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_free),
		.encode = encode_files_free,
		.decode = decode_files_free,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FILES_TOTAL] = {
		.name = "FATTR4_FILES_TOTAL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_files_total),
		.encode = encode_files_total,
		.decode = decode_files_total,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FS_LOCATIONS] = {
		.name = "FATTR4_FS_LOCATIONS",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_locations),
		.encode = encode_fs_locations,
		.decode = decode_fs_locations,
		.access = FATTR4_ATTR_READ},
	[FATTR4_HIDDEN] = {
		.name = "FATTR4_HIDDEN",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_hidden),
		.encode = encode_hidden,
		.decode = decode_hidden,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_HOMOGENEOUS] = {
		.name = "FATTR4_HOMOGENEOUS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_homogeneous),
		.encode = encode_homogeneous,
		.decode = decode_homogeneous,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MAXFILESIZE] = {
		.name = "FATTR4_MAXFILESIZE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxfilesize),
		.encode = encode_maxfilesize,
		.decode = decode_maxfilesize,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MAXLINK] = {
		.name = "FATTR4_MAXLINK",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxlink),
		.encode = encode_maxlink,
		.decode = decode_maxlink,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MAXNAME] = {
		.name = "FATTR4_MAXNAME",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxname),
		.encode = encode_maxname,
		.decode = decode_maxname,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MAXREAD] = {
		.name = "FATTR4_MAXREAD",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxread),
		.encode = encode_maxread,
		.decode = decode_maxread,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MAXWRITE] = {
		.name = "FATTR4_MAXWRITE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_maxwrite),
		.encode = encode_maxwrite,
		.decode = decode_maxwrite,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MIMETYPE] = {
		.name = "FATTR4_MIMETYPE",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mimetype),
		.encode = encode_mimetype,
		.decode = decode_mimetype,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_MODE] = {
		.name = "FATTR4_MODE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_mode),
		.encode = encode_mode,
		.decode = decode_mode,
		.attrmask = ATTR_MODE,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_NO_TRUNC] = {
		.name = "FATTR4_NO_TRUNC",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_no_trunc),
		.encode = encode_no_trunc,
		.decode = decode_no_trunc,
		.access = FATTR4_ATTR_READ},
	[FATTR4_NUMLINKS] = {
		.name = "FATTR4_NUMLINKS",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_numlinks),
		.encode = encode_numlinks,
		.decode = decode_numlinks,
		.attrmask = ATTR_NUMLINKS,
		.access = FATTR4_ATTR_READ},
	[FATTR4_OWNER] = {
		.name = "FATTR4_OWNER",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_owner),
		.encode = encode_owner,
		.decode = decode_owner,
		.attrmask = ATTR_OWNER,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_OWNER_GROUP] = {
		.name = "FATTR4_OWNER_GROUP",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_owner_group),
		.encode = encode_group,
		.decode = decode_group,
		.attrmask = ATTR_GROUP,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_QUOTA_AVAIL_HARD] = {
		.name = "FATTR4_QUOTA_AVAIL_HARD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_avail_hard),
		.encode = encode_quota_avail_hard,
		.decode = decode_quota_avail_hard,
		.access = FATTR4_ATTR_READ},
	[FATTR4_QUOTA_AVAIL_SOFT] = {
		.name = "FATTR4_QUOTA_AVAIL_SOFT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_avail_soft),
		.encode = encode_quota_avail_soft,
		.decode = decode_quota_avail_soft,
		.access = FATTR4_ATTR_READ},
	[FATTR4_QUOTA_USED] = {
		.name = "FATTR4_QUOTA_USED",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_quota_used),
		.encode = encode_quota_used,
		.decode = decode_quota_used,
		.access = FATTR4_ATTR_READ},
	[FATTR4_RAWDEV] = {
		.name = "FATTR4_RAWDEV",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_rawdev), /** @todo use FSAL attrs instead ??? */
		.encode = encode_rawdev,
		.decode = decode_rawdev,
		.attrmask = ATTR_RAWDEV,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SPACE_AVAIL] = {
		.name = "FATTR4_SPACE_AVAIL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_avail),
		.encode = encode_space_avail,
		.decode = decode_space_avail,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SPACE_FREE] = {
		.name = "FATTR4_SPACE_FREE",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_used),
		.encode = encode_space_free,
		.decode = decode_space_free,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SPACE_TOTAL] = {
		.name = "FATTR4_SPACE_TOTAL",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_total),
		.encode = encode_space_total,
		.decode = decode_space_total,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SPACE_USED] = {
		.name = "FATTR4_SPACE_USED",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_space_used),
		.encode = encode_spaceused,
		.decode = decode_spaceused,
		.attrmask = ATTR_SPACEUSED,
		.access = FATTR4_ATTR_READ},
	[FATTR4_SYSTEM] = {
		.name = "FATTR4_SYSTEM",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_system),
		.encode = encode_system,
		.decode = decode_system,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_TIME_ACCESS] = {
		.name = "FATTR4_TIME_ACCESS",
		.supported = 1,
		.size_fattr4 = 12, /* ( fattr4_time_access )  not aligned on 32 bits */
		.encode = encode_accesstime,
		.decode = decode_accesstime,
		.attrmask = ATTR_ATIME,
		.access = FATTR4_ATTR_READ},
	[FATTR4_TIME_ACCESS_SET] = {
		.name = "FATTR4_TIME_ACCESS_SET",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_time_access_set),
		.encode = encode_accesstimeset,
		.decode = decode_accesstimeset,
		.attrmask = ATTR_ATIME,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_TIME_BACKUP] = {
		.name = "FATTR4_TIME_BACKUP",
		.supported = 0,
		.size_fattr4 = 12, /*( fattr4_time_backup ) not aligned on 32 bits */
		.encode = encode_backuptime,
		.decode = decode_backuptime,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_TIME_CREATE] = {
		.name = "FATTR4_TIME_CREATE",
		.supported = 0,
		.size_fattr4 = 12, /*( fattr4_time_create ) not aligned on 32 bits */
		.encode = encode_createtime,
		.decode = decode_createtime,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_TIME_DELTA] = {
		.name = "FATTR4_TIME_DELTA",
		.supported = 1,
		12, /*  .size_fattr4 = sizeof( fattr4_time_delta ) not aligned on 32 bits */
		.encode = encode_deltatime,
		.decode = decode_deltatime,
		.access = FATTR4_ATTR_READ},
	[FATTR4_TIME_METADATA] = {
		.name = "FATTR4_TIME_METADATA",
		.supported = 1,
		.size_fattr4 = 12, /*( fattr4_time_metadata ) not aligned on 32 bits */
		.encode = encode_metatime,
		.decode = decode_metatime,
		.attrmask = ATTR_CTIME,
		.access = FATTR4_ATTR_READ},
	[FATTR4_TIME_MODIFY] = {
		.name = "FATTR4_TIME_MODIFY",
		.supported = 1,
		.size_fattr4 = 12, /*( fattr4_time_modify ) not aligned on 32 bits */
		.encode = encode_modifytime,
		.decode = decode_modifytime,
		.attrmask = ATTR_MTIME,
		.access = FATTR4_ATTR_READ},
	[FATTR4_TIME_MODIFY_SET] = {
		.name = "FATTR4_TIME_MODIFY_SET",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_time_modify_set),
		.encode = encode_modifytimeset,
		.decode = decode_modifytimeset,
		.attrmask = ATTR_MTIME,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_MOUNTED_ON_FILEID] = {
		.name = "FATTR4_MOUNTED_ON_FILEID",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_mounted_on_fileid),
		.encode = encode_mounted_on_fileid,
		.decode = decode_mounted_on_fileid,
		.access = FATTR4_ATTR_READ},
	[FATTR4_DIR_NOTIF_DELAY] = {
		.name = "FATTR4_DIR_NOTIF_DELAY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dir_notif_delay),
		.encode = encode_dir_notif_delay,
		.decode = decode_dir_notif_delay,
		.access = FATTR4_ATTR_READ},
	[FATTR4_DIRENT_NOTIF_DELAY] = {
		.name = "FATTR4_DIRENT_NOTIF_DELAY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dirent_notif_delay),
		.encode = encode_dirent_notif_delay,
		.decode = decode_dirent_notif_delay,
		.access = FATTR4_ATTR_READ},
	[FATTR4_DACL] = {
		.name = "FATTR4_DACL",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_dacl),
		.encode = encode_dacl,
		.decode = decode_dacl,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_SACL] = {
		.name = "FATTR4_SACL",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_sacl),
		.encode = encode_sacl,
		.decode = decode_sacl,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_CHANGE_POLICY] = {
		.name = "FATTR4_CHANGE_POLICY",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_change_policy),
		.encode = encode_change_policy,
		.decode = decode_change_policy,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FS_STATUS] = {
		.name = "FATTR4_FS_STATUS",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_status),
		.encode = encode_fs_status,
		.decode = decode_fs_status,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FS_LAYOUT_TYPES] = {
		.name = "FATTR4_FS_LAYOUT_TYPES",
#ifdef _PNFS_MDS
		.supported = 1,
#else
		.supported = 0,
#endif /* _PNFS_MDS */
		.size_fattr4 = sizeof(fattr4_fs_layout_types),
		.encode = encode_fs_layout_types,
		.decode = decode_fs_layout_types,
		.access = FATTR4_ATTR_READ},
	[FATTR4_LAYOUT_HINT] = {
		.name = "FATTR4_LAYOUT_HINT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_hint),
		.encode = encode_layout_hint,
		.decode = decode_layout_hint,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_LAYOUT_TYPES] = {
		.name = "FATTR4_LAYOUT_TYPES",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_types),
		.encode = encode_layout_types,
		.decode = decode_layout_types,
		.access = FATTR4_ATTR_READ},
	[FATTR4_LAYOUT_BLKSIZE] = {
		.name = "FATTR4_LAYOUT_BLKSIZE",
#ifdef _PNFS_MDS
		.supported = 1,
#else
		.supported = 0,
#endif /* _PNFS_MDS */
		.size_fattr4 = sizeof(fattr4_layout_blksize),
		.encode = encode_layout_blocksize,
		.decode = decode_layout_blocksize,
		.access = FATTR4_ATTR_READ},
	[FATTR4_LAYOUT_ALIGNMENT] = {
		.name = "FATTR4_LAYOUT_ALIGNMENT",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_layout_alignment),
		.encode = encode_layout_alignment,
		.decode = decode_layout_alignment,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FS_LOCATIONS_INFO] = {
		.name = "FATTR4_FS_LOCATIONS_INFO",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_locations_info),
		.encode = encode_fs_locations_info,
		.decode = decode_fs_locations_info,
		.access = FATTR4_ATTR_READ},
	[FATTR4_MDSTHRESHOLD] = {
		.name = "FATTR4_MDSTHRESHOLD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mdsthreshold),
		.encode = encode_mdsthreshold,
		.decode = decode_mdsthreshold,
		.access = FATTR4_ATTR_READ},
	[FATTR4_RETENTION_GET] = {
		.name = "FATTR4_RETENTION_GET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_get),
		.encode = encode_retention_get,
		.decode = decode_retention_get,
		.access = FATTR4_ATTR_READ},
	[FATTR4_RETENTION_SET] = {
		.name = "FATTR4_RETENTION_SET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_set),
		.encode = encode_retention_set,
		.decode = decode_retention_set,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_RETENTEVT_GET] = {
		.name = "FATTR4_RETENTEVT_GET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retentevt_get),
		.encode = encode_retentevt_get,
		.decode = decode_retentevt_get,
		.access = FATTR4_ATTR_READ},
	[FATTR4_RETENTEVT_SET] = {
		.name = "FATTR4_RETENTEVT_SET",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retentevt_set),
		.encode = encode_retentevt_set,
		.decode = decode_retentevt_set,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_RETENTION_HOLD] = {
		.name = "FATTR4_RETENTION_HOLD",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_retention_hold),
		.encode = encode_retention_hold,
		.decode = decode_retention_hold,
		.access = FATTR4_ATTR_READ_WRITE},
	[FATTR4_MODE_SET_MASKED] = {
		.name = "FATTR4_MODE_SET_MASKED",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_mode_set_masked),
		.encode = encode_mode_set_masked,
		.decode = decode_mode_set_masked,
		.access = FATTR4_ATTR_WRITE},
	[FATTR4_SUPPATTR_EXCLCREAT] = {
		.name = "FATTR4_SUPPATTR_EXCLCREAT",
		.supported = 1,
		.size_fattr4 = sizeof(fattr4_suppattr_exclcreat),
		.encode = encode_support_exclusive_create,
		.decode = decode_support_exclusive_create,
		.access = FATTR4_ATTR_READ},
	[FATTR4_FS_CHARSET_CAP] = {
		.name = "FATTR4_FS_CHARSET_CAP",
		.supported = 0,
		.size_fattr4 = sizeof(fattr4_fs_charset_cap),
		.encode = encode_fs_charset_cap,
		.decode = decode_fs_charset_cap,
		.access = FATTR4_ATTR_READ}
};


/* goes in a more global header?
 */

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

/** path_filter
 * scan the path we are given for bad filenames.
 *
 * scan control:
 *    UTF8_SCAN_NOSLASH - detect and reject '/' in names
 *    UTF8_NODOT - detect and reject "." and ".." as the name
 *    UTF8_SCAN_CKUTF8 - detect invalid utf8 sequences
 *
 * NULL termination is required.  It also speeds up the scan
 * UTF-8 scanner courtesy Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/>
 * GPL licensed per licensing referenced in source.
 *
 */

static nfsstat4 path_filter(const char *name,
			    utf8_scantype_t scan)
{
	const unsigned char *np = (const unsigned char *)name;
	nfsstat4 status = NFS4_OK;
	unsigned int c, first;

	first = 1;
	c = *np++;
	while (c) {
		if (likely(c < 0x80)) { /* ascii */
			if (unlikely(c == '/' && (scan & UTF8_SCAN_NOSLASH))) {
				status = NFS4ERR_BADCHAR;
				goto error;
			}
			if (unlikely(first && c == '.' && (scan & UTF8_SCAN_NODOT))) {
				if (np[0] == '\0' || (np[0] == '.' && np[1] == '\0')) {
					status = NFS4ERR_BADNAME;
					goto error;
				}
			}
		} else if(likely(scan & UTF8_SCAN_CKUTF8)) { /* UTF-8 range */
			if ((c & 0xe0) == 0xc0) { /* 2 octet UTF-8 */
				if ((*np & 0xc0) != 0x80 ||
				    (c & 0xfe) == 0xc0) { /* overlong */
					goto badutf8;
				} else {
					np++;
				}
			} else if ((c & 0xf0) == 0xe0) { /* 3 octet UTF-8 */
				if ((*np & 0xc0) != 0x80 ||
				    (np[1] & 0xc0) != 0x80 ||
				    (c == 0xe0 && (*np & 0xe0) == 0x80) || /* overlong */
				    (c == 0xed && (*np & 0xe0) == 0xa0) || /* surrogate */
				    (c == 0xef && *np == 0xbf &&
				     (np[1] & 0xfe) == 0xbe)) { /* U+fffe - u+ffff*/
					goto badutf8;
				} else {
					np += 2;
				}
			} else if ((c & 0xf8) == 0xf0) { /* 4 octet UTF-8 */
				if ((*np & 0xc0) != 0x80 ||
				    (np[1] & 0xc0) != 0x80 ||
				    (np[2] & 0xc0) != 0x80 ||
				    (c == 0xf0 && (*np & 0xf0) == 0x80) || /* overlong */
				    (c == 0xf4 && *np > 0x8f) ||
				    c > 0xf4) { /* > u+10ffff */
					goto badutf8;
				} else {
					np += 3;
				}
			} else {
				goto badutf8;
			}
		}
		c = *np++;
		first = 0;
	}
	return NFS4_OK;

badutf8:
	status = NFS4ERR_INVAL;
error:
	return status;
}

#ifdef _USE_NFS4_ACL
/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_special_user(int who, char *attrvalsBuffer,
                                        u_int *LastOffset)
{
  int rc = 0;
  int i;
  u_int utf8len = 0;
  u_int deltalen = 0;

  for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++)
    {
      if (whostr_2_type_map[i].type == who)
        {
          if(whostr_2_type_map[i].stringlen % 4 == 0)
            deltalen = 0;
          else
            deltalen = 4 - whostr_2_type_map[i].stringlen % 4;

          utf8len = htonl(whostr_2_type_map[i].stringlen + deltalen);
          memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
          *LastOffset += sizeof(int);

          memcpy((char *)(attrvalsBuffer + *LastOffset), whostr_2_type_map[i].string,
                 whostr_2_type_map[i].stringlen);
          *LastOffset += whostr_2_type_map[i].stringlen;

          /* Pad with zero to keep xdr alignement */
          if(deltalen != 0)
            memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
          *LastOffset += deltalen;

          /* Found a matched one. */
          rc = 1;
          break;
        }
    }

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_group_name(fsal_gid_t gid, char *attrvalsBuffer,
                                      u_int *LastOffset)
{
  int rc = 0;
  char name[MAXNAMLEN];
  u_int utf8len = 0;
  u_int stringlen = 0;
  u_int deltalen = 0;

  rc = gid2name(name, &gid);
  LogFullDebug(COMPONENT_NFS_V4,
               "encode gid2name = %s, strlen = %llu",
               name, (long long unsigned int)strlen(name));
  if(rc == 0)  /* Failure. */
    {
      /* Encode gid itself without @. */
      sprintf(name, "%u", gid);
    }

  stringlen = strlen(name);
  if(stringlen % 4 == 0)
    deltalen = 0;
  else
    deltalen = 4 - (stringlen % 4);

  utf8len = htonl(stringlen + deltalen);
  memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
  *LastOffset += sizeof(int);

  memcpy((char *)(attrvalsBuffer + *LastOffset), name, stringlen);
  *LastOffset += stringlen;

  /* Pad with zero to keep xdr alignement */
  if(deltalen != 0)
    memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
  *LastOffset += deltalen;

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_user_name(int whotype, fsal_uid_t uid,
                                     char *attrvalsBuffer, u_int *LastOffset)
{
  int rc = 0;
  char name[MAXNAMLEN];
  u_int utf8len = 0;
  u_int stringlen = 0;
  u_int deltalen = 0;

  /* Encode special user first. */
  if (whotype != FSAL_ACE_NORMAL_WHO)
    {
      rc = nfs4_encode_acl_special_user(uid, attrvalsBuffer, LastOffset);
      if(rc == 1)  /* Success. */
        return rc;
    }

  /* Encode normal user or previous user we failed to encode as special user. */
  rc = uid2name(name, &uid);
  LogFullDebug(COMPONENT_NFS_V4,
               "econde uid2name = %s, strlen = %llu",
               name, (long long unsigned int)strlen(name));
  if(rc == 0)  /* Failure. */
    {
      /* Encode uid itself without @. */
      sprintf(name, "%u", uid);
    }

  stringlen = strlen(name);
  if(stringlen % 4 == 0)
    deltalen = 0;
  else
    deltalen = 4 - (stringlen % 4);

  utf8len = htonl(stringlen + deltalen);
  memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
  *LastOffset += sizeof(int);

  memcpy((char *)(attrvalsBuffer + *LastOffset), name, stringlen);
  *LastOffset += stringlen;

  /* Pad with zero to keep xdr alignement */
  if(deltalen != 0)
    memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
  *LastOffset += deltalen;

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl(fsal_attrib_list_t * pattr, char *attrvalsBuffer, u_int *LastOffset)
{
  int rc = 0;
  uint32_t naces, type, flag, access_mask, whotype;
  fsal_ace_t *pace;

  if(pattr->acl)
    {
      LogFullDebug(COMPONENT_NFS_V4,
                   "GATTR: Number of ACEs = %u",
                   pattr->acl->naces);

      /* Encode number of ACEs. */
      naces = htonl(pattr->acl->naces);
      memcpy((char *)(attrvalsBuffer + *LastOffset), &naces, sizeof(uint32_t));
      *LastOffset += sizeof(uint32_t);

      /* Encode ACEs. */
      for(pace = pattr->acl->aces; pace < pattr->acl->aces + pattr->acl->naces; pace++)
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "GATTR: type=0X%x, flag=0X%x, perm=0X%x",
                       pace->type, pace->flag, pace->perm);

          type = htonl(pace->type);
          flag = htonl(pace->flag);
          access_mask = htonl(pace->perm);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &type, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &flag, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &access_mask, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          if(IS_FSAL_ACE_GROUP_ID(*pace))  /* Encode group name. */
            {
              rc = nfs4_encode_acl_group_name(pace->who.gid, attrvalsBuffer, LastOffset);
            }
          else
            {
              if(!IS_FSAL_ACE_SPECIAL_ID(*pace))
                {
                  whotype = FSAL_ACE_NORMAL_WHO;
                }
              else
                whotype = pace->who.uid;

              /* Encode special or normal user name. */
              rc = nfs4_encode_acl_user_name(whotype, pace->who.uid, attrvalsBuffer, LastOffset);
            }

          LogFullDebug(COMPONENT_NFS_V4,
                       "GATTR: special = %u, %s = %u",
                       IS_FSAL_ACE_SPECIAL_ID(*pace),
                       IS_FSAL_ACE_GROUP_ID(*pace) ? "gid" : "uid",
                       IS_FSAL_ACE_GROUP_ID(*pace) ? pace->who.gid : pace->who.uid);

        }
    }
  else
    {
      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_encode_acl: no acl available");

      fattr4_acl acl;
      acl.fattr4_acl_len = htonl(0);
      memcpy((char *)(attrvalsBuffer + *LastOffset), &acl, sizeof(fattr4_acl));
      *LastOffset += fattr4tab[FATTR4_ACL].size_fattr4;
    }

  return rc;
}
#endif                          /* _USE_NFS4_ACL */

void nfs4_Fattr_Free(fattr4 *fattr)
{
  if(fattr->attrmask.bitmap4_val != NULL)
    {
      gsh_free(fattr->attrmask.bitmap4_val);
      fattr->attrmask.bitmap4_val = NULL;
    }

  if(fattr->attr_vals.attrlist4_val != NULL)
    {
      gsh_free(fattr->attr_vals.attrlist4_val);
      fattr->attr_vals.attrlist4_val = NULL;
    }
}

/**
 *
 * nfs4_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * @param pexport [IN]  the related export entry.
 * @param pattr   [IN]  pointer to FSAL attributes.
 * @param Fattr   [OUT] NFSv4 Fattr buffer
 *		  Memory for bitmap_val and attr_val is dynamically allocated,
 *		  caller is responsible for freeing it.
 * @param data    [IN]  NFSv4 compoud request's data.
 * @param objFH   [IN]  The NFSv4 filehandle of the object whose
 *                      attributes are requested
 * @param Bitmap  [IN]  Bitmap of attributes being requested
 *
 * @return -1 if failed, 0 if successful.
 *
 */

int nfs4_FSALattr_To_Fattr(const struct attrlist *attrs,
                           fattr4 *Fattr,
                           compound_data_t *data,
                           nfs_fh4 *objFH,
                           bitmap4 *Bitmap)
{
	int attribute_to_set = 0;
	u_int LastOffset;
	fsal_dynamicfsinfo_t dynamicinfo;
	XDR attr_body;
	struct xdr_attrs_args args;
	fattr_xdr_result xdr_res;

	/* basic init */
	Fattr->attrmask.bitmap4_val = gsh_calloc(3, sizeof(uint32_t));
	if(Fattr->attrmask.bitmap4_val == NULL)
		return -1;
	Fattr->attrmask.bitmap4_len = 0; /* bitmap starts off empty */
	if(Bitmap->bitmap4_len == 0) {
		return 0;  /* they ask for nothing, they get nothing */
	}
	Fattr->attr_vals.attrlist4_val = gsh_malloc(NFS4_ATTRVALS_BUFFLEN);
	if(Fattr->attr_vals.attrlist4_val == NULL) {
		gsh_free(Fattr->attrmask.bitmap4_val);
		Fattr->attrmask.bitmap4_val = NULL;
		return -1;
	}

	LastOffset = 0;
	xdrmem_create(&attr_body,
		      Fattr->attr_vals.attrlist4_val,
		      NFS4_ATTRVALS_BUFFLEN,
		      XDR_ENCODE);
	memset(&args, 0, sizeof(args));
	args.attrs = (struct attrlist *)attrs; /* overriding const */
	args.hdl4 = objFH;
	args.data = data;
	args.rdattr_error = NFS4_OK;
	args.dynamicinfo = &dynamicinfo;

	for(attribute_to_set = next_attr_from_bitmap(Bitmap, -1);
	    attribute_to_set != -1;
	    attribute_to_set = next_attr_from_bitmap(Bitmap, attribute_to_set)) {
		if(attribute_to_set > FATTR4_FS_CHARSET_CAP) {
			break;  /* skip out of bounds */
		}
		xdr_res = fattr4tab[attribute_to_set].encode(&attr_body, &args);
		if(xdr_res == FATTR_XDR_SUCCESS) {
			assert(set_attribute_in_bitmap(&Fattr->attrmask,
						       attribute_to_set));
			LogFullDebug(COMPONENT_NFS_V4,
				     "Encoded attribute %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
		} else if(xdr_res == FATTR_XDR_NOOP) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Attribute not supported %d name=%s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
			continue;
		} else {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Encode FAILED for attribute %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
			goto err;  /* signal fail so if(LastOffset > 0) works right */
		}
		/* mark the attribute in the bitmap should be new bitmap btw */
	}
	LastOffset = xdr_getpos(&attr_body);  /* dumb but for now */
	xdr_destroy(&attr_body);

	if(LastOffset == 0) {  /* no supported attrs so we can free */
		assert(Fattr->attrmask.bitmap4_len == 0);
		gsh_free(Fattr->attrmask.bitmap4_val);
		Fattr->attr_vals.attrlist4_val = NULL;
	}
	Fattr->attr_vals.attrlist4_len = LastOffset;
	return 0;

err:
	gsh_free(Fattr->attrmask.bitmap4_val);
	Fattr->attr_vals.attrlist4_val = NULL;
	return -1;
}

/**
 *
 * nfs3_Sattr_To_FSALattr: Converts NFSv3 Sattr to FSAL Attributes.
 *
 * Converts NFSv3 Sattr to FSAL Attributes.
 *
 * @param pFSAL_attr  [OUT]  computed FSAL attributes.
 * @param psattr      [IN]   NFSv3 sattr to be set.
 *
 * @return 0 if failed, 1 if successful.
 *
 */
int nfs3_Sattr_To_FSALattr(struct attrlist *pFSAL_attr,
                           sattr3 *psattr)
{
  struct timeval t;

  if(pFSAL_attr == NULL || psattr == NULL)
    return 0;

  pFSAL_attr->mask = 0;

  if(psattr->mode.set_it)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: mode = %o",
                   psattr->mode.set_mode3_u.mode);
      pFSAL_attr->mode = unix2fsal_mode(psattr->mode.set_mode3_u.mode);
      pFSAL_attr->mask |= ATTR_MODE;
    }

  if(psattr->uid.set_it)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: uid = %d",
                   psattr->uid.set_uid3_u.uid);
      pFSAL_attr->owner = psattr->uid.set_uid3_u.uid;
      pFSAL_attr->mask |= ATTR_OWNER;
    }

  if(psattr->gid.set_it)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: gid = %d",
                   psattr->gid.set_gid3_u.gid);
      pFSAL_attr->group = psattr->gid.set_gid3_u.gid;
      pFSAL_attr->mask |= ATTR_GROUP;
    }

  if(psattr->size.set_it)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: size = %lld",
                   psattr->size.set_size3_u.size);
      pFSAL_attr->filesize = psattr->size.set_size3_u.size;
      pFSAL_attr->spaceused = psattr->size.set_size3_u.size;
      /* Both ATTR_SIZE and ATTR_SPACEUSED are to be managed */
      pFSAL_attr->mask |= ATTR_SIZE;
      pFSAL_attr->mask |= ATTR_SPACEUSED;
    }

  if(psattr->atime.set_it != DONT_CHANGE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: set=%d atime = %d,%d",
                   psattr->atime.set_it, psattr->atime.set_atime_u.atime.seconds,
                   psattr->atime.set_atime_u.atime.nseconds);
      if(psattr->atime.set_it == SET_TO_CLIENT_TIME)
        {
          pFSAL_attr->atime.seconds = psattr->atime.set_atime_u.atime.seconds;
          pFSAL_attr->atime.nseconds = 0;
        }
      else
        {
          /* Use the server's current time */
          gettimeofday(&t, NULL);

          pFSAL_attr->atime.seconds = t.tv_sec;
          pFSAL_attr->atime.nseconds = 0;
        }
      pFSAL_attr->mask |= ATTR_ATIME;
    }

  if(psattr->mtime.set_it != DONT_CHANGE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: set=%d mtime = %d",
                   psattr->atime.set_it, psattr->mtime.set_mtime_u.mtime.seconds ) ;
      if(psattr->mtime.set_it == SET_TO_CLIENT_TIME)
        {
          pFSAL_attr->mtime.seconds = psattr->mtime.set_mtime_u.mtime.seconds;
          pFSAL_attr->mtime.nseconds = 0 ;
        }
      else
        {
          /* Use the server's current time */
          gettimeofday(&t, NULL);
          pFSAL_attr->mtime.seconds = t.tv_sec;
          pFSAL_attr->mtime.nseconds = 0 ;
        }
      pFSAL_attr->mask |= ATTR_MTIME;
    }

  return 1;
}                               /* nfs3_Sattr_To_FSALattr */

/**
 * nfs2_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv2 attributes.
 *
 * Converts FSAL Attributes to NFSv2 attributes.
 *
 * @param pexport   [IN]  the related export entry.
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv2 attributes. 
 * 
 * @return 1 if successful, 0 otherwise.
 *
 */
int nfs2_FSALattr_To_Fattr(exportlist_t *pexport,
                           const struct attrlist *pFSAL_attr,
                           fattr2 *pFattr)
{
  /* Badly formed arguments */
  if(pFSAL_attr == NULL || pFattr == NULL)
    return 0;

  /* @todo BUGAZOMEU: sanity check on attribute mask (does the FSAL support the attributes required to support NFSv2 ? */

  /* initialize mode */
  pFattr->mode = 0;

  switch (pFSAL_attr->type)
    {
    case REGULAR_FILE:
      pFattr->type = NFREG;
      pFattr->mode = NFS2_MODE_NFREG;
      break;

    case DIRECTORY:
      pFattr->type = NFDIR;
      pFattr->mode = NFS2_MODE_NFDIR;
      break;

    case BLOCK_FILE:
      pFattr->type = NFBLK;
      pFattr->mode = NFS2_MODE_NFBLK;
      break;

    case CHARACTER_FILE:
      pFattr->type = NFCHR;
      pFattr->mode = NFS2_MODE_NFCHR;
      break;

    case FIFO_FILE:
      pFattr->type = NFFIFO;
      /** @todo mode mask ? */
      break;

    case SYMBOLIC_LINK:
      pFattr->type = NFLNK;
      pFattr->mode = NFS2_MODE_NFLNK;
      break;

    case SOCKET_FILE:
      pFattr->type = NFSOCK;
      /** @todo mode mask ? */
      break;

    case NO_FILE_TYPE:
    case EXTENDED_ATTR:
    case FS_JUNCTION:
      pFattr->type = NFBAD;
    }

  pFattr->mode |= fsal2unix_mode(pFSAL_attr->mode);
  pFattr->nlink = pFSAL_attr->numlinks;
  pFattr->uid = pFSAL_attr->owner;
  pFattr->gid = pFSAL_attr->group;

  /* in NFSv2, it only keeps fsid.major, casted into an into an int32 */
  pFattr->fsid = (u_int) (pexport->filesystem_id.major & 0xFFFFFFFFLL);

  LogFullDebug(COMPONENT_NFSPROTO,
               "nfs2_FSALattr_To_Fattr: fsid.major = %#"PRIX64" (%"PRIu64"), "
               "fsid.minor = %#"PRIX64" (%"PRIu64"), "
               "nfs2_fsid = %#X (%u)",
               pexport->filesystem_id.major, pexport->filesystem_id.major,
               pexport->filesystem_id.minor, pexport->filesystem_id.minor, pFattr->fsid,
               pFattr->fsid);

  if(pFSAL_attr->filesize > NFS2_MAX_FILESIZE)
    pFattr->size = NFS2_MAX_FILESIZE;
  else
    pFattr->size = pFSAL_attr->filesize;

  pFattr->blocksize = DEV_BSIZE;

  pFattr->blocks = pFattr->size >> 9;   /* dividing by 512 */
  if(pFattr->size % DEV_BSIZE != 0)
    pFattr->blocks += 1;

  if(pFSAL_attr->type == CHARACTER_FILE || pFSAL_attr->type == BLOCK_FILE)
    pFattr->rdev = pFSAL_attr->rawdev.major;
  else
    pFattr->rdev = 0;

  pFattr->atime.seconds = pFSAL_attr->atime.seconds;
  pFattr->atime.useconds = pFSAL_attr->atime.nseconds / 1000;
  pFattr->mtime.seconds = pFSAL_attr->mtime.seconds;
  pFattr->mtime.useconds = pFSAL_attr->mtime.nseconds / 1000;
  pFattr->ctime.seconds = pFSAL_attr->ctime.seconds;
  pFattr->ctime.useconds = pFSAL_attr->ctime.nseconds / 1000;
  pFattr->fileid = pFSAL_attr->fileid;

  return 1;
}                               /*  nfs2_FSALattr_To_Fattr */

/**
 * 
 * nfs4_SetCompoundExport
 * 
 * This routine fills in the pexport field in the compound data.
 *
 * @param pfh [OUT] pointer to compound data to be used. 
 * 
 * @return NFS4_OK if successfull. Possible errors are NFS4ERR_BADHANDLE and NFS4ERR_WRONGSEC.
 *
 */

int nfs4_SetCompoundExport(compound_data_t * data)
{
  short exportid;

  /* This routine is not related to pseudo fs file handle, do not handle them */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return NFS4_OK;

  /* Get the export id */
  if((exportid = nfs4_FhandleToExportId(&(data->currentFH))) == 0)
    return NFS4ERR_BADHANDLE;

  if((data->pexport = nfs_Get_export_by_id(data->pfullexportlist, exportid)) == NULL)
    return NFS4ERR_BADHANDLE;

  if((data->pexport->options & EXPORT_OPTION_NFSV4) == 0)
    return NFS4ERR_ACCESS;

  if(nfs4_MakeCred(data) != NFS4_OK)
    return NFS4ERR_WRONGSEC;

  return NFS4_OK;
}                               /* nfs4_SetCompoundExport */

/**
 * 
 * nfs4_FhandleToExId
 * 
 * This routine extracts the export id from the filehandle
 *
 * @param fh4p  [IN]  pointer to file handle to be used.
 * @param ExIdp [OUT] pointer to buffer in which found export id will be stored. 
 *
 * @return true if successful, false otherwise.
 *
 */
int nfs4_FhandleToExId(nfs_fh4 * fh4p, unsigned short *ExIdp)
{
  file_handle_v4_t *pfhandle4;

  /* Map the filehandle to the correct structure */
  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  /* The function should not be used on a pseudo fhandle */
  if(pfhandle4->pseudofs_flag)
    return false;

  *ExIdp = pfhandle4->exportid;
  return true;
}                               /* nfs4_FhandleToExId */

/**** Glue related functions ****/

/**
 *
 * nfs4_stringid_split: Splits a domain stamped name in two different parts.
 *
 * Splits a domain stamped name in two different parts.
 *
 * @param buff [IN] the input string
 * @param uidname [OUT] the extracted uid name
 * @param domainname [OUT] the extracted fomain name
 *
 * @return nothing (void function) 
 *
 */
void nfs4_stringid_split(char *buff, char *uidname, char *domainname)
{
  char *c = NULL;
  unsigned int i = 0;

  for(c = buff, i = 0; *c != '\0'; c++, i++)
    if(*c == '@')
      break;

  strncpy(uidname, buff, i);
  uidname[i] = '\0';
  strcpy(domainname, c);

  LogFullDebug(COMPONENT_NFS_V4,
               "buff = #%s#    uid = #%s#   domain = #%s#",
               buff, uidname, domainname);
}                               /* nfs4_stringid_split */

/**
 *
 * free_utf8: Free's a utf8str that was created by utf8dup
 *
 * @param utf8str [IN]  UTF8 string to be freed
 *
 */
void free_utf8(utf8string * utf8str)
{
  if(utf8str != NULL)
    {
      if(utf8str->utf8string_val != NULL)
        gsh_free(utf8str->utf8string_val);
      utf8str->utf8string_val = 0;
      utf8str->utf8string_len = 0;
    }
}

/**
 *
 * utf8dup: Makes a copy of a utf8str.
 *
 * @param newstr  [OUT] copied UTF8 string
 * @param oldstr  [IN]  input UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
nfsstat4 utf8dup(utf8string * newstr,
	    utf8string * oldstr,
	    utf8_scantype_t scan)
{
  nfsstat4 status = NFS4_OK;

  newstr->utf8string_len = oldstr->utf8string_len;
  newstr->utf8string_val = NULL;

  if(oldstr->utf8string_len == 0 || oldstr->utf8string_val == NULL)
    return status;

  newstr->utf8string_val = gsh_malloc(oldstr->utf8string_len + 1);
  if(newstr->utf8string_val == NULL)
    return NFS4ERR_SERVERFAULT;

  strncpy(newstr->utf8string_val, oldstr->utf8string_val, oldstr->utf8string_len);
  newstr->utf8string_val[oldstr->utf8string_len] = '\0';  /* NULL term just in case */
  if(scan != UTF8_SCAN_NONE)
	  status = path_filter(newstr->utf8string_val, scan);

  return status;
}

/**
 *
 * utf82str: converts a UTF8 string buffer into a string descriptor.
 *
 * Converts a UTF8 string buffer into a string descriptor.
 *
 * @param str     [OUT] computed output string
 * @param utf8str [IN]  input UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
int utf82str(char *str, int size, utf8string * utf8str)
{
  int copy;

  if(str == NULL)
    return -1;

  if(utf8str == NULL || utf8str->utf8string_len == 0)
    {
      str[0] = '\0';
      return -1;
    }

  if(utf8str->utf8string_len >= size)
    copy = size - 1;
  else
    copy = utf8str->utf8string_len;

  strncpy(str, utf8str->utf8string_val, copy);
  str[copy] = '\0';

  if(copy < utf8str->utf8string_len)
    return -1;

  return 0;
}                               /* uft82str */

/**
 *
 * str2utf8: converts a string buffer into a UTF8 string descriptor.
 *
 * Converts a string buffer into a UTF8 string descriptor.
 *
 * @param str     [IN]  input string
 * @param utf8str [OUT] computed UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
int str2utf8(char *str, utf8string * utf8str)
{
  uint_t len;
  char buff[MAXNAMLEN];

  /* The uft8 will probably be sent over XDR, for this reason, its size MUST be a multiple of 32 bits = 4 bytes */
  strcpy(buff, str);
  len = strlen(buff);

  /* BUGAZOMEU: TO BE DONE: use STUFF ALLOCATOR here */
  if(utf8str->utf8string_val == NULL)
    return -1;

  utf8str->utf8string_len = len;
  memcpy(utf8str->utf8string_val, buff, utf8str->utf8string_len);
  return 0;
}                               /* str2utf8 */

/**
 * 
 * nfs4_NextSeqId: compute the next nfsv4 sequence id.
 *
 * Compute the next nfsv4 sequence id.
 *
 * @param seqid [IN] previous sequence number.
 * 
 * @return the requested sequence number.
 *
 */

seqid4 nfs4_NextSeqId(seqid4 seqid)
{
  return ((seqid + 1) % 0xFFFFFFFF);
}                               /* nfs4_NextSeqId */

/**
 *
 * nfs_bitmap4_to_list: convert an attribute's bitmap to a list of attributes.
 *
 * Convert an attribute's bitmap to a list of attributes.
 *
 * @param b     [IN] bitmap to convert.
 * @param plen  [OUT] list's length.
 * @param plval [OUT] list's values.
 *
 * @return nothing (void function)
 *
 */
/** @TODO deprecate both of these.  some use remains in PROXY fasl
 */

void nfs4_bitmap4_to_list(const bitmap4 * b, uint_t * plen, uint32_t * pval)
{
  uint_t i = 0;
  uint_t val = 0;
  uint_t index = 0;
  uint_t offset = 0;
  uint_t fattr4tabidx=0;
  if(b->bitmap4_len > 0)
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u Val = %u|%u",
                 b->bitmap4_len, b->bitmap4_val[0], b->bitmap4_val[1]);
  else
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u ... ", b->bitmap4_len);

  for(offset = 0; offset < b->bitmap4_len; offset++)
    {
      for(i = 0; i < 32; i++)
        {
          fattr4tabidx = i+32*offset;
          if (fattr4tabidx > FATTR4_FS_CHARSET_CAP)
             goto exit;

          val = 1 << i;         /* Compute 2**i */
          if(b->bitmap4_val[offset] & val)
            pval[index++] = fattr4tabidx;
        }
    }
exit:
  *plen = index;

}                               /* nfs4_bitmap4_to_list */

/**
 *
 * nfs4_list_to_bitmap4: convert a list of attributes to an attributes's bitmap.
 *
 * Convert a list of attributes to an attributes's bitmap.
 *
 * @param b [OUT] computed bitmap
 * @param plen [IN] list's length 
 * @param pval [IN] list's array
 *
 * @return nothing (void function).
 *
 */

/* This function converts a list of attributes to a bitmap4 structure */
void nfs4_list_to_bitmap4(bitmap4 * b, uint_t plen, uint32_t * pval)
{
  uint_t i;
  int maxpos =  -1;

  memset(b->bitmap4_val, 0, sizeof(uint32_t)*b->bitmap4_len);

  for(i = 0; i < plen; i++)
    {
      int intpos = pval[i] / 32;
      int bitpos = pval[i] % 32;

      if(intpos >= b->bitmap4_len)
        {
          LogCrit(COMPONENT_NFS_V4,
                  "Mismatch between bitmap len and the list: "
                  "got %d, need %d to accomodate attribute %d",
                  b->bitmap4_len, intpos+1, pval[i]);
        assert(intpos < b->bitmap4_len);
          continue;
        }
      b->bitmap4_val[intpos] |= (1U << bitpos);
      if(intpos > maxpos)
        maxpos = intpos;
    }

  b->bitmap4_len = maxpos + 1;
  LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u   Val = %u|%u|%u",
               b->bitmap4_len,
               b->bitmap4_len >= 1 ? b->bitmap4_val[0] : 0,
               b->bitmap4_len >= 2 ? b->bitmap4_val[1] : 0,
               b->bitmap4_len >= 3 ? b->bitmap4_val[2] : 0);
}                               /* nfs4_list_to_bitmap4 */

/*
 * Conversion of attributes
 */

/**
 *
 * nfs3_FSALattr_To_PartialFattr: Converts FSAL Attributes to NFSv3 attributes.
 *
 * Fill in the fields in the fattr3 structure which have matching
 * attribute bits set. Caller must explictly specify which bits it expects
 * to avoid misunderstandings.
 *
 * @param[in] FSAL_attr FSAL attributes.
 * @param[in,out] want  Attributes which should be/have been copied
 * @param[out] Fattr    NFSv3 attributes.
 *
 */
void
nfs3_FSALattr_To_PartialFattr(const struct attrlist *FSAL_attr,
                              attrmask_t *mask,
                              fattr3 *Fattr)
{
        *mask = 0;

        if (FSAL_attr->mask & ATTR_TYPE) {
                *mask |= ATTR_TYPE;
                switch (FSAL_attr->type) {
                case FIFO_FILE:
                        Fattr->type = NF3FIFO;
                        break;

                case CHARACTER_FILE:
                        Fattr->type = NF3CHR;
                        break;

                case DIRECTORY:
                        Fattr->type = NF3DIR;
                        break;

                case BLOCK_FILE:
                        Fattr->type = NF3BLK;
                        break;

                case REGULAR_FILE:
                case EXTENDED_ATTR:
                        Fattr->type = NF3REG;
                        break;

                case SYMBOLIC_LINK:
                        Fattr->type = NF3LNK;
                        break;

                case SOCKET_FILE:
                        Fattr->type = NF3SOCK;
                        break;

                default:
                        LogEvent(COMPONENT_NFSPROTO,
                                 "nfs3_FSALattr_To_Fattr: Bogus type = %d",
                                 FSAL_attr->type);
                        *mask &= ~ATTR_TYPE;
                }
        }

        if (FSAL_attr->mask & ATTR_MODE) {
                Fattr->mode = fsal2unix_mode(FSAL_attr->mode);
                *mask |= ATTR_MODE;
        }

        if (FSAL_attr->mask & ATTR_NUMLINKS) {
                Fattr->nlink = FSAL_attr->numlinks;
                *mask |= ATTR_NUMLINKS;
        }

        if (FSAL_attr->mask & ATTR_OWNER) {
                Fattr->uid = FSAL_attr->owner;
                *mask |= ATTR_OWNER;
        }

        if (FSAL_attr->mask & ATTR_GROUP) {
                Fattr->gid = FSAL_attr->group;
                *mask |= ATTR_GROUP;
        }

        if (FSAL_attr->mask & ATTR_SIZE) {
                Fattr->size = FSAL_attr->filesize;
                *mask |= ATTR_SIZE;
        }

        if (FSAL_attr->mask & ATTR_SPACEUSED) {
                Fattr->used = FSAL_attr->spaceused;
                *mask |= ATTR_SPACEUSED;
        }

        if (FSAL_attr->mask & ATTR_RAWDEV) {
                Fattr->rdev.specdata1 = FSAL_attr->rawdev.major;
                Fattr->rdev.specdata2 = FSAL_attr->rawdev.minor;
                *mask |= ATTR_RAWDEV;
        }

        if (FSAL_attr->mask & ATTR_FILEID) {
                Fattr->fileid = FSAL_attr->fileid;
                *mask |= ATTR_FILEID;
        }

        if (FSAL_attr->mask & ATTR_ATIME) {
                Fattr->atime.seconds = FSAL_attr->atime.seconds;
                Fattr->atime.nseconds = FSAL_attr->atime.nseconds;
                *mask |= ATTR_ATIME;
        }

        if (FSAL_attr->mask & ATTR_MTIME) {
                Fattr->mtime.seconds = FSAL_attr->mtime.seconds;
                Fattr->mtime.nseconds = FSAL_attr->mtime.nseconds;
                *mask |= ATTR_MTIME;
        }

        if (FSAL_attr->mask & ATTR_CTIME) {
                Fattr->ctime.seconds = FSAL_attr->ctime.seconds;
                Fattr->ctime.nseconds = FSAL_attr->ctime.nseconds;
                *mask |= ATTR_CTIME;
        }
}                         /* nfs3_FSALattr_To_PartialFattr */

/**
 *
 * nfs3_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv3 attributes.
 *
 * Converts FSAL Attributes to NFSv3 attributes.
 * The callee is expecting the full compliment of FSAL attributes to fill
 * in all the fields in the fattr3 structure.
 *
 * @param pexport   [IN]  the related export entry
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv3 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */
int nfs3_FSALattr_To_Fattr(exportlist_t *pexport,
                           const struct attrlist *FSAL_attr,
                           fattr3 *Fattr)
{
        attrmask_t want, got;

        want = got = (ATTR_TYPE      | ATTR_MODE   |
                      ATTR_NUMLINKS  | ATTR_OWNER  |
                      ATTR_GROUP     | ATTR_SIZE   |
                      ATTR_SPACEUSED | ATTR_RAWDEV |
                      ATTR_ATIME     | ATTR_MTIME  |
                      ATTR_CTIME);

        if(FSAL_attr == NULL || Fattr == NULL) {
                LogFullDebug(COMPONENT_NFSPROTO,
                             "nfs3_FSALattr_To_Fattr: FSAL_attr=%p, Fattr=%p",
                             FSAL_attr, Fattr);
                return 0;
        }

        nfs3_FSALattr_To_PartialFattr(FSAL_attr, &got, Fattr);
        if (want & ~got) {
                LogCrit(COMPONENT_NFSPROTO,
                        "Likely bug: FSAL did not fill in a standard NFSv3 "
                        "attribute: missing %lx", want & ~ got);
        }

        /* in NFSv3, we only keeps fsid.major, casted into an nfs_uint64 */
        Fattr->fsid = (nfs3_uint64) pexport->filesystem_id.major;
        LogFullDebug(COMPONENT_NFSPROTO,
                     "fsid.major = %#"PRIX64" (%"PRIu64
                     "), fsid.minor = %#"PRIX64" (%"PRIu64
                     "), nfs3_fsid = %#"PRIX64" (%"PRIu64")",
                     pexport->filesystem_id.major, pexport->filesystem_id.major,
                     pexport->filesystem_id.minor,
                     pexport->filesystem_id.minor,
                     (uint64_t) Fattr->fsid,
                     (uint64_t) Fattr->fsid);
        return 1;
}

/**
 * nfs2_Sattr_To_FSALattr: Converts NFSv2 Set Attributes to FSAL attributes.
 *
 * Converts NFSv2 Set Attributes to FSAL attributes.
 *
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv2 set attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */
int nfs2_Sattr_To_FSALattr(struct attrlist *pFSAL_attr,
                           sattr2 *Fattr)
{
  struct timeval t;

  FSAL_CLEAR_MASK(pFSAL_attr->mask);

  if(Fattr->mode != (unsigned int)-1)
    {
      pFSAL_attr->mode = unix2fsal_mode(Fattr->mode);
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_MODE);
    }

  if(Fattr->uid != (unsigned int)-1)
    {
      pFSAL_attr->owner = Fattr->uid;
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_OWNER);
    }

  if(Fattr->gid != (unsigned int)-1)
    {
      pFSAL_attr->group = Fattr->gid;
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_GROUP);
    }

  if(Fattr->size != (unsigned int)-1)
    {
      /* Both ATTR_SIZE and ATTR_SPACEUSED are to be managed */
      pFSAL_attr->filesize = Fattr->size;
      pFSAL_attr->spaceused = Fattr->size;
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_SIZE);
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_SPACEUSED);
    }

  /* if mtime.useconds == 1 millions,
   * this means we must set atime and mtime
   * to server time (NFS Illustrated p. 98)
   */
  if(Fattr->mtime.useconds == 1000000)
    {
      gettimeofday(&t, NULL);

      pFSAL_attr->atime.seconds = pFSAL_attr->mtime.seconds = t.tv_sec;
      pFSAL_attr->atime.nseconds = pFSAL_attr->mtime.nseconds = 0 ;
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_ATIME);
      FSAL_SET_MASK(pFSAL_attr->mask, ATTR_MTIME);
    }
  else
    {
      /* set atime to client */

      if(Fattr->atime.seconds != (unsigned int)-1)
        {
          pFSAL_attr->atime.seconds = Fattr->atime.seconds;

          if(Fattr->atime.seconds != (unsigned int)-1)
            pFSAL_attr->atime.nseconds = Fattr->atime.useconds * 1000;
          else
            pFSAL_attr->atime.nseconds = 0;     /* ignored */

          FSAL_SET_MASK(pFSAL_attr->mask, ATTR_ATIME);
        }

      /* set mtime to client */

      if(Fattr->mtime.seconds != (unsigned int)-1)
        {
          pFSAL_attr->mtime.seconds = Fattr->mtime.seconds;

          if(Fattr->mtime.seconds != (unsigned int)-1)
            pFSAL_attr->mtime.nseconds = Fattr->mtime.useconds * 1000;
          else
            pFSAL_attr->mtime.nseconds = 0;     /* ignored */

          FSAL_SET_MASK(pFSAL_attr->mask, ATTR_MTIME);
        }
    }

  return 1;
}                               /* nfs2_Sattr_To_FSALattr */

/**
 *
 * nfs4_Fattr_Check_Access_Bitmap: checks if attributes bitmaps have READ or WRITE access.
 *
 * Checks if attributes have READ or WRITE access.
 *
 * @param pbitmap    [IN] pointer to NFSv4 attributes.
 * @param access     [IN] access to be checked, either FATTR4_ATTR_READ or FATTR4_ATTR_WRITE
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Check_Access_Bitmap(bitmap4 * bitmap, int access)
{
  int attribute;

  /* Parameter sanity check */
  if(bitmap == NULL)
    return 0;

  if(access != FATTR4_ATTR_READ && access != FATTR4_ATTR_WRITE)
    return 0;

  for(attribute = next_attr_from_bitmap(bitmap, -1);
      attribute != -1;
      attribute = next_attr_from_bitmap(bitmap, attribute))
    {
      if(attribute > FATTR4_FS_CHARSET_CAP) {
	      /* Erroneous value... skip */
	      continue;
      }
      if(((int)fattr4tab[attribute].access & access) != access)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Check_Access */

/**
 *
 * nfs4_Fattr_Check_Access: checks if attributes have READ or WRITE access.
 *
 * Checks if attributes have READ or WRITE access.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 * @param access     [IN] access to be checked, either FATTR4_ATTR_READ or FATTR4_ATTR_WRITE
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Check_Access(fattr4 * Fattr, int access)
{
  /* Parameter sanity check */
  if(Fattr == NULL)
    return 0;

  return nfs4_Fattr_Check_Access_Bitmap(&Fattr->attrmask, access);
}                               /* nfs4_Fattr_Check_Access */

/**
 *
 * nfs4_bitmap4_Remove_Unsupported: removes unsupported attributes from bitmap4
 *
 * Removes unsupported attributes from bitmap4
 *
 * @param pbitmap    [IN] pointer to NFSv4 attributes's bitmap.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

/** @TODO .supported is not nearly as good as actually checking with the
 *  export.
 */

int nfs4_bitmap4_Remove_Unsupported(bitmap4 *bitmap )
{
	int attribute;

	for(attribute = 0; attribute <= FATTR4_FS_CHARSET_CAP; attribute++) {
		if( !fattr4tab[attribute].supported) {
			if( !clear_attribute_in_bitmap(bitmap, attribute))
				break;
		}
	}
	return 1;
}


/**
 *
 * nfs4_Fattr_Supported: Checks if an attribute is supported.
 *
 * Checks if an attribute is supported.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Supported(fattr4 * Fattr)
{
  /* Parameter sanity check */
  if(Fattr == NULL)
    return 0;

  return nfs4_Fattr_Supported_Bitmap(&Fattr->attrmask);
}                               /* nfs4_Fattr_Supported */

/**
 *
 * nfs4_Fattr_Supported: Checks if an attribute is supported.
 *
 * Checks if an attribute is supported.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Supported_Bitmap(bitmap4 * bitmap)
{
  int attribute;

  /* Parameter sanity check */
  if(bitmap == NULL)
    return 0;

  for(attribute = next_attr_from_bitmap(bitmap, -1);
      attribute != -1;
      attribute = next_attr_from_bitmap(bitmap, attribute))
    {
      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_Fattr_Supported  ==============> %s supported flag=%u | ",
                   fattr4tab[attribute].name, fattr4tab[attribute].supported);

      if( !fattr4tab[attribute].supported)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Supported */

/**
 *
 * nfs4_Fattr_cmp: compares 2 fattr4 buffers.
 *
 * Compares 2 fattr4 buffers.
 *
 * @param Fattr1      [IN] pointer to NFSv4 attributes.
 * @param Fattr2      [IN] pointer to NFSv4 attributes.
 *
 * @return true if attributes are the same, false otherwise, but -1 if RDATTR_ERROR is set
 *
 */
int nfs4_Fattr_cmp(fattr4 * Fattr1, fattr4 * Fattr2)
{
  u_int LastOffset;
  uint32_t i;
  uint32_t k;
  unsigned int cmp = 0;
  u_int len = 0;
  int attribute_to_set = 0;

  if(Fattr1 == NULL)
    return false;

  if(Fattr2 == NULL)
    return false;

  if(Fattr1->attrmask.bitmap4_len != Fattr2->attrmask.bitmap4_len)      /* different mask */
    return false;

  for(i = 0; i < Fattr1->attrmask.bitmap4_len; i++)
	  if(Fattr1->attrmask.bitmap4_val[i] != Fattr2->attrmask.bitmap4_val[i])
		  return FALSE;
  if(attribute_is_set(&Fattr1->attrmask, FATTR4_RDATTR_ERROR))
	  return -1;

  cmp = 0;
  LastOffset = 0;
  len = 0;

  /** There has got to be a better way to do this but we do have to cope
   *  with unaligned buffers for opaque data
   */
  for(attribute_to_set = next_attr_from_bitmap(&Fattr1->attrmask, -1);
      attribute_to_set != -1;
      attribute_to_set = next_attr_from_bitmap(&Fattr1->attrmask, attribute_to_set))
    {
      if(attribute_to_set > FATTR4_FS_CHARSET_CAP) {
	      /* Erroneous value... skip */
	      continue;
      }
      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_Fattr_cmp ==============> %s",
                   fattr4tab[attribute_to_set].name);

      switch (attribute_to_set)
        {
        case FATTR4_SUPPORTED_ATTRS:
          memcpy(&len, (char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          cmp +=
              memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                     (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                     sizeof(u_int));

          len = htonl(len);
          LastOffset += sizeof(u_int);

          for(k = 0; k < len; k++)
            {
              cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                            (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                            sizeof(uint32_t));
              LastOffset += sizeof(uint32_t);
            }

          break;

        case FATTR4_FILEHANDLE:
        case FATTR4_OWNER:
        case FATTR4_OWNER_GROUP:
          memcpy(&len, (char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          len = ntohl(len);     /* xdr marshalling on fattr4 */
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                        sizeof(u_int));
          LastOffset += sizeof(u_int);
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset), len);
          break;

        case FATTR4_TYPE:
        case FATTR4_FH_EXPIRE_TYPE:
        case FATTR4_CHANGE:
        case FATTR4_SIZE:
        case FATTR4_LINK_SUPPORT:
        case FATTR4_SYMLINK_SUPPORT:
        case FATTR4_NAMED_ATTR:
        case FATTR4_FSID:
        case FATTR4_UNIQUE_HANDLES:
        case FATTR4_LEASE_TIME:
        case FATTR4_RDATTR_ERROR:
        case FATTR4_ACL:
        case FATTR4_ACLSUPPORT:
        case FATTR4_ARCHIVE:
        case FATTR4_CANSETTIME:
        case FATTR4_CASE_INSENSITIVE:
        case FATTR4_CASE_PRESERVING:
        case FATTR4_CHOWN_RESTRICTED:
        case FATTR4_FILEID:
        case FATTR4_FILES_AVAIL:
        case FATTR4_FILES_FREE:
        case FATTR4_FILES_TOTAL:
        case FATTR4_FS_LOCATIONS:
        case FATTR4_HIDDEN:
        case FATTR4_HOMOGENEOUS:
        case FATTR4_MAXFILESIZE:
        case FATTR4_MAXLINK:
        case FATTR4_MAXNAME:
        case FATTR4_MAXREAD:
        case FATTR4_MAXWRITE:
        case FATTR4_MIMETYPE:
        case FATTR4_MODE:
        case FATTR4_NO_TRUNC:
        case FATTR4_NUMLINKS:
        case FATTR4_QUOTA_AVAIL_HARD:
        case FATTR4_QUOTA_AVAIL_SOFT:
        case FATTR4_QUOTA_USED:
        case FATTR4_RAWDEV:
        case FATTR4_SPACE_AVAIL:
        case FATTR4_SPACE_FREE:
        case FATTR4_SPACE_TOTAL:
        case FATTR4_SPACE_USED:
        case FATTR4_SYSTEM:
        case FATTR4_TIME_ACCESS:
        case FATTR4_TIME_ACCESS_SET:
        case FATTR4_TIME_BACKUP:
        case FATTR4_TIME_CREATE:
        case FATTR4_TIME_DELTA:
        case FATTR4_TIME_METADATA:
        case FATTR4_TIME_MODIFY:
        case FATTR4_TIME_MODIFY_SET:
        case FATTR4_MOUNTED_ON_FILEID:
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                        fattr4tab[attribute_to_set].size_fattr4);
          break;

        default:
          return 0;
          break;
        }
    }
  if(cmp == 0)
    return true;
  else
    return false;
}

#ifdef _USE_NFS4_ACL
static int nfs4_decode_acl_special_user(utf8string *utf8str, int *who)
{
  int i;

  for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++)
    {
      if(strncmp(utf8str->utf8string_val, whostr_2_type_map[i].string, utf8str->utf8string_len) == 0)
        {
          *who = whostr_2_type_map[i].type;
          return 0;
        }
    }

  return -1;
}

static int nfs4_decode_acl(fsal_attrib_list_t * pFSAL_attr,
			   unsigned char *current_pos,
			   u_int *attr_len)
{
  fsal_acl_status_t status;
  fsal_acl_data_t acldata;
  fsal_ace_t *pace;
  fsal_acl_t *pacl;
  int len;
  char buffer[MAXNAMLEN];
  utf8string utf8buffer;
  int who;
  u_int offset;
  int nfs_status = NFS4_OK;

  if(*attr_len < sizeof(u_int)) {
	  nfs_status = NFS4ERR_BADXDR;
	  goto out;
  }
  /* Decode number of ACEs. */
  memcpy(&(acldata.naces), current_pos, sizeof(u_int));
  acldata.naces = ntohl(acldata.naces);
  LogFullDebug(COMPONENT_NFS_V4,
               "SATTR: Number of ACEs = %u",
               acldata.naces);
  offset = sizeof(u_int);

  /* Allocate memory for ACEs. */
  acldata.aces = (fsal_ace_t *)nfs4_ace_alloc(acldata.naces);
  if(acldata.aces == NULL)
    {
      LogCrit(COMPONENT_NFS_V4,
              "SATTR: Failed to allocate ACEs");
      nfs_status = NFS4ERR_SERVERFAULT;
      goto out;
    }
  memset(acldata.aces, 0, acldata.naces * sizeof(fsal_ace_t));

  /* Decode ACEs. */
  for(pace = acldata.aces; pace < acldata.aces + acldata.naces; pace++)
    {
      if(*attr_len < (sizeof(fsal_acetype_t) +
		      sizeof(fsal_aceflag_t) +
		      sizeof(fsal_aceperm_t) +
		      sizeof(u_int))) {
	nfs_status = NFS4ERR_BADXDR;
	goto baderr;
      }
      memcpy(&(pace->type), (char*)(current_pos + offset), sizeof(fsal_acetype_t));
      pace->type = ntohl(pace->type);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE type = 0x%x",
                   pace->type);
      offset += sizeof(fsal_acetype_t);

      memcpy(&(pace->flag), (char*)(current_pos + offset), sizeof(fsal_aceflag_t));
      pace->flag = ntohl(pace->flag);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE flag = 0x%x",
                   pace->flag);
      offset += sizeof(fsal_aceflag_t);

      memcpy(&(pace->perm), (char*)(current_pos + offset), sizeof(fsal_aceperm_t));
      pace->perm = ntohl(pace->perm);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE perm = 0x%x",
                   pace->perm);
      offset += sizeof(fsal_aceperm_t);

      /* Find out who type */

      /* Convert name to uid or gid */
      memcpy(&len, (char *)(current_pos + offset), sizeof(u_int));
      len = ntohl(len);        /* xdr marshalling on fattr4 */
      offset += sizeof(u_int);

      if(*attr_len < (offset + len)) {
	nfs_status = NFS4ERR_BADXDR;
	goto baderr;
      }
      memcpy(buffer, (char *)(current_pos + offset), len);
      buffer[len] = '\0';

      /* Do not forget that xdr_opaque are aligned on 32bit long words */
      while((len % 4) != 0)
        len += 1;

      offset += len;

      /* Decode users. */
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: owner = %s, len = %d, type = %s",
                   buffer, len,
                   GET_FSAL_ACE_WHO_TYPE(*pace));

      utf8buffer.utf8string_val = buffer;
      utf8buffer.utf8string_len = strlen(buffer);

      if(nfs4_decode_acl_special_user(&utf8buffer, &who) == 0)  /* Decode special user. */
        {
          /* Clear group flag for special users */
          pace->flag &= ~(FSAL_ACE_FLAG_GROUP_ID);
          pace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
          pace->who.uid = who;
          LogFullDebug(COMPONENT_NFS_V4,
                       "SATTR: ACE special who.uid = 0x%x",
                       pace->who.uid);
        }
      else
        {
          if(pace->flag == FSAL_ACE_FLAG_GROUP_ID)  /* Decode group. */
            {
              utf82gid(&utf8buffer, &(pace->who.gid));
              LogFullDebug(COMPONENT_NFS_V4,
                           "SATTR: ACE who.gid = 0x%x",
                           pace->who.gid);
            }
          else  /* Decode user. */
            {
              utf82uid(&utf8buffer, &(pace->who.uid));
              LogFullDebug(COMPONENT_NFS_V4,
                           "SATTR: ACE who.uid = 0x%x",
                           pace->who.uid);
            }
        }

      /* Check if we can map a name string to uid or gid. If we can't, do cleanup
       * and bubble up NFS4ERR_BADOWNER. */
      if((pace->flag == FSAL_ACE_FLAG_GROUP_ID ? pace->who.gid : pace->who.uid) == -1)
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "SATTR: bad owner");
          nfs4_ace_free(acldata.aces);
          nfs_status =  NFS4ERR_BADOWNER;
	  goto baderr;
        }
    }

  pacl = nfs4_acl_new_entry(&acldata, &status);
  pFSAL_attr->acl = pacl;
  if(pacl == NULL)
    {
      LogCrit(COMPONENT_NFS_V4,
              "SATTR: Failed to create a new entry for ACL");
      nfs_status =  NFS4ERR_SERVERFAULT;
      goto baderr;
    }
  else
     LogFullDebug(COMPONENT_NFS_V4,
                  "SATTR: Successfully created a new entry for ACL, status = %u",
                  status);

  /* Set new ACL */
  LogFullDebug(COMPONENT_NFS_V4,
               "SATTR: new acl = %p",
               pacl);
  goto out;

baderr:
/* free memory or leak! or does new_entry release it? */

out:
  *attr_len = offset;
  return nfs_status;
}
#endif                          /* _USE_NFS4_ACL */

/**
 *
 * Fattr4_To_FSAL_attr: Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * NB! If the pointer for the handle is provided the memory is not allocated,
 *     the handle's nfs_fh4_val points inside fattr4. The pointer is valid
 *     as long as fattr4 is valid.
 *
 * @param pFSAL_attr [OUT]  pointer to FSAL attributes.
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 * @param hdl4       [OUT] optional pointer to return NFSv4 file handle
 *
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */

int Fattr4_To_FSAL_attr(struct attrlist *attrs,
                        fattr4 *Fattr, nfs_fh4 *hdl4)
{
	int attribute_to_set = 0;
	int nfs_status = NFS4_OK;
	XDR attr_body;
	struct xdr_attrs_args args;
	fattr_xdr_result xdr_res;

	if(attrs == NULL || Fattr == NULL)
		return NFS4ERR_BADXDR;

	/* Check attributes data */
	if((Fattr->attr_vals.attrlist4_val == NULL) ||
	   (Fattr->attr_vals.attrlist4_len == 0))
		return NFS4_OK;

	/* Init */
	xdrmem_create(&attr_body,
		      Fattr->attr_vals.attrlist4_val,
		      Fattr->attr_vals.attrlist4_len,
		      XDR_DECODE);
	FSAL_CLEAR_MASK(attrs->mask);
	memset(&args, 0, sizeof(args));
	args.attrs = attrs;
	args.hdl4 = hdl4;
	args.nfs_status = NFS4_OK;

	for(attribute_to_set = next_attr_from_bitmap(&Fattr->attrmask, -1);
	    attribute_to_set != -1;
	    attribute_to_set = next_attr_from_bitmap(&Fattr->attrmask,
						     attribute_to_set)) {
		if(attribute_to_set > FATTR4_FS_CHARSET_CAP) {
			nfs_status = NFS4ERR_BADXDR; /* undefined attr */
			break;
		}
		xdr_res = fattr4tab[attribute_to_set].decode(&attr_body, &args);
		if(xdr_res == FATTR_XDR_SUCCESS) {
			FSAL_SET_MASK(attrs->mask, fattr4tab[attribute_to_set].attrmask);
			LogFullDebug(COMPONENT_NFS_V4,
				     "Decode attribute %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
		} else if(xdr_res == FATTR_XDR_NOOP) {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Attribute not supported %d name=%s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
			if(nfs_status == NFS4_OK) /* preserve previous error */
				nfs_status = NFS4ERR_ATTRNOTSUPP;
			break;
		} else {
			LogFullDebug(COMPONENT_NFS_V4,
				     "Decode attribute FAILED: %d, name = %s",
				     attribute_to_set,
				     fattr4tab[attribute_to_set].name);
			if(args.nfs_status == NFS4_OK)
				nfs_status = NFS4ERR_BADXDR;
			else
				nfs_status = args.nfs_status;
			break;
		}
	}
	if(xdr_getpos(&attr_body) < Fattr->attr_vals.attrlist4_len) {
		nfs_status = NFS4ERR_BADXDR;  /* underrun on attribute */
	}
	xdr_destroy(&attr_body);
	return nfs_status;
}                               /* Fattr4_To_FSAL_attr */

/**
 *
 * nfs4_Fattr_To_FSAL_attr: Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * @param pFSAL_attr [OUT]  pointer to FSAL attributes.
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */
int nfs4_Fattr_To_FSAL_attr(struct attrlist *pFSAL_attr, fattr4 *Fattr)
{
  return Fattr4_To_FSAL_attr(pFSAL_attr, Fattr, NULL);
}

/* Error conversion routines */
/**
 *
 * nfs4_Errno: Converts a cache_inode status to a nfsv4 status.
 *
 *  Converts a cache_inode status to a nfsv4 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 *
 * @return the converted NFSv4 status.
 *
 */
nfsstat4 nfs4_Errno(cache_inode_status_t error)
{
  nfsstat4 nfserror= NFS4ERR_INVAL;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS4_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
      nfserror = NFS4ERR_SERVERFAULT;
      break;

    case CACHE_INODE_UNAPPROPRIATED_KEY:
      nfserror = NFS4ERR_BADHANDLE;
      break;

    case CACHE_INODE_BAD_TYPE:
    case CACHE_INODE_INVALID_ARGUMENT:
      nfserror = NFS4ERR_INVAL;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFS4ERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFS4ERR_EXIST;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFS4ERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFS4ERR_NOENT;
      break;

    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
      nfserror = NFS4ERR_IO;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFS4ERR_ACCESS;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFS4ERR_PERM;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFS4ERR_NOSPC;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFS4ERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFS4ERR_ROFS;
      break;

    case CACHE_INODE_IO_ERROR:
      nfserror = NFS4ERR_IO;
      break;

     case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFS4ERR_NAMETOOLONG;
      break;

    case CACHE_INODE_KILLED:
    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFS4ERR_STALE;
      break;

    case CACHE_INODE_STATE_CONFLICT:
      nfserror = NFS4ERR_PERM;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFS4ERR_DQUOT;
      break;

    case CACHE_INODE_NOT_SUPPORTED:
      nfserror = NFS4ERR_NOTSUPP;
      break;

    case CACHE_INODE_DELAY:
      nfserror = NFS4ERR_DELAY;
      break;

    case CACHE_INODE_FILE_BIG:
      nfserror = NFS4ERR_FBIG;
      break;

    case CACHE_INODE_FILE_OPEN:
      nfserror = NFS4ERR_FILE_OPEN;
      break;

    case CACHE_INODE_STATE_ERROR:
      nfserror = NFS4ERR_BAD_STATEID;
      break;

    case CACHE_INODE_BAD_COOKIE:
      nfserror = NFS4ERR_BAD_COOKIE;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_ASYNC_POST_ERROR:
      /* Should not occur */
      nfserror = NFS4ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs4_Errno */

/**
 *
 * nfs3_Errno: Converts a cache_inode status to a nfsv3 status.
 *
 *  Converts a cache_inode status to a nfsv3 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 *
 * @return the converted NFSv3 status.
 *
 */
nfsstat3 nfs3_Errno(cache_inode_status_t error)
{
  nfsstat3 nfserror= NFS3ERR_INVAL;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS3_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
    case CACHE_INODE_FILE_OPEN:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFS3ERR_IO but was set non-retryable",
              error);
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_INVALID_ARGUMENT:
      nfserror = NFS3ERR_INVAL;
      break;

    case CACHE_INODE_FSAL_ERROR:
                                         /** @todo: Check if this works by making stress tests */
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_FSAL_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFS3ERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFS3ERR_EXIST;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFS3ERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFS3ERR_NOENT;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFS3ERR_ACCES;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFS3ERR_PERM;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFS3ERR_NOSPC;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFS3ERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFS3ERR_ROFS;
      break;

    case CACHE_INODE_KILLED:
    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFS3ERR_STALE;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFS3ERR_DQUOT;
      break;

    case CACHE_INODE_BAD_TYPE:
      nfserror = NFS3ERR_BADTYPE;
      break;

    case CACHE_INODE_NOT_SUPPORTED:
      nfserror = NFS3ERR_NOTSUPP;
      break;

    case CACHE_INODE_DELAY:
      nfserror = NFS3ERR_JUKEBOX;
      break;

    case CACHE_INODE_IO_ERROR:
        LogCrit(COMPONENT_NFSPROTO,
                "Error CACHE_INODE_IO_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFS3ERR_NAMETOOLONG;
      break;

    case CACHE_INODE_FILE_BIG:
      nfserror = NFS3ERR_FBIG;
      break;

    case CACHE_INODE_BAD_COOKIE:
      nfserror = NFS3ERR_BAD_COOKIE;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
        /* Should not occur */
        LogDebug(COMPONENT_NFSPROTO,
                 "Line %u should never be reached in nfs3_Errno for cache_status=%u",
                 __LINE__, error);
      nfserror = NFS3ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs3_Errno */

/**
 *
 * nfs2_Errno: Converts a cache_inode status to a nfsv2 status.
 *
 *  Converts a cache_inode status to a nfsv2 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 *
 * @return the converted NFSv2 status.
 *
 */
nfsstat2 nfs2_Errno(cache_inode_status_t error)
{
  nfsstat2 nfserror= NFSERR_IO;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_BAD_TYPE:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
    case CACHE_INODE_INVALID_ARGUMENT:
    case CACHE_INODE_FILE_OPEN:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFSERR_IO but was set non-retryable",
              error);
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFSERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFSERR_EXIST;
      break;

    case CACHE_INODE_FSAL_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_FSAL_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFSERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFSERR_NOENT;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFSERR_ACCES;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFSERR_NOSPC;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFSERR_PERM;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFSERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFSERR_ROFS;
      break;

    case CACHE_INODE_KILLED:
    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFSERR_STALE;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFSERR_DQUOT;
      break;

    case CACHE_INODE_IO_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_IO_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFSERR_NAMETOOLONG;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
    case CACHE_INODE_NOT_SUPPORTED:
    case CACHE_INODE_DELAY:
    case CACHE_INODE_BAD_COOKIE:
    case CACHE_INODE_FILE_BIG:
        /* Should not occur */
      LogDebug(COMPONENT_NFSPROTO,
               "Line %u should never be reached in nfs2_Errno", __LINE__);
      nfserror = NFSERR_IO;
      break;
    }

  return nfserror;
}                               /* nfs2_Errno */

/**
 *
 * nfs3_AllocateFH: Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * Allocates a buffer to be used for storing a NFSv3 filehandle.
 *
 * @param fh [INOUT] the filehandle to manage.
 *
 * @return NFS3_OK if successful, NFS3ERR_SERVERFAULT, NFS3ERR_RESOURCE or NFS3ERR_STALE  otherwise.
 *
 */
int nfs3_AllocateFH(nfs_fh3 *fh)
{
  if(fh == NULL)
    return NFS3ERR_SERVERFAULT;

  /* Allocating the filehandle in memory */
  fh->data.data_len = sizeof(struct alloc_file_handle_v3);
  if((fh->data.data_val = gsh_malloc(fh->data.data_len)) == NULL)
    {
      LogError(COMPONENT_NFSPROTO, ERR_SYS, ERR_MALLOC, errno);
      return NFS3ERR_SERVERFAULT;
    }

  memset((char *)fh->data.data_val, 0, fh->data.data_len);

  return NFS3_OK;
}                               /* nfs4_AllocateFH */

/**
 *
 * nfs4_AllocateFH: Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * @param fh [INOUT] the filehandle to manage.
 *
 * @return NFS4_OK if successful, NFS3ERR_SERVERFAULT, NFS4ERR_RESOURCE or NFS4ERR_STALE  otherwise.
 *
 */
int nfs4_AllocateFH(nfs_fh4 * fh)
{
  if(fh == NULL)
    return NFS4ERR_SERVERFAULT;

  /* Allocating the filehandle in memory */
  fh->nfs_fh4_len = sizeof(struct alloc_file_handle_v4);
  if((fh->nfs_fh4_val = gsh_malloc(fh->nfs_fh4_len)) == NULL)
    {
      LogError(COMPONENT_NFS_V4, ERR_SYS, ERR_MALLOC, errno);
      return NFS4ERR_RESOURCE;
    }

  memset((char *)fh->nfs_fh4_val, 0, fh->nfs_fh4_len);

  return NFS4_OK;
}                               /* nfs4_AllocateFH */

/**
 *
 * nfs4_MakeCred
 *
 * This routine fills in the pcontext field in the compound data.
 *
 * @param pfh [INOUT] pointer to compound data to be used. NOT YET IMPLEMENTED
 *
 * @return NFS4_OK if successful, NFS4ERR_WRONGSEC otherwise.
 *
 */
int nfs4_MakeCred(compound_data_t * data)
{
  exportlist_client_entry_t related_client;
  struct user_cred user_credentials;

  if (!(get_req_uid_gid(data->reqp,
                      data->pexport,
                        &user_credentials)))
    return NFS4ERR_WRONGSEC;

  LogFullDebug(COMPONENT_DISPATCH,
               "nfs4_MakeCred about to call nfs_export_check_access");
  if(!(nfs_export_check_access(&data->pworker->hostaddr,
                               data->reqp,
                               data->pexport,
                               nfs_param.core_param.program[P_NFS],
                               nfs_param.core_param.program[P_MNT],
                               data->pworker->ht_ip_stats,
                               ip_stats_pool,
                               &related_client,
                               data->req_ctx->creds,
                               false) /* So check_access() doesn't deny based on whether this is a RO export. */
             ))
    return NFS4ERR_WRONGSEC;
  if(!(nfs_check_anon(&related_client, data->pexport,
                      data->req_ctx->creds)))
    return NFS4ERR_WRONGSEC;

  return NFS4_OK;
}                               /* nfs4_MakeCred */

/* Create access mask based on given access operation. Both mode and ace4
 * mask are encoded. */
fsal_accessflags_t nfs_get_access_mask(uint32_t op,
                                       const struct attrlist *pattr)
{
  fsal_accessflags_t access_mask = 0;

  switch(op)
    {
      case ACCESS3_READ:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_R_OK);
        if(pattr->type == DIRECTORY)
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_DATA);
      break;

      case ACCESS3_LOOKUP:
        if(pattr->type != DIRECTORY)
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_X_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
      break;

      case ACCESS3_MODIFY:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        if(pattr->type == DIRECTORY)
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
      break;

      case ACCESS3_EXTEND:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        if(pattr->type == DIRECTORY)
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                                            FSAL_ACE_PERM_ADD_SUBDIRECTORY);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_APPEND_DATA);
      break;

      case ACCESS3_DELETE:
        if(pattr->type != DIRECTORY)
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);
      break;

      case ACCESS3_EXECUTE:
        if(pattr->type == DIRECTORY)
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_X_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE);
      break;
    }

  return access_mask;
}

void nfs3_access_debug(char *label, uint32_t access)
{
  LogDebug(COMPONENT_NFSPROTO, "%s=%s,%s,%s,%s,%s,%s",
           label,
           FSAL_TEST_MASK(access, ACCESS3_READ) ? "READ" : "-",
           FSAL_TEST_MASK(access, ACCESS3_LOOKUP) ? "LOOKUP" : "-",
           FSAL_TEST_MASK(access, ACCESS3_MODIFY) ? "MODIFY" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXTEND) ? "EXTEND" : "-",
           FSAL_TEST_MASK(access, ACCESS3_DELETE) ? "DELETE" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXECUTE) ? "EXECUTE" : "-");
}

void nfs4_access_debug(char *label, uint32_t access, fsal_aceperm_t v4mask)
{
  LogDebug(COMPONENT_NFSPROTO, "%s=%s,%s,%s,%s,%s,%s",
           label,
           FSAL_TEST_MASK(access, ACCESS3_READ) ? "READ" : "-",
           FSAL_TEST_MASK(access, ACCESS3_LOOKUP) ? "LOOKUP" : "-",
           FSAL_TEST_MASK(access, ACCESS3_MODIFY) ? "MODIFY" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXTEND) ? "EXTEND" : "-",
           FSAL_TEST_MASK(access, ACCESS3_DELETE) ? "DELETE" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXECUTE) ? "EXECUTE" : "-");

  if(v4mask)
    LogDebug(COMPONENT_NFSPROTO, "v4mask=%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_DATA)		 ? 'r':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_DATA)		 ? 'w':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_EXECUTE)		 ? 'x':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_ADD_SUBDIRECTORY)    ? 'm':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_NAMED_ATTR)	 ? 'n':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_NAMED_ATTR) 	 ? 'N':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_DELETE_CHILD) 	 ? 'p':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_ATTR)		 ? 't':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_ATTR)		 ? 'T':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_DELETE)		 ? 'd':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_ACL) 		 ? 'c':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_ACL)		 ? 'C':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_OWNER)	 ? 'o':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_SYNCHRONIZE)	 ? 'z':'-');
}

/**
 * @brief Do basic checks on a filehandle
 *
 * This function performs basic checks to make sure the supplied
 * filehandle is sane for a given operation.
 *
 * @param data          [IN] Compound_data_t for the operation to check
 * @param required_type [IN] The file type this operation requires.
 *                           Set to 0 to allow any type.
 * @param ds_allowed    [IN] true if DS handles are allowed.
 *
 * @return NFSv4.1 status codes
 */

nfsstat4
nfs4_sanity_check_FH(compound_data_t *data,
                     object_file_type_t required_type,
                     bool ds_allowed)
{
        /* If there is no FH */
        if (nfs4_Is_Fh_Empty(&(data->currentFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Empty failed");
                return NFS4ERR_NOFILEHANDLE;
        }

        /* If the filehandle is invalid */
        if (nfs4_Is_Fh_Invalid(&(data->currentFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Invalid failed");
                return NFS4ERR_BADHANDLE;
        }

        /* Tests if the Filehandle is expired (for volatile filehandle) */
        if (nfs4_Is_Fh_Expired(&(data->currentFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Expired failed");
                return NFS4ERR_FHEXPIRED;
        }

        /* Check for the correct file type */
        if (required_type != NO_FILE_TYPE) {
                if (data->current_filetype != required_type) {
                        LogDebug(COMPONENT_NFSPROTO,
                                 "Wrong file type");

                        if(required_type == DIRECTORY) {
                                return NFS4ERR_NOTDIR;
                        }
                        else if(required_type == SYMBOLIC_LINK) {
                                return NFS4ERR_INVAL;
                        }

                        switch (data->current_filetype) {
                        case DIRECTORY:
                                return NFS4ERR_ISDIR;
                        default:
                                return NFS4ERR_INVAL;
                        }
                }
        }

        if (nfs4_Is_Fh_DSHandle(&data->currentFH) && !ds_allowed) {
                return NFS4ERR_INVAL;
        }

        return NFS4_OK;
} /* nfs4_sanity_check_FH */

/* nfs4_utf8string2dynamic
 * unpack the input string from the XDR into a null term'd string
 * scan for bad chars
 */

nfsstat4 nfs4_utf8string2dynamic(const utf8string *input,
				 utf8_scantype_t scan,
				 char **obj_name)
{
	nfsstat4 status = NFS4_OK;

	*obj_name = NULL;
	if(input->utf8string_val == NULL ||
	   input->utf8string_len == 0) {
		return NFS4ERR_INVAL;
	}
	if(input->utf8string_len >=  MAXNAMLEN) {
		return NFS4ERR_NAMETOOLONG;
	}
        char *name = gsh_malloc(input->utf8string_len + 1);
        if (name == NULL) {
		return NFS4ERR_SERVERFAULT;
        }
	memcpy(name,
	       input->utf8string_val,
	       input->utf8string_len);
	name[input->utf8string_len] = '\0';
	if(scan != UTF8_SCAN_NONE)
		status = path_filter(name, scan);
	if(status == NFS4_OK)
		*obj_name = name;
	else
		gsh_free(name);
        return status;
}

/**
 *
 * @brief Do basic checks on saved filehandle
 *
 * This function performs basic checks to make sure the supplied
 * filehandle is sane for a given operation.
 *
 * @param data          [IN] Compound_data_t for the operation to check
 * @param required_type [IN] The file type this operation requires.
 *                           Set to 0 to allow any type.
 * @param ds_allowed    [IN] true if DS handles are allowed.
 *
 * @return NFSv4.1 status codes
 */

nfsstat4
nfs4_sanity_check_saved_FH(compound_data_t *data,
                           object_file_type_t required_type,
                           bool ds_allowed)
{
        /* If there is no FH */
        if (nfs4_Is_Fh_Empty(&(data->savedFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Empty failed");
                return NFS4ERR_NOFILEHANDLE;
        }

        /* If the filehandle is invalid */
        if (nfs4_Is_Fh_Invalid(&(data->savedFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Invalid failed");
                return NFS4ERR_BADHANDLE;
        }

        /* Tests if the Filehandle is expired (for volatile filehandle) */
        if (nfs4_Is_Fh_Expired(&(data->savedFH))) {
                LogDebug(COMPONENT_FILEHANDLE,
                         "nfs4_Is_Fh_Expired failed");
                return NFS4ERR_FHEXPIRED;
        }

        /* Check for the correct file type */
        if (required_type != NO_FILE_TYPE) {
                if (data->saved_filetype != required_type) {
                        LogDebug(COMPONENT_NFSPROTO,
                                 "Wrong file type");

                        if (required_type == DIRECTORY) {
                                return NFS4ERR_NOTDIR;
                        }
                        else if (required_type == SYMBOLIC_LINK) {
                                return NFS4ERR_INVAL;
                        }

                        switch (data->saved_filetype) {
                        case DIRECTORY:
                                return NFS4ERR_ISDIR;
                        default:
                                return NFS4ERR_INVAL;
                        }
                }
        }

        if (nfs4_Is_Fh_DSHandle(&data->savedFH) && !ds_allowed) {
                return NFS4ERR_INVAL;
        }

        return NFS4_OK;
} /* nfs4_sanity_check_saved_FH */
