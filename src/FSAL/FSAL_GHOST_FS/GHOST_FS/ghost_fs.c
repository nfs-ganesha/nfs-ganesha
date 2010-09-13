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
 * \file    ghost_fs.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:35 $
 * \version $Revision: 1.6 $
 * \brief   Implementation of a very simple file system in memory,
 *          used for basic tests.
 *          Thread-safe.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "FSAL/FSAL_GHOST_FS/ghost_fs.h"
#include "stuff_alloc.h"
#include <string.h>
#include <sys/time.h>

#define TRUE  1
#define FALSE 0

/* FS root */
static GHOSTFS_item_t *p_root = NULL;
/* FS info */
static GHOSTFS_stats_t stats;

/* configuration parameters */
static GHOSTFS_parameter_t config;

/* computes a validator based on inode number an current time */
static unsigned int mk_magic(GHOSTFS_inode_t inode)
{
  unsigned int validator;
  unsigned long long inode64 = (unsigned long long)inode;

  struct timeval tv;
  gettimeofday(&tv, NULL);

  validator = (unsigned int)tv.tv_sec
      ^ (unsigned int)tv.tv_usec ^ (unsigned int)(inode64 >> 32) ^ (unsigned int)inode64;

  LogFullDebug(COMPONENT_FSAL, "validator(%llu)=%u", inode, validator);

  return validator;

}

static GHOSTFS_item_t *GetEntry_From_Handle(GHOSTFS_handle_t handle)
{
  GHOSTFS_item_t *p_entry;

  if(!handle.inode)
    return NULL;

  p_entry = (GHOSTFS_item_t *) handle.inode;

  /* check the magic number */
  if(p_entry->magic != handle.magic)
    return NULL;

  /* the entry seems to be OK, return it */
  return p_entry;

}

/* creates a new entry.
 * this entry is locked for modification.
 */

static GHOSTFS_item_t *create_new_ghostfs_entry(GHOSTFS_typeitem_t type)
{
  GHOSTFS_item_t *p_entry;

  /* Allocates a new entry */
  p_entry = (GHOSTFS_item_t *) Mem_Alloc(sizeof(GHOSTFS_item_t));

  memset(p_entry, 0, sizeof(GHOSTFS_item_t));

  if(p_entry == NULL)
    return NULL;

  rw_lock_init(&p_entry->entry_lock);

  /* lock the entry for modification */
  P_w(&p_entry->entry_lock);

  p_entry->inode = (GHOSTFS_inode_t) p_entry;

  /* generates a new magic number for this entry */
  p_entry->magic = mk_magic(p_entry->inode);

  /* if is not in a filesystem for the moment
   * (and do not contain . nor .., etc...)
   */
  p_entry->linkcount = 0;

  p_entry->type = type;

  return p_entry;

}

/* add an entry to a directory
 * does NOT verify if it already exists.
 */
static int Add_Dir_Entry(GHOSTFS_item_t * dir_item,
                         GHOSTFS_handle_t object_handle, char *object_name)
{
  GHOSTFS_dirlist_t *p_entry;

  if((dir_item == NULL) || (object_name == NULL) || (object_handle.inode == NULL))
    return ERR_GHOSTFS_INTERNAL;

  /* allocates a dirent */
  p_entry = (GHOSTFS_dirlist_t *) Mem_Alloc(sizeof(GHOSTFS_dirlist_t));

  if(p_entry == NULL)
    return ERR_GHOSTFS_MALLOC;

  memset(p_entry, 0, sizeof(GHOSTFS_dirlist_t));

  p_entry->handle = object_handle;
  strncpy(p_entry->name, object_name, GHOSTFS_MAX_FILENAME);
  p_entry->next = NULL;

  /* insertion */
  if(dir_item->ITEM_DIR.lastentry == NULL)
    {
      dir_item->ITEM_DIR.direntries = dir_item->ITEM_DIR.lastentry = p_entry;
    }
  else
    {
      dir_item->ITEM_DIR.lastentry->next = p_entry;
      dir_item->ITEM_DIR.lastentry = p_entry;
    }

  return ERR_GHOSTFS_NO_ERROR;

}

/**
 * find an entry in a directory list
 * @return ERR_GHOSTFS_NO_ERROR if it was found,
 *         ERR_GHOSTFS_NOENT else.
 */

static int Find_Entry(GHOSTFS_item_t * p_parent,
                      char *entry_name, GHOSTFS_handle_t * p_found_hdl)
{
  GHOSTFS_dirlist_t *dirl;

  dirl = p_parent->ITEM_DIR.direntries;

  while(dirl)
    {
      if(!strncmp(dirl->name, entry_name, GHOSTFS_MAX_FILENAME))
        {
          *p_found_hdl = dirl->handle;

          /* item found */
          return ERR_GHOSTFS_NO_ERROR;
        }
      dirl = dirl->next;
    }

  /* item not found */
  return ERR_GHOSTFS_NOENT;
}

/**
 * rename an entry in a directory list
 * @return ERR_GHOSTFS_NO_ERROR if it was found,
 *         ERR_GHOSTFS_NOENT else.
 */

static int Rename_Entry(GHOSTFS_item_t * p_parent,
                        char *entry_old_name, char *entry_new_name)
{
  GHOSTFS_dirlist_t *dirl;

  dirl = p_parent->ITEM_DIR.direntries;

  while(dirl)
    {
      if(!strncmp(dirl->name, entry_old_name, GHOSTFS_MAX_FILENAME))
        {
          strncpy(dirl->name, entry_new_name, GHOSTFS_MAX_FILENAME);

          /* item found */
          return ERR_GHOSTFS_NO_ERROR;
        }
      dirl = dirl->next;
    }

  /* item not found */
  return ERR_GHOSTFS_NOENT;
}

/**
 * change a handle in a directory
 * @return ERR_GHOSTFS_NO_ERROR if it was found,
 *         ERR_GHOSTFS_NOENT else.
 */

static int Change_Entry_Handle(GHOSTFS_item_t * p_parent,
                               char *entry_name, GHOSTFS_handle_t entry_handle)
{
  GHOSTFS_dirlist_t *dirl;

  dirl = p_parent->ITEM_DIR.direntries;

  while(dirl)
    {
      if(!strncmp(dirl->name, entry_name, GHOSTFS_MAX_FILENAME))
        {
          dirl->handle = entry_handle;

          /* item found */
          return ERR_GHOSTFS_NO_ERROR;
        }
      dirl = dirl->next;
    }

  /* item not found */
  return ERR_GHOSTFS_NOENT;
}

/**
 * find an entry in a directory list and remove it
 * @return ERR_GHOSTFS_NO_ERROR if it was found and removed,
 *         ERR_GHOSTFS_NOENT else.
 */

static int Remove_Entry(GHOSTFS_item_t * p_parent, char *entry_name)
{
  GHOSTFS_dirlist_t *dirl;
  GHOSTFS_dirlist_t *last;

  last = NULL;
  dirl = p_parent->ITEM_DIR.direntries;

  while(dirl)
    {
      if(!strncmp(dirl->name, entry_name, GHOSTFS_MAX_FILENAME))
        {
          /* item found */

          /* if it was the first entry */
          if(last == NULL)
            p_parent->ITEM_DIR.direntries = dirl->next;
          else
            last->next = dirl->next;

          /* if it was the last entry */
          if(dirl == p_parent->ITEM_DIR.lastentry)
            p_parent->ITEM_DIR.lastentry = last;

          return ERR_GHOSTFS_NO_ERROR;
        }
      last = dirl;
      dirl = dirl->next;
    }

  /* item not found */
  return ERR_GHOSTFS_NOENT;
}

/* check that the name does not contain special sequences */
static int is_name_ok(char *name)
{

  if(name[0] == '\0')
    return FALSE;
  if(!strcmp(name, "."))
    return FALSE;
  if(!strcmp(name, ".."))
    return FALSE;
  if(strchr(name, '/') != NULL)
    return FALSE;

  return TRUE;
}

static int is_empty_dir(GHOSTFS_item_t * p_dir)
{
  GHOSTFS_dirlist_t *dirl;

  dirl = p_dir->ITEM_DIR.direntries;

  while(dirl)
    {
      if(strncmp(dirl->name, ".", GHOSTFS_MAX_FILENAME) &&
         strncmp(dirl->name, "..", GHOSTFS_MAX_FILENAME))
        {
          /* not empty */
          return FALSE;
        }
      dirl = dirl->next;
    }

  /* item not found */
  return TRUE;
}

static void fill_attributes(GHOSTFS_item_t * p_entry, GHOSTFS_Attrs_t * p_out_attrs)
{

  p_out_attrs->inode = p_entry->inode;
  p_out_attrs->linkcount = p_entry->linkcount;
  p_out_attrs->type = p_entry->type;
  p_out_attrs->uid = p_entry->attributes.uid;
  p_out_attrs->gid = p_entry->attributes.gid;
  p_out_attrs->mode = p_entry->attributes.mode;
  p_out_attrs->atime = p_entry->attributes.atime;
  p_out_attrs->mtime = p_entry->attributes.mtime;
  p_out_attrs->ctime = p_entry->attributes.ctime;
  p_out_attrs->creationTime = p_entry->attributes.creationTime;
  p_out_attrs->size = p_entry->attributes.size;

}

/*------------------------ Library functions -------------------*/

/* Initialise the filesystem and creates the root entry.
 */
int GHOSTFS_Init(GHOSTFS_parameter_t init_cfg)
{

  int rc;
  GHOSTFS_handle_t roothandle;

  /* checks whether the FS is already loaded. */
  if(p_root)
    return ERR_GHOSTFS_ALREADYINIT;

  /* saves the configuration */
  config = init_cfg;

  /* creates the root entry */
  p_root = create_new_ghostfs_entry(GHOSTFS_DIR);

  if(p_root == NULL)
    return ERR_GHOSTFS_MALLOC;

  /* fill directory attributes */
  LogFullDebug(COMPONENT_FSAL, "init_cfg.root_owner = %d, config.", config.root_owner);

  p_root->attributes.uid = config.root_owner;
  p_root->attributes.gid = config.root_group;
  p_root->attributes.mode = config.root_mode;

  p_root->attributes.atime =
      p_root->attributes.ctime =
      p_root->attributes.mtime = p_root->attributes.creationTime = time(NULL);

  p_root->attributes.size = 0;

  /* empty for the moment */
  p_root->ITEM_DIR.direntries = NULL;
  p_root->ITEM_DIR.lastentry = NULL;

  roothandle.inode = p_root->inode;
  roothandle.magic = p_root->magic;

  /* add . and .. entries */
  rc = Add_Dir_Entry(p_root, roothandle, ".");
  if(rc)
    {
      V_w(&p_root->entry_lock);
      return rc;
    }
  p_root->linkcount++;

  if(config.dot_dot_root_eq_root)
    {
      rc = Add_Dir_Entry(p_root, roothandle, "..");
      if(rc)
        {
          V_w(&p_root->entry_lock);
          return rc;
        }
      p_root->linkcount++;
    }

  /* unlock and return */

  V_w(&p_root->entry_lock);

  return ERR_GHOSTFS_NO_ERROR;

}

/** Gets the root directory inode. */
int GHOSTFS_GetRoot(GHOSTFS_handle_t * root_handle)
{
  /* checks args. */
  if(!root_handle)
    return ERR_GHOSTFS_ARGS;

  /* checks whether the FS has been intialized. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* we kwnow that the root will never be deleted,
   * no need to lock it.
   */

  root_handle->inode = p_root->inode;
  root_handle->magic = p_root->magic;

  /* return */
  return ERR_GHOSTFS_NO_ERROR;

}

/** Find a named object in the filesystem. */
int GHOSTFS_Lookup(GHOSTFS_handle_t handle_parent,
                   char *ghostfs_name, GHOSTFS_handle_t * p_handle)
{
  int rc;
  GHOSTFS_item_t *p_parent;

  /* checks whether the FS has been initialized. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!p_handle || !ghostfs_name)
    return ERR_GHOSTFS_ARGS;

  /* sets null handle for output */
  memset(p_handle, 0, sizeof(GHOSTFS_handle_t));

  /* verifies that there is no slash in the name */
  if(strchr(ghostfs_name, '/'))
    return ERR_GHOSTFS_ARGS;

  /* convert inode to item adress */
  p_parent = GetEntry_From_Handle(handle_parent);

  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  /* lock the directory for reading */
  P_r(&p_parent->entry_lock);

  /* check object type */
  if(p_parent->type != GHOSTFS_DIR)
    {
      V_r(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* find entry */
  rc = Find_Entry(p_parent, ghostfs_name, p_handle);

  V_r(&p_parent->entry_lock);
  return rc;

}

/* Gets the attributes of an object in the filesystem. */
int GHOSTFS_GetAttrs(GHOSTFS_handle_t handle, GHOSTFS_Attrs_t * object_attributes)
{
  GHOSTFS_item_t *p_item;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!object_attributes)
    return ERR_GHOSTFS_ARGS;

  p_item = GetEntry_From_Handle(handle);
  if(p_item == NULL)
    return ERR_GHOSTFS_STALE;

  /* locks the entry for reading */
  P_r(&p_item->entry_lock);

  /* fill in the attribute structure */

  /* inode attributes */
  fill_attributes(p_item, object_attributes);

  V_r(&p_item->entry_lock);
  return ERR_GHOSTFS_NO_ERROR;

}

/** Tests whether a user can access an object */
int GHOSTFS_Access(GHOSTFS_handle_t handle,
                   GHOSTFS_testperm_t test_set,
                   GHOSTFS_user_t userid, GHOSTFS_group_t groupid)
{
  int is_owner = FALSE;
  int is_grp = FALSE;
  GHOSTFS_perm_t mask, result_mask;
  GHOSTFS_item_t *p_item;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* convert inode to item adress */

  p_item = GetEntry_From_Handle(handle);
  if(p_item == NULL)
    return ERR_GHOSTFS_STALE;

  /* locks the entry for reading */
  P_r(&p_item->entry_lock);

  /* if the user is root he can always access the file */
  if((userid == 0) && config.root_access)
    {
      V_r(&p_item->entry_lock);
      return ERR_GHOSTFS_NO_ERROR;
    }

  is_owner = (p_item->attributes.uid == userid);
  is_grp = (p_item->attributes.gid == groupid);
  mask = p_item->attributes.mode;

  /* we have accessed the item, we can release lock. */
  V_r(&p_item->entry_lock);

  /* in this version, we only test for the last 9 bits */
  mask &= 0777;

  /* computes mask */
  result_mask = 0;
  if(is_owner)
    result_mask |= (mask & (test_set << 6));
  else if(is_grp)
    result_mask |= (mask & (test_set << 3));
  else
    result_mask |= (mask & test_set);

  LogFullDebug(COMPONENT_FSAL, "GHOSTFS_Access : mask=%#o : perms=%#o owner=%s group=%s => result_mask=%#o",
         mask, test_set, (is_owner ? "yes" : "no"), (is_grp ? "yes" : "no"), result_mask);

  if(result_mask)
    return ERR_GHOSTFS_NO_ERROR;
  else
    return ERR_GHOSTFS_ACCES;

}

/** Reads the content of a symlink */
int GHOSTFS_ReadLink(GHOSTFS_handle_t handle, char *buffer, GHOSTFS_mdsize_t buff_size)
{
  GHOSTFS_item_t *p_item;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!buffer)
    return ERR_GHOSTFS_ARGS;

  /* convert inode to item adress */

  p_item = GetEntry_From_Handle(handle);
  if(p_item == NULL)
    return ERR_GHOSTFS_STALE;

  /* locks the entry for reading */
  P_r(&p_item->entry_lock);

  /* check type */
  if(p_item->type != GHOSTFS_LNK)
    {
      V_r(&p_item->entry_lock);
      return ERR_GHOSTFS_NOTLNK;
    }

  /* verifies buffer length including '\0' */
  if(buff_size < strlen(p_item->ITEM_SYMLNK.linkdata) + 1)
    {
      V_r(&p_item->entry_lock);
      return ERR_GHOSTFS_TOOSMALL;
    }

  /* copy link content */
  strncpy(buffer, p_item->ITEM_SYMLNK.linkdata, buff_size);

  V_r(&p_item->entry_lock);
  return ERR_GHOSTFS_NO_ERROR;

}

/** Opens a directory stream. */

int GHOSTFS_Opendir(GHOSTFS_handle_t handle, dir_descriptor_t * dir)
{
  GHOSTFS_item_t *p_item;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!dir)
    return ERR_GHOSTFS_ARGS;

  /* convert inode to item adress */

  p_item = GetEntry_From_Handle(handle);
  if(p_item == NULL)
    return ERR_GHOSTFS_STALE;

  /* locks the entry for reading */
  P_r(&p_item->entry_lock);

  /* check type */
  if(p_item->type != GHOSTFS_DIR)
    {
      V_r(&p_item->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* fill dir descriptor */
  dir->handle.inode = p_item->inode;
  dir->handle.magic = p_item->magic;
  dir->master_record = &(p_item->ITEM_DIR);
  dir->current_dir_entry = p_item->ITEM_DIR.direntries;

  /* DO NOT RELEASE THE READ LOCK ON IT
   * UNTIL CLOSEDIR HAS BEEN CALLED
   */

  return ERR_GHOSTFS_NO_ERROR;

}

/** Reads an entry from an opened directory stream */
int GHOSTFS_Readdir(dir_descriptor_t * dir, GHOSTFS_dirent_t * dirent)
{
  GHOSTFS_item_t *p_dir;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!dir)
    return ERR_GHOSTFS_ARGS;
  if(!dirent)
    return ERR_GHOSTFS_ARGS;

  p_dir = GetEntry_From_Handle(dir->handle);

  /* sanity checks on dir descriptor */
  if((p_dir == NULL)
     || (dir->master_record == NULL)
     || (dir->handle.inode != p_dir->inode)
     || (dir->handle.magic != p_dir->magic)
     || (dir->master_record != &(p_dir->ITEM_DIR)) || (p_dir->type != GHOSTFS_DIR))
    return ERR_GHOSTFS_NOTOPENED;

  /* end of dir ? */
  if(!dir->current_dir_entry)
    return ERR_GHOSTFS_ENDOFDIR;

  /* fill in dirent */
  dirent->handle = dir->current_dir_entry->handle;
  strncpy(dirent->name, dir->current_dir_entry->name, GHOSTFS_MAX_FILENAME);
  dirent->cookie = dir->current_dir_entry;

  /* updates dir descriptor */
  dir->current_dir_entry = dir->current_dir_entry->next;

  return ERR_GHOSTFS_NO_ERROR;
}

int GHOSTFS_Seekdir(dir_descriptor_t * dir, GHOSTFS_cookie_t cookie)
{
  GHOSTFS_item_t *p_dir;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!dir)
    return ERR_GHOSTFS_ARGS;

  p_dir = GetEntry_From_Handle(dir->handle);

  /* sanity checks on dir descriptor */
  if((p_dir == NULL)
     || (dir->master_record == NULL)
     || (dir->handle.inode != p_dir->inode)
     || (dir->handle.magic != p_dir->magic)
     || (dir->master_record != &(p_dir->ITEM_DIR)) || (p_dir->type != GHOSTFS_DIR))
    return ERR_GHOSTFS_NOTOPENED;

  /* updates dir descriptor */
  if(cookie == NULL)
    {
      /* begin of the directory */
      dir->current_dir_entry = dir->master_record->direntries;
    }
  else
    {
      /* last read == cookie => next = the one that follows the cookie */
      dir->current_dir_entry = cookie->next;
    }

  return ERR_GHOSTFS_NO_ERROR;

}

/** Closes a directory stream */
int GHOSTFS_Closedir(dir_descriptor_t * dir)
{
  GHOSTFS_item_t *p_dir;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!dir)
    return ERR_GHOSTFS_ARGS;

  p_dir = GetEntry_From_Handle(dir->handle);

  /* sanity checks on dir descriptor */
  if((p_dir == NULL)
     || (dir->master_record == NULL)
     || (dir->handle.inode != p_dir->inode)
     || (dir->handle.magic != p_dir->magic)
     || (dir->master_record != &(p_dir->ITEM_DIR)) || (p_dir->type != GHOSTFS_DIR))
    return ERR_GHOSTFS_NOTOPENED;

  /* unlocking the directory */
  V_r(&p_dir->entry_lock);

  /* closing dir descriptor */
  memset(dir, 0, sizeof(dir_descriptor_t));

  return ERR_GHOSTFS_NO_ERROR;
}

/* set file attributes */
int GHOSTFS_SetAttrs(GHOSTFS_handle_t handle,
                     GHOSTFS_setattr_mask_t setattr_mask, GHOSTFS_Attrs_t attrs_values)
{
  GHOSTFS_item_t *p_item;
  GHOSTFS_setattr_mask_t editable;

  /* checks whether the FS has been loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  p_item = GetEntry_From_Handle(handle);
  if(p_item == NULL)
    return ERR_GHOSTFS_STALE;

  /* locks the entry for modification */
  P_w(&p_item->entry_lock);

  /* check settable attributes */

  if(p_item->type == GHOSTFS_FILE)
    editable =
        (SETATTR_UID | SETATTR_GID | SETATTR_MODE | SETATTR_ATIME | SETATTR_MTIME |
         SETATTR_SIZE);
  else
    editable = (SETATTR_UID | SETATTR_GID | SETATTR_MODE | SETATTR_ATIME | SETATTR_MTIME);

  /* check for unsupported atributes */
  if(setattr_mask & ~editable)
    {
      V_w(&p_item->entry_lock);
      return ERR_GHOSTFS_ATTR_NOT_SUPP;
    }

  /* operations restricted to root */
  if(setattr_mask & SETATTR_UID)
    p_item->attributes.uid = attrs_values.uid;

  if(setattr_mask & SETATTR_GID)
    p_item->attributes.gid = attrs_values.gid;

  if(setattr_mask & SETATTR_MODE)
    p_item->attributes.mode = attrs_values.mode & 0777;

  if(setattr_mask & SETATTR_ATIME)
    p_item->attributes.atime = attrs_values.atime;

  if(setattr_mask & SETATTR_MTIME)
    p_item->attributes.mtime = attrs_values.mtime;

  if(setattr_mask & SETATTR_UID)
    p_item->attributes.uid = attrs_values.uid;

  V_w(&p_item->entry_lock);
  return ERR_GHOSTFS_NO_ERROR;

}                               /* GHOSTFS_SetAttrs */

int GHOSTFS_MkDir(GHOSTFS_handle_t parent_handle,
                  char *new_dir_name,
                  GHOSTFS_user_t owner,
                  GHOSTFS_group_t group,
                  GHOSTFS_perm_t mode,
                  GHOSTFS_handle_t * p_new_dir_handle, GHOSTFS_Attrs_t * p_new_dir_attrs)
{
  int rc;
  GHOSTFS_handle_t newhandle, tmphandle;

  GHOSTFS_item_t *p_parent;
  GHOSTFS_item_t *p_newdir;

  /* checks whether the FS is already loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!new_dir_name)
    return ERR_GHOSTFS_ARGS;
  if(!p_new_dir_handle)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(new_dir_name))
    return ERR_GHOSTFS_ARGS;

  /* get the parent and lock it for writing */

  p_parent = GetEntry_From_Handle(parent_handle);
  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_parent->entry_lock);

  /* check type */
  if(p_parent->type != GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* First try looking up the entry (check if it does not exist) */

  rc = Find_Entry(p_parent, new_dir_name, &tmphandle);

  if(rc == 0)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_EXIST;
    }

  if(rc != ERR_GHOSTFS_NOENT)
    {
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* creates the new entry */
  p_newdir = create_new_ghostfs_entry(GHOSTFS_DIR);

  if(p_newdir == NULL)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_MALLOC;
    }

  /* fill directory attributes */

  p_newdir->attributes.uid = owner;
  p_newdir->attributes.gid = group;
  p_newdir->attributes.mode = mode;

  p_newdir->attributes.atime =
      p_newdir->attributes.ctime =
      p_newdir->attributes.mtime = p_newdir->attributes.creationTime = time(NULL);

  p_newdir->attributes.size = 0;

  /* empty for the moment */
  p_newdir->ITEM_DIR.direntries = NULL;
  p_newdir->ITEM_DIR.lastentry = NULL;

  p_new_dir_handle->inode = p_newdir->inode;
  p_new_dir_handle->magic = p_newdir->magic;

  /* add '.' entry into the new directory */

  rc = Add_Dir_Entry(p_newdir, *p_new_dir_handle, ".");
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_newdir->entry_lock);
      return rc;
    }
  p_newdir->linkcount++;

  /* add '..' entry into the new directory */

  rc = Add_Dir_Entry(p_newdir, parent_handle, "..");
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_newdir->entry_lock);
      return rc;
    }
  p_parent->linkcount++;

  /* add named entry into the parent directory */

  rc = Add_Dir_Entry(p_parent, *p_new_dir_handle, new_dir_name);
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_newdir->entry_lock);
      return rc;
    }
  p_newdir->linkcount++;

  /* update parent mtime and ctime */
  p_parent->attributes.mtime = p_parent->attributes.ctime = time(NULL);

  /* return new dir attributes (if asked) */

  if(p_new_dir_attrs != NULL)
    fill_attributes(p_newdir, p_new_dir_attrs);

  /* unlock and return */

  V_w(&p_parent->entry_lock);
  V_w(&p_newdir->entry_lock);

  return ERR_GHOSTFS_NO_ERROR;

}                               /* GHOSTFS_MkDir */

int GHOSTFS_Create(GHOSTFS_handle_t parent_handle,
                   char *new_file_name,
                   GHOSTFS_user_t owner,
                   GHOSTFS_group_t group,
                   GHOSTFS_perm_t mode,
                   GHOSTFS_handle_t * p_new_file_handle,
                   GHOSTFS_Attrs_t * p_new_file_attrs)
{
  int rc;
  GHOSTFS_handle_t newhandle, tmphandle;

  GHOSTFS_item_t *p_parent;
  GHOSTFS_item_t *p_new_file;

  /* checks whether the FS is already loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!new_file_name)
    return ERR_GHOSTFS_ARGS;
  if(!p_new_file_handle)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(new_file_name))
    return ERR_GHOSTFS_ARGS;

  /* get the parent and lock it for writing */

  p_parent = GetEntry_From_Handle(parent_handle);
  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_parent->entry_lock);

  /* check type */
  if(p_parent->type != GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* First try looking up the entry (check if it does not exist) */

  rc = Find_Entry(p_parent, new_file_name, &tmphandle);

  if(rc == 0)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_EXIST;
    }

  if(rc != ERR_GHOSTFS_NOENT)
    {
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* creates the new entry */
  p_new_file = create_new_ghostfs_entry(GHOSTFS_FILE);

  if(p_new_file == NULL)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_MALLOC;
    }

  /* fill file attributes */

  p_new_file->attributes.uid = owner;
  p_new_file->attributes.gid = group;
  p_new_file->attributes.mode = mode;

  p_new_file->attributes.atime =
      p_new_file->attributes.ctime =
      p_new_file->attributes.mtime = p_new_file->attributes.creationTime = time(NULL);

  p_new_file->attributes.size = 0;

  p_new_file_handle->inode = p_new_file->inode;
  p_new_file_handle->magic = p_new_file->magic;

  /* add named entry into the parent directory */

  rc = Add_Dir_Entry(p_parent, *p_new_file_handle, new_file_name);
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_new_file->entry_lock);
      return rc;
    }
  p_new_file->linkcount++;

  /* update parent mtime and ctime */
  p_parent->attributes.mtime = p_parent->attributes.ctime = time(NULL);

  /* return new file attributes (if asked) */

  if(p_new_file_attrs != NULL)
    fill_attributes(p_new_file, p_new_file_attrs);

  /* unlock and return */

  V_w(&p_parent->entry_lock);
  V_w(&p_new_file->entry_lock);

  return ERR_GHOSTFS_NO_ERROR;

}                               /* GHOSTFS_Create */

int GHOSTFS_Link(GHOSTFS_handle_t parent_handle,
                 char *new_link_name,
                 GHOSTFS_handle_t target_handle, GHOSTFS_Attrs_t * p_link_attrs)
{
  int rc;
  GHOSTFS_handle_t tmphandle;
  GHOSTFS_item_t *p_parent;
  GHOSTFS_item_t *p_object;

  /* checks whether the FS is already loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!new_link_name)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(new_link_name))
    return ERR_GHOSTFS_ARGS;

  /* get the parent and lock it for writing */

  p_parent = GetEntry_From_Handle(parent_handle);
  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_parent->entry_lock);

  /* check type */
  if(p_parent->type != GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* First try looking up the entry (check if it does not exist) */

  rc = Find_Entry(p_parent, new_link_name, &tmphandle);

  if(rc == 0)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_EXIST;
    }

  if(rc != ERR_GHOSTFS_NOENT)
    {
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* get the target item and lock it for mofification */
  p_object = GetEntry_From_Handle(target_handle);

  if(p_object == NULL)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_STALE;
    }

  if(p_object->type == GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_ISDIR;
    }

  P_w(&p_object->entry_lock);

  /* add named entry into the parent directory */

  rc = Add_Dir_Entry(p_parent, target_handle, new_link_name);
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_object->entry_lock);
      return rc;
    }

  /* update file & parent attributes */
  p_object->linkcount++;
  p_object->attributes.ctime =
      p_parent->attributes.mtime = p_parent->attributes.ctime = time(NULL);

  /* return new file attributes (if asked) */

  if(p_link_attrs != NULL)
    fill_attributes(p_object, p_link_attrs);

  /* unlock and return */

  V_w(&p_parent->entry_lock);
  V_w(&p_object->entry_lock);

  return ERR_GHOSTFS_NO_ERROR;

}                               /* GHOSTFS_Create */

int GHOSTFS_Symlink(GHOSTFS_handle_t parent_handle,
                    char *new_symlink_name,
                    char *symlink_content,
                    GHOSTFS_user_t owner,
                    GHOSTFS_group_t group,
                    GHOSTFS_perm_t mode,
                    GHOSTFS_handle_t * p_new_symlink_handle,
                    GHOSTFS_Attrs_t * p_new_symlink_attrs)
{
  int rc;
  GHOSTFS_handle_t newhandle, tmphandle;

  GHOSTFS_item_t *p_parent;
  GHOSTFS_item_t *p_new_lnk;

  /* checks whether the FS is already loaded. */
  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */
  if(!new_symlink_name)
    return ERR_GHOSTFS_ARGS;
  if(!p_new_symlink_handle)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(new_symlink_name))
    return ERR_GHOSTFS_ARGS;

  /* get the parent and lock it for writing */

  p_parent = GetEntry_From_Handle(parent_handle);
  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_parent->entry_lock);

  /* check type */
  if(p_parent->type != GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* First try looking up the entry (check if it does not exist) */

  rc = Find_Entry(p_parent, new_symlink_name, &tmphandle);

  if(rc == 0)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_EXIST;
    }

  if(rc != ERR_GHOSTFS_NOENT)
    {
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* creates the new entry */
  p_new_lnk = create_new_ghostfs_entry(GHOSTFS_LNK);

  if(p_new_lnk == NULL)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_MALLOC;
    }

  /* fill symlink attributes & content */
  strncpy(p_new_lnk->ITEM_SYMLNK.linkdata, symlink_content, GHOSTFS_MAX_PATH);
  p_new_lnk->attributes.size = strlen(symlink_content);

  p_new_lnk->attributes.uid = owner;
  p_new_lnk->attributes.gid = group;
  p_new_lnk->attributes.mode = mode;

  p_new_lnk->attributes.atime =
      p_new_lnk->attributes.ctime =
      p_new_lnk->attributes.mtime = p_new_lnk->attributes.creationTime = time(NULL);

  p_new_symlink_handle->inode = p_new_lnk->inode;
  p_new_symlink_handle->magic = p_new_lnk->magic;

  /* add named entry into the parent directory */

  rc = Add_Dir_Entry(p_parent, *p_new_symlink_handle, new_symlink_name);
  if(rc)
    {
      V_w(&p_parent->entry_lock);
      V_w(&p_new_lnk->entry_lock);
      return rc;
    }
  p_new_lnk->linkcount++;

  /* update parent mtime and ctime */
  p_parent->attributes.mtime = p_parent->attributes.ctime = time(NULL);

  /* return new file attributes (if asked) */

  if(p_new_symlink_attrs != NULL)
    fill_attributes(p_new_lnk, p_new_symlink_attrs);

  /* unlock and return */

  V_w(&p_parent->entry_lock);
  V_w(&p_new_lnk->entry_lock);

  return ERR_GHOSTFS_NO_ERROR;

}                               /* GHOSTFS_Symlink */

/* removes a filesystem entry */
int GHOSTFS_Unlink(GHOSTFS_handle_t parent_handle,      /* IN */
                   char *object_name,   /* IN */
                   GHOSTFS_Attrs_t * p_parent_attrs)    /* [IN/OUT ] */
{
  GHOSTFS_item_t *p_parent;
  GHOSTFS_item_t *p_object;

  GHOSTFS_handle_t obj_handle;
  int rc;

  /* checks whether the FS is already loaded. */

  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */

  if(!object_name)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(object_name))
    return ERR_GHOSTFS_ARGS;

  /* get the parent and lock it for writing */

  p_parent = GetEntry_From_Handle(parent_handle);
  if(p_parent == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_parent->entry_lock);

  /* check type */

  if(p_parent->type != GHOSTFS_DIR)
    {
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTDIR;
    }

  /* First try looking up the entry (check if it exists) */

  rc = Find_Entry(p_parent, object_name, &obj_handle);

  if(rc)
    {
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* get the object to be deleted and lock it for writing */

  p_object = GetEntry_From_Handle(obj_handle);
  if(p_object == NULL)
    return ERR_GHOSTFS_STALE;

  P_w(&p_object->entry_lock);

  /* test if it is a non empty directory */

  if(p_object->type == GHOSTFS_DIR && !is_empty_dir(p_object))
    {
      V_w(&p_object->entry_lock);
      V_w(&p_parent->entry_lock);
      return ERR_GHOSTFS_NOTEMPTY;
    }

  /* removes the object from the directory */

  if((rc = Remove_Entry(p_parent, object_name)))
    {
      V_w(&p_object->entry_lock);
      V_w(&p_parent->entry_lock);
      return rc;
    }

  /* update parent mtime and ctime */
  p_parent->attributes.mtime = p_parent->attributes.ctime = time(NULL);

  /* If it is a directory, destroy '.' and '..' from
   * the list and then the directory itself.
   * update parent's linkcount.
   */
  if(p_object->type == GHOSTFS_DIR)
    {
      GHOSTFS_dirlist_t *dirl;
      GHOSTFS_dirlist_t *next;

      dirl = p_object->ITEM_DIR.direntries;

      while(dirl)
        {
          next = dirl->next;
          Mem_Free(dirl);
          dirl = next;
        }

      p_parent->linkcount--;

      /* destroy the entry */

      rw_lock_destroy(&p_object->entry_lock);
      Mem_Free(p_object);

    }                           /* dir */
  else
    {
      /* If it is a file or symlink, decrease its linkcount,
       * if it is null, we can destroy the objet.
       */

      p_object->linkcount--;

      if(p_object->linkcount == 0)
        {
          /* destroy the entry */
          rw_lock_destroy(&p_object->entry_lock);
          Mem_Free(p_object);
        }
      else
        {
          /* unlock the object */
          V_w(&p_object->entry_lock);
        }

    }                           /* file or symlink */

  /* at this point, the object is unlocked or destroyed
   * no need to unlock it there.
   */

  /* send back parent object attributes */

  if(p_parent_attrs != NULL)
    fill_attributes(p_parent, p_parent_attrs);

  /* unlock the parent and return */

  V_w(&p_parent->entry_lock);
  return ERR_GHOSTFS_NO_ERROR;

}

/* The most complex call */

int GHOSTFS_Rename(GHOSTFS_handle_t src_dir_handle,
                   GHOSTFS_handle_t tgt_dir_handle,
                   char *src_name,
                   char *tgt_name,
                   GHOSTFS_Attrs_t * p_src_dir_attrs, GHOSTFS_Attrs_t * p_tgt_dir_attrs)
{
  GHOSTFS_item_t *p_parent1;
  GHOSTFS_item_t *p_parent2;
  GHOSTFS_item_t *p_object1;
  GHOSTFS_item_t *p_object2;

  GHOSTFS_handle_t tmphandle, srchandle;
  int rc;

  int src_eq_tgt = ((src_dir_handle.inode == tgt_dir_handle.inode) &&
                    (src_dir_handle.magic == tgt_dir_handle.magic));

  int target_exists;

  /* checks whether the FS is already loaded. */

  if(!p_root)
    return ERR_GHOSTFS_NOTINIT;

  /* checks args. */

  if(!src_name)
    return ERR_GHOSTFS_ARGS;
  if(!tgt_name)
    return ERR_GHOSTFS_ARGS;
  if(!is_name_ok(tgt_name))
    return ERR_GHOSTFS_ARGS;

  /* if the source directory = target directory, lock only once */
  if(src_eq_tgt)
    {
      /* get the parent and lock it for writing */

      p_parent1 = GetEntry_From_Handle(src_dir_handle);
      if(p_parent1 == NULL)
        return ERR_GHOSTFS_STALE;

      /* check type */
      if(p_parent1->type != GHOSTFS_DIR)
        return ERR_GHOSTFS_NOTDIR;

      P_w(&p_parent1->entry_lock);

      p_parent2 = p_parent1;
    }
  else
    {
      /* get the parents and lock them for writing */

      p_parent1 = GetEntry_From_Handle(src_dir_handle);
      p_parent2 = GetEntry_From_Handle(tgt_dir_handle);

      if(p_parent1 == NULL || p_parent2 == NULL)
        return ERR_GHOSTFS_STALE;

      /* check type */
      if(p_parent1->type != GHOSTFS_DIR || p_parent2->type != GHOSTFS_DIR)
        return ERR_GHOSTFS_NOTDIR;

      /* always lock dirs in the same order for avoiding deadlocks */
      if(src_dir_handle.inode > tgt_dir_handle.inode)
        {
          P_w(&p_parent1->entry_lock);
          P_w(&p_parent2->entry_lock);
        }
      else
        {
          P_w(&p_parent2->entry_lock);
          P_w(&p_parent1->entry_lock);
        }
    }

  /* 1- First try looking up the source entry (check if it exists) */

  rc = Find_Entry(p_parent1, src_name, &srchandle);

  if(rc != 0)
    {
      V_w(&p_parent1->entry_lock);
      if(!src_eq_tgt)
        V_w(&p_parent2->entry_lock);
      return rc;
    }

  p_object1 = GetEntry_From_Handle(srchandle);

  if(p_object1 == NULL)
    {
      V_w(&p_parent1->entry_lock);
      if(!src_eq_tgt)
        V_w(&p_parent2->entry_lock);
      return ERR_GHOSTFS_STALE;
    }

  /* 2- try looking up the target entry (check if it does not exist) */

  target_exists = FALSE;
  rc = Find_Entry(p_parent2, tgt_name, &tmphandle);

  if(rc == 0)
    target_exists = TRUE;
  else if(rc != ERR_GHOSTFS_NOENT)
    {
      V_w(&p_parent1->entry_lock);
      if(!src_eq_tgt)
        V_w(&p_parent2->entry_lock);
      return rc;
    }

  /* 3 - if source handle = destination handle,
   *     return attributes and do nothing.
   */
  if((srchandle.inode == tmphandle.inode) && (tmphandle.magic == tgt_dir_handle.magic))
    {
      if(p_src_dir_attrs)
        fill_attributes(p_parent1, p_src_dir_attrs);
      if(p_tgt_dir_attrs)
        fill_attributes(p_parent2, p_tgt_dir_attrs);
      LogFullDebug(COMPONENT_FSAL, "src=tgt");
      V_w(&p_parent1->entry_lock);
      return ERR_GHOSTFS_NO_ERROR;
    }

  /* 4- if the target exists it must be compatible (for removal) */
  if(target_exists)
    {
      /* get target object */

      p_object2 = GetEntry_From_Handle(tmphandle);

      if(p_object2 == NULL)
        {
          V_w(&p_parent1->entry_lock);
          if(!src_eq_tgt)
            V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_STALE;
        }

      /* lock the target before removal */

      P_w(&p_object2->entry_lock);

      /* check compatibility */
      LogFullDebug(COMPONENT_FSAL, "type1=%d, type2=%d, dir=%d", p_object1->type, p_object2->type,
             GHOSTFS_DIR);
      if(p_object2->type == GHOSTFS_DIR)
        LogFullDebug(COMPONENT_FSAL, "2 empty : %d", is_empty_dir(p_object2));

      if((p_object1->type == GHOSTFS_DIR) && (p_object2->type == GHOSTFS_DIR))
        {

          if(!is_empty_dir(p_object2))
            {
              V_w(&p_object2->entry_lock);
              V_w(&p_parent1->entry_lock);
              if(!src_eq_tgt)
                V_w(&p_parent2->entry_lock);
              return ERR_GHOSTFS_NOTEMPTY;
            }

          /* compatible types, we remove the target directory */

          GHOSTFS_dirlist_t *dirl;
          GHOSTFS_dirlist_t *next;

          /* removes the object from the directory */

          if((rc = Remove_Entry(p_parent2, tgt_name)))
            {
              V_w(&p_object2->entry_lock);
              V_w(&p_parent1->entry_lock);
              if(!src_eq_tgt)
                V_w(&p_parent2->entry_lock);
              return rc;
            }

          /* update parent mtime and ctime */
          p_parent2->attributes.mtime = p_parent2->attributes.ctime = time(NULL);

          /* destroy '.' and '..' from the list
           * and then the directory itself.
           * update parent's linkcount.
           */

          dirl = p_object2->ITEM_DIR.direntries;

          while(dirl)
            {
              next = dirl->next;
              Mem_Free(dirl);
              dirl = next;
            }

          p_parent2->linkcount--;

          /* destroy the entry */

          rw_lock_destroy(&p_object2->entry_lock);
          Mem_Free(p_object2);

        }
      else if((p_object1->type != GHOSTFS_DIR) && (p_object2->type != GHOSTFS_DIR))
        {
          /* compatible types, we remove the target file/link */

          /* removes the object from the directory */

          if((rc = Remove_Entry(p_parent2, tgt_name)))
            {
              V_w(&p_object2->entry_lock);
              V_w(&p_parent1->entry_lock);
              if(!src_eq_tgt)
                V_w(&p_parent2->entry_lock);
              return rc;
            }

          /* update parent mtime and ctime */
          p_parent2->attributes.mtime = p_parent2->attributes.ctime = time(NULL);

          /* If it is a file or symlink, decrease its linkcount,
           * if it is null, we can destroy the objet.
           */

          p_object2->linkcount--;

          if(p_object2->linkcount == 0)
            {
              /* destroy the entry */
              rw_lock_destroy(&p_object2->entry_lock);
              Mem_Free(p_object2);
            }
          else
            {
              /* unlock the object */
              V_w(&p_object2->entry_lock);
            }

        }
      else
        {
          /* incompatible types or non empty target dir, return an error */
          V_w(&p_object2->entry_lock);
          V_w(&p_parent1->entry_lock);
          if(!src_eq_tgt)
            V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_EXIST;
        }

    }

  /* end if target exists */
  /* ---- at this point, we are sure the target does not exist ---- */
  /* if the source and target parents are the same */
  if(src_eq_tgt)
    {
      /* directly rename the item in the directory entries */
      rc = Rename_Entry(p_parent1, src_name, tgt_name);

      if(rc != 0)
        {
          /* unexpected error !!! */
          V_w(&p_parent1->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

    }                           /* end if srcpath = tgtpath */
  else if(p_object1->type == GHOSTFS_DIR)
    {
      /* we must remove the directory from the source dir,
       * and put it into the new dir.
       */
      GHOSTFS_dirlist_t *dirl;
      GHOSTFS_dirlist_t *next;

      /* lock the child directory */
      P_w(&p_object1->entry_lock);

      /* removes the dir from the parent */

      if((rc = Remove_Entry(p_parent1, src_name)))
        {
          /* unexpected error !!! */
          V_w(&p_object1->entry_lock);
          V_w(&p_parent1->entry_lock);
          V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

      /* update parent mtime and ctime */
      p_parent1->attributes.mtime = p_parent1->attributes.ctime = time(NULL);

      /* replace the '..' entry with the new directory handle */
      rc = Change_Entry_Handle(p_object1, "..", tgt_dir_handle);

      if(rc != 0)
        {
          /* unexpected error !!! */
          V_w(&p_object1->entry_lock);
          V_w(&p_parent1->entry_lock);
          V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

      /* decrease old parent linkcount and increase new parent linkcount,
       * related to their numer of '..' entries pointing on them.
       */
      p_parent1->linkcount--;
      p_parent2->linkcount++;

      /* insert the directory into the target dir */

      if((rc = Add_Dir_Entry(p_parent2, srchandle, tgt_name)))
        {
          /* unexpected error !!! */
          V_w(&p_object1->entry_lock);
          V_w(&p_parent1->entry_lock);
          V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

      /* update new parent mtime and ctime */
      p_parent2->attributes.mtime = p_parent2->attributes.ctime = time(NULL);

      /* the directory inode has changed (not the same .. entry) */
      p_object1->attributes.mtime = p_object1->attributes.ctime = time(NULL);

      /* unlock the child directory */
      V_w(&p_object1->entry_lock);

    }                           /* end if dir */
  else                          /* file and symlinks */
    {
      /* we must remove the object from the source dir */

      /* removes the object from the parent */

      if((rc = Remove_Entry(p_parent1, src_name)))
        {
          /* unexpected error !!! */
          V_w(&p_object1->entry_lock);
          V_w(&p_parent1->entry_lock);
          V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

      /* update parent mtime and ctime */
      p_parent1->attributes.mtime = p_parent1->attributes.ctime = time(NULL);

      /* insert the object into the target dir */

      if((rc = Add_Dir_Entry(p_parent2, srchandle, tgt_name)))
        {
          /* unexpected error !!! */
          V_w(&p_object1->entry_lock);
          V_w(&p_parent1->entry_lock);
          V_w(&p_parent2->entry_lock);
          return ERR_GHOSTFS_INTERNAL;
        }

      /* update new parent mtime and ctime */
      p_parent2->attributes.mtime = p_parent2->attributes.ctime = time(NULL);

    }                           /* end if file and symlinks */

  /* copy attributes if needed, and return */

  if(p_src_dir_attrs)
    fill_attributes(p_parent1, p_src_dir_attrs);
  if(p_tgt_dir_attrs)
    fill_attributes(p_parent2, p_tgt_dir_attrs);

  V_w(&p_parent1->entry_lock);
  if(!src_eq_tgt)
    V_w(&p_parent2->entry_lock);
  return ERR_GHOSTFS_NO_ERROR;

}
