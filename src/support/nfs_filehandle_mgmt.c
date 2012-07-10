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

  BUILD_BUG_ON(sizeof(struct alloc_file_handle_v4) != NFS4_FHSIZE);
  print_fhandle4(COMPONENT_FILEHANDLE, pfh4);

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v4_t *) (pfh4->nfs_fh4_val);

  /* validate the filehandle  */
  if(pfh4->nfs_fh4_len != nfs4_sizeof_handle(pfile_handle) ||
     pfile_handle->fhversion != GANESHA_FH_VERSION ||
     pfile_handle->pseudofs_id != 0 ||
     pfile_handle->pseudofs_flag != FALSE)
    return 0;                   /* Bad FH */

  /* Fill in the fs opaque part */
  fh_desc->start = (caddr_t)&pfile_handle->fsopaque;
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_SIZEOF,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status)) /* Currently can't happen */
    return 0;                   /* Corrupted FH */
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_NFSV4,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status))
    return 0;                   /* Corrupted (or stale) FH */
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

  BUILD_BUG_ON(sizeof(struct alloc_file_handle_v3) != NFS3_FHSIZE);
  print_fhandle3(COMPONENT_FILEHANDLE, pfh3);

  /* Cast the fh as a non opaque structure */
  pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

  /* validate the filehandle  */
  if(nfs3_Is_Fh_Invalid(pfh3) != NFS3_OK)
    return 0;                   /* Bad FH */

  /* Fill in the fs opaque part */
  fh_desc->start = (caddr_t) &pfile_handle->fsopaque;
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_SIZEOF,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status)) /* Currently can't happen */
    return 0;                   /* Corrupted FH */
  fsal_status = FSAL_ExpandHandle(FSAL_GET_EXP_CTX(pcontext),
				  FSAL_DIGEST_NFSV3,
				  fh_desc);
  if(FSAL_IS_ERROR(fsal_status))
    return 0;                   /* Corrupted FH */

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
  print_fhandle2(COMPONENT_FILEHANDLE, pfh2);

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

  print_buff(COMPONENT_FILEHANDLE, (char *)fh_desc->start, fh_desc->len);

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
    return 0;

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

  print_buff(COMPONENT_FILEHANDLE, (char *)pfsalhandle, sizeof(fsal_handle_t));

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
    return 0;

  file_handle->fhversion = GANESHA_FH_VERSION;
  file_handle->fs_len = fh_desc.len;   /* set the actual size */
  /* keep track of the export id */
  file_handle->exportid = pexport->id;

  /* Set the len */
  pfh3->data.data_len = nfs3_sizeof_handle(file_handle); /* re-adjust to as built */

  print_fhandle3(COMPONENT_FILEHANDLE, pfh3);

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

  print_buff(COMPONENT_FILEHANDLE, (char *)pfsalhandle, sizeof(fsal_handle_t));

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

  print_fhandle2(COMPONENT_FILEHANDLE, pfh2);

  return 1;
}                               /* nfs2_FSALToFhandle */

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
short nfs4_FhandleToExportId(nfs_fh4 * pfh4)
{
  file_handle_v4_t *pfile_handle = NULL;

  pfile_handle = (file_handle_v4_t *) (pfh4->nfs_fh4_val);

  if(pfile_handle == NULL)
    return -1;                  /* Badly formed arguments */

  return pfile_handle->exportid;
}                               /* nfs4_FhandleToExportId */

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

  pfile_handle = (file_handle_v3_t *) (pfh3->data.data_val);

  if(pfile_handle == NULL)
    return -1;                  /* Badly formed argument */

  print_buff(COMPONENT_FILEHANDLE, (char *)pfh3->data.data_val, pfh3->data.data_len);

  return pfile_handle->exportid;
}                               /* nfs3_FhandleToExportId */

#ifdef _USE_NLM
short nlm4_FhandleToExportId(netobj * pfh3)
{
  file_handle_v3_t *pfile_handle;

  if(pfh3->n_bytes == NULL || pfh3->n_len < sizeof(file_handle_v3_t))
    return -1;                  /* Badly formed argument */

  pfile_handle = (file_handle_v3_t *) (pfh3->n_bytes);

  print_buff(COMPONENT_FILEHANDLE, pfh3->n_bytes, pfh3->n_len);

  return pfile_handle->exportid;
}
#endif

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
    return NFS4ERR_NOFILEHANDLE;

  if(pfh->nfs_fh4_len == 0)
    return NFS4ERR_NOFILEHANDLE;

  return 0;
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

  if(pfh == NULL)
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
    return NFS4ERR_BADHANDLE;

  pfilehandle4 = (file_handle_v4_t *) pfh;

  if((nfs_param.nfsv4_param.fh_expire == TRUE)
     && (pfilehandle4->srvboot_time != (unsigned int)ServerBootTime))
    {
      if(nfs_param.nfsv4_param.returns_err_fh_expired == TRUE)
        return NFS4ERR_FHEXPIRED;
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
  file_handle_v4_t *pfilehandle4;

  if(pfh == NULL || pfh->nfs_fh4_val == NULL)
  {
    LogMajor(COMPONENT_FILEHANDLE,
	     "Invalid (NULL) File handle: pfh=0x%p",
	     pfh);
    return NFS4ERR_BADHANDLE;
  }

  pfilehandle4 = (file_handle_v4_t *) pfh->nfs_fh4_val;
  if(pfh->nfs_fh4_len > sizeof(struct alloc_file_handle_v4) ||
     pfh->nfs_fh4_len < nfs4_sizeof_handle(pfilehandle4) ||
     pfilehandle4->fhversion != GANESHA_FH_VERSION)
  {
    LogMajor(COMPONENT_FILEHANDLE,
	     "Invalid File handle: len=%d, version=%x",
	     pfh->nfs_fh4_len,
	     pfilehandle4->fhversion);
    return NFS4ERR_BADHANDLE;
  }

  return NFS4_OK;
}                               /* nfs4_Is_Fh_Invalid */

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
  file_handle_v3_t *pfilehandle3;

  if(pfh3 == NULL || pfh3->data.data_val == NULL)
  {
    LogMajor(COMPONENT_FILEHANDLE,
	     "Invalid (NULL) File handle: pfh3=0x%p",
	     pfh3);
    return NFS3ERR_BADHANDLE;
  }

  pfilehandle3 = (file_handle_v3_t *) pfh3->data.data_val;
  if(pfh3->data.data_len > sizeof(struct alloc_file_handle_v3) ||
     pfh3->data.data_len < nfs3_sizeof_handle(pfilehandle3) ||
     pfilehandle3->fhversion != GANESHA_FH_VERSION)
  {
    LogMajor(COMPONENT_FILEHANDLE,
	     "Invalid File handle: len=%d, version=%x",
	     pfh3->data.data_len,
	     pfilehandle3->fhversion);
    return NFS3ERR_BADHANDLE;
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

/**
 *
 * print_fhandle2
 *
 * This routine prints a NFSv2 file handle (for debugging purpose)
 *
 * @param fh [IN] file handle to print.
 * 
 * @return nothing (void function).
 *
 */
void print_fhandle2(log_components_t component, fhandle2 *fh)
{
  if(isFullDebug(component))
    {
      char str[LEN_FH_STR];

      sprint_fhandle2(str, fh);
      LogFullDebug(component, "%s", str);
    }
}                               /* print_fhandle2 */

void sprint_fhandle2(char *str, fhandle2 *fh)
{
  char *tmp = str +  sprintf(str, "File Handle V2: ");

  sprint_mem(tmp, (char *) fh, 32);
}                               /* sprint_fhandle2 */

/**
 *
 * print_fhandle3
 *
 * This routine prints a NFSv3 file handle (for debugging purpose)
 *
 * @param fh [IN] file handle to print.
 * 
 * @return nothing (void function).
 *
 */
void print_fhandle3(log_components_t component, nfs_fh3 *fh)
{
  if(isFullDebug(component))
    {
      char str[LEN_FH_STR];

      sprint_fhandle3(str, fh);
      LogFullDebug(component, "%s", str);
    }
}                               /* print_fhandle3 */

void sprint_fhandle3(char *str, nfs_fh3 *fh)
{
  char *tmp = str + sprintf(str, "File Handle V3: Len=%u ", fh->data.data_len);

  sprint_mem(tmp, fh->data.data_val, fh->data.data_len);
}                               /* sprint_fhandle3 */

/**
 *
 * print_fhandle4
 *
 * This routine prints a NFSv4 file handle (for debugging purpose)
 *
 * @param fh [IN] file handle to print.
 * 
 * @return nothing (void function).
 *
 */
void print_fhandle4(log_components_t component, nfs_fh4 *fh)
{
  if(isFullDebug(component))
    {
      char str[LEN_FH_STR];

      sprint_fhandle4(str, fh);
      LogFullDebug(component, "%s", str);
    }
}                               /* print_fhandle4 */

void sprint_fhandle4(char *str, nfs_fh4 *fh)
{
  char *tmp = str + sprintf(str, "File Handle V4: Len=%u ", fh->nfs_fh4_len);

  sprint_mem(tmp, fh->nfs_fh4_val, fh->nfs_fh4_len);
}                               /* sprint_fhandle4 */

/**
 *
 * print_fhandle_nlm
 *
 * This routine prints a NFSv3 file handle (for debugging purpose)
 *
 * @param fh [IN] file handle to print.
 * 
 * @return nothing (void function).
 *
 */
void print_fhandle_nlm(log_components_t component, netobj *fh)
{
  if(isFullDebug(component))
    {
      char str[LEN_FH_STR];

      sprint_fhandle_nlm(str, fh);
      LogFullDebug(component, "%s", str);
    }
}                               /* print_fhandle_nlm */

void sprint_fhandle_nlm(char *str, netobj *fh)
{
  char *tmp = str + sprintf(str, "File Handle V3: Len=%u ", fh->n_len);

  sprint_mem(tmp, fh->n_bytes, fh->n_len);
}                               /* sprint_fhandle_nlm */

/**
 *
 * print_buff
 *
 * This routine prints the content of a buffer.
 *
 * @param buff [IN] buffer to print.
 * @param len  [IN] length of the buffer.
 * 
 * @return nothing (void function).
 *
 */
void print_buff(log_components_t component, char *buff, int len)
{
  if(isFullDebug(component))
    {
      char str[1024];

      sprint_buff(str, buff, len);
      LogFullDebug(component, "%s", str);
    }
}                               /* print_buff */

void sprint_buff(char *str, char *buff, int len)
{
  char *tmp = str + sprintf(str, "  Len=%u Buff=%p Val: ", len, buff);

  sprint_mem(tmp, buff, len);
}                               /* sprint_buff */

void sprint_mem(char *str, char *buff, int len)
{
  int i;

  if(buff == NULL)
    sprintf(str, "<null>");
  else for(i = 0; i < len; i++)
    sprintf(str + i * 2, "%02x", (unsigned char)buff[i]);
}

/**
 *
 * print_compound_fh
 *
 * This routine prints all the file handle within a compoud request's data structure.
 * 
 * @param data [IN] compound's data to manage.
 *
 * @return nothing (void function).
 *
 */
void LogCompoundFH(compound_data_t * data)
{
  if(isFullDebug(COMPONENT_FILEHANDLE))
    {
      char str[LEN_FH_STR];
      
      sprint_fhandle4(str, &data->currentFH);
      LogFullDebug(COMPONENT_FILEHANDLE, "Current FH  %s", str);
      
      sprint_fhandle4(str, &data->savedFH);
      LogFullDebug(COMPONENT_FILEHANDLE, "Saved FH    %s", str);
      
      sprint_fhandle4(str, &data->publicFH);
      LogFullDebug(COMPONENT_FILEHANDLE, "Public FH   %s", str);
      
      sprint_fhandle4(str, &data->rootFH);
      LogFullDebug(COMPONENT_FILEHANDLE, "Root FH     %s", str);
    }
}                               /* print_compoud_fh */

/**
 * nfs4_sprint_fhandle : converts a file handle v4 to a string.
 *
 * Converts a file handle v4 to a string. This will be used mostly for debugging purpose. 
 * 
 * @param fh4p [OUT]   pointer to the file handle to be converted to a string.
 * @param data [INOUT] pointer to the char * resulting from the operation.
 * 
 * @return nothing (void function).
 *
 */

void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr)
{
  char *tmp = outstr + sprintf(outstr, "File Handle V4: Len=%u ", fh4p->nfs_fh4_len);

  sprint_mem(tmp, (char *)fh4p->nfs_fh4_val, fh4p->nfs_fh4_len);
}                               /* nfs4_sprint_fhandle */
