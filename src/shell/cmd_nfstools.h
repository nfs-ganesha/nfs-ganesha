/**
 *
 * \file    cmd_nfstools.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 17:03:35 $
 * \version $Revision: 1.22 $
 * \brief   nfs tools for ganeshell.
 *
 *
 * $Log: cmd_nfstools.h,v $
 * Revision 1.22  2006/01/18 17:03:35  leibovic
 * Removing some warnings.
 *
 * Revision 1.21  2006/01/18 08:02:04  deniel
 * Order in includes and libraries
 *
 * Revision 1.20  2006/01/18 07:29:11  leibovic
 * Fixing bugs about exportlists.
 *
 * Revision 1.19  2005/10/12 11:30:10  leibovic
 * NFSv2.
 *
 * Revision 1.18  2005/10/10 12:39:08  leibovic
 * Using mnt/nfs free functions.
 *
 * Revision 1.17  2005/10/07 08:30:43  leibovic
 * nfs2_rename + New FSAL init functions.
 *
 * Revision 1.16  2005/09/30 14:30:43  leibovic
 * Adding nfs2_readdir commqnd.
 *
 * Revision 1.15  2005/09/30 06:56:55  leibovic
 * Adding nfs2_setattr command.
 *
 * Revision 1.14  2005/09/30 06:46:00  leibovic
 * New create2 and mkdir2 args format.
 *
 * Revision 1.13  2005/09/07 14:08:22  leibovic
 * Adding NFS3_pathconf command.
 *
 * Revision 1.12  2005/08/30 13:22:26  leibovic
 * Addind nfs3_fsinfo et nfs3_pathconf functions.
 *
 * Revision 1.11  2005/08/10 14:55:05  leibovic
 * NFS support of setattr, rename, link, symlink.
 *
 * Revision 1.10  2005/08/10 10:57:17  leibovic
 * Adding removal functions.
 *
 * Revision 1.9  2005/08/09 14:52:57  leibovic
 * Addinf create and mkdir commands.
 *
 * Revision 1.8  2005/08/08 11:42:49  leibovic
 * Adding some stardard unix calls through NFS (ls, cd, pwd).
 *
 * Revision 1.7  2005/08/05 15:17:56  leibovic
 * Adding mount and pwd commands for browsing.
 *
 * Revision 1.6  2005/08/05 10:42:38  leibovic
 * Adding readdirplus.
 *
 * Revision 1.5  2005/08/05 07:59:07  leibovic
 * some nfs3 commands added.
 *
 * Revision 1.4  2005/08/04 06:57:41  leibovic
 * some NFSv2 commands are completed.
 *
 * Revision 1.3  2005/08/03 12:51:16  leibovic
 * MNT3 protocol OK.
 *
 * Revision 1.2  2005/08/03 11:51:10  leibovic
 * MNT1 protocol OK.
 *
 * Revision 1.1  2005/08/03 08:16:23  leibovic
 * Adding nfs layer structures.
 *
 *
 */

#ifndef _CMD_NFSTOOLS_H
#define _CMD_NFSTOOLS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rpc.h"
#include "nfs_proto_functions.h"
#include "nfs_remote_functions.h"

typedef enum cmdnfs_encodetype__
{
  CMDNFS_ENCODE = 1,
  CMDNFS_DECODE = 2,
  CMDNFS_FREE = 3
} cmdnfs_encodetype_t;

/* defining encoding/decoding function type */

typedef int (*cmdnfs_encoding_func_t) (cmdnfs_encodetype_t,     /* encoding or decoding */
                                       int, char **,    /* inputs for encoding  */
                                       int, FILE *,     /* indentation and output stream for decoding */
                                       caddr_t  /* pointer to nfs structure to encode or decode */
    );

/* defining generic command type */

typedef struct cmdnfs_funcdesc__
{

  /* nfs function name */
  char *func_name;

  /* related nfs function */
  nfs_protocol_function_t func_call;

  /* for freeing resources */
  nfs_protocol_free_t func_free;

  /* encoding function shell->nfs  */
  cmdnfs_encoding_func_t func_encode;

  /* decoding function nfs->shell */
  cmdnfs_encoding_func_t func_decode;

  /* syntaxe for the command */
  char *func_help;

} cmdnfs_funcdesc_t;

typedef struct cmdnfsremote_funcdesc__
{

  /* nfs function name */
  char *func_name;

  /* related nfs function */
  nfsremote_protocol_function_t func_call;

  /* for freeing resources */
  nfs_protocol_free_t func_free;

  /* encoding function shell->nfs  */
  cmdnfs_encoding_func_t func_encode;

  /* decoding function nfs->shell */
  cmdnfs_encoding_func_t func_decode;

  /* syntaxe for the command */
  char *func_help;

} cmdnfsremote_funcdesc_t;

/* encoding/decoding function definitions */

int cmdnfs_void(cmdnfs_encodetype_t encodeflag,
                int argc, char **argv,
                int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_dirpath(cmdnfs_encodetype_t encodeflag,
                   int argc, char **argv,
                   int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_fhstatus2(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_fhandle2(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_mountlist(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_exports(cmdnfs_encodetype_t encodeflag,
                   int argc, char **argv,
                   int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_mountres3(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_fhandle3(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_fattr2(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_ATTR2res(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_DIROP2res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_diropargs2(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_nfsstat2(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READLINK2res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_sattr2(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_CREATE2args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_SETATTR2args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIR2args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIR2res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_nfsstat3(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_fattr3(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_sattr3(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_GETATTR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_postopattr(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_postopfh3(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_diropargs3(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_LOOKUP3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIR3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIRPLUS3args(cmdnfs_encodetype_t encodeflag,
                            int argc, char **argv,
                            int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READDIRPLUS3res(cmdnfs_encodetype_t encodeflag,
                           int argc, char **argv,
                           int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_READLINK3res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_FSSTAT3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_ACCESS3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_ACCESS3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_CREATE3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_CREATE3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_MKDIR3args(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_MKDIR3res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_REMOVE3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_RMDIR3res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_SETATTR3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_SETATTR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_RENAME3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_RENAME3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_LINK3args(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_LINK3res(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_SYMLINK3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_SYMLINK3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_FSINFO3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_PATHCONF3res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_RENAME2args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct);

int cmdnfs_STATFS2res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct);

static cmdnfs_funcdesc_t __attribute((__unused__)) mnt1_funcdesc[] =
{
  {
  "mnt1_null", mnt_Null, mnt_Null_Free, cmdnfs_void, cmdnfs_void, "mnt1_null"},
  {
  "mnt1_mount", mnt_Mnt, mnt1_Mnt_Free, cmdnfs_dirpath, cmdnfs_fhstatus2,
        "mnt1_mount <dirpath>"},
  {
  "mnt1_dump", mnt_Dump, mnt_Dump_Free, cmdnfs_void, cmdnfs_mountlist, "mnt1_dump"},
  {
  "mnt1_umount", mnt_Umnt, mnt_Umnt_Free, cmdnfs_dirpath, cmdnfs_void,
        "mnt1_umount <dirpath>"},
  {
  "mnt1_umount_all", mnt_UmntAll, mnt_UmntAll_Free, cmdnfs_void, cmdnfs_void,
        "mnt1_umount_all"},
  {
  "mnt1_export", mnt_Export, mnt_Export_Free, cmdnfs_void, cmdnfs_exports, "mnt1_export"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
};

static cmdnfsremote_funcdesc_t __attribute((__unused__)) mnt1_remote_funcdesc[] =
{
  {
  "mnt1_null", mnt1_remote_Null, mnt1_remote_Null_Free, cmdnfs_void, cmdnfs_void,
        "mnt1_null"},
  {
  "mnt1_mount", mnt1_remote_Mnt, mnt1_remote_Mnt_Free, cmdnfs_dirpath, cmdnfs_fhstatus2,
        "mnt1_mount <dirpath>"},
  {
  "mnt1_dump", mnt1_remote_Dump, mnt1_remote_Dump_Free, cmdnfs_void, cmdnfs_mountlist,
        "mnt1_dump"},
  {
  "mnt1_umount", mnt1_remote_Umnt, mnt1_remote_Umnt_Free, cmdnfs_dirpath, cmdnfs_void,
        "mnt1_umount <dirpath>"},
  {
  "mnt1_umount_all", mnt1_remote_UmntAll, mnt1_remote_UmntAll_Free, cmdnfs_void,
        cmdnfs_void, "mnt1_umount_all"},
  {
  "mnt1_export", mnt1_remote_Export, mnt1_remote_Export_Free, cmdnfs_void,
        cmdnfs_exports, "mnt1_export"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
};

static cmdnfs_funcdesc_t __attribute((__unused__)) mnt3_funcdesc[] =
{
  {
  "mnt3_null", mnt_Null, mnt_Null_Free, cmdnfs_void, cmdnfs_void, "mnt3_null"},
  {
  "mnt3_mount", mnt_Mnt, mnt3_Mnt_Free, cmdnfs_dirpath, cmdnfs_mountres3,
        "mnt3_mount <dirpath>"},
  {
  "mnt3_dump", mnt_Dump, mnt_Dump_Free, cmdnfs_void, cmdnfs_mountlist, "mnt3_dump"},
  {
  "mnt3_umount", mnt_Umnt, mnt_Umnt_Free, cmdnfs_dirpath, cmdnfs_void,
        "mnt3_umount <dirpath>"},
  {
  "mnt3_umount_all", mnt_UmntAll, mnt_UmntAll_Free, cmdnfs_void, cmdnfs_void,
        "mnt3_umount_all"},
  {
  "mnt3_export", mnt_Export, mnt_Export_Free, cmdnfs_void, cmdnfs_exports, "mnt3_export"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
};

static cmdnfsremote_funcdesc_t __attribute((__unused__)) mnt3_remote_funcdesc[] =
{
  {
  "mnt3_null", mnt3_remote_Null, mnt3_remote_Null_Free, cmdnfs_void, cmdnfs_void,
        "mnt3_null"},
  {
  "mnt3_mount", mnt3_remote_Mnt, mnt3_remote_Mnt_Free, cmdnfs_dirpath, cmdnfs_mountres3,
        "mnt3_mount <dirpath>"},
  {
  "mnt3_dump", mnt3_remote_Dump, mnt3_remote_Dump_Free, cmdnfs_void, cmdnfs_mountlist,
        "mnt3_dump"},
  {
  "mnt3_umount", mnt3_remote_Umnt, mnt3_remote_Umnt_Free, cmdnfs_dirpath, cmdnfs_void,
        "mnt3_umount <dirpath>"},
  {
  "mnt3_umount_all", mnt3_remote_UmntAll, mnt3_remote_UmntAll_Free, cmdnfs_void,
        cmdnfs_void, "mnt3_umount_all"},
  {
  "mnt3_export", mnt3_remote_Export, mnt3_remote_Export_Free, cmdnfs_void,
        cmdnfs_exports, "mnt3_export"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
};

static cmdnfs_funcdesc_t __attribute((__unused__)) nfs2_funcdesc[] =
{
  {
  "nfs2_null", nfs_Null, nfs_Null_Free, cmdnfs_void, cmdnfs_void, "nfs2_null"},
  {
  "nfs2_getattr", nfs_Getattr, nfs_Getattr_Free, cmdnfs_fhandle2, cmdnfs_ATTR2res,
        "nfs2_getattr <@handle2>"},
  {
  "nfs2_lookup", nfs_Lookup, nfs2_Lookup_Free, cmdnfs_diropargs2, cmdnfs_DIROP2res,
        "nfs2_lookup <@dir_handle> <name>"},
  {
  "nfs2_readlink", nfs_Readlink, nfs2_Readlink_Free, cmdnfs_fhandle2,
        cmdnfs_READLINK2res, "nfs2_readlink <@symlink_handle2>"},
  {
  "nfs2_create", nfs_Create, nfs_Create_Free, cmdnfs_CREATE2args, cmdnfs_DIROP2res,
        "nfs2_create <@dir_handle2> <name>  [<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_mkdir", nfs_Mkdir, nfs_Mkdir_Free, cmdnfs_CREATE2args, cmdnfs_DIROP2res,
        "nfs2_mkdir <@dir_handle2> <name>[<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_remove", nfs_Remove, nfs_Remove_Free, cmdnfs_diropargs2, cmdnfs_nfsstat2,
        "nfs2_remove <@dir_handle> <name>"},
  {
  "nfs2_rmdir", nfs_Rmdir, nfs_Rmdir_Free, cmdnfs_diropargs2, cmdnfs_nfsstat2,
        "nfs2_rmdir <@dir_handle> <name>"},
  {
  "nfs2_root", nfs2_Root, nfs2_Root_Free, cmdnfs_void, cmdnfs_void,
        "nfs2_root (not supported)"},
  {
  "nfs2_writecache", nfs2_Writecache, nfs2_Writecache_Free, cmdnfs_void, cmdnfs_void,
        "nfs2_writecache (not supported)"},
  {
  "nfs2_setattr", nfs_Setattr, nfs_Setattr_Free, cmdnfs_SETATTR2args, cmdnfs_ATTR2res,
        "nfs2_setattr <@handle2> <attr>=<value>[,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_readdir", nfs_Readdir, nfs2_Readdir_Free, cmdnfs_READDIR2args,
        cmdnfs_READDIR2res, "nfs2_readdir <@dir_handle> <cookie> <count>"},
  {
  "nfs2_rename", nfs_Rename, nfs_Rename_Free, cmdnfs_RENAME2args, cmdnfs_nfsstat2,
        "nfs2_rename <@src_handle> <src_name> <@tgt_handle> <tgt_name>"},
  {
  "nfs2_statfs", nfs_Fsstat, nfs_Fsstat_Free, cmdnfs_fhandle2, cmdnfs_STATFS2res,
        "nfs2_statfs <@handle2>"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
/*  nfs_Read,        xdr_READ2args,    xdr_READ2res,    "nfs_Read", 
  nfs_Write,       xdr_WRITE2args,   xdr_ATTR2res,    "nfs_Write", 
  nfs_Link,        xdr_LINK2args,    xdr_nfsstat2,    "nfs_Link", 
  nfs_Symlink,     xdr_SYMLINK2args, xdr_nfsstat2,    "nfs_Symlink", 
  nfs_Fsstat,      xdr_fhandle2,     xdr_STATFS2res,  "nfs_Fsstat"*/
};

static cmdnfsremote_funcdesc_t __attribute((__unused__)) nfs2_remote_funcdesc[] =
{
  {
  "nfs2_null", nfs2_remote_Null, nfs2_remote_Null_Free, cmdnfs_void, cmdnfs_void,
        "nfs2_null"},
  {
  "nfs2_getattr", nfs2_remote_Getattr, nfs2_remote_Getattr_Free, cmdnfs_fhandle2,
        cmdnfs_ATTR2res, "nfs2_getattr <@handle2>"},
  {
  "nfs2_lookup", nfs2_remote_Lookup, nfs2_remote_Lookup_Free, cmdnfs_diropargs2,
        cmdnfs_DIROP2res, "nfs2_lookup <@dir_handle> <name>"},
  {
  "nfs2_readlink", nfs2_remote_Readlink, nfs2_remote_Readlink_Free, cmdnfs_fhandle2,
        cmdnfs_READLINK2res, "nfs2_readlink <@symlink_handle2>"},
  {
  "nfs2_create", nfs2_remote_Create, nfs2_remote_Create_Free, cmdnfs_CREATE2args,
        cmdnfs_DIROP2res,
        "nfs2_create <@dir_handle2> <name>  [<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_mkdir", nfs2_remote_Mkdir, nfs2_remote_Mkdir_Free, cmdnfs_CREATE2args,
        cmdnfs_DIROP2res,
        "nfs2_mkdir <@dir_handle2> <name>[<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_remove", nfs2_remote_Remove, nfs2_remote_Remove_Free, cmdnfs_diropargs2,
        cmdnfs_nfsstat2, "nfs2_remove <@dir_handle> <name>"},
  {
  "nfs2_rmdir", nfs2_remote_Rmdir, nfs2_remote_Rmdir_Free, cmdnfs_diropargs2,
        cmdnfs_nfsstat2, "nfs2_rmdir <@dir_handle> <name>"},
  {
  "nfs2_root", nfs2_remote_Root, nfs2_remote_Root_Free, cmdnfs_void, cmdnfs_void,
        "nfs2_root (not supported)"},
  {
  "nfs2_writecache", nfs2_remote_Writecache, nfs2_remote_Writecache_Free, cmdnfs_void,
        cmdnfs_void, "nfs2_writecache (not supported)"},
  {
  "nfs2_setattr", nfs2_remote_Setattr, nfs2_remote_Setattr_Free, cmdnfs_SETATTR2args,
        cmdnfs_ATTR2res,
        "nfs2_setattr <@handle2> <attr>=<value>[,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.uuuuuu), mtime."},
  {
  "nfs2_readdir", nfs2_remote_Readdir, nfs2_remote_Readdir_Free, cmdnfs_READDIR2args,
        cmdnfs_READDIR2res, "nfs2_readdir <@dir_handle> <cookie> <count>"},
  {
  "nfs2_rename", nfs2_remote_Rename, nfs2_remote_Rename_Free, cmdnfs_RENAME2args,
        cmdnfs_nfsstat2, "nfs2_rename <@src_handle> <src_name> <@tgt_handle> <tgt_name>"},
  {
  "nfs2_statfs", nfs2_remote_Fsstat, nfs2_remote_Fsstat_Free, cmdnfs_fhandle2,
        cmdnfs_STATFS2res, "nfs2_statfs <@handle2>"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
/*  nfs_Read,        xdr_READ2args,    xdr_READ2res,    "nfs_Read", 
  nfs_Write,       xdr_WRITE2args,   xdr_ATTR2res,    "nfs_Write", 
  nfs_Link,        xdr_LINK2args,    xdr_nfsstat2,    "nfs_Link", 
  nfs_Symlink,     xdr_SYMLINK2args, xdr_nfsstat2,    "nfs_Symlink", 
  nfs_Fsstat,      xdr_fhandle2,     xdr_STATFS2res,  "nfs_Fsstat"*/
};

static cmdnfs_funcdesc_t __attribute__ ((__unused__)) nfs3_funcdesc[] =
{
  {
  "nfs3_null", nfs_Null, nfs_Null_Free, cmdnfs_void, cmdnfs_void, "nfs3_null"},
  {
  "nfs3_getattr", nfs_Getattr, nfs_Getattr_Free, cmdnfs_fhandle3, cmdnfs_GETATTR3res,
        "nfs3_getattr <@handle3>"},
  {
  "nfs3_lookup", nfs_Lookup, nfs3_Lookup_Free, cmdnfs_diropargs3, cmdnfs_LOOKUP3res,
        "nfs3_lookup <@dir_handle3> <name>"},
  {
  "nfs3_readdir", nfs_Readdir, nfs3_Readdir_Free, cmdnfs_READDIR3args,
        cmdnfs_READDIR3res,
        "nfs3_readdir <@dir_handle3> <cookie(uint64)> <cookieverf(8 bytes hexa)> <count>"},
  {
  "nfs3_readdirplus", nfs3_Readdirplus, nfs3_Readdirplus_Free, cmdnfs_READDIRPLUS3args,
        cmdnfs_READDIRPLUS3res,
        "nfs3_readdirplus <@dir_handle3> <cookie(uint64)> <cookieverf(8 bytes hexa)> <dircount> <maxcount>"},
  {
  "nfs3_readlink", nfs_Readlink, nfs3_Readlink_Free, cmdnfs_fhandle3,
        cmdnfs_READLINK3res, "nfs3_readlink <@symlnk_handle3>"},
  {
  "nfs3_access", nfs3_Access, nfs3_Access_Free, cmdnfs_ACCESS3args, cmdnfs_ACCESS3res,
        "nfs3_access <@handle3> <[R][M][L][E][D][X]>\n"
        "\twith flags: R(ead) L(ookup) M(odify) E(xtend) D(elete) (e)X(ecute)"},
  {
  "nfs3_create", nfs_Create, nfs_Create_Free, cmdnfs_CREATE3args, cmdnfs_CREATE3res,
        "nfs3_create <@dir_handle3> <name> <UNCHECKED|GUARDED> [<attr>=<value>},<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime.\n"
        "nfs3_create <@dir_handle3> <name> EXCLUSIVE <createverf(8 bytes hexa)>"},
  {
  "nfs3_mkdir", nfs_Mkdir, nfs_Mkdir_Free, cmdnfs_MKDIR3args, cmdnfs_MKDIR3res,
        "nfs3_mkdir <@dir_handle3> <name> [<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime."},
  {
  "nfs3_remove", nfs_Remove, nfs_Remove_Free, cmdnfs_diropargs3, cmdnfs_REMOVE3res,
        "nfs3_remove <@dir_handle3> <name>"},
  {
  "nfs3_rmdir", nfs_Rmdir, nfs_Rmdir_Free, cmdnfs_diropargs3, cmdnfs_RMDIR3res,
        "nfs3_rmdir <@dir_handle3> <name>"},
  {
  "nfs3_fsstat", nfs_Fsstat, nfs_Fsstat_Free, cmdnfs_fhandle3, cmdnfs_FSSTAT3res,
        "nfs3_fsstat <@handle3>"},
  {
  "nfs3_setattr", nfs_Setattr, nfs_Setattr_Free, cmdnfs_SETATTR3args,
        cmdnfs_SETATTR3res,
        "nfs3_setattr <@handle3> <attr>=<value>,<attr>=<value>,... [check_obj_ctime(format: YYYYMMDDHHMMSS.nnnnnnnnn)]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime."},
  {
  "nfs3_rename", nfs_Rename, nfs_Rename_Free, cmdnfs_RENAME3args, cmdnfs_RENAME3res,
        "nfs3_rename  <@from_dir_handle3> <from_name> <@to_dir_handle3> <to_name>"},
  {
  "nfs3_link", nfs_Link, nfs_Link_Free, cmdnfs_LINK3args, cmdnfs_LINK3res,
        "nfs3_link <@handle3> <@link_dir_handle3> <link_name>"},
  {
  "nfs3_symlink", nfs_Symlink, nfs_Symlink_Free, cmdnfs_SYMLINK3args,
        cmdnfs_SYMLINK3res,
        "nfs3_symlink <@dir_handle3> <name> [<attr>=<value>},<attr>=<value>,...] <symlink_data>"},
  {
  "nfs3_fsinfo", nfs3_Fsinfo, nfs3_Fsinfo_Free, cmdnfs_fhandle3, cmdnfs_FSINFO3res,
        "nfs3_fsinfo <@root_hdl3>"},
  {
  "nfs3_pathconf", nfs3_Pathconf, nfs3_Pathconf_Free, cmdnfs_fhandle3,
        cmdnfs_PATHCONF3res, "nfs3_pathconf <@handle3>"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
/*
  nfs_Read,         xdr_READ3args,        xdr_READ3res,        "nfs_Read", 
  nfs_Write,        xdr_WRITE3args,       xdr_WRITE3res,       "nfs_Write", 
  nfs3_Mknod,       xdr_MKNOD3args,       xdr_MKNOD3res,       "nfs3_Mknod", 
  nfs3_Commit,      xdr_COMMIT3args,      xdr_COMMIT3res,      "nfs3_Commit"
*/
};

static cmdnfsremote_funcdesc_t __attribute((__unused__)) nfs3_remote_funcdesc[] =
{
  {
  "nfs3_null", nfs3_remote_Null, nfs3_remote_Null_Free, cmdnfs_void, cmdnfs_void,
        "nfs3_null"},
  {
  "nfs3_getattr", nfs3_remote_Getattr, nfs3_remote_Getattr_Free, cmdnfs_fhandle3,
        cmdnfs_GETATTR3res, "nfs3_getattr <@handle3>"},
  {
  "nfs3_lookup", nfs3_remote_Lookup, nfs3_remote_Lookup_Free, cmdnfs_diropargs3,
        cmdnfs_LOOKUP3res, "nfs3_lookup <@dir_handle3> <name>"},
  {
  "nfs3_readdir", nfs3_remote_Readdir, nfs3_remote_Readdir_Free, cmdnfs_READDIR3args,
        cmdnfs_READDIR3res,
        "nfs3_readdir <@dir_handle3> <cookie(uint64)> <cookieverf(8 bytes hexa)> <count>"},
  {
  "nfs3_readdirplus", nfs3_remote_Readdirplus, nfs3_remote_Readdirplus_Free,
        cmdnfs_READDIRPLUS3args, cmdnfs_READDIRPLUS3res,
        "nfs3_readdirplus <@dir_handle3> <cookie(uint64)> <cookieverf(8 bytes hexa)> <dircount> <maxcount>"},
  {
  "nfs3_readlink", nfs3_remote_Readlink, nfs3_remote_Readlink_Free, cmdnfs_fhandle3,
        cmdnfs_READLINK3res, "nfs3_readlink <@symlnk_handle3>"},
  {
  "nfs3_access", nfs3_remote_Access, nfs3_remote_Access_Free, cmdnfs_ACCESS3args,
        cmdnfs_ACCESS3res,
        "nfs3_access <@handle3> <[R][M][L][E][D][X]>\n"
        "\twith flags: R(ead) L(ookup) M(odify) E(xtend) D(elete) (e)X(ecute)"},
  {
  "nfs3_create", nfs3_remote_Create, nfs3_remote_Create_Free, cmdnfs_CREATE3args,
        cmdnfs_CREATE3res,
        "nfs3_create <@dir_handle3> <name> <UNCHECKED|GUARDED> [<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime.\n"
        "nfs3_create <@dir_handle3> <name> EXCLUSIVE <createverf(8 bytes hexa)>"},
  {
  "nfs3_mkdir", nfs3_remote_Mkdir, nfs3_remote_Mkdir_Free, cmdnfs_MKDIR3args,
        cmdnfs_MKDIR3res,
        "nfs3_mkdir <@dir_handle3> <name> [<attr>=<value>,<attr>=<value>,...]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime."},
  {
  "nfs3_remove", nfs3_remote_Remove, nfs3_remote_Remove_Free, cmdnfs_diropargs3,
        cmdnfs_REMOVE3res, "nfs3_remove <@dir_handle3> <name>"},
  {
  "nfs3_rmdir", nfs3_remote_Rmdir, nfs3_remote_Rmdir_Free, cmdnfs_diropargs3,
        cmdnfs_RMDIR3res, "nfs3_rmdir <@dir_handle3> <name>"},
  {
  "nfs3_fsstat", nfs3_remote_Fsstat, nfs3_remote_Fsstat_Free, cmdnfs_fhandle3,
        cmdnfs_FSSTAT3res, "nfs3_fsstat <@handle3>"},
  {
  "nfs3_setattr", nfs3_remote_Setattr, nfs3_remote_Setattr_Free, cmdnfs_SETATTR3args,
        cmdnfs_SETATTR3res,
        "nfs3_setattr <@handle3> <attr>=<value>,<attr>=<value>,... [check_obj_ctime(format: YYYYMMDDHHMMSS.nnnnnnnnn)]\n"
        "\twhere <attr> can be: mode(octal value), uid, gid, size, atime(format: YYYYMMDDHHMMSS.nnnnnnnnn), mtime."},
  {
  "nfs3_rename", nfs3_remote_Rename, nfs3_remote_Rename_Free, cmdnfs_RENAME3args,
        cmdnfs_RENAME3res,
        "nfs3_rename  <@from_dir_handle3> <from_name> <@to_dir_handle3> <to_name>"},
  {
  "nfs3_link", nfs3_remote_Link, nfs3_remote_Link_Free, cmdnfs_LINK3args,
        cmdnfs_LINK3res, "nfs3_link <@handle3> <@link_dir_handle3> <link_name>"},
  {
  "nfs3_symlink", nfs3_remote_Symlink, nfs3_remote_Symlink_Free, cmdnfs_SYMLINK3args,
        cmdnfs_SYMLINK3res,
        "nfs3_symlink <@dir_handle3> <name> [<attr>=<value>},<attr>=<value>,...] <symlink_data>"},
  {
  "nfs3_fsinfo", nfs3_remote_Fsinfo, nfs3_remote_Fsinfo_Free, cmdnfs_fhandle3,
        cmdnfs_FSINFO3res, "nfs3_fsinfo <@root_hdl3>"},
  {
  "nfs3_pathconf", nfs3_remote_Pathconf, nfs3_remote_Pathconf_Free, cmdnfs_fhandle3,
        cmdnfs_PATHCONF3res, "nfs3_pathconf <@handle3>"},
  {
  NULL, NULL, NULL, NULL, NULL, NULL}
/*
  nfs_Read,         xdr_READ3args,        xdr_READ3res,        "nfs_Read", 
  nfs_Write,        xdr_WRITE3args,       xdr_WRITE3res,       "nfs_Write", 
  nfs3_Mknod,       xdr_MKNOD3args,       xdr_MKNOD3res,       "nfs3_Mknod", 
  nfs3_Commit,      xdr_COMMIT3args,      xdr_COMMIT3res,      "nfs3_Commit"
*/
};

/**
 * print_nfsitem_line:
 * Prints a nfs element on one line, like the Unix ls command.
 * 
 * \param out (in FILE*) The file where the item is to be printed.
 * \param attrib (fattr3 *) the NFS attributes for the item.
 * \param name (in char *) The name of the item to be printed.
 * \param target (in char *) It the item is a symbolic link,
 *        this contains the link target.
 * \return Nothing. 
 */
void print_nfsitem_line(FILE * out, fattr3 * attrib, char *name, char *target);

/**
 * print_nfs_attributes:
 * print an fattr3 to a given output file.
 * 
 * \param attrs (in fattr3) The attributes to be printed.
 * \param output (in FILE *) The file where the attributes are to be printed.
 * \return Nothing.
 */
void print_nfs_attributes(fattr3 * attrs, FILE * output);

#endif
