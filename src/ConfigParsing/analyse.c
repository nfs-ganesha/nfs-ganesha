/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
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

  if(list)
    {
      new->item.block.block_content = *list;
      free(list);
    }
  else
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
  if((*list) == NULL)
    {
      (*list) = item;
    }
  else
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
  if(!list)
    return;

  curr_item = (*list);

  while(curr_item)
    {

      if(curr_item->type == TYPE_BLOCK)
        {
          fprintf(output, "%*s<BLOCK '%s'>\n", indent, " ",
                  curr_item->item.block.block_name);
          print_list_ident(output, &curr_item->item.block.block_content, indent + 3);
          fprintf(output, "%*s</BLOCK '%s'>\n", indent, " ",
                  curr_item->item.block.block_name);
        }
      else
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
  if(!list)
    return;

  curr_item = (*list);

  while(curr_item)
    {

      next_item = curr_item->next;

      if(curr_item->type == TYPE_BLOCK)
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
