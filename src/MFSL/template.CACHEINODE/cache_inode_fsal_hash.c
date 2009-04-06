/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2005)
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
 * \file    cache_inode_fsal_hash.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:39:23 $
 * \version $Revision: 1.9 $
 * \brief   Glue functions between the FSAL and the Cache inode layers.
 *
 * cache_inode_fsal_glue.c : Glue functions between the FSAL and the Cache inode layers.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "log_functions.h"
#include "err_fsal.h"
#include "err_cache_inode.h"
#include "stuff_alloc.h"
#include <unistd.h> /* for using gethostname */
#include <stdlib.h> /* for using exit */
#include <strings.h>
#include <sys/types.h>

/**
 *
 * cache_inode_fsal_hash_func: Compute the hash value for the cache_inode hash table.
 *
 * Computes the hash value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL. 
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed hash value.
 *
 */
unsigned long cache_inode_fsal_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  unsigned long h = 0 ;
#ifdef _DEBUG_HASHTABLE
  char printbuf[512];
#endif
  cache_inode_fsal_data_t * pfsdata = (cache_inode_fsal_data_t *)(buffclef->pdata) ;

  h = FSAL_Handle_to_HashIndex( &pfsdata->handle, pfsdata->cookie, p_hparam->alphabet_length, p_hparam->index_size );
    
#ifdef _DEBUG_HASHTABLE
  snprintHandle( printbuf, 512, &pfsdata->handle );
  printf( "hash_func key: buff =(Handle=%s, Cookie=%u), hash value=%lu\n", printbuf, pfsdata->cookie, h ) ;
#endif

  return h;
} /* cache_inode_fsal_hash_func */

/**
 *
 * cache_inode_fsal_rbt_func: Compute the rbt value for the cache_inode hash table.
 *
 * Computes the rbt value for the cache_inode hash table. This function is specific
 * to use with HPSS/FSAL. 
 *
 * @param hparam [IN] hash table parameter.
 * @param buffclef [IN] key to be used for computing the hash value.
 * @return the computed rbt value.
 *
 */
unsigned long cache_inode_fsal_rbt_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef ) 
{
  /* A polynomial function too, but reversed, to avoid producing same value as decimal_simple_hash_func */
  unsigned long h = 0 ;
#ifdef _DEBUG_HASHTABLE
  char printbuf[512];
#endif

  cache_inode_fsal_data_t * pfsdata = (cache_inode_fsal_data_t *)(buffclef->pdata) ;
    
  h = FSAL_Handle_to_RBTIndex( &pfsdata->handle, pfsdata->cookie );

#ifdef _DEBUG_HASHTABLE
  snprintHandle( printbuf, 512, &pfsdata->handle );
  printf( "hash_func rbt: buff =(Handle=%s, Cookie=%u), value=%lu\n", printbuf, pfsdata->cookie, h ) ;
#endif
  return h;
} /* cache_inode_fsal_rbt_func */




int display_key( hash_buffer_t * pbuff, char * str )
{
  cache_inode_fsal_data_t * pfsdata ;
  char buffer[128];
  
  pfsdata = (cache_inode_fsal_data_t *)pbuff->pdata ;
  
  snprintHandle( buffer, 128, &(pfsdata->handle));

  return snprintf( str, HASHTABLE_DISPLAY_STRLEN, "(Handle=%s, Cookie=%u)", buffer, pfsdata->cookie ) ;
}

int display_not_implemented( hash_buffer_t * pbuff, char * str )
{
  
  return snprintf( str, HASHTABLE_DISPLAY_STRLEN, "Print Not Implemented" ) ;
}

int display_value( hash_buffer_t * pbuff, char * str )
{
  cache_entry_t * pentry ;

  pentry = (cache_entry_t *)pbuff->pdata ;
  
  return snprintf( str, HASHTABLE_DISPLAY_STRLEN, "(Type=%d, Address=%p)", pentry->internal_md.type, pentry ) ;
}



