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
 * \file    ghost_fs.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:23 $
 * \version $Revision: 1.17 $
 * \brief   Interface of a very simple file system in memory,
 *          used for basic tests.
 *          Thread-safe.
 *
 */

#ifndef _GHOST_FS_H
#define _GHOST_FS_H

#include "err_ghost_fs.h"
#include <sys/types.h>
#include "RW_Lock.h"

/* Maximum name lengths */
#define GHOSTFS_MAX_FILENAME    256
#define GHOSTFS_MAX_PATH        1024

/* types */

/** link count type */
typedef unsigned int GHOSTFS_count_t;
/** time format */
typedef time_t GHOSTFS_time_t;
/** file size type*/
typedef unsigned long long GHOSTFS_size_t;
/** metadatasize type */
typedef unsigned int GHOSTFS_mdsize_t;
/** user identifier type */
typedef int GHOSTFS_user_t;
/** group identifier type */
typedef int GHOSTFS_group_t;
/** inode type */
typedef caddr_t GHOSTFS_inode_t;

/* handle type */
typedef struct GHOSTFS_handle__
{
  GHOSTFS_inode_t inode;
  unsigned int magic;
} GHOSTFS_handle_t;

/** Values for the type of filesystem objects */
typedef enum GHOSTFS_typeitem_t
{
  GHOSTFS_DIR,
  GHOSTFS_FILE,
  GHOSTFS_LNK
} GHOSTFS_typeitem_t;

/** file access permissions */
typedef int GHOSTFS_perm_t;
#define  GHOSTFS_UR   0400
#define  GHOSTFS_UW   0200
#define  GHOSTFS_UX   0100
#define  GHOSTFS_GR   0040
#define  GHOSTFS_GW   0020
#define  GHOSTFS_GX   0010
#define  GHOSTFS_OR   0004
#define  GHOSTFS_OW   0002
#define  GHOSTFS_OX   0001

/* parameter for ghostFS initialization */

typedef struct GHOSTFS_parameter__
{
  GHOSTFS_perm_t root_mode;
  GHOSTFS_user_t root_owner;
  GHOSTFS_group_t root_group;
  int dot_dot_root_eq_root;
  int root_access;

} GHOSTFS_parameter_t;

/* ********* INTERNAL DATA TYPES ************** */

/** List of the entries of a directory */
typedef struct GHOSTFS_dirlist__
{

  GHOSTFS_handle_t handle;
  char name[GHOSTFS_MAX_FILENAME];
  struct GHOSTFS_dirlist__ *next;

} GHOSTFS_dirlist_t;

/** Directory metadatas */
typedef struct GHOSTFS_dir__
{
  /* directory content */
  GHOSTFS_dirlist_t *direntries;

  /* used for insertion */
  GHOSTFS_dirlist_t *lastentry;

} GHOSTFS_dir_t;

/** File metadatas */
typedef struct GHOSTFS_file__
{
  int unused;
} GHOSTFS_file_t;

/** Symlink metadatas */
typedef struct GHOSTFS_symlink__
{
  char linkdata[GHOSTFS_MAX_PATH];
} GHOSTFS_symlink_t;

/* object common attributes */
typedef struct GHOSTFS_metadata__
{
  GHOSTFS_user_t uid;
  GHOSTFS_group_t gid;
  GHOSTFS_perm_t mode;
  GHOSTFS_time_t atime;
  GHOSTFS_time_t mtime;
  GHOSTFS_time_t ctime;
  GHOSTFS_time_t creationTime;
  GHOSTFS_size_t size;
} GHOSTFS_metadata_t;

/**
 * Represents an item in the filesystem,
 * identified by a handle 'inode+magic'.
 */
typedef struct GHOSTFS_item__
{

  rw_lock_t entry_lock;         /* RW lock on the element */

  GHOSTFS_inode_t inode;        /* inode of this element */

  unsigned int magic;           /* magic number to indicate
                                 * if the entry is still valid. */

  GHOSTFS_count_t linkcount;    /* number of pointer on
                                   this element in the namespace */

  GHOSTFS_typeitem_t type;      /* type of this element */

  GHOSTFS_metadata_t attributes;        /* attributes of this element */

  union
  {

    /* directory */
    GHOSTFS_dir_t dir;
    /* file */
    GHOSTFS_file_t file;
    /* symlink */
    GHOSTFS_symlink_t symlink;

  } content_u;

} GHOSTFS_item_t;

/**
 *  Filesystem stats
 */
typedef struct GHOSTFS_stats__
{
  GHOSTFS_count_t nb_dir;
  GHOSTFS_count_t nb_file;
  GHOSTFS_count_t nb_lnk;
} GHOSTFS_stats_t;

#define ITEM_DIR     content_u.dir
#define ITEM_FILE    content_u.file
#define ITEM_SYMLNK  content_u.symlink

/*
 *  Output data types
 */

typedef GHOSTFS_dirlist_t *GHOSTFS_cookie_t;

/** Entry in a directory */
typedef struct GHOSTFS_dirent__
{

  GHOSTFS_handle_t handle;
  char name[GHOSTFS_MAX_FILENAME];
  GHOSTFS_cookie_t cookie;

} GHOSTFS_dirent_t;

/** Common Attributes */
typedef struct GHOSTFS_Attrs__
{

  GHOSTFS_inode_t inode;
  GHOSTFS_count_t linkcount;
  GHOSTFS_typeitem_t type;
  GHOSTFS_user_t uid;
  GHOSTFS_group_t gid;
  GHOSTFS_perm_t mode;
  GHOSTFS_time_t atime;
  GHOSTFS_time_t mtime;
  GHOSTFS_time_t ctime;
  GHOSTFS_time_t creationTime;
  GHOSTFS_size_t size;

} GHOSTFS_Attrs_t;

/* Attribute mask used for setattr */
typedef unsigned char GHOSTFS_setattr_mask_t;

#define SETATTR_UID     0x01
#define SETATTR_GID     0x02
#define SETATTR_MODE    0x04
#define SETATTR_ATIME   0x08
#define SETATTR_MTIME   0x10
#define SETATTR_CTIME   0x20
#define SETATTR_SIZE    0x40

/* directory stream descriptor */
typedef struct dir_descriptor__
{

  GHOSTFS_handle_t handle;
  GHOSTFS_dir_t *master_record;
  GHOSTFS_dirlist_t *current_dir_entry;

} dir_descriptor_t;

/* Initialise the filesystem and creates the root entry. */
int GHOSTFS_Init(GHOSTFS_parameter_t init_cfg);

/** Gets the root directory handle. */
int GHOSTFS_GetRoot(GHOSTFS_handle_t * root_handle);

/** Find a named object in the filesystem.
 *  The item can't contain slashes,
 *  but it can equal to . or ..
 */
int GHOSTFS_Lookup(GHOSTFS_handle_t handle_parent,
                   char *ghostfs_name, GHOSTFS_handle_t * p_handle);

/** Gets the attributes of an object in the filesystem. */
int GHOSTFS_GetAttrs(GHOSTFS_handle_t handle, GHOSTFS_Attrs_t * object_attributes);

/** Constants for permissions tests */
typedef int GHOSTFS_testperm_t;
#define GHOSTFS_TEST_READ    4
#define GHOSTFS_TEST_WRITE   2
#define GHOSTFS_TEST_EXEC    1

/** Tests whether a user can access an object
    by the ways defined in test_set */
int GHOSTFS_Access(GHOSTFS_handle_t handle,
                   GHOSTFS_testperm_t test_set,
                   GHOSTFS_user_t userid, GHOSTFS_group_t groupid);

/** Reads the content of a symlink */
int GHOSTFS_ReadLink(GHOSTFS_handle_t handle, char *buffer, GHOSTFS_mdsize_t buff_size);

/** Opens a directory stream */
int GHOSTFS_Opendir(GHOSTFS_handle_t handle, dir_descriptor_t * dir);

/** Reads an entry from an opened directory stream */
int GHOSTFS_Readdir(dir_descriptor_t * dir, GHOSTFS_dirent_t * dirent);

/** sets the position into a directory stream.
 *  If cookie = NULL, it restarts from the beginning.
 */
int GHOSTFS_Seekdir(dir_descriptor_t * dir, GHOSTFS_cookie_t cookie);

/** Closes a directory stream */
int GHOSTFS_Closedir(dir_descriptor_t * dir);

/** set object attributes */
int GHOSTFS_SetAttrs(GHOSTFS_handle_t handle,
                     GHOSTFS_setattr_mask_t setattr_mask, GHOSTFS_Attrs_t attrs_values);

int GHOSTFS_MkDir(GHOSTFS_handle_t parent_handle,
                  char *new_dir_name,
                  GHOSTFS_user_t owner,
                  GHOSTFS_group_t group,
                  GHOSTFS_perm_t mode,
                  GHOSTFS_handle_t * p_new_dir_handle, GHOSTFS_Attrs_t * p_new_dir_attrs);

int GHOSTFS_Create(GHOSTFS_handle_t parent_handle,
                   char *new_file_name,
                   GHOSTFS_user_t owner,
                   GHOSTFS_group_t group,
                   GHOSTFS_perm_t mode,
                   GHOSTFS_handle_t * p_new_file_handle,
                   GHOSTFS_Attrs_t * p_new_file_attrs);

int GHOSTFS_Link(GHOSTFS_handle_t parent_handle,
                 char *new_link_name,
                 GHOSTFS_handle_t target_handle, GHOSTFS_Attrs_t * p_link_attrs);

int GHOSTFS_Symlink(GHOSTFS_handle_t parent_handle,
                    char *new_symlink_name,
                    char *symlink_content,
                    GHOSTFS_user_t owner,
                    GHOSTFS_group_t group,
                    GHOSTFS_perm_t mode,
                    GHOSTFS_handle_t * p_new_symlink_handle,
                    GHOSTFS_Attrs_t * p_new_symlink_attrs);

int GHOSTFS_Unlink(GHOSTFS_handle_t parent_handle,      /* IN */
                   char *object_name,   /* IN */
                   GHOSTFS_Attrs_t * p_parent_attrs);   /* [IN/OUT ] */

int GHOSTFS_Rename(GHOSTFS_handle_t src_dir_handle,
                   GHOSTFS_handle_t tgt_dir_handle,
                   char *src_name,
                   char *tgt_name,
                   GHOSTFS_Attrs_t * p_src_dir_attrs, GHOSTFS_Attrs_t * p_tgt_dir_attrs);

#endif                          /* _GHOST_FS_H */
