#ifndef _ERR_HASHTABLE_H
#define _ERR_HASHTABLE_H

#include "log_functions.h"
#include "HashTable.h"

/**
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

 /**/
/**
 * \file    err_HashTable.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.20 $
 * \brief   Definition des erreur des tables de hachage.
 * 
 * err_HashTable.h : Definition des erreur des tables de hachage.
 *
 *
 */
    static family_error_t __attribute__ ((__unused__)) tab_errctx_hash[] =
{
#define ERR_HASHTABLE_NO_ERROR       0
  {
  ERR_HASHTABLE_NO_ERROR, "ERR_HASHTABLE_NO_ERROR", "Success"},
#define ERR_HASHTABLE_GET            1
  {
  ERR_HASHTABLE_GET, "ERR_HASHTABLE_GET", "Error when getting an entry"},
#define ERR_HASHTABLE_SET            2
  {
  ERR_HASHTABLE_SET, "ERR_HASHTABLE_SET", "Error when setting an entry"},
#define ERR_HASHTABLE_DEL            3
  {
  ERR_HASHTABLE_DEL, "ERR_HASHTABLE_DEL", "Error while deleting an entry"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

static family_error_t __attribute__ ((__unused__)) tab_errstatus_hash[] =
{
  {
  HASHTABLE_SUCCESS, "HASHTABLE_SUCCESS", "Success"},
  {
  HASHTABLE_UNKNOWN_HASH_TYPE, "HASHTABLE_UNKNOWN_HASH_TYPE", "Unknown hash type"},
  {
  HASHTABLE_INSERT_MALLOC_ERROR, "HASHTABLE_INSERT_MALLOC_ERROR",
        "Malloc error at insert time"},
  {
  HASHTABLE_ERROR_NO_SUCH_KEY, "HASHTABLE_ERROR_NO_SUCH_KEY", "No such key"},
  {
  HASHTABLE_ERROR_KEY_ALREADY_EXISTS, "HASHTABLE_ERROR_KEY_ALREADY_EXISTS",
        "Entry of that key already exists"},
  {
  HASHTABLE_ERROR_INVALID_ARGUMENT, "HASHTABLE_ERROR_INVALID_ARGUMENT",
        "Invalid argument"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif                          /* _ERR_HASHTABLE_H */
