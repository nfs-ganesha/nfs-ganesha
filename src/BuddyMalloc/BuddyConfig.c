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
 * \file    BuddyConfig.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 13:43:20 $
 * \version $Revision: 1.1 $
 * \brief   Configuration parsing for BuddyMallocator.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "BuddyMalloc.h"
#include "log_functions.h"
#include "common_utils.h"
#include <strings.h>
#include <string.h>

#define STRCMP strcasecmp

extern buddy_parameter_t default_buddy_parameter;

/* there but be exactly 1 bit set */
static int check_2power(unsigned long long tested_size)
{
  int i;

  for (i = 0; i < 64; i++)
    {
      /* if the first detected bit equals tested_size,
       * it's OK.
       */
      if ((tested_size & (1 << i)) == tested_size)
	return TRUE;
    }
  return FALSE;

}

int Buddy_set_default_parameter(buddy_parameter_t * out_parameter)
{
  if (out_parameter == NULL)
    return BUDDY_ERR_EFAULT;

  *out_parameter = default_buddy_parameter;

  return BUDDY_SUCCESS;
}

int Buddy_load_parameter_from_conf(config_file_t in_config,
				   buddy_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_BUDDY);

  /* cannot read item */

  if (block == NULL)
    {
      DisplayLog("BUDDY LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
		 CONF_LABEL_BUDDY);
      return BUDDY_ERR_ENOENT;
  } else if (config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      DisplayLog("BUDDY LOAD PARAMETER: Item \"%s\" is expected to be a block",
		 CONF_LABEL_BUDDY);
      return BUDDY_ERR_EINVAL;
    }

  /* read variable for fsal init */

  var_max = config_GetNbItems(block);

  for (var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if (err)
	{
	  DisplayLog
	      ("BUDDY LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
	       var_index, CONF_LABEL_BUDDY);
	  return BUDDY_ERR_EFAULT;
	}

      if (!STRCMP(key_name, "Page_Size"))
	{
	  size_t page_size;

	  if (s_read_size(key_value, &page_size) || !check_2power(page_size))
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: must be a 2^n value.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->memory_area_size = page_size;

      } else if (!STRCMP(key_name, "Enable_OnDemand_Alloc"))
	{
	  int bool;

	  bool = StrToBoolean(key_value);

	  if (bool == -1)
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->on_demand_alloc = bool;

      } else if (!STRCMP(key_name, "Enable_Extra_Alloc"))
	{
	  int bool;

	  bool = StrToBoolean(key_value);

	  if (bool == -1)
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->extra_alloc = bool;

      } else if (!STRCMP(key_name, "Enable_GC"))
	{
	  int bool;

	  bool = StrToBoolean(key_value);

	  if (bool == -1)
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->free_areas = bool;

      } else if (!STRCMP(key_name, "GC_Keep_Factor"))
	{

	  int keep_factor = s_read_int(key_value);

	  if (keep_factor < 1)
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->keep_factor = keep_factor;

      } else if (!STRCMP(key_name, "GC_Keep_Min"))
	{

	  int keep_min = s_read_int(key_value);

	  if (keep_min < 0)
	    {
	      DisplayLog
		  ("BUDDY LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
		   key_name);
	      return BUDDY_ERR_EINVAL;
	    }

	  out_parameter->keep_minimum = keep_min;

      } else if (!STRCMP(key_name, "LogFile"))
	{

	  strncpy(out_parameter->buddy_error_file, key_value, 256);

	} else
	{
	  DisplayLog
	      ("BUDDY LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
	       key_name, CONF_LABEL_BUDDY);
	  return BUDDY_ERR_EINVAL;
	}

    }

  return BUDDY_SUCCESS;

}				/* Buddy_load_parameter_from_conf */
