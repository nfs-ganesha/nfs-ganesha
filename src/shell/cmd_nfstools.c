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
 * \file    cmd_nfstools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:44 $
 * \version $Revision: 1.24 $
 * \brief   nfs conversion tools.
 *
 *
 * $Log: cmd_nfstools.c,v $
 * Revision 1.24  2006/02/17 13:37:44  leibovic
 * Ghost FS is back !
 *
 * Revision 1.23  2006/01/31 12:25:42  leibovic
 * Fixing a minor display bug.
 *
 * Revision 1.22  2006/01/18 08:02:04  deniel
 * Order in includes and libraries
 *
 * Revision 1.21  2006/01/18 07:29:11  leibovic
 * Fixing bugs about exportlists.
 *
 * Revision 1.19  2005/10/12 11:30:10  leibovic
 * NFSv2.
 *
 * Revision 1.18  2005/10/07 08:30:43  leibovic
 * nfs2_rename + New FSAL init functions.
 *
 * Revision 1.17  2005/09/30 14:30:43  leibovic
 * Adding nfs2_readdir commqnd.
 *
 * Revision 1.16  2005/09/30 06:56:55  leibovic
 * Adding nfs2_setattr command.
 *
 * Revision 1.15  2005/09/30 06:46:00  leibovic
 * New create2 and mkdir2 args format.
 *
 * Revision 1.14  2005/09/28 09:08:00  leibovic
 * thread-safe version of localtime.
 *
 * Revision 1.13  2005/08/30 13:22:26  leibovic
 * Addind nfs3_fsinfo et nfs3_pathconf functions.
 *
 * Revision 1.12  2005/08/10 14:55:05  leibovic
 * NFS support of setattr, rename, link, symlink.
 *
 * Revision 1.11  2005/08/10 10:57:17  leibovic
 * Adding removal functions.
 *
 * Revision 1.10  2005/08/09 14:52:57  leibovic
 * Addinf create and mkdir commands.
 *
 * Revision 1.9  2005/08/08 11:42:49  leibovic
 * Adding some stardard unix calls through NFS (ls, cd, pwd).
 *
 * Revision 1.8  2005/08/05 15:17:56  leibovic
 * Adding mount and pwd commands for browsing.
 *
 * Revision 1.7  2005/08/05 10:42:38  leibovic
 * Adding readdirplus.
 *
 * Revision 1.6  2005/08/05 08:59:32  leibovic
 * Adding explicit strings for type and NFS status.
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
 * Revision 1.2  2005/08/03 11:51:09  leibovic
 * MNT1 protocol OK.
 *
 * Revision 1.1  2005/08/03 08:16:23  leibovic
 * Adding nfs layer structures.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
#endif

#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include "cmd_nfstools.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "cmd_tools.h"

/* 2 char per byte + '\0' */
#define SIZE_STR_NFSHANDLE2 (2 * NFS2_FHSIZE + 1)
#define SIZE_STR_NFSHANDLE3 (2 * NFS3_FHSIZE + 1)

/* unsolved symbols */
int get_rpc_xid()
{
  return 0;
}

void *rpc_tcp_socket_manager_thread(void *Arg)
{
  return NULL;
}

/* encoding/decoding function definitions */

int cmdnfs_void(cmdnfs_encodetype_t encodeflag,
                int argc, char **argv,
                int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  if((encodeflag == CMDNFS_ENCODE) && (argc != 0))
    return FALSE;

  return TRUE;
}

int cmdnfs_dirpath(cmdnfs_encodetype_t encodeflag,
                   int argc, char **argv,
                   int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  size_t len;
  dirpath *p_dirpath = (dirpath *) p_nfs_struct;

  /* sanity check */
  if(p_dirpath == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 1)
        return FALSE;

      len = strlen(argv[0]);
      *p_dirpath = Mem_Alloc(len + 1);

      if(*p_dirpath == NULL)
        {
          fprintf(stderr, "Not enough memory.\n");
          return FALSE;
        }

      strcpy(*p_dirpath, argv[0]);

      return TRUE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sdirpath = %s\n", indent, " ", *p_dirpath);
      return TRUE;

      break;

    case CMDNFS_FREE:

      Mem_Free(*p_dirpath);
      break;

    default:
      return FALSE;
    }
  return FALSE;
}

int cmdnfs_fhandle2(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  fhandle2 *p_fhandle = (fhandle2 *) p_nfs_struct;

  char *str_handle;
  char str_printhandle[SIZE_STR_NFSHANDLE2];
  int rc;

  /* sanity check */
  if(p_fhandle == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 1)
        return FALSE;

      str_handle = argv[0];

      /* check that it begins with an @ */
      if(str_handle[0] != '@')
        return FALSE;

      /* escaping the first char @ */
      str_handle++;

      rc = sscanmem((caddr_t) p_fhandle, NFS2_FHSIZE, str_handle);

      /* we must have read at least the handle size */
      if(rc < 2 * (int)sizeof(file_handle_v2_t))
        return FALSE;

      return TRUE;

      break;
    case CMDNFS_DECODE:

      if(snprintmem
         (str_printhandle, SIZE_STR_NFSHANDLE2, (caddr_t) p_fhandle, NFS2_FHSIZE) < 0)
        {
          return FALSE;
        }

      fprintf(out_stream, "%*sfhandle2 = @%s\n", indent, " ", str_printhandle);

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* nothing to do */
      return TRUE;

      break;

    default:
      return FALSE;
    }

}

int cmdnfs_fhstatus2(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  fhstatus2 *p_fhstatus = (fhstatus2 *) p_nfs_struct;

  /* sanity check */
  if(p_fhstatus == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* it is never an input */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sfhstatus2 =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      /* Convert status to error code */
      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_fhstatus->status) == FALSE)
        {
          return FALSE;
        }

      if(p_fhstatus->status == NFS_OK)
        {
          if(cmdnfs_fhandle2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                             (caddr_t) & p_fhstatus->fhstatus2_u.directory) == FALSE)
            {
              return FALSE;
            }
        }
      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* it is never an input (never encoded, never allocated) */
      return FALSE;

      break;

    default:
      return FALSE;
    }
}

int cmdnfs_STATFS2res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  STATFS2res *p_statres = (STATFS2res *) p_nfs_struct;

  /* sanity check */
  if(p_statres == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* it is never an input */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sSTATFS2res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      /* Convert status to error code */
      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_statres->status) == FALSE)
        {
          return FALSE;
        }

      if(p_statres->status == NFS_OK)
        {
          /* print statinfo2 */
          fprintf(out_stream, "%*sinfo =\n", indent + 2, " ");
          fprintf(out_stream, "%*s{\n", indent + 2, " ");
          fprintf(out_stream, "%*stsize  = %u\n", indent + 4, " ",
                  p_statres->STATFS2res_u.info.tsize);
          fprintf(out_stream, "%*sbsize  = %u\n", indent + 4, " ",
                  p_statres->STATFS2res_u.info.bsize);
          fprintf(out_stream, "%*sblocks = %u\n", indent + 4, " ",
                  p_statres->STATFS2res_u.info.blocks);
          fprintf(out_stream, "%*sbfree  = %u\n", indent + 4, " ",
                  p_statres->STATFS2res_u.info.bfree);
          fprintf(out_stream, "%*sbavail = %u\n", indent + 4, " ",
                  p_statres->STATFS2res_u.info.bavail);
          fprintf(out_stream, "%*s}\n", indent + 2, " ");

        }
      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* it is never an input (never encoded, never allocated) */
      return FALSE;

      break;

    default:
      return FALSE;
    }

}

int cmdnfs_mountlist(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  mountlist *p_mountlist = (mountlist *) p_nfs_struct;

  /* sanity check */
  if(p_mountlist == NULL)
    return FALSE;

  /* empty list */
  if(*p_mountlist == NULL)
    return TRUE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* it is never an input */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*s{\n", indent, " ");
      fprintf(out_stream, "%*shostname = %s\n", indent + 2, " ",
              (*p_mountlist)->ml_hostname);
      fprintf(out_stream, "%*spathname = %s\n", indent + 2, " ",
              (*p_mountlist)->ml_directory);
      fprintf(out_stream, "%*s}\n", indent, " ");

      if(cmdnfs_mountlist(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                          (caddr_t) & ((*p_mountlist)->ml_next)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* it is never an input (never encoded, never allocated) */
      return FALSE;

      break;

    default:
      return FALSE;
    }

}

int cmdnfs_exports(cmdnfs_encodetype_t encodeflag,
                   int argc, char **argv,
                   int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  exports *p_exports = (exports *) p_nfs_struct;

  groups group_list;

  /* sanity check */
  if(p_exports == NULL)
    return FALSE;

  /* empty list */
  if(*p_exports == NULL)
    return TRUE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* it is never an input */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*s{\n", indent, " ");
      fprintf(out_stream, "%*sex_dir = %s\n", indent + 2, " ", (*p_exports)->ex_dir);
      fprintf(out_stream, "%*sex_groups =\n", indent + 2, " ");

      group_list = (*p_exports)->ex_groups;
      while(group_list)
        {
          fprintf(out_stream, "%*sgr_name = %s\n", indent + 4, " ", group_list->gr_name);
          group_list = group_list->gr_next;
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      if(cmdnfs_exports(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                        (caddr_t) & ((*p_exports)->ex_next)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* it is never an input (never encoded, never allocated) */
      return FALSE;

      break;

    default:
      return FALSE;
    }

}

int cmdnfs_fhandle3(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  fhandle3 *p_fhandle = (fhandle3 *) p_nfs_struct;

  char *str_handle;
  char str_printhandle[SIZE_STR_NFSHANDLE3];
  int rc;

  /* sanity check */
  if(p_fhandle == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 1)
        return FALSE;

      str_handle = argv[0];

      /* check that it begins with an @ */
      if(str_handle[0] != '@')
        return FALSE;

      /* escaping the first char @ */
      str_handle++;

      /* Allocation of the nfs3 handle */
      p_fhandle->fhandle3_val = Mem_Alloc(NFS3_FHSIZE);

      if(p_fhandle->fhandle3_val == NULL)
        {
          fprintf(stderr, "Not enough memory.\n");
          return FALSE;
        }

      p_fhandle->fhandle3_len = (unsigned int)sizeof(file_handle_v3_t);

      rc = sscanmem((caddr_t) (p_fhandle->fhandle3_val), (int)sizeof(file_handle_v3_t),
                    str_handle);

      if(rc < 2 * (int)sizeof(file_handle_v3_t))
        return FALSE;

      return TRUE;

      break;
    case CMDNFS_DECODE:

      if(snprintmem
         (str_printhandle, SIZE_STR_NFSHANDLE3, (caddr_t) (p_fhandle->fhandle3_val),
          p_fhandle->fhandle3_len) < 0)
        {
          return FALSE;
        }

      fprintf(out_stream, "%*sfhandle3 = @%s\n", indent, " ", str_printhandle);

      return TRUE;

      break;

    case CMDNFS_FREE:

      Mem_Free(p_fhandle->fhandle3_val);
      return TRUE;

      break;

    default:
      return FALSE;
    }

}

#define MNT_HANDLE( p_mount_res3 ) ( (p_mount_res3)->mountres3_u.mountinfo.fhandle )
#define AUTH_FLAVORS( p_mount_res3 ) ( (p_mount_res3)->mountres3_u.mountinfo.auth_flavors )

int cmdnfs_mountres3(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  unsigned int i;
  mountres3 *p_mountres = (mountres3 *) p_nfs_struct;

  /* sanity check */
  if(p_mountres == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*smountres3 =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      /** @todo Convert status to error code */
      fprintf(out_stream, "%*sfhs_status = %u\n", indent + 2, " ",
              p_mountres->fhs_status);

      if(p_mountres->fhs_status == MNT3_OK)
        {
          fprintf(out_stream, "%*smountinfo =\n", indent + 2, " ");
          fprintf(out_stream, "%*s{\n", indent + 2, " ");

          if(cmdnfs_fhandle3(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                             (caddr_t) & MNT_HANDLE(p_mountres)) == FALSE)
            {
              return FALSE;
            }

          for(i = 0; i < AUTH_FLAVORS(p_mountres).auth_flavors_len; i++)
            {
              fprintf(out_stream, "%*sauth_flavor = %d\n", indent + 4, " ",
                      AUTH_FLAVORS(p_mountres).auth_flavors_val[i]);
            }

          fprintf(out_stream, "%*s}\n", indent + 2, " ");
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never an input */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

/* undefine dirty macros... */
#undef MNT_HANDLE
#undef AUTH_FLAVORS

int cmdnfs_nfsstat2(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  nfsstat2 *p_stat2 = (nfsstat2 *) p_nfs_struct;

  /* sanity check */
  if(p_stat2 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sstatus = %d (%s)\n", indent, " ", (int)(*p_stat2),
              nfsstat2_to_str(*p_stat2));
      return TRUE;

      break;

    case CMDNFS_ENCODE:
    case CMDNFS_FREE:
      /* never encoded */
    default:
      return FALSE;
    }

}

int cmdnfs_fattr2(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  fattr2 *p_fattr = (fattr2 *) p_nfs_struct;
  char tmp_buff[256] = "";

  struct tm paramtm;

  /* sanity check */
  if(p_fattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /** @todo */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sfattr2 =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      fprintf(out_stream, "%*stype = %d (%s)\n", indent + 2, " ", (int)p_fattr->type,
              nfstype2_to_str(p_fattr->type));
      fprintf(out_stream, "%*smode = 0%o\n", indent + 2, " ", p_fattr->mode);
      fprintf(out_stream, "%*snlink = %u\n", indent + 2, " ", p_fattr->nlink);
      fprintf(out_stream, "%*suid = %u\n", indent + 2, " ", p_fattr->uid);
      fprintf(out_stream, "%*sgid = %u\n", indent + 2, " ", p_fattr->gid);
      fprintf(out_stream, "%*ssize = %u\n", indent + 2, " ", p_fattr->size);
      fprintf(out_stream, "%*sblocksize = %u\n", indent + 2, " ", p_fattr->blocksize);
      fprintf(out_stream, "%*srdev = %hu.%hu\n", indent + 2, " ",
              (unsigned short)(p_fattr->rdev >> 16), (unsigned short)p_fattr->rdev);
      fprintf(out_stream, "%*sblocks = %u\n", indent + 2, " ", p_fattr->blocks);
      fprintf(out_stream, "%*sfsid = %#x\n", indent + 2, " ", p_fattr->fsid);
      fprintf(out_stream, "%*sfileid = %#x\n", indent + 2, " ", p_fattr->fileid);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->atime.seconds, &paramtm));
      fprintf(out_stream, "%*satime = %u.%.6u (%s)\n", indent + 2, " ",
              p_fattr->atime.seconds, p_fattr->atime.useconds, tmp_buff);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->mtime.seconds, &paramtm));
      fprintf(out_stream, "%*smtime = %u.%.6u (%s)\n", indent + 2, " ",
              p_fattr->mtime.seconds, p_fattr->mtime.useconds, tmp_buff);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->ctime.seconds, &paramtm));
      fprintf(out_stream, "%*sctime = %u.%.6u (%s)\n", indent + 2, " ",
              p_fattr->ctime.seconds, p_fattr->ctime.useconds, tmp_buff);

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_FREE:

      /** @todo */
      return FALSE;

      break;

    default:
      return FALSE;
    }

}

#define ATTR2_ATTRIBUTES( _p_attr2res ) ( (_p_attr2res)->ATTR2res_u.attributes )

int cmdnfs_ATTR2res(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  ATTR2res *p_attr2res = (ATTR2res *) p_nfs_struct;

  /* sanity check */
  if(p_attr2res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sATTR2res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_attr2res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_attr2res->status == NFS_OK)
        {
          if(cmdnfs_fattr2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                           (caddr_t) & ATTR2_ATTRIBUTES(p_attr2res)) == FALSE)
            {
              return FALSE;
            }

        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef ATTR2_ATTRIBUTES

#define DIROP2_HANDLE( _p_dirop ) ( (_p_dirop)->DIROP2res_u.diropok.file )
#define DIROP2_ATTRIBUTES( _p_dirop ) ( (_p_dirop)->DIROP2res_u.diropok.attributes )

int cmdnfs_DIROP2res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  DIROP2res *p_dirop2res = (DIROP2res *) p_nfs_struct;

  /* sanity check */
  if(p_dirop2res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sDIROP2res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_dirop2res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_dirop2res->status == NFS_OK)
        {
          if(cmdnfs_fhandle2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                             (caddr_t) & DIROP2_HANDLE(p_dirop2res)) == FALSE)
            {
              return FALSE;
            }

          if(cmdnfs_fattr2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                           (caddr_t) & DIROP2_ATTRIBUTES(p_dirop2res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef DIROP2_HANDLE
#undef DIROP2_ATTRIBUTES

int cmdnfs_diropargs2(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  diropargs2 *p_diropargs = (diropargs2 *) p_nfs_struct;

  /* sanity check */
  if(p_diropargs == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 2)
        return FALSE;

      if(cmdnfs_fhandle2(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_diropargs->dir)) == FALSE)
        {
          return FALSE;
        }

      if(cmdnfs_dirpath(CMDNFS_ENCODE, 1, argv + 1, 0, NULL,
                        (caddr_t) & (p_diropargs->name)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle2(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_diropargs->dir));
      cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_diropargs->name));
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

#define READLINKRES_PATH( _p_rl2res ) ( (_p_rl2res)->READLINK2res_u.data )

int cmdnfs_READLINK2res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  READLINK2res *p_rl2res = (READLINK2res *) p_nfs_struct;

  /* sanity check */
  if(p_rl2res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREADLINK2res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_rl2res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_rl2res->status == NFS_OK)
        {
          fprintf(out_stream, "%*sdata = \"%s\"\n", indent + 2, " ",
                  READLINKRES_PATH(p_rl2res));
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef READLINKRES_PATH

int cmdnfs_sattr2(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  sattr2 *p_sattr2 = (sattr2 *) p_nfs_struct;

  char *next_str = NULL;
  char *attrib_str;
  char *value_str;

  int mode, userid, groupid, rc;
  time_t a_sec, m_sec;
  int a_usec, m_usec;
  unsigned long long size;
  char *usec_str;
  char *time_str;

  char tmp_buff[1024];

  /* sanity check */
  if(p_sattr2 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* inits structure */

      p_sattr2->mode = (unsigned int)-1;
      p_sattr2->uid = (unsigned int)-1;
      p_sattr2->gid = (unsigned int)-1;
      p_sattr2->size = (unsigned int)-1;
      p_sattr2->atime.seconds = (unsigned int)-1;
      p_sattr2->atime.useconds = (unsigned int)-1;
      p_sattr2->mtime.seconds = (unsigned int)-1;
      p_sattr2->mtime.useconds = (unsigned int)-1;

      if(argc == 0)
        return TRUE;

      /* at this point it must not be more than one string */
      if(argc != 1)
        return FALSE;

      /* the attributes are:
       * mode, uid, gid, size, atime, mtime
       */
      strncpy(tmp_buff, argv[0], 1024);

      attrib_str = strtok_r(tmp_buff, ",", &next_str);

      if(attrib_str == NULL)
        {
#ifdef _DEBUG_NFS_SHELL
          printf("Unexpected parsing error.\n");
#endif
          return FALSE;
        }

      while(attrib_str != NULL)
        {

          /* retrieving attribute name and value */
          attrib_str = strtok_r(attrib_str, "=", &value_str);

          if((attrib_str == NULL) || (value_str == NULL))
            {
#ifdef _DEBUG_NFS_SHELL
              printf
                  ("Syntax error for sattr2.\nExpected syntax: <attr>=<value>,<attr>=<value>,...\n");
#endif
              return FALSE;
            }
#ifdef _DEBUG_NFS_SHELL
          printf("Attribute: \"%s\", Value: \"%s\"\n", attrib_str, value_str);
#endif

          if(!strcasecmp(attrib_str, "mode"))
            {
              mode = atomode(value_str);
              if(mode < 0)
                return FALSE;
              p_sattr2->mode = (unsigned int)mode;
#ifdef _DEBUG_NFS_SHELL
              printf("  mode = 0%o\n", p_sattr2->mode);
#endif
            }
          else if(!strcasecmp(attrib_str, "uid"))
            {
              userid = my_atoi(value_str);
              if(userid < 0)
                return FALSE;
              p_sattr2->uid = (unsigned int)userid;
#ifdef _DEBUG_NFS_SHELL
              printf("  uid = %u\n", p_sattr2->uid);
#endif
            }
          else if(!strcasecmp(attrib_str, "gid"))
            {
              groupid = my_atoi(value_str);
              if(groupid < 0)
                return FALSE;
              p_sattr2->gid = (unsigned int)groupid;
#ifdef _DEBUG_NFS_SHELL
              printf("  gid = %u\n", p_sattr2->gid);
#endif
            }
          else if(!strcasecmp(attrib_str, "size"))
            {
              rc = ato64(value_str, &size);
              if(rc)
                return FALSE;
              if(size > (unsigned long long)UINT_MAX)
                return FALSE;
              p_sattr2->size = (unsigned int)size;
#ifdef _DEBUG_NFS_SHELL
              printf("  size = %u\n", p_sattr2->size);
#endif
            }
          else if(!strcasecmp(attrib_str, "atime"))
            {
              time_str = strtok_r(value_str, ".", &usec_str);

              a_sec = atotime(time_str);
              if(a_sec == ((time_t) - 1))
                return FALSE;

              if(usec_str == NULL)
                {
                  a_usec = 0;
                }
              else
                {
                  a_usec = my_atoi(usec_str);
                  /* 1 million is authorized and is interpreted by server as a "set to server time" */
                  if((a_usec < 0) || (a_usec > 1000000))
                    return FALSE;
                }
              p_sattr2->atime.seconds = (unsigned int)a_sec;
              p_sattr2->atime.useconds = (unsigned int)a_usec;

#ifdef _DEBUG_NFS_SHELL
              printf("  atime = %u.%.6u\n", p_sattr2->atime.seconds,
                     p_sattr->atime.useconds);
#endif
            }
          else if(!strcasecmp(attrib_str, "mtime"))
            {
              time_str = strtok_r(value_str, ".", &usec_str);

              m_sec = atotime(time_str);
              if(m_sec == ((time_t) - 1))
                return FALSE;

              if(usec_str == NULL)
                {
                  m_usec = 0;
                }
              else
                {
                  m_usec = my_atoi(usec_str);
                  /* 1 million is authorized and is interpreted by server as a "set to server time" */
                  if((m_usec < 0) || (m_usec > 1000000))
                    return FALSE;
                }
              p_sattr2->mtime.seconds = (unsigned int)m_sec;
              p_sattr2->mtime.useconds = (unsigned int)m_usec;

#ifdef _DEBUG_NFS_SHELL
              printf("  mtime = %u.%.6u\n", p_sattr2->mtime.seconds,
                     p_sattr->mtime.useconds);
#endif
            }
          else
            {
#ifdef _DEBUG_NFS_SHELL
              printf
                  ("Syntax error for sattr2.\n<attr> must be one of the following: mode, uid, gid, size, atime, mtime.\n");
#endif
              return FALSE;
            }

          attrib_str = next_str;

          next_str = NULL;      /* paranoid setting */
          value_str = NULL;     /* paranoid setting */

          if(attrib_str != NULL)
            attrib_str = strtok_r(attrib_str, ",", &next_str);

        }

      return TRUE;

    case CMDNFS_FREE:
      /* nothing to do */
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }

}

int cmdnfs_CREATE2args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  CREATE2args *p_create2args = (CREATE2args *) p_nfs_struct;

  /* sanity check */
  if(p_create2args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(cmdnfs_diropargs2(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_create2args->where)) == FALSE)
        {
          return FALSE;
        }

      /* optional sattr2 parameters */
      if(cmdnfs_sattr2(CMDNFS_ENCODE, argc - 2, argv + 2, 0, NULL,
                       (caddr_t) & (p_create2args->attributes)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_diropargs2(CMDNFS_FREE, 0, NULL, 0, NULL,
                        (caddr_t) & (p_create2args->where));
      cmdnfs_sattr2(CMDNFS_FREE, 0, NULL, 0, NULL,
                    (caddr_t) & (p_create2args->attributes));
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

int cmdnfs_SETATTR2args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  SETATTR2args *p_SETATTR2args = (SETATTR2args *) p_nfs_struct;

  /* sanity check */
  if(p_SETATTR2args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 2)
        return FALSE;

      if(cmdnfs_fhandle2(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_SETATTR2args->file)) == FALSE)
        {
          return FALSE;
        }

      /* mandatory sattr2 parameter */
      if(cmdnfs_sattr2(CMDNFS_ENCODE, argc - 1, argv + 1, 0, NULL,
                       (caddr_t) & (p_SETATTR2args->attributes)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle2(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_SETATTR2args->file));
      cmdnfs_sattr2(CMDNFS_FREE, 0, NULL, 0, NULL,
                    (caddr_t) & (p_SETATTR2args->attributes));
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

int cmdnfs_READDIR2args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  caddr_t p_cookie;
  int count;
  int cookie;
  READDIR2args *p_READDIR2args = (READDIR2args *) p_nfs_struct;

  /* sanity check */
  if(p_READDIR2args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 3)
        return FALSE;

      if(cmdnfs_fhandle2(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_READDIR2args->dir)) == FALSE)
        {
          return FALSE;
        }

      /* cookie = 4 octets */

      cookie = my_atoi(argv[1]);
      if(cookie < 0)
        return FALSE;
      p_cookie = (caddr_t) & (p_READDIR2args->cookie);

      memcpy(p_cookie, &cookie, 4);

      /* count */

      count = my_atoi(argv[2]);
      if(count < 0)
        return FALSE;
      p_READDIR2args->count = (unsigned int)count;

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle2(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_READDIR2args->dir));

      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

static int cmdnfs_READDIR2resok(cmdnfs_encodetype_t encodeflag,
                                int argc, char **argv,
                                int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIR2resok *p_READDIR2resok = (READDIR2resok *) p_nfs_struct;

  entry2 *p_entry2 = p_READDIR2resok->entries;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      /* print the list of entries */
      if(p_entry2 != NULL)
        fprintf(out_stream, "%*sDirEntries:\n", indent, " ");

      while(p_entry2)
        {
          fprintf(out_stream, "%*s{\n", indent + 2, " ");
          fprintf(out_stream, "%*sfileid = %#x\n", indent + 4, " ", p_entry2->fileid);
          fprintf(out_stream, "%*sname = %s\n", indent + 4, " ", p_entry2->name);
          fprintf(out_stream, "%*scookie = %u\n", indent + 4, " ",
                  *(p_entry2->cookie));
          fprintf(out_stream, "%*s}\n", indent + 2, " ");

          p_entry2 = p_entry2->nextentry;
        }

      /* prinf the eof boolean */

      if(p_READDIR2resok->eof)
        fprintf(out_stream, "%*seof = TRUE\n", indent, " ");
      else
        fprintf(out_stream, "%*seof = FALSE\n", indent, " ");

      return TRUE;
      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

int cmdnfs_READDIR2res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIR2res *p_READDIR2res = (READDIR2res *) p_nfs_struct;

  /* sanity check */
  if(p_READDIR2res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* it is never an input */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREADDIR2res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      /* Convert status to error code */
      if(cmdnfs_nfsstat2(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_READDIR2res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_READDIR2res->status == NFS_OK)
        {
          if(cmdnfs_READDIR2resok(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                                  (caddr_t) & p_READDIR2res->READDIR2res_u.readdirok) ==
             FALSE)
            {
              return FALSE;
            }
        }
      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_FREE:

      /* it is never an input (never encoded, never allocated) */
      return FALSE;

      break;

    default:
      return FALSE;
    }
}

int cmdnfs_RENAME2args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  RENAME2args *p_RENAME2args = (RENAME2args *) p_nfs_struct;

  /* sanity check */
  if(p_RENAME2args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 4)
        return FALSE;

      /* The first 2 args */
      if(cmdnfs_diropargs2(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_RENAME2args->from)) == FALSE)
        {
          return FALSE;
        }

      /* The last 2 args */
      if(cmdnfs_diropargs2(CMDNFS_ENCODE, argc - 2, argv + 2, 0, NULL,
                           (caddr_t) & (p_RENAME2args->to)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_diropargs2(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_RENAME2args->from));
      cmdnfs_diropargs2(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_RENAME2args->to));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

int cmdnfs_nfsstat3(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  nfsstat3 *p_stat3 = (nfsstat3 *) p_nfs_struct;

  /* sanity check */
  if(p_stat3 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sstatus = %d (%s)\n", indent, " ", (int)(*p_stat3),
              nfsstat3_to_str(*p_stat3));
      return TRUE;

      break;

    case CMDNFS_ENCODE:
    case CMDNFS_FREE:
      /* never encoded */
    default:
      return FALSE;
    }

}

int cmdnfs_fattr3(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  fattr3 *p_fattr = (fattr3 *) p_nfs_struct;
  char tmp_buff[256] = "";
  struct tm paramtm;

  /* sanity check */
  if(p_fattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /** @todo */
      return FALSE;

      break;
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sfattr3 =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      fprintf(out_stream, "%*stype = %d (%s)\n", indent + 2, " ", (int)p_fattr->type,
              nfstype3_to_str(p_fattr->type));
      fprintf(out_stream, "%*smode = 0%o\n", indent + 2, " ", p_fattr->mode);
      fprintf(out_stream, "%*snlink = %u\n", indent + 2, " ", p_fattr->nlink);
      fprintf(out_stream, "%*suid = %u\n", indent + 2, " ", p_fattr->uid);
      fprintf(out_stream, "%*sgid = %u\n", indent + 2, " ", p_fattr->gid);
      fprintf(out_stream, "%*ssize = %llu\n", indent + 2, " ", p_fattr->size);
      fprintf(out_stream, "%*sused = %llu\n", indent + 2, " ", p_fattr->used);
      fprintf(out_stream, "%*srdev = %u.%u\n", indent + 2, " ", p_fattr->rdev.specdata1,
              p_fattr->rdev.specdata2);
      fprintf(out_stream, "%*sfsid = %#llx\n", indent + 2, " ", p_fattr->fsid);
      fprintf(out_stream, "%*sfileid = %#llx\n", indent + 2, " ", p_fattr->fileid);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->atime.seconds, &paramtm));
      fprintf(out_stream, "%*satime = %u.%.9u (%s)\n", indent + 2, " ",
              p_fattr->atime.seconds, p_fattr->atime.nseconds, tmp_buff);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->mtime.seconds, &paramtm));
      fprintf(out_stream, "%*smtime = %u.%.9u (%s)\n", indent + 2, " ",
              p_fattr->mtime.seconds, p_fattr->mtime.nseconds, tmp_buff);

      strftime(tmp_buff, 256, "%Y-%m-%d %T",
               Localtime_r((time_t *) & p_fattr->ctime.seconds, &paramtm));
      fprintf(out_stream, "%*sctime = %u.%.9u (%s)\n", indent + 2, " ",
              p_fattr->ctime.seconds, p_fattr->ctime.nseconds, tmp_buff);

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_FREE:

      /** @todo */
      return FALSE;

      break;

    default:
      return FALSE;
    }

}

/* interprets a list of attributes separated with a colon :
 * mode=0755,uid=...,gid=...,etc...
 */
int cmdnfs_sattr3(cmdnfs_encodetype_t encodeflag,
                  int argc, char **argv,
                  int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  sattr3 *p_sattr = (sattr3 *) p_nfs_struct;
  char *next_str = NULL;
  char *attrib_str;
  char *value_str;

  int mode, userid, groupid, rc;
  time_t a_sec, m_sec;
  int a_nsec, m_nsec;
  unsigned long long size;
  char *nsec_str;
  char *time_str;
  char tmp_buff[1024];

  /* sanity check */
  if(p_sattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* inits structure */
      memset((caddr_t) p_sattr, 0, sizeof(sattr3));

      p_sattr->mode.set_it = FALSE;
      p_sattr->uid.set_it = FALSE;
      p_sattr->gid.set_it = FALSE;
      p_sattr->size.set_it = FALSE;
      p_sattr->atime.set_it = DONT_CHANGE;
      p_sattr->mtime.set_it = DONT_CHANGE;

      if(argc == 0)
        return TRUE;

      /* at this point it must not be more than one string */
      if(argc != 1)
        return FALSE;

      /* the attributes are:
       * mode, uid, gid, size, atime, mtime
       */
      strncpy(tmp_buff, argv[0], 1024);

      attrib_str = strtok_r(tmp_buff, ",", &next_str);

      if(attrib_str == NULL)
        {
#ifdef _DEBUG_NFS_SHELL
          printf("Unexpected parsing error.\n");
#endif
          return FALSE;
        }

      while(attrib_str != NULL)
        {

          /* retrieving attribute value */
          attrib_str = strtok_r(attrib_str, "=", &value_str);

          if((attrib_str == NULL) || (value_str == NULL))
            {
#ifdef _DEBUG_NFS_SHELL
              printf
                  ("Syntax error for sattr3.\nExpected syntax: <attr>=<value>,<attr>=<value>,...\n");
#endif
              return FALSE;
            }
#ifdef _DEBUG_NFS_SHELL
          printf("Attribute: \"%s\", Value: \"%s\"\n", attrib_str, value_str);
#endif

          if(!strcasecmp(attrib_str, "mode"))
            {
              mode = atomode(value_str);
              if(mode < 0)
                return FALSE;
              p_sattr->mode.set_it = TRUE;
              p_sattr->mode.set_mode3_u.mode = (unsigned int)mode;
#ifdef _DEBUG_NFS_SHELL
              printf("  mode = 0%o\n", p_sattr->mode.set_mode3_u.mode);
#endif
            }
          else if(!strcasecmp(attrib_str, "uid"))
            {
              userid = my_atoi(value_str);
              if(userid < 0)
                return FALSE;
              p_sattr->uid.set_it = TRUE;
              p_sattr->uid.set_uid3_u.uid = (unsigned int)userid;
#ifdef _DEBUG_NFS_SHELL
              printf("  uid = %u\n", p_sattr->uid.set_uid3_u.uid);
#endif
            }
          else if(!strcasecmp(attrib_str, "gid"))
            {
              groupid = my_atoi(value_str);
              if(groupid < 0)
                return FALSE;
              p_sattr->gid.set_it = TRUE;
              p_sattr->gid.set_gid3_u.gid = (unsigned int)groupid;
#ifdef _DEBUG_NFS_SHELL
              printf("  gid = %u\n", p_sattr->gid.set_gid3_u.gid);
#endif
            }
          else if(!strcasecmp(attrib_str, "size"))
            {
              rc = ato64(value_str, &size);
              if(rc)
                return FALSE;
              p_sattr->size.set_it = TRUE;
              p_sattr->size.set_size3_u.size = (size3) size;
#ifdef _DEBUG_NFS_SHELL
              printf("  size = %llu\n", p_sattr->size.set_size3_u.size);
#endif
            }
          else if(!strcasecmp(attrib_str, "atime"))
            {
              time_str = strtok_r(value_str, ".", &nsec_str);

              a_sec = atotime(time_str);
              if(a_sec == ((time_t) - 1))
                return FALSE;

              if(nsec_str == NULL)
                {
                  a_nsec = 0;
                }
              else
                {
                  a_nsec = my_atoi(nsec_str);
                  if((a_nsec < 0) || (a_nsec > 999999999))
                    return FALSE;
                }

              p_sattr->atime.set_it = SET_TO_CLIENT_TIME;
              p_sattr->atime.set_atime_u.atime.seconds = a_sec;
              p_sattr->atime.set_atime_u.atime.nseconds = a_nsec;

#ifdef _DEBUG_NFS_SHELL
              printf("  atime = %u.%.9u\n", p_sattr->atime.set_atime_u.atime.seconds,
                     p_sattr->atime.set_atime_u.atime.nseconds);
#endif
            }
          else if(!strcasecmp(attrib_str, "mtime"))
            {
              time_str = strtok_r(value_str, ".", &nsec_str);

              m_sec = atotime(time_str);
              if(m_sec == ((time_t) - 1))
                return FALSE;

              if(nsec_str == NULL)
                {
                  m_nsec = 0;
                }
              else
                {
                  m_nsec = my_atoi(nsec_str);
                  if((m_nsec < 0) || (m_nsec > 999999999))
                    return FALSE;
                }

              p_sattr->mtime.set_it = SET_TO_CLIENT_TIME;
              p_sattr->mtime.set_mtime_u.mtime.seconds = m_sec;
              p_sattr->mtime.set_mtime_u.mtime.nseconds = m_nsec;
#ifdef _DEBUG_NFS_SHELL
              printf("  mtime = %u.%.9u\n", p_sattr->mtime.set_mtime_u.mtime.seconds,
                     p_sattr->mtime.set_mtime_u.mtime.nseconds);
#endif
            }
          else
            {
#ifdef _DEBUG_NFS_SHELL
              printf
                  ("Syntax error for sattr3.\n<attr> must be one of the following: mode, uid, gid, size, atime, mtime.\n");
#endif
              return FALSE;
            }

          attrib_str = next_str;

          next_str = NULL;      /* paranoid setting */
          value_str = NULL;     /* paranoid setting */

          if(attrib_str != NULL)
            attrib_str = strtok_r(attrib_str, ",", &next_str);

        }

      return TRUE;
      break;

    case CMDNFS_FREE:
      /* nothing to do */
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }

}                               /* cmdnfs_sattr3 */

#define GETATTR3_ATTRIBUTES( _p_GETATTR3res ) ( (_p_GETATTR3res)->GETATTR3res_u.resok.obj_attributes )

int cmdnfs_GETATTR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  GETATTR3res *p_gattr3res = (GETATTR3res *) p_nfs_struct;

  /* sanity check */
  if(p_gattr3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sGETATTR3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_gattr3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_gattr3res->status == NFS3_OK)
        {
          if(cmdnfs_fattr3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                           (caddr_t) & GETATTR3_ATTRIBUTES(p_gattr3res)) == FALSE)
            {
              return FALSE;
            }

        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef GETATTR3_ATTRIBUTES

int cmdnfs_diropargs3(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  diropargs3 *p_dirop3 = (diropargs3 *) p_nfs_struct;

  /* sanity check */
  if(p_dirop3 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_dirop3->dir)) == FALSE)
        {
          return FALSE;
        }

      if(cmdnfs_dirpath(CMDNFS_ENCODE, argc - 1, argv + 1, 0, NULL,
                        (caddr_t) & (p_dirop3->name)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_dirop3->dir));
      cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_dirop3->name));
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

int cmdnfs_postopattr(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  post_op_attr *p_opattr = (post_op_attr *) p_nfs_struct;

  /* sanity check */
  if(p_opattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      if(p_opattr->attributes_follow)
        {
          return cmdnfs_fattr3(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                               (caddr_t) & (p_opattr->post_op_attr_u.attributes));
        }
      else
        {
          fprintf(out_stream, "%*sN/A\n", indent, " ");
          return TRUE;
        }

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

int cmdnfs_postopfh3(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  post_op_fh3 *p_opfh3 = (post_op_fh3 *) p_nfs_struct;

  /* sanity check */
  if(p_opfh3 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      if(p_opfh3->handle_follows)
        {
          return cmdnfs_fhandle3(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                                 (caddr_t) & (p_opfh3->post_op_fh3_u.handle));
        }
      else
        {
          fprintf(out_stream, "%*sN/A\n", indent, " ");
          return TRUE;
        }

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#define LOOKUP3_OK_HANDLE( _p_LOOKUP3res ) ( (_p_LOOKUP3res)->LOOKUP3res_u.resok.object )
#define LOOKUP3_OK_OBJATTR( _p_LOOKUP3res ) ( (_p_LOOKUP3res)->LOOKUP3res_u.resok.obj_attributes )
#define LOOKUP3_OK_DIRATTR( _p_LOOKUP3res ) ( (_p_LOOKUP3res)->LOOKUP3res_u.resok.dir_attributes )
#define LOOKUP3_FAIL_DIRATTR( _p_LOOKUP3res ) ( (_p_LOOKUP3res)->LOOKUP3res_u.resfail.dir_attributes )

int cmdnfs_LOOKUP3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  LOOKUP3res *p_lkup3res = (LOOKUP3res *) p_nfs_struct;

  /* sanity check */
  if(p_lkup3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sLOOKUP3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_lkup3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_lkup3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sObject Handle:\n", indent + 2, " ");
          if(cmdnfs_fhandle3(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                             (caddr_t) & LOOKUP3_OK_HANDLE(p_lkup3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & LOOKUP3_OK_OBJATTR(p_lkup3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & LOOKUP3_OK_DIRATTR(p_lkup3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & LOOKUP3_FAIL_DIRATTR(p_lkup3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef LOOKUP3_OK_HANDLE
#undef LOOKUP3_OK_OBJATTR
#undef LOOKUP3_OK_DIRATTR
#undef LOOKUP3_FAIL_DIRATTR

static int cmdnfs_verf3(cmdnfs_encodetype_t encodeflag, int argc, char **argv, int indent, FILE * out_stream, caddr_t p_nfs_struct, char *verfname      /* name to be printed for the verifier (DECODE mode only) */
    )
{
  /* pointer to an opaque buff */
  caddr_t p_verf = p_nfs_struct;

  char *str_verf;
  char str_printverf[17];
  int rc;

  /* sanity check */
  if(p_verf == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 1)
        return FALSE;

      str_verf = argv[0];

      memset(p_verf, 0, 8);

      rc = sscanmem(p_verf, 8, str_verf);

#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "verf = \"%s\"\n", str_verf);
      fprintf(stderr, "-> %d bytes read.\n", rc);
      fprintf(stderr, "buffer=%02X.%02X.%02X.%02X.%02X.%02X.%02X.%02X\n",
              p_verf[0], p_verf[1], p_verf[2], p_verf[3],
              p_verf[4], p_verf[5], p_verf[6], p_verf[7]);

#endif

      /* we must have read at least the verf size */
      if(rc < 0)
        return FALSE;

      return TRUE;

      break;
    case CMDNFS_DECODE:

      if(snprintmem(str_printverf, 17, p_verf, 8) < 0)
        {
          return FALSE;
        }

      fprintf(out_stream, "%*s%s = %s\n", indent, " ", verfname, str_printverf);

      return TRUE;
      break;

    case CMDNFS_FREE:
      /* nothing to do */
      return TRUE;
      break;

    default:
      return FALSE;
    }
}

static int cmdnfs_dirlist3(cmdnfs_encodetype_t encodeflag,
                           int argc, char **argv,
                           int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  dirlist3 *p_dirlist3 = (dirlist3 *) p_nfs_struct;

  entry3 *p_entry = NULL;

  /* sanity check */
  if(p_dirlist3 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      /* print the list of entries */
      p_entry = p_dirlist3->entries;

      fprintf(out_stream, "%*sDirEntries:\n", indent, " ");
      while(p_entry)
        {
          fprintf(out_stream, "%*s{\n", indent + 2, " ");
          fprintf(out_stream, "%*sfileid = %#llx\n", indent + 4, " ", p_entry->fileid);
          fprintf(out_stream, "%*sname = %s\n", indent + 4, " ", p_entry->name);
          fprintf(out_stream, "%*scookie = %llu\n", indent + 4, " ", p_entry->cookie);
          fprintf(out_stream, "%*s}\n", indent + 2, " ");

          p_entry = p_entry->nextentry;
        }

      /* prinf the eof boolean */

      if(p_dirlist3->eof)
        fprintf(out_stream, "%*seof = TRUE\n", indent, " ");
      else
        fprintf(out_stream, "%*seof = FALSE\n", indent, " ");

      return TRUE;
      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

int cmdnfs_READDIR3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIR3args *p_rd3arg = (READDIR3args *) p_nfs_struct;
  unsigned long long tmp64;
  int rc;

  /* sanity check */
  if(p_rd3arg == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 4)
        return FALSE;

      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_rd3arg->dir)) == FALSE)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "dir_handle error.\n");
#endif
          return FALSE;
        }

      rc = ato64(argv[1], &tmp64);
      if(rc)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "cookie error.\n");
#endif
          return FALSE;
        }
      p_rd3arg->cookie = tmp64;

#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "cookie = %llu.\n", tmp64);
#endif

      if(cmdnfs_verf3(CMDNFS_ENCODE, 1, argv + 2, 0, NULL,
                      (caddr_t) & (p_rd3arg->cookieverf), NULL) == FALSE)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "cookieverf error.\n");
#endif
          return FALSE;
        }

      rc = ato64(argv[3], &tmp64);
      if(rc)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "count error (not a number).\n");
#endif
          return FALSE;
        }
      if(tmp64 > (unsigned long long)UINT_MAX)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "count error (number too big).\n");
#endif
          return FALSE;
        }

      p_rd3arg->count = (unsigned int)tmp64;
#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "count = %u\n", p_rd3arg->count);
#endif

      return TRUE;

    case CMDNFS_FREE:

      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_rd3arg->dir));

      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

#define READDIR3_OK( _p_READDIR3res ) ( (_p_READDIR3res)->READDIR3res_u.resok )
#define READDIR3_FAIL( _p_READDIR3res ) ( (_p_READDIR3res)->READDIR3res_u.resfail )

int cmdnfs_READDIR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIR3res *p_rd3res = (READDIR3res *) p_nfs_struct;

  /* sanity check */
  if(p_rd3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREADDIR3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_rd3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_rd3res->status == NFS3_OK)
        {
          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & (READDIR3_OK(p_rd3res).dir_attributes)) ==
             FALSE)
            {
              return FALSE;
            }
          if(cmdnfs_verf3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                          (caddr_t) & (READDIR3_OK(p_rd3res).cookieverf),
                          "cookieverf") == FALSE)
            {
              return FALSE;
            }

          if(cmdnfs_dirlist3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                             (caddr_t) & (READDIR3_OK(p_rd3res).reply)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & (READDIR3_FAIL(p_rd3res).dir_attributes)) ==
             FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#undef READDIR3_OK
#undef READDIR3_FAIL

int cmdnfs_READDIRPLUS3args(cmdnfs_encodetype_t encodeflag,
                            int argc, char **argv,
                            int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIRPLUS3args *p_rdp3arg = (READDIRPLUS3args *) p_nfs_struct;
  unsigned long long tmp64;
  int rc;

  /* sanity check */
  if(p_rdp3arg == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 5)
        return FALSE;

      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_rdp3arg->dir)) == FALSE)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "dir_handle error.\n");
#endif
          return FALSE;
        }

      rc = ato64(argv[1], &tmp64);
      if(rc)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "cookie error.\n");
#endif
          return FALSE;
        }
      p_rdp3arg->cookie = tmp64;

#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "cookie = %llu.\n", tmp64);
#endif

      if(cmdnfs_verf3(CMDNFS_ENCODE, 1, argv + 2, 0, NULL,
                      (caddr_t) & (p_rdp3arg->cookieverf), NULL) == FALSE)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "cookieverf error.\n");
#endif
          return FALSE;
        }

      rc = ato64(argv[3], &tmp64);
      if(rc)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "dircount error (not a number).\n");
#endif
          return FALSE;
        }
      if(tmp64 > (unsigned long long)UINT_MAX)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "dircount error (number too big).\n");
#endif
          return FALSE;
        }

      p_rdp3arg->dircount = (unsigned int)tmp64;
#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "dircount = %u\n", p_rdp3arg->dircount);
#endif

      rc = ato64(argv[4], &tmp64);
      if(rc)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "maxcount error (not a number).\n");
#endif
          return FALSE;
        }
      if(tmp64 > (unsigned long long)UINT_MAX)
        {
#ifdef _DEBUG_NFS_SHELL
          fprintf(stderr, "maxcount error (number too big).\n");
#endif
          return FALSE;
        }

      p_rdp3arg->maxcount = (unsigned int)tmp64;
#ifdef _DEBUG_NFS_SHELL
      fprintf(stderr, "maxcount = %u\n", p_rdp3arg->maxcount);
#endif

      return TRUE;

    case CMDNFS_FREE:

      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_rdp3arg->dir));

      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
  return FALSE;
}

static int cmdnfs_dirlistplus3(cmdnfs_encodetype_t encodeflag,
                               int argc, char **argv,
                               int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  dirlistplus3 *p_dirlistplus3 = (dirlistplus3 *) p_nfs_struct;

  entryplus3 *p_entry = NULL;

  /* sanity check */
  if(p_dirlistplus3 == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      /* print the list of entries */
      p_entry = p_dirlistplus3->entries;

      fprintf(out_stream, "%*sDirEntries:\n", indent, " ");
      while(p_entry)
        {
          fprintf(out_stream, "%*s{\n", indent + 2, " ");
          fprintf(out_stream, "%*sfileid = %#llx\n", indent + 4, " ", p_entry->fileid);
          fprintf(out_stream, "%*sname = %s\n", indent + 4, " ", p_entry->name);
          fprintf(out_stream, "%*scookie = %llu\n", indent + 4, " ", p_entry->cookie);

          fprintf(out_stream, "%*sPost-op attributes:\n", indent + 4, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 6, out_stream,
                               (caddr_t) & (p_entry->name_attributes)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op handle:\n", indent + 4, " ");
          if(cmdnfs_postopfh3(CMDNFS_DECODE, 0, NULL, indent + 6, out_stream,
                              (caddr_t) & (p_entry->name_handle)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*s}\n", indent + 2, " ");

          p_entry = p_entry->nextentry;
        }

      /* prinf the eof boolean */

      if(p_dirlistplus3->eof)
        fprintf(out_stream, "%*seof = TRUE\n", indent, " ");
      else
        fprintf(out_stream, "%*seof = FALSE\n", indent, " ");

      return TRUE;
      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#define READDIRPLUS3_OK( _p_READDIRPLUS3res ) ( (_p_READDIRPLUS3res)->READDIRPLUS3res_u.resok )
#define READDIRPLUS3_FAIL( _p_READDIRPLUS3res ) ( (_p_READDIRPLUS3res)->READDIRPLUS3res_u.resfail )

int cmdnfs_READDIRPLUS3res(cmdnfs_encodetype_t encodeflag,
                           int argc, char **argv,
                           int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READDIRPLUS3res *p_rdp3res = (READDIRPLUS3res *) p_nfs_struct;

  /* sanity check */
  if(p_rdp3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREADDIRPLUS3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_rdp3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_rdp3res->status == NFS3_OK)
        {
          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & (READDIRPLUS3_OK(p_rdp3res).dir_attributes))
             == FALSE)
            {
              return FALSE;
            }
          if(cmdnfs_verf3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                          (caddr_t) & (READDIRPLUS3_OK(p_rdp3res).cookieverf),
                          "cookieverf") == FALSE)
            {
              return FALSE;
            }

          if(cmdnfs_dirlistplus3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                                 (caddr_t) & (READDIRPLUS3_OK(p_rdp3res).reply)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (directory):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & (READDIRPLUS3_FAIL(p_rdp3res).dir_attributes))
             == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#undef READDIRPLUS3_OK
#undef READDIRPLUS3_FAIL

#define READLINK3_OK_ATTRS( _p_READLINK3res ) ( (_p_READLINK3res)->READLINK3res_u.resok.symlink_attributes )
#define READLINK3_OK_DATA( _p_READLINK3res ) ( (_p_READLINK3res)->READLINK3res_u.resok.data )
#define READLINK3_FAIL_ATTRS( _p_READLINK3res ) ( (_p_READLINK3res)->READLINK3res_u.resfail.symlink_attributes )

int cmdnfs_READLINK3res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  READLINK3res *p_readlnk3res = (READLINK3res *) p_nfs_struct;

  /* sanity check */
  if(p_readlnk3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREADLINK3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_readlnk3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_readlnk3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (symlink):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & READLINK3_OK_ATTRS(p_readlnk3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sdata = \"%s\"\n", indent + 2, " ",
                  READLINK3_OK_DATA(p_readlnk3res));
        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (symlink):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & READLINK3_FAIL_ATTRS(p_readlnk3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#undef READLINK3_OK_ATTRS
#undef READLINK3_OK_DATA
#undef READLINK3_FAIL_ATTRS

#define FSSTAT3_OK( _p_FSSTAT3res ) ( (_p_FSSTAT3res)->FSSTAT3res_u.resok )
#define FSSTAT3_FAIL_ATTRS( _p_FSSTAT3res ) ( (_p_FSSTAT3res)->FSSTAT3res_u.resfail.obj_attributes )

int cmdnfs_FSSTAT3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  FSSTAT3res *p_fsstat3res = (FSSTAT3res *) p_nfs_struct;

  /* sanity check */
  if(p_fsstat3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sFSSTAT3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_fsstat3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_fsstat3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (symlink):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & (FSSTAT3_OK(p_fsstat3res).obj_attributes)) ==
             FALSE)
            {
              return FALSE;
            }
          fprintf(out_stream, "%*stotal_bytes = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).tbytes);
          fprintf(out_stream, "%*sfree_bytes = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).fbytes);
          fprintf(out_stream, "%*savail_bytes = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).abytes);
          fprintf(out_stream, "%*stotal_files = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).tfiles);
          fprintf(out_stream, "%*sfree_files = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).ffiles);
          fprintf(out_stream, "%*savail_files = %llu\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).afiles);
          fprintf(out_stream, "%*sinvar_sec = %u\n", indent + 2, " ",
                  FSSTAT3_OK(p_fsstat3res).invarsec);

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & FSSTAT3_FAIL_ATTRS(p_fsstat3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#undef FSSTAT3_OK
#undef FSSTAT3_FAIL_ATTRS

int cmdnfs_ACCESS3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  ACCESS3args *p_access3args = (ACCESS3args *) p_nfs_struct;

  char *str_access;
  /* sanity check */
  if(p_access3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 2)
        return FALSE;

      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_access3args->object)) == FALSE)
        {
          return FALSE;
        }

      /* access 2e arg: R(ead) L(ookup) M(odify) E(xtend) D(elete) (e)X(ecute) */
      /* convert a "RLMEDX" string to an NFS access mask. */

      p_access3args->access = 0;

      str_access = argv[1];
      while(str_access[0] != '\0')
        {
          if((str_access[0] == 'r') || (str_access[0] == 'R'))
            p_access3args->access |= ACCESS3_READ;
          else if((str_access[0] == 'l') || (str_access[0] == 'L'))
            p_access3args->access |= ACCESS3_LOOKUP;
          else if((str_access[0] == 'm') || (str_access[0] == 'M'))
            p_access3args->access |= ACCESS3_MODIFY;
          else if((str_access[0] == 'e') || (str_access[0] == 'E'))
            p_access3args->access |= ACCESS3_EXTEND;
          else if((str_access[0] == 'd') || (str_access[0] == 'D'))
            p_access3args->access |= ACCESS3_DELETE;
          else if((str_access[0] == 'x') || (str_access[0] == 'X'))
            p_access3args->access |= ACCESS3_EXECUTE;
          else
            {
#ifdef _DEBUG_NFS_SHELL
              fprintf(stderr, "access flag error: unknown flag \"%c\".\n", str_access[0]);
#endif
              return FALSE;
            }

          str_access++;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_access3args->object));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

#define ACCESS3_OK_ATTRS( _p_ACCESS3res ) ( (_p_ACCESS3res)->ACCESS3res_u.resok.obj_attributes )
#define ACCESS3_OK_ACCESS( _p_ACCESS3res ) ( (_p_ACCESS3res)->ACCESS3res_u.resok.access )
#define ACCESS3_FAIL_ATTRS( _p_ACCESS3res ) ( (_p_ACCESS3res)->ACCESS3res_u.resfail.obj_attributes )

int cmdnfs_ACCESS3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  ACCESS3res *p_access3res = (ACCESS3res *) p_nfs_struct;

  /* sanity check */
  if(p_access3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sACCESS3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_access3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_access3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & ACCESS3_OK_ATTRS(p_access3res)) == FALSE)
            {
              return FALSE;
            }

          /* print acces rights */
          fprintf(out_stream, "%*saccess =", indent + 2, " ");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_READ)
            fprintf(out_stream, " READ");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_LOOKUP)
            fprintf(out_stream, " LOOKUP");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_MODIFY)
            fprintf(out_stream, " MODIFY");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_EXTEND)
            fprintf(out_stream, " EXTEND");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_DELETE)
            fprintf(out_stream, " DELETE");
          if(ACCESS3_OK_ACCESS(p_access3res) & ACCESS3_EXECUTE)
            fprintf(out_stream, " EXECUTE");
          fprintf(out_stream, " (%#.4x)\n", ACCESS3_OK_ACCESS(p_access3res));

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & ACCESS3_FAIL_ATTRS(p_access3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#undef ACCESS3_OK_ATTRS
#undef ACCESS3_OK_ACCESS
#undef ACCESS3_FAIL_ATTRS

int cmdnfs_CREATE3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  CREATE3args *p_CREATE3args = (CREATE3args *) p_nfs_struct;
  char *str_mode;

  /* sanity check */
  if(p_CREATE3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if((argc != 3) && (argc != 4))
        return FALSE;

      /* The first two args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_CREATE3args->where)) == FALSE)
        {
          return FALSE;
        }

      /* CREATE 3e arg: UNCHECKED, GUARDED or EXCLUSIVE */

      str_mode = argv[2];
      if(!strcasecmp(str_mode, "UNCHECKED"))
        p_CREATE3args->how.mode = UNCHECKED;
      else if(!strcasecmp(str_mode, "GUARDED"))
        p_CREATE3args->how.mode = GUARDED;
      else if(!strcasecmp(str_mode, "EXCLUSIVE"))
        p_CREATE3args->how.mode = EXCLUSIVE;
      else
        return FALSE;

      /* CREATE 4th arg: sattr3 list or a create verifier */

      switch (p_CREATE3args->how.mode)
        {
        case UNCHECKED:
        case GUARDED:

          /* The optional 4th arg is a sattr3 list */
          if(cmdnfs_sattr3(CMDNFS_ENCODE, argc - 3, argv + 3, 0, NULL,
                           (caddr_t) & (p_CREATE3args->how.createhow3_u.obj_attributes))
             == FALSE)
            {
              return FALSE;
            }

          break;
        case EXCLUSIVE:

          /* The mandatory 4rd arg is a create verifier */
          if(cmdnfs_verf3(CMDNFS_ENCODE, argc - 3, argv + 3, 0, NULL,
                          (caddr_t) & (p_CREATE3args->how.createhow3_u.verf),
                          NULL) == FALSE)
            {
              return FALSE;
            }

          break;
        }

      return TRUE;

    case CMDNFS_FREE:

      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL,
                        (caddr_t) & (p_CREATE3args->where));

      switch (p_CREATE3args->how.mode)
        {
        case UNCHECKED:
        case GUARDED:
          cmdnfs_sattr3(CMDNFS_FREE, 0, NULL, 0, NULL,
                        (caddr_t) & (p_CREATE3args->how.createhow3_u.obj_attributes));
          break;

        case EXCLUSIVE:
          cmdnfs_verf3(CMDNFS_FREE, 0, NULL, 0, NULL,
                       (caddr_t) & (p_CREATE3args->how.createhow3_u.verf), NULL);
          break;
        }

      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

int cmdnfs_preopattr(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  pre_op_attr *p_opattr = (pre_op_attr *) p_nfs_struct;
  wcc_attr *p_wccattr;
  char tmp_buff[256];
  struct tm paramtm;
  /* sanity check */
  if(p_opattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      if(p_opattr->attributes_follow)
        {
          p_wccattr = &(p_opattr->pre_op_attr_u.attributes);

          fprintf(out_stream, "%*s{\n", indent, " ");

          fprintf(out_stream, "%*ssize = %llu\n", indent + 2, " ", p_wccattr->size);

          strftime(tmp_buff, 256, "%Y-%m-%d %T",
                   Localtime_r((time_t *) & p_wccattr->mtime.seconds, &paramtm));
          fprintf(out_stream, "%*smtime = %u.%.9u (%s)\n", indent + 2, " ",
                  p_wccattr->mtime.seconds, p_wccattr->mtime.nseconds, tmp_buff);

          strftime(tmp_buff, 256, "%Y-%m-%d %T",
                   Localtime_r((time_t *) & p_wccattr->ctime.seconds, &paramtm));
          fprintf(out_stream, "%*sctime = %u.%.9u (%s)\n", indent + 2, " ",
                  p_wccattr->ctime.seconds, p_wccattr->ctime.nseconds, tmp_buff);

          fprintf(out_stream, "%*s}\n", indent, " ");
        }
      else
        {
          fprintf(out_stream, "%*sN/A\n", indent, " ");
          return TRUE;
        }

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
  return FALSE;
}

int cmdnfs_wccdata(cmdnfs_encodetype_t encodeflag,
                   int argc, char **argv,
                   int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  wcc_data *p_wccdata = (wcc_data *) p_nfs_struct;
  /* sanity check */

  if(p_wccdata == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*swcc_before:\n", indent, " ");
      if(cmdnfs_preopattr(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                          (caddr_t) & (p_wccdata->before)) == FALSE)
        return FALSE;

      fprintf(out_stream, "%*swcc_after:\n", indent, " ");
      if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent, out_stream,
                           (caddr_t) & (p_wccdata->after)) == FALSE)
        return FALSE;

      return TRUE;
      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }
}

#define CREATE3_OK_FH( _p_CREATE3res ) ( (_p_CREATE3res)->CREATE3res_u.resok.obj )
#define CREATE3_OK_ATTRS( _p_CREATE3res ) ( (_p_CREATE3res)->CREATE3res_u.resok.obj_attributes )
#define CREATE3_OK_WCC( _p_CREATE3res ) ( (_p_CREATE3res)->CREATE3res_u.resok.dir_wcc )
#define CREATE3_FAIL_WCC( _p_CREATE3res ) ( (_p_CREATE3res)->CREATE3res_u.resfail.dir_wcc )

int cmdnfs_CREATE3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  CREATE3res *p_CREATE3res = (CREATE3res *) p_nfs_struct;

  /* sanity check */
  if(p_CREATE3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sCREATE3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_CREATE3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_CREATE3res->status == NFS3_OK)
        {

          if(cmdnfs_postopfh3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                              (caddr_t) & CREATE3_OK_FH(p_CREATE3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & CREATE3_OK_ATTRS(p_CREATE3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sWcc_data (directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & CREATE3_OK_WCC(p_CREATE3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & CREATE3_FAIL_WCC(p_CREATE3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef CREATE3_OK_FH
#undef CREATE3_OK_ATTRS
#undef CREATE3_OK_WCC
#undef CREATE3_FAIL_WCC

int cmdnfs_MKDIR3args(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  MKDIR3args *p_MKDIR3args = (MKDIR3args *) p_nfs_struct;

  /* sanity check */
  if(p_MKDIR3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if((argc != 2) && (argc != 3))
        return FALSE;

      /* The first two args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_MKDIR3args->where)) == FALSE)
        {
          return FALSE;
        }

      /* MKDIR 3th arg: sattr3 list or a MKDIR verifier */

      if(argc == 3)
        {
          /* The 3th arg is a sattr3 list */
          if(cmdnfs_sattr3(CMDNFS_ENCODE, 1, argv + 2, 0, NULL,
                           (caddr_t) & (p_MKDIR3args->attributes)) == FALSE)
            {
              return FALSE;
            }
        }
      else
        {
          /* the sattr3 strucure is empty */
          if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL,
                           (caddr_t) & (p_MKDIR3args->attributes)) == FALSE)
            {
              return FALSE;
            }
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_MKDIR3args->where));
      cmdnfs_sattr3(CMDNFS_FREE, 0, NULL, 0, NULL,
                    (caddr_t) & (p_MKDIR3args->attributes));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

#define MKDIR3_OK_FH( _p_MKDIR3res ) ( (_p_MKDIR3res)->MKDIR3res_u.resok.obj )
#define MKDIR3_OK_ATTRS( _p_MKDIR3res ) ( (_p_MKDIR3res)->MKDIR3res_u.resok.obj_attributes )
#define MKDIR3_OK_WCC( _p_MKDIR3res ) ( (_p_MKDIR3res)->MKDIR3res_u.resok.dir_wcc )
#define MKDIR3_FAIL_WCC( _p_MKDIR3res ) ( (_p_MKDIR3res)->MKDIR3res_u.resfail.dir_wcc )

int cmdnfs_MKDIR3res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  MKDIR3res *p_MKDIR3res = (MKDIR3res *) p_nfs_struct;

  /* sanity check */
  if(p_MKDIR3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sMKDIR3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_MKDIR3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_MKDIR3res->status == NFS3_OK)
        {

          if(cmdnfs_postopfh3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                              (caddr_t) & MKDIR3_OK_FH(p_MKDIR3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op attributes (new dir):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & MKDIR3_OK_ATTRS(p_MKDIR3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & MKDIR3_OK_WCC(p_MKDIR3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & MKDIR3_FAIL_WCC(p_MKDIR3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef MKDIR3_OK_FH
#undef MKDIR3_OK_ATTRS
#undef MKDIR3_OK_WCC
#undef MKDIR3_FAIL_WCC

#define REMOVE3_OK_WCC( _p_REMOVE3res ) ( (_p_REMOVE3res)->REMOVE3res_u.resok.dir_wcc )
#define REMOVE3_FAIL_WCC( _p_REMOVE3res ) ( (_p_REMOVE3res)->REMOVE3res_u.resfail.dir_wcc )

int cmdnfs_REMOVE3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  REMOVE3res *p_REMOVE3res = (REMOVE3res *) p_nfs_struct;

  /* sanity check */
  if(p_REMOVE3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sREMOVE3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_REMOVE3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_REMOVE3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & REMOVE3_OK_WCC(p_REMOVE3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & REMOVE3_FAIL_WCC(p_REMOVE3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef REMOVE3_OK_WCC
#undef REMOVE3_FAIL_WCC

#define RMDIR3_OK_WCC( _p_RMDIR3res ) ( (_p_RMDIR3res)->RMDIR3res_u.resok.dir_wcc )
#define RMDIR3_FAIL_WCC( _p_RMDIR3res ) ( (_p_RMDIR3res)->RMDIR3res_u.resfail.dir_wcc )

int cmdnfs_RMDIR3res(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  RMDIR3res *p_RMDIR3res = (RMDIR3res *) p_nfs_struct;

  /* sanity check */
  if(p_RMDIR3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sRMDIR3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_RMDIR3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_RMDIR3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RMDIR3_OK_WCC(p_RMDIR3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (parent dir):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RMDIR3_FAIL_WCC(p_RMDIR3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef RMDIR3_OK_WCC
#undef RMDIR3_FAIL_WCC

int cmdnfs_sattrguard3(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  sattrguard3 *p_sattr = (sattrguard3 *) p_nfs_struct;

  time_t c_sec;
  int c_nsec;
  char *nsec_str = NULL;
  char *time_str;

  /* sanity check */
  if(p_sattr == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      /* inits structure */
      memset((caddr_t) p_sattr, 0, sizeof(sattrguard3));

      if(argc == 0)
        {
          /* empty structure */
          p_sattr->check = FALSE;
          return TRUE;
        }

      /* at this point it must not be more than one string */
      if(argc != 1)
        return FALSE;

      time_str = strtok_r(argv[0], ".", &nsec_str);

      c_sec = atotime(time_str);
      if(c_sec == ((time_t) - 1))
        return FALSE;

      if(nsec_str == NULL)
        {
          c_nsec = 0;
        }
      else
        {
          c_nsec = my_atoi(nsec_str);
          if((c_nsec < 0) || (c_nsec > 999999999))
            return FALSE;
        }

      p_sattr->check = TRUE;
      p_sattr->sattrguard3_u.obj_ctime.seconds = c_sec;
      p_sattr->sattrguard3_u.obj_ctime.nseconds = c_nsec;

#ifdef _DEBUG_NFS_SHELL
      printf("ctime check = %u.%.9u\n", p_sattr->sattrguard3_u.obj_ctime.seconds,
             p_sattr->sattrguard3_u.obj_ctime.nseconds);
#endif

      return TRUE;
      break;
    case CMDNFS_FREE:
      /* nothing to do */
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }

}                               /* cmdnfs_sattrguard3 */

int cmdnfs_SETATTR3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  SETATTR3args *p_SETATTR3args = (SETATTR3args *) p_nfs_struct;

  /* sanity check */
  if(p_SETATTR3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if((argc != 2) && (argc != 3))
        return FALSE;

      /* The first arg */
      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_SETATTR3args->object)) == FALSE)
        {
          return FALSE;
        }

      /* SETATTR 2th arg: sattr3 list */

      if(cmdnfs_sattr3(CMDNFS_ENCODE, 1, argv + 1, 0, NULL,
                       (caddr_t) & (p_SETATTR3args->new_attributes)) == FALSE)
        {
          return FALSE;
        }

      /* SETATTR optionnal 3th arg: sattrguard3 */

      if(cmdnfs_sattrguard3(CMDNFS_ENCODE, argc - 2, argv + 2, 0, NULL,
                            (caddr_t) & (p_SETATTR3args->guard)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL,
                      (caddr_t) & (p_SETATTR3args->object));
      cmdnfs_sattr3(CMDNFS_FREE, 0, NULL, 0, NULL,
                    (caddr_t) & (p_SETATTR3args->new_attributes));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

#define SETATTR3_OK_WCC( _p_SETATTR3res ) ( (_p_SETATTR3res)->SETATTR3res_u.resok.obj_wcc )
#define SETATTR3_FAIL_WCC( _p_SETATTR3res ) ( (_p_SETATTR3res)->SETATTR3res_u.resfail.obj_wcc )

int cmdnfs_SETATTR3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  SETATTR3res *p_SETATTR3res = (SETATTR3res *) p_nfs_struct;

  /* sanity check */
  if(p_SETATTR3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sSETATTR3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_SETATTR3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_SETATTR3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sWcc_data (object):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & SETATTR3_OK_WCC(p_SETATTR3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (object):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & SETATTR3_FAIL_WCC(p_SETATTR3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef SETATTR3_OK_WCC
#undef SETATTR3_FAIL_WCC

int cmdnfs_RENAME3args(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  RENAME3args *p_RENAME3args = (RENAME3args *) p_nfs_struct;

  /* sanity check */
  if(p_RENAME3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 4)
        return FALSE;

      /* The first 2 args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_RENAME3args->from)) == FALSE)
        {
          return FALSE;
        }

      /* The last 2 args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, argc - 2, argv + 2, 0, NULL,
                           (caddr_t) & (p_RENAME3args->to)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_RENAME3args->from));
      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_RENAME3args->to));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

#define RENAME3_OK_FROMWCC( _p_RENAME3res ) ( (_p_RENAME3res)->RENAME3res_u.resok.fromdir_wcc )
#define RENAME3_OK_TOWCC( _p_RENAME3res ) ( (_p_RENAME3res)->RENAME3res_u.resok.todir_wcc )
#define RENAME3_FAIL_FROMWCC( _p_RENAME3res ) ( (_p_RENAME3res)->RENAME3res_u.resfail.fromdir_wcc )
#define RENAME3_FAIL_TOWCC( _p_RENAME3res ) ( (_p_RENAME3res)->RENAME3res_u.resfail.todir_wcc )

int cmdnfs_RENAME3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  RENAME3res *p_RENAME3res = (RENAME3res *) p_nfs_struct;

  /* sanity check */
  if(p_RENAME3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sRENAME3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_RENAME3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_RENAME3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sWcc_data (source directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RENAME3_OK_FROMWCC(p_RENAME3res)) == FALSE)
            {
              return FALSE;
            }
          fprintf(out_stream, "%*sWcc_data (target directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RENAME3_OK_TOWCC(p_RENAME3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (source directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RENAME3_FAIL_FROMWCC(p_RENAME3res)) == FALSE)
            {
              return FALSE;
            }
          fprintf(out_stream, "%*sWcc_data (target directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & RENAME3_FAIL_TOWCC(p_RENAME3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef RENAME3_OK_FROMWCC
#undef RENAME3_OK_TOWCC
#undef RENAME3_FAIL_FROMWCC
#undef RENAME3_FAIL_TOWCC

int cmdnfs_LINK3args(cmdnfs_encodetype_t encodeflag,
                     int argc, char **argv,
                     int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  LINK3args *p_LINK3args = (LINK3args *) p_nfs_struct;

  /* sanity check */
  if(p_LINK3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if(argc != 3)
        return FALSE;

      /* The first arg */
      if(cmdnfs_fhandle3(CMDNFS_ENCODE, 1, argv, 0, NULL,
                         (caddr_t) & (p_LINK3args->file)) == FALSE)
        {
          return FALSE;
        }

      /* The last 2 args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, argc - 1, argv + 1, 0, NULL,
                           (caddr_t) & (p_LINK3args->link)) == FALSE)
        {
          return FALSE;
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_LINK3args->file));
      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & (p_LINK3args->link));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}

#define LINK3_OK_ATTRS( _p_LINK3res ) ( (_p_LINK3res)->LINK3res_u.resok.file_attributes )
#define LINK3_OK_WCC( _p_LINK3res ) ( (_p_LINK3res)->LINK3res_u.resok.linkdir_wcc )
#define LINK3_FAIL_ATTRS( _p_LINK3res ) ( (_p_LINK3res)->LINK3res_u.resfail.file_attributes )
#define LINK3_FAIL_WCC( _p_LINK3res ) ( (_p_LINK3res)->LINK3res_u.resfail.linkdir_wcc )

int cmdnfs_LINK3res(cmdnfs_encodetype_t encodeflag,
                    int argc, char **argv,
                    int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  LINK3res *p_LINK3res = (LINK3res *) p_nfs_struct;

  /* sanity check */
  if(p_LINK3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sLINK3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_LINK3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_LINK3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (file):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & LINK3_OK_ATTRS(p_LINK3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sWcc_data (link directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & LINK3_OK_WCC(p_LINK3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (file):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & LINK3_FAIL_ATTRS(p_LINK3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sWcc_data (link directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & LINK3_FAIL_WCC(p_LINK3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef LINK3_OK_ATTRS
#undef LINK3_OK_WCC
#undef LINK3_FAIL_ATTRS
#undef LINK3_FAIL_WCC

int cmdnfs_SYMLINK3args(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{
  SYMLINK3args *p_SYMLINK3args = (SYMLINK3args *) p_nfs_struct;

  /* sanity check */
  if(p_SYMLINK3args == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_ENCODE:

      if((argc != 3) && (argc != 4))
        return FALSE;

      /* The first 2 args */
      if(cmdnfs_diropargs3(CMDNFS_ENCODE, 2, argv, 0, NULL,
                           (caddr_t) & (p_SYMLINK3args->where)) == FALSE)
        {
          return FALSE;
        }

      /* optional 3rd arg */
      if(argc == 4)
        {
          if(cmdnfs_sattr3(CMDNFS_ENCODE, 1, argv + 2, 0, NULL,
                           (caddr_t) & (p_SYMLINK3args->symlink.symlink_attributes)) ==
             FALSE)
            {
              return FALSE;
            }
          /* 4th arg */
          if(cmdnfs_dirpath(CMDNFS_ENCODE, argc - 3, argv + 3, 0, NULL,
                            (caddr_t) & (p_SYMLINK3args->symlink.symlink_data)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          /* empty attr set */
          if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL,
                           (caddr_t) & (p_SYMLINK3args->symlink.symlink_attributes)) ==
             FALSE)
            {
              return FALSE;
            }
          if(cmdnfs_dirpath(CMDNFS_ENCODE, argc - 2, argv + 2, 0, NULL,
                            (caddr_t) & (p_SYMLINK3args->symlink.symlink_data)) == FALSE)
            {
              return FALSE;
            }
        }

      return TRUE;

    case CMDNFS_FREE:
      cmdnfs_diropargs3(CMDNFS_FREE, 0, NULL, 0, NULL,
                        (caddr_t) & (p_SYMLINK3args->where));
      cmdnfs_sattr3(CMDNFS_FREE, 0, NULL, 0, NULL,
                    (caddr_t) & (p_SYMLINK3args->symlink.symlink_attributes));
      cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL,
                     (caddr_t) & (p_SYMLINK3args->symlink.symlink_data));
      return TRUE;
      break;

    case CMDNFS_DECODE:
      /* never decoded */
    default:
      return FALSE;
    }
}                               /* cmdnfs_SYMLINK3args */

#define SYMLINK3_OK_FH( _p_SYMLINK3res ) ( (_p_SYMLINK3res)->SYMLINK3res_u.resok.obj )
#define SYMLINK3_OK_ATTRS( _p_SYMLINK3res ) ( (_p_SYMLINK3res)->SYMLINK3res_u.resok.obj_attributes )
#define SYMLINK3_OK_WCC( _p_SYMLINK3res ) ( (_p_SYMLINK3res)->SYMLINK3res_u.resok.dir_wcc )
#define SYMLINK3_FAIL_WCC( _p_SYMLINK3res ) ( (_p_SYMLINK3res)->SYMLINK3res_u.resfail.dir_wcc )

int cmdnfs_SYMLINK3res(cmdnfs_encodetype_t encodeflag,
                       int argc, char **argv,
                       int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  SYMLINK3res *p_SYMLINK3res = (SYMLINK3res *) p_nfs_struct;

  /* sanity check */
  if(p_SYMLINK3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sSYMLINK3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_SYMLINK3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_SYMLINK3res->status == NFS3_OK)
        {

          if(cmdnfs_postopfh3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                              (caddr_t) & SYMLINK3_OK_FH(p_SYMLINK3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sPost-op attributes (symlink):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & SYMLINK3_OK_ATTRS(p_SYMLINK3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*sWcc_data (directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & SYMLINK3_OK_WCC(p_SYMLINK3res)) == FALSE)
            {
              return FALSE;
            }

        }
      else
        {
          fprintf(out_stream, "%*sWcc_data (directory):\n", indent + 2, " ");
          if(cmdnfs_wccdata(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                            (caddr_t) & SYMLINK3_FAIL_WCC(p_SYMLINK3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}

#undef SYMLINK3_OK_FH
#undef SYMLINK3_OK_ATTRS
#undef SYMLINK3_OK_WCC
#undef SYMLINK3_FAIL_WCC

#define FSINFO3_OK_ATTRS( _p_FSINFO3res ) ( (_p_FSINFO3res)->FSINFO3res_u.resok.obj_attributes )
#define FSINFO3_OK_INFO( _p_FSINFO3res ) ( (_p_FSINFO3res)->FSINFO3res_u.resok )
#define FSINFO3_FAIL_ATTRS( _p_FSINFO3res ) ( (_p_FSINFO3res)->FSINFO3res_u.resfail.obj_attributes )

int cmdnfs_FSINFO3res(cmdnfs_encodetype_t encodeflag,
                      int argc, char **argv,
                      int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  FSINFO3res *p_FSINFO3res = (FSINFO3res *) p_nfs_struct;

  /* sanity check */
  if(p_FSINFO3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sFSINFO3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_FSINFO3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_FSINFO3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (root):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & FSINFO3_OK_ATTRS(p_FSINFO3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*srtmax = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).rtmax);
          fprintf(out_stream, "%*srtpref = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).rtpref);
          fprintf(out_stream, "%*srtmult = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).rtmult);
          fprintf(out_stream, "%*swtmax = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).wtmax);
          fprintf(out_stream, "%*swtpref = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).wtpref);
          fprintf(out_stream, "%*swtmult = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).wtmult);
          fprintf(out_stream, "%*sdtpref = %u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).dtpref);
          fprintf(out_stream, "%*smaxfilesize = %llu (%#llx)\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).maxfilesize,
                  FSINFO3_OK_INFO(p_FSINFO3res).maxfilesize);
          fprintf(out_stream, "%*stime_delta = %u.%.9u\n", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).time_delta.seconds,
                  FSINFO3_OK_INFO(p_FSINFO3res).time_delta.nseconds);
          fprintf(out_stream, "%*sproperties = %#x : ", indent + 2, " ",
                  FSINFO3_OK_INFO(p_FSINFO3res).properties);

          if(FSINFO3_OK_INFO(p_FSINFO3res).properties & FSF3_LINK)
            fprintf(out_stream, "FSF3_LINK ");
          if(FSINFO3_OK_INFO(p_FSINFO3res).properties & FSF3_SYMLINK)
            fprintf(out_stream, "FSF3_SYMLINK ");
          if(FSINFO3_OK_INFO(p_FSINFO3res).properties & FSF3_HOMOGENEOUS)
            fprintf(out_stream, "FSF3_HOMOGENEOUS ");
          if(FSINFO3_OK_INFO(p_FSINFO3res).properties & FSF3_CANSETTIME)
            fprintf(out_stream, "FSF3_CANSETTIME ");

          fprintf(out_stream, "\n");

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (root):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & FSINFO3_FAIL_ATTRS(p_FSINFO3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}                               /* cmdnfs_FSINFO3res */

#undef FSINFO3_OK_ATTRS
#undef FSINFO3_OK_INFO
#undef FSINFO3_FAIL_ATTRS

#define PATHCONF3_OK_ATTRS( _p_PATHCONF3res ) ( (_p_PATHCONF3res)->PATHCONF3res_u.resok.obj_attributes )
#define PATHCONF3_OK_INFO( _p_PATHCONF3res ) ( (_p_PATHCONF3res)->PATHCONF3res_u.resok )
#define PATHCONF3_FAIL_ATTRS( _p_PATHCONF3res ) ( (_p_PATHCONF3res)->PATHCONF3res_u.resfail.obj_attributes )

int cmdnfs_PATHCONF3res(cmdnfs_encodetype_t encodeflag,
                        int argc, char **argv,
                        int indent, FILE * out_stream, caddr_t p_nfs_struct)
{

  PATHCONF3res *p_PATHCONF3res = (PATHCONF3res *) p_nfs_struct;

  /* sanity check */
  if(p_PATHCONF3res == NULL)
    return FALSE;

  switch (encodeflag)
    {
    case CMDNFS_DECODE:

      fprintf(out_stream, "%*sPATHCONF3res =\n", indent, " ");
      fprintf(out_stream, "%*s{\n", indent, " ");

      if(cmdnfs_nfsstat3(CMDNFS_DECODE, 0, NULL, indent + 2, out_stream,
                         (caddr_t) & p_PATHCONF3res->status) == FALSE)
        {
          return FALSE;
        }

      if(p_PATHCONF3res->status == NFS3_OK)
        {

          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & PATHCONF3_OK_ATTRS(p_PATHCONF3res)) == FALSE)
            {
              return FALSE;
            }

          fprintf(out_stream, "%*slinkmax = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).linkmax);
          fprintf(out_stream, "%*sname_max = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).name_max);
          fprintf(out_stream, "%*sno_trunc = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).no_trunc);
          fprintf(out_stream, "%*schown_restricted = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).chown_restricted);
          fprintf(out_stream, "%*scase_insensitive = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).case_insensitive);
          fprintf(out_stream, "%*scase_preserving = %u\n", indent + 2, " ",
                  PATHCONF3_OK_INFO(p_PATHCONF3res).case_preserving);

        }
      else
        {
          fprintf(out_stream, "%*sPost-op attributes (object):\n", indent + 2, " ");
          if(cmdnfs_postopattr(CMDNFS_DECODE, 0, NULL, indent + 4, out_stream,
                               (caddr_t) & PATHCONF3_FAIL_ATTRS(p_PATHCONF3res)) == FALSE)
            {
              return FALSE;
            }
        }

      fprintf(out_stream, "%*s}\n", indent, " ");

      return TRUE;

      break;

    case CMDNFS_ENCODE:
      /* never encoded */
    case CMDNFS_FREE:
      /* it is never an input (never encoded, never allocated) */
    default:
      return FALSE;
    }

}                               /* cmdnfs_PATHCONF3res */

#undef PATHCONF3_OK_ATTRS
#undef PATHCONF3_OK_INFO
#undef PATHCONF3_FAIL_ATTRS

/*-----------------------------------------------------------------------*/

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
#define print_mask(_out,_mode,_mask,_lettre) do {    \
        if (_mode & _mask) fprintf(_out,_lettre);    \
        else fprintf(_out,"-");                      \
      } while(0)

void print_nfsitem_line(FILE * out, fattr3 * attrib, char *name, char *target)
{

  char buff[256];

  /* print inode */
  fprintf(out, "%10llx ", attrib->fileid);

  /* printing type */
  switch (attrib->type)
    {
    case NF3FIFO:
      fprintf(out, "p");
      break;
    case NF3CHR:
      fprintf(out, "c");
      break;
    case NF3DIR:
      fprintf(out, "d");
      break;
    case NF3BLK:
      fprintf(out, "b");
      break;
    case NF3REG:
      fprintf(out, "-");
      break;
    case NF3LNK:
      fprintf(out, "l");
      break;
    case NF3SOCK:
      fprintf(out, "s");
      break;
    default:
      fprintf(out, "?");
    }

  /* printing rights */
  print_mask(out, attrib->mode, S_IRUSR, "r");
  print_mask(out, attrib->mode, S_IWUSR, "w");

  if(attrib->mode & S_ISUID)
    {
      if(attrib->mode & S_IXUSR)
        fprintf(out, "s");
      else
        fprintf(out, "S");
    }
  else
    {
      if(attrib->mode & S_IXUSR)
        fprintf(out, "x");
      else
        fprintf(out, "-");
    }

  print_mask(out, attrib->mode, S_IRGRP, "r");
  print_mask(out, attrib->mode, S_IWGRP, "w");

  if(attrib->mode & S_ISGID)
    {
      if(attrib->mode & S_IXGRP)
        fprintf(out, "s");
      else
        fprintf(out, "l");
    }
  else
    {
      if(attrib->mode & S_IXGRP)
        fprintf(out, "x");
      else
        fprintf(out, "-");
    }
  print_mask(out, attrib->mode, S_IROTH, "r");
  print_mask(out, attrib->mode, S_IWOTH, "w");
  print_mask(out, attrib->mode, S_IXOTH, "x");

  fprintf(out, " %3u", attrib->nlink);
  fprintf(out, " %8d", attrib->uid);
  fprintf(out, " %8d", attrib->gid);
  fprintf(out, " %15llu", attrib->size);

  /* print mtime */
  fprintf(out, " %15s", time2str(attrib->mtime.seconds, buff));

  /* print name */
  fprintf(out, " %s", name);

  if(attrib->type == NF3LNK)
    fprintf(out, " -> %s", target);

  fprintf(out, "\n");
  return;

}                               /* print_nfsitem_line */

/**
 * print_nfs_attributes:
 * print an fattr3 to a given output file.
 *
 * \param attrs (in fattr3) The attributes to be printed.
 * \param output (in FILE *) The file where the attributes are to be printed.
 * \return Nothing.
 */
void print_nfs_attributes(fattr3 * attrs, FILE * output)
{

  fprintf(output, "\tType : %s\n", nfstype3_to_str(attrs->type));
  fprintf(output, "\tSize : %llu\n", attrs->size);
  fprintf(output, "\tfsId : %u.%u\n", (unsigned int)(attrs->fsid >> 32),
          (unsigned int)attrs->fsid);
  fprintf(output, "\tFileId : %#llx\n", attrs->fileid);
  fprintf(output, "\tMode : %#o\n", attrs->mode);
  fprintf(output, "\tNumlinks : %u\n", attrs->nlink);
  fprintf(output, "\tuid : %d\n", attrs->uid);
  fprintf(output, "\tgid : %d\n", attrs->gid);
  fprintf(output, "\tRawdev : %u.%u\n", attrs->rdev.specdata1, attrs->rdev.specdata2);
  fprintf(output, "\tatime : %s", ctime((time_t *) & attrs->atime.seconds));
  fprintf(output, "\tctime : %s", ctime((time_t *) & attrs->ctime.seconds));
  fprintf(output, "\tmtime : %s", ctime((time_t *) & attrs->mtime.seconds));
  fprintf(output, "\tspaceused : %llu\n", attrs->used);

}
