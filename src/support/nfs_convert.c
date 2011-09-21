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
 * \file    nfs_convert.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:02 $
 * \version $Revision: 1.3 $
 * \brief   nfs conversion tools.
 *
 * $Log: nfs_convert.c,v $
 *
 * Revision 1.2  2005/10/12 08:28:00  deniel
 * Format of the errror message.
 *
 * Revision 1.1  2005/08/05 08:46:35  leibovic
 * Convertion tools.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <string.h>
#include "rpc.h"
#include "nfs_core.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs23.h"

#ifdef HAVE_GSSAPI
OM_uint32 Gss_release_buffer(OM_uint32 * minor_status, gss_buffer_t buffer);
#endif

char *nfsstat2_to_str(nfsstat2 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NFS_OK:             return "NFS_OK";
    case NFSERR_PERM:        return "NFSERR_PERM";
    case NFSERR_NOENT:       return "NFSERR_NOENT";
    case NFSERR_IO:          return "NFSERR_IO";
    case NFSERR_NXIO:        return "NFSERR_NXIO";
    case NFSERR_ACCES:       return "NFSERR_ACCES";
    case NFSERR_EXIST:       return "NFSERR_EXIST";
    case NFSERR_NODEV:       return "NFSERR_NODEV";
    case NFSERR_NOTDIR:      return "NFSERR_NOTDIR";
    case NFSERR_ISDIR:       return "NFSERR_ISDIR";
    case NFSERR_FBIG:        return "NFSERR_FBIG";
    case NFSERR_NOSPC:       return "NFSERR_NOSPC";
    case NFSERR_ROFS:        return "NFSERR_ROFS";
    case NFSERR_NAMETOOLONG: return "NFSERR_NAMETOOLONG";
    case NFSERR_NOTEMPTY:    return "NFSERR_NOTEMPTY";
    case NFSERR_DQUOT:       return "NFSERR_DQUOT";
    case NFSERR_STALE:       return "NFSERR_STALE";
    case NFSERR_WFLUSH:      return "NFSERR_WFLUSH";
    }
  return "UNKNOWN NFSv2 ERROR CODE";
}

char *nfsstat3_to_str(nfsstat3 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NFS3_OK:             return "NFS3_OK";
    case NFS3ERR_PERM:        return "NFS3ERR_PERM";
    case NFS3ERR_NOENT:       return "NFS3ERR_NOENT";
    case NFS3ERR_IO:          return "NFS3ERR_IO";
    case NFS3ERR_NXIO:        return "NFS3ERR_NXIO";
    case NFS3ERR_ACCES:       return "NFS3ERR_ACCES";
    case NFS3ERR_EXIST:       return "NFS3ERR_EXIST";
    case NFS3ERR_XDEV:        return "NFS3ERR_XDEV";
    case NFS3ERR_NODEV:       return "NFS3ERR_NODEV";
    case NFS3ERR_NOTDIR:      return "NFS3ERR_NOTDIR";
    case NFS3ERR_ISDIR:       return "NFS3ERR_ISDIR";
    case NFS3ERR_INVAL:       return "NFS3ERR_INVAL";
    case NFS3ERR_FBIG:        return "NFS3ERR_FBIG";
    case NFS3ERR_NOSPC:       return "NFS3ERR_NOSPC";
    case NFS3ERR_ROFS:        return "NFS3ERR_ROFS";
    case NFS3ERR_MLINK:       return "NFS3ERR_MLINK";
    case NFS3ERR_NAMETOOLONG: return "NFS3ERR_NAMETOOLONG";
    case NFS3ERR_NOTEMPTY:    return "NFS3ERR_NOTEMPTY";
    case NFS3ERR_DQUOT:       return "NFS3ERR_DQUOT";
    case NFS3ERR_STALE:       return "NFS3ERR_STALE";
    case NFS3ERR_REMOTE:      return "NFS3ERR_REMOTE";
    case NFS3ERR_BADHANDLE:   return "NFS3ERR_BADHANDLE";
    case NFS3ERR_NOT_SYNC:    return "NFS3ERR_NOT_SYNC";
    case NFS3ERR_BAD_COOKIE:  return "NFS3ERR_BAD_COOKIE";
    case NFS3ERR_NOTSUPP:     return "NFS3ERR_NOTSUPP";
    case NFS3ERR_TOOSMALL:    return "NFS3ERR_TOOSMALL";
    case NFS3ERR_SERVERFAULT: return "NFS3ERR_SERVERFAULT";
    case NFS3ERR_BADTYPE:     return "NFS3ERR_BADTYPE";
    case NFS3ERR_JUKEBOX:     return "NFS3ERR_JUKEBOX";
    }
  return "UNKNOWN NFSv3 ERROR CODE";
}

char *nfsstat4_to_str(nfsstat4 code)
{
  switch(code)
    {
    case NFS4_OK:                     return "NFS4_OK";
    case NFS4ERR_PERM:                return "NFS4ERR_PERM";
    case NFS4ERR_NOENT:               return "NFS4ERR_NOENT";
    case NFS4ERR_IO:                  return "NFS4ERR_IO";
    case NFS4ERR_NXIO:                return "NFS4ERR_NXIO";
    case NFS4ERR_ACCESS:              return "NFS4ERR_ACCESS";
    case NFS4ERR_EXIST:               return "NFS4ERR_EXIST";
    case NFS4ERR_XDEV:                return "NFS4ERR_XDEV";
    case NFS4ERR_NOTDIR:              return "NFS4ERR_NOTDIR";
    case NFS4ERR_ISDIR:               return "NFS4ERR_ISDIR";
    case NFS4ERR_INVAL:               return "NFS4ERR_INVAL";
    case NFS4ERR_FBIG:                return "NFS4ERR_FBIG";
    case NFS4ERR_NOSPC:               return "NFS4ERR_NOSPC";
    case NFS4ERR_ROFS:                return "NFS4ERR_ROFS";
    case NFS4ERR_MLINK:               return "NFS4ERR_MLINK";
    case NFS4ERR_NAMETOOLONG:         return "NFS4ERR_NAMETOOLONG";
    case NFS4ERR_NOTEMPTY:            return "NFS4ERR_NOTEMPTY";
    case NFS4ERR_DQUOT:               return "NFS4ERR_DQUOT";
    case NFS4ERR_STALE:               return "NFS4ERR_STALE";
    case NFS4ERR_BADHANDLE:           return "NFS4ERR_BADHANDLE";
    case NFS4ERR_BAD_COOKIE:          return "NFS4ERR_BAD_COOKIE";
    case NFS4ERR_NOTSUPP:             return "NFS4ERR_NOTSUPP";
    case NFS4ERR_TOOSMALL:            return "NFS4ERR_TOOSMALL";
    case NFS4ERR_SERVERFAULT:         return "NFS4ERR_SERVERFAULT";
    case NFS4ERR_BADTYPE:             return "NFS4ERR_BADTYPE";
    case NFS4ERR_DELAY:               return "NFS4ERR_DELAY";
    case NFS4ERR_SAME:                return "NFS4ERR_SAME";
    case NFS4ERR_DENIED:              return "NFS4ERR_DENIED";
    case NFS4ERR_EXPIRED:             return "NFS4ERR_EXPIRED";
    case NFS4ERR_LOCKED:              return "NFS4ERR_LOCKED";
    case NFS4ERR_GRACE:               return "NFS4ERR_GRACE";
    case NFS4ERR_FHEXPIRED:           return "NFS4ERR_FHEXPIRED";
    case NFS4ERR_SHARE_DENIED:        return "NFS4ERR_SHARE_DENIED";
    case NFS4ERR_WRONGSEC:            return "NFS4ERR_WRONGSEC";
    case NFS4ERR_CLID_INUSE:          return "NFS4ERR_CLID_INUSE";
    case NFS4ERR_RESOURCE:            return "NFS4ERR_RESOURCE";
    case NFS4ERR_MOVED:               return "NFS4ERR_MOVED";
    case NFS4ERR_NOFILEHANDLE:        return "NFS4ERR_NOFILEHANDLE";
    case NFS4ERR_MINOR_VERS_MISMATCH: return "NFS4ERR_MINOR_VERS_MISMATCH";
    case NFS4ERR_STALE_CLIENTID:      return "NFS4ERR_STALE_CLIENTID";
    case NFS4ERR_STALE_STATEID:       return "NFS4ERR_STALE_STATEID";
    case NFS4ERR_OLD_STATEID:         return "NFS4ERR_OLD_STATEID";
    case NFS4ERR_BAD_STATEID:         return "NFS4ERR_BAD_STATEID";
    case NFS4ERR_BAD_SEQID:           return "NFS4ERR_BAD_SEQID";
    case NFS4ERR_NOT_SAME:            return "NFS4ERR_NOT_SAME";
    case NFS4ERR_LOCK_RANGE:          return "NFS4ERR_LOCK_RANGE";
    case NFS4ERR_SYMLINK:             return "NFS4ERR_SYMLINK";
    case NFS4ERR_RESTOREFH:           return "NFS4ERR_RESTOREFH";
    case NFS4ERR_LEASE_MOVED:         return "NFS4ERR_LEASE_MOVED";
    case NFS4ERR_ATTRNOTSUPP:         return "NFS4ERR_ATTRNOTSUPP";
    case NFS4ERR_NO_GRACE:            return "NFS4ERR_NO_GRACE";
    case NFS4ERR_RECLAIM_BAD:         return "NFS4ERR_RECLAIM_BAD";
    case NFS4ERR_RECLAIM_CONFLICT:    return "NFS4ERR_RECLAIM_CONFLICT";
    case NFS4ERR_BADXDR:              return "NFS4ERR_BADXDR";
    case NFS4ERR_LOCKS_HELD:          return "NFS4ERR_LOCKS_HELD";
    case NFS4ERR_OPENMODE:            return "NFS4ERR_OPENMODE";
    case NFS4ERR_BADOWNER:            return "NFS4ERR_BADOWNER";
    case NFS4ERR_BADCHAR:             return "NFS4ERR_BADCHAR";
    case NFS4ERR_BADNAME:             return "NFS4ERR_BADNAME";
    case NFS4ERR_BAD_RANGE:           return "NFS4ERR_BAD_RANGE";
    case NFS4ERR_LOCK_NOTSUPP:        return "NFS4ERR_LOCK_NOTSUPP";
    case NFS4ERR_OP_ILLEGAL:          return "NFS4ERR_OP_ILLEGAL";
    case NFS4ERR_DEADLOCK:            return "NFS4ERR_DEADLOCK";
    case NFS4ERR_FILE_OPEN:           return "NFS4ERR_FILE_OPEN";
    case NFS4ERR_ADMIN_REVOKED:       return "NFS4ERR_ADMIN_REVOKED";
    case NFS4ERR_CB_PATH_DOWN:        return "NFS4ERR_CB_PATH_DOWN";
#ifdef _USE_NFS4_1
    case NFS4ERR_BADIOMODE:                 return "NFS4ERR_BADIOMODE";
    case NFS4ERR_BADLAYOUT:                 return "NFS4ERR_BADLAYOUT";
    case NFS4ERR_BAD_SESSION_DIGEST:        return "NFS4ERR_BAD_SESSION_DIGEST";
    case NFS4ERR_BADSESSION:                return "NFS4ERR_BADSESSION";
    case NFS4ERR_BADSLOT:                   return "NFS4ERR_BADSLOT";
    case NFS4ERR_COMPLETE_ALREADY:          return "NFS4ERR_COMPLETE_ALREADY";
    case NFS4ERR_CONN_NOT_BOUND_TO_SESSION: return "NFS4ERR_CONN_NOT_BOUND_TO_SESSION";
    case NFS4ERR_DELEG_ALREADY_WANTED:      return "NFS4ERR_DELEG_ALREADY_WANTED";
    case NFS4ERR_BACK_CHAN_BUSY:            return "NFS4ERR_BACK_CHAN_BUSY";
    case NFS4ERR_LAYOUTTRYLATER:            return "NFS4ERR_LAYOUTTRYLATER";
    case NFS4ERR_LAYOUTUNAVAILABLE:         return "NFS4ERR_LAYOUTUNAVAILABLE";
    case NFS4ERR_NOMATCHING_LAYOUT:         return "NFS4ERR_NOMATCHING_LAYOUT";
    case NFS4ERR_RECALLCONFLICT:            return "NFS4ERR_RECALLCONFLICT";
    case NFS4ERR_UNKNOWN_LAYOUTTYPE:        return "NFS4ERR_UNKNOWN_LAYOUTTYPE";
    case NFS4ERR_SEQ_MISORDERED:            return "NFS4ERR_SEQ_MISORDERED";
    case NFS4ERR_SEQUENCE_POS:              return "NFS4ERR_SEQUENCE_POS";
    case NFS4ERR_REQ_TOO_BIG:               return "NFS4ERR_REQ_TOO_BIG";
    case NFS4ERR_REP_TOO_BIG:               return "NFS4ERR_REP_TOO_BIG";
    case NFS4ERR_REP_TOO_BIG_TO_CACHE:      return "NFS4ERR_REP_TOO_BIG_TO_CACHE";
    case NFS4ERR_RETRY_UNCACHED_REP:        return "NFS4ERR_RETRY_UNCACHED_REP";
    case NFS4ERR_UNSAFE_COMPOUND:           return "NFS4ERR_UNSAFE_COMPOUND";
    case NFS4ERR_TOO_MANY_OPS:              return "NFS4ERR_TOO_MANY_OPS";
    case NFS4ERR_OP_NOT_IN_SESSION:         return "NFS4ERR_OP_NOT_IN_SESSION";
    case NFS4ERR_HASH_ALG_UNSUPP:           return "NFS4ERR_HASH_ALG_UNSUPP";
    case NFS4ERR_CLIENTID_BUSY:             return "NFS4ERR_CLIENTID_BUSY";
    case NFS4ERR_PNFS_IO_HOLE:              return "NFS4ERR_PNFS_IO_HOLE";
    case NFS4ERR_SEQ_FALSE_RETRY:           return "NFS4ERR_SEQ_FALSE_RETRY";
    case NFS4ERR_BAD_HIGH_SLOT:             return "NFS4ERR_BAD_HIGH_SLOT";
    case NFS4ERR_DEADSESSION:               return "NFS4ERR_DEADSESSION";
    case NFS4ERR_ENCR_ALG_UNSUPP:           return "NFS4ERR_ENCR_ALG_UNSUPP";
    case NFS4ERR_PNFS_NO_LAYOUT:            return "NFS4ERR_PNFS_NO_LAYOUT";
    case NFS4ERR_NOT_ONLY_OP:               return "NFS4ERR_NOT_ONLY_OP";
    case NFS4ERR_WRONG_CRED:                return "NFS4ERR_WRONG_CRED";
    case NFS4ERR_WRONG_TYPE:                return "NFS4ERR_WRONG_TYPE";
    case NFS4ERR_DIRDELEG_UNAVAIL:          return "NFS4ERR_DIRDELEG_UNAVAIL";
    case NFS4ERR_REJECT_DELEG:              return "NFS4ERR_REJECT_DELEG";
    case NFS4ERR_RETURNCONFLICT:            return "NFS4ERR_RETURNCONFLICT";
    case NFS4ERR_DELEG_REVOKED:             return "NFS4ERR_DELEG_REVOKED";
#endif
    }
  return "UNKNOWN NFSv4 ERROR CODE";
}

char *nfstype2_to_str(ftype2 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NFNON:  return "NFNON";
    case NFREG:  return "NFREG";
    case NFDIR:  return "NFDIR";
    case NFBLK:  return "NFBLK";
    case NFCHR:  return "NFCHR";
    case NFLNK:  return "NFLNK";
    case NFSOCK: return "NFSOCK";
    case NFBAD:  return "NFBAD";
    case NFFIFO: return "NFFIFO";
    }
  return "UNKNOWN NFSv2 TYPE";
}

char *nfstype3_to_str(ftype3 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NF3REG:  return "NF3REG";
    case NF3DIR:  return "NF3DIR";
    case NF3BLK:  return "NF3BLK";
    case NF3CHR:  return "NF3CHR";
    case NF3LNK:  return "NF3LNK";
    case NF3SOCK: return "NF3SOCK";
    case NF3FIFO: return "NF3FIFO";
    }
  return "UNKNOWN NFSv3 TYPE";
}

/**
 *
 * nfs_htonl64: Same as htonl, but on 64 bits.
 *
 * @param arg64 [IN] input value
 *
 * @return converted value.
 *
 */
uint64_t nfs_htonl64(uint64_t arg64)
{
  uint64_t res64;

#ifdef LITTLEEND
  uint32_t low = (uint32_t) (arg64 & 0x00000000FFFFFFFFLL);
  uint32_t high = (uint32_t) ((arg64 & 0xFFFFFFFF00000000LL) >> 32);

  low = htonl(low);
  high = htonl(high);

  res64 = (uint64_t) high + (((uint64_t) low) << 32);
#else
  res64 = arg64;
#endif

  return res64;
}                               /* nfs_htonl64 */

/**
 *
 * nfs_ntohl64: Same as ntohl, but on 64 bits.
 *
 * @param arg64 [IN] input value
 *
 * @return converted value.
 *
 */
uint64_t nfs_ntohl64(uint64_t arg64)
{
  uint64_t res64;

#ifdef LITTLEEND
  uint32_t low = (uint32_t) (arg64 & 0x00000000FFFFFFFFLL);
  uint32_t high = (uint32_t) ((arg64 & 0xFFFFFFFF00000000LL) >> 32);

  low = ntohl(low);
  high = ntohl(high);

  res64 = (uint64_t) high + (((uint64_t) low) << 32);
#else
  res64 = arg64;
#endif

  return res64;
}                               /* nfs_ntonl64 */

/**
 *
 * auth_stat2str: converts a auth_stat enum to a string
 *
 * @param why       [IN]  the stat to convert
 * @param out       [IN]  output string
 *
 * @return nothing (void function).
 *
 */

void auth_stat2str(enum auth_stat why, char *str)
{
  switch (why)
    {
    case AUTH_OK:
      strncpy(str, "AUTH_OK", AUTH_STR_LEN);
      break;

    case AUTH_BADCRED:
      strncpy(str, "AUTH_BADCRED", AUTH_STR_LEN);
      break;

    case AUTH_REJECTEDCRED:
      strncpy(str, "AUTH_REJECTEDCRED", AUTH_STR_LEN);
      break;

    case AUTH_BADVERF:
      strncpy(str, "AUTH_BADVERF", AUTH_STR_LEN);
      break;

    case AUTH_REJECTEDVERF:
      strncpy(str, "AUTH_REJECTEDVERF", AUTH_STR_LEN);
      break;

    case AUTH_TOOWEAK:
      strncpy(str, "AUTH_TOOWEAK", AUTH_STR_LEN);
      break;

    case AUTH_INVALIDRESP:
      strncpy(str, "AUTH_INVALIDRESP", AUTH_STR_LEN);
      break;

    case AUTH_FAILED:
      strncpy(str, "AUTH_FAILED", AUTH_STR_LEN);
      break;

#ifdef _HAVE_GSSAPI
    case RPCSEC_GSS_CREDPROBLEM:
      strncpy(str, "RPCSEC_GSS_CREDPROBLEM", AUTH_STR_LEN);
      break;

    case RPCSEC_GSS_CTXPROBLEM:
      strncpy(str, "RPCSEC_GSS_CTXPROBLEM", AUTH_STR_LEN);
      break;
#endif

    default:
      strncpy(str, "UNKNOWN AUTH", AUTH_STR_LEN);
      break;
    }                           /* switch */
}                               /* auth_stat2str */
