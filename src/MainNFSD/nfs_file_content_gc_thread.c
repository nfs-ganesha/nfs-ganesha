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
 * \file    nfs_file_content_gc_thread.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   The file that contain the 'file_content_gc_thread' routine for the nfsd.
 *
 * nfs_file_content_gc_thread.c : The file that contain the 'admin_thread' routine for the nfsd.
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
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
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
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"

/* Structures from another module */
extern nfs_parameter_t nfs_param;
extern nfs_worker_data_t *workers_data;
extern cache_content_client_t recover_datacache_client;

extern char ganesha_exec_path[MAXPATHLEN];
extern char config_path[MAXPATHLEN];
extern char fcc_log_path[MAXPATHLEN];
extern int fcc_debug_level;

/* Use the same structure as the worker (but not all the fields will be used) */
nfs_worker_data_t fcc_gc_data;
static fsal_op_context_t fsal_context;

/* Variable used for forcing flush via a signal */
unsigned int force_flush_by_signal;

int ___cache_content_invalidate_flushed(LRU_entry_t * plru_entry, void *addparam)
{
  cache_content_entry_t *pentry = NULL;
  cache_content_client_t *pclient = NULL;

  pentry = (cache_content_entry_t *) plru_entry->buffdata.pdata;
  pclient = (cache_content_client_t *) addparam;

  if(pentry->local_fs_entry.sync_state != SYNC_OK)
    {
      /* Entry is not to be set invalid */
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* Clean up and set the entry invalid */
  P_w(&pentry->pentry_inode->lock);
  pentry->pentry_inode->object.file.pentry_content = NULL;
  V_w(&pentry->pentry_inode->lock);

  /* Release the entry */
  RELEASE_PREALLOC(pentry, pclient->pool_entry, next_alloc);

  /* Retour de la pentry dans le pool */
  return LRU_LIST_SET_INVALID;
}                               /* cache_content_invalidate_flushed */

int file_content_gc_manage_entry(LRU_entry_t * plru_entry, void *addparam)
{
  cache_content_entry_t *pentry = NULL;
  cache_content_status_t cache_content_status;
  exportlist_t *pexport = NULL;

  pentry = (cache_content_entry_t *) plru_entry->buffdata.pdata;
  pexport = (exportlist_t *) addparam;

  if(pentry->local_fs_entry.sync_state == SYNC_OK)
    {
      /* No flush needed */
      return TRUE;
    }
  if((cache_content_status = cache_content_flush(pentry,
                                                 CACHE_CONTENT_FLUSH_AND_DELETE,
                                                 &fcc_gc_data.cache_content_client,
                                                 &fsal_context,
                                                 &cache_content_status)) !=
     CACHE_CONTENT_SUCCESS)
    {
      DisplayLog("NFS FILE CONTENT GARBAGE COLLECTION : /!\\ Can't flush %s : error %d",
                 pentry->local_fs_entry.cache_path_data, cache_content_status);
    }

  /* Continue with next entry in LRU_Apply_function */
  return TRUE;
}                               /* file_content_gc_manage_entry */

void *file_content_gc_thread(void *IndexArg)
{
  long index = (long)IndexArg;
  char command[2 * MAXPATHLEN];
  char *debuglevelstr;
  unsigned int i;
  exportlist_t *pexport = NULL;
  int is_hw_reached = FALSE;
  int some_flush_to_do = FALSE;
  unsigned long nb_blocks_to_manage;
  char cache_sub_dir[MAXPATHLEN];
  cache_content_status_t cache_content_status;
  FILE *command_stream = NULL;

  SetNameFunction("file_content_fc_thread");

  DisplayLog("NFS FILE CONTENT GARBAGE COLLECTION : Starting GC thread");
  DisplayLog("NFS FILE CONTENT GARBAGE COLLECTION : my pthread id is %p",
             (caddr_t) pthread_self());

  debuglevelstr = ReturnLevelInt(fcc_debug_level);

  while(1)
    {
      /* Sleep until some work is to be done */
      sleep(nfs_param.cache_layers_param.dcgcpol.run_interval);

      DisplayLogLevel(NIV_EVENT, "NFS FILE CONTENT GARBAGE COLLECTION : awakening...");

      for(pexport = nfs_param.pexportlist; pexport != NULL; pexport = pexport->next)
        {
          if(pexport->options & EXPORT_OPTION_USE_DATACACHE)
            {
              snprintf(cache_sub_dir, MAXPATHLEN, "%s/export_id=%d",
                       nfs_param.cache_layers_param.cache_content_client_param.cache_dir,
                       0);

              if((cache_content_status = cache_content_check_threshold(cache_sub_dir,
                                                                       nfs_param.cache_layers_param.dcgcpol.
                                                                       lwmark_df,
                                                                       nfs_param.cache_layers_param.dcgcpol.
                                                                       hwmark_df,
                                                                       &is_hw_reached,
                                                                       &nb_blocks_to_manage))
                 == CACHE_CONTENT_SUCCESS)
                {
                  if(is_hw_reached)
                    {
                      DisplayLogLevel(NIV_EVENT,
                                      "NFS FILE CONTENT GARBAGE COLLECTION : High Water Mark is  reached, %llu blocks to be removed",
                                      nb_blocks_to_manage);
                      some_flush_to_do = TRUE;
                      break;
                    }
                  else
                    DisplayLogLevel(NIV_EVENT,
                                    "NFS FILE CONTENT GARBAGE COLLECTION : High Water Mark is not reached");

                  /* Use signal management */
                  if(force_flush_by_signal == TRUE)
                    {
                      some_flush_to_do = TRUE;
                      break;
                    }
                }
            }
        }                       /* for */

      if(debuglevelstr)
        snprintf(command, 2 * MAXPATHLEN, "%s -f %s -N %s -L %s",
                 ganesha_exec_path, config_path, debuglevelstr, fcc_log_path);
      else
        snprintf(command, 2 * MAXPATHLEN, "%s -f %s -N NIV_MAJ -L %s",
                 ganesha_exec_path, config_path, fcc_log_path);

      if(some_flush_to_do)
        strncat(command, " -P 3", 2 * MAXPATHLEN);      /* Sync and erase */
      else
        strncat(command, " -S 3", 2 * MAXPATHLEN);      /* Sync Only */

      if((command_stream = popen(command, "r")) == NULL)
        DisplayLog("NFS FILE CONTENT GARBAGE COLLECTION : /!\\ Cannot lauch command %s",
                   command);
      else
        DisplayLog("NFS FILE CONTENT GARBAGE COLLECTION : I launched command %s",
                   command);

      pclose(command_stream);
    }
}                               /* file_content_gc_thread */
