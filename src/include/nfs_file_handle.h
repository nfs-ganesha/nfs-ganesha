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
#include "log_macros.h"
#include "nfs23.h"
#ifdef _USE_NLM
#include "nlm4.h"
#endif

/*
 * Structure of the filehandle 
 */

/* This must be exactly 32 bytes long, and aligned on 32 bits */
typedef struct file_handle_v2__
{
  unsigned short exportid;      /* must be correlated to exportlist_t::id   len = 2 bytes  */
  char fsopaque[29];            /* persistent part of FSAL handle, opaque   len = 25 bytes */
  char xattr_pos;               /* Used for xattr management                len = 1  byte  */
} file_handle_v2_t;

/* This is up to 64 bytes long, aligned on 32 bits */
typedef struct file_handle_v3__
{
  unsigned short exportid;      /* must be correlated to exportlist_t::id   len = 2 bytes   */
  char fsopaque[61];            /* persistent part of FSAL handle, opaque   len = 41 bytes  */
  char xattr_pos;               /* Used for xattr management                len = 1  byte  */
} file_handle_v3_t;



/* This must be up to 128 bytes, aligned on 32 bits */
typedef struct file_handle_v4__
{
  unsigned short pseudofs_id;   /* Id for the pseudo fs related to this fh  len = 2 bytes   */
  unsigned char ds_flag;        /* TRUE if FH is a 'DS file handle'         len = 1 byte    */
  unsigned char pseudofs_flag;  /* TRUE if FH is within pseudofs            len = 1 byte    */
  unsigned int exportid;        /* must be correlated to exportlist_t::id   len = 4 bytes   */
  unsigned short refid;         /* used for referral                        len = 2 bytes   */
  unsigned int srvboot_time;    /* 0 if FH won't expire                     len = 4 bytes   */
#ifdef _USE_PROXY
  char fsopaque[108];            /* persistent part of FSAL handle */
#else
  char fsopaque[61];            /* persistent part of FSAL handle */
#endif /* _USE_FSAL_PROXY */
  char xattr_pos;               /*                                          len = 1 byte    */
} file_handle_v4_t;

#define LEN_FH_STR 1024

/* File handle translation utility */
int nfs4_FhandleToFSAL(nfs_fh4 * pfh4, fsal_handle_t * pfsalhandle,
                       fsal_op_context_t * pcontext);
int nfs3_FhandleToFSAL(nfs_fh3 * pfh3, fsal_handle_t * pfsalhandle,
                       fsal_op_context_t * pcontext);
int nfs2_FhandleToFSAL(fhandle2 * pfh2, fsal_handle_t * pfsalhandle,
                       fsal_op_context_t * pcontext);

int nfs4_FSALToFhandle(nfs_fh4 * pfh4, fsal_handle_t * pfsalhandle,
                       compound_data_t * data);
int nfs3_FSALToFhandle(nfs_fh3 * pfh3, fsal_handle_t * pfsalhandle,
                       exportlist_t * pexport);
int nfs2_FSALToFhandle(fhandle2 * pfh2, fsal_handle_t * pfsalhandle,
                       exportlist_t * pexport);

/* Extraction of export id from a file handle */
short nfs2_FhandleToExportId(fhandle2 * pfh2);
short nfs4_FhandleToExportId(nfs_fh4 * pfh4);
short nfs3_FhandleToExportId(nfs_fh3 * pfh3);

#ifdef _USE_NLM
short nlm4_FhandleToExportId(netobj * pfh3);
#endif

/* NFSv4 specific FH related functions */
int nfs4_Is_Fh_Empty(nfs_fh4 * pfh);
int nfs4_Is_Fh_Xattr(nfs_fh4 * pfh);
int nfs4_Is_Fh_Pseudo(nfs_fh4 * pfh);
int nfs4_Is_Fh_Expired(nfs_fh4 * pfh);
int nfs4_Is_Fh_Invalid(nfs_fh4 * pfh);
int nfs4_Is_Fh_Referral(nfs_fh4 * pfh);
int nfs4_Is_Fh_DSHandle(nfs_fh4 * pfh);

/* This one is used to detect Xattr related FH */
int nfs3_Is_Fh_Xattr(nfs_fh3 * pfh);

/* File handle print function (;ostly use for debugging) */
void print_fhandle2(log_components_t component, fhandle2 *fh);
void print_fhandle3(log_components_t component, nfs_fh3 *fh);
void print_fhandle4(log_components_t component, nfs_fh4 *fh);
void print_fhandle_nlm(log_components_t component, netobj *fh);
void print_buff(log_components_t component, char *buff, int len);
void LogCompoundFH(compound_data_t * data);

void sprint_fhandle2(char *str, fhandle2 *fh);
void sprint_fhandle3(char *str, nfs_fh3 *fh);
void sprint_fhandle4(char *str, nfs_fh4 *fh);
void sprint_fhandle_nlm(char *str, netobj *fh);
void sprint_buff(char *str, char *buff, int len);
void sprint_mem(char *str, char *buff, int len);

void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr) ;

#define LogHandleNFS4( label, fh4p )                        \
  do {                                                      \
    if(isFullDebug(COMPONENT_NFS_V4))                       \
      {                                                     \
        char str[LEN_FH_STR];                               \
        sprint_fhandle4(str, fh4p);                         \
        LogFullDebug(COMPONENT_NFS_V4, "%s%s", label, str); \
      }                                                     \
  } while (0)

#endif                          /* _NFS_FILE_HANDLE_H */
