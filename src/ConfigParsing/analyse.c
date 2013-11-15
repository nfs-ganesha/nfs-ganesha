/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

#include "config.h"
#include "analyse.h"
#include <stdlib.h>
#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include "abstract_mem.h"

/**
 *  Displays the content of a list of blocks.
 */
static void print_node(FILE *output,
		       struct config_node *node,
		       unsigned int indent)
{
	if (node->type == TYPE_BLOCK) {
		struct config_node *sub_node;
		struct glist_head *nsi, *nsn;

		fprintf(output, "%*s<BLOCK '%s' %s:%d>\n", indent, " ",
			node->name, node->filename, node->linenumber);
		glist_for_each_safe(nsi, nsn, &node->u.blk.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			print_node(output, sub_node, indent + 3);
		}
		fprintf(output, "%*s</BLOCK '%s'>\n", indent, " ",
			node->name);
	} else {
		/* a statement */
		fprintf(output, "%*s%s:%d: '%s' = '%s'\n", indent, " ",
			node->filename, node->linenumber,
			node->name, node->u.varvalue);
	}
}

void print_parse_tree(FILE *output, struct config_root *tree)
{
	struct config_node *node;
	struct glist_head *nsi, *nsn;

	glist_for_each_safe(nsi, nsn, &tree->root.node) {
		node = glist_entry(nsi, struct config_node, node);
		print_node(output, node, 0);
	}
	return;
}

static void free_node(struct config_node *node)
{
	gsh_free(node->name);
	if (node->type == TYPE_BLOCK) {
		struct config_node *sub_node;
		struct glist_head *nsi, *nsn;

		glist_for_each_safe(nsi, nsn, &node->u.blk.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			glist_del(&sub_node->node);
			free_node(sub_node);
		}
	} else {
		gsh_free(node->u.varvalue);
	}
	gsh_free(node);
	return;
}

void free_parse_tree(struct config_root *tree)
{
	struct file_list *file, *next_file;
	struct config_node *node;
	struct glist_head *nsi, *nsn;

	glist_for_each_safe(nsi, nsn, &tree->root.node) {
		node = glist_entry(nsi, struct config_node, node);
		glist_del(&node->node);
		free_node(node);
	}
	if(tree->conf_dir != NULL)
		gsh_free(tree->conf_dir);
	file = tree->files;
	while (file != NULL) {
		next_file = file->next;
		gsh_free(file->pathname);
		gsh_free(file);
		file = next_file;
	}
	gsh_free(tree);
	return;
}
