#ifndef _ERR_CACHE_CONTENT_H
#define _ERR_CACHE_CONTENT_H

#include "log_functions.h"
#include "cache_content.h"

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
 * \file    err_cache_content.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.2 $
 * \brief   Cache content error definitions.
 * 
 * err_cache_content.h : Cache content error definitions.
 *
 *
 */
static  family_error_t __attribute__(( __unused__ )) tab_errctx_cache_content[] =
{
#define ERR_CACHE_CONTENT_NEW_ENTRY     CACHE_CONTENT_NEW_ENTRY
  {ERR_CACHE_CONTENT_NEW_ENTRY, "ERR_CACHE_CONTENT_NEW_ENTRY", "Impossible to create a new entry"},
#define ERR_CACHE_CONTENT_RELEASE_ENTRY CACHE_CONTENT_RELEASE_ENTRY 
  {ERR_CACHE_CONTENT_RELEASE_ENTRY, "ERR_CACHE_CONTENT_RELEASE_ENTRY", "Impossible to release an entry"},
#define ERR_CACHE_CONTENT_READ_ENTRY    CACHE_CONTENT_READ_ENTRY 
  {ERR_CACHE_CONTENT_READ_ENTRY, "ERR_CACHE_CONTENT_READ_ENTRY", "Entry could not be read"},
#define ERR_CACHE_CONTENT_WRITE_ENTRY   CACHE_CONTENT_WRITE_ENTRY
  {ERR_CACHE_CONTENT_WRITE_ENTRY, "ERR_CACHE_CONTENT_WRITE_ENTRY", "Entry could not be written"}, 
#define ERR_CACHE_CONTENT_TRUNCATE      CACHE_CONTENT_TRUNCATE
  {ERR_CACHE_CONTENT_TRUNCATE, "ERR_CACHE_CONTENT_TRUNCATE", "Entry could not be truncated"},
#define ERR_CACHE_CONTENT_FLUSH         CACHE_CONTENT_FLUSH 
  {ERR_CACHE_CONTENT_FLUSH, "ERR_CACHE_CONTENT_FLUSH", "Entry could not be flush to FSAL"},
#define ERR_CACHE_CONTENT_REFRESH       CACHE_CONTENT_REFRESH
  {ERR_CACHE_CONTENT_REFRESH, "ERR_CACHE_CONTENT_REFRESH", "Entry could not be updated from FSAL"},

  {ERR_NULL, "ERR_NULL", ""}
};


static  family_error_t __attribute(( __unused__ )) tab_errstatus_cache_content[] =
{
#define ERR_CACHE_CONTENT_SUCCESS   CACHE_CONTENT_SUCCESS
#define ERR_CACHE_CONTENT_NO_ERROR  ERR_CACHE_CONTENT_SUCCESS
  {ERR_CACHE_CONTENT_NO_ERROR, "ERR_CACHE_CONTENT_NO_ERROR", "No error"},
#define ERR_CACHE_CONTENT_INVALID_ARGUMENT  CACHE_CONTENT_INVALID_ARGUMENT
  {ERR_CACHE_CONTENT_INVALID_ARGUMENT, "ERR_CACHE_CONTENT_INVALID_ARGUMENT", "Invalid argument"},
#define ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY  CACHE_CONTENT_UNAPPROPRIATED_KEY  
  {ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY, "ERR_CACHE_CONTENT_UNAPPROPRIATED_KEY", "Bad key"},
#define ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY CACHE_CONTENT_BAD_CACHE_INODE_ENTRY
  {ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY, "ERR_CACHE_CONTENT_BAD_CACHE_INODE_ENTRY", "Bad cache inode entry"}, 
#define ERR_CACHE_CONTENT_ENTRY_EXISTS CACHE_CONTENT_ENTRY_EXISTS
  {ERR_CACHE_CONTENT_ENTRY_EXISTS, "ERR_CACHE_CONTENT_ENTRY_EXISTS", "Entry already exists"},
#define ERR_CACHE_CONTENT_FSAL_ERROR     CACHE_CONTENT_FSAL_ERROR    
  {ERR_CACHE_CONTENT_FSAL_ERROR, "ERR_CACHE_CONTENT_FSAL_ERROR", "Unexpected FSAL error"},
#define ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR  CACHE_CONTENT_LOCAL_CACHE_ERROR
  {ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR, "ERR_CACHE_CONTENT_LOCAL_CACHE_ERROR", "Unexpected local cache error"},
#define ERR_CACHE_CONTENT_MALLOC_ERROR     CACHE_CONTENT_MALLOC_ERROR      
  {ERR_CACHE_CONTENT_MALLOC_ERROR, "ERR_CACHE_CONTENT_MALLOC_ERROR", "resource allocation error"},
#define ERR_CACHE_CONTENT_LRU_ERROR   CACHE_CONTENT_LRU_ERROR         
  {ERR_CACHE_CONTENT_LRU_ERROR, "ERR_CACHE_CONTENT_LRU_ERROR", "Unexpected LRU error"},

  {ERR_NULL, "ERR_NULL", ""}
};


#endif /* _ERR_CACHE_CONTENT_H */
