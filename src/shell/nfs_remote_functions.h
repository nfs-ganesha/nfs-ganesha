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
 *
 * nfs_remote_functions.h : Prototypes for NFS protocol functions through RPCs. 
 *
 *
 */

#ifndef _NFS_REMOTE_FUNCTIONS_H
#define _NFS_REMOTE_FUNCTIONS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nfs_proto_functions.h"
#include "rpc.h"

/**
 * @defgroup MNTprocs    Mount protocol functions.
 * 
 * @{
 */
int mnt1_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt1_remote_Mnt(CLIENT * clnt /* IN  */ ,
                    nfs_arg_t * parg /* IN  */ ,
                    nfs_res_t * pres /* OUT */ );

int mnt1_remote_Dump(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt1_remote_Umnt(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt1_remote_UmntAll(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int mnt1_remote_Export(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int mnt3_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt3_remote_Mnt(CLIENT * clnt /* IN  */ ,
                    nfs_arg_t * parg /* IN  */ ,
                    nfs_res_t * pres /* OUT */ );

int mnt3_remote_Dump(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt3_remote_Umnt(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int mnt3_remote_UmntAll(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int mnt3_remote_Export(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

/* @}
 * -- End of MNT protocol functions. --
 */

/**
 * @defgroup NFSprocs    NFS protocols functions.
 * 
 * @{
 */

int nfs2_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs2_remote_Getattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs2_remote_Setattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs2_remote_Root(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs2_remote_Lookup(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs2_remote_Readlink(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ );

int nfs2_remote_Read(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs2_remote_Writecache(CLIENT * clnt /* IN  */ ,
                           nfs_arg_t * parg /* IN  */ ,
                           nfs_res_t * pres /* OUT */ );

int nfs2_remote_Write(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs2_remote_Create(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs2_remote_Remove(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs2_remote_Rename(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs2_remote_Link(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs2_remote_Symlink(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs2_remote_Mkdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs2_remote_Rmdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs2_remote_Readdir(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs2_remote_Fsstat(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs3_remote_Getattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs3_remote_Setattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs3_remote_Lookup(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Readlink(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ );

int nfs3_remote_Read(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs3_remote_Write(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs3_remote_Create(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Remove(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Rename(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Link(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs3_remote_Symlink(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs3_remote_Mkdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs3_remote_Rmdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs3_remote_Readdir(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ );

int nfs3_remote_Fsstat(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Access(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Readdirplus(CLIENT * clnt /* IN  */ ,
                            nfs_arg_t * parg /* IN  */ ,
                            nfs_res_t * pres /* OUT */ );

int nfs3_remote_Fsinfo(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Pathconf(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ );

int nfs3_remote_Commit(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ );

int nfs3_remote_Mknod(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs4_remote_COMPOUND(CLIENT * clnt /* IN  */ ,
                         COMPOUND4args * parg /* IN  */ ,
                         COMPOUND4res * pres /* OUT */ );

int nfs4_remote_Null(CLIENT * clnt /* IN  */ );

/* Free functions */
void mnt1_remote_Mnt_Free(nfs_res_t * pres);
void mnt1_remote_Dump_Free(nfs_res_t * pres);
void mnt1_remote_Export_Free(nfs_res_t * pres);
void mnt1_remote_Null_Free(nfs_res_t * pres);
void mnt1_remote_Umnt_Free(nfs_res_t * pres);
void mnt1_remote_UmntAll_Free(nfs_res_t * pres);

void mnt3_remote_Mnt_Free(nfs_res_t * pres);
void mnt3_remote_Dump_Free(nfs_res_t * pres);
void mnt3_remote_Export_Free(nfs_res_t * pres);
void mnt3_remote_Null_Free(nfs_res_t * pres);
void mnt3_remote_Umnt_Free(nfs_res_t * pres);
void mnt3_remote_UmntAll_Free(nfs_res_t * pres);

void nfs2_remote_Null_Free(nfs_res_t * resp);
void nfs2_remote_Getattr_Free(nfs_res_t * resp);
void nfs2_remote_Setattr_Free(nfs_res_t * resp);
void nfs2_remote_Lookup_Free(nfs_res_t * resp);
void nfs2_remote_Read_Free(nfs_res_t * resp);
void nfs2_remote_Write_Free(nfs_res_t * resp);
void nfs2_remote_Create_Free(nfs_res_t * resp);
void nfs2_remote_Mkdir_Free(nfs_res_t * resp);
void nfs2_remote_Symlink_Free(nfs_res_t * resp);
void nfs2_remote_Remove_Free(nfs_res_t * resp);
void nfs2_remote_Rmdir_Free(nfs_res_t * resp);
void nfs2_remote_Rename_Free(nfs_res_t * resp);
void nfs2_remote_Link_Free(nfs_res_t * resp);
void nfs2_remote_Fsstat_Free(nfs_res_t * resp);
void nfs2_remote_Root_Free(nfs_res_t * pres);
void nfs2_remote_Writecache_Free(nfs_res_t * pres);
void nfs2_remote_Readdir_Free(nfs_res_t * resp);
void nfs2_remote_Readlink_Free(nfs_res_t * resp);

void nfs3_remote_Null_Free(nfs_res_t * resp);
void nfs3_remote_Getattr_Free(nfs_res_t * resp);
void nfs3_remote_Setattr_Free(nfs_res_t * resp);
void nfs3_remote_Lookup_Free(nfs_res_t * resp);
void nfs3_remote_Access_Free(nfs_res_t * pres);
void nfs3_remote_Readlink_Free(nfs_res_t * resp);
void nfs3_remote_Write_Free(nfs_res_t * resp);
void nfs3_remote_Create_Free(nfs_res_t * resp);
void nfs3_remote_Mkdir_Free(nfs_res_t * resp);
void nfs3_remote_Symlink_Free(nfs_res_t * resp);
void nfs3_remote_Mknod_Free(nfs_res_t * pres);
void nfs3_remote_Remove_Free(nfs_res_t * resp);
void nfs3_remote_Rmdir_Free(nfs_res_t * resp);
void nfs3_remote_Rename_Free(nfs_res_t * resp);
void nfs3_remote_Link_Free(nfs_res_t * resp);
void nfs3_remote_Readdir_Free(nfs_res_t * resp);
void nfs3_remote_Readdirplus_Free(nfs_res_t * resp);
void nfs3_remote_Fsstat_Free(nfs_res_t * resp);
void nfs3_remote_Fsinfo_Free(nfs_res_t * pres);
void nfs3_remote_Pathconf_Free(nfs_res_t * pres);
void nfs3_remote_Commit_Free(nfs_res_t * pres);
void nfs3_remote_Read_Free(nfs_res_t * resp);

#endif
