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
#include "config_parsing.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include "abstract_mem.h"

/**
 * @brief Insert a scanner token into the token table
 *
 * Look up the token in the list matching case insensitive.
 * If there is a match, return a pointer to the token in the table.
 * Otherwise, allocate space, link it and return the pointer.
 * if 'esc' == true, this is a double quoted string which needs to
 * be filtered.  Turn the escaped non-printable into the non-printable.
 *
 * @param token [IN] pointer to the yytext token from flex
 * @param esc [IN] bool, filter if true
 * @param st [IN] pointer to parser state
 * @return pointer to persistant storage for token or NULL;
 */

char *save_token(char *token, bool esc, struct parser_state *st)
{
	struct token_tab *tokp, *new_tok;

	for (tokp = st->root_node->tokens;
	     tokp != NULL;
	     tokp = tokp->next) {
		if (strcasecmp(token, tokp->token) == 0)
			return tokp->token;
	}
	new_tok = gsh_calloc(1, (sizeof(struct token_tab) +
				 strlen(token) + 1));
	if (new_tok == NULL)
		return NULL;
	if (esc) {
		char *sp, *dp;
		int c;

		sp = token;
		dp = new_tok->token;
		c = *sp++;
		if (c == '\"')
			c = *sp++; /* gobble leading '"' from regexp */
		while (c != '\0') {
			if (c == '\\') {
				c = *sp++;
				if (c == '\0')
					break;
				switch (c) {
				case 'n':
					c = '\n';
					break;
				case 't':
					c = '\t';
					break;
				case 'r':
					c = '\r';
					break;
				default:
					break;
				}
			} else if (c == '"' && *sp == '\0')
				break;  /* skip trailing '"' from regexp */
			*dp++ = c;
			c = *sp++;
		}
	} else
		strcpy(new_tok->token, token);
	new_tok->next = st->root_node->tokens;
	st->root_node->tokens = new_tok;
	return new_tok->token;
}

struct {
	const char *name;
	const char *desc;
} config_term_type[] = {
	[TERM_TOKEN]  = {"TOKEN", "option name or number"},
	[TERM_PATH]   = {"PATH", "file path name"},
	[TERM_STRING] = {"STRING", "simple string"},
	[TERM_DQUOTE] = {"DQUOTE", "double quoted string"},
	[TERM_SQUOTE] = {"SQUOTE", "single quoted string"},
	[TERM_TRUE]   = {"TRUE", "boolean TRUE"},
	[TERM_FALSE]  = {"FALSE", "boolean FALSE"},
	[TERM_DECNUM] = {"DECNUM", "decimal number"},
	[TERM_HEXNUM] = {"HEXNUM", "hexadecimal number"},
	[TERM_OCTNUM] = {"OCTNUM", "octal number"}
};

const char *config_term_name(enum term_type type)
{
	return config_term_type[(int)type].name;
}

const char *config_term_desc(enum term_type type)
{
	return config_term_type[(int)type].desc;
}

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
		fprintf(output, "%*s(%s)'%s' '%s'\n", indent, " ",
			(node->u.term.type != 0
			 ? config_term_type[node->u.term.type].name
			 : "unknown"),
			(node->u.term.op_code != NULL
			 ? node->u.term.op_code : " "),
			node->u.term.varvalue);
	}
}

void print_parse_tree(FILE *output, struct config_root *tree)
{
	struct config_node *node;
	struct file_list *file;
	struct token_tab *token;
	struct glist_head *nsi, *nsn;

	assert(tree->root.type == TYPE_ROOT);
	fprintf(output, "<SUMMARY>\n");
	fprintf(output, "   <BLOCK_COUNT> %ld </BLOCKCOUNT>\n",
		glist_length(&tree->root.u.nterm.sub_nodes));
	fprintf(output, "   <CONFIGURATION_FILES>\n");
	for (file = tree->files; file != NULL; file = file->next)
		fprintf(output, "      <FILE> \"%s\" </FILE>\n",
			file->pathname);
	fprintf(output, "   </CONFIGURATION_FILES>\n");
	fprintf(output, "   <TOKEN_TABLE>\n");
	for (token = tree->tokens; token != NULL; token = token->next)
		fprintf(output, "      <TOKEN>%s</TOKEN>\n", token->token);
	fprintf(output, "   </TOKEN_TABLE>\n");
	fprintf(output, "</SUMMARY>\n");
	fprintf(output, "<PARSE_TREE>\n");
	glist_for_each_safe(nsi, nsn, &tree->root.u.nterm.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		print_node(output, node, 3);
	}
	fprintf(output, "</PARSE_TREE>\n");
	return;
}

/**
 * @brief Free a parse tree node.
 *
 * Note that we do not free either u.nterm.name or u.term.varvalue.
 * this is because these are pointers into the token table which
 * is freed elsewhere.
 */

static void free_node(struct config_node *node)
{
	if (node->type == TYPE_BLOCK || node->type == TYPE_STMT) {
		struct config_node *sub_node;
		struct glist_head *nsi, *nsn;

		glist_for_each_safe(nsi, nsn, &node->u.nterm.sub_nodes) {
			sub_node = glist_entry(nsi, struct config_node, node);
			glist_del(&sub_node->node);
			free_node(sub_node);
		}
	}
	gsh_free(node);
	return;
}

void free_parse_tree(struct config_root *tree)
{
	struct file_list *file, *next_file;
	struct token_tab *token, *next_token;
	struct config_node *node;
	struct glist_head *nsi, *nsn;

	glist_for_each_safe(nsi, nsn, &tree->root.u.nterm.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		glist_del(&node->node);
		free_node(node);
	}
	gsh_free(tree->root.filename);
	if(tree->conf_dir != NULL)
		gsh_free(tree->conf_dir);
	file = tree->files;
	while (file != NULL) {
		next_file = file->next;
		gsh_free(file->pathname);
		gsh_free(file);
		file = next_file;
	}
	token = tree->tokens;
	while (token != NULL) {
		next_token = token->next;
		gsh_free(token);
		token = next_token;
	}
	gsh_free(tree);
	return;
}

void config_error(FILE *fp, const char *filename, int linenum,
			 char *fmt, va_list args)
{
	fprintf(fp, "Config File (%s:%d): ", filename, linenum);
	vfprintf(fp, fmt, args);
	fputc('\f', fp); /* form feed (remember those?) used as msg sep */
}

