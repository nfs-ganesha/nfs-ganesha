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
#include "rpc.h"
#include "log_macros.h"
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
  fattr4_rdattr_error rdattr_error;
  file_handle_v4_t *pfile_handle = NULL;
  fsal_attrib_list_t fsalattr;

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

  LogFullDebug(COMPONENT_NFS_V4_XATTR,
               "Asked Attributes (Pseudo): Bitmap = (len=%d, val[0]=%d, val[1]=%d), %d item in list",
               Bitmap->bitmap4_len, Bitmap->bitmap4_val[0], Bitmap->bitmap4_val[1],
               attrmasklen);

  for(i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

      LogFullDebug(COMPONENT_NFS_V4_XATTR,
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
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SUPPORTED_ATTRS");

          /* The supported attributes have field ',supported' set in tab fattr4tab, I will proceed in 2 pass 
           * 1st: compute the number of supported attributes
           * 2nd: allocate the replyed bitmap and fill it
           *
           * I do not set a #define to keep the number of supported attributes because I want this parameter
           * to be a consequence of fattr4tab and avoid incoherency */

          /* How many supported attributes ? Compute the result in variable named c and set attrvalslist_supported  */
          c = 0;
          for(k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_MOUNTED_ON_FILEID; k++)
            {
              if(fattr4tab[k].supported)
                {
                  attrvalslist_supported[c++] = k;
                }
            }

          /* Let set the reply bitmap */
          /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
          if((supported_attrs.bitmap4_val =
              (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 2 * sizeof(uint32_t));
          nfs4_list_to_bitmap4(&supported_attrs, &c, attrvalslist_supported);

	  LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "Fattr (pseudo) supported_attrs(len)=%u -> %u|%u",
                       supported_attrs.bitmap4_len, supported_attrs.bitmap4_val[0],
                       supported_attrs.bitmap4_val[1]);

          /* This kind of operation is always a success */
          op_attr_success = 1;

          /* we store the index */
          supported_attrs_len = htonl(supported_attrs.bitmap4_len);
          memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_len,
                 sizeof(uint32_t));
          LastOffset += sizeof(uint32_t);

          /* And then the data */
          for(k = 0; k < supported_attrs.bitmap4_len; k++)
            {
              supported_attrs_val = htonl(supported_attrs.bitmap4_val[k]);
              memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_val,
                     sizeof(uint32_t));
              LastOffset += sizeof(uint32_t);
            }
          break;

        case FATTR4_TYPE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TYPE");

          op_attr_success = 1;

          if(pfile_handle->xattr_pos == 1)
            file_type = htonl(NF4DIR);  /* There are only directories in the pseudo fs */
          else
            file_type = htonl(NF4REG);

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_type, sizeof(fattr4_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          break;

        case FATTR4_FH_EXPIRE_TYPE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FH_EXPIRE_TYPE");

          /* For the moment, we handle only the persistent filehandle */
          /* expire_type = htonl( FH4_VOLATILE_ANY ) ; */
          expire_type = htonl(FH4_PERSISTENT);
          memcpy((char *)(attrvalsBuffer + LastOffset), &expire_type,
                 sizeof(expire_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHANGE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_CHANGE");

          /* Use boot time as time value for every pseudo fs object */
          memset(&file_change, 0, sizeof(changeid4));
          file_change = nfs_htonl64((changeid4) time(NULL));

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_change,
                 sizeof(fattr4_change));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SIZE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SIZE");

          file_size = nfs_htonl64((fattr4_size) DEV_BSIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_size, sizeof(fattr4_size));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LINK_SUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_LINK_SUPPORT");

          /* HPSS NameSpace support hard link */
          link_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &link_support,
                 sizeof(fattr4_link_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYMLINK_SUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SYMLINK_SUPPORT");

          /* HPSS NameSpace support symbolic link */
          symlink_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &symlink_support,
                 sizeof(fattr4_symlink_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NAMED_ATTR:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_NAMED_ATTR");

          /* For this version of the binary, named attributes is not supported */
          named_attr = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &named_attr,
                 sizeof(fattr4_named_attr));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FSID:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FSID");

          fsid.major = nfs_htonl64((uint64_t) data->pexport->filesystem_id.major);
          fsid.minor = nfs_htonl64((uint64_t) data->pexport->filesystem_id.minor);

          memcpy((char *)(attrvalsBuffer + LastOffset), &fsid, sizeof(fattr4_fsid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_UNIQUE_HANDLES:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_UNIQUE_HANDLES");

          /* Filehandles are unique */
          unique_handles = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &unique_handles,
                 sizeof(fattr4_unique_handles));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LEASE_TIME:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_LEASE_TIME");

          lease_time = htonl(NFS4_LEASE_LIFETIME);
          memcpy((char *)(attrvalsBuffer + LastOffset), &lease_time,
                 sizeof(fattr4_lease_time));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RDATTR_ERROR:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_RDATTR_ERROR");

          rdattr_error = htonl(NFS4_OK);        /* By default, READDIR call may use a different value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &rdattr_error,
                 sizeof(fattr4_rdattr_error));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACL:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_ACL");

          acl.fattr4_acl_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &acl, sizeof(fattr4_acl));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACLSUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_ACL_SUPPORT");

          aclsupport = htonl(ACL4_SUPPORT_DENY_ACL);    /* temporary, wanting for houston to give me information to implemente ACL's support */
          memcpy((char *)(attrvalsBuffer + LastOffset), &aclsupport,
                 sizeof(fattr4_aclsupport));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ARCHIVE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_ARCHIVE");

          /* Archive flag is not supported */
          archive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &archive, sizeof(fattr4_archive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CANSETTIME:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_CANSETTIME");

          /* The time can be set on files */
          cansettime = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &cansettime,
                 sizeof(fattr4_cansettime));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_INSENSITIVE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_CASE_INSENSITIVE");

          /* pseudofs is not case INSENSITIVE... it is Read-Only */
          case_insensitive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_insensitive,
                 sizeof(fattr4_case_insensitive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_PRESERVING:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_PRESERVING");

          /* pseudofs is case preserving... it is Read-Only */
          case_preserving = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_preserving,
                 sizeof(fattr4_case_preserving));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHOWN_RESTRICTED:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_CHOWN_RESTRICTED");

          /* chown is restricted to root, but in fact no chown will be done on pseudofs */
          chown_restricted = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &chown_restricted,
                 sizeof(fattr4_chown_restricted));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILEHANDLE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FILEHANDLE");

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
          while((len + off) % 4 != 0)
            {
              char c = '\0';

              off += 1;
              memset((char *)(attrvalsBuffer + LastOffset), c, 1);
              LastOffset += 1;
            }

          op_attr_success = 1;
          break;

        case FATTR4_FILEID:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FILEID  xattr_pos=%u",
                 pfile_handle->xattr_pos + 1);

          /* The analog to the inode number. RFC3530 says "a number uniquely identifying the file within the filesystem" 
           * In the case of a pseudofs entry, the entry's unique id is used */
          cache_inode_get_attributes(data->current_entry, &fsalattr);

#ifndef _XATTR_D_USE_SAME_INUM  /* I wrapped off this part of the code... Not sure it would be useful */
          file_id = nfs_htonl64(~(fsalattr.fileid));

          file_id = nfs_htonl64(~(fsalattr.fileid)) - pfile_handle->xattr_pos;
#else
          file_id = nfs_htonl64(fsalattr.fileid);
#endif

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_AVAIL:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FILES_AVAIL");

          files_avail = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_avail,
                 sizeof(fattr4_files_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_FREE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FILES_FREE");

          files_free = nfs_htonl64((fattr4_files_avail) 512);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_free,
                 sizeof(fattr4_files_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_TOTAL:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FILES_TOTAL");

          files_total = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_total,
                 sizeof(fattr4_files_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FS_LOCATIONS:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_FS_LOCATIONS");

          fs_locations.fs_root.pathname4_len = 0;
          fs_locations.locations.locations_len = 0;     /* No FS_LOCATIONS no now */
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HIDDEN:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_HIDDEN");

          /* There are no hidden file in pseudofs */
          hidden = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &hidden, sizeof(fattr4_hidden));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HOMOGENEOUS:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_HOMOGENEOUS");

          /* Unix semantic is homogeneous (all objects have the same kind of attributes) */
          homogeneous = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &homogeneous,
                 sizeof(fattr4_homogeneous));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXFILESIZE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MAXFILESIZE");

          max_filesize = nfs_htonl64((fattr4_maxfilesize) FSINFO_MAX_FILESIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &max_filesize,
                 sizeof(fattr4_maxfilesize));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXLINK:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MAXLINK");

          maxlink = htonl(MAX_HARD_LINK_VALUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxlink, sizeof(fattr4_maxlink));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXNAME:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MAXNAME");

          maxname = htonl((fattr4_maxname) MAXNAMLEN);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxname, sizeof(fattr4_maxname));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXREAD:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MAXREAD");

          maxread = nfs_htonl64((fattr4_maxread) NFS4_PSEUDOFS_MAX_READ_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxread, sizeof(fattr4_maxread));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXWRITE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MAXWRITE");

          maxwrite = nfs_htonl64((fattr4_maxwrite) NFS4_PSEUDOFS_MAX_WRITE_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxwrite,
                 sizeof(fattr4_maxwrite));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MIMETYPE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MIMETYPE");

          mimetype.utf8string_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &mimetype,
                 sizeof(fattr4_mimetype));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;  /* No supported for the moment */
          break;

        case FATTR4_MODE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MODE");

          if(pfile_handle->xattr_pos == 1)
            file_mode = htonl(0555);    /* Every pseudo fs object is dr-xr-xr-x */
          else
            file_mode = htonl(0644);    /* -rw-r--r-- */

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_mode, sizeof(fattr4_mode));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NO_TRUNC:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_NO_TRUNC");

          /* File's names are not truncated, an error is returned is name is too long */
          no_trunc = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &no_trunc,
                 sizeof(fattr4_no_trunc));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NUMLINKS:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_NUMLINKS");

          /* Reply the number of links found in vattr structure */
          file_numlinks = htonl((fattr4_numlinks) 1);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_numlinks,
                 sizeof(fattr4_numlinks));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_OWNER:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_OWNER");

          /* Return the uid as a human readable utf8 string */
          if(uid2utf8(NFS4_ROOT_UID, &file_owner) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner.utf8string_len % 4 == 0)
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
              if(deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;
            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_OWNER_GROUP:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_OWNER_GROUP");

          /* Return the uid as a human readable utf8 string */
          if(gid2utf8(2, &file_owner_group) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner_group.utf8string_len % 4 == 0)
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
              if(deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;
            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_QUOTA_AVAIL_HARD:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_HARD");

          quota_avail_hard = nfs_htonl64((fattr4_quota_avail_hard) NFS_V4_MAX_QUOTA_HARD);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_hard,
                 sizeof(fattr4_quota_avail_hard));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_AVAIL_SOFT:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_SOFT");

          quota_avail_soft = nfs_htonl64((fattr4_quota_avail_soft) NFS_V4_MAX_QUOTA_SOFT);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_soft,
                 sizeof(fattr4_quota_avail_soft));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_USED:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_USED");

          quota_used = nfs_htonl64((fattr4_quota_used) NFS_V4_MAX_QUOTA);
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_used,
                 sizeof(fattr4_quota_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RAWDEV:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_RAWDEV");

          /* Not usefull, there are no special block or character file in HPSS */
          /* since FATTR4_TYPE will never be NFS4BLK or NFS4CHR, this value should not be used by the client */
          rawdev.specdata1 = htonl(0);
          rawdev.specdata2 = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &rawdev, sizeof(fattr4_rawdev));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_AVAIL:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SPACE_AVAIL");

          space_avail = nfs_htonl64(512000LL);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_avail,
                 sizeof(fattr4_space_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_FREE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SPACE_FREE");

          space_free = nfs_htonl64(512000LL);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_free,
                 sizeof(fattr4_space_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_TOTAL:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SPACE_TOTAL");

          space_total = nfs_htonl64(1024000LL); /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_total,
                 sizeof(fattr4_space_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_USED:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SPACE_USED");

          /* the number of bytes on the filesystem used by the object, which is slightly different 
           * from the file's size (there can be hole in the file) */
          file_space_used = nfs_htonl64((fattr4_space_used) DEV_BSIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_space_used,
                 sizeof(fattr4_space_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYSTEM:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_SYSTEM");

          /* This is not a windows system File-System with respect to the regarding API */
          system = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &system, sizeof(fattr4_system));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_ACCESS");

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
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_ACCESS_SET");

          /* To be used with NFS4_OP_SETATTR only */
          op_attr_success = 0;
          break;

        case FATTR4_TIME_BACKUP:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_BACKUP");

          /* No time backup, return unix's beginning of time */
          time_backup.seconds = nfs_htonl64(0LL);
          time_backup.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_backup,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_CREATE:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_CREATE");

          /* No time create, return unix's beginning of time */
          time_create.seconds = nfs_htonl64(0LL);
          time_create.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_create,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_DELTA:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_DELTA");


          /* According to RFC3530, this is "the smallest usefull server time granularity", I set this to 1s */
          time_delta.seconds = nfs_htonl64(1LL);
          time_delta.nseconds = 0;
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_delta,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_METADATA:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_METADATA");


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
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_MODIFY");


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
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_TIME_MODIFY_SET");


          op_attr_success = 0;  /* should never be used here, only for setattr */
          break;

        case FATTR4_MOUNTED_ON_FILEID:
          LogFullDebug(COMPONENT_NFS_V4_XATTR,
                       "-----> Wanting FATTR4_MOUNTED_ON_FILEID");

          cache_inode_get_attributes(data->current_entry, &fsalattr);

#ifndef _XATTR_D_USE_SAME_INUM  /* I wrapped off this part of the code... Not sure it would be useful */
          file_id = nfs_htonl64(~(fsalattr.fileid));

          file_id = nfs_htonl64(~(fsalattr.fileid)) - pfile_handle->xattr_pos;
#else
          file_id = nfs_htonl64(fsalattr.fileid);
#endif

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;

          break;

        default:
	  LogWarn(COMPONENT_NFS_V4_XATTR, "Bad file attributes %d queried",
                  attribute_to_set);
          /* BUGAZOMEU : un traitement special ici */
          break;
        }                       /* switch( attr_to_set ) */

      /* Increase the Offset for the next operation if this was a success */
      if(op_attr_success)
        {
          /* Set the returned bitmask */
          attrvalslist[j] = attribute_to_set;
          j += 1;

          /* Be carefull not to get out of attrvalsBuffer */
          if(LastOffset > NFS4_ATTRVALS_BUFFLEN)
            return -1;
        }

    }                           /* for i */

  LogFullDebug(COMPONENT_NFS_V4_XATTR,
	       "----------------------------------------");

  /* LastOffset contains the length of the attrvalsBuffer usefull data */
  LogFullDebug(COMPONENT_NFS_V4_XATTR,
               "Fattr (pseudo) At the end LastOffset = %u, i=%d, j=%d",
               LastOffset, i, j);

  /* Set the bitmap for result */
  /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
  if((Fattr->attrmask.bitmap4_val = (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
    return -1;
  memset(Fattr->attrmask.bitmap4_val, 0, 2 * sizeof(uint32_t));

  nfs4_list_to_bitmap4(&(Fattr->attrmask), &j, attrvalslist);

  /* Set the attrlist4 */
  Fattr->attr_vals.attrlist4_len = LastOffset;

  /** @todo: BUGAZOMEU: Allocation at NULL Adress here.... */
  if((Fattr->attr_vals.attrlist4_val = Mem_Alloc(Fattr->attr_vals.attrlist4_len)) == NULL)
    return -1;
  memset(Fattr->attr_vals.attrlist4_val, 0, Fattr->attr_vals.attrlist4_len);

  memcpy(Fattr->attr_vals.attrlist4_val, attrvalsBuffer, Fattr->attr_vals.attrlist4_len);

  LogFullDebug(COMPONENT_NFS_V4_XATTR,
               "nfs4_PseudoToFattr (end): Fattr->attr_vals.attrlist4_len = %d",
               Fattr->attr_vals.attrlist4_len);
  LogFullDebug(COMPONENT_NFS_V4_XATTR,
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
 * nfs4_xattrfh_to_fh: builds the fh from the xattrs ghost directory 
 *
 * @param pfhin  [IN]  input file handle 
 * @param pfhout [OUT] output file handle
 *
 * @return NFS4_OK 
 *
 */
nfsstat4 nfs4_xattrfh_to_fh(nfs_fh4 * pfhin, nfs_fh4 * pfhout)
{
  file_handle_v4_t *pfile_handle = NULL;

  memcpy(pfhout->nfs_fh4_val, pfhin->nfs_fh4_val, pfhin->nfs_fh4_len);

  pfile_handle = (file_handle_v4_t *) (pfhout->nfs_fh4_val);

  pfile_handle->xattr_pos = 0;  /**< 0 = real filehandle */

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

  if(nfs4_XattrToFattr(&(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
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
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(strname, sizeof(strname), &arg_LOOKUP4.objname);

  /* Build the FSAL name */
  if((cache_status = cache_inode_error_convert(FSAL_str2name(strname,
                                                             MAXNAMLEN,
                                                             &name))) !=
     CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Try to get a FSAL_XAttr of that name */
  fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &name, data->pcontext, &xattr_id);
  if(FSAL_IS_ERROR(fsal_status))
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

  res_LOOKUPP4.status = nfs4_xattrfh_to_fh(&(data->currentFH), &(data->currentFH));

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
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  file_handle_v4_t file_handle;
  nfs_fh4 nfsfh;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readdir_xattr";

  bitmap4 RdAttrErrorBitmap = { 1, (uint32_t *) "\0\0\0\b" };   /* 0xB = 11 = FATTR4_RDATTR_ERROR */
  attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  memset(&file_handle, 0, sizeof(file_handle_v4_t));
  memcpy((char *)&file_handle, data->currentFH.nfs_fh4_val, data->currentFH.nfs_fh4_len);
  nfsfh.nfs_fh4_len = data->currentFH.nfs_fh4_len;
  nfsfh.nfs_fh4_val = (char *)&file_handle;

  entryFH.nfs_fh4_len = 0;

  LogFullDebug(COMPONENT_NFS_V4_XATTR, "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  LogFullDebug(COMPONENT_NFS_V4_XATTR,
               "PSEUDOFS READDIR: dircount=%lu, maxcount=%lu, cookie=%"PRIu64", sizeof(entry4)=%lu num_entries=%lu",
               dircount, maxcount, (uint64_t)cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
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
  if(cookie == 1 || cookie == 2)
    {
      res_READDIR4.status = NFS4ERR_BAD_COOKIE;
      return res_READDIR4.status;
    }

  if(cookie != 0)
    cookie = cookie - 3;        /* 0,1 and 2 are reserved, there is a delta of '3' because of this */

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access_Bitmap(&arg_READDIR4.attr_request, FATTR4_ATTR_READ))
    {
      res_READDIR4.status = NFS4ERR_INVAL;
      return res_READDIR4.status;
    }

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Cookie verifier has the value of the Server Boot Time for pseudo fs */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
#ifdef _WITH_COOKIE_VERIFIER
  /* BUGAZOMEU: management of the cookie verifier */
  if(NFS_SpecificConfig.UseCookieVerf == 1)
    {
      memcpy(cookie_verifier, &ServerBootTime, sizeof(ServerBootTime));
      if(cookie != 0)
        {
          if(memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE) != 0)
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
  if(cache_status != CACHE_INODE_SUCCESS)
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
  if(FSAL_IS_ERROR(fsal_status))
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }

  if(eod_met == TRUE)
    {
      /* This is the end of the directory */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }

  if(nb_xattrs_read == 0)
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
      if((entry_name_array =
          (entry_name_array_item_t *) Mem_Alloc(estimated_num_entries *
                                                (FSAL_MAX_NAME_LEN + 1))) == NULL)
        {
          LogError(COMPONENT_NFS_V4_XATTR, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }
      memset((char *)entry_name_array, 0,
             estimated_num_entries * (FSAL_MAX_NAME_LEN + 1));

      if((entry_nfs_array =
          (entry4 *) Mem_Alloc(estimated_num_entries * sizeof(entry4))) == NULL)
        {
          LogError(COMPONENT_NFS_V4_XATTR, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      for(i = 0; i < nb_xattrs_read; i++)
        {
          entry_nfs_array[i].name.utf8string_val = entry_name_array[i];

          if(str2utf8(xattrs_tab[i].xattr_name.name, &entry_nfs_array[i].name) == -1)
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }

          /* Set the cookie value */
          entry_nfs_array[i].cookie = cookie + i + 3;   /* 0, 1 and 2 are reserved */

          file_handle.xattr_pos = xattrs_tab[i].xattr_id + 2;
          if(nfs4_XattrToFattr(&(entry_nfs_array[i].attrs),
                               data, &nfsfh, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }

          /* Chain the entries together */
          entry_nfs_array[i].nextentry = NULL;
          if(i != 0)
            entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);

          /* This test is there to avoid going further than the buffer provided by the client 
           * the factor "9/10" is there for safety. Its value could be change as beta tests will be done */
          if((caddr_t)
             ((caddr_t) (&entry_nfs_array[i]) - (caddr_t) (&entry_nfs_array[0])) >
             (caddr_t) (maxcount * 9 / 10))
            break;

        }                       /* for */

    }                           /* else */

  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if(i == 0)
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

#define arg_OPEN4 op->nfs_argop4_u.opopen
#define res_OPEN4 resp->nfs_resop4_u.opopen
int nfs4_op_open_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_name_t name;
  char strname[MAXNAMLEN];
  fsal_status_t fsal_status;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  unsigned int xattr_id = 0;
  file_handle_v4_t *pfile_handle = NULL;
  char empty_buff[16] = "";

  res_OPEN4.status = NFS4_OK;

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      return res_OPEN4.status;
    }

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(strname, sizeof(strname), &arg_OPEN4.claim.open_claim4_u.file);

  /* Build the FSAL name */
  if((cache_status = cache_inode_error_convert(FSAL_str2name(strname,
                                                             MAXNAMLEN,
                                                             &name))) !=
     CACHE_INODE_SUCCESS)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      return res_OPEN4.status;
    }

  /* we do not use the stateful logic for accessing xattrs */
  switch (arg_OPEN4.openhow.opentype)
    {
    case OPEN4_CREATE:
      /* To be done later */
      /* set empty attr */
      fsal_status = FSAL_SetXAttrValue(pfsal_handle,
                                       &name,
                                       data->pcontext, empty_buff, sizeof(empty_buff),
                                       TRUE);

      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = nfs4_Errno(cache_inode_error_convert(fsal_status));
          return res_OPEN4.status;
        }

      /* Now, getr the id */
      fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &name, data->pcontext, &xattr_id);
      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = NFS4ERR_NOENT;
          return res_OPEN4.status;
        }

      /* Attribute was found */
      pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

      /* for Xattr FH, we adopt the current convention:
       * xattr_pos = 0 ==> the FH is the one of the actual FS object
       * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
       * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
      pfile_handle->xattr_pos = xattr_id + 2;

      res_OPEN4.status = NFS4_OK;
      return NFS4_OK;

      break;

    case OPEN4_NOCREATE:

      /* Try to get a FSAL_XAttr of that name */
      fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &name, data->pcontext, &xattr_id);
      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = NFS4ERR_NOENT;
          return res_OPEN4.status;
        }

      /* Attribute was found */
      pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

      /* for Xattr FH, we adopt the current convention:
       * xattr_pos = 0 ==> the FH is the one of the actual FS object
       * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
       * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
      pfile_handle->xattr_pos = xattr_id + 2;

      res_OPEN4.status = NFS4_OK;
      return NFS4_OK;

      break;

    }                           /* switch (arg_OPEN4.openhow.opentype) */

  res_OPEN4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_open_xattr */

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
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
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
  if((buffer = (char *)Mem_Alloc(XATTR_BUFFERSIZE)) == NULL)
    {
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }
  memset(buffer, 0, XATTR_BUFFERSIZE);

  fsal_status = FSAL_GetXAttrValueById(pfsal_handle,
                                       xattr_id,
                                       data->pcontext,
                                       buffer, XATTR_BUFFERSIZE, &size_returned);

  if(FSAL_IS_ERROR(fsal_status))
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

extern verifier4 NFS4_write_verifier;   /* NFS V4 write verifier from nfs_Main.c     */

int nfs4_op_write_xattr(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_handle_t *pfsal_handle = NULL;
  file_handle_v4_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  cache_inode_status_t cache_status;
  fsal_status_t fsal_status;

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
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

  fsal_status = FSAL_SetXAttrValueById(pfsal_handle,
                                       xattr_id,
                                       data->pcontext,
                                       arg_WRITE4.data.data_val,
                                       arg_WRITE4.data.data_len);

  if(FSAL_IS_ERROR(fsal_status))
    {
      res_WRITE4.status = NFS4ERR_SERVERFAULT;
      return res_WRITE4.status;
    }

  res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;

  res_WRITE4.WRITE4res_u.resok4.count = arg_WRITE4.data.data_len;
  memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier, sizeof(verifier4));

  res_WRITE4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_write_xattr */

#define arg_REMOVE4 op->nfs_argop4_u.opremove
#define res_REMOVE4 resp->nfs_resop4_u.opremove
int nfs4_op_remove_xattr(struct nfs_argop4 *op, compound_data_t * data,
                         struct nfs_resop4 *resp)
{
  fsal_status_t fsal_status;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_name_t name;

  /* Check for name length */
  if(arg_REMOVE4.target.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_REMOVE4.status = NFS4ERR_NAMETOOLONG;
      return res_REMOVE4.status;
    }

  /* get the filename from the argument, it should not be empty */
  if(arg_REMOVE4.target.utf8string_len == 0)
    {
      res_REMOVE4.status = NFS4ERR_INVAL;
      return res_REMOVE4.status;
    }

  /* NFS4_OP_REMOVE can delete files as well as directory, it replaces NFS3_RMDIR and NFS3_REMOVE
   * because of this, we have to know if object is a directory or not */
  if((cache_status =
      cache_inode_error_convert(FSAL_buffdesc2name
                                ((fsal_buffdesc_t *) & arg_REMOVE4.target,
                                 &name))) != CACHE_INODE_SUCCESS)
    {
      res_REMOVE4.status = nfs4_Errno(cache_status);
      return res_REMOVE4.status;
    }

  /* Get the FSAL Handle fo the current object */
  pfsal_handle = cache_inode_get_fsal_handle(data->current_entry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_REMOVE4.status = nfs4_Errno(cache_status);
      return res_REMOVE4.status;
    }

  /* Test RM7: remiving '.' should return NFS4ERR_BADNAME */
  if(!FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_REMOVE4.status = NFS4ERR_BADNAME;
      return res_REMOVE4.status;
    }

  fsal_status = FSAL_RemoveXAttrByName(pfsal_handle, data->pcontext, &name);
  if(FSAL_IS_ERROR(fsal_status))
    {
      res_REMOVE4.status = NFS4ERR_SERVERFAULT;
      return res_REMOVE4.status;
    }

  res_REMOVE4.status = NFS4_OK;
  return res_REMOVE4.status;
}
