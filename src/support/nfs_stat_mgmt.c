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
 */

/**
 * \file    nfs_stat_mgmt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:03 $
 * \version $Revision: 1.4 $
 * \brief   routines for managing the nfs statistics.
 *
 * nfs_stat_mgmt.c : routines for managing the nfs statistics.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_stat_mgmt.c,v 1.4 2005/11/28 17:03:03 deniel Exp $
 *
 * $Log: nfs_stat_mgmt.c,v $
 * Revision 1.4  2005/11/28 17:03:03  deniel
 * Added CeCILL headers
 *
 * Revision 1.3  2005/09/30 15:50:19  deniel
 * Support for mount and nfs protocol different from the default
 *
 * Revision 1.2  2005/09/30 14:27:34  deniel
 * Adding some configurationsa items in nfs_core
 *
 * Revision 1.1  2005/08/11 12:37:28  deniel
 * Added statistics management
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>  /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
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
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include "nfs_stat.h"

extern nfs_parameter_t     nfs_param ;


/**
 *
 * nfs_stat_update: Update a client's statistics.
 *
 * Update a client's statistics.
 *
 * @param type    [IN]    type of the stat to dump
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 *
 * @return nothing (void function)
 *
 */
void nfs_stat_update( nfs_stat_type_t        type,
                      nfs_request_stat_t   * pstat_req,
                      struct svc_req       * preq ) 
{
  nfs_request_stat_item_t * pitem = NULL ;

  if( preq->rq_prog == nfs_param.core_param.nfs_program )
    {
      switch( preq->rq_vers )
        {
        case NFS_V2:
          pitem = &pstat_req->stat_req_nfs2[preq->rq_proc] ;
          pstat_req->nb_nfs2_req += 1 ;
          break ;
          
        case NFS_V3:
          pitem = &pstat_req->stat_req_nfs3[preq->rq_proc] ;
          pstat_req->nb_nfs3_req += 1 ;
          break ;
          
        case NFS_V4:
          pitem = &pstat_req->stat_req_nfs4[preq->rq_proc] ;
          pstat_req->nb_nfs4_req += 1 ;
          break ;

        default:
          /* Bad vers ? */
          DisplayLog( "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %", __FILE__, __LINE__ ) ;
          return ;
          break ;
        }
    }
  else if( preq->rq_prog == nfs_param.core_param.mnt_program )
    {
      switch( preq->rq_vers )
        {
        case MOUNT_V1:
          pitem = &pstat_req->stat_req_mnt1[preq->rq_proc] ;
          pstat_req->nb_mnt1_req += 1 ;
          break ;
          
        case MOUNT_V3:
          pitem = &pstat_req->stat_req_mnt3[preq->rq_proc] ;
          pstat_req->nb_mnt3_req += 1 ;
          break ;

        default:
          /* Bad vers ? */
          DisplayLog( "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %", __FILE__, __LINE__ ) ;
          return ;
          break ;
        }
    }
  else
    {
      /* Bad program ? */
      DisplayLog( "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %", __FILE__, __LINE__ ) ;
      return ;
    }

  pitem->total += 1 ;
  
  switch( type ) 
    {
    case GANESHA_STAT_SUCCESS:
      pitem->success += 1 ;
      break ;
      
    case GANESHA_STAT_DROP:
      pitem->dropped += 1 ;
      break ;

    default:
      /* Bad type ? */
      DisplayLog( "IMPLEMENTATION ERROR: /!\\ | you should never step here file %s, line %", __FILE__, __LINE__ ) ;
      break ;
    }
  
  return ;
  
} /* nfs_stat_update */
