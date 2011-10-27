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
 * \file    nfs4_pseudo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:49:32 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4 pseudo file system.
 *
 * nfs4_pseudo.c: Routines used for managing the NFS4 pseudo file system.
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

#define NB_TOK_ARG 10
#define NB_OPT_TOK 10
#define NB_TOK_PATH 20

static pseudofs_t gPseudoFs;

/**
 * nfs4_PseudoToId: TConverts a file handle (to a pseudo object) to the id of this pseudo object in the pseudofs
 * 
 *  This routine merely extracts a field from the file handle which is not seen as opaque in this case. Because
 *  file handle are opaque structure, it is prefered to have a dedicated function for this and so hiding the
 *  file handle internal structure.
 * 
 *  @param fh4p       [IN]  pointer to the file handle to process. 
 * 
 *  @return the pseudo id found
 *  @see   nfs_GetPseudoFs
 * 
 */

uint64_t nfs4_PseudoToId(nfs_fh4 * fh4p)
{
  file_handle_v4_t *pfhandle4;
  uint64_t out = 0LL;

  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  out = (uint64_t) (pfhandle4->pseudofs_id);
  return out;
}                               /* nfs4_PseudoToId */

/**
 * nfs4_GetPseudoFs: Gets the root of the pseudo file system.
 * 
 * Gets the root of the pseudo file system. This is only a wrapper to static variable gPseudoFs. 
 *
 * @return the pseudo fs root 
 * 
 */

pseudofs_t *nfs4_GetPseudoFs(void)
{
  return &gPseudoFs;
}                               /*  nfs4_GetExportList */

/**
 * nfs4_ExportToPseudoFS: Build a pseudo fs from an exportlist
 * 
 * Build a pseudo fs from an exportlist. This export list itself is obtained by reading the configuration file. 
 *
 * @return the pseudo fs root 
 * 
 */

int nfs4_ExportToPseudoFS(exportlist_t * pexportlist)
{
  exportlist_t *entry;
  exportlist_t *next;           /* exportlist entry   */
  int i = 0;
  int j = 0;
  int found = 0;
  char tmp_pseudopath[MAXPATHLEN];
  char *PathTok[NB_TOK_PATH];
  int NbTokPath;
  pseudofs_t *PseudoFs = NULL;
  pseudofs_entry_t *PseudoFsRoot = NULL;
  pseudofs_entry_t *PseudoFsCurrent = NULL;
  pseudofs_entry_t *newPseudoFsEntry = NULL;
  pseudofs_entry_t *iterPseudoFs = NULL;

  entry = pexportlist;

  PseudoFs = &gPseudoFs;

  /* Init Root of the Pseudo FS tree */
  strncpy(PseudoFs->root.name, "/", MAXNAMLEN);
  strncpy(PseudoFs->root.fullname, "(nfsv4root)", MAXPATHLEN);
  PseudoFs->root.pseudo_id = 0;
  PseudoFs->root.junction_export = NULL;
  PseudoFs->root.next = NULL;
  PseudoFs->root.last = PseudoFsRoot;
  PseudoFs->root.sons = NULL;
  PseudoFs->root.parent = &(PseudoFs->root);    /* root is its own parent */

  /* Allocation of the parsing table */
  for(i = 0; i < NB_TOK_PATH; i++)
    if((PathTok[i] = (char *)Mem_Alloc(MAXNAMLEN)) == NULL)
      return ENOMEM;

  while(entry)
    {
      /* To not forget to init "/" entry */
      PseudoFsCurrent = &(PseudoFs->root);
      PseudoFs->reverse_tab[0] = &(PseudoFs->root);

      /* skip exports that aren't for NFS v4 */
      if((entry->options & EXPORT_OPTION_NFSV4) == 0)
        {
          next = entry->next;
          entry = next;
          continue;
        }

      if(entry->options & EXPORT_OPTION_PSEUDO)
        {
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Id          = %d",
                       entry->id);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: ANON        = %d",
                       entry->anonymous_uid);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Path        = %s",
                       entry->fullpath);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Options     = 0x%x",
                       entry->options);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Num Clients = %d",
                       entry->clients.num_clients);

          /* A pseudo path is to ne managed */
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Now managing %s seen as %s",
                       entry->fullpath, entry->pseudopath);

          /* Parsing the path */
          strncpy(tmp_pseudopath, entry->pseudopath, MAXPATHLEN);
          if((NbTokPath =
              nfs_ParseConfLine(PathTok, NB_TOK_PATH, tmp_pseudopath, find_slash,
                                find_endLine)) < 0)
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "BUILDING PSEUDOFS: Invalid 'pseudo' option: %s",
                      entry->pseudopath);
              next = entry->next;
              entry = next;
              continue;
            }

          /* there must be a leading '/' in the pseudo path */
          if(entry->pseudopath[0] != '/')
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "Pseudo Path '%s' is badly formed",
                      entry->pseudopath);
              next = entry->next;
              entry = next;
              continue;
            }

          /* Loop on each token. Because first character in pseudo path is '/'
           * we can avoid looking at PathTok[0] which is necessary '\0'. That's 
           * the reason why we start looping at pos = 1 */
          for(j = 1; j < NbTokPath; j++)
            LogFullDebug(COMPONENT_NFS_V4, "     tokens are #%s#", PathTok[j]);

          for(j = 1; j < NbTokPath; j++)
            {
              found = 0;
              for(iterPseudoFs = PseudoFsCurrent->sons; iterPseudoFs != NULL;
                  iterPseudoFs = iterPseudoFs->next)
                {
                  /* Looking for a matching entry */
                  if(!strcmp(iterPseudoFs->name, PathTok[j]))
                    {
                      found = 1;
                      break;
                    }
                }               /* for iterPseudoFs */

              if(found)
                {
                  /* a matching entry was found in the tree */
                  PseudoFsCurrent = iterPseudoFs;
                }
              else
                {
                  /* a new entry is to be created */
                  if((newPseudoFsEntry =
                      (pseudofs_entry_t *) Mem_Alloc(sizeof(pseudofs_entry_t))) == NULL)
                    return ENOMEM;

                  /* Creating the new entry, allocate an id for it and add it to reverse tab */
                  strncpy(newPseudoFsEntry->name, PathTok[j], MAXNAMLEN);
                  newPseudoFsEntry->pseudo_id = PseudoFs->last_pseudo_id + 1;
                  PseudoFs->last_pseudo_id = newPseudoFsEntry->pseudo_id;
                  PseudoFs->reverse_tab[PseudoFs->last_pseudo_id] = newPseudoFsEntry;
                  newPseudoFsEntry->junction_export = NULL;
                  newPseudoFsEntry->last = newPseudoFsEntry;
                  newPseudoFsEntry->next = NULL;
                  newPseudoFsEntry->sons = NULL;
                  snprintf(newPseudoFsEntry->fullname, MAXPATHLEN, "%s/%s",
                           PseudoFsCurrent->fullname, PathTok[j]);

                  /* Step into the new entry and attach it to the tree */
                  if(PseudoFsCurrent->sons == NULL)
                    PseudoFsCurrent->sons = newPseudoFsEntry;
                  else
                    {
                      PseudoFsCurrent->sons->last->next = newPseudoFsEntry;
                      PseudoFsCurrent->sons->last = newPseudoFsEntry;
                    }
                  newPseudoFsEntry->parent = PseudoFsCurrent;
                  PseudoFsCurrent = newPseudoFsEntry;
                }

            }                   /* for j */

          /* Now that all entries are added to pseudofs tree, add the junction to the pseudofs */
          PseudoFsCurrent->junction_export = entry;

        }
      /* if( entry->options & EXPORT_OPTION_PSEUDO ) */
      next = entry->next;

      entry = next;
    }                           /* while( entry ) */

  /* desalocation of the parsing table */
  for(i = 0; i < NB_TOK_PATH; i++)
    Mem_Free(PathTok[i]);

  return (0);
}

/**
 * nfs4_PseudoToFattr: Gets the attributes for an entry in the pseudofs
 * 
 * Gets the attributes for an entry in the pseudofs. Because pseudo fs structure is very simple (it is read-only and contains
 * only directory that belongs to root), a set of standardized values is returned
 * 
 * @param psfp       [IN]    pointer to the pseudo fs entry on which attributes are queried
 * @param Fattr      [OUT]   Pointer to the buffer that will contain the queried attributes
 * @param data       [INOUT] Pointer to the compound request's data
 * @param Bitmap     [IN]    Pointer to a bitmap that describes the attributes to be returned
 * 
 * @return 0 if successfull, -1 if something wrong occured. In this case, the reason is that too many attributes were asked.
 * 
 */

int nfs4_PseudoToFattr(pseudofs_entry_t * psfsp,
                       fattr4 * Fattr,
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
#ifdef _USE_NFS4_1
  fattr4_fs_layout_types layout_types;
  layouttype4 layouts[1];
#endif

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

#ifdef _USE_NFS4_1
  unsigned int attrvalslist_supported[FATTR4_FS_CHARSET_CAP];
  unsigned int attrmasklist[FATTR4_FS_CHARSET_CAP];     /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
  unsigned int attrvalslist[FATTR4_FS_CHARSET_CAP];     /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
#else
  unsigned int attrvalslist_supported[FATTR4_MOUNTED_ON_FILEID];
  unsigned int attrmasklist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  unsigned int attrvalslist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
#endif
  char attrvalsBuffer[ATTRVALS_BUFFLEN];

  char __attribute__ ((__unused__)) funcname[] = "nfs4_PseudoToFattr";

  /* memset to make sure the arrays are initiated to 0 */
  memset(attrvalsBuffer, 0, NFS4_ATTRVALS_BUFFLEN);
#ifdef _USE_NFS4_1
  memset((uint32_t *) attrmasklist, 0, FATTR4_FS_CHARSET_CAP * sizeof(uint32_t));
  memset((uint32_t *) attrvalslist, 0, FATTR4_FS_CHARSET_CAP * sizeof(uint32_t));
#else
  memset((uint32_t *) attrmasklist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(uint32_t));
  memset((uint32_t *) attrvalslist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(uint32_t));
#endif

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(Bitmap, &attrmasklen, attrmasklist);

  /* Once the bitmap has been converted to a list of attribute, manage each attribute */
  Fattr->attr_vals.attrlist4_len = 0;
  LastOffset = 0;
  j = 0;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "Asked Attributes (Pseudo): Bitmap = (len=%d, val[0]=%d, val[1]=%d), %d item in list",
               Bitmap->bitmap4_len, Bitmap->bitmap4_val[0],
               Bitmap->bitmap4_val[1], attrmasklen);

  if(attrmasklen == 0)
    {
      Bitmap->bitmap4_len = 0;
      Bitmap->bitmap4_val = 0;
      return 0;                 /* Nothing to be done */
    }

  for(i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SUPPORTED_ATTRS");

          /* The supported attributes have field ',supported' set in tab fattr4tab, I will proceed in 2 pass 
           * 1st: compute the number of supported attributes
           * 2nd: allocate the replyed bitmap and fill it
           *
           * I do not set a #define to keep the number of supported attributes because I want this parameter
           * to be a consequence of fattr4tab and avoid incoherency */

          /* How many supported attributes ? Compute the result in variable named c and set attrvalslist_supported  */
          c = 0;
#ifdef _USE_NFS4_1
          for(k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_FS_CHARSET_CAP; k++)
#else
          for(k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_MOUNTED_ON_FILEID; k++)
#endif
            {
              if(fattr4tab[k].supported)
                {
                  attrvalslist_supported[c++] = k;
                }
            }

          /* Let set the reply bitmap */
#ifdef _USE_NFS4_1
          if((supported_attrs.bitmap4_val =
              (uint32_t *) Mem_Alloc(3 * sizeof(uint32_t))) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 3 * sizeof(uint32_t));
#else
          if((supported_attrs.bitmap4_val =
              (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 2 * sizeof(uint32_t));
#endif

          nfs4_list_to_bitmap4(&supported_attrs, &c, attrvalslist_supported);

	  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TYPE");

          op_attr_success = 1;
          file_type = htonl(NF4DIR);    /* There are only directories in the pseudo fs */

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_type, sizeof(fattr4_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          break;

        case FATTR4_FH_EXPIRE_TYPE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FH_EXPIRE_TYPE");

          /* For the moment, we handle only the persistent filehandle */
          expire_type = htonl(FH4_VOLATILE_ANY);
          /* expire_type = htonl( FH4_PERSISTENT ) ; */
          memcpy((char *)(attrvalsBuffer + LastOffset), &expire_type,
                 sizeof(expire_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHANGE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_CHANGE");

          /* Use boot time as time value for every pseudo fs object */
          memset(&file_change, 0, sizeof(changeid4));
          file_change = nfs_htonl64((changeid4) ServerBootTime);

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_change,
                 sizeof(fattr4_change));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SIZE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SIZE");

          file_size = nfs_htonl64((fattr4_size) DEV_BSIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_size, sizeof(fattr4_size));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LINK_SUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_LINK_SUPPORT");

          /* HPSS NameSpace support hard link */
          link_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &link_support,
                 sizeof(fattr4_link_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYMLINK_SUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SYMLINK_SUPPORT");

          /* HPSS NameSpace support symbolic link */
          symlink_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &symlink_support,
                 sizeof(fattr4_symlink_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NAMED_ATTR:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_NAMED_ATTR");

          /* For this version of the binary, named attributes is not supported */
          named_attr = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &named_attr,
                 sizeof(fattr4_named_attr));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FSID:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FSID");

          /* The file system id (should be unique per fileset according to the HPSS logic) */
          if(psfsp->junction_export == NULL)
            {
              fsid.major = nfs_htonl64(152LL);
              fsid.minor = nfs_htonl64(152LL);
            }
          else
            {
              fsid.major = nfs_htonl64(153LL);  /* @todo BUGAZOMEU : tres cradem mais utile */
              fsid.minor = nfs_htonl64(153LL);
            }
          memcpy((char *)(attrvalsBuffer + LastOffset), &fsid, sizeof(fattr4_fsid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_UNIQUE_HANDLES:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_UNIQUE_HANDLES");

          /* Filehandles are unique */
          unique_handles = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &unique_handles,
                 sizeof(fattr4_unique_handles));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LEASE_TIME:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_LEASE_TIME");

          lease_time = htonl(NFS4_LEASE_LIFETIME);
          memcpy((char *)(attrvalsBuffer + LastOffset), &lease_time,
                 sizeof(fattr4_lease_time));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RDATTR_ERROR:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_RDATTR_ERROR");

          op_attr_success = htonl(0);   /* not used in a getattr call */
          break;

        case FATTR4_ACL:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_ACL");

          acl.fattr4_acl_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &acl, sizeof(fattr4_acl));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACLSUPPORT:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_ACL_SUPPORT");

#ifdef _USE_NFS4_ACL
          aclsupport = htonl(ACL4_SUPPORT_ALLOW_ACL | ACL4_SUPPORT_DENY_ACL);
#else
          aclsupport = htonl(0);
#endif
          memcpy((char *)(attrvalsBuffer + LastOffset), &aclsupport,
                 sizeof(fattr4_aclsupport));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ARCHIVE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_ARCHIVE");

          /* Archive flag is not supported */
          archive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &archive, sizeof(fattr4_archive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CANSETTIME:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_CANSETTIME");

          /* The time can be set on files */
          cansettime = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &cansettime,
                 sizeof(fattr4_cansettime));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_INSENSITIVE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_CASE_INSENSITIVE");

          /* pseudofs is not case INSENSITIVE... it is Read-Only */
          case_insensitive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_insensitive,
                 sizeof(fattr4_case_insensitive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_PRESERVING:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_PRESERVING");

          /* pseudofs is case preserving... it is Read-Only */
          case_preserving = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_preserving,
                 sizeof(fattr4_case_preserving));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHOWN_RESTRICTED:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_CHOWN_RESTRICTED");

          /* chown is restricted to root, but in fact no chown will be done on pseudofs */
          chown_restricted = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &chown_restricted,
                 sizeof(fattr4_chown_restricted));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILEHANDLE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FILEID");

          /* The analog to the inode number. RFC3530 says "a number uniquely identifying the file within the filesystem" 
           * In the case of a pseudofs entry, the entry's unique id is used */
          file_id = nfs_htonl64((fattr4_fileid) psfsp->pseudo_id);

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_AVAIL:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FILES_AVAIL");

          files_avail = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_avail,
                 sizeof(fattr4_files_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_FREE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FILES_FREE");

          files_free = nfs_htonl64((fattr4_files_avail) 512);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_free,
                 sizeof(fattr4_files_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_TOTAL:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FILES_TOTAL");

          files_total = nfs_htonl64((fattr4_files_avail) 512);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_total,
                 sizeof(fattr4_files_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FS_LOCATIONS:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_FS_LOCATIONS");

          fs_locations.fs_root.pathname4_len = 0;
          fs_locations.locations.locations_len = 0;     /* No FS_LOCATIONS no now */
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HIDDEN:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_HIDDEN");

          /* There are no hidden file in pseudofs */
          hidden = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &hidden, sizeof(fattr4_hidden));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HOMOGENEOUS:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_HOMOGENEOUS");

          /* Unix semantic is homogeneous (all objects have the same kind of attributes) */
          homogeneous = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &homogeneous,
                 sizeof(fattr4_homogeneous));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXFILESIZE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MAXFILESIZE");

          max_filesize = nfs_htonl64((fattr4_maxfilesize) FSINFO_MAX_FILESIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &max_filesize,
                 sizeof(fattr4_maxfilesize));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXLINK:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MAXLINK");

          maxlink = htonl(MAX_HARD_LINK_VALUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxlink, sizeof(fattr4_maxlink));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXNAME:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MAXNAME");

          maxname = htonl((fattr4_maxname) MAXNAMLEN);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxname, sizeof(fattr4_maxname));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXREAD:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MAXREAD");

          maxread = nfs_htonl64((fattr4_maxread) NFS4_PSEUDOFS_MAX_READ_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxread, sizeof(fattr4_maxread));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXWRITE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MAXWRITE");

          maxwrite = nfs_htonl64((fattr4_maxwrite) NFS4_PSEUDOFS_MAX_WRITE_SIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxwrite,
                 sizeof(fattr4_maxwrite));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MIMETYPE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MIMETYPE");

          mimetype.utf8string_len = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &mimetype,
                 sizeof(fattr4_mimetype));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;  /* No supported for the moment */
          break;

        case FATTR4_MODE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MODE");

          file_mode = htonl(0555);      /* Every pseudo fs object is dr-xr-xr-x */
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_mode, sizeof(fattr4_mode));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NO_TRUNC:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_NO_TRUNC");

          /* File's names are not truncated, an error is returned is name is too long */
          no_trunc = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &no_trunc,
                 sizeof(fattr4_no_trunc));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NUMLINKS:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_NUMLINKS");

          /* Reply the number of links found in vattr structure */
          file_numlinks = htonl((fattr4_numlinks) 1);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_numlinks,
                 sizeof(fattr4_numlinks));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_OWNER:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_HARD");

          quota_avail_hard = nfs_htonl64((fattr4_quota_avail_hard) NFS_V4_MAX_QUOTA_HARD);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_hard,
                 sizeof(fattr4_quota_avail_hard));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_AVAIL_SOFT:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_SOFT");

          quota_avail_soft = nfs_htonl64((fattr4_quota_avail_soft) NFS_V4_MAX_QUOTA_SOFT);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_soft,
                 sizeof(fattr4_quota_avail_soft));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_USED:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_QUOTA_AVAIL_USED");

          quota_used = nfs_htonl64((fattr4_quota_used) NFS_V4_MAX_QUOTA);
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_used,
                 sizeof(fattr4_quota_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RAWDEV:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SPACE_AVAIL");

          space_avail = nfs_htonl64(512000LL);  /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_avail,
                 sizeof(fattr4_space_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_FREE:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SPACE_FREE");

          space_free = nfs_htonl64(512000LL);   /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_free,
                 sizeof(fattr4_space_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_TOTAL:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SPACE_TOTAL");

          space_total = nfs_htonl64(1024000LL); /* Fake value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_total,
                 sizeof(fattr4_space_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_USED:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SYSTEM");

          /* This is not a windows system File-System with respect to the regarding API */
          system = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &system, sizeof(fattr4_system));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TIME_ACCESS");

          /* This will contain the object's time os last access, the 'atime' in the Unix semantic */
          memset(&(time_access.seconds), 0, sizeof(int64_t));
          time_access.seconds = nfs_htonl64((int64_t) ServerBootTime);
          time_access.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_access,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS_SET:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TIME_ACCESS_SET");

          /* To be used with NFS4_OP_SETATTR only */
          op_attr_success = 0;
          break;

        case FATTR4_TIME_BACKUP:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TIME_METADATA");

          /* The time for the last metadata operation, the ctime in the unix's semantic */
          memset(&(time_metadata.seconds), 0, sizeof(int64_t));
          time_metadata.seconds = nfs_htonl64((int64_t) ServerBootTime);
          time_metadata.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_metadata,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TIME_MODIFY");

          /* The time for the last modify operation, the mtime in the unix's semantic */
          memset(&(time_modify.seconds), 0, sizeof(int64_t));
          time_modify.seconds = nfs_htonl64((int64_t) ServerBootTime);
          time_modify.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_modify,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY_SET:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_TIME_MODIFY_SET");

          op_attr_success = 0;  /* should never be used here, only for setattr */
          break;

        case FATTR4_MOUNTED_ON_FILEID:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_MOUNTED_ON_FILEID");

          mounted_on_fileid = nfs_htonl64((fattr4_fileid) psfsp->pseudo_id);
          memcpy((char *)(attrvalsBuffer + LastOffset), &mounted_on_fileid,
                 sizeof(fattr4_mounted_on_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

#ifdef _USE_NFS4_1
        case FATTR4_FS_LAYOUT_TYPES:
          layout_types.fattr4_fs_layout_types_len = htonl(1);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &layout_types.fattr4_fs_layout_types_len, sizeof(u_int));
          LastOffset += sizeof(u_int);

          layout_types.fattr4_fs_layout_types_val = layouts;
          layouts[0] = htonl(LAYOUT4_NFSV4_1_FILES);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 layout_types.fattr4_fs_layout_types_val, sizeof(layouttype4));
          LastOffset += sizeof(layouttype4);

          op_attr_success = 1;
          break;
#endif

        default:
	  LogWarn(COMPONENT_NFS_V4_PSEUDO, "Bad file attributes %d queried",
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

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "----------------------------------------");

  /* LastOffset contains the length of the attrvalsBuffer usefull data */
  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
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

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "nfs4_PseudoToFattr (end): Fattr->attr_vals.attrlist4_len = %d",
               Fattr->attr_vals.attrlist4_len);
  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "nfs4_PseudoToFattr (end):Fattr->attrmask.bitmap4_len = %d  [0]=%u, [1]=%u",
               Fattr->attrmask.bitmap4_len, Fattr->attrmask.bitmap4_val[0],
               Fattr->attrmask.bitmap4_val[1]);

  return 0;
}                               /* nfs4_PseudoToFattr */

/**
 * nfs4_FhandleToPseudo: converts  a NFSv4 file handle fs to an id in the pseudo
 * 
 * Converts  a NFSv4 file handle fs to an id in the pseudo, and check if the fh is related to a pseudo entry
 *
 * @param fh4p      [IN] pointer to nfsv4 filehandle
 * @param psfsentry [OUT]  pointer to pseudofs entry
 * 
 * @return TRUE if successfull, FALSE if an error occured (this means the fh4 was not related to a pseudo entry)
 * 
 */
int nfs4_FhandleToPseudo(nfs_fh4 * fh4p, pseudofs_t * psfstree,
                         pseudofs_entry_t * psfsentry)
{
  file_handle_v4_t *pfhandle4;

  /* Map the filehandle to the correct structure */
  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  /* The function must be called with a fh pointed to a pseudofs entry */
  if(pfhandle4->pseudofs_flag == FALSE)
    return FALSE;

  /* Get the object pointer by using the reverse tab in the pseudofs structure */
  memcpy(psfsentry, psfstree->reverse_tab[pfhandle4->pseudofs_id],
         sizeof(pseudofs_entry_t));

  return TRUE;
}                               /* nfs4_FhandleToPseudo */

/**
 * nfs4_PseudoToFhandle: converts an id in the pseudo fs to a NFSv4 file handle
 * 
 * Converts an id in the pseudo fs to a NFSv4 file handle. 
 *
 * @param fh4p      [OUT] pointer to nfsv4 filehandle
 * @param psfsentry [IN]  pointer to pseudofs entry
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

int nfs4_PseudoToFhandle(nfs_fh4 * fh4p, pseudofs_entry_t * psfsentry)
{
  file_handle_v4_t fhandle4;

  memset(&fhandle4, 0, sizeof(fhandle4));

  fhandle4.pseudofs_flag = TRUE;
  fhandle4.pseudofs_id = psfsentry->pseudo_id;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "PSEUDO_TO_FH: Pseudo id = %d -> %d",
               psfsentry->pseudo_id, fhandle4.pseudofs_id);

  memcpy(fh4p->nfs_fh4_val, &fhandle4, sizeof(fhandle4));
  fh4p->nfs_fh4_len = sizeof(file_handle_v4_t);

  return TRUE;
}                               /* nfs4_PseudoToFhandle */

/**
 * nfs4_CreateROOTFH: Creates the file handle for the "/" of the pseudo file system
 *
 *  Creates the file handle for the "/" of the pseudo file syste.
 * 
 * @param fh4p [OUT]   pointer to the file handle to be allocated
 * @param data [INOUT] pointer to the compound request's data
 * 
 * @return NFS4_OK if successfull, NFS4ERR_BADHANDLE if an error occured when creating the file handle.
 *
 */

int nfs4_CreateROOTFH4(nfs_fh4 * fh4p, compound_data_t * data)
{
  pseudofs_entry_t psfsentry;
  int status = 0;

  psfsentry = *(data->pseudofs->reverse_tab[0]);

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "CREATE ROOTFH (pseudo): root to pseudofs = #%s#",
               psfsentry.name);

  if((status = nfs4_AllocateFH(&(data->rootFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->rootFH), &psfsentry))
    {
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "CREATE ROOTFH (pseudo): Creation of root fh is impossible");
      return NFS4ERR_BADHANDLE;
    }

  /* Test */
  if(isFullDebug(COMPONENT_NFS_V4))
    {
      char str[LEN_FH_STR];
      sprint_fhandle4(str, &data->rootFH);
      LogFullDebug(COMPONENT_NFS_V4, "CREATE ROOT FH: %s", str);
    }

  return NFS4_OK;
}                               /* nfs4_CreateROOTFH4 */

/**
 * nfs4_op_getattr_pseudo: Gets attributes for directory in pseudo fs
 * 
 * Gets attributes for directory in pseudo fs. These are hardcoded constants. 
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

int nfs4_op_getattr_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  pseudofs_entry_t psfsentry;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getattr";

  resp->resop = NFS4_OP_GETATTR;

  /* Get the pseudo entry related to this fhandle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_GETATTR4.status = NFS4ERR_BADHANDLE;
      return res_GETATTR4.status;
    }

  /* All directories in pseudo fs have the same Fattr */
  if(nfs4_PseudoToFattr(&psfsentry,
                        &(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                        data, &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
    res_GETATTR4.status = NFS4ERR_SERVERFAULT;
  else
    res_GETATTR4.status = NFS4_OK;

  LogFullDebug(COMPONENT_NFS_V4,
               "Apres nfs4_PseudoToFattr: attrmask(bitmap4_len)=%d attrlist4_len=%d",
               res_GETATTR4.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len,
               res_GETATTR4.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len);

  return res_GETATTR4.status;
}                               /* nfs4_op_getattr */

/**
 * nfs4_op_access_pseudo: Checks for object accessibility in pseudo fs. 
 * 
 * Checks for object accessibility in pseudo fs. All entries in pseudo fs return can't be accessed as 
 * ACCESS4_MODIFY|ACCESS4_EXTEND|ACCESS4_DELETE because pseudo fs is behaving as a read-only fs.
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

int nfs4_op_access_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_access_pseudo";

  resp->resop = NFS4_OP_ACCESS;

  /* All access types are supported */
  res_ACCESS4.ACCESS4res_u.resok4.supported = ACCESS4_READ | ACCESS4_LOOKUP;

  /* DELETE/MODIFY/EXTEND are not supported in the pseudo fs */
  res_ACCESS4.ACCESS4res_u.resok4.access =
      arg_ACCESS4.access & ~(ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE);

  return NFS4_OK;
}                               /* nfs4_op_access_pseudo */

/**
 * nfs4_op_lookup_pseudo: looks up into the pseudo fs.
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

int nfs4_op_lookup_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  char name[MAXNAMLEN];
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookup_pseudo";
  pseudofs_entry_t psfsentry;
  pseudofs_entry_t *iter = NULL;
  int found = FALSE;
  int pseudo_is_slash = FALSE ;
  int error = 0;
  cache_inode_status_t cache_status = 0;
  fsal_status_t fsal_status;
  cache_inode_fsal_data_t fsdata;
  fsal_path_t exportpath_fsal;
  char pathfsal[MAXPATHLEN] ;
  fsal_attrib_list_t attr;
  fsal_handle_t fsal_handle;
#ifdef _USE_MFSL
  mfsl_object_t mobject;
#endif
  cache_entry_t *pentry = NULL;
  fsal_mdsize_t strsize = MNTPATHLEN + 1;

  resp->resop = NFS4_OP_LOOKUP;

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(name, sizeof(name), &arg_LOOKUP4.objname);

  /* Get the pseudo fs entry related to the file handle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_LOOKUP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUP4.status;
    }

 
  /* If "/" is set as pseudopath, then gPseudoFS.root.junction_export is not NULL but 
   * gPseudoFS.root has no son */
  if( ( gPseudoFs.root.junction_export != NULL ) && ( gPseudoFs.root.sons == NULL )  )
   {
	iter = &gPseudoFs.root ;
        pseudo_is_slash = TRUE ;
        found = TRUE ;
   }
  else
   {
     found = FALSE;
     for(iter = psfsentry.sons; iter != NULL; iter = iter->next)
       {
         if(!strcmp(iter->name, name))
           {
             found = TRUE;
             break;
           } 
       } /* for */
    } /* else */

  if(!found)
    {
      res_LOOKUP4.status = NFS4ERR_NOENT;
      return res_LOOKUP4.status;
    }

  /* A matching entry was found */
  if(iter->junction_export == NULL)
    {
      /* The entry is not a junction, we stay within the pseudo fs */
      if(!nfs4_PseudoToFhandle(&(data->currentFH), iter))
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }
    }
  else
    {
#ifdef _USE_SHARED_FSAL 
      /* Set the FSAL ID here */
      FSAL_SetId( iter->junction_export->fsalid ) ;
#endif

      /* The entry is a junction */
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,      
                   "A junction in pseudo fs is traversed: name = %s, id = %d",
                   iter->name, iter->junction_export->id);
      data->pexport = iter->junction_export;
      strncpy(data->MntPath, iter->fullname, NFS_MAXPATHLEN);

      /* Build credentials */
      if(nfs4_MakeCred(data) != 0)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_LOOKUP4.status = NFS4ERR_WRONGSEC;
          return res_LOOKUP4.status;
        }

      /* Build fsal data for creation of the first entry */
      strncpy( pathfsal, data->pexport->fullpath, MAXPATHLEN ) ;

      if( pseudo_is_slash == TRUE )
       {
         strncat( pathfsal, "/", MAXPATHLEN ) ;
         strncat( pathfsal, name, MAXPATHLEN - strlen(pathfsal)) ;
       }

      if(FSAL_IS_ERROR
         ((fsal_status =
           FSAL_str2path( pathfsal, strsize, &exportpath_fsal))))
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Lookup the FSAL to build the fsal handle */
#ifdef _USE_MFSL
      if(FSAL_IS_ERROR(fsal_status = MFSL_lookupPath(&exportpath_fsal,
                                                     data->pcontext,
                                                     &data->pclient->mfsl_context,
                                                     &mobject, NULL)))
#else
      if(FSAL_IS_ERROR(fsal_status = FSAL_lookupPath(&exportpath_fsal,
                                                     data->pcontext, &fsal_handle, NULL)))
#endif
        {
	  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d )",
                   fsal_status.major, fsal_status.minor);
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }
#ifdef _USE_MFSL
      fsal_handle = mobject.handle;
#endif

      if(data->mounted_on_FH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->mounted_on_FH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the 'mounted on' file handle");
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              return res_LOOKUP4.status;
            }
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the first file handle");
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              return res_LOOKUP4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, &fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to build the first file handle");
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* The new fh is to be the "mounted on Filehandle" */
      memcpy(data->mounted_on_FH.nfs_fh4_val, data->currentFH.nfs_fh4_val,
             sizeof(file_handle_v4_t));
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Add the entry to the cache as a root (BUGAZOMEU: make it a junction entry when junction is available) */
      fsdata.handle = fsal_handle;
      fsdata.cookie = 0;
      if((pentry = cache_inode_make_root(&fsdata,
                                         data->pexport->cache_inode_policy,
                                         data->ht,
                                         ((cache_inode_client_t *) data->pclient),
                                         data->pcontext, &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Allocate root entry in cache inode failed, for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Get the attributes (costless: the attributes was cached when the root pentry was created */
      if(cache_inode_getattr(pentry,
                             &attr,
                             data->ht,
                             ((cache_inode_client_t *) data->pclient),
                             data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get attributes for root pentry");
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Keep the pentry within the compound data */
      data->current_entry = pentry;
      data->current_filetype = cache_inode_fsal_type_convert(attr.type);

    }                           /* else */


  res_LOOKUP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookup_pseudo */

/**
 * nfs4_op_lookupp_pseudo: looks up into the pseudo fs for the parent directory
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

int nfs4_op_lookupp_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookup_pseudo";
  pseudofs_entry_t psfsentry;

  resp->resop = NFS4_OP_LOOKUPP;

  /* Get the pseudo fs entry related to the file handle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_LOOKUPP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUPP4.status;
    }

  /* lookupp on the root on the pseudofs should return NFS4ERR_NOENT (RFC3530, page 166) */
  if(!memcmp(&psfsentry, data->pseudofs->reverse_tab[0], sizeof(psfsentry)))
    {
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* A matching entry was found */
  if(!nfs4_PseudoToFhandle(&(data->currentFH), psfsentry.parent))
    {
      res_LOOKUPP4.status = NFS4ERR_SERVERFAULT;
      return res_LOOKUPP4.status;
    }

  /* Copy this to the mounted on FH (if no junction is traversed */
  memcpy((char *)(data->mounted_on_FH.nfs_fh4_val),
         (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len);
  data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

  /* Keep the vnode pointer within the data compound */
  data->current_entry = NULL;
  data->current_filetype = UNASSIGNED;

  res_LOOKUPP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookupp_pseudo */

/**
 * nfs4_op_readdir_pseudo: Reads a directory in the pseudo fs 
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

int nfs4_op_readdir_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  unsigned long dircount = 0;
  unsigned long maxcount = 0;
  unsigned long estimated_num_entries = 0;
  unsigned long i = 0;
  nfs_cookie4 cookie;
  verifier4 cookie_verifier;
  unsigned long space_used = 0;
  pseudofs_entry_t psfsentry;
  pseudofs_entry_t *iter = NULL;
  entry4 *entry_nfs_array = NULL;
  entry_name_array_item_t *entry_name_array = NULL;
  nfs_fh4 entryFH;
  cache_inode_fsal_data_t fsdata;
  fsal_path_t exportpath_fsal;
  fsal_attrib_list_t attr;
  fsal_handle_t fsal_handle;
#ifdef _USE_MFSL
  mfsl_object_t mobject;
#endif
  fsal_mdsize_t strsize = MNTPATHLEN + 1;
  fsal_status_t fsal_status;
  int error = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t *pentry = NULL;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_readdir_pseudo";

  bitmap4 RdAttrErrorBitmap = { 1, (uint32_t *) "\0\0\0\b" };   /* 0xB = 11 = FATTR4_RDATTR_ERROR */
  attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  entryFH.nfs_fh4_len = 0;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "PSEUDOFS READDIR: dircount=%lu, maxcount=%lu, cookie=%"PRIu64", sizeof(entry4)=%lu num_entries=%lu",
               dircount, maxcount, (uint64_t)cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Now resolve the file handle to pseudo fs */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_READDIR4.status = NFS4ERR_BADHANDLE;
      return res_READDIR4.status;
    }
  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "PSEUDOFS READDIR in #%s#", psfsentry.name);

  /* If this a junction filehandle ? */
  if(psfsentry.junction_export != NULL)
    {
      /* This is a junction */
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDOFS READDIR : DIR #%s# id=%u is a junction",
                   psfsentry.name, psfsentry.junction_export->id);

      /* Step up the compound data */
      data->pexport = psfsentry.junction_export;
      strncpy(data->MntPath, psfsentry.fullname, NFS_MAXPATHLEN);

      /* Build the credentials */
      if(nfs4_MakeCred(data) != 0)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_READDIR4.status = NFS4ERR_WRONGSEC;
          return res_READDIR4.status;
        }
      /* Build fsal data for creation of the first entry */
      if(FSAL_IS_ERROR
         ((fsal_status =
           FSAL_str2path(data->pexport->fullpath, strsize, &exportpath_fsal))))
        {
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* Lookup the FSAL to build the fsal handle */
#ifdef _USE_MFSL
      if(FSAL_IS_ERROR(fsal_status = MFSL_lookupPath(&exportpath_fsal,
                                                     data->pcontext,
                                                     &data->pclient->mfsl_context,
                                                     &mobject, NULL)))
#else
      if(FSAL_IS_ERROR(fsal_status = FSAL_lookupPath(&exportpath_fsal,
                                                     data->pcontext, &fsal_handle, NULL)))
#endif
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d )",
                   fsal_status.major, fsal_status.minor);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }
#ifdef _USE_MFSL
      fsal_handle = mobject.handle;
#endif

      if(data->mounted_on_FH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->mounted_on_FH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the 'mounted on' file handle");
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the first file handle");
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, &fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to build the first file handle");
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* The new fh is to be the "mounted on Filehandle" */
      memcpy(data->mounted_on_FH.nfs_fh4_val, data->currentFH.nfs_fh4_val,
             sizeof(file_handle_v4_t));
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Add the entry to the cache as a root (BUGAZOMEU: make it a junction entry when junction is available) */
      fsdata.handle = fsal_handle;
      fsdata.cookie = 0;
      if((pentry = cache_inode_make_root(&fsdata,
                                         data->pexport->cache_inode_policy,
                                         data->ht,
                                         ((cache_inode_client_t *) data->pclient),
                                         data->pcontext, &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Allocate root entry in cache inode failed, for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* Get the attributes (costless: the attributes was cached when the root pentry was created */
      if(cache_inode_getattr(pentry,
                             &attr,
                             data->ht,
                             ((cache_inode_client_t *) data->pclient),
                             data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get attributes for root pentry");
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* Keep the pentry within the compound data */
      data->current_entry = pentry;
      data->current_filetype = cache_inode_fsal_type_convert(attr.type);

      /* redo the call on the other side of the junction */
      return nfs4_op_readdir(op, data, resp);
    }

  /* Allocation of the entries array */
  if((entry_name_array =
      (entry_name_array_item_t *) Mem_Alloc(estimated_num_entries *
                                            (FSAL_MAX_NAME_LEN + 1))) == NULL)
    {
      LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }
  memset((char *)entry_name_array, 0, estimated_num_entries * (FSAL_MAX_NAME_LEN + 1));

  if((entry_nfs_array =
      (entry4 *) Mem_Alloc(estimated_num_entries * sizeof(entry4))) == NULL)
    {
      LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
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
  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and HPSS cookie */

  /* make sure to start at the right position given by the cookie */
  iter = psfsentry.sons;
  if(cookie != 0)
    {
      for(; iter != NULL; iter = iter->next)
        if((iter->pseudo_id + 3) == cookie)
          break;
    }

  /* Here, where are sure that iter is set to the position indicated eventually by the cookie */
  i = 0;
  for(; iter != NULL; iter = iter->next)
    {
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS: Found entry %s", iter->name);

      entry_nfs_array[i].name.utf8string_len = strlen(iter->name);
      strncpy(entry_name_array[i], iter->name, FSAL_MAX_NAME_LEN);

      entry_nfs_array[i].name.utf8string_val = entry_name_array[i];
      entry_nfs_array[i].cookie = iter->pseudo_id + 3;

      /* If file handle is asked in the attributes, provide it */
      if(arg_READDIR4.attr_request.bitmap4_val[0] & FATTR4_FILEHANDLE)
        {
          if(entryFH.nfs_fh4_len == 0)
            {
              if(nfs4_AllocateFH(&entryFH) != NFS4_OK)
                {
                  return res_READDIR4.status;
                }
            }

          if(!nfs4_PseudoToFhandle(&entryFH, iter))
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              Mem_Free(entry_nfs_array);
              return res_READDIR4.status;
            }
        }

      if(nfs4_PseudoToFattr(iter,
                            &(entry_nfs_array[i].attrs),
                            data, &entryFH, &(arg_READDIR4.attr_request)) != 0)
        {
          /* Should never occured, but the is no reason for leaving the section without any information */
          entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
          entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
        }

      /* Chain the entry together */
      entry_nfs_array[i].nextentry = NULL;
      if(i != 0)
        entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);

      /* Increment the counter */
      i += 1;

      /* Did we reach the maximum number of entries */
      if(i == estimated_num_entries)
        break;
    }

  /* Resize entry_nfs_array */
  /* @todo : Is this reallocation actually needed ? */
#ifdef BUGAZOMEU
  if(i < estimated_num_entries)
    if((entry_nfs_array = Mem_Realloc_Label(entry_nfs_array, i * sizeof(entry4),
                                            "entry4")) == NULL)
      {
        LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
        res_READDIR4.status = NFS4ERR_SERVERFAULT;
        Mem_Free(entry_nfs_array);
        return res_READDIR4.status;
      }
#endif
  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if(i == 0)
    res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
  else
    res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;

  /* did we reach the end ? */
  if(iter == NULL)
    {
      /* Yes, we did */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
    }
  else
    {
      /* No, there are some more entries */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;
    }

  /* Exit properly */
  res_READDIR4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_readdir_pseudo */
