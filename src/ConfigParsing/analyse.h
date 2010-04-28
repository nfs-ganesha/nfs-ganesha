/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

#ifndef CONFPARSER_H
#define CONFPARSER_H

#include <stdio.h>

#define MAXSTRLEN   1024

/* A program consists of several blocks,
 * each block consists of variables definitions
 * and subblocks.
 */

/* forward declaration of generic item */
struct _generic_item_;

typedef enum
{
  TYPE_BLOCK,
  TYPE_AFFECT
} type_item;

typedef struct _type_affect_
{

  char varname[MAXSTRLEN];
  char varvalue[MAXSTRLEN];

} type_affect;

typedef struct _type_block_
{

  char block_name[MAXSTRLEN];
  struct _generic_item_ *block_content;

} type_block;

typedef struct _generic_item_
{

  type_item type;
  union
  {
    type_block block;
    type_affect affect;
  } item;

  /* next item in the list */
  struct _generic_item_ *next;

} generic_item;

typedef generic_item *list_items;

/**
 *  create a list of items
 */
list_items *config_CreateItemsList();

/**
 *  Create a block item with the given content
 */
generic_item *config_CreateBlock(char *blockname, list_items * list);

/**
 *  Create a key=value peer (assignment)
 */
generic_item *config_CreateAffect(char *varname, char *varval);

/**
 *  Add an item to a list
 */
void config_AddItem(list_items * list, generic_item * item);

/**
 *  Displays the content of a list of blocks.
 */
void config_print_list(FILE * output, list_items * list);

/**
 * config_free_list:
 * Free ressources for a list
 */
void config_free_list(list_items * list);

#endif
