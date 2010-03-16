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
 * \file    cache_content_read_conf.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:33 $
 * \version $Revision: 1.10 $
 * \brief   Management of the file content cache: configuration file parsing.
 *
 * cache_content_read_conf.c : Management of the file content cache: configuration file parsing.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "config_parsing.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

char fcc_log_path[MAXPATHLEN];
int fcc_debug_level;

/*
 *
 * cache_content_read_conf_client_parameter: read the configuration for a client to File Content layer.
 * 
 * Reads the configuration for a client to File Content layer (typically a worker thread).
 * 
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_CONTENT_SUCCESS if ok, CACHE_CONTENT_INVALID_ARGUMENT otherwise.
 *
 */
cache_content_status_t cache_content_read_conf_client_parameter(config_file_t in_config,
                                                                cache_content_client_parameter_t
                                                                * pparam)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  int DebugLevel = -1;
  char *LogFile = NULL;

  /* Is the config tree initialized ? */
  if (in_config == NULL || pparam == NULL)
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if ((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_CONTENT_CLIENT)) == NULL)
    {
      /* fprintf(stderr, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CACHE_CONTENT_CLIENT ) ; */
      return CACHE_CONTENT_NOT_FOUND;
  } else if (config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return CACHE_CONTENT_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for (var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if ((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          fprintf(stderr,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_CACHE_CONTENT_CLIENT);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

      if (!strcasecmp(key_name, "LRU_Prealloc_PoolSize"))    /** @todo: BUGAZOMEU: to be removed */
        {
          //pparam->lru_param.nb_entry_prealloc = atoi( key_value ) ;
      } else if (!strcasecmp(key_name, "LRU_Nb_Call_Gc_invalid"))   /** @todo: BUGAZOMEU: to be removed */
        {
          //pparam->lru_param.nb_call_gc_invalid = atoi( key_value ) ;
      } else if (!strcasecmp(key_name, "Entry_Prealloc_PoolSize"))   /** @todo: BUGAZOMEU: to be removed */
        {
          pparam->nb_prealloc_entry = atoi(key_value);
      } else if (!strcasecmp(key_name, "Cache_Directory"))
        {
          strcpy(pparam->cache_dir, key_value);
      } else if (!strcasecmp(key_name, "Refresh_FSAL_Force"))
        {
          pparam->flush_force_fsal = atoi(key_value);
      } else if (!strcasecmp(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if (DebugLevel == -1)
            {
              DisplayLog
                  ("cache_content_read_conf: ERROR: Invalid debug level name: \"%s\".",
                   key_value);
              return CACHE_CONTENT_INVALID_ARGUMENT;
            }
      } else if (!strcasecmp(key_name, "LogFile"))
        {
          LogFile = key_value;
      } else if (!strcasecmp(key_name, "Max_Fd"))
        {
          pparam->max_fd_per_thread = atoi(key_value);
      } else if (!strcasecmp(key_name, "OpenFile_Retention"))
        {
          pparam->retention = atoi(key_value);
      } else if (!strcasecmp(key_name, "Use_OpenClose_cache"))
        {
          pparam->use_cache = StrToBoolean(key_value);
        } else
        {
          fprintf(stderr,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_CACHE_CONTENT_CLIENT);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }
    }

  fcc_debug_level = DebugLevel;
  if (LogFile)
    strncpy(fcc_log_path, LogFile, MAXPATHLEN);
    else
    strncpy(fcc_log_path, "/dev/null", MAXPATHLEN);

  /* init logging */
  if (LogFile)
    {
      desc_log_stream_t log_stream;

      strcpy(log_stream.path, LogFile);

      /* Default : NIV_CRIT */

      if (DebugLevel == -1)
        AddLogStreamJd(&(pparam->log_outputs), V_FILE, log_stream, NIV_CRIT, SUP);
        else
        AddLogStreamJd(&(pparam->log_outputs), V_FILE, log_stream, DebugLevel, SUP);

    }

  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_read_conf_client_parameter */

/**
 *
 * cache_content_print_conf_client_parameter: prints the client parameter.
 * 
 * Prints the client parameters.
 * 
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed. 
 *
 * @return nothing (void function).
 *
 */
void cache_content_print_conf_client_parameter(FILE * output,
                                               cache_content_client_parameter_t param)
{
  /* @todo BUGAZOMEU virer ces deux lignes */
  //fprintf( output, "FileContent Client: LRU_Prealloc_PoolSize   = %d\n", param.lru_param.nb_entry_prealloc ) ;
  //fprintf( output, "FileContent Client: LRU_Nb_Call_Gc_invalid  = %d\n", param.lru_param.nb_call_gc_invalid ) ;
  fprintf(output, "FileContent Client: Entry_Prealloc_PoolSize = %d\n",
          param.nb_prealloc_entry);
  fprintf(output, "FileContent Client: Cache Directory         = %s\n", param.cache_dir);
}                               /* cache_content_print_conf_client_parameter */

/**
 *
 * cache_content_read_conf_gc_policy: read the garbage collection policy in configuration file.
 *
 * Reads the garbage collection policy in configuration file.
 *
 * @param in_config [IN] configuration file handle
 * @param pparam [OUT] read parameters
 *
 * @return CACHE_CONTENT_SUCCESS if ok, CACHE_CONTENT_NOT_FOUND is stanza is not there, CACHE_CONTENT_INVALID_ARGUMENT otherwise.
 *
 */
cache_content_status_t cache_content_read_conf_gc_policy(config_file_t in_config,
                                                         cache_content_gc_policy_t *
                                                         ppolicy)
{
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  config_item_t block;

  /* Is the config tree initialized ? */
  if (in_config == NULL || ppolicy == NULL)
    return CACHE_CONTENT_INVALID_ARGUMENT;

  /* Get the config BLOCK */
  if ((block = config_FindItemByName(in_config, CONF_LABEL_CACHE_CONTENT_GCPOL)) == NULL)
    {
      /* fprintf(stderr, "Cannot read item \"%s\" from configuration file\n", CONF_LABEL_CACHE_CONTENT_GCPOL ) ; */
      return CACHE_CONTENT_NOT_FOUND;
  } else if (config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return CACHE_CONTENT_INVALID_ARGUMENT;
    }

  var_max = config_GetNbItems(block);

  for (var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if ((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          fprintf(stderr,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, CONF_LABEL_CACHE_CONTENT_GCPOL);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

      if (!strcasecmp(key_name, "Lifetime"))
        {
          ppolicy->lifetime = atoi(key_value);
      } else if (!strcasecmp(key_name, "Inactivity_Before_Flush"))
        {
          ppolicy->inactivity_before_flush = atoi(key_value);
      } else if (!strcasecmp(key_name, "Runtime_Interval"))
        {
          ppolicy->run_interval = atoi(key_value);
      } else if (!strcasecmp(key_name, "Nb_Call_Before_GC"))
        {
          ppolicy->nb_call_before_gc = atoi(key_value);
      } else if (!strcasecmp(key_name, "Df_HighWater"))
        {
          ppolicy->hwmark_df = atoi(key_value);
      } else if (!strcasecmp(key_name, "Df_LowWater"))
        {
          ppolicy->lwmark_df = atoi(key_value);
      } else if (!strcasecmp(key_name, "Emergency_Grace_Delay"))
        {
          ppolicy->emergency_grace_delay = atoi(key_value);
        } else
        {
          fprintf(stderr,
                  "Unknown or unsettable key: %s (item %s)\n",
                  key_name, CONF_LABEL_CACHE_CONTENT_GCPOL);
          return CACHE_CONTENT_INVALID_ARGUMENT;
        }

    }
  return CACHE_CONTENT_SUCCESS;
}                               /* cache_content_read_conf_gc_policy */

/**
 *
 * cache_content_print_gc_pol: prints the garbage collection policy.
 *
 * Prints the garbage collection policy.
 *
 * @param output [IN] a descriptor to the IO for printing the data.
 * @param param [IN] structure to be printed.
 *
 * @return nothing (void function).
 *
 */
void cache_content_print_conf_gc_policy(FILE * output, cache_content_gc_policy_t gcpolicy)
{
  fprintf(output, "Garbage Policy: Lifetime              = %u\n",
          (unsigned int)gcpolicy.lifetime);
  fprintf(output, "Garbage Policy: Df_HighWater          = %u%%\n", gcpolicy.hwmark_df);
  fprintf(output, "Garbage Policy: Df_LowWater           = %u%%\n", gcpolicy.lwmark_df);
  fprintf(output, "Garbage Policy: Emergency Grace Delay = %u\n",
          (unsigned int)gcpolicy.emergency_grace_delay);
  fprintf(output, "Garbage Policy: Nb_Call_Before_GC     = %u\n",
          gcpolicy.nb_call_before_gc);
  fprintf(output, "Garbage Policy: Runtime_Interval      = %u\n", gcpolicy.run_interval);
}                               /* cache_content_print_gc_pol */
