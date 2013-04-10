/*
 *
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
 * \file    nfs_file_handle.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.8 $
 * \brief   Prototypes for the file handle in v2, v3, v4
 *
 * nfs_file_handle.h : Prototypes for the file handle in v2, v3, v4. 
 *
 */

#ifndef _NFS_FILE_HANDLE_H
#define _NFS_FILE_HANDLE_H

#include <sys/types.h>
#include <sys/param.h>


#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>              /* for having MAXHOSTNAMELEN */
#include "log.h"
#include "nfs23.h"
#ifdef _USE_NLM
#include "nlm4.h"
#endif

/*
 * Structure of the filehandle
 * these structures must be naturally aligned.  The xdr buffer from/to which
 * they come/go are 4 byte aligned.
 */

#define ULTIMATE_ANSWER 0x42

#define GANESHA_FH_VERSION ULTIMATE_ANSWER - 1

/* This must be exactly 32 bytes long, and aligned on 32 bits */
typedef struct file_handle_v2
{
  uint8_t fhversion;	/* set to 0x41 to separate from Linux knfsd len = 1 byte */
  uint8_t xattr_pos;    /* Used for xattr management                len = 1 byte  */
  uint16_t exportid;    /* must be correlated to exportlist_t::id   len = 2 bytes  */
  uint8_t fsopaque[28]; /* persistent part of FSAL handle, opaque   len = 28 bytes */
} file_handle_v2_t;

/* An NFSv2 handle of fixed size. use for allocations only.
 * there is no padding because v2 handles must be fixed size.
 */

struct alloc_file_handle_v2 {
	struct file_handle_v2 handle;	/* the real handle */
};

/* This is up to 64 bytes long, aligned on 32 bits */
typedef struct file_handle_v3
{
  uint8_t fhversion;	/* set to 0x41 to separate from Linux knfsd len = 1 byte */
  uint8_t xattr_pos;    /* Used for xattr management                len = 1  byte  */
  uint16_t exportid;    /* must be correlated to exportlist_t::id   len = 2 bytes   */
  uint8_t fs_len;       /* actual length of opaque handle           len = 1  byte */
  uint8_t fsopaque[];   /* persistent part of FSAL handle, opaque   len <= 59 bytes  */
} file_handle_v3_t;

/* An NFSv3 handle of maximum size. use this for allocations, sizeof, and memset only.
 * the pad space is where the opaque handle expands into. pad is struct aligned
 */

struct alloc_file_handle_v3 {
	struct file_handle_v3 handle;	/* the real handle */
	uint8_t pad[58];			/* pad to mandatory max 64 bytes */
};

/* nfs3_sizeof_handle
 * return the actual size of a handle based on the sized fsopaque
 */

static inline size_t nfs3_sizeof_handle(struct file_handle_v3 *hdl)
{
	int padding = 0;
	int len = offsetof(struct file_handle_v3, fsopaque) + hdl->fs_len;

	/* Correct packet's fh length so it's divisible by 4 to trick dNFS into
	 * working. This is essentially sending the padding.
	 */
	padding = (4 - (len % 4)) % 4;
	if ((len + padding) <= sizeof(struct alloc_file_handle_v3))
		len += padding;

	return len;
}

/* This is up to 128 bytes, aligned on 32 bits
 */
typedef struct file_handle_v4
{
  uint8_t fhversion;	  /* set to 0x41 to separate from Linux knfsd len = 1 byte */
  uint8_t xattr_pos;      /*                                          len = 1 byte    */
  uint16_t exportid;      /* must be correlated to exportlist_t::id   len = 2 bytes   */
  uint32_t srvboot_time;  /* 0 if FH won't expire                     len = 4 bytes   */
  uint16_t pseudofs_id;   /* Id for the pseudo fs related to this fh  len = 2 bytes   */
  uint16_t refid;         /* used for referral                        len = 2 bytes   */
  uint8_t ds_flag;        /* TRUE if FH is a 'DS file handle'         len = 1 byte    */
  uint8_t pseudofs_flag;  /* TRUE if FH is within pseudofs            len = 1 byte    */
  uint8_t fs_len;         /* actual length of opaque handle           len = 1  byte */
  uint8_t fsopaque[];     /* persistent part of FSAL handle           len <= 113 bytes */
} file_handle_v4_t;

/* An NFSv4 handle of maximum size.  use for allocations, sizeof, and memset only
 * the pad space is where the opaque handle expands into. pad is struct aligned
 */
struct alloc_file_handle_v4 {
	struct file_handle_v4 handle;	/* the real handle */
	uint8_t pad[112];			/* pad to mandatory max 128 bytes */
};

/* nfs4_sizeof_handle
 * return the actual size of a handle based on the sized fsopaque
 */

static inline size_t nfs4_sizeof_handle(struct file_handle_v4 *hdl)
{
	return offsetof(struct file_handle_v4, fsopaque) + hdl->fs_len;
}

/* Define size of string buffer to hold an NFS handle large enough
 * to hold a display_opaque_value of an NFS v4 handle (plus a bit).
 */
#define LEN_FH_STR (NFS4_FHSIZE * 2 + 10)


/* File handle translation utility */
int nfs4_FhandleToFSAL(nfs_fh4 * pfh4,
		       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t * pcontext);
int nfs3_FhandleToFSAL(nfs_fh3 * pfh3,
		       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t * pcontext);
int nfs2_FhandleToFSAL(fhandle2 * pfh2,
		       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t * pcontext);

int nfs4_FSALToFhandle(nfs_fh4 *pfh4,
                       fsal_handle_t *pfsalhandle,
                       compound_data_t *data);
int nfs3_FSALToFhandle(nfs_fh3 * pfh3, fsal_handle_t * pfsalhandle,
                       exportlist_t * pexport);
int nfs2_FSALToFhandle(fhandle2 * pfh2, fsal_handle_t * pfsalhandle,
                       exportlist_t * pexport);

/* Extraction of export id from a file handle */
short nfs2_FhandleToExportId(fhandle2 * pfh2);
short nfs3_FhandleToExportId(nfs_fh3 * pfh3);

/* NFSv4 specific FH related functions */
int nfs4_Is_Fh_Empty(nfs_fh4 * pfh);
int nfs4_Is_Fh_Xattr(nfs_fh4 * pfh);
int nfs4_Is_Fh_Pseudo(nfs_fh4 * pfh);
int nfs4_Is_Fh_Expired(nfs_fh4 * pfh);
int nfs4_Is_Fh_Invalid(nfs_fh4 * pfh);
int nfs4_Is_Fh_Referral(nfs_fh4 * pfh);
int nfs4_Is_Fh_DSHandle(nfs_fh4 * pfh);

/**
 *
 * nfs4_FhandleToExportId
 *
 * This routine extracts the export id from the file handle NFSv4
 *
 * @param pfh4 [IN] file handle to manage.
 * 
 * @return the export id.
 *
 */
static inline short nfs4_FhandleToExportId(nfs_fh4 * pfh4)
{
  file_handle_v4_t *pfile_handle = (file_handle_v4_t *) (pfh4->nfs_fh4_val);

  if(nfs4_Is_Fh_Invalid(pfh4) != NFS4_OK)
    return -1;                  /* Badly formed arguments */

  if(nfs4_Is_Fh_Pseudo(pfh4))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: PseudoFS handle");
      return -1;
    }

  return pfile_handle->exportid;
}                               /* nfs4_FhandleToExportId */


#ifdef _USE_NLM
static inline short nlm4_FhandleToExportId(netobj * pfh3)
{
  nfs_fh3 fh3;
  if(pfh3 == NULL)
    return nfs3_FhandleToExportId(NULL);
  fh3.data.data_val = pfh3->n_bytes;
  fh3.data.data_len = pfh3->n_len;
  return nfs3_FhandleToExportId(&fh3);
}
#endif

/* nfs3 validation */
int nfs3_Is_Fh_Invalid(nfs_fh3 *pfh3);

/* This one is used to detect Xattr related FH */
int nfs3_Is_Fh_Xattr(nfs_fh3 * pfh);

/* File handle display functions */

static inline int display_fhandle2(struct display_buffer * dspbuf,
                                   fhandle2              * fh)
{
  return display_opaque_value(dspbuf, fh, sizeof(*fh));
}

static inline int display_fhandle3(struct display_buffer * dspbuf,
                                   nfs_fh3               * fh)
{
  return display_opaque_value(dspbuf, fh->data.data_val, fh->data.data_len);
}


static inline int display_fhandle_nlm(struct display_buffer * dspbuf,
                                      netobj                * fh)
{
  return display_opaque_value(dspbuf, fh->n_bytes, fh->n_len);
}

#endif                          /* _NFS_FILE_HANDLE_H */
