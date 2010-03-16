/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 * ----------------------------------------------------------------------------
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
 * Copyright CEA/DAM/DIF  (2008)
 * contributor : Philippe DENIEL   philippe.deniel@cea.fr
 *               Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * This software is a server that implements the NFS protocol.
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
 * therefore means  that it is reserved for developers  and  experienced
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

#include "config.h"
#include "analyse.h"
#include <stdlib.h>
#include <stdio.h>

#if HAVE_STRING_H
#   include <string.h>
#endif

/**
 *  create a list of items
 */
list_items *config_CreateItemsList()
{
  list_items *new = (list_items *) malloc(sizeof(list_items));

  (*new) = NULL;
  return new;
}

/**
 *  Create a block item with the given content
 */
generic_item *config_CreateBlock(char *blockname, list_items * list)
{
  generic_item *new = (generic_item *) malloc(sizeof(generic_item));

  new->type = TYPE_BLOCK;

  strncpy(new->item.block.block_name, blockname, MAXSTRLEN);

  if (list)
    {
      new->item.block.block_content = *list;
      free(list);
    } else
    new->item.block.block_content = NULL;

  new->next = NULL;

  return new;

}

/**
 *  Create a key=value peer (assignment)
 */
generic_item *config_CreateAffect(char *varname, char *varval)
{
  generic_item *new = (generic_item *) malloc(sizeof(generic_item));

  new->type = TYPE_AFFECT;
  strncpy(new->item.affect.varname, varname, MAXSTRLEN);
  strncpy(new->item.affect.varvalue, varval, MAXSTRLEN);

  new->next = NULL;

  return new;

}

/**
 *  Add an item to a list as first element
 */
void config_AddItem(list_items * list, generic_item * item)
{
  if ((*list) == NULL)
    {
      (*list) = item;
    } else
    {
      item->next = (*list);
      (*list) = item;
    }
}

/**
 *  Displays the content of a list of blocks.
 */
static void print_list_ident(FILE * output, list_items * list, unsigned int indent)
{

  generic_item *curr_item;

  /* sanity check */
  if (!list)
    return;

  curr_item = (*list);

  while (curr_item)
    {

      if (curr_item->type == TYPE_BLOCK)
	{
	  fprintf(output, "%*s<BLOCK '%s'>\n", indent, " ",
		  curr_item->item.block.block_name);
	  print_list_ident(output, &curr_item->item.block.block_content, indent + 3);
	  fprintf(output, "%*s</BLOCK '%s'>\n", indent, " ",
		  curr_item->item.block.block_name);
	} else
	{
	  /* affectation */
	  fprintf(output, "%*sKEY: '%s', VALUE: '%s'\n", indent, " ",
		  curr_item->item.affect.varname, curr_item->item.affect.varvalue);
	}

      curr_item = curr_item->next;
    }

}

/**
 *  Displays the content of a list of blocks.
 */
void config_print_list(FILE * output, list_items * list)
{

  print_list_ident(output, list, 0);

}

static void free_list_items_recurse(list_items * list)
{
  generic_item *curr_item;
  generic_item *next_item;

  /* sanity check */
  if (!list)
    return;

  curr_item = (*list);

  while (curr_item)
    {

      next_item = curr_item->next;

      if (curr_item->type == TYPE_BLOCK)
	{
	  free_list_items_recurse(&curr_item->item.block.block_content);
	}

      free(curr_item);
      curr_item = next_item;

    }
  return;
}

/**
 * config_free_list:
 * Free ressources for a list
 */
void config_free_list(list_items * list)
{

  free_list_items_recurse(list);
  free(list);
  return;
}
