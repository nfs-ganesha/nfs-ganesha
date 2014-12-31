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

struct config_term_type config_term_type[] = {
	[TERM_TOKEN]  = {"TOKEN", "option name or number"},
	[TERM_PATH]   = {"PATH", "file path name"},
	[TERM_STRING] = {"STRING", "quoted string"}
};

/**
 *  Displays the content of a list of blocks.
 */
static void print_node(FILE *output,
		       struct config_node *node,
		       unsigned int indent)
{
	struct config_node *sub_node;
	struct glist_head *nsi, *nsn;

	if (node->type == TYPE_BLOCK) {
		fprintf(output, "%*s<BLOCK '%s' %s:%d>\n", indent, " ",
			node->u.nterm.name, node->filename, node->linenumber);
		glist_for_each_safe(nsi, nsn, &node->u.nterm.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			print_node(output, sub_node, indent + 3);
		}
		fprintf(output, "%*s</BLOCK '%s'>\n", indent, " ",
			node->u.nterm.name);
	} else if (node->type == TYPE_STMT) {
		fprintf(output, "%*s<STMT '%s' %s:%d>\n", indent, " ",
			node->u.nterm.name, node->filename, node->linenumber);
		glist_for_each_safe(nsi, nsn, &node->u.nterm.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			print_node(output, sub_node, indent + 3);
		}
		fprintf(output, "%*s</STMT '%s'>\n", indent, " ",
			node->u.nterm.name);
	} else {
		/* a statement value */
		fprintf(output, "%*s(%s)'%s'\n", indent, " ",
			(node->u.term.type != 0
			 ? config_term_type[node->u.term.type].name
			 : "unknown"),
			node->u.term.varvalue);
	}
}

void print_parse_tree(FILE *output, struct config_root *tree)
{
	struct config_node *node;
	struct glist_head *nsi, *nsn;

	assert(tree->root.type == TYPE_ROOT);
	glist_for_each_safe(nsi, nsn, &tree->root.u.nterm.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		print_node(output, node, 0);
	}
	return;
}

static void free_node(struct config_node *node)
{
	if (node->type == TYPE_BLOCK || node->type == TYPE_STMT) {
		struct config_node *sub_node;
		struct glist_head *nsi, *nsn;

		gsh_free(node->u.nterm.name);
		glist_for_each_safe(nsi, nsn, &node->u.nterm.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			glist_del(&sub_node->node);
			free_node(sub_node);
		}
	} else { /* TYPE_TERM */
		gsh_free(node->u.term.varvalue);
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
