/**
 *
 * \file    commands.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 15:04:22 $
 * \version $Revision: 1.52 $
 * \brief   Header file for processing user's command line.
 *
 */

#ifndef _COMMANDS_H
#define _COMMANDS_H

#include "shell_types.h"
#include <stdio.h>

/*----------------------------------*
 *    FSAL commands prototypes.
 *----------------------------------*/

/* a setloglevel command is needed for each layer */
void fsal_layer_SetLogLevel(int log_lvl);

/** inits the filesystem. */
int fn_fsal_init_fs(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** proceed an pwd command. */
int fn_fsal_pwd(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

/** proceed a cd command. */
int fn_fsal_cd(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** proceed a stat command. */
int fn_fsal_stat(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** proceed an ls command. */
int fn_fsal_ls(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** display statistics about FSAL calls. */
int fn_fsal_callstat(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** change current user. */
int fn_fsal_su(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** unlink an fs object. */
int fn_fsal_unlink(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

/** create a directory. */
int fn_fsal_mkdir(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** create a directory. */
int fn_fsal_rename(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

/** create a symlink. */
int fn_fsal_ln(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** create a hardlink. */
int fn_fsal_hardlink(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** create a regular file. */
int fn_fsal_create(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

/** change file attributes. */
int fn_fsal_setattr(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** test access rights. */
int fn_fsal_access(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

/** truncate file. */
int fn_fsal_truncate(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** open a file. */
int fn_fsal_open(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** open a file (using FSAL_open_by_name). */
int fn_fsal_open_byname(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output   /* IN : output stream          */
    );

/** open a file (using FSAL_open_by_name). */
int fn_fsal_open_byfileid(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */
    );

/** read from file. */
int fn_fsal_read(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** write to file. */
int fn_fsal_write(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** close a file. */
int fn_fsal_close(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** close a file. */
int fn_fsal_close_byfileid(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** display a file. */
int fn_fsal_cat(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

/** copy a file to/from local path. */
int fn_fsal_rcp(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

/** cross a junction. */
int fn_fsal_cross(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** handle operations */
int fn_fsal_handle(int argc,     /* IN : number of args in argv */
                   char **argv,  /* IN : arg list               */
                   FILE * output /* IN : output stream          */
    );


/** compare 2 handles. */
int fn_fsal_handlecmp(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list   */
                      FILE * output     /* IN : output stream */
    );

/** list extended attributes. */
int fn_fsal_lsxattrs(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** display an extended attribute. */
int fn_fsal_getxattr(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

#ifdef _USE_MFSL
void mfsl_layer_SetLogLevel(int log_lvl);

int fn_mfsl_init(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output);        /* IN : output stream          */

int fn_mfsl_pwd(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

int fn_mfsl_cd(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

int fn_mfsl_stat(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

int fn_mfsl_ls(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output);

int fn_mfsl_su(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

int fn_mfsl_unlink(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int fn_mfsl_mkdir(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int fn_mfsl_rename(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int fn_mfsl_ln(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

int fn_mfsl_hardlink(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

int fn_mfsl_create(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int fn_mfsl_setattr(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

int fn_mfsl_access(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

int fn_mfsl_truncate(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

int fn_mfsl_open(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

int fn_mfsl_read(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

int fn_mfsl_write(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int fn_mfsl_close(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

int fn_mfsl_cat(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

int fn_mfsl_handlecmp(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list   */
                      FILE * output     /* IN : output stream */
    );

#endif                          /* _USE_MFSL */

/*----------------------------------*
 * Cache_inode commands prototypes.
 *----------------------------------*/

/* a setloglevel command is needed for each layer */
void Cache_inode_layer_SetLogLevel(int log_lvl);

/** inits the filesystem. */
int fn_Cache_inode_cache_init(int argc, /* IN : number of args in argv */
                              char **argv,      /* IN : arg list               */
                              FILE * output     /* IN : output stream          */
    );

/** proceed an pwd command. */
int fn_Cache_inode_pwd(int argc,        /* IN : number of args in argv */
                       char **argv,     /* IN : arg list               */
                       FILE * output    /* IN : output stream          */
    );

/** proceed a cd command. */
int fn_Cache_inode_cd(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output     /* IN : output stream          */
    );

/** proceed a stat command. */
int fn_Cache_inode_stat(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output   /* IN : output stream          */
    );

/** proceed to a call to the garbagge collector. */
int fn_Cache_inode_gc(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output);   /* IN : output stream          */

/** proceed an ls command. */
int fn_Cache_inode_ls(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output     /* IN : output stream          */
    );

/** proceed a hard link (hard link) command. */
int fn_Cache_inode_link(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output /* IN : output stream          */ );

/** proceed a mkdir command. */
int fn_Cache_inode_mkdir(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ );

/** proceed a rename command. */
int fn_Cache_inode_rename(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ );

/** proceed an create command. */
int fn_Cache_inode_create(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ );

/** proceed an ln (symlink) command. */
int fn_Cache_inode_ln(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output /* IN : output stream          */ );

/** proceed an open by name (open_by_name) command. */
int fn_Cache_inode_open_by_name(int argc,       /* IN : number of args in argv */
                                char **argv,    /* IN : arg list               */
                                FILE * output /* IN : output stream          */ );

/** Close a previously opened file */
int fn_Cache_inode_close(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ );

/** setattr
 *
 * syntax of command line:
 * setattr file_path  attribute_name  attribute_value
 *
 */
int fn_Cache_inode_setattr(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output /* IN : output stream          */ );

/** proceed an unlink command. */
int fn_Cache_inode_unlink(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ );

/** display statistics about FSAL calls. */
int fn_Cache_inode_callstat(int argc,   /* IN : number of args in argv */
                            char **argv,        /* IN : arg list               */
                            FILE * output /* IN : output stream          */ );

/** cache en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_data_cache(int argc, /* IN : number of args in argv */
                              char **argv,      /* IN : arg list               */
                              FILE * output /* IN : output stream          */ );

/** recover the data cache */
int fn_Cache_inode_recover_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ );

/** refresh en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_refresh_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ );

/** flush en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_flush_cache(int argc,        /* IN : number of args in argv */
                               char **argv,     /* IN : arg list               */
                               FILE * output /* IN : output stream          */ );

/** cache en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_release_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ );

/** Reads the content of a cached regular file */
int fn_Cache_inode_read(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output /* IN : output stream          */ );

/** Reads the content of a cached regular file */
int fn_Cache_inode_write(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ );

/** Change current user */
int fn_Cache_inode_su(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output /* IN : output stream          */ );

/**
 * perform an access command.
 * syntax: access [F][R][W][X] <file>
 * example: access toto FRX
 */

int fn_Cache_inode_access(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ );

/**
 * perform an invalidate command.
 * syntax: invalidate  <file>
 * example: invalidate toto 
 */
int fn_Cache_inode_invalidate(int argc,      /* IN : number of args in argv */
                              char **argv,   /* IN : arg list               */
                              FILE * output  /* IN : output stream          */ ) ;

/*----------------------------------*
 *      NFS commands prototypes.
 *----------------------------------*/

/* a setloglevel command is needed for each layer */
void nfs_layer_SetLogLevel(int log_lvl);

/** process NFS layer initialization. */
int fn_nfs_init(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

/** process MNT1 protocol's command. */
int fn_MNT1_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** process MNT3 protocol's command. */
int fn_MNT3_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** process NFS2 protocol's command. */
int fn_NFS2_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** process NFS3 protocol's command. */
int fn_NFS3_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** process a cd command using NFS protocol. */
int fn_nfs_cd(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    );

/** process an ls command using NFS protocol. */
int fn_nfs_ls(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    );

/** process a mount command using MOUNT protocol. */
int fn_nfs_mount(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** process an umount command using MOUNT protocol. */
int fn_nfs_umount(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** process an ls command using NFS protocol. */
int fn_nfs_pwd(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_create(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_mkdir(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_unlink(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_setattr(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_rename(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_hardlink(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_ln(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    );

/** process a stat command using NFS protocol. */
int fn_nfs_stat(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

int fn_nfs_su(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output);   /* IN : output stream          */

int fn_nfs_id(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output);   /* IN : output stream          */

/*----------------------------------*
 *      NFS_remote commands prototypes.
 *----------------------------------*/

/* a setloglevel command is needed for each layer */
void nfs_remote_layer_SetLogLevel(int log_lvl);

/** process RPC clients initialization. */
int fn_rpc_init(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    );

/** process MNT1 protocol's command. */
int fn_MNT1_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** process MNT3 protocol's command. */
int fn_MNT3_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** process NFS2 protocol's command. */
int fn_NFS2_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** process NFS3 protocol's command. */
int fn_NFS3_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** process a cd command using NFS protocol. */
int fn_nfs_remote_cd(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** process an ls command using NFS protocol. */
int fn_nfs_remote_ls(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** process an mount command using MOUNT protocol. */
int fn_nfs_remote_mount(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output   /* IN : output stream          */
    );

/** process a umount command using MOUNT protocol. */
int fn_nfs_remote_umount(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    );

/** process an ls command using NFS protocol. */
int fn_nfs_remote_pwd(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output     /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_create(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_mkdir(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output   /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_unlink(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_setattr(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_rename(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_hardlink(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    );

/** process an create command using NFS protocol. */
int fn_nfs_remote_ln(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    );

/** process a stat command using NFS protocol. */
int fn_nfs_remote_stat(int argc,        /* IN : number of args in argv */
                       char **argv,     /* IN : arg list               */
                       FILE * output    /* IN : output stream          */
    );

int fn_nfs_remote_su(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output);    /* IN : output stream          */

int fn_nfs_remote_id(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output);    /* IN : output stream          */

/*------------------------------------------
 *       Layers and commands definitions
 *-----------------------------------------*/

/* FSAL command list */

static command_def_t __attribute__ ((__unused__)) commands_FSAL[] =
{
  {
  "access", fn_fsal_access, "test access rights"},
  {
  "callstat", fn_fsal_callstat, "display stats about FSAL calls"},
  {
  "cat", fn_fsal_cat, "display the content of a file"},
  {
  "cd", fn_fsal_cd, "change current directory"},
  {
  "close", fn_fsal_close, "close an opened file"},
  {
  "close_byfileid", fn_fsal_close, "close an opened file by fileid"},
  {
  "create", fn_fsal_create, "create a regular file"},
  {
  "cross", fn_fsal_cross, "traverse a junction"},
  {
  "getxattr", fn_fsal_getxattr, "display the value of an extended attribute"},
  {"handle", fn_fsal_handle, "handle digest/expend operations"},
  {"handlecmp", fn_fsal_handlecmp, "compare 2 handles"},
  {
  "hardlink", fn_fsal_hardlink, "create a hardlink"},
  {
  "init_fs", fn_fsal_init_fs, "initialize filesystem"},
  {
  "ln", fn_fsal_ln, "create a symlink"},
  {
  "ls", fn_fsal_ls, "list contents of directory"},
  {
  "lsxattrs", fn_fsal_lsxattrs, "list extended attributes for an object"},
  {
  "mkdir", fn_fsal_mkdir, "create a directory"},
  {
  "open", fn_fsal_open, "open an existing file"},
  {
  "open_byname", fn_fsal_open_byname, "open an existing file by name"},
  {
  "open_byfileid", fn_fsal_open_byfileid, "open an existing file by fileid"},
  {
  "pwd", fn_fsal_pwd, "print current path"},
  {
  "rcp", fn_fsal_rcp, "copy a file to/from a local path"},
  {
  "read", fn_fsal_read, "read data from current file"},
  {
  "rename", fn_fsal_rename, "rename/move an object"},
  {
  "setattr", fn_fsal_setattr, "change attributes of an object"},
  {
  "stat", fn_fsal_stat, "display stat about a filesystem object"},
  {
  "su", fn_fsal_su, "change current user"},
  {
  "truncate", fn_fsal_truncate, "change file size"},
  {
  "unlink", fn_fsal_unlink, "remove a filesystem object"},
  {
  "write", fn_fsal_write, "write data to current file"},
  {
  NULL, NULL, NULL}             /* End of command list */
};

/* MFSL command list */

#ifdef _USE_MFSL
static command_def_t __attribute__ ((__unused__)) commands_MFSL[] =
{
  {
  "access", fn_mfsl_access, "test access rights"},
  {
  "cat", fn_mfsl_cat, "display the content of a file"},
  {
  "cd", fn_mfsl_cd, "change current directory"},
  {
  "close", fn_mfsl_close, "close an opened file"},
  {
  "close_byfileid", fn_mfsl_close, "close an opened file by fileid"},
  {
  "create", fn_mfsl_create, "create a regular file"},
  {
  "handlecmp", fn_mfsl_handlecmp, "compare 2 handles"},
  {
  "hardlink", fn_mfsl_hardlink, "create a hardlink"},
  {
  "init_fs", fn_mfsl_init, "initialize filesystem"},
  {
  "ln", fn_mfsl_ln, "create a symlink"},
  {
  "ls", fn_mfsl_ls, "list contents of directory"},
  {
  "mkdir", fn_mfsl_mkdir, "create a directory"},
  {
  "open", fn_mfsl_open, "open an existing file"},
  {
  "pwd", fn_mfsl_pwd, "print current path"},
  {
  "read", fn_mfsl_read, "read data from current file"},
  {
  "rename", fn_mfsl_rename, "rename/move an object"},
  {
  "setattr", fn_mfsl_setattr, "change attributes of an object"},
  {
  "stat", fn_mfsl_stat, "display stat about a filesystem object"},
  {
  "su", fn_mfsl_su, "change current user"},
  {
  "truncate", fn_mfsl_truncate, "change file size"},
  {
  "unlink", fn_mfsl_unlink, "remove a filesystem object"},
  {
  "write", fn_mfsl_write, "write data to current file"},
  {
  NULL, NULL, NULL}             /* End of command list */
};
#endif                          /* _USE_MFSL */

/* Cache inode command list */

static command_def_t __attribute__ ((__unused__)) commands_Cache_inode[] =
{
  {
  "access", fn_Cache_inode_access, "test access rights"},
  {
  "callstat", fn_Cache_inode_callstat, "display stats about FSAL calls"},
  {
  "cd", fn_Cache_inode_cd, "change current directory"},
  {
  "close", fn_Cache_inode_close, "close the currently opened file"},
  {
  "create", fn_Cache_inode_create, "create regular file"},
  {
  "data_cache", fn_Cache_inode_data_cache, "cache a file in the Data Cache"},
  {
  "flush_cache", fn_Cache_inode_flush_cache, "flushes a previously Data cached entry"},
  {
  "gc", fn_Cache_inode_gc, "run the garbagge collector on the cache inode"},
  {
  "hardlink", fn_Cache_inode_link, "create hard link"},
  {
  "init_cache", fn_Cache_inode_cache_init, "initialize filesystem"},
  {
  "invalidate", fn_Cache_inode_invalidate, "invalidate a cached entry"},
  {
  "ln", fn_Cache_inode_ln, "creates a new symbolic link"},
  {
  "ls", fn_Cache_inode_ls, "list contents of directory"},
  {
  "mkdir", fn_Cache_inode_mkdir, "create a new directory"},
  {
  "open_byname", fn_Cache_inode_open_by_name, "open an existing file by name"},
  {
  "read", fn_Cache_inode_read, "reads the content of a data cached file"},
  {
  "recover_cache", fn_Cache_inode_recover_cache, "recover the data cache after a crash"},
  {
  "refresh_cache", fn_Cache_inode_refresh_cache,
        "refreshes a previously Data cached entry"},
  {
  "release_cache", fn_Cache_inode_release_cache,
        "releases a previously Data cached entry"},
  {
  "rename", fn_Cache_inode_rename, "rename/move an object"},
  {
  "setattr", fn_Cache_inode_setattr, "change attributes of an object"},
  {
  "pwd", fn_Cache_inode_pwd, "print current path"},
  {
  "stat", fn_Cache_inode_stat, "display stat about a filesystem object"},
  {
  "su", fn_Cache_inode_su, "change current user"},
  {
  "unlink", fn_Cache_inode_unlink, "unlink an entry in a directory"},
  {
  "write", fn_Cache_inode_write, "writes the content of a data cached file"},
  {
  NULL, NULL, NULL}             /* End of command list */
};

static command_def_t __attribute__ ((__unused__)) commands_NFS[] =
{
  {
  "nfs_init", fn_nfs_init, "initialize NFS layer"},
  {
  "mnt1_null", fn_MNT1_command, "MNTPROC_NULL v1"},
  {
  "mnt1_mount", fn_MNT1_command, "MNTPROC_MNT v1"},
  {
  "mnt1_dump", fn_MNT1_command, "MNTPROC_DUMP v1"},
  {
  "mnt1_umount", fn_MNT1_command, "MNTPROC_UMNT v1"},
  {
  "mnt1_umount_all", fn_MNT1_command, "MNTPROC_UMNTALL v1"},
  {
  "mnt1_export", fn_MNT1_command, "MNTPROC_EXPORT v1"},
  {
  "mnt3_null", fn_MNT3_command, "MNTPROC_NULL v3"},
  {
  "mnt3_mount", fn_MNT3_command, "MNTPROC_MNT v3"},
  {
  "mnt3_dump", fn_MNT3_command, "MNTPROC_DUMP v3"},
  {
  "mnt3_umount", fn_MNT3_command, "MNTPROC_UMNT v3"},
  {
  "mnt3_umount_all", fn_MNT3_command, "MNTPROC_UMNTALL v3"},
  {
  "mnt3_export", fn_MNT3_command, "MNTPROC_EXPORT v3"},
  {
  "nfs2_null", fn_NFS2_command, "NFSPROC_NULL"},
  {
  "nfs2_getattr", fn_NFS2_command, "NFSPROC_GETATTR"},
  {
  "nfs2_setattr", fn_NFS2_command, "NFSPROC_SETATTR"},
  {
  "nfs2_root", fn_NFS2_command, "NFSPROC_ROOT"},
  {
  "nfs2_lookup", fn_NFS2_command, "NFSPROC_LOOKUP"},
  {
  "nfs2_readlink", fn_NFS2_command, "NFSPROC_READLINK"},
  {
  "nfs2_read", fn_NFS2_command, "NFSPROC_READ"},
  {
  "nfs2_writecache", fn_NFS2_command, "NFSPROC_WRITECACHE"},
  {
  "nfs2_write", fn_NFS2_command, "NFSPROC_WRITE"},
  {
  "nfs2_create", fn_NFS2_command, "NFSPROC_CREATE"},
  {
  "nfs2_remove", fn_NFS2_command, "NFSPROC_REMOVE"},
  {
  "nfs2_rename", fn_NFS2_command, "NFSPROC_RENAME"},
  {
  "nfs2_link", fn_NFS2_command, "NFSPROC_LINK"},
  {
  "nfs2_symlink", fn_NFS2_command, "NFSPROC_SYMLINK"},
  {
  "nfs2_mkdir", fn_NFS2_command, "NFSPROC_MKDIR"},
  {
  "nfs2_rmdir", fn_NFS2_command, "NFSPROC_RMDIR"},
  {
  "nfs2_readdir", fn_NFS2_command, "NFSPROC_READDIR"},
  {
  "nfs2_statfs", fn_NFS2_command, "NFSPROC_STATFS"},
  {
  "nfs3_null", fn_NFS3_command, "NFSPROC3_NULL"},
  {
  "nfs3_getattr", fn_NFS3_command, "NFSPROC3_GETATTR"},
  {
  "nfs3_setattr", fn_NFS3_command, "NFSPROC3_SETATTR"},
  {
  "nfs3_lookup", fn_NFS3_command, "NFSPROC3_LOOKUP"},
  {
  "nfs3_access", fn_NFS3_command, "NFSPROC3_ACCESS"},
  {
  "nfs3_readlink", fn_NFS3_command, "NFSPROC3_READLINK"},
  {
  "nfs3_read", fn_NFS3_command, "NFSPROC3_READ"},
  {
  "nfs3_write", fn_NFS3_command, "NFSPROC3_WRITE"},
  {
  "nfs3_create", fn_NFS3_command, "NFSPROC3_CREATE"},
  {
  "nfs3_mkdir", fn_NFS3_command, "NFSPROC3_MKDIR"},
  {
  "nfs3_symlink", fn_NFS3_command, "NFSPROC3_SYMLINK"},
  {
  "nfs3_mknod", fn_NFS3_command, "NFSPROC3_MKNOD"},
  {
  "nfs3_remove", fn_NFS3_command, "NFSPROC3_REMOVE"},
  {
  "nfs3_rmdir", fn_NFS3_command, "NFSPROC3_RMDIR"},
  {
  "nfs3_rename", fn_NFS3_command, "NFSPROC3_RENAME"},
  {
  "nfs3_link", fn_NFS3_command, "NFSPROC3_LINK"},
  {
  "nfs3_readdir", fn_NFS3_command, "NFSPROC3_READDIR"},
  {
  "nfs3_readdirplus", fn_NFS3_command, "NFSPROC3_READDIRPLUS"},
  {
  "nfs3_fsstat", fn_NFS3_command, "NFSPROC3_FSSTAT"},
  {
  "nfs3_fsinfo", fn_NFS3_command, "NFSPROC3_FSINFO"},
  {
  "nfs3_pathconf", fn_NFS3_command, "NFSPROC3_PATHCONF"},
  {
  "nfs3_commit", fn_NFS3_command, "NFSPROC3_COMMIT"},
  {
  "cd", fn_nfs_cd, "change current directory"},
  {
  "create", fn_nfs_create, "create a regular file"},
  {
  "hardlink", fn_nfs_hardlink, "create a hard link"},
  {
  "ln", fn_nfs_ln, "create a symbolic link"},
  {
  "ls", fn_nfs_ls, "list contents of directory"},
  {
  "mkdir", fn_nfs_mkdir, "create a directory"},
  {
  "mount", fn_nfs_mount, "mount an exported path"},
  {
  "umount", fn_nfs_umount, "umount a mounted path"},
  {
  "pwd", fn_nfs_pwd, "print current path"},
  {
  "rename", fn_nfs_rename, "rename/move an object"},
  {
  "setattr", fn_nfs_setattr, "change object attributes"},
  {
  "stat", fn_nfs_stat, "show file attributes"},
  {
  "su", fn_nfs_su, "change current user"},
  {
  "id", fn_nfs_id, "show who I am"},
  {
  "unlink", fn_nfs_unlink, "remove an object"},
  {
  NULL, NULL, NULL}             /* End of NFS command list. */
};

static command_def_t __attribute__ ((__unused__)) commands_NFS_remote[] =
{
  {
  "rpc_init", fn_rpc_init, "initialize RPCs"},
  {
  "mnt1_null", fn_MNT1_remote_command, "MNTPROC_NULL v1"},
  {
  "mnt1_mount", fn_MNT1_remote_command, "MNTPROC_MNT v1"},
  {
  "mnt1_dump", fn_MNT1_remote_command, "MNTPROC_DUMP v1"},
  {
  "mnt1_umount", fn_MNT1_remote_command, "MNTPROC_UMNT v1"},
  {
  "mnt1_umount_all", fn_MNT1_remote_command, "MNTPROC_UMNTALL v1"},
  {
  "mnt1_export", fn_MNT1_remote_command, "MNTPROC_EXPORT v1"},
  {
  "mnt3_null", fn_MNT3_remote_command, "MNTPROC_NULL v3"},
  {
  "mnt3_mount", fn_MNT3_remote_command, "MNTPROC_MNT v3"},
  {
  "mnt3_dump", fn_MNT3_remote_command, "MNTPROC_DUMP v3"},
  {
  "mnt3_umount", fn_MNT3_remote_command, "MNTPROC_UMNT v3"},
  {
  "mnt3_umount_all", fn_MNT3_remote_command, "MNTPROC_UMNTALL v3"},
  {
  "mnt3_export", fn_MNT3_remote_command, "MNTPROC_EXPORT v3"},
  {
  "nfs2_null", fn_NFS2_remote_command, "NFSPROC_NULL"},
  {
  "nfs2_getattr", fn_NFS2_remote_command, "NFSPROC_GETATTR"},
  {
  "nfs2_setattr", fn_NFS2_remote_command, "NFSPROC_SETATTR"},
  {
  "nfs2_root", fn_NFS2_remote_command, "NFSPROC_ROOT"},
  {
  "nfs2_lookup", fn_NFS2_remote_command, "NFSPROC_LOOKUP"},
  {
  "nfs2_readlink", fn_NFS2_remote_command, "NFSPROC_READLINK"},
  {
  "nfs2_read", fn_NFS2_remote_command, "NFSPROC_READ"},
  {
  "nfs2_writecache", fn_NFS2_remote_command, "NFSPROC_WRITECACHE"},
  {
  "nfs2_write", fn_NFS2_remote_command, "NFSPROC_WRITE"},
  {
  "nfs2_create", fn_NFS2_remote_command, "NFSPROC_CREATE"},
  {
  "nfs2_remove", fn_NFS2_remote_command, "NFSPROC_REMOVE"},
  {
  "nfs2_rename", fn_NFS2_remote_command, "NFSPROC_RENAME"},
  {
  "nfs2_link", fn_NFS2_remote_command, "NFSPROC_LINK"},
  {
  "nfs2_symlink", fn_NFS2_remote_command, "NFSPROC_SYMLINK"},
  {
  "nfs2_mkdir", fn_NFS2_remote_command, "NFSPROC_MKDIR"},
  {
  "nfs2_rmdir", fn_NFS2_remote_command, "NFSPROC_RMDIR"},
  {
  "nfs2_readdir", fn_NFS2_remote_command, "NFSPROC_READDIR"},
  {
  "nfs2_statfs", fn_NFS2_remote_command, "NFSPROC_STATFS"},
  {
  "nfs3_null", fn_NFS3_remote_command, "NFSPROC3_NULL"},
  {
  "nfs3_getattr", fn_NFS3_remote_command, "NFSPROC3_GETATTR"},
  {
  "nfs3_setattr", fn_NFS3_remote_command, "NFSPROC3_SETATTR"},
  {
  "nfs3_lookup", fn_NFS3_remote_command, "NFSPROC3_LOOKUP"},
  {
  "nfs3_access", fn_NFS3_remote_command, "NFSPROC3_ACCESS"},
  {
  "nfs3_readlink", fn_NFS3_remote_command, "NFSPROC3_READLINK"},
  {
  "nfs3_read", fn_NFS3_remote_command, "NFSPROC3_READ"},
  {
  "nfs3_write", fn_NFS3_remote_command, "NFSPROC3_WRITE"},
  {
  "nfs3_create", fn_NFS3_remote_command, "NFSPROC3_CREATE"},
  {
  "nfs3_mkdir", fn_NFS3_remote_command, "NFSPROC3_MKDIR"},
  {
  "nfs3_symlink", fn_NFS3_remote_command, "NFSPROC3_SYMLINK"},
  {
  "nfs3_mknod", fn_NFS3_remote_command, "NFSPROC3_MKNOD"},
  {
  "nfs3_remove", fn_NFS3_remote_command, "NFSPROC3_REMOVE"},
  {
  "nfs3_rmdir", fn_NFS3_remote_command, "NFSPROC3_RMDIR"},
  {
  "nfs3_rename", fn_NFS3_remote_command, "NFSPROC3_RENAME"},
  {
  "nfs3_link", fn_NFS3_remote_command, "NFSPROC3_LINK"},
  {
  "nfs3_readdir", fn_NFS3_remote_command, "NFSPROC3_READDIR"},
  {
  "nfs3_readdirplus", fn_NFS3_remote_command, "NFSPROC3_READDIRPLUS"},
  {
  "nfs3_fsstat", fn_NFS3_remote_command, "NFSPROC3_FSSTAT"},
  {
  "nfs3_fsinfo", fn_NFS3_remote_command, "NFSPROC3_FSINFO"},
  {
  "nfs3_pathconf", fn_NFS3_remote_command, "NFSPROC3_PATHCONF"},
  {
  "nfs3_commit", fn_NFS3_remote_command, "NFSPROC3_COMMIT"},
  {
  "cd", fn_nfs_remote_cd, "change current directory"},
  {
  "create", fn_nfs_remote_create, "create a regular file"},
  {
  "hardlink", fn_nfs_remote_hardlink, "create a hard link"},
  {
  "ln", fn_nfs_remote_ln, "create a symbolic link"},
  {
  "ls", fn_nfs_remote_ls, "list contents of directory"},
  {
  "mkdir", fn_nfs_remote_mkdir, "create a directory"},
  {
  "mount", fn_nfs_remote_mount, "mount an exported path"},
  {
  "umount", fn_nfs_remote_umount, "umount a mounted path"},
  {
  "pwd", fn_nfs_remote_pwd, "print current path"},
  {
  "rename", fn_nfs_remote_rename, "rename/move an object"},
  {
  "setattr", fn_nfs_remote_setattr, "change object attributes"},
  {
  "stat", fn_nfs_remote_stat, "show file attributes"},
  {
  "su", fn_nfs_remote_su, "change current user"},
  {
  "id", fn_nfs_remote_id, "show who I am"},
  {
  "unlink", fn_nfs_remote_unlink, "remove an object"},
  {
  NULL, NULL, NULL}             /* End of NFS command list. */
};

/**
 * Layer list.
 */

extern layer_def_t layer_list[];

#endif
