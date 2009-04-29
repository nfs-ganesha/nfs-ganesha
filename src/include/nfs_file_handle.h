/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#endif

#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>  /* for having MAXHOSTNAMELEN */
/*
 * Structure of the filehandle 
 */

/* This must be exactly 32 bytes long, and aligned on 32 bits */
typedef struct file_handle_v2__ 
{
  unsigned int   checksum ;     /* FH checksum, for encryption support      len = 4 bytes  */
  unsigned short exportid ;     /* must be correlated to exportlist_t::id   len = 2 bytes  */
  char           fsopaque[25] ; /* persistent part of FSAL handle, opaque   len = 25 bytes */
  char           xattr_pos ;    /* Used for xattr management                len = 1  byte  */
} file_handle_v2_t ;

/* This is up to 64 bytes long, aligned on 32 bits */
typedef struct file_handle_v3__
{
  char           checksum[16] ; /* FH checksum, for encryption support      len = 16 bytes  */
  unsigned short exportid ;     /* must be correlated to exportlist_t::id   len = 2 bytes   */
  char           fsopaque[25] ; /* persistent part of FSAL handle, opaque   len = 25 bytes  */
  /* char           reserved[18] ; */ /* what's left...                     len = 18 bytes  */
  char           xattr_pos ;    /* Used for xattr management                len = 1  byte  */
} file_handle_v3_t ; 

/* This must be up to 64 bytes, aligned on 32 bits */
typedef struct file_handle_v4__
{
  char           checksum[16] ;  /* FH checksum, for encryption support      len = 16 bytes  */
  unsigned short exportid ;      /* must be correlated to exportlist_t::id   len = 2 bytes   */
  unsigned short refid ;         /* used for referral                        len = 2 bytes   */
  unsigned int   pseudofs_id ;   /* Id for the pseudo fs related to this fh  len = 4 bytes   */
  unsigned char  pseudofs_flag ; /* TRUE if FH is within pseudofs            len = 1 byte    */
  unsigned int   srvboot_time ;  /* 0 if FH won't expire                     len = 4 bytes   */
#ifdef _USE_PROXY
  char           fsopaque[93] ;  /* persistent part of FSAL handle */
#else
  char           fsopaque[22] ;
#endif
  char           xattr_pos ;     /*                                          len = 1 byte    */
} file_handle_v4_t ;

#define LEN_FH_STR 1024 
void nfs4_sprint_fhandle( nfs_fh4 * fh4p, char * outstr ) ;

/* File handle translation utility */
int nfs4_FhandleToFSAL( nfs_fh4 * pfh4, fsal_handle_t * pfsalhandle, fsal_op_context_t * pcontext ) ;
int nfs3_FhandleToFSAL( nfs_fh3 * pfh3, fsal_handle_t * pfsalhandle, fsal_op_context_t * pcontext ) ;
int nfs2_FhandleToFSAL( fhandle2 * pfh2, fsal_handle_t * pfsalhandle, fsal_op_context_t * pcontext ) ;

int nfs4_FSALToFhandle( nfs_fh4 * pfh4, fsal_handle_t * pfsalhandle, compound_data_t * data ) ;
int nfs3_FSALToFhandle( nfs_fh3 * pfh3, fsal_handle_t * pfsalhandle, exportlist_t * pexport ) ;
int nfs2_FSALToFhandle( fhandle2 * pfh2, fsal_handle_t * pfsalhandle, exportlist_t * pexport ) ;

/* Computation of file handle checksum */
unsigned int  nfs2_FhandleCheckSum( file_handle_v2_t * pfh ) ;
unsigned long long  nfs3_FhandleCheckSum( file_handle_v3_t * pfh ) ;
unsigned long long  nfs4_FhandleCheckSum( file_handle_v4_t * pfh ) ;

/* Extraction of export id from a file handle */
short nfs2_FhandleToExportId( fhandle2 * pfh2 )  ;
short nfs4_FhandleToExportId( nfs_fh4 * pfh4 ) ;
short nfs3_FhandleToExportId( nfs_fh3 * pfh3 ) ;

/* NFSv4 specific FH related functions */
int nfs4_Is_Fh_Empty( nfs_fh4 * pfh ) ;
int nfs4_Is_Fh_Xattr( nfs_fh4 * pfh ) ;
int nfs4_Is_Fh_Pseudo( nfs_fh4 * pfh ) ;
int nfs4_Is_Fh_Expired( nfs_fh4 * pfh ) ;
int nfs4_Is_Fh_Invalid(  nfs_fh4 * pfh ) ;
int nfs4_Is_Fh_Referral( nfs_fh4 * pfh ) ;

/* This one is used to detect Xattr related FH */
int nfs3_Is_Fh_Xattr( nfs_fh3 * pfh ) ;

/* File handle print function (;ostly use for debugging) */
void print_fhandle2( fhandle2 fh ) ;
void print_fhandle3( nfs_fh3 fh ) ;
void print_fhandle4( nfs_fh4 fh ) ;
void print_buff( char * buff, int len ) ;
void print_compound_fh( compound_data_t * data ) ;

void sprint_fhandle2( char * str, fhandle2 fh ) ;
void sprint_fhandle3( char * str, nfs_fh3 fh ) ;
void sprint_fhandle4( char * str, nfs_fh4 fh ) ;
void sprint_buff( char * str, char * buff, int len ) ;

#endif /* _NFS_FILE_HANDLE_H */


