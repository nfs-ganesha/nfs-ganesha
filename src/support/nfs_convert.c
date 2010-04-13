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
 * \file    nfs_convert.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:02 $
 * \version $Revision: 1.3 $
 * \brief   nfs conversion tools.
 *
 * $Log: nfs_convert.c,v $
 * Revision 1.3  2005/11/28 17:03:02  deniel
 * Added CeCILL headers
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
#ifdef _USE_GSSRPC
#include <gssapi/gssapi.h>
#ifdef HAVE_KRB5
#include <gssapi/gssapi_krb5.h>
#endif
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
    case NFS_OK:
      return "NFS_OK";
    case NFSERR_PERM:
      return "NFSERR_PERM";
    case NFSERR_NOENT:
      return "NFSERR_NOENT";
    case NFSERR_IO:
      return "NFSERR_IO";
    case NFSERR_NXIO:
      return "NFSERR_NXIO";
    case NFSERR_ACCES:
      return "NFSERR_ACCES";
    case NFSERR_EXIST:
      return "NFSERR_EXIST";
    case NFSERR_NODEV:
      return "NFSERR_NODEV";
    case NFSERR_NOTDIR:
      return "NFSERR_NOTDIR";
    case NFSERR_ISDIR:
      return "NFSERR_ISDIR";
    case NFSERR_FBIG:
      return "NFSERR_FBIG";
    case NFSERR_NOSPC:
      return "NFSERR_NOSPC";
    case NFSERR_ROFS:
      return "NFSERR_ROFS";
    case NFSERR_NAMETOOLONG:
      return "NFSERR_NAMETOOLONG";
    case NFSERR_NOTEMPTY:
      return "NFSERR_NOTEMPTY";
    case NFSERR_DQUOT:
      return "NFSERR_DQUOT";
    case NFSERR_STALE:
      return "NFSERR_STALE";
    case NFSERR_WFLUSH:
      return "NFSERR_WFLUSH";

    default:
      return "/!\\ | UNKNOWN NFSv2 ERROR CODE";
    }
}

char *nfsstat3_to_str(nfsstat3 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NFS3_OK:
      return "NFS3_OK";
    case NFS3ERR_PERM:
      return "NFS3ERR_PERM";
    case NFS3ERR_NOENT:
      return "NFS3ERR_NOENT";
    case NFS3ERR_IO:
      return "NFS3ERR_IO";
    case NFS3ERR_NXIO:
      return "NFS3ERR_NXIO";
    case NFS3ERR_ACCES:
      return "NFS3ERR_ACCES";
    case NFS3ERR_EXIST:
      return "NFS3ERR_EXIST";
    case NFS3ERR_XDEV:
      return "NFS3ERR_XDEV";
    case NFS3ERR_NODEV:
      return "NFS3ERR_NODEV";
    case NFS3ERR_NOTDIR:
      return "NFS3ERR_NOTDIR";
    case NFS3ERR_ISDIR:
      return "NFS3ERR_ISDIR";
    case NFS3ERR_INVAL:
      return "NFS3ERR_INVAL";
    case NFS3ERR_FBIG:
      return "NFS3ERR_FBIG";
    case NFS3ERR_NOSPC:
      return "NFS3ERR_NOSPC";
    case NFS3ERR_ROFS:
      return "NFS3ERR_ROFS";
    case NFS3ERR_MLINK:
      return "NFS3ERR_MLINK";
    case NFS3ERR_NAMETOOLONG:
      return "NFS3ERR_NAMETOOLONG";
    case NFS3ERR_NOTEMPTY:
      return "NFS3ERR_NOTEMPTY";
    case NFS3ERR_DQUOT:
      return "NFS3ERR_DQUOT";
    case NFS3ERR_STALE:
      return "NFS3ERR_STALE";
    case NFS3ERR_REMOTE:
      return "NFS3ERR_REMOTE";
    case NFS3ERR_BADHANDLE:
      return "NFS3ERR_BADHANDLE";
    case NFS3ERR_NOT_SYNC:
      return "NFS3ERR_NOT_SYNC";
    case NFS3ERR_BAD_COOKIE:
      return "NFS3ERR_BAD_COOKIE";
    case NFS3ERR_NOTSUPP:
      return "NFS3ERR_NOTSUPP";
    case NFS3ERR_TOOSMALL:
      return "NFS3ERR_TOOSMALL";
    case NFS3ERR_SERVERFAULT:
      return "NFS3ERR_SERVERFAULT";
    case NFS3ERR_BADTYPE:
      return "NFS3ERR_BADTYPE";
    case NFS3ERR_JUKEBOX:
      return "NFS3ERR_JUKEBOX";

    default:
      return "/!\\ | UNKNOWN NFSv3 ERROR CODE";
    }
}

char *nfstype2_to_str(ftype2 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NFNON:
      return "NFNON";
    case NFREG:
      return "NFREG";
    case NFDIR:
      return "NFDIR";
    case NFBLK:
      return "NFBLK";
    case NFCHR:
      return "NFCHR";
    case NFLNK:
      return "NFLNK";
    case NFSOCK:
      return "NFSOCK";
    case NFBAD:
      return "NFBAD";
    case NFFIFO:
      return "NFFIFO";

    default:
      return "/!\\ | UNKNOWN NFSv2 TYPE";
    }
}

char *nfstype3_to_str(ftype3 code)
{
  switch (code)
    {
      /* no nead for break statments,
       * because we "return".
       */
    case NF3REG:
      return "NF3REG";
    case NF3DIR:
      return "NF3DIR";
    case NF3BLK:
      return "NF3BLK";
    case NF3CHR:
      return "NF3CHR";
    case NF3LNK:
      return "NF3LNK";
    case NF3SOCK:
      return "NF3SOCK";
    case NF3FIFO:
      return "NF3FIFO";

    default:
      return "/!\\ | UNKNOWN NFSv3 TYPE";
    }
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

#ifdef _HAVE_GSSAPI
/**
 *
 * sperror_gss: converts GSSAPI status to a string.
 * 
 * @param outmsg    [OUT] output string 
 * @param tag       [IN]  input tag
 * @param maj_stat  [IN]  GSSAPI major status
 * @param min_stat  [IN]  GSSAPI minor status
 *
 * @return TRUE is successfull, false otherwise.
 * 
 */
int log_sperror_gss(char *outmsg, char *tag, OM_uint32 maj_stat, OM_uint32 min_stat)
{
  OM_uint32 smin;
  gss_buffer_desc msg;
  gss_buffer_desc msg2;
  int msg_ctx = 0;
  FILE *tmplog;

  if(gss_display_status(&smin,
                        maj_stat,
                        GSS_C_GSS_CODE, GSS_C_NULL_OID, &msg_ctx, &msg) != GSS_S_COMPLETE)
    return FALSE;

  if(gss_display_status(&smin,
                        min_stat,
                        GSS_C_MECH_CODE,
                        GSS_C_NULL_OID, &msg_ctx, &msg2) != GSS_S_COMPLETE)
    return FALSE;

  sprintf(outmsg, "%s - %s : %s ", tag, (char *)msg.value, (char *)msg2.value);

  gss_release_buffer(&smin, &msg);
  gss_release_buffer(&smin, &msg2);

  return TRUE;
}                               /* log_sperror_gss */
#endif

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

#ifdef _USE_GSSRPC
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
