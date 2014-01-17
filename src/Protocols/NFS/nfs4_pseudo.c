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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "city.h"
#include "cache_inode_avl.h"
#include "avltree.h"
#include "common_utils.h"

#define NB_TOK_PATH 128

static pseudofs_t gPseudoFs;
hash_table_t *ht_nfs4_pseudo;
#define V4_FH_OPAQUE_SIZE (sizeof(struct alloc_file_handle_v4 ) - sizeof(struct file_handle_v4))

/**
 * nfs4_GetPseudoFs: Gets the root of the pseudo file system.
 * 
 * Gets the root of the pseudo file system. This is only a wrapper to static variable gPseudoFs. 
 *
 * @return the pseudo fs root 
 */
pseudofs_t *nfs4_GetPseudoFs(void)
{
  return &gPseudoFs;
}                               /*  nfs4_GetExportList */

/**                                                                    
 * @brief Free the opaque nfsv4 handle used as a key in the pseudofs   
 *                                                                     
 * This will free the opaque nfsv4 handle used as a key in the pseudofs
 * hashtable. It is passed to the hashtable during initialization.     
 *                                                                     
 * @param[in] key the key used in pseudofs hashtable                   
 *                                                                     
 * @return void                                                        
 */
void free_pseudo_handle_key(hash_buffer_t key)
{
  gsh_free(key.pdata);
}

/**                                                                      
 * @brief Construct the fs opaque part of a pseudofs nfsv4 handle        
 *                                                                       
 * Given the components of a pseudofs nfsv4 handle, the nfsv4 handle is  
 * created by concatenating the components. This is the fs opaque piece  
 * of struct file_handle_v4 and what is sent over the wire.              
 *                                                                       
 * @param[in] pseudopath Full patch of the pseudofs node                 
 * @param[in] len length of the pseudopath parameter                     
 * @param[in] hashkey a 64 bit hash of the pseudopath parameter          
 *                                                                       
 * @return The nfsv4 pseudofs file handle as a char *                    
 */
char *package_pseudo_handle(char *pseudopath, ushort len, uint64 hashkey)
{
  char *buff = NULL;
  int opaque_bytes_used=0,pathlen=0;

  /* This is the size of the v4 file handle opaque area used for pseudofs or
   * FSAL file handles. */
  buff = gsh_malloc(V4_FH_OPAQUE_SIZE); 
  if (buff == NULL)
    {
      LogCrit(COMPONENT_NFS_V4_PSEUDO,
              "Failed to malloc space for pseudofs handle.");
      return NULL;
    }

  memcpy(buff, &hashkey, sizeof(hashkey));
  opaque_bytes_used += sizeof(hashkey);

  /* include length of the path in the handle */
  /* MAXPATHLEN=4096 ... max path length can be contained in a short int. */
  memcpy(buff + opaque_bytes_used, &len, sizeof(ushort));
  opaque_bytes_used += sizeof(ushort);

  /* Either the nfsv4 fh opaque size or the length of the pseudopath. Ideally
   * we can include entire pseudofs pathname for guaranteed uniqueness of
   * pseudofs handles. */
  pathlen = min(V4_FH_OPAQUE_SIZE - opaque_bytes_used, len);
  memcpy(buff + opaque_bytes_used, pseudopath, pathlen);
  opaque_bytes_used += pathlen;

  /* If there is more space in the opaque handle due to a short pseudofs path
   * ... zero it. */
  if (opaque_bytes_used < V4_FH_OPAQUE_SIZE)
    {
      memset(buff + opaque_bytes_used, 0,
             V4_FH_OPAQUE_SIZE - opaque_bytes_used);
    }

  return buff;
}

/**                                                                      
 * @brief Creates a hashtable key for a pseudofs node given the fullpath.
 *                                                                       
 * Creates a hashtable key for a pseudofs node given the fullpath.       
 *                                                                       
 * @param[in] pseudopath Full path of the pseudofs node                  
 * @param[in] len Length of the full path                                
 *                                                                       
 * @return the key                                                       
 */
hash_buffer_t create_pseudo_handle_key(char *pseudopath, int len)
{
  hash_buffer_t key;
  uint64 hashkey;

  hashkey = CityHash64(pseudopath, len);
  key.pdata = package_pseudo_handle(pseudopath, (ushort)len, hashkey);
  key.len = V4_FH_OPAQUE_SIZE;

  /* key.pdata == NULL upon error */
  return key;
}

/**                                                                         
 * @brief Compares the name attribute contained in pseudofs avltree keys    
 *                                                                          
 * Compares the name attribute contained in pseudofs avltree keys. The      
 * key will be of type pseudofs_entry_t. avltree nodes are compared by name 
 * and name length.                                                         
 *                                                                          
 * @param[in] lhs left hande side psuedofs entry key                        
 * @param[in] rhs right hande side psuedofs entry key                       
 *                                                                          
 * @return -1 if rhs is bigger, 1 if lhs is bigger, 0 if they are the same. 
 */
static inline int avl_pseudo_name_cmp(const struct avltree_node *lhs,
                                      const struct avltree_node *rhs)
{
  pseudofs_entry_t *lk, *rk;
  int llen,rlen,res;
  lk = avltree_container_of(lhs, pseudofs_entry_t, nameavlnode);
  rk = avltree_container_of(rhs, pseudofs_entry_t, nameavlnode);

  llen = strlen(lk->name);
  rlen = strlen(rk->name);

  if (llen < rlen)
    return (-1);
  if (llen > rlen)
    return (1);

  /* Compare strings*/
  res = strncmp(lk->name, rk->name, llen);
  if (res < 0) /* lk->name is bigger */
    return 1;
  if (res > 0) /* rk->name is bigger */
    return -1;

  /* Exact same name */
  return 0;
}

/**                                                                          
 * @brief Compares the pseudo_id attribute contained in pseudofs avltree keys
 *                                                                           
 * Compares the pseudo_id attribute contained in pseudofs avltree keys. The  
 * key will be of type pseudofs_entry_t.                                     
 * NOTE: There is a chance of a collision. We will not have the node name to 
 * avoid the collision.                                                      
 *                                                                           
 * @param[in] lhs left hande side psuedofs entry key                         
 * @param[in] rhs right hande side psuedofs entry key                        
 *                                                                           
 * @return -1 if rhs is bigger, 1 if lhs is bigger, 0 if they are the same.  
 */
static inline int avl_pseudo_id_cmp(const struct avltree_node *lhs,
                                    const struct avltree_node *rhs)
{
  pseudofs_entry_t *lk, *rk;
  lk = avltree_container_of(lhs, pseudofs_entry_t, idavlnode);
  rk = avltree_container_of(rhs, pseudofs_entry_t, idavlnode);

  if (lk->pseudo_id < rk->pseudo_id)
    return (-1);
  if (lk->pseudo_id > rk->pseudo_id)
    return (1);
  return 0;
}

/**                                                                          
 * @brief Concatenate a number of pseudofs tokens into a string              
 *                                                                           
 * When reading pseudofs paths from export entries, we divide the            
 * path into tokens. This function will recombine a specific number          
 * of those tokens into a string.                                            
 *                                                                           
 * @param[in/out] fullpseudopath Must be not NULL. Tokens are copied to here.
 * @param[in] PathTok List of token strings                                  
 * @param[in] tok Number of tokens to use for full pseudopath                
 * @param[in] maxlen maximum number of chars to copy to fullpseudopath       
 *                                                                           
 * @return void                                                              
 */
void fullpath(char *fullpseudopath, char **PathTok, int tok, int maxlen)
{
  int currtok, currlen=0;

  fullpseudopath[currlen++] = '/';
  for(currtok=0; currtok<=tok&&currlen<maxlen; currtok++)
    {
      if (currlen + strlen(PathTok[currtok]) > maxlen)
        {
          LogWarn(COMPONENT_NFS_V4_PSEUDO,"Pseudopath length is too long, can't "
                  "create pseudofs node.");
          break;
        }
      strncpy(fullpseudopath+currlen, PathTok[currtok], strlen(PathTok[currtok]));
      currlen += strlen(PathTok[currtok]);
      if (currtok<tok)
        fullpseudopath[currlen++] = '/';
    }
  fullpseudopath[currlen] = '\0';
}

/**
 * nfs4_ExportToPseudoFS: Build a pseudo fs from an exportlist
 * 
 * Build a pseudo fs from an exportlist. This export list itself is obtained by reading the configuration file. 
 *
 * @return the pseudo fs root 
 * 
 */
int nfs4_ExportToPseudoFS(struct glist_head * pexportlist)
{
  hash_buffer_t key, value;
  exportlist_t *entry;
  struct glist_head * glist;
  int j=0, NbTokPath;
  char tmp_pseudopath[MAXPATHLEN+2], fullpseudopath[MAXPATHLEN+2], *PathTok[NB_TOK_PATH];
  pseudofs_entry_t *PseudoFsCurrent=NULL,*newPseudoFsEntry=NULL;
  hash_error_t hrc = 0;
  struct hash_latch latch;
  struct avltree_node *node;

  /* Init Root of the Pseudo FS tree */
  strcpy(gPseudoFs.root.name, "/");
  gPseudoFs.root.junction_export = NULL;

  /* root is its own parent */
  gPseudoFs.root.parent = &gPseudoFs.root;
  avltree_init(&gPseudoFs.root.child_tree_byname, avl_pseudo_name_cmp, 0);
  avltree_init(&gPseudoFs.root.child_tree_byid, avl_pseudo_id_cmp, 0);

  key = create_pseudo_handle_key(gPseudoFs.root.name, strlen(gPseudoFs.root.name));
  if(isFullDebug(COMPONENT_NFS_V4_PSEUDO))
    {
      char str[256];
      sprint_mem(str, key.pdata, key.len);
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,"created key for path:%s handle:%s",
                   gPseudoFs.root.name,str);
    }

  gPseudoFs.root.fsopaque = (uint8_t *)key.pdata;
  gPseudoFs.root.pseudo_id = *(uint64_t *)key.pdata;
  value.pdata = &gPseudoFs.root;
  value.len = sizeof(gPseudoFs.root);
  hrc = HashTable_Test_And_Set(ht_nfs4_pseudo, &key, &value,
                               HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
  if(hrc != HASHTABLE_SUCCESS)
    {
      LogCrit(COMPONENT_NFS_V4_PSEUDO,
              "Failed to add ROOT pseudofs path %s due to hashtable error: %s",
              gPseudoFs.root.name, hash_table_err_to_str(hrc));
      free_pseudo_handle_key(key);
      return -1;
    }

  LogDebug(COMPONENT_NFS_V4_PSEUDO, "Added root pseudofs node to hashtable");

  glist_for_each(glist, pexportlist)
    {
      entry = glist_entry(glist, exportlist_t, exp_list);

      /* skip exports that aren't for NFS v4 */
      if((entry->export_perms.options & EXPORT_OPTION_NFSV4) == 0)
        continue;

      if(entry->export_perms.options & EXPORT_OPTION_PSEUDO)
        {
          LogDebug(COMPONENT_NFS_V4_PSEUDO,
                   "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s",
                   entry->id, entry->fullpath, entry->pseudopath);

          /* there must be a leading '/' in the pseudo path */
          if(entry->pseudopath[0] != '/')
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "Pseudo Path '%s' is badly formed",
                      entry->pseudopath);
              continue;
            }

          /* Parsing the path */
          memset(PathTok, 0, sizeof(PathTok));

          /* Make a copy of the pseudopath since it will be modified,
           * also, skip the leading '/'.
           */
          strcpy(tmp_pseudopath, entry->pseudopath + 1);

          NbTokPath = nfs_ParseConfLine(PathTok,
                                        NB_TOK_PATH,
                                        sizeof(gPseudoFs.root.name),
                                        tmp_pseudopath,
                                        '/');
          if(NbTokPath < 0)
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "Bad Pseudo=\"%s\", path too long or a component is too long",
                      entry->pseudopath);
              continue;
            }

          /* Start at the pseudo root. */
          PseudoFsCurrent = &(gPseudoFs.root);

          /* Loop on each token. */
          for(j = 0; j < NbTokPath; j++)
            LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "tokens are %s", PathTok[j]);

          for(j = 0; j < NbTokPath; j++)
            {
              /* Pseudofs path */
              fullpath(fullpseudopath, PathTok, j, MAXPATHLEN);
              key = create_pseudo_handle_key(fullpseudopath, strlen(fullpseudopath));

              if(isFullDebug(COMPONENT_NFS_V4_PSEUDO))
                {
                  char str[256];
                  sprint_mem(str, key.pdata, key.len);
                  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,"created key for path:%s handle:%s",
                               fullpseudopath,str);
                }
              /* Now we create the pseudo entry */
              newPseudoFsEntry = gsh_malloc(sizeof(pseudofs_entry_t));
              if(newPseudoFsEntry == NULL)
                {
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                           "Insufficient memory to create pseudo fs node");
                  return ENOMEM;
                }
              /* We will fill in the newPseudoFsEntry after we know it doesn't
               * already exist. */
              value.pdata = newPseudoFsEntry;
              value.len = sizeof(newPseudoFsEntry);

              /* Looking for a matching entry and creating if nonexistent */
              hrc = HashTable_Test_And_Set(ht_nfs4_pseudo, &key, &value,
                                           HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
              if(hrc != HASHTABLE_SUCCESS && hrc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
                {
                  LogCrit(COMPONENT_NFS_V4_PSEUDO,
                          "Failed to add pseudofs path %s due to hashtable error: %s",
                          newPseudoFsEntry->name, hash_table_err_to_str(hrc));
                  return -1;
                }
              if (hrc == HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
                {
                  LogDebug(COMPONENT_NFS_V4_PSEUDO,
                           "Failed to add pseudofs path, path already exists: %s",
                           newPseudoFsEntry->name);
                  
                  /* Now set PseudoFsCurrent to existing entry. */
                  hrc = HashTable_GetLatch(ht_nfs4_pseudo, &key, &value,
                                           FALSE, &latch);
                  if ((hrc != HASHTABLE_SUCCESS))
                    {
                      /* This should not happened */
                      LogCrit(COMPONENT_NFS_V4_PSEUDO, "Can't add/get key for %s"
                              " hashtable error: %s",
                              newPseudoFsEntry->name, hash_table_err_to_str(hrc));
                      free_pseudo_handle_key(key);
                      gsh_free(newPseudoFsEntry);
                      return -1;
                    }
                  else
                    {
                      /* Now we have the cached pseudofs entry*/
                      PseudoFsCurrent = value.pdata;

                      /* Release the lock ... we should be calling this funciton
                       * in a serial fashion before Ganesha is operational. No
                       * chance of contention. */
                      HashTable_ReleaseLatched(ht_nfs4_pseudo, &latch);
                    }
                  /* Free the key and value that we weren't able to add. */
                  free_pseudo_handle_key(key);
                  gsh_free(newPseudoFsEntry);
                  continue;
                }

              /* Creating the new pseudofs entry */
              strncpy(newPseudoFsEntry->name, PathTok[j], strlen(PathTok[j]));
              newPseudoFsEntry->name[strlen(PathTok[j])] = '\0';
              newPseudoFsEntry->fsopaque = (uint8_t *)key.pdata;
              newPseudoFsEntry->pseudo_id = *(uint64_t *)key.pdata;
              newPseudoFsEntry->junction_export = NULL;
              newPseudoFsEntry->parent = PseudoFsCurrent;
              avltree_init(&newPseudoFsEntry->child_tree_byname,avl_pseudo_name_cmp, 0);
              avltree_init(&newPseudoFsEntry->child_tree_byid,avl_pseudo_id_cmp, 0);

              LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
                          "Creating pseudo fs entry for %s, pseudo_id %"PRIu64,
                          newPseudoFsEntry->name, newPseudoFsEntry->pseudo_id);

              /* Insert new pseudofs entry into tree */
              node = avltree_insert(&newPseudoFsEntry->nameavlnode,
                                    &PseudoFsCurrent->child_tree_byname);
              node = avltree_insert(&newPseudoFsEntry->idavlnode,
                                    &PseudoFsCurrent->child_tree_byid);

              PseudoFsCurrent = newPseudoFsEntry;
            } /* for j */


          /* Now that all entries are added to pseudofs tree, add the junction to the pseudofs */
          PseudoFsCurrent->junction_export = entry;

          /* And fill in our part of the export root data */
          entry->exp_mounted_on_file_id = PseudoFsCurrent->pseudo_id;
        }
    }                           /* while( entry ) */
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
  fattr4_maxread maxread;
  fattr4_maxwrite maxwrite;
  fattr4_maxname maxname;
  fattr4_maxlink maxlink;
  fattr4_homogeneous homogeneous;
  fattr4_acl acl;
  fattr4_rdattr_error rdattr_error;
  fattr4_mimetype mimetype;
  fattr4_aclsupport aclsupport;
  fattr4_quota_avail_hard quota_avail_hard;
  fattr4_quota_avail_soft quota_avail_soft;
  fattr4_quota_used quota_used;
  fattr4_mounted_on_fileid mounted_on_fileid;
#ifdef _USE_NFS4_1
  fattr4_fs_layout_types layout_types;
  layouttype4 layouts[1];
#endif

  u_int fhandle_len = 0;
  unsigned int LastOffset;
  unsigned int len = 0, off = 0;        /* Use for XDR alignment */
  int op_attr_success = 0;
  unsigned int i = 0;
  unsigned int j = 0;
  unsigned int attrmasklen = 0;
  unsigned int attribute_to_set = 0;

#ifdef _USE_NFS4_1
  unsigned int attrmasklist[FATTR4_FS_CHARSET_CAP];     /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
  unsigned int attrvalslist[FATTR4_FS_CHARSET_CAP];     /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
#else
  unsigned int attrmasklist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  unsigned int attrvalslist[FATTR4_MOUNTED_ON_FILEID];  /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
#endif
  char attrvalsBuffer[ATTRVALS_BUFFLEN];

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
                   "Flag for Operation (Pseudo) = %d|%d is ON,  name  = %s  reply_size = %d supported = %d",
                   attrmasklist[i], fattr4tab[attribute_to_set].val,
                   fattr4tab[attribute_to_set].name,
                   fattr4tab[attribute_to_set].size_fattr4,
                   fattr4tab[attribute_to_set].supported);

      op_attr_success = 0;

      /* compute the new size for the fattr4 reply */
      /* This space is to be filled below in the big switch/case statement */

      switch (attribute_to_set)
        {
        case FATTR4_SUPPORTED_ATTRS:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_SUPPORTED_ATTRS");

          LastOffset += nfs4_supported_attrs_to_fattr(attrvalsBuffer+LastOffset);

          /* This kind of operation is always a success */
          op_attr_success = 1;
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

          /* For the moment, we handle only the persistent filehandle 
          expire_type = htonl(FH4_VOLATILE_ANY); */
          expire_type = htonl( FH4_PERSISTENT ) ; 
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

          lease_time = htonl(nfs_param.nfsv4_param.lease_lifetime);
          memcpy((char *)(attrvalsBuffer + LastOffset), &lease_time,
                 sizeof(fattr4_lease_time));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RDATTR_ERROR:
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "-----> Wanting FATTR4_RDATTR_ERROR");

          rdattr_error = htonl(NFS4_OK);
          memcpy((char *)(attrvalsBuffer + LastOffset), &rdattr_error,
                 sizeof(fattr4_rdattr_error));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
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

          if(objFH == NULL)
            {
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "No file handle provided for attributes");
              op_attr_success = 0;
              break;
            }

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
/* RFC 3530: "When the fs_locations attribute is interrogated and there are no
   alternate file system locations, the server SHOULD return a zero-
   length array of fs_location4 structures, together with a valid
   fs_root. The code below does not return a fs_root which causes client
   problems when they interrogate this attribute. For now moving attribute to
   unsupported.
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
*/
          op_attr_success = 0;
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
          if(uid2utf8(NULL, NULL, NFS4_ROOT_UID, &file_owner) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner.utf8string_len % 4;
/* Following code used to add deltalen to utf8len which is wrong. It caused
 * clients verifying utf8 strings to reject the attribute.
 */
              utf8len = htonl(file_owner.utf8string_len);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner.utf8string_val, file_owner.utf8string_len);
              LastOffset += file_owner.utf8string_len;

              /* Free what was allocated by uid2utf8 */
              free_utf8(&file_owner);

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
          if(gid2utf8(NULL, NULL, 2, &file_owner_group) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner_group.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner_group.utf8string_len % 4;

/* Following code used to add deltalen to utf8len which is wrong. It caused
 * clients verifying utf8 strings to reject the attribute.
 */
              utf8len = htonl(file_owner_group.utf8string_len);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner_group.utf8string_val, file_owner_group.utf8string_len);
              LastOffset += file_owner_group.utf8string_len;

              /* Free what was used for utf8 conversion */
              free_utf8(&file_owner_group);

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
          /* MOUNTED_ON_FILEID is the same as FILEID unless this entry
           * is the root of an export. But since the pseudo fs is not
           * mounted on anything, this value will always be the same as
           * FILEID. The root is fileid 0 anyway, which is what we would
           * use for the MOUNTED_ON_FILEID anyway.
           */
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

  return nfs4_Fattr_Fill(Fattr, j, attrvalslist, LastOffset, attrvalsBuffer);
}                               /* nfs4_PseudoToFattr */

/**
 * nfs4_CurrentFHToPseudo: converts CurrentFH to an id in the pseudo
 * 
 * Converts  a NFSv4 file handle fs to an id in the pseudo, and check if the fh is related to a pseudo entry
 *
 * @param data      [IN] pointer to compound data
 * @param psfsentry [OUT]  pointer to pseudofs entry
 * 
 * @return Appropriate NFS4ERR or NFS4_OK
 * 
 */
int nfs4_CurrentFHToPseudo(compound_data_t   * data,
                           pseudofs_entry_t ** psfsentry)
{
  file_handle_v4_t *pfhandle4;
  hash_buffer_t key, value;
  hash_error_t hrc = 0;
  struct hash_latch latch;

  /* Map the filehandle to the correct structure */
  pfhandle4 = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* The function must be called with a fh pointed to a pseudofs entry */
  if(pfhandle4 == NULL || pfhandle4->exportid != 0 || /* exportid 0 indicates pseudofs node*/
     pfhandle4->fhversion != GANESHA_FH_VERSION)
    {
      LogDebug(COMPONENT_NFS_V4_PSEUDO,
               "Pseudo fs handle=%p, pseudofs_flag=%d, fhversion=%d",
               pfhandle4,
               pfhandle4 != NULL ? pfhandle4->exportid == 0 : 0,
               pfhandle4 != NULL ? pfhandle4->fhversion : 0);
      return NFS4ERR_BADHANDLE;
    }

  /* Find pseudofs in hashtable */
  /* key generated from pathname and cityhash64 of pathname */
  key.pdata = pfhandle4->fsopaque;
  key.len = pfhandle4->fs_len;

  if(isFullDebug(COMPONENT_NFS_V4_PSEUDO))
    {
      char str[256];
      sprint_mem(str, key.pdata, key.len);
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,"looking up pseudofs node for handle:%s",
                   str);
    }

  hrc = HashTable_GetLatch(ht_nfs4_pseudo, &key, &value, FALSE, &latch);
  if ((hrc != HASHTABLE_SUCCESS))
    {
      /* This should not happen */
      LogDebug(COMPONENT_NFS_V4_PSEUDO, "Can't get key for FHToPseudo conversion"
               ", hashtable error %s", hash_table_err_to_str(hrc));
      *psfsentry = NULL;
    } else
    {
      *psfsentry = value.pdata;
      
      /* Release the lock ... This is a read-only hashtable and entry.
       * It's possible we reload exports ... but in that case we
       * catch the worker threads at a safe location where we aren't
       * using any export entries. */
      HashTable_ReleaseLatched(ht_nfs4_pseudo, &latch);
    }

  /* If an export was removed and we restarted or reloaded exports then the
   * PseudoFS entry corresponding to a handle might not exist now.
   */
  if(*psfsentry == NULL)
    return NFS4ERR_STALE;

  return NFS4_OK;
}                               /* nfs4_CurrentFHToPseudo */

/**
 * nfs4_PseudoToFhandle: converts an id in the pseudo fs to a NFSv4 file handle
 * 
 * Converts an id in the pseudo fs to a NFSv4 file handle. 
 *
 * @param fh4p      [OUT] pointer to nfsv4 filehandle
 * @param psfsentry [IN]  pointer to pseudofs entry
 * 
 */

void nfs4_PseudoToFhandle(nfs_fh4 * fh4p, pseudofs_entry_t * psfsentry)
{
  file_handle_v4_t *fhandle4;

  memset(fh4p->nfs_fh4_val, 0, sizeof(struct alloc_file_handle_v4)); /* clean whole thing */
  fhandle4 = (file_handle_v4_t *)fh4p->nfs_fh4_val;
  fhandle4->fhversion = GANESHA_FH_VERSION;
  fhandle4->exportid = 0;
  memcpy(fhandle4->fsopaque, psfsentry->fsopaque, V4_FH_OPAQUE_SIZE);
  fhandle4->fs_len = V4_FH_OPAQUE_SIZE;

  if(isFullDebug(COMPONENT_NFS_V4_PSEUDO))
    {
      char str[256];
      sprint_mem(str, psfsentry->fsopaque, V4_FH_OPAQUE_SIZE);
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,"pseudoToFhandle for name:%s handle:%s",
                   psfsentry->name,str);
    }

  fh4p->nfs_fh4_len = nfs4_sizeof_handle(fhandle4); /* no handle in opaque */
}                               /* nfs4_PseudoToFhandle */

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
  pseudofs_entry_t * psfsentry;

  resp->resop = NFS4_OP_GETATTR;

  /* Get the pseudo entry related to this fhandle */
  res_GETATTR4.status = nfs4_CurrentFHToPseudo(data, &psfsentry);
  if(res_GETATTR4.status != NFS4_OK)
    {
      return res_GETATTR4.status;
    }

  /* All directories in pseudo fs have the same Fattr */
  if(nfs4_PseudoToFattr(psfsentry,
                        &(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                        data, &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
    res_GETATTR4.status = NFS4ERR_RESOURCE;
  else
    res_GETATTR4.status = NFS4_OK;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "attrmask(bitmap4_len)=%d attrlist4_len=%d",
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
  char name[MAXNAMLEN+1];
  struct pseudofs_entry *parent_fsentry=NULL, *thefsentry;
  int error = 0;
  cache_inode_status_t cache_status = 0;
  fsal_status_t fsal_status;
  cache_inode_fsal_data_t fsdata;
  fsal_path_t exportpath_fsal;
  fsal_attrib_list_t attr;
  fsal_handle_t fsal_handle;
  cache_entry_t *pentry = NULL;

  /* Used for child avltree lookup*/
  pseudofs_entry_t tempentry;
  struct avltree_node *keynode, *foundnode;

  resp->resop = NFS4_OP_LOOKUP;

  /* UTF8 strings may not end with \0, but they carry their length */
  if(utf82str(name, sizeof(name), &arg_LOOKUP4.objname) == -1)
    {
      res_LOOKUP4.status = NFS4ERR_NAMETOOLONG;
      return res_LOOKUP4.status;
    }

  /* Get the pseudo fs entry related to the file handle */
  res_LOOKUP4.status = nfs4_CurrentFHToPseudo(data, &parent_fsentry);
  if(res_LOOKUP4.status != NFS4_OK)
    return res_LOOKUP4.status;

  /* Search for name in pseudo fs directory. We use a temporary
   * avlnode and pseudofs_entry_t to perform a name lookup in
   * the child tree. If it's not here, it doesn't exist.*/
  keynode = &tempentry.nameavlnode;
  strcpy(tempentry.name, name);
  avltree_container_of(keynode, pseudofs_entry_t,
                       nameavlnode);

  foundnode = avltree_lookup(keynode, &parent_fsentry->child_tree_byname);
  if(foundnode == NULL)
    {
      res_LOOKUP4.status = NFS4ERR_NOENT;
      return res_LOOKUP4.status;
    }

  /* we found the requested pseudofs node. */
  thefsentry = avltree_container_of(foundnode, pseudofs_entry_t,
                                    nameavlnode);

  /* A matching entry was found */
  if(thefsentry->junction_export == NULL)
    {
      /* The entry is not a junction, we stay within the pseudo fs */
      nfs4_PseudoToFhandle(&(data->currentFH), thefsentry);

      /* No need to fill in compound data because it doesn't change. */
    }
  else
    {
      /* The entry is a junction */
      LogMidDebug(COMPONENT_NFS_V4_PSEUDO,      
                  "A junction in pseudo fs is traversed: name = %s, id = %d",
                  thefsentry->name, thefsentry->junction_export->id);
      data->pexport = thefsentry->junction_export;

      /* Build credentials */
      res_LOOKUP4.status = nfs4_MakeCred(data);

      /* Test for access error (export should not be visible). */
      if(res_LOOKUP4.status == NFS4ERR_ACCESS)
        {
          /* If return is NFS4ERR_ACCESS then this client doesn't have
           * access to this export, return NFS4ERR_NOENT to hide it.
           * It was not visible in READDIR response.
           */
          LogDebug(COMPONENT_NFS_V4_PSEUDO,
                   "NFS4ERR_ACCESS Hiding Export_Id %d Path %s with NFS4ERR_NOENT",
                   data->pexport->id, data->pexport->fullpath);
          res_LOOKUP4.status = NFS4ERR_NOENT;
          return res_LOOKUP4.status;
        }

      if(res_LOOKUP4.status != NFS4_OK)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          return res_LOOKUP4.status;
        }

      /* Build fsal data for creation of the first entry */
      fsal_status = FSAL_str2path(data->pexport->fullpath,
                                  0,
                                  &exportpath_fsal);

      if(FSAL_IS_ERROR(fsal_status))
        {
          cache_status = cache_inode_error_convert(fsal_status);
          res_LOOKUP4.status = nfs4_Errno(cache_status);
          return res_LOOKUP4.status;
        }

      /* Lookup the FSAL to build the fsal handle */
      if(FSAL_IS_ERROR(fsal_status = FSAL_lookupPath(&exportpath_fsal,
                                                     data->pcontext, &fsal_handle, NULL)))
        {
          cache_status = cache_inode_error_convert(fsal_status);
          res_LOOKUP4.status = nfs4_Errno(cache_status);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d ) = %s",
                   fsal_status.major, fsal_status.minor,
                   cache_inode_err_str(cache_status));
          return res_LOOKUP4.status;
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: Failed to allocate the first file handle");
              res_LOOKUP4.status = error;
              return res_LOOKUP4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, &fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle");
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Get the cache inode entry on the other side of the junction */
      fsdata.fh_desc.start = (caddr_t)&fsal_handle;
      fsdata.fh_desc.len = 0;
      FSAL_ExpandHandle(data->pcontext->export_context,
                        FSAL_DIGEST_SIZEOF,
                        &fsdata.fh_desc);

      if((pentry = cache_inode_get(&fsdata,
                                   &attr,
                                   data->pcontext,
                                   NULL,
                                   &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to get attributes for root pentry, status = %s",
                   cache_inode_err_str(cache_status));
          res_LOOKUP4.status = nfs4_Errno(cache_status);
          return res_LOOKUP4.status;
        }

      /* Return the reference to the old current entry */
      if (data->current_entry)
        cache_inode_put(data->current_entry);

      /* Make the cache inode entry the current entry */
      data->current_entry = pentry;
      data->current_filetype = cache_inode_fsal_type_convert(attr.type);

    }                           /* else */


  res_LOOKUP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookup_pseudo */

/**
 * set_compound_data_for_pseudo: fills in compound data for pseudo fs
 * 
 * Fills in:
 *
 * data->current_entry
 * data->current_filetype
 * data->pexport
 * data->export_perms.options
 *
 * @param data  [INOUT] Pointer to the compound request's data
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
int set_compound_data_for_pseudo(compound_data_t * data)
{
  pseudofs_entry_t * dummy;

  data->current_entry        = NULL; /* No cache inode entry */
  data->current_filetype     = DIRECTORY; /* Always a directory */
  data->pexport              = NULL; /* No exportlist is related to pseudo fs */
  data->export_perms.options = EXPORT_OPTION_ROOT |
                               EXPORT_OPTION_MD_READ_ACCESS |
                               EXPORT_OPTION_AUTH_TYPES |
                               EXPORT_OPTION_NFSV4 |
                               EXPORT_OPTION_TRANSPORTS;

  /* Make sure the handle is good. */
  return nfs4_CurrentFHToPseudo(data, &dummy);
}

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
  pseudofs_entry_t * psfsentry;

  resp->resop = NFS4_OP_LOOKUPP;

  /* Get the pseudo fs entry related to the file handle */
  res_LOOKUPP4.status = nfs4_CurrentFHToPseudo(data, &psfsentry);
  if(res_LOOKUPP4.status != NFS4_OK)
    {
      return res_LOOKUPP4.status;
    }

  /* lookupp on the root on the pseudofs should return NFS4ERR_NOENT (RFC3530, page 166) */
  if(psfsentry->pseudo_id == 0)
    {
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* A matching entry was found */
  nfs4_PseudoToFhandle(&(data->currentFH), psfsentry->parent);

  /* Return the reference to the old current entry */
  if (data->current_entry)
    cache_inode_put(data->current_entry);

  /* Fill in compound data */
  res_LOOKUPP4.status = set_compound_data_for_pseudo(data);

  return res_LOOKUPP4.status;
}                               /* nfs4_op_lookupp_pseudo */

/**
 * nfs4_op_lookupp_pseudo_by_exp: looks up into the pseudo fs for the parent directory
 * 
 * looks up into the pseudo fs for the parent directory of the export. 
 *
 * @param op             [IN]    pointer to nfs4_op arguments
 * @param data           [INOUT] Pointer to the compound request's data
 * @paranm exp_root_data [IN]    Pointer to the export root data
 * @param resp           [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
int nfs4_op_lookupp_pseudo_by_exp(struct nfs_argop4  * op,
                                  compound_data_t    * data,
                                  struct nfs_resop4  * resp)
{
  pseudofs_entry_t * psfsentry;

  resp->resop = NFS4_OP_LOOKUPP;

  /* Get the parent pseudo fs entry related to the export */
  res_LOOKUP4.status = nfs4_CurrentFHToPseudo(data, &psfsentry);
  if(res_LOOKUP4.status != NFS4_OK)
    return res_LOOKUP4.status;

  LogDebug(COMPONENT_NFS_V4_PSEUDO,
           "LOOKUPP Traversing junction from Export_Id %d Pseudo %s back to pseudo fs id %"PRIu64,
           data->pcontext->export_context->fe_export->id,
           data->pcontext->export_context->fe_export->pseudopath,
           (uint64_t) data->pcontext->export_context->fe_export->exp_mounted_on_file_id);

  /* lookupp on the root on the pseudofs should return NFS4ERR_NOENT (RFC3530, page 166) */
  if(psfsentry->pseudo_id == 0)
    {
      LogDebug(COMPONENT_NFS_V4_PSEUDO,
               "Returning NFS4ERR_NOENT because pseudo_id == 0");
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* A matching entry was found */
  nfs4_PseudoToFhandle(&(data->currentFH), psfsentry->parent);

  /* Return the reference to the old current entry */
  if (data->current_entry)
    {
      cache_inode_put(data->current_entry);
    }

  /* Fill in compound data */
  res_LOOKUPP4.status = set_compound_data_for_pseudo(data);

  return res_LOOKUPP4.status;
}

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
  pseudofs_entry_t *psfsentry, *curr_psfsentry;
  entry4 *entry_nfs_array = NULL;
  exportlist_t *save_pexport;
  export_perms_t save_export_perms;
  nfs_fh4 entryFH;
  cache_inode_fsal_data_t fsdata;
  fsal_path_t exportpath_fsal;
  fsal_attrib_list_t attr;
  fsal_handle_t fsal_handle;
  fsal_mdsize_t strsize = MNTPATHLEN + 1;
  fsal_status_t fsal_status;
  int error = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t *pentry = NULL;
  pseudofs_entry_t tempentry;
  struct avltree_node *keynode, *currnode;

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  entryFH.nfs_fh4_len = 0;

  LogDebug(COMPONENT_NFS_V4_PSEUDO, "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
              "dircount=%lu, maxcount=%lu, cookie=%"PRIu64", sizeof(entry4)=%lu num_entries=%lu",
              dircount, maxcount, (uint64_t)cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Now resolve the file handle to pseudo fs */
  res_READDIR4.status = nfs4_CurrentFHToPseudo(data, &psfsentry);
  if(res_READDIR4.status != NFS4_OK)
    {
      return res_READDIR4.status;
    }

  LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
              "PSEUDOFS READDIR in %s", psfsentry->name);

  /* If this a junction filehandle ? */
  if(psfsentry->junction_export != NULL)
    {
      /* This is a junction */
      LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
                  "DIR %s id=%u is a junction",
                  psfsentry->name, psfsentry->junction_export->id);

      /* Step up the compound data */
      data->pexport = psfsentry->junction_export;

      /* Build the credentials */
      res_READDIR4.status = nfs4_MakeCred(data);
      if(res_READDIR4.status != NFS4_OK)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
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
      if(FSAL_IS_ERROR(fsal_status = FSAL_lookupPath(&exportpath_fsal,
                                                     data->pcontext, &fsal_handle, NULL)))
        {
          cache_status = cache_inode_error_convert(fsal_status);
          res_READDIR4.status = nfs4_Errno(cache_status);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d ) = %s",
                   fsal_status.major, fsal_status.minor,
                   cache_inode_err_str(cache_status));
          return res_READDIR4.status;
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: Failed to allocate the first file handle");
              res_READDIR4.status = error;
              return res_READDIR4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, &fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle");
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* Get the cache inode entry on the other side of the junction */
      fsdata.fh_desc.start = (caddr_t) &fsal_handle;
      fsdata.fh_desc.len = 0;
      FSAL_ExpandHandle(data->pcontext->export_context,
                        FSAL_DIGEST_SIZEOF,
                        &fsdata.fh_desc);

      if((pentry = cache_inode_get(&fsdata,
                                   &attr,
                                   data->pcontext,
                                   NULL,
                                   &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: Failed to get attributes for root pentry, status = %s",
                   cache_inode_err_str(cache_status));
          res_LOOKUP4.status = nfs4_Errno(cache_status);
          return res_LOOKUP4.status;
        }

      /* Return the reference to the old current entry */
      if (data->current_entry)
        cache_inode_put(data->current_entry);

      /* Make the cache inode entry the current entry */
      data->current_entry = pentry;
      data->current_filetype = cache_inode_fsal_type_convert(attr.type);

      /* redo the call on the other side of the junction */
      return nfs4_op_readdir(op, data, resp);
    }

  /* Allocation of the entries array */
  if((entry_nfs_array =
      gsh_calloc(estimated_num_entries, sizeof(entry4))) == NULL)
    {
      LogMajor(COMPONENT_NFS_V4_PSEUDO,
               "Failed to allocate memory for entries");
      res_READDIR4.status = NFS4ERR_RESOURCE;
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
              gsh_free(entry_nfs_array);
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
  if (cookie == 0)
    currnode = avltree_first(&psfsentry->child_tree_byid);
  else
    {
      /* Find entry with cookie (which was set to pseudo_id) */
      memset(&tempentry.idavlnode, 0, sizeof(tempentry.idavlnode));
      keynode = &tempentry.idavlnode;
      tempentry.pseudo_id = cookie;
      currnode = avltree_lookup(keynode, &psfsentry->child_tree_byid);
      if(currnode == NULL)
        {
          res_READDIR4.status = NFS4ERR_BAD_COOKIE;
          return res_READDIR4.status;
        }
  }
  for( ;
       currnode != NULL;
       currnode = avltree_next(currnode)) {
    curr_psfsentry = avltree_container_of(currnode, pseudofs_entry_t, idavlnode);
    LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
                "PSEUDO FS: Found entry %s pseudo_id %"PRIu64,
                curr_psfsentry->name, curr_psfsentry->pseudo_id);

    entry_nfs_array[i].name.utf8string_len = strlen(curr_psfsentry->name);
    entry_nfs_array[i].name.utf8string_val = gsh_strdup(curr_psfsentry->name);

    if(entry_nfs_array[i].name.utf8string_val == NULL)
      {
        LogMajor(COMPONENT_NFS_V4_PSEUDO,
                 "Failed to allocate memory for entry's name");
        res_READDIR4.status = NFS4ERR_RESOURCE;
        return res_READDIR4.status;
      }

    entry_nfs_array[i].cookie = curr_psfsentry->pseudo_id;

    /* This used to be in an if with a bogus check for FATTR4_FILEHANDLE. Such
     * a common case, elected to set up FH for call to xxxx_ToFattr
     * unconditionally.
     */
    if(entryFH.nfs_fh4_len == 0)
      {
        res_READDIR4.status = nfs4_AllocateFH(&entryFH);
        if(res_READDIR4.status != NFS4_OK)
          {
            return res_READDIR4.status;
          }
      }
    /* Do the case where we stay within the pseudo file system. */
    if(curr_psfsentry->junction_export == NULL)
      {
        nfs4_PseudoToFhandle(&entryFH, curr_psfsentry);

        if(nfs4_PseudoToFattr(curr_psfsentry,
                              &(entry_nfs_array[i].attrs),
                              data, &entryFH, &(arg_READDIR4.attr_request)) != 0)
          {
            LogFatal(COMPONENT_NFS_V4_PSEUDO,
                     "nfs4_PseudoToFattr failed to convert pseudo fs attr");
          }
      }
      else
        {
          /* This is a junction. Code used to not recognize this which resulted
           * in readdir giving different attributes ( including FH, FSid, etc... )
           * to clients from a lookup. AIX refused to list the directory because of
           * this. Now we go to the junction to get the attributes.
           */
          LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
                      "Offspring DIR %s pseudo_id %"PRIu64 " is a junction Export_id %d Path %s",
                      curr_psfsentry->name,
                      curr_psfsentry->pseudo_id,
                      curr_psfsentry->junction_export->id,
                      curr_psfsentry->junction_export->fullpath);

          /* Save the compound data context */
          save_pexport      = data->pexport;
          save_export_perms = data->export_perms;
          data->pexport     = curr_psfsentry->junction_export;
          /* Build the credentials */
          /* XXX Is this really necessary for doing a lookup and 
           * getting attributes?
           * The logic is borrowed from the process invoked above in this code
           * when the target directory is a junction.
           */
          res_READDIR4.status = nfs4_MakeCred(data);

          if(res_READDIR4.status == NFS4ERR_ACCESS)
            {
              /* If return is NFS4ERR_ACCESS then this client doesn't have
               * access to this export, quietly skip the export.
               */
              LogDebug(COMPONENT_NFS_V4_PSEUDO,
                       "NFS4ERR_ACCESS Skipping Export_Id %d Path %s",
                       data->pexport->id, data->pexport->fullpath);
              data->pexport      = save_pexport;
              data->export_perms = save_export_perms;
              continue;
            }

          if(res_READDIR4.status == NFS4ERR_WRONGSEC)
            {
              /* Client isn't using the right SecType for this export,
               * we will report NFS4ERR_WRONGSEC in FATTR4_RDATTR_ERROR.
               *
               * If the ONLY attributes requested are FATTR4_RDATTR_ERROR and
               * FATTR4_MOUNTED_ON_FILEID we will not return an error and
               * instead will return success with FATTR4_MOUNTED_ON_FILEID.
               * AIX clients make this request and expect it to succeed.
               */
              LogDebug(COMPONENT_NFS_V4_PSEUDO,
                       "NFS4ERR_WRONGSEC On ReadDir Export_Id %d Path %s",
                       data->pexport->id, data->pexport->fullpath);

              if(check_for_wrongsec_ok_attr(&arg_READDIR4.attr_request))
                {
                  /* Client is requesting attr that are allowed when
                   * NFS4ERR_WRONGSEC occurs.
                   *
                   * Because we are not asking for any attributes
                   * which are a property of the exported file system's
                   * root, really just asking for MOUNTED_ON_FILEID,
                   * we can just get the attr for this pseudo fs node
                   * since it will result in the correct value for
                   * MOUNTED_ON_FILEID since pseudo fs FILEID and
                   * MOUNTED_ON_FILEID are always the same. FILEID
                   * of pseudo fs node is what we actually want here...
                   */
                  if(nfs4_PseudoToFattr(curr_psfsentry,
                                        &(entry_nfs_array[i].attrs),
                                        data,
                                        NULL, /* don't need the file handle */
                                        &(arg_READDIR4.attr_request)) != 0)
                    {
                      LogFatal(COMPONENT_NFS_V4_PSEUDO,
                               "nfs4_PseudoToFattr failed to convert pseudo fs attr");
                    }
                  // next step
                }
              else
                {
                  // report NFS4ERR_WRONGSEC
                  if(nfs4_Fattr_Fill_Error(&(entry_nfs_array[i].attrs),
                                           NFS4ERR_WRONGSEC) != 0)
                    {
                      LogFatal(COMPONENT_NFS_V4_PSEUDO,
                               "nfs4_Fattr_Fill_Error failed to fill in RDATTR_ERROR");
                    }
                }
            }
          else
            {
              /* Traverse junction to get attrs */

              /* Do the look up. */
              fsal_status = FSAL_str2path(curr_psfsentry->junction_export->fullpath,
                                          (strlen(curr_psfsentry->junction_export->fullpath) +1 ),
                                          &exportpath_fsal);

              if(FSAL_IS_ERROR(fsal_status))
                {
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: Failed to convert %s to string, id=%d",
                       data->pexport->fullpath, data->pexport->id);

                  /* We just skip this entry, something bad has happened. */
                  data->pexport      = save_pexport;
                  data->export_perms = save_export_perms;
                  continue;
                }

              fsal_status = FSAL_lookupPath(&exportpath_fsal,
                                            data->pcontext, 
                                            &fsal_handle,
                                            NULL);

              if(FSAL_IS_ERROR(fsal_status))
                {
                  cache_status = cache_inode_error_convert(fsal_status);
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: Failed to lookup for %s, id=%d",
                       data->pexport->fullpath, data->pexport->id);
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d ) = %s",
                       fsal_status.major, fsal_status.minor,
                       cache_inode_err_str(cache_status));

                  if(nfs4_Fattr_Fill_Error(&(entry_nfs_array[i].attrs),
                                           nfs4_Errno(cache_status)) != 0)
                    {
                      LogFatal(COMPONENT_NFS_V4_PSEUDO,
                               "nfs4_Fattr_Fill_Error failed to fill in RDATTR_ERROR");
                    }

                  /* We just skip this entry, something bad has happened.
                   * One possibility is that the exported directory has been
                   * removed.
                   */
                  data->pexport      = save_pexport;
                  data->export_perms = save_export_perms;
                  continue;
                }

              /* Build the nfs4 handle. Again, we do this unconditionally. */
              if(!nfs4_FSALToFhandle(&entryFH, &fsal_handle, data))
                {
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle for %s, id=%d",
                       data->pexport->fullpath, data->pexport->id);

                  /* We just skip this entry, something bad has happened. */
                  data->pexport      = save_pexport;
                  data->export_perms = save_export_perms;
                  continue;
                }

              /* Get the cache inode entry on the other side of the junction
               * and it's attributes.
               */
              fsdata.fh_desc.start = (caddr_t) &fsal_handle;
              fsdata.fh_desc.len = 0;

              FSAL_ExpandHandle(data->pcontext->export_context,
                                FSAL_DIGEST_SIZEOF,
                                &fsdata.fh_desc);

              if((pentry = cache_inode_get(&fsdata,
                                           &attr,
                                           data->pcontext,
                                           NULL,
                                           &cache_status)) == NULL)
                {
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                           "PSEUDO FS JUNCTION TRAVERSAL: Failed to get attributes for root pentry for %s, id=%d, status = %s",
                           data->pexport->fullpath, data->pexport->id,
                           cache_inode_err_str(cache_status));

                  if(nfs4_Fattr_Fill_Error(&(entry_nfs_array[i].attrs),
                                           nfs4_Errno(cache_status)) != 0)
                    {
                      LogFatal(COMPONENT_NFS_V4_PSEUDO,
                               "nfs4_Fattr_Fill_Error failed to fill in RDATTR_ERROR");
                    }

                  /* We just skip this entry, something bad has happened.
                   * One possibity is that we weren't able to get the attributes,
                   * but we set up to always allow READ_ATTR, though not READ_ACL.
                   */
                  data->pexport      = save_pexport;
                  data->export_perms = save_export_perms;
                  continue;
                }

              /* Release the reference we just got. */
              cache_inode_put(pentry);

              if(nfs4_FSALattr_To_Fattr(data->pexport,
                                        &attr,
                                        &(entry_nfs_array[i].attrs),
                                        data,
                                        &entryFH,
                                        &(arg_READDIR4.attr_request)) != 0)
                {
                  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                           "nfs4_FSALattr_To_Fattr failed to convert attr for %s, id=%d",
                           data->pexport->fullpath, data->pexport->id);

                  /* We just skip this entry, something bad has happened. */
                  data->pexport      = save_pexport;
                  data->export_perms = save_export_perms;
                  continue;
                }
              }

           data->pexport      = save_pexport;
           data->export_perms = save_export_perms;
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
  } /* avltree for loop */


  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if(i == 0)
    res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
  else
    res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;

  /* did we reach the end ? */
  if(currnode == NULL) /* Yes, we did */
    res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
  else /* No, there are some more entries */
    res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;

  if (entryFH.nfs_fh4_val != NULL)
    gsh_free(entryFH.nfs_fh4_val);

  /* Exit properly */
  res_READDIR4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_readdir_pseudo */

/**
 *
 * @brief Compares two pseudofs ids used in pseudofs hashtable
 *
 * Compare two keys used in pseudofs cache. These keys are made from
 * the pseudofs pathname and a hash of that pathname.
 *
 * @param[in] buff1 First key
 * @param[in] buff2 Second key
 * @return 0 if keys are the same, 1 otherwise
 *
 *
 */
int compare_nfs4_pseudo_key(hash_buffer_t *buff1,
                            hash_buffer_t *buff2)
{
  /* This compares cityhash64, path length, and fullpath */
  if (buff1->len != buff2->len ||
      (memcmp(buff1->pdata, buff2->pdata, buff1->len != 0)) )
      return 1;
  return 0;
}

/**                                          
 * @brief Hash function for pseudofs hashtable
 *                                           
 * Hash function for pseudofs hashtable.     
 *                                           
 * @param[in] param hash parameter           
 * @param[in] buffclef hashtable buffer      
 *                                           
 * @return 32 bit hash                       
 */
uint32_t nfs4_pseudo_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef)
{
  uint32_t res = 0;

  res = *((uint64_t *)buffclef->pdata) % (uint32_t) p_hparam->index_size;
  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(p_hparam->ht_log_component, "%s: value = %"PRIu32,
                 p_hparam->ht_name, res);
  return res;
}                               /* nfs4_owner_value_hash_func */

/**                                                
 * @brief nfsv4 pseudofs hash function for avltree
 *
 * nfsv4 pseudofs hash function for avltree
 *
 * @param[in] param
 * @param[in] buffclef
 *
 * @return 64 bit hash
 */
uint64_t nfs4_pseudo_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef)
{
  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(p_hparam->ht_log_component, "rbt = %"PRIu64, *(uint64_t *)buffclef->pdata);
  return *(uint64_t *)buffclef->pdata;
}                               /* state_id_rbt_hash_func */

/**                                                                    
 * @brief Display a value from the pseudofs handle hashtable           
 *                                                                     
 * Display a value from the pseudofs handle hashtable. This function is
 * passed to the pseudofs hashtable.                                   
 *                                                                     
 * @param[in] pbuff                                                    
 * @param[out] str                                                     
 *                                                                     
 * @return 1 if success, 0 if fail                                     
 */
int display_pseudo_val(struct display_buffer * dspbuf, hash_buffer_t * pbuff)
{
  pseudofs_entry_t *psfsentry = (pseudofs_entry_t *)pbuff->pdata;

  return display_printf(dspbuf, "nodename=%s nodeid=%"PRIu64, psfsentry->name,
                        psfsentry->pseudo_id);
}

/**                                                                  
 * @brief Display a key from the pseudofs handle hashtable           
 *                                                                   
 * Display a key from the pseudofs handle hashtable. This function is
 * passed to the pseudofs hashtable.                                 
 *                                                                   
 * @param[in] pbuff                                                  
 * @param[out] str                                                   
 *                                                                   
 * @return 1 if success, 0 if fail                                   
 */
int display_pseudo_key(struct display_buffer * dspbuf, hash_buffer_t * pbuff)
{
  uint64_t ch64_hash = 0;
  int len;
  char pseudopath[MAXPATHLEN+2];
  char str[17]; // 64 bits will be 16 chars printed in hexadecimal

  ch64_hash = *(uint64_t *)pbuff->pdata;
  sprint_mem(str, (char *)&ch64_hash, sizeof(ch64_hash));

  len = *(ushort *)(pbuff->pdata+sizeof(ch64_hash));
  strncpy(pseudopath, pbuff->pdata + sizeof(ch64_hash) + sizeof(ushort),
         len);
  return display_printf(dspbuf, "cityhash64=%s len=%d path=%s", str, len, pseudopath);
}

/**
 *
 * Init_nfs4_pseudo: Init the hashtable for NFS Pseudofs nodeid cache.
 *
 * Perform all the required initialization for hashtable Pseudofs nodeid cache
 *
 * @param param [IN] parameter used to init the pseudofs nodeid cache
 *
 * @return 0 if successful, -1 otherwise
 */
int Init_nfs4_pseudo(nfs4_pseudo_parameter_t param)
{
  if((ht_nfs4_pseudo = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(param.hash_param.ht_log_component,
              "Cannot init %s cache", param.hash_param.ht_name);
      return -1;
    }

  return 0;
}                               /* Init_nfs4_pseudo */
