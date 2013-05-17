/*
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
 * \file    nfs_filehandle_mgmt.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.12 $
 * \brief   Some tools for managing the file handles. 
 *
 * nfs_filehandle_mgmt.c : Some tools for managing the file handles.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_filehandle_mgmt.c,v 1.12 2006/01/24 11:43:05 deniel Exp $
 *
 * $Log: nfs_filehandle_mgmt.c,v $
 * Revision 1.12  2006/01/24 11:43:05  deniel
 * Code cleaning in progress
 *
 * Revision 1.11  2006/01/11 08:12:18  deniel
 * Added bug track and warning for badly formed handles
 *
 * Revision 1.9  2005/09/07 08:58:30  deniel
 * NFSv2 FH was only 31 byte long instead of 32
 *
 * Revision 1.8  2005/09/07 08:16:07  deniel
 * The checksum is filled with zeros before being computed to avoid 'dead beef' values
 *
 * Revision 1.7  2005/08/11 12:37:28  deniel
 * Added statistics management
 *
 * Revision 1.6  2005/08/09 12:35:37  leibovic
 * setting file_handle to 0 in nfs3_FSALToFhandle, before writting into it.
 *
 * Revision 1.5  2005/08/08 14:09:25  leibovic
 * setting checksum to 0 before writting in it.
 *
 * Revision 1.4  2005/08/04 08:34:32  deniel
 * memset management was badly made
 *
 * Revision 1.3  2005/08/03 13:23:43  deniel
 * Possible incoherency in CVS or in Emacs
 *
 * Revision 1.2  2005/08/03 13:13:59  deniel
 * memset to zero before building the filehandles
 *
 * Revision 1.1  2005/08/03 06:57:54  deniel
 * Added a libsupport for miscellaneous service functions
 *
 * Revision 1.4  2005/07/28 12:26:57  deniel
 * NFSv3 PROTOCOL Ok
 *
 * Revision 1.3  2005/07/26 07:39:15  deniel
 * Integration of NFSv2/NFSv3 In progress
 *
 * Revision 1.2  2005/07/21 09:18:42  deniel
 * Structure of the file handles was redefined
 *
 * Revision 1.1  2005/07/20 12:56:54  deniel
 * Reorganisation of the source files
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
#include <sys/types.h>
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"

/**
 *
 *  nfs4_FhandleToFSAL: converts a nfs4 file handle to a FSAL file handle.
 *
 * Validates and Converts a nfs4 file handle to a FSAL file handle.
 *
 * @param pfh4 [IN] pointer to the file handle to be converted
 * @param fh_desc [OUT] extracted handle descriptor
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs4_FhandleToFSAL(nfs_fh4 *pfh4,
                       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t *pcontext)
{
  fsal_status_t fsal_status;
  file_handle_v4_t *pfile_handle;

  if(nfs4_Is_Fh_Invalid(pfh4) != NFS4_OK)
    return 0;

  if(nfs4_Is_Fh_Pseudo(pfh4))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: PseudoFS handle");
      return 0;
    }

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v4_t *) (pfh4->nfs_fh4_val);

  /* Fill in the fs opaque part */
  fh_desc->start = (caddr_t)&pfile_handle->fsopaque;

  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_SIZEOF,
				  fh_desc);

  if(FSAL_IS_ERROR(fsal_status)) /* Currently can't happen */
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_ExpandHandle FSAL_DIGEST_SIZEOF failed");
      return 0;                   /* Corrupted FH */
    }

  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_NFSV4,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_ExpandHandle FSAL_DIGEST_NFSV4 failed");
      return 0;                   /* Corrupted (or stale) FH */
    }

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "FSAL Handle %s",
                     FSAL_HANDLE_STR_LEN,
                     fh_desc->start,
                     fh_desc->len);

  return 1;
}                               /* nfs4_FhandleToFSAL */

/**
 *
 *  nfs3_FhandleToFSAL: converts a nfs3 file handle to a FSAL file handle.
 *
 * Converts a nfs3 file handle to a FSAL file handle.
 *
 * @param pfh3 [IN] pointer to the file handle to be converted
 * @param pfsalhandle [OUT] pointer to the extracted FSAL handle
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs3_FhandleToFSAL(nfs_fh3 * pfh3,
		       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t * pcontext)
{
  fsal_status_t fsal_status;
  file_handle_v3_t *pfile_handle;

  /* validate the filehandle  */
  if(nfs3_Is_Fh_Invalid(pfh3) != NFS3_OK)
    return 0;                   /* Bad FH */

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

  /* Fill in the fs opaque part */
  fh_desc->start = (caddr_t) &pfile_handle->fsopaque;

  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_SIZEOF,
				  fh_desc);

  if(FSAL_IS_ERROR(fsal_status)) /* Currently can't happen */
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_ExpandHandle FSAL_DIGEST_SIZEOF failed");
      return 0;                   /* Corrupted FH */
    }

  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_NFSV3,
				  fh_desc);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_ExpandHandle FSAL_DIGEST_NFSV3 failed");
      return 0;                   /* Corrupted (or stale) FH */
    }

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "FSAL Handle %s",
                     FSAL_HANDLE_STR_LEN,
                     fh_desc->start,
                     fh_desc->len);

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "FSAL Handle %s",
                     FSAL_HANDLE_STR_LEN,
                     fh_desc->start,
                     fh_desc->len);

  return 1;
}                               /* nfs3_FhandleToFSAL */

/**
 *
 *  nfs2_FhandleToFSAL: converts a nfs2 file handle to a FSAL file handle.
 *
 * Converts a nfs2 file handle to a FSAL file handle.
 *
 * @param pfh2 [IN] pointer to the file handle to be converted
 * @param pfsalhandle [OUT] pointer to the extracted FSAL handle
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs2_FhandleToFSAL(fhandle2 * pfh2,
		       struct fsal_handle_desc *fh_desc,
                       fsal_op_context_t * pcontext)
{
  fsal_status_t fsal_status;
  file_handle_v2_t *pfile_handle;

  BUILD_BUG_ON(sizeof(struct alloc_file_handle_v2) != NFS2_FHSIZE);
  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v2_t *) pfh2;

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS3 Handle %s",
                     LEN_FH_STR,
                     pfh2,
                     sizeof(*pfh2));

  /* validate the filehandle  */
  if(pfile_handle->fhversion != GANESHA_FH_VERSION)
    return 0;                   /* Bad FH */

  /* Fill in the fs opaque part */
  fh_desc->start = (caddr_t) & (pfile_handle->fsopaque);
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_SIZEOF,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status)) /* Currently can't happen */
    return 0;                   /* Corrupted FH */
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_NFSV2,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status))
    return 0;                   /* Corrupted FH */

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "FSAL Handle %s",
                     FSAL_HANDLE_STR_LEN,
                     fh_desc->start,
                     fh_desc->len);

  return 1;
}                               /* nfs2_FhandleToFSAL */

/**
 *
 *  nfs4_FSALToFhandle: converts a FSAL file handle to a nfs4 file handle.
 *
 * Converts a nfs4 file handle to a FSAL file handle.
 *
 * @param pfh4 [OUT] pointer to the extracted file handle
 * @param pfsalhandle [IN] pointer to the FSAL handle to be converted
 * @param data [IN] pointer to NFSv4 compound data structure.
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs4_FSALToFhandle(nfs_fh4 *pfh4,
                       fsal_handle_t *pfsalhandle,
                       compound_data_t *data)
{
  fsal_status_t fsal_status;
  file_handle_v4_t *file_handle;
  struct fsal_handle_desc fh_desc;

  if(isFullDebug(COMPONENT_FILEHANDLE))
    {
      char                  printbuf[FSAL_HANDLE_STR_LEN];
      struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

      (void) display_FSAL_handle(&dspbuf, pfsalhandle);

      LogFullDebug(COMPONENT_FILEHANDLE,
                   "FSAL Handle=%s",
                   printbuf);
    }

  /* reset the buffer to be used as handle */
  pfh4->nfs_fh4_len = sizeof(struct alloc_file_handle_v4);
  memset(pfh4->nfs_fh4_val, 0, pfh4->nfs_fh4_len);
  file_handle = (file_handle_v4_t *)pfh4->nfs_fh4_val;

  /* Fill in the fs opaque part */
  fh_desc.start = (caddr_t) &file_handle->fsopaque;
  fh_desc.len = pfh4->nfs_fh4_len - offsetof(file_handle_v4_t, fsopaque);

  fsal_status =
      FSAL_DigestHandle(&data->pexport->FS_export_context,
                        FSAL_DIGEST_NFSV4,
                        pfsalhandle,
                        &fh_desc);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_DigestHandle FSAL_DIGEST_NFSV4 failed");
      return 0;                   /* Corrupted (or stale) FH */
    }

  file_handle->fhversion = GANESHA_FH_VERSION;
  file_handle->fs_len = fh_desc.len;   /* set the actual size */

  /* keep track of the export id */
  file_handle->exportid = data->pexport->id;

  /* if FH expires, set it there */
  if(nfs_param.nfsv4_param.fh_expire == TRUE)
    {
      LogFullDebug(COMPONENT_NFS_V4, "An expireable file handle was created.");
      file_handle->srvboot_time = ServerBootTime;
    }

  /* Set the len */
  pfh4->nfs_fh4_len = nfs4_sizeof_handle(file_handle); /* re-adjust to as built */

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS4 Handle %s",
                     LEN_FH_STR,
                     pfh4->nfs_fh4_val,
                     pfh4->nfs_fh4_len);

  return 1;
}                               /* nfs4_FSALToFhandle */

/**
 *
 *  nfs3_FSALToFhandle: converts a FSAL file handle to a nfs3 file handle.
 *
 * Converts a nfs3 file handle to a FSAL file handle.
 *
 * @param pfh3 [OUT] pointer to the extracted file handle 
 * @param pfsalhandle [IN] pointer to the FSAL handle to be converted
 * @param pexport [IN] pointer to the export list entry the FH belongs to
 *
 * @return 1 if successful, 0 otherwise
 *
 * FIXME: do we have to worry about buffer alignment and memcpy to 
 * compensate??
 */
int nfs3_FSALToFhandle(nfs_fh3 *pfh3,
                       fsal_handle_t *pfsalhandle,
                       exportlist_t *pexport)
{
  fsal_status_t fsal_status;
  file_handle_v3_t *file_handle;
  struct fsal_handle_desc fh_desc;

  if(isFullDebug(COMPONENT_FILEHANDLE))
    {
      char                  printbuf[FSAL_HANDLE_STR_LEN];
      struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

      (void) display_FSAL_handle(&dspbuf, pfsalhandle);

      LogFullDebug(COMPONENT_FILEHANDLE,
                   "FSAL Handle=%s",
                   printbuf);
    }

  /* reset the buffer to be used as handle */
  pfh3->data.data_len = sizeof(struct alloc_file_handle_v3);
  memset(pfh3->data.data_val, 0, pfh3->data.data_len);
  file_handle = (file_handle_v3_t *)pfh3->data.data_val;

  /* Fill in the fs opaque part */
  fh_desc.start = (caddr_t) &file_handle->fsopaque;
  fh_desc.len = pfh3->data.data_len - offsetof(file_handle_v3_t, fsopaque);
  fsal_status =
      FSAL_DigestHandle(&pexport->FS_export_context, FSAL_DIGEST_NFSV3, pfsalhandle,
                        &fh_desc);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogDebug(COMPONENT_FILEHANDLE,
               "FSAL_DigestHandle FSAL_DIGEST_NFSV3 failed");
      return 0;                   /* Corrupted (or stale) FH */
    }

  file_handle->fhversion = GANESHA_FH_VERSION;
  file_handle->fs_len = fh_desc.len;   /* set the actual size */
  /* keep track of the export id */
  file_handle->exportid = pexport->id;

  /* Set the len */
  pfh3->data.data_len = nfs3_sizeof_handle(file_handle); /* re-adjust to as built */

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS3 Handle %s",
                     LEN_FH_STR,
                     pfh3->data.data_val,
                     pfh3->data.data_len);

  return 1;
}                               /* nfs3_FSALToFhandle */

/**
 *
 *  nfs2_FSALToFhandle: converts a FSAL file handle to a nfs2 file handle.
 *
 * Converts a nfs2 file handle to a FSAL file handle.
 *
 * @param pfh2 [OUT] pointer to the extracted file handle 
 * @param pfsalhandle [IN] pointer to the FSAL handle to be converted
 * @param pfsalhandle [IN] pointer to the FSAL handle to be converted
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs2_FSALToFhandle(fhandle2 * pfh2, fsal_handle_t * pfsalhandle,
                       exportlist_t * pexport)
{
  fsal_status_t fsal_status;
  file_handle_v2_t *file_handle;
  struct fsal_handle_desc fh_desc;

  if(isFullDebug(COMPONENT_FILEHANDLE))
    {
      char                  printbuf[FSAL_HANDLE_STR_LEN];
      struct display_buffer dspbuf = {sizeof(printbuf), printbuf, printbuf};

      (void) display_FSAL_handle(&dspbuf, pfsalhandle);

      LogFullDebug(COMPONENT_FILEHANDLE,
                   "FSAL Handle=%s",
                   printbuf);
    }

  /* zero-ification of the buffer to be used as handle */
  memset(pfh2, 0, sizeof(struct alloc_file_handle_v2));
  file_handle = (file_handle_v2_t *)pfh2;

  /* Fill in the fs opaque part */
  fh_desc.start = (caddr_t) &file_handle->fsopaque;
  fh_desc.len = sizeof(file_handle->fsopaque);
  fsal_status =
      FSAL_DigestHandle(&pexport->FS_export_context, FSAL_DIGEST_NFSV2, pfsalhandle,
                        &fh_desc);
  if(FSAL_IS_ERROR(fsal_status))
   {
     if( fsal_status.major == ERR_FSAL_TOOSMALL )
      LogCrit( COMPONENT_FILEHANDLE, "NFSv2 file handle is too small to manage this fsal" ) ;
     else
      LogCrit( COMPONENT_FILEHANDLE, "FSAL_DigestHandle return (%u,%u) when called from %s", 
               fsal_status.major, fsal_status.minor, __func__ ) ;
    return 0;
   }

  file_handle->fhversion = GANESHA_FH_VERSION;
  /* keep track of the export id */
  file_handle->exportid = pexport->id;

  /* Set the last byte */
  file_handle->xattr_pos = 0;

/*   /\* Set the data *\/ */
/*   memcpy((caddr_t) pfh2, &file_handle, sizeof(file_handle_v2_t)); */

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS3 Handle %s",
                     LEN_FH_STR,
                     pfh2,
                     sizeof(*pfh2));

  return 1;
}                               /* nfs2_FSALToFhandle */

/**
 *
 * nfs3_FhandleToExportId
 *
 * This routine extracts the export id from the file handle NFSv3
 *
 * @param pfh3 [IN] file handle to manage.
 * 
 * @return the export id.
 *
 */
short nfs3_FhandleToExportId(nfs_fh3 * pfh3)
{
  file_handle_v3_t *pfile_handle;

  if(nfs3_Is_Fh_Invalid(pfh3) != NFS4_OK)
    return -1;                  /* Badly formed argument */

  pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

  return pfile_handle->exportid;
}                               /* nfs3_FhandleToExportId */

/**
 *
 * nfs2_FhandleToExportId
 *
 * This routine extracts the export id from the file handle NFSv2
 *
 * @param pfh2 [IN] file handle to manage.
 * 
 * @return the export id.
 *
 */
short nfs2_FhandleToExportId(fhandle2 * pfh2)
{
  file_handle_v2_t *pfile_handle;

  pfile_handle = (file_handle_v2_t *) (*pfh2);

  if(pfile_handle == NULL)
    return -1;                  /* Badly formed argument */

  return pfile_handle->exportid;
}                               /* nfs2_FhandleToExportId */

/**
 *    
 * nfs4_Is_Fh_Xattr
 * 
 * This routine is used to test is a fh refers to a Xattr related stuff
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return TRUE if in pseudo fh, FALSE otherwise 
 *
 */
int nfs3_Is_Fh_Xattr(nfs_fh3 * pfh)
{
  file_handle_v3_t *pfhandle3;

  if(pfh == NULL)
    return 0;

  pfhandle3 = (file_handle_v3_t *) (pfh->data.data_val);

  return (pfhandle3->xattr_pos != 0) ? 1 : 0;
}                               /* nfs4_Is_Fh_Xattr */

/**
 *
 * nfs4_Is_Fh_Empty
 *
 * This routine is used to test if a fh is empty (contains no data).
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return NFS4_OK if successfull, NFS4ERR_NOFILEHANDLE is fh is empty.  
 *
 */
int nfs4_Is_Fh_Empty(nfs_fh4 * pfh)
{
  if(pfh == NULL)
    {
      LogMajor(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: pfh=NULL");
      return NFS4ERR_NOFILEHANDLE;
    }

  if(pfh->nfs_fh4_len == 0)
    {
      LogInfo(COMPONENT_FILEHANDLE,
              "INVALID HANDLE: empty");
      return NFS4ERR_NOFILEHANDLE;
    }

  return NFS4_OK;
}                               /* nfs4_Is_Fh_Empty */

/**
 *  
 *  nfs4_Is_Fh_Xattr
 *
 *  This routine is used to test is a fh refers to a Xattr related stuff
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return TRUE if in pseudo fh, FALSE otherwise 
 *
 */
int nfs4_Is_Fh_Xattr(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfhandle4;

  if(pfh == NULL)
    return 0;

  pfhandle4 = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  return (pfhandle4->xattr_pos != 0) ? 1 : 0;
}                               /* nfs4_Is_Fh_Xattr */

/**
 *
 * nfs4_Is_Fh_Pseudo
 *
 * This routine is used to test if a fh refers to pseudo fs
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return TRUE if in pseudo fh, FALSE otherwise 
 *
 */
int nfs4_Is_Fh_Pseudo(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfhandle4;

  if(pfh == NULL || pfh->nfs_fh4_val == NULL)
    return 0;

  pfhandle4 = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  return pfhandle4->pseudofs_flag;
}                               /* nfs4_Is_Fh_Pseudo */

/**
 *
 * nfs4_Is_Fh_DSHandle
 *
 * This routine is used to test if a fh is a DS fh
 *
 * @param pfh [IN] file handle to test.
 *
 * @return TRUE if DS fh, FALSE otherwise
 *
 */
int nfs4_Is_Fh_DSHandle(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfhandle4;

  if(pfh == NULL)
    return 0;

  pfhandle4 = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  return pfhandle4->ds_flag;
}                               /* nfs4_Is_Fh_DSHandle */

/**
 *
 * nfs4_Is_Fh_Expired
 *
 * This routine is used to test if a fh is expired
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return NFS4_OK if successfull. All the FH are persistent for now. 
 *
 */
int nfs4_Is_Fh_Expired(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfilehandle4;

  if(pfh == NULL)
    {
      LogMajor(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: pfh=NULL");
      return NFS4ERR_BADHANDLE;
    }

  pfilehandle4 = (file_handle_v4_t *) pfh;

  if((nfs_param.nfsv4_param.fh_expire == TRUE)
     && (pfilehandle4->srvboot_time != (unsigned int)ServerBootTime))
    {
      if(nfs_param.nfsv4_param.returns_err_fh_expired == TRUE)
        {
          LogInfo(COMPONENT_FILEHANDLE,
                  "INVALID HANDLE: EXPIRED");
          return NFS4ERR_FHEXPIRED;
        }
    }

  return NFS4_OK;
}                               /* nfs4_Is_Fh_Expired */

/**
 *
 * nfs4_Is_Fh_Invalid
 *
 * This routine is used to test if a fh is invalid.
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return NFS4_OK if successfull. 
 *
 */
int nfs4_Is_Fh_Invalid(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfile_handle;

  BUILD_BUG_ON(sizeof(struct alloc_file_handle_v4) != NFS4_FHSIZE);

  if(pfh == NULL)
    {
      LogMajor(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: pfh==NULL");
      return NFS4ERR_BADHANDLE;
    }

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS4 Handle %s",
                     LEN_FH_STR,
                     pfh->nfs_fh4_val,
                     pfh->nfs_fh4_len);

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  /* validate the filehandle  */
  if(pfile_handle == NULL ||
     pfh->nfs_fh4_len == 0 ||
     pfile_handle->fhversion != GANESHA_FH_VERSION ||
     pfh->nfs_fh4_len < offsetof(struct file_handle_v4, fsopaque) ||
     pfh->nfs_fh4_len > sizeof(struct alloc_file_handle_v4) ||
     pfh->nfs_fh4_len != nfs4_sizeof_handle(pfile_handle) ||
     (pfile_handle->pseudofs_id != 0 &&
      pfile_handle->pseudofs_flag == FALSE))
    {
      if(isInfo(COMPONENT_FILEHANDLE))
        {
          if(pfile_handle == NULL)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: nfs_fh4_val=NULL");
            }
          else if(pfh->nfs_fh4_len == 0)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: zero length handle");
            }
          else if(pfile_handle->fhversion != GANESHA_FH_VERSION)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: not a Ganesha handle, fhversion=%d",
                      pfile_handle->fhversion);
            }
          else if(pfh->nfs_fh4_len < offsetof(struct file_handle_v4, fsopaque))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_len=%d is less than %d",
                      pfh->nfs_fh4_len,
                      (int) offsetof(struct file_handle_v4, fsopaque));
            }
          else if(pfh->nfs_fh4_len > sizeof(struct alloc_file_handle_v4))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_len=%d is greater than %d",
                      pfh->nfs_fh4_len,
                      (int) sizeof(struct alloc_file_handle_v4));
            }
          else if(pfh->nfs_fh4_len != nfs4_sizeof_handle(pfile_handle))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: nfs_fh4_len=%d, should be %d",
                      pfh->nfs_fh4_len, (int)nfs4_sizeof_handle(pfile_handle));
            }
          else
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: pseudofs_id=%d pseudofs_flag=%d",
                      pfile_handle->pseudofs_id,
                      pfile_handle->pseudofs_flag);
            }
        }
               
      return NFS4ERR_BADHANDLE;  /* Bad FH */
    }

  return NFS4_OK;
}


/**
 *
 * nfs3_Is_Fh_Invalid
 *
 * This routine is used to test if a fh is invalid.
 *
 * @param pfh [IN] file handle to test.
 * 
 * @return NFS4_OK if successfull. 
 *
 */
int nfs3_Is_Fh_Invalid(nfs_fh3 *pfh3)
{
  file_handle_v3_t *pfile_handle;

  BUILD_BUG_ON(sizeof(struct alloc_file_handle_v3) != NFS3_FHSIZE);

  if(pfh3 == NULL)
    {
      LogMajor(COMPONENT_FILEHANDLE,
               "INVALID HANDLE: pfh3==NULL");
      return NFS3ERR_BADHANDLE;
    }

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "NFS3 Handle %s",
                     LEN_FH_STR,
                     pfh3->data.data_val,
                     pfh3->data.data_len);

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

  /* validate the filehandle  */
  if(pfile_handle == NULL ||
     pfh3->data.data_len == 0 ||
     pfile_handle->fhversion != GANESHA_FH_VERSION ||
     pfh3->data.data_len < sizeof(file_handle_v3_t) ||
     pfh3->data.data_len > sizeof(struct alloc_file_handle_v3) ||
     pfh3->data.data_len != nfs3_sizeof_handle(pfile_handle))
    {
      if(isInfo(COMPONENT_FILEHANDLE))
        {
          if(pfile_handle == NULL)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_val=NULL");
            }
          else if(pfh3->data.data_len == 0)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: zero length handle");
            }
          else if(pfile_handle->fhversion != GANESHA_FH_VERSION)
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: not a Ganesha handle, fhversion=%d",
                      pfile_handle->fhversion);
            }
          else if(pfh3->data.data_len < sizeof(file_handle_v3_t))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_len=%d is less than %d",
                      pfh3->data.data_len,
                      (int) sizeof(file_handle_v3_t));
            }
          else if(pfh3->data.data_len > sizeof(struct alloc_file_handle_v3))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_len=%d is greater than %d",
                      pfh3->data.data_len,
                      (int) sizeof(struct alloc_file_handle_v3));
            }
          else if(pfh3->data.data_len != nfs3_sizeof_handle(pfile_handle))
            {
              LogInfo(COMPONENT_FILEHANDLE,
                      "INVALID HANDLE: data.data_len=%d, should be %d",
                      pfh3->data.data_len, (int)nfs3_sizeof_handle(pfile_handle));
            }
        }
               
      return NFS3ERR_BADHANDLE;  /* Bad FH */
    }

  return NFS3_OK;
}                               /* nfs4_Is_Fh_Invalid */

/**
 * 
 * nfs4_Is_Fh_Referral
 *
 * This routine is used to identify fh related to a pure referral
 *
 * @param pfh [IN] file handle to test.
 *
 * @return TRUE is fh is a referral, FALSE otherwise
 *
 */
int nfs4_Is_Fh_Referral(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfhandle4;

  if(pfh == NULL)
    return 0;

  pfhandle4 = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  /* Referrals are fh whose pseudofs_id is set without pseudofs_flag set */
  if(pfhandle4->refid > 0)
    {
      return TRUE;
    }

  return FALSE;
}                               /* nfs4_Is_Fh_Referral */
