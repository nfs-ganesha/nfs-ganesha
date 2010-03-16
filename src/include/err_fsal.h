/*
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
 */

/**
 * \file    err_fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:22:57 $
 * \version $Revision: 1.30 $
 * \brief   FSAL error codes.
 *
 *
 */

#ifndef _ERR_FSAL_H
#define _ERR_FSAL_H

#include <log_functions.h>

static family_error_t __attribute__ ((__unused__)) tab_errstatus_FSAL[] =
{

#define ERR_FSAL_NO_ERROR 0
  {
  ERR_FSAL_NO_ERROR, "ERR_FSAL_NO_ERROR", "No error"},
#define ERR_FSAL_PERM     1
  {
  ERR_FSAL_PERM, "ERR_FSAL_PERM", "Forbidden action"},
#define ERR_FSAL_NOENT    2
  {
  ERR_FSAL_NOENT, "ERR_FSAL_NOENT", "No such file or directory"},
#define ERR_FSAL_IO       5
  {
  ERR_FSAL_IO, "ERR_FSAL_IO", "I/O error"},
#define ERR_FSAL_NXIO     6
  {
  ERR_FSAL_NXIO, "ERR_FSAL_NXIO", "No such device or address"},
#define ERR_FSAL_NOMEM    12
  {
  ERR_FSAL_NOMEM, "ERR_FSAL_NOMEM", "Not enough memory"},
#define ERR_FSAL_ACCESS   13
  {
  ERR_FSAL_ACCESS, "ERR_FSAL_ACCESS", "Permission denied"},
#define ERR_FSAL_FAULT   14
  {
  ERR_FSAL_FAULT, "ERR_FSAL_FAULT", "Bad address"},
#define ERR_FSAL_EXIST    17
  {
  ERR_FSAL_EXIST, "ERR_FSAL_EXIST", "This object already exists"},
#define ERR_FSAL_XDEV     18
  {
  ERR_FSAL_XDEV, "ERR_FSAL_XDEV", "This operation can't cross filesystems"},
#define ERR_FSAL_NOTDIR   20
  {
  ERR_FSAL_NOTDIR, "ERR_FSAL_NOTDIR", "This object is not a directory"},
#define ERR_FSAL_ISDIR   21
  {
  ERR_FSAL_ISDIR, "ERR_FSAL_ISDIR", "Directory used in a nondirectory operation"},
#define ERR_FSAL_INVAL   22
  {
  ERR_FSAL_INVAL, "ERR_FSAL_INVAL", "Invalid object type"},
#define ERR_FSAL_FBIG    27
  {
  ERR_FSAL_FBIG, "ERR_FSAL_FBIG", "File exceeds max file size"},
#define ERR_FSAL_NOSPC           28
  {
  ERR_FSAL_NOSPC, "ERR_FSAL_NOSPC", "No space left on filesystem"},
#define ERR_FSAL_ROFS            30
  {
  ERR_FSAL_ROFS, "ERR_FSAL_ROFS", "Read-only filesystem"},
#define ERR_FSAL_MLINK           31
  {
  ERR_FSAL_MLINK, "ERR_FSAL_MLINK", "Too many hard links"},
#define ERR_FSAL_DQUOT           49
  {
  ERR_FSAL_DQUOT, "ERR_FSAL_DQUOT", "Quota exceeded"},
#define ERR_FSAL_NAMETOOLONG     78
  {
  ERR_FSAL_NAMETOOLONG, "ERR_FSAL_NAMETOOLONG", "Max name length exceeded"},
#define ERR_FSAL_NOTEMPTY        93
  {
  ERR_FSAL_NOTEMPTY, "ERR_FSAL_NOTEMPTY", "The directory is not empty"},
#define ERR_FSAL_STALE           151
  {
  ERR_FSAL_STALE, "ERR_FSAL_STALE", "The file no longer exists"},
#define ERR_FSAL_BADHANDLE       10001
  {
  ERR_FSAL_BADHANDLE, "ERR_FSAL_BADHANDLE", "Illegal filehandle"},
#define ERR_FSAL_BADCOOKIE       10003
  {
  ERR_FSAL_BADCOOKIE, "ERR_FSAL_BADCOOKIE", "Invalid cookie"},
#define ERR_FSAL_NOTSUPP         10004
  {
  ERR_FSAL_NOTSUPP, "ERR_FSAL_NOTSUPP", "Operation not supported"},
#define ERR_FSAL_TOOSMALL         10005
  {
  ERR_FSAL_TOOSMALL, "ERR_FSAL_TOOSMALL", "Output buffer too small"},
#define ERR_FSAL_SERVERFAULT      10006
  {
  ERR_FSAL_SERVERFAULT, "ERR_FSAL_SERVERFAULT", "Undefined server error"},
#define ERR_FSAL_BADTYPE          10007
  {
  ERR_FSAL_BADTYPE, "ERR_FSAL_BADTYPE", "Invalid type for create operation"},
#define ERR_FSAL_DELAY            10008
  {
  ERR_FSAL_DELAY, "ERR_FSAL_DELAY", "File busy, retry"},
#define ERR_FSAL_FHEXPIRED        10014
  {
  ERR_FSAL_FHEXPIRED, "ERR_FSAL_FHEXPIRED", "Filehandle expired"},
#define ERR_FSAL_SYMLINK          10029
  {
  ERR_FSAL_SYMLINK, "ERR_FSAL_SYMLINK",
	"This is a symbolic link, should be file/directory"},
#define ERR_FSAL_ATTRNOTSUPP      10032
  {
  ERR_FSAL_ATTRNOTSUPP, "ERR_FSAL_ATTRNOTSUPP", "Attribute not supported"},
#define ERR_FSAL_NOT_INIT       20001
  {
  ERR_FSAL_NOT_INIT, "ERR_FSAL_NOT_INIT", "Filesystem not initialized"},
#define ERR_FSAL_ALREADY_INIT   20002
  {
  ERR_FSAL_ALREADY_INIT, "ERR_FSAL_ALREADY_INIT", "Filesystem already initialised"},
#define ERR_FSAL_BAD_INIT       20003
  {
  ERR_FSAL_BAD_INIT, "ERR_FSAL_BAD_INIT", "Filesystem initialisation error"},
/* security context errors */
#define ERR_FSAL_SEC       20004
  {
  ERR_FSAL_SEC, "ERR_FSAL_SEC", "Security context error"},
#define ERR_FSAL_NOT_OPENED     20010
  {
  ERR_FSAL_NOT_OPENED, "ERR_FSAL_NOT_OPENED", "File/directory not opened"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif /*_ERR_FSAL_H*/
