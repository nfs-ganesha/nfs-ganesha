/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    nfs4_xattr.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   Routines used for managing the NFS4 xattrs
t*
 * nfs4_xattr.c: Routines used for managing the NFS4 xattrs
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
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "cache_content.h"

int nfs4_XattrToFattr(fattr4 * Fattr,
                      compound_data_t * data, nfs_fh4 * objFH, bitmap4 * Bitmap)
{
  fattr4_type file_type;
  fattr4_link_support link_support;
  fattr4_symlink_support symlink_support;
  fattr4_fh_expire_type expire_type;
  fattr4_named_attr named_attr;
  fattr4_unique_handles unique_handles;
  fattr4_archive archive;
  fattr4_cansettime cansettime;
  fattr4_case_insensitive case_insensitive;
  fattr4_case_preserving case_preserving;
  fattr4_chown_restricted chown_restricted;
  fattr4_hidden hidden;
  fattr4_mode file_mode;
  fattr4_no_trunc no_trunc;
  fattr4_numlinks file_numlinks;
  fattr4_rawdev rawdev;
  fattr4_system system;
  fattr4_size file_size;
  fattr4_space_used file_space_used;
  fattr4_fsid fsid;
  fattr4_time_access time_access;
  fattr4_time_modify time_modify;
  fattr4_time_metadata time_metadata;
  fattr4_time_delta time_delta;
  fattr4_time_backup time_backup;
  fattr4_time_create time_create;
  fattr4_change file_change;
  fattr4_fileid file_id;
  fattr4_owner file_owner;
  fattr4_owner_group file_owner_group;
  fattr4_space_avail space_avail;
  fattr4_space_free space_free;
  fattr4_space_total space_total;
  fattr4_files_avail files_avail;
  fattr4_files_free files_free;
  fattr4_files_total files_total;
  fattr4_lease_time lease_time;
  fattr4_maxfilesize max_filesize;
  fattr4_supported_attrs supported_attrs;
  fattr4_maxread maxread;
  fattr4_maxwrite maxwrite;
  fattr4_maxname maxname;
  fattr4_maxlink maxlink;
  fattr4_homogeneous homogeneous;
  fattr4_acl acl;
  fattr4_mimetype mimetype;
  fattr4_aclsupport aclsupport;
  fattr4_fs_locations fs_locations;
  fattr4_quota_avail_hard quota_avail_hard;
  fattr4_quota_avail_soft quota_avail_soft;
  fattr4_quota_used quota_used;
  fattr4_mounted_on_fileid mounted_on_fileid;
  fattr4_rdattr_error rdattr_error;
  file_handle_v4_t *pfile_handle = NULL;

  fsal_attrib_list_t fsalattr;
  fsal_handle_t mounted_on_fsal_handle;
  fsal_status_t fsal_status;

  u_int fhandle_len = 0;
  uint32_t supported_attrs_len = 0;
  uint32_t supported_attrs_val = 0;
  unsigned int LastOffset;
  unsigned int len = 0, off = 0;        /* Use for XDR alignment */
  int op_attr_success = 0;
  unsigned int c = 0;
  unsigned int i = 0;
  unsigned int j = 0;
  unsigned int k = 0;
  unsigned int attrmasklen = 0;
  unsigned int attribute_to_set = 0;

  unsigned int attrvalslist_supported[FATTR4_MOUNTED_ON_FILEID];
  unsigned int attrmasklist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  unsigned int attrvalslist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  char attrvalsBuffer[ATTRVALS_BUFFLEN];

  char __attribute__ ((__unused__)) funcname[] = "nfs4_XattrToFattr";

  pfile_handle = (file_handle_v4_t *) (objFH->nfs_fh4_val);

  /* memset to make sure the arrays are initiated to 0 */
  memset(attrvalsBuffer, 0, NFS4_ATTRVALS_BUFFLEN);
  memset(&attrmasklist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(unsigned int));
  memset(&attrvalslist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(unsigned int));

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(Bitmap, &attrmasklen, attrmasklist);

  /* Once the bitmap has been converted to a list of attribute, manage each attribute */
  Fattr->attr_vals.attrlist4_len = 0;
  LastOffset = 0;
  j = 0;

#ifdef _DEBUG_NFS_V4_XATTR
  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs, NIV_FULL_DEBUG,
                    "Asked Attributes (Pseudo): Bitmap = (len=%d, val[0]=%d, val[1]=%d), %d item in list",
                    Bitmap->bitmap4_len, Bitmap->bitmap4_val[0], Bitmap->bitmap4_val[1],
                    attrmasklen);
#endif

  for (i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

      DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs,
                        NIV_FULL_DEBUG,
                        "Flag for Operation (Pseudo) = %d|%d is ON,  name  = %s  reply_size = %d",
                        attrmasklist[i], fattr4tab[attribute_to_set].val,
                        fattr4tab[attribute_to_set].name,
                        fattr4tab[attribute_to_set].size_fattr4);

      op_attr_success = 0;

      /* compute the new size for the fattr4 reply */
      /* This space is to be filled below in the big switch/case statement */

      switch (attribute_to_set)
        {
        case FATTR4_SUPPORTED_ATTRS:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SUPPORTED_ATTRS\n");
#endif
          /* The supported attributes have field ',supported' set in tab fattr4tab, I will proceed in 2 pass 
           * 1st: compute the number of supported attributes
           * 2nd: allocate the replyed bitmap and fill it
           *
           * I do not set a #define to keep the number of supported attributes because I want this parameter
           * to be a consequence of fattr4tab and avoid incoherency */

          /* How many supported attributes ? Compute the result in variable named c and set attrvalslist_supported  */
          c = 0;
          for (k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_MOUNTED_ON_FILEID; k++)
            {
              if (fattr4tab[k].supported)
                {
                  attrvalslist_supported[c++] = k;
                }
            }

          /* Let set the reply bitmap */
          /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
          if ((supported_attrs.bitmap4_val =
               (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 2 * sizeof(uint32_t));
          nfs4_list_to_bitmap4(&supported_attrs, &c, attrvalslist_supported);

#ifdef _DEBUG_NFS_V4_XATTR
          DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs,
                            NIV_FULL_DEBUG,
                            "Fattr (pseudo) supported_attrs(len)=%u -> %u|%u",
                            supported_attrs.bitmap4_len, supported_attrs.bitmap4_val[0],
                            supported_attrs.bitmap4_val[1]);
#endif

          /* This kind of operation is always a success */
          op_attr_success = 1;

          /* we store the index */
          supported_attrs_len = htonl(supported_attrs.bitmap4_len);
          memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_len,
                 sizeof(uint32_t));
          LastOffset += sizeof(uint32_t);

          /* And then the data */
          for (k = 0; k < supported_attrs.bitmap4_len; k++)
            {
              supported_attrs_val = htonl(supported_attrs.bitmap4_val[k]);
              memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_val,
                     sizeof(uint32_t));
              LastOffset += sizeof(uint32_t);
            }
          break;

        case FATTR4_TYPE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TYPE\n");
#endif
          op_attr_success = 1;

          if (pfile_handle->xattr_pos == 1)
            file_type = htonl(NF4DIR);  /* There are only directories in the pseudo fs */
          else
            file_type = htonl(NF4REG);

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_type, sizeof(fattr4_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          break;

        case FATTR4_FH_EXPIRE_TYPE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FH_EXPIRE_TYPE\n");
#endif
          /* For the moment, we handle only the persistent filehandle */
          /* expire_type = htonl( FH4_VOLATILE_ANY ) ; */
          expire_type = htonl(FH4_PERSISTENT);
          memcpy((char *)(attrvalsBuffer + LastOffset), &expire_type,
                 sizeof(expire_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHANGE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_CHANGE\n");
#endif
          /* Use boot time as time value for every pseudo fs object */
          memset(&file_change, 0, sizeof(changeid4));
          file_change = nfs_htonl64((changeid4) time(NULL));

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_change,
                 sizeof(fattr4_change));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SIZE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SIZE\n");
#endif
          file_size = nfs_htonl64((fattr4_size) DEV_BSIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_size, sizeof(fattr4_size));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LINK_SUPPORT:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_LINK_SUPPORT\n");
#endif
          /* HPSS NameSpace support hard link */
          link_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &link_support,
                 sizeof(fattr4_link_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYMLINK_SUPPORT:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SYMLINK_SUPPORT\n");
#endif
          /* HPSS NameSpace support symbolic link */
          symlink_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &symlink_support,
                 sizeof(fattr4_symlink_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NAMED_ATTR:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_NAMED_ATTR\n");
#endif
          /* For this version of the binary, named attributes is not supported */
          named_attr = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &named_attr,
                 sizeof(fattr4_named_attr));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FSID:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FSID\n");
#endif
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_UNIQUE_HANDLES:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_UNIQUE_HANDLES\n");
#endif
          /* Filehandles are unique */
          unique_handles = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &unique_handles,
                 sizeof(fattr4_unique_handles));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LEASE_TIME:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_LEASE_TIME\n");
#endif
          lease_time = htonl(NFS4_LEASE_LIFETIME);
          memcpy((char *)(attrvalsBuffer + LastOffset), &lease_time,
                 sizeof(fattr4_lease_time));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RDATTR_ERROR:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_RDATTR_ERROR\n");
#endif
          rdattr_error = htonl(NFS4_OK);        /* By default, READDIR call may use a different value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &rdattr_error,
                 sizeof(fattr4_rdattr_error));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACL:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_ACL\n");
#endif
          acl.fattr4_acl_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &acl, sizeof(fattr4_acl));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACLSUPPORT:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_ACL_SUPPORT\n");
#endif
          aclsupport = htonl(ACL4_SUPPORT_DENY_ACL);    /* temporary, wanting for houston to give me information to implemente ACL's support */
          memcpy((char *)(attrvalsBuffer + LastOffset), &aclsupport,
                 sizeof(fattr4_aclsupport));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ARCHIVE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_ARCHIVE\n");
#endif
          /* Archive flag is not supported */
          archive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &archive, sizeof(fattr4_archive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CANSETTIME:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_CANSETTIME\n");
#endif
          /* The time can be set on files */
          cansettime = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &cansettime,
                 sizeof(fattr4_cansettime));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_INSENSITIVE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_CASE_INSENSITIVE\n");
#endif
          /* pseudofs is not case INSENSITIVE... it is Read-Only */
          case_insensitive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_insensitive,
                 sizeof(fattr4_case_insensitive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_PRESERVING:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_PRESERVING\n");
#endif
          /* pseudofs is case preserving... it is Read-Only */
          case_preserving = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_preserving,
                 sizeof(fattr4_case_preserving));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHOWN_RESTRICTED:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_CHOWN_RESTRICTED\n");
#endif
          /* chown is restricted to root, but in fact no chown will be done on pseudofs */
          chown_restricted = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &chown_restricted,
                 sizeof(fattr4_chown_restricted));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILEHANDLE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FILEHANDLE\n");
#endif
          /* Return the file handle */
          fhandle_len = htonl(objFH->nfs_fh4_len);

          memcpy((char *)(attrvalsBuffer + LastOffset), &fhandle_len, sizeof(u_int));
          LastOffset += sizeof(u_int);

          memcpy((char *)(attrvalsBuffer + LastOffset),
                 objFH->nfs_fh4_val, objFH->nfs_fh4_len);
          LastOffset += objFH->nfs_fh4_len;

          /* XDR's special stuff for 32-bit alignment */
          len = objFH->nfs_fh4_len;
          off = 0;
          while ((len + off) % 4 != 0)
            {
              char c = '\0';

              off += 1;
              memset((char *)(attrvalsBuffer + LastOffset), c, 1);
              LastOffset += 1;
            }

          op_attr_success = 1;
          break;

        case FATTR4_FILEID:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FILEID\n");
#endif
          /* The analog to the inode number. RFC3530 says "a number uniquely identifying the file within the filesystem" 
           * In the case of a pseudofs entry, the entry's unique id is used */
          cache_inode_get_attributes(data->current_entry, &fsalattr);

#ifndef _XATTR_D_USE_SAME_INUM  /* I wrapped off this part of the code... Not sure it would be useful */
          file_id = nfs_htonl64(~(fsalattr.fileid));

          if (pfile_handle->xattr_pos == 1)
            file_id = nfs_htonl64(~(fsalattr.fileid));
          else
            file_mode = nfs_htonl64(~(fsalattr.fileid)) - pfile_handle->xattr_pos + 1;
#else
          file_id = nfs_htonl64(fsalattr.fileid);
#endif
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_AVAIL:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FILES_AVAIL\n");
#endif
          files_avail = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_avail,
                 sizeof(fattr4_files_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_FREE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FILES_FREE\n");
#endif
          files_free = nfs_htonl64((fattr4_files_avail) 512);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_free,
                 sizeof(fattr4_files_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_TOTAL:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FILES_TOTAL\n");
#endif
          files_total = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_total,
                 sizeof(fattr4_files_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FS_LOCATIONS:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_FS_LOCATIONS\n");
#endif
          fs_locations.fs_root.pathname4_len = 0;
          fs_locations.locations.locations_len = 0;     /* No FS_LOCATIONS no now */
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HIDDEN:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_HIDDEN\n");
#endif
          /* There are no hidden file in pseudofs */
          hidden = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &hidden, sizeof(fattr4_hidden));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HOMOGENEOUS:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_HOMOGENEOUS\n");
#endif
          /* Unix semantic is homogeneous (all objects have the same kind of attributes) */
          homogeneous = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &homogeneous,
                 sizeof(fattr4_homogeneous));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXFILESIZE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MAXFILESIZE\n");
#endif
          max_filesize = nfs_htonl64((fattr4_maxfilesize) FSINFO_MAX_FILESIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &max_filesize,
                 sizeof(fattr4_maxfilesize));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXLINK:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MAXLINK\n");
#endif
          maxlink = htonl(MAX_HARD_LINK_VALUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxlink, sizeof(fattr4_maxlink));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXNAME:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MAXNAME\n");
#endif
          maxname = htonl((fattr4_maxname) MAXNAMLEN);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxname, sizeof(fattr4_maxname));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXREAD:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MAXREAD\n");
#endif
          maxread = nfs_htonl64((fattr4_maxread) NFS4_PSEUDOFS_MAX_READ_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxread, sizeof(fattr4_maxread));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXWRITE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MAXWRITE\n");
#endif
          maxwrite = nfs_htonl64((fattr4_maxwrite) NFS4_PSEUDOFS_MAX_WRITE_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxwrite,
                 sizeof(fattr4_maxwrite));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MIMETYPE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MIMETYPE\n");
#endif
          mimetype.utf8string_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &mimetype,
                 sizeof(fattr4_mimetype));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;  /* No supported for the moment */
          break;

        case FATTR4_MODE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MODE\n");
#endif
          if (pfile_handle->xattr_pos == 1)
            file_mode = htonl(0555);    /* Every pseudo fs object is dr-xr-xr-x */
          else
            file_mode = htonl(0644);    /* -rw-r--r-- */

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_mode, sizeof(fattr4_mode));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NO_TRUNC:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_NO_TRUNC\n");
#endif
          /* File's names are not truncated, an error is returned is name is too long */
          no_trunc = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &no_trunc,
                 sizeof(fattr4_no_trunc));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NUMLINKS:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_NUMLINKS\n");
#endif
          /* Reply the number of links found in vattr structure */
          file_numlinks = htonl((fattr4_numlinks) 1);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_numlinks,
                 sizeof(fattr4_numlinks));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_OWNER:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_OWNER\n");
#endif
          /* Return the uid as a human readable utf8 string */
          if (uid2utf8(NFS4_ROOT_UID, &file_owner) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if (file_owner.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner.utf8string_len % 4;

              utf8len = htonl(file_owner.utf8string_len + deltalen);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner.utf8string_val, file_owner.utf8string_len);
              LastOffset += file_owner.utf8string_len;

              /* Free what was allocated by uid2utf8 */
              Mem_Free((char *)file_owner.utf8string_val);

              /* Pad with zero to keep xdr alignement */
              if (deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;
            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_OWNER_GROUP:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_OWNER_GROUP\n");
#endif
          /* Return the uid as a human readable utf8 string */
          if (gid2utf8(2, &file_owner_group) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if (file_owner_group.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner_group.utf8string_len % 4;

              utf8len = htonl(file_owner_group.utf8string_len + deltalen);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner_group.utf8string_val, file_owner_group.utf8string_len);
              LastOffset += file_owner_group.utf8string_len;

              /* Free what was used for utf8 conversion */
              Mem_Free((char *)file_owner_group.utf8string_val);

              /* Pad with zero to keep xdr alignement */
              if (deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;
            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_QUOTA_AVAIL_HARD:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_QUOTA_AVAIL_HARD\n");
#endif
          quota_avail_hard = nfs_htonl64((fattr4_quota_avail_hard) NFS_V4_MAX_QUOTA_HARD);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_hard,
                 sizeof(fattr4_quota_avail_hard));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_AVAIL_SOFT:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_QUOTA_AVAIL_SOFT\n");
#endif
          quota_avail_soft = nfs_htonl64((fattr4_quota_avail_soft) NFS_V4_MAX_QUOTA_SOFT);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_soft,
                 sizeof(fattr4_quota_avail_soft));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_USED:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_QUOTA_AVAIL_USED\n");
#endif
          quota_used = nfs_htonl64((fattr4_quota_used) NFS_V4_MAX_QUOTA);
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_used,
                 sizeof(fattr4_quota_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RAWDEV:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_RAWDEV\n");
#endif
          /* Not usefull, there are no special block or character file in HPSS */
          /* since FATTR4_TYPE will never be NFS4BLK or NFS4CHR, this value should not be used by the client */
          rawdev.specdata1 = htonl(0);
          rawdev.specdata2 = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &rawdev, sizeof(fattr4_rawdev));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_AVAIL:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SPACE_AVAIL\n");
#endif
          space_avail = nfs_htonl64(512000LL);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_avail,
                 sizeof(fattr4_space_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_FREE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SPACE_FREE\n");
#endif
          space_free = nfs_htonl64(512000LL);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_free,
                 sizeof(fattr4_space_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_TOTAL:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SPACE_TOTAL\n");
#endif
          space_total = nfs_htonl64(1024000LL); /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_total,
                 sizeof(fattr4_space_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_USED:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SPACE_USED\n");
#endif
          /* the number of bytes on the filesystem used by the object, which is slightly different 
           * from the file's size (there can be hole in the file) */
          file_space_used = nfs_htonl64((fattr4_space_used) DEV_BSIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_space_used,
                 sizeof(fattr4_space_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYSTEM:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_SYSTEM\n");
#endif
          /* This is not a windows system File-System with respect to the regarding API */
          system = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &system, sizeof(fattr4_system));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_ACCESS\n");
#endif
          /* This will contain the object's time os last access, the 'atime' in the Unix semantic */
          memset(&(time_access.seconds), 0, sizeof(int64_t));
          time_access.seconds = nfs_htonl64((int64_t) time(NULL));
          time_access.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_access,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS_SET:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_ACCESS_SET\n");
#endif
          /* To be used with NFS4_OP_SETATTR only */
          op_attr_success = 0;
          break;

        case FATTR4_TIME_BACKUP:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_BACKUP\n");
#endif
          /* No time backup, return unix's beginning of time */
          time_backup.seconds = nfs_htonl64(0LL);
          time_backup.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_backup,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_CREATE:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_CREATE\n");
#endif
          /* No time create, return unix's beginning of time */
          time_create.seconds = nfs_htonl64(0LL);
          time_create.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_create,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_DELTA:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_DELTA\n");
#endif

          /* According to RFC3530, this is "the smallest usefull server time granularity", I set this to 1s */
          time_delta.seconds = nfs_htonl64(1LL);
          time_delta.nseconds = 0;
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_delta,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_METADATA:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_METADATA\n");
#endif

          /* The time for the last metadata operation, the ctime in the unix's semantic */
          memset(&(time_metadata.seconds), 0, sizeof(int64_t));
          time_metadata.seconds = nfs_htonl64((int64_t) time(NULL));
          time_metadata.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_metadata,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_MODIFY\n");
#endif

          /* The time for the last modify operation, the mtime in the unix's semantic */
          memset(&(time_modify.seconds), 0, sizeof(int64_t));
          time_modify.seconds = nfs_htonl64((int64_t) time(NULL));
          time_modify.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_modify,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY_SET:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_TIME_MODIFY_SET\n");
#endif

          op_attr_success = 0;  /* should never be used here, only for setattr */
          break;

        case FATTR4_MOUNTED_ON_FILEID:
#ifdef   _DEBUG_NFS_V4_XATTR
          printf("-----> Wanting FATTR4_MOUNTED_ON_FILEID\n");
#endif
          if (!nfs4_FhandleToFSAL
              (&data->mounted_on_FH, &mounted_on_fsal_handle, data->pcontext))
            {
              if (FSAL_IS_ERROR
                  (fsal_status =
                   FSAL_DigestHandle(data->pcontext->export_context, FSAL_DIGEST_FILEID4,
                                     &mounted_on_fsal_handle,
                                     (caddr_t) & mounted_on_fileid)))
                {
                  op_attr_success = 0;
                }
              else
                {
                  mounted_on_fileid = nfs_htonl64(mounted_on_fileid);
                  memcpy((char *)(attrvalsBuffer + LastOffset), &mounted_on_fileid,
                         sizeof(fattr4_mounted_on_fileid));
                  LastOffset += fattr4tab[attribute_to_set].size_fattr4;
                  op_attr_success = 1;
                }
            }
          else
            op_attr_success = 0;

          break;

        default:
          DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs,
                            NIV_EVENT, "Bad file attributes %d queried",
                            attribute_to_set);
          /* BUGAZOMEU : un traitement special ici */
          break;
        }                       /* switch( attr_to_set ) */

      /* Increase the Offset for the next operation if this was a success */
      if (op_attr_success)
        {
          /* Set the returned bitmask */
          attrvalslist[j] = attribute_to_set;
          j += 1;

          /* Be carefull not to get out of attrvalsBuffer */
          if (LastOffset > NFS4_ATTRVALS_BUFFLEN)
            return -1;
        }

    }                           /* for i */

#ifdef   _DEBUG_NFS_V4_XATTR
  printf("----------------------------------------\n");
#endif

  /* LastOffset contains the length of the attrvalsBuffer usefull data */
  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs, NIV_FULL_DEBUG,
                    "Fattr (pseudo) At the end LastOffset = %u, i=%d, j=%d", LastOffset,
                    i, j);

  /* Set the bitmap for result */
  /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
  if ((Fattr->attrmask.bitmap4_val =
       (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
    return -1;
  memset(Fattr->attrmask.bitmap4_val, 0, 2 * sizeof(uint32_t));

  nfs4_list_to_bitmap4(&(Fattr->attrmask), &j, attrvalslist);

  /* Set the attrlist4 */
  Fattr->attr_vals.attrlist4_len = LastOffset;

  /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
  if ((Fattr->attr_vals.attrlist4_val =
       Mem_Alloc(Fattr->attr_vals.attrlist4_len)) == NULL)
    return -1;
  memset(Fattr->attr_vals.attrlist4_val, 0, Fattr->attr_vals.attrlist4_len);

  memcpy(Fattr->attr_vals.attrlist4_val, attrvalsBuffer, Fattr->attr_vals.attrlist4_len);

  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs, NIV_FULL_DEBUG,
                    "nfs4_PseudoToFattr (end): Fattr->attr_vals.attrlist4_len = %d",
                    Fattr->attr_vals.attrlist4_len);
  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs, NIV_FULL_DEBUG,
                    "nfs4_PseudoToFattr (end):Fattr->attrmask.bitmap4_len = %d  [0]=%u, [1]=%u",
                    Fattr->attrmask.bitmap4_len, Fattr->attrmask.bitmap4_val[0],
                    Fattr->attrmask.bitmap4_val[1]);

  return 0;
}                               /* nfs4_XattrToFattr */

/** 
 * nfs4_fh_to_xattrfh: builds the fh to the xattrs ghost directory 
 *
 * @param pfhin  [IN]  input file handle (the object whose xattr fh is queryied)
 * @param pfhout [OUT] output file handle
 *
 * @return NFS4_OK 
 *
 */
nfsstat4 nfs4_fh_to_xattrfh(nfs_fh4 * pfhin, nfs_fh4 * pfhout)
{
  file_handle_v4_t *pfile_handle = NULL;

  memcpy(pfhout->nfs_fh4_val, pfhin->nfs_fh4_val, pfhin->nfs_fh4_len);

  pfile_handle = (file_handle_v4_t *) (pfhout->nfs_fh4_val);

  /* the following choice is made for xattr: the field xattr_pos contains :
   * - 0 if the FH is related to an actual FH object
   * - 1 if the FH is the one for the xattr ghost directory
   * - a value greater than 1 if the fh is related to a ghost file in ghost xattr directory that represents a xattr. The value is then equal 
   *   to the xattr_id + 1 (see how FSAL manages xattrs for meaning of this field). This limits the number of xattrs per object to 254. 
   */
  pfile_handle->xattr_pos = 1;  /**< 1 = xattr ghost directory */

  return NFS4_OK;
}                               /* nfs4_fh_to_xattrfh */

/**
 * nfs4_op_getattr_xattr: Gets attributes for xattrs objects
 * 
 * Gets attributes for xattrs objects
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

#define arg_GETATTR4 op->nfs_argop4_u.opgetattr
#define res_GETATTR4 resp->nfs_resop4_u.opgetattr

int nfs4_op_getattr_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getattr";

  resp->resop = NFS4_OP_GETATTR;

  res_GETATTR4.status = NFS4_OK;

  if (nfs4_XattrToFattr(&(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                        data, &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
    res_GETATTR4.status = NFS4ERR_SERVERFAULT;
  else
    res_GETATTR4.status = NFS4_OK;

  return res_GETATTR4.status;
}                               /* nfs4_op_getattr_xattr */

/**
 * nfs4_op_access_xattrs: Checks for xattrs accessibility 
 * 
 * Checks for object accessibility in xattrs fs. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

/* Shorter notation to avoid typos */
#define res_ACCESS4 resp->nfs_resop4_u.opaccess
#define arg_ACCESS4 op->nfs_argop4_u.opaccess

int nfs4_op_access_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_access_xattr";

  resp->resop = NFS4_OP_ACCESS;

  /* All access types are supported */
  /** @todo think about making this RW, it is RO for now */
  res_ACCESS4.ACCESS4res_u.resok4.supported = ACCESS4_READ | ACCESS4_LOOKUP;

  /* DELETE/MODIFY/EXTEND are not supported in the pseudo fs */
  res_ACCESS4.ACCESS4res_u.resok4.access =
      arg_ACCESS4.access & ~(ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE);

  return NFS4_OK;
}                               /* nfs4_op_access_xattr */

/**
 * nfs4_op_lookup_xattr: looks up into the pseudo fs.
 * 
 * looks up into the pseudo fs. If a junction traversal is detected, does the necessary stuff for correcting traverse.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

/* Shorter notation to avoid typos */
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_name_t name;
  char strname[MAXNAMLEN];
  fsal_status_t fsal_status;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  unsigned int xattr_id = 0;
  file_handle_v4_t *pfile_handle = NULL;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookup_xattr";

  /* The xattr directory contains no subdirectory, lookup always returns ENOENT */
  res_LOOKUP4.status = NFS4_OK;

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* UTF8 strings may not end with \0, but they carry their length */
  strncpy(strname, arg_LOOKUP4.objname.utf8string_val,
          arg_LOOKUP4.objname.utf8string_len);
  strname[arg_LOOKUP4.objname.utf8string_len] = '\0';

  /* Build the FSAL name */
  if ((cache_status = cache_inode_error_convert(FSAL_str2name(strname,
                                                              MAXNAMLEN,
                                                              &name))) !=
      CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Try to get a FSAL_XAttr of that name */
  fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &name, data->pcontext, &xattr_id);
  if (FSAL_IS_ERROR(fsal_status))
    {
      return NFS4ERR_NOENT;
    }

  /* Attribute was found */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  pfile_handle->xattr_pos = xattr_id + 2;

  return NFS4_OK;
}                               /* nfs4_op_lookup_xattr */

/**
 * nfs4_op_lookupp_xattr: looks up into the pseudo fs for the parent directory
 * 
 * looks up into the pseudo fs for the parent directory of the current file handle. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

/* Shorter notation to avoid typos */
#define arg_LOOKUPP4 op->nfs_argop4_u.oplookupp
#define res_LOOKUPP4 resp->nfs_resop4_u.oplookupp

int nfs4_op_lookupp_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookup_xattr";

  resp->resop = NFS4_OP_LOOKUPP;

  res_LOOKUPP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookupp_xattr */

/**
 * nfs4_op_readdir_xattr: Reads a directory in the pseudo fs 
 * 
 * Reads a directory in the pseudo fs.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

extern time_t ServerBootTime;

/* shorter notation to avoid typo */
#define arg_READDIR4 op->nfs_argop4_u.opreaddir
#define res_READDIR4 resp->nfs_resop4_u.opreaddir

int nfs4_op_readdir_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  unsigned long dircount = 0;
  unsigned long maxcount = 0;
  unsigned long estimated_num_entries = 0;
  unsigned long i = 0;
  int eod_met = FALSE;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];
  nfs_cookie4 cookie;
  verifier4 cookie_verifier;
  unsigned long space_used = 0;
  entry4 *entry_nfs_array = NULL;
  entry_name_array_item_t *entry_name_array = NULL;
  nfs_fh4 entryFH;
  cache_inode_fsal_data_t fsdata;
  fsal_path_t exportpath_fsal;
  fsal_attrib_list_t attr;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_mdsize_t strsize = MNTPATHLEN + 1;
  fsal_status_t fsal_status;
  int error = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t *pentry = NULL;
  file_handle_v4_t file_handle;
  nfs_fh4 nfsfh;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readdir_xattr";

  bitmap4 RdAttrErrorBitmap = { 1, (uint32_t *) "\0\0\0\b" };   /* 0xB = 11 = FATTR4_RDATTR_ERROR */
  attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  memcpy((char *)&file_handle, data->currentFH.nfs_fh4_val, data->currentFH.nfs_fh4_len);
  nfsfh.nfs_fh4_len = data->currentFH.nfs_fh4_len;
  nfsfh.nfs_fh4_val = (char *)&file_handle;

  entryFH.nfs_fh4_len = 0;

  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs, NIV_FULL_DEBUG,
                    "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  DisplayLogJdLevel(((cache_inode_client_t *) data->pclient)->log_outputs,
                    NIV_FULL_DEBUG,
                    "PSEUDOFS READDIR: dircount=%d, maxcount=%d, cookie=%d, sizeof(entry4)=%d num_entries=%d",
                    dircount, maxcount, cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if (maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and HPSS cookie */

  /* Do not use a cookie of 1 or 2 (reserved values) */
  if (cookie == 1 || cookie == 2)
    {
      res_READDIR4.status = NFS4ERR_BAD_COOKIE;
      return res_READDIR4.status;
    }

  if (cookie != 0)
    cookie = cookie - 2;        /* 0,1 and 2 are reserved, there is a delta of '3' because of this */

  /* Get only attributes that are allowed to be read */
  if (!nfs4_Fattr_Check_Access_Bitmap(&arg_READDIR4.attr_request, FATTR4_ATTR_READ))
    {
      res_READDIR4.status = NFS4ERR_INVAL;
      return res_READDIR4.status;
    }

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if (maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }
  /* Cookie verifier has the value of the Server Boot Time for pseudo fs */

#ifdef _WITH_COOKIE_VERIFIER
  /* BUGAZOMEU: management of the cookie verifier */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
  if (NFS_SpecificConfig.UseCookieVerf == 1)
    {
      memcpy(cookie_verifier, &ServerBootTime, sizeof(ServerBootTime));
      if (cookie != 0)
        {
          if (memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE) != 0)
            {
              res_READDIR4.status = NFS4ERR_BAD_COOKIE;
              Mem_Free(entry_nfs_array);
              return res_READDIR4.status;
            }
        }
    }
#endif

  /* The default behaviour is to consider that eof is not reached, the returned values by cache_inode_readdir 
   * will let us know if eod was reached or not */
  res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;

  /* Get the fsal_handle */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }

  /* Used FSAL extended attributes functions */
  fsal_status = FSAL_ListXAttrs(pfsal_handle,
                                cookie,
                                data->pcontext,
                                xattrs_tab,
                                estimated_num_entries, &nb_xattrs_read, &eod_met);
  if (FSAL_IS_ERROR(fsal_status))
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }

  if (eod_met == TRUE)
    {
      /* This is the end of the directory */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }

  if (nb_xattrs_read == 0)
    {
      /* only . and .. */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }
  else
    {
      /* Allocation of reply structures */
      if ((entry_name_array =
           (entry_name_array_item_t *) Mem_Alloc(estimated_num_entries *
                                                 (FSAL_MAX_NAME_LEN + 1))) == NULL)
        {
          DisplayErrorLog(ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }
      memset((char *)entry_name_array, 0,
             estimated_num_entries * (FSAL_MAX_NAME_LEN + 1));

      if ((entry_nfs_array =
           (entry4 *) Mem_Alloc(estimated_num_entries * sizeof(entry4))) == NULL)
        {
          DisplayErrorLog(ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      for (i = 0; i < nb_xattrs_read; i++)
        {
          entry_nfs_array[i].name.utf8string_val = entry_name_array[i];

          if (str2utf8(xattrs_tab[i].xattr_name.name, &entry_nfs_array[i].name) == -1)
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }

          /* Set the cookie value */
          entry_nfs_array[i].cookie = cookie + i + 2;   /* 0, 1 and 2 are reserved */

          file_handle.xattr_pos = xattrs_tab[i].xattr_id + 2;
          if (nfs4_XattrToFattr(&(entry_nfs_array[i].attrs),
                                data, &nfsfh, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }

          /* Chain the entries together */
          entry_nfs_array[i].nextentry = NULL;
          if (i != 0)
            entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);

          /* This test is there to avoid going further than the buffer provided by the client 
           * the factor "9/10" is there for safety. Its value could be change as beta tests will be done */
          if ((caddr_t)
              ((caddr_t) (&entry_nfs_array[i]) - (caddr_t) (&entry_nfs_array[0])) >
              (caddr_t) (maxcount * 9 / 10))
            break;

        }                       /* for */

    }                           /* else */

  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if (i == 0)
    res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
  else
    res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;

  res_READDIR4.status = NFS4_OK;

  return NFS4_OK;

}                               /* nfs4_op_readdir_xattr */

/**
 * nfs4_op_open_xattr: Opens the content of a xattr attribute
 * 
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

int nfs4_op_open_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp)
{
  /* Nothing done here, we do not use the stateful logic for accessing xattrs */
  return NFS4_OK;
}                               /* nfs4_op_open_xattr

                                   /**
                                   * nfs4_op_read_xattr: Reads the content of a xattr attribute
                                   * 
                                   * @param op    [IN]    pointer to nfs4_op arguments
                                   * @param data  [INOUT] Pointer to the compound request's data
                                   * @param resp  [IN]    Pointer to nfs4_op results
                                   * 
                                   * @return NFS4_OK if successfull, other values show an error. 
                                   * 
                                 */
#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

int nfs4_op_read_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_handle_t *pfsal_handle = NULL;
  file_handle_v4_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  cache_inode_status_t cache_status;
  fsal_status_t fsal_status;
  char *buffer = NULL;
  size_t size_returned;

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the xattr_id */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  /* Get the xattr related to this xattr_id */
  if ((buffer = (char *)Mem_Alloc(XATTR_BUFFERSIZE)) == NULL)
    {
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }
  memset(buffer, 0, XATTR_BUFFERSIZE);

  fsal_status = FSAL_GetXAttrValueById(pfsal_handle,
                                       xattr_id,
                                       data->pcontext,
                                       buffer, XATTR_BUFFERSIZE, &size_returned);

  if (FSAL_IS_ERROR(fsal_status))
    {
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }

  res_READ4.READ4res_u.resok4.data.data_len = size_returned;
  res_READ4.READ4res_u.resok4.data.data_val = buffer;

  res_READ4.READ4res_u.resok4.eof = TRUE;

  res_READ4.status = NFS4_OK;

  return res_READ4.status;
}                               /* nfs4_op_read_xattr */

/**
 * nfs4_op_write_xattr: Writes the content of a xattr attribute
 * 
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
#define arg_WRITE4 op->nfs_argop4_u.opwrite
#define res_WRITE4 resp->nfs_resop4_u.opwrite

int nfs4_op_write_xattr(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_handle_t *pfsal_handle = NULL;
  file_handle_v4_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  cache_inode_status_t cache_status;
  fsal_status_t fsal_status;
  char *buffer = NULL;
  size_t size_returned;

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if (cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the xattr_id */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  /** @todo to be continued */

  return NFS4_OK;
}                               /* nfs4_op_write_xattr */
