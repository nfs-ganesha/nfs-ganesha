/*
 *
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
 * \file    err_ghost_fs.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.15 $
 * \brief   Ghost Filesystem error codes.
 *
 *
 */
 
#ifndef _ERR_GHOSTFS_H
#define _ERR_GHOSTFS_H

#include <log_functions.h>

static family_error_t __attribute(( __unused__ ))  tab_errstatus_GHOSTFS[] =
{
#define ERR_GHOSTFS_NO_ERROR 0
  {ERR_GHOSTFS_NO_ERROR, "ERR_GHOSTFS_NO_ERROR", "No error"}, 

#define ERR_GHOSTFS_NOENT    2
  {ERR_GHOSTFS_NOENT, "ERR_GHOSTFS_NOENT", "No such file or directory"},
#define ERR_GHOSTFS_NOTDIR   3
  {ERR_GHOSTFS_NOTDIR, "ERR_GHOSTFS_NOTDIR", "Not a directory"},
      
#define ERR_GHOSTFS_ACCES   13
  {ERR_GHOSTFS_ACCES, "ERR_GHOSTFS_ACCES", "Permission denied"},

#define ERR_GHOSTFS_EXIST   17
  {ERR_GHOSTFS_EXIST, "ERR_GHOSTFS_EXIST", "Entry already exist"},

#define ERR_GHOSTFS_ISDIR   21
  {ERR_GHOSTFS_ISDIR, "ERR_GHOSTFS_ISDIR", "Directory used in a non-directory operation"},

#define ERR_GHOSTFS_NOTEMPTY   23
  {ERR_GHOSTFS_NOTEMPTY, "ERR_GHOSTFS_NOTEMPTY", "Directory is not empty"},
      
#define ERR_GHOSTFS_INTERNAL 1001
  {ERR_GHOSTFS_INTERNAL, "ERR_GHOSTFS_INTERNAL", "GhostFS internal error"},

#define ERR_GHOSTFS_MALLOC   1002
  {ERR_GHOSTFS_MALLOC,   "ERR_GHOSTFS_MALLOC", "Memory allocation error"},

#define ERR_GHOSTFS_OPEN     1003
  {ERR_GHOSTFS_OPEN,     "ERR_GHOSTFS_OPEN", "Error opening fislesystem definition file"},
#define ERR_GHOSTFS_READ     1004
  {ERR_GHOSTFS_READ,     "ERR_GHOSTFS_READ", "Error while reading filesytem definition file"},
#define ERR_GHOSTFS_WRITE    1005
  {ERR_GHOSTFS_WRITE,    "ERR_GHOSTFS_WRITE", "Error while dumping filesytem definition file"},

#define ERR_GHOSTFS_SYNTAX   1006
  {ERR_GHOSTFS_SYNTAX,   "ERR_GHOSTFS_SYNTAX", "Syntax error into filesytem definition file"},

#define ERR_GHOSTFS_ARGS     1007
  {ERR_GHOSTFS_ARGS,     "ERR_GHOSTFS_ARGS", "Invalid argument"},

#define ERR_GHOSTFS_ALREADYINIT     1008
  {ERR_GHOSTFS_ALREADYINIT,     "ERR_GHOSTFS_ALREADYINIT", "The filesystem has already been loaded"},

#define ERR_GHOSTFS_NOTINIT     1009
  {ERR_GHOSTFS_NOTINIT,     "ERR_GHOSTFS_NOTINIT", "No filesystem has been loaded"},

#define ERR_GHOSTFS_STALE     1010
  {ERR_GHOSTFS_STALE,     "ERR_GHOSTFS_STALE", "Invalid file handle"},

#define ERR_GHOSTFS_CORRUPT    1011
  {ERR_GHOSTFS_CORRUPT,     "ERR_GHOSTFS_CORRUPT", "The filesystem seems to be corrupted"},

#define ERR_GHOSTFS_NOTLNK   1012
  {ERR_GHOSTFS_NOTLNK, "ERR_GHOSTFS_NOTLNK", "Not a link"},

#define ERR_GHOSTFS_TOOSMALL   1013
  {ERR_GHOSTFS_TOOSMALL, "ERR_GHOSTFS_TOOSMALL", "Buffer too small"},

#define ERR_GHOSTFS_NOTOPENED   1014
  {ERR_GHOSTFS_NOTOPENED, "ERR_GHOSTFS_NOTOPENED", "Directory is not opened"},

#define ERR_GHOSTFS_ENDOFDIR   1015
  {ERR_GHOSTFS_ENDOFDIR, "ERR_GHOSTFS_ENDOFDIR", "End of directory"},

#define ERR_GHOSTFS_ATTR_NOT_SUPP   1016
  {ERR_GHOSTFS_ATTR_NOT_SUPP, "ERR_GHOSTFS_ATTR_NOT_SUPP", "Unsupported or read-only attribute"},

  {ERR_NULL, "ERR_NULL", ""}
};

#endif /*_ERR_GHOSTFS_H*/
