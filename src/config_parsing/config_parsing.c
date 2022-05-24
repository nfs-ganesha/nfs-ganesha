// SPDX-License-Identifier: LGPL-3.0-or-later
/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#if HAVE_STRING_H
#include <string.h>
#endif
#include <sys/types.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "config_parsing.h"
#include "analyse.h"
#include "abstract_mem.h"
#include "conf_yacc.h"
#include "log.h"
#include "fsal_convert.h"

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 */

config_file_t config_ParseFile(char *file_path,
			       struct config_error_type *err_type)
{
	struct parser_state st;
	struct config_root *root;
	int rc;

	glist_init(&all_blocks);
	memset(&st, 0, sizeof(struct parser_state));
	st.err_type = err_type;
	rc = ganeshun_yy_init_parser(file_path, &st);
	if (rc) {
		return NULL;
	}
	rc = ganesha_yyparse(&st);
	root = st.root_node;
	if (rc != 0)
		config_proc_error(root, err_type,
				  (rc == 1
				   ? "Configuration syntax errors found"
				   : "Configuration parse ran out of memory"));
#ifdef DUMP_PARSE_TREE
	print_parse_tree(stderr, root);
#endif
	ganeshun_yy_cleanup_parser(&st);
	return (config_file_t)root;
}

/**
 *  Return the first node in the global config block list with
 *  name == block_name
 */
void *config_GetBlockNode(const char *block_name)
{
	struct glist_head *glh;
	struct config_node *node;

	glist_for_each(glh, &all_blocks) {
		node = glist_entry(glh, struct config_node, blocks);
		if (!strcasecmp(node->u.nterm.name, block_name)) {
			return node;
		}
	}
	return NULL;
}

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print(FILE * output, config_file_t config)
{
	print_parse_tree(output, (struct config_root *)config);
}

/**
 * config_Free:
 * Free the memory structure that store the configuration.
 */

void config_Free(config_file_t config)
{
	if (config != NULL)
		free_parse_tree((struct config_root *)config);
	return;

}

/**
 * @brief Return an error string constructed from an err_type
 *
 * The string is constructed in allocate memory that must be freed
 * by the caller.
 *
 * @param err_type [IN] the err_type struct in question.
 *
 * @return a NULL term'd string or NULL on failure.
 */

char *err_type_str(struct config_error_type *err_type)
{
	char *buf = NULL;
	size_t bufsize;
	FILE *fp;

	if (config_error_no_error(err_type))
		return gsh_strdup("(no errors)");
	fp = open_memstream(&buf, &bufsize);
	if (fp == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Could not open memstream for err_type string");
		return NULL;
	}
	fputc('(', fp);
	if (err_type->scan)
		fputs("token scan, ", fp);
	if (err_type->parse)
		fputs("parser rule, ", fp);
	if (err_type->init)
		fputs("block init, ", fp);
	if (err_type->fsal)
		fputs("fsal load, ", fp);
	if (err_type->all_exp_create_err)
		fputs("export create error, ", fp);
	if (err_type->resource)
		fputs("resource alloc, ", fp);
	if (err_type->unique)
		fputs("not unique param, ", fp);
	if (err_type->invalid)
		fputs("invalid param value, ", fp);
	if (err_type->missing)
		fputs("missing mandatory param, ", fp);
	if (err_type->validate)
		fputs("block validation, ", fp);
	if (err_type->exists)
		fputs("block exists, ", fp);
	if (err_type->internal)
		fputs("internal error, ", fp);
	if (err_type->bogus)
		fputs("unknown param, ", fp);
	if (err_type->deprecated)
		fputs("deprecated param, ", fp);
	if (ferror(fp))
		LogCrit(COMPONENT_CONFIG,
			"file error while constructing err_type string");
	fclose(fp);
	if (buf == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"close of memstream for err_type string failed");
		return NULL;
	}
	/* each of the above strings (had better) have ', ' at the end! */
	if (buf[strlen(buf) -1] == ' ') {
		buf[bufsize - 2] = ')';
		buf[bufsize - 1] = '\0';
	}
	return buf;
}

bool init_error_type(struct config_error_type *err_type)
{
	memset(err_type, 0, sizeof(struct config_error_type));
	err_type->fp = open_memstream(&err_type->diag_buf,
				     &err_type->diag_buf_size);
	if (err_type->fp == NULL) {
		LogCrit(COMPONENT_MAIN,
			 "Could not open memory stream for parser errors");
		return false;
	}
	return true;
}

/**
 * @brief Log an error to the parse error stream
 *
 * cnode is void * because struct config_node is hidden outside parse code
 * and we can report errors in fsal_manager.c etc.
 */

void config_proc_error(void *cnode,
		       struct config_error_type *err_type,
		       char *format, ...)
{
	struct config_node *node = cnode;
	FILE *fp = err_type->fp;
	char *filename = "<unknown file>";
	int linenumber = 0;
	va_list arguments;

	if (fp == NULL)
		return;  /* no stream, no message */
	if (node != NULL && node->filename != NULL) {
		filename = node->filename;
		linenumber = node->linenumber;
	}
	va_start(arguments, format);
	config_error(fp, filename, linenumber,
		     format, arguments);
	va_end(arguments);
}
void config_errs_to_log(char *err, void *dest,
			struct config_error_type *err_type)
{
	log_levels_t log_level;

	if (config_error_is_fatal(err_type) ||
	    config_error_is_crit(err_type))
		log_level = NIV_CRIT;
	else if (config_error_is_harmless(err_type))
		log_level = NIV_WARN;
	else
		log_level = NIV_EVENT;
	DisplayLogComponentLevel(COMPONENT_CONFIG,
				 __FILE__, __LINE__, (char *)__func__,
				  log_level, "%s", err);
}

void report_config_errors(struct config_error_type *err_type, void *dest,
			  void (*logger)(char *msg, void *dest,
					 struct config_error_type *err_type))
{
	char *msgp, *cp;

	if (err_type->fp == NULL)
		return;
	fclose(err_type->fp);
	err_type->fp = NULL;
	msgp = err_type->diag_buf;
	if (msgp == NULL)
		return;
	while (*msgp != '\0') {
		cp = index(msgp, '\f');
		if (cp != NULL) {
			*cp++ = '\0';
			logger(msgp, dest, err_type);
			msgp = cp;
		} else {
			logger(msgp, dest, err_type);
			break;
		}
	}
	gsh_free(err_type->diag_buf);
	err_type->diag_buf = NULL;
}

static bool convert_bool(struct config_node *node,
			 bool *b,
			 struct config_error_type *err_type)
{
	if (node->u.term.type == TERM_TRUE)
		*b = true;
	else if (node->u.term.type == TERM_FALSE)
		*b = false;
	else {
		config_proc_error(node, err_type,
		 "Expected boolean (true/false) got (%s)",
		 node->u.term.varvalue);
		err_type->errors++;
		err_type->invalid = true;
		return false;
	}
	return true;
}

static bool convert_number(struct config_node *node,
			   struct config_item *item,
			   uint64_t *num,
			   struct config_error_type *err_type)
{
	uint64_t val, mask, min, max;
	int64_t sval, smin, smax;
	char *endptr;
	int base;
	bool signed_int = false;
	bool zero_ok = false, inrange;

	if (node->type != TYPE_TERM) {
		config_proc_error(node, err_type,
				  "Expected a number, got a %s",
				  (node->type == TYPE_ROOT
				   ? "root node"
				   : (node->type == TYPE_BLOCK
				      ? "block" : "statement")));
		goto errout;
	} else if (node->u.term.type == TERM_DECNUM) {
		base = 10;
	} else if (node->u.term.type == TERM_HEXNUM) {
		base = 16;
	} else if (node->u.term.type == TERM_OCTNUM) {
		base = 8;
	} else {
		config_proc_error(node, err_type,
				  "Expected a number, got a %s",
				  config_term_desc(node->u.term.type));
		goto errout;
	}
	errno = 0;
	assert(*node->u.term.varvalue != '\0');
	val = strtoull(node->u.term.varvalue, &endptr, base);
	if (*endptr != '\0' || errno != 0) {
		config_proc_error(node, err_type,
				  "(%s) is not an integer",
				  node->u.term.varvalue);
		goto errout;
	}
	switch (item->type) {
	case CONFIG_INT16:
		smin = item->u.i16.minval;
		smax = item->u.i16.maxval;
		zero_ok = item->u.i16.zero_ok;
		signed_int = true;
		break;
	case CONFIG_UINT16:
		mask = UINT16_MAX;
		min = item->u.ui16.minval;
		max = item->u.ui16.maxval;
		zero_ok = item->u.ui16.zero_ok;
		break;
	case CONFIG_INT32:
		smin = item->u.i32.minval;
		smax = item->u.i32.maxval;
		zero_ok = item->u.i32.zero_ok;
		signed_int = true;
		break;
	case CONFIG_UINT32:
		mask = UINT32_MAX;
		min = item->u.ui32.minval;
		max = item->u.ui32.maxval;
		zero_ok = item->u.ui32.zero_ok;
		break;
	case CONFIG_ANON_ID:
		/* Internal to config, anonymous id is treated as int64_t */
		smin = item->u.i64.minval;
		smax = item->u.i64.maxval;
		zero_ok = item->u.i64.zero_ok;
		signed_int = true;
		break;
	case CONFIG_INT64:
		smin = item->u.i64.minval;
		smax = item->u.i64.maxval;
		zero_ok = item->u.i64.zero_ok;
		signed_int = true;
		break;
	case CONFIG_UINT64:
		mask = UINT64_MAX;
		min = item->u.ui64.minval;
		max = item->u.ui64.maxval;
		zero_ok = item->u.ui64.zero_ok;
		break;
	default:
		goto errout;
	}

	if (signed_int) {
		if (node->u.term.op_code == NULL) {
			/* Check for overflow of int64_t on positive */
			if (val > (uint64_t) INT64_MAX) {
				config_proc_error(node, err_type,
					  "(%s) is out of range",
					  node->u.term.varvalue);
				goto errout;
			}
			sval = val;
		} else if (*node->u.term.op_code == '-') {
			/* Check for underflow of int64_t on negative */
			if (val > ((uint64_t) INT64_MAX) + 1) {
				config_proc_error(node, err_type,
					  "(%s) is out of range",
					  node->u.term.varvalue);
				goto errout;
			}
			sval = -((int64_t) val);
		} else {
			config_proc_error(node, err_type,
				  "(%c) is not allowed for signed values",
				  *node->u.term.op_code);
			goto errout;
		}

		inrange = (sval >= smin && sval <= smax) ||
			  (zero_ok && sval == 0);

		if (!inrange) {
			config_proc_error(node, err_type,
				  "(%s) is out of range",
				  node->u.term.varvalue);
			goto errout;
		}

		val = (uint64_t) sval;
	} else {
		if (node->u.term.op_code != NULL &&
		    *node->u.term.op_code == '~') {
			/* Check for overflow before negation */
			if ((val & ~mask) != 0) {
				config_proc_error(node, err_type,
					  "(%s) is out of range",
					  node->u.term.varvalue);
				goto errout;
			}
			val = ~val & mask;
		} else if (node->u.term.op_code != NULL) {
			config_proc_error(node, err_type,
				  "(%c) is not allowed for signed values",
				  *node->u.term.op_code);
			goto errout;
		}

		inrange = (val >= min && val <= max) ||
			  (zero_ok && val == 0);

		if (!inrange) {
			config_proc_error(node, err_type,
				  "(%s) is out of range",
				  node->u.term.varvalue);
			goto errout;
		}
	}

	*num = val;
	return true;

errout:
	err_type->errors++;
	err_type->invalid = true;
	return false;
}

/**
 * @brief convert an fsid which is a "<64bit num>.<64bit num"
 *
 * NOTE: using assert() here because the parser has already
 * validated so bad things have happened to the parse tree if...
 *
 */

static bool convert_fsid(struct config_node *node, void *param,
			 struct config_error_type *err_type)
{
	struct fsal_fsid__ *fsid = (struct fsal_fsid__ *)param;
	uint64_t major, minor;
	char *endptr, *sp;
	int base;

	if (node->type != TYPE_TERM) {
		config_proc_error(node, err_type,
				  "Expected an FSID, got a %s",
				  (node->type == TYPE_ROOT
				   ? "root node"
				   : (node->type == TYPE_BLOCK
				      ? "block" : "statement")));
		goto errout;
	}
	if (node->u.term.type != TERM_FSID) {
		config_proc_error(node, err_type,
				  "Expected an FSID, got a %s",
				  config_term_desc(node->u.term.type));
		goto errout;
	}
	errno = 0;
	sp = node->u.term.varvalue;
	if (sp[0] == '0') {
		if (sp[1] == 'x' || sp[1] == 'X')
			base = 16;
		else
			base = 8;
	} else
		base = 10;
	major = strtoull(sp, &endptr, base);
	assert(*endptr == '.');
	if (errno != 0 || major == ULLONG_MAX) {
		config_proc_error(node, err_type,
				  "(%s) major is out of range",
				  node->u.term.varvalue);
		goto errout;
	}
	sp = endptr + 1;
	if (sp[0] == '0') {
		if (sp[1] == 'x' || sp[1] == 'X')
			base = 16;
		else
			base = 8;
	} else
		base = 10;
	minor = strtoull(sp, &endptr, base);
	assert(*endptr == '\0');
	if (errno != 0 || minor == ULLONG_MAX) {
		config_proc_error(node, err_type,
				  "(%s) minor is out of range",
				  node->u.term.varvalue);
		goto errout;
	}
	fsid->major = major;
	fsid->minor = minor;
	return true;

errout:
	err_type->invalid = true;
	err_type->errors++;
	return false;
}

/**
 * @brief Scan a list of CSV tokens.
 *
 */

static bool convert_list(struct config_node *node,
			 struct config_item *item,
			 uint32_t *flags,
			 struct config_error_type *err_type)
{
	struct config_item_list *tok;
	struct config_node *sub_node;
	struct glist_head *nsi, *nsn;
	bool found;
	int errors = 0;

	*flags = 0;
	glist_for_each_safe(nsi, nsn, &node->u.nterm.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		assert(sub_node->type == TYPE_TERM);
		found = false;
		for (tok = item->u.lst.tokens;
		     tok->token != NULL;
		     tok++) {
			if (strcasecmp(sub_node->u.term.varvalue,
				       tok->token) == 0) {
				*flags |= tok->value;
				found = true;
			}
		}
		if (!found) {
			config_proc_error(node, err_type,
					  "Unknown token (%s)",
					  sub_node->u.term.varvalue);
			err_type->bogus = true;
			errors++;
		}
	}
	err_type->errors += errors;
	return errors == 0;
}

static bool convert_enum(struct config_node *node,
			 struct config_item *item,
			 uint32_t *val,
			 struct config_error_type *err_type)
{
	struct config_item_list *tok;
	bool found;

	tok = item->u.lst.tokens;
	found = false;
	while (tok->token != NULL) {
		if (strcasecmp(node->u.term.varvalue, tok->token) == 0) {
			*val = tok->value;
			found = true;
		}
		tok++;
	}
	if (!found) {
		config_proc_error(node, err_type,
			 "Unknown token (%s)",
			 node->filename,
			 node->linenumber,
			 node->u.term.varvalue);
		err_type->bogus = true;
		err_type->errors++;
	}
	return found;
}

static void convert_inet_addr(struct config_node *node,
			     struct config_item *item,
			     sockaddr_t *sock,
			     struct config_error_type *err_type)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;

	if (node->u.term.type != TERM_V4ADDR &&
	    node->u.term.type != TERM_V4_ANY &&
	    node->u.term.type != TERM_V6ADDR) {
		config_proc_error(node, err_type,
				  "Expected an IP address, got a %s",
				  config_term_desc(node->u.term.type));
		err_type->invalid = true;
		err_type->errors++;
		return;
	}

	/* Try IPv6 (with mapping) first.  If this fails, fall back on IPv4, if
	 * a v4 address was given. */
	hints.ai_family = AF_INET6;
	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	rc = getaddrinfo(node->u.term.varvalue, NULL,
			 &hints, &res);

	if (rc != 0 && (node->u.term.type == TERM_V4ADDR ||
			node->u.term.type == TERM_V4_ANY)) {
		hints.ai_family = AF_INET;
		rc = getaddrinfo(node->u.term.varvalue, NULL,
				 &hints, &res);
	}
	if (rc == 0) {
		memcpy(sock, res->ai_addr, res->ai_addrlen);
	} else {
		config_proc_error(node, err_type,
				  "No IP address found for %s because:%s",
				  node->u.term.varvalue,
				  gai_strerror(rc));
		err_type->invalid = true;
		err_type->errors++;
	}
	if (res != NULL)
		freeaddrinfo(res);
	return;
}

/**
 * @brief Walk the term node list and call handler for each node
 *
 * This is effectively a callback on each token in the list.
 * Pass along a type hint based on what the parser recognized.
 *
 * @param node [IN] pointer to the statement node
 * @param item [IN] pointer to the config_item table entry
 * @param param_addr [IN] pointer to target struct member
 * @param err_type [OUT] error handling ref
 * @return number of errors
 */

static void do_proc(struct config_node *node,
		    struct config_item *item,
		    void *param_addr,
		    struct config_error_type *err_type)
{
	struct config_node *term_node;
	struct glist_head *nsi, *nsn;
	int rc = 0;

	assert(node->type == TYPE_STMT);
	glist_for_each_safe(nsi, nsn,
			    &node->u.nterm.sub_nodes) {
		term_node = glist_entry(nsi,
				       struct config_node,
				       node);
		rc += item->u.proc.handler(term_node->u.term.varvalue,
					   term_node->u.term.type,
					   item,
					   param_addr,
					   term_node,
					   err_type);
	}
	err_type->errors += rc;
}

/**
 * @brief Lookup the first node in the list by this name
 *
 * @param list - head of the glist
 * @param name - node name of interest
 *
 * @return first matching node or NULL
 */

static struct config_node *lookup_node(struct glist_head *list,
				       const char *name)
{
	struct config_node *node;
	struct glist_head *ns;
	
	glist_for_each(ns, list) {
		node = glist_entry(ns, struct config_node, node);
		assert(node->type == TYPE_BLOCK ||
		       node->type == TYPE_STMT);
		if (strcasecmp(name, node->u.nterm.name) == 0) {
			node->found = true;
			return node;
		}
	}
	return NULL;
}

/**
 * @brief Lookup the next node in list
 *
 * @param list - head of the glist
 * @param start - continue the lookup from here
 * @param name - node name of interest
 *
 * @return first matching node or NULL
 */


static struct config_node *lookup_next_node(struct glist_head *list,
				     struct glist_head *start,
				     const char *name)
{
	struct config_node *node;
	struct glist_head *ns;

	glist_for_each_next(start, ns, list) {
		node = glist_entry(ns, struct config_node, node);
		assert(node->type == TYPE_BLOCK ||
		       node->type == TYPE_STMT);
		if (strcasecmp(name, node->u.nterm.name) == 0) {
			node->found = true;
			return node;
		}
	}
	return NULL;
}

static const char *config_type_str(enum config_type type)
{
	switch(type) {
	case CONFIG_NULL:
		return "CONFIG_NULL";
	case CONFIG_INT16:
		return "CONFIG_INT16";
	case CONFIG_UINT16:
		return "CONFIG_UINT16";
	case CONFIG_INT32:
		return "CONFIG_INT32";
	case CONFIG_UINT32:
		return "CONFIG_UINT32";
	case CONFIG_INT64:
		return "CONFIG_INT64";
	case CONFIG_UINT64:
		return "CONFIG_UINT64";
	case CONFIG_FSID:
		return "CONFIG_FSID";
	case CONFIG_ANON_ID:
		return "CONFIG_ANON_ID";
	case CONFIG_STRING:
		return "CONFIG_STRING";
	case CONFIG_PATH:
		return "CONFIG_PATH";
	case CONFIG_LIST:
		return "CONFIG_LIST";
	case CONFIG_ENUM:
		return "CONFIG_ENUM";
	case CONFIG_TOKEN:
		return "CONFIG_TOKEN";
	case CONFIG_BOOL:
		return "CONFIG_BOOL";
	case CONFIG_BOOLBIT:
		return "CONFIG_BOOLBIT";
	case CONFIG_IP_ADDR:
		return "CONFIG_IP_ADDR";
	case CONFIG_BLOCK:
		return "CONFIG_BLOCK";
	case CONFIG_PROC:
		return "CONFIG_PROC";
	case CONFIG_DEPRECATED:
		return "CONFIG_DEPRECATED";
	}
	return "unknown";
}

static bool do_block_init(struct config_node *blk_node,
			  struct config_item *params,
			  void *param_struct,
			  struct config_error_type *err_type)
{
	struct config_item *item;
	void *param_addr;
	sockaddr_t *sock;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;
	int errors = 0;

	for (item = params; item->name != NULL; item++) {
		param_addr = ((char *)param_struct + item->off);
		LogFullDebug(COMPONENT_CONFIG,
			     "%p name=%s type=%s",
			     param_addr, item->name, config_type_str(item->type));
		switch (item->type) {
		case CONFIG_NULL:
			break;
		case CONFIG_INT16:
			*(int16_t *)param_addr = item->u.i16.def;
			break;
		case CONFIG_UINT16:
			*(uint16_t *)param_addr = item->u.ui16.def;
			break;
		case CONFIG_INT32:
			*(int32_t *)param_addr = item->u.i32.def;
			break;
		case CONFIG_UINT32:
			*(uint32_t *)param_addr = item->u.ui32.def;
			break;
		case CONFIG_INT64:
			*(int64_t *)param_addr = item->u.i64.def;
			break;
		case CONFIG_UINT64:
			*(uint64_t *)param_addr = item->u.ui64.def;
			break;
		case CONFIG_ANON_ID:
			*(uid_t *)param_addr = item->u.i64.def;
			break;
		case CONFIG_FSID:
			((struct fsal_fsid__ *)param_addr)->major
				= item->u.fsid.def_maj;
			((struct fsal_fsid__ *)param_addr)->minor
				= item->u.fsid.def_min;
			break;
		case CONFIG_STRING:
		case CONFIG_PATH:
			if (item->u.str.def)
				*(char **)param_addr
					= gsh_strdup(item->u.str.def);
			else
				*(char **)param_addr = NULL;
			break;
		case CONFIG_TOKEN:
			*(uint32_t *)param_addr = item->u.lst.def;
			break;
		case CONFIG_BOOL:
			*(bool *)param_addr = item->u.b.def;
			break;
		case CONFIG_BOOLBIT:
			if (item->u.bit.def)
				*(uint32_t *)param_addr |= item->u.bit.bit;
			else
				*(uint32_t *)param_addr &= ~item->u.bit.bit;
			break;
		case CONFIG_LIST:
			*(uint32_t *)param_addr |= item->u.lst.def;
			LogFullDebug(COMPONENT_CONFIG,
				     "%p CONFIG_LIST %s mask=%08x def=%08x"
				     " value=%08"PRIx32,
				     param_addr,
				     item->name,
				     item->u.lst.mask, item->u.lst.def,
				     *(uint32_t *)param_addr);
			break;
		case CONFIG_ENUM:
			*(uint32_t *)param_addr |= item->u.lst.def;
			LogFullDebug(COMPONENT_CONFIG,
				     "%p CONFIG_ENUM %s mask=%08x def=%08x"
				     " value=%08"PRIx32,
				     param_addr,
				     item->name,
				     item->u.lst.mask, item->u.lst.def,
				     *(uint32_t *)param_addr);
			break;
		case CONFIG_IP_ADDR:
			sock = (sockaddr_t *)param_addr;
			memset(sock, 0, sizeof(sockaddr_t));
			errno = 0;
			/* Try IPv6 (with mapping) first.  If this fails, fall
			 * back on IPv4, if a v4 address was given. */
			memset(&hints, 0, sizeof(struct addrinfo));
			hints.ai_family = AF_INET6;
			hints.ai_flags = AI_PASSIVE;
			hints.ai_socktype = 0;
			hints.ai_protocol = 0;
			/* We don't actually pass "0.0.0.0" to this, so that it
			 * gets the correct address for each address family.
			 * AI_PASSIVE assures this. */
			rc = getaddrinfo(NULL, "0", &hints, &res);

			if (rc != 0) {
				hints.ai_family = AF_INET;
				rc = getaddrinfo(NULL, "0", &hints, &res);
			}
			if (rc == 0) {
				memcpy(sock, res->ai_addr, res->ai_addrlen);
			} else {
				config_proc_error(blk_node, err_type,
						  "Cannot set IP default for %s to %s because %s",
						  item->name,
						  item->u.ip.def,
						  gai_strerror(rc));
				errors++;
			}
			if (res != NULL) {
				freeaddrinfo(res);
				res = NULL;
			}
			break;
		case CONFIG_BLOCK:
			(void) item->u.blk.init(NULL, param_addr);
			break;
		case CONFIG_PROC:
			(void) item->u.proc.init(NULL, param_addr);
			break;
		case CONFIG_DEPRECATED:
			break;
		default:
			config_proc_error(blk_node, err_type,
					  "Cannot set default for parameter %s, type(%d) yet",
					  item->name, item->type);
			errors++;
			break;
		}
	}
	err_type->errors += errors;
	return errors == 0;
}

/**
 * @brief This is the NOOP init for block and proc parsing
 *
 * @param link_mem [IN] pointer to member in referencing structure
 * @param self_struct [IN] pointer to space reserved/allocated for block
 *
 * @return a pointer depending on context
 */

void *noop_conf_init(void *link_mem, void *self_struct)
{
	assert(link_mem != NULL || self_struct != NULL);

	if (link_mem == NULL)
		return self_struct;
	else if (self_struct == NULL)
		return link_mem;
	else
		return NULL;
}

int noop_conf_commit(void *node, void *link_mem, void *self_struct,
		     struct config_error_type *err_type)
{
	return 0;
}

static bool proc_block(struct config_node *node,
		      struct config_item *item,
		      void *link_mem,
		      struct config_error_type *err_type);

/*
 * All the types of integers supported by the config processor
 */

union gen_int {
	bool b;
	int16_t i16;
	uint16_t ui16;
	int32_t i32;
	uint32_t ui32;
	int64_t i64;
	uint64_t ui64;
};

/**
 * @brief Process the defined tokens in the params table
 *
 * The order of the parameter table is important.  If any
 * parameter requires that another parameter in the table be
 * processed first, order the table appropriately.
 *
 * @param blk    [IN] a parse tree node of type CONFIG_BLOCK
 * @param params [IN] points to a NULL term'd table of config_item's
 * @param relax  [IN] if true, don't report unrecognized params.
 * @param param_struct [IN/OUT] the structure to be filled.
 *
 * @returns 0 on success, number of errors on failure.
 */

static int do_block_load(struct config_node *blk,
			 struct config_item *params,
			 bool relax,
			 void *param_struct,
			 struct config_error_type *err_type)
{
	struct config_item *item;
	void *param_addr;
	struct config_node *node, *term_node, *next_node = NULL;
	struct glist_head *ns;
	int errors = 0, prev_errors = err_type->errors;

	for (item = params; item->name != NULL; item++) {
		uint64_t num64;
		bool bool_val;
		uint32_t num32 = 0;

		node = lookup_node(&blk->u.nterm.sub_nodes, item->name);
		if ((item->flags & CONFIG_MANDATORY) && (node == NULL)) {
			err_type->missing = true;
			errors = ++err_type->errors;
			config_proc_error(blk, err_type,
					  "Mandatory field, %s is missing from block (%s)",
					  item->name, blk->u.nterm.name);
			continue;
		}
		while (node != NULL) {
			next_node = lookup_next_node(&blk->u.nterm.sub_nodes,
						     &node->node, item->name);
			if (next_node != NULL &&
			    (item->flags & CONFIG_UNIQUE)) {
				config_proc_error(next_node, err_type,
						  "Parameter %s set more than once",
						  next_node->u.nterm.name);
				err_type->unique = true;
				errors = ++err_type->errors;
				node = next_node;
				continue;
			}
			param_addr = ((char *)param_struct + item->off);
			LogFullDebug(COMPONENT_CONFIG,
				     "%p name=%s type=%s",
				     param_addr, item->name,
				     config_type_str(item->type));
			if (glist_empty(&node->u.nterm.sub_nodes)) {
				LogInfo(COMPONENT_CONFIG,
					"%s %s is empty",
					(node->type == TYPE_STMT
					 ? "Statement" : "Block"),
					node->u.nterm.name);
				node = next_node;
				continue;
			}
			term_node = glist_first_entry(&node->u.nterm.sub_nodes,
						      struct config_node,
						      node);
			if ((item->type != CONFIG_BLOCK &&
			     item->type != CONFIG_PROC &&
			     item->type != CONFIG_LIST) &&
			    glist_length(&node->u.nterm.sub_nodes) > 1) {
				config_proc_error(node, err_type,
						  "%s can have only one option.  First one is (%s)",
						  node->u.nterm.name,
						  term_node->u.term.varvalue);
				err_type->invalid = true;
				errors = ++err_type->errors;
				node = next_node;
				continue;
			}
			switch (item->type) {
			case CONFIG_NULL:
				break;
			case CONFIG_INT16:
				if (convert_number(term_node, item,
						   &num64, err_type))
					*(int16_t *)param_addr = num64;
				break;
			case CONFIG_UINT16:
				if (convert_number(term_node, item,
						   &num64, err_type))
					*(uint16_t *)param_addr	= num64;
				break;
			case CONFIG_INT32:
				if (convert_number(term_node, item,
						   &num64, err_type)) {
					*(int32_t *)param_addr = num64;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							 + item->u.i32.set_off);
						*(uint32_t *)mask_addr
							|= item->u.i32.bit;
					}
				}
				break;
			case CONFIG_UINT32:
				if (convert_number(term_node, item,
						   &num64, err_type))
					*(uint32_t *)param_addr	= num64;
				break;
			case CONFIG_INT64:
				if (convert_number(term_node, item,
						   &num64, err_type))
					*(int64_t *)param_addr = num64;
				break;
			case CONFIG_UINT64:
				if (convert_number(term_node, item,
						   &num64, err_type)) {
					*(uint64_t *)param_addr = num64;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
						((char *)param_struct
						 + item->u.ui64.set_off);
						*(uint32_t *)mask_addr
						|= item->u.ui64.bit;
					}
				}
				break;
			case CONFIG_ANON_ID:
				if (convert_number(term_node, item,
						   &num64, err_type)) {
					*(uid_t *)param_addr = num64;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							 + item->u.i64.set_off);
						*(uint32_t *)mask_addr
							|= item->u.i64.bit;
					}
				}
				break;
			case CONFIG_FSID:
				if (convert_fsid(term_node, param_addr,
						 err_type)) {
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							+ item->u.fsid.set_off);
						*(uint32_t *)mask_addr
							|= item->u.fsid.bit;
					}
				}
				break;
			case CONFIG_STRING:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				*(char **)param_addr =
					gsh_strdup(term_node->u.term.varvalue);
				break;
			case CONFIG_PATH:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				/** @todo validate path with access() */
				*(char **)param_addr =
					gsh_strdup(term_node->u.term.varvalue);
				break;
			case CONFIG_TOKEN:
				if (convert_enum(term_node, item, &num32,
						 err_type))
					*(uint32_t *)param_addr = num32;
				break;
			case CONFIG_BOOL:
				if (convert_bool(term_node, &bool_val,
						 err_type))
					*(bool *)param_addr = bool_val;
				break;
			case CONFIG_BOOLBIT:
				if (convert_bool(term_node, &bool_val,
						 err_type)) {
					if (bool_val)
						*(uint32_t *)param_addr
							|= item->u.bit.bit;
					else
						*(uint32_t *)param_addr
							&= ~item->u.bit.bit;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							+ item->u.bit.set_off);
						*(uint32_t *)mask_addr
							|= item->u.bit.bit;
					}	
				}
				break;
			case CONFIG_LIST:
				if (item->u.lst.def ==
				   (*(uint32_t *)param_addr & item->u.lst.mask))
					*(uint32_t *)param_addr &=
							~item->u.lst.mask;
				if (convert_list(node, item, &num32,
						 err_type)) {
					*(uint32_t *)param_addr |= num32;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							+ item->u.lst.set_off);
						*(uint32_t *)mask_addr
							|= item->u.lst.mask;
					}	
				}
				LogFullDebug(COMPONENT_CONFIG,
					     "%p CONFIG_LIST %s mask=%08x flags=%08x"
					     " value=%08"PRIx32,
					     param_addr,
					     item->name,
					     item->u.lst.mask, num32,
					     *(uint32_t *)param_addr);
				break;
			case CONFIG_ENUM:
				if (item->u.lst.def ==
				   (*(uint32_t *)param_addr & item->u.lst.mask))
					*(uint32_t *)param_addr &=
							~item->u.lst.mask;
				if (convert_enum(term_node, item, &num32,
						 err_type)) {
					*(uint32_t *)param_addr |= num32;
					if (item->flags & CONFIG_MARK_SET) {
						void *mask_addr;

						mask_addr =
							((char *)param_struct
							+ item->u.lst.set_off);
						*(uint32_t *)mask_addr
							|= item->u.lst.mask;
					}	
				}
				LogFullDebug(COMPONENT_CONFIG,
					     "%p CONFIG_ENUM %s mask=%08x flags=%08x"
					     " value=%08"PRIx32,
					     param_addr,
					     item->name,
					     item->u.lst.mask, num32,
					     *(uint32_t *)param_addr);
				break;
			case CONFIG_IP_ADDR:
				convert_inet_addr(term_node, item,
						  (sockaddr_t *)param_addr,
						  err_type);
				break;
			case CONFIG_BLOCK:
				if (!proc_block(node, item, param_addr,
						err_type))
					config_proc_error(node, err_type,
							  "Errors processing block (%s)",
							  node->u.nterm.name);
				break;
			case CONFIG_PROC:
				do_proc(node, item, param_addr,	err_type);
				break;
			case CONFIG_DEPRECATED:
				config_proc_error(node, err_type,
					"Deprecated parameter (%s)%s%s",
					item->name,
					item->u.deprecated.message
						? " - "
						: "",
					item->u.deprecated.message
						? item->u.deprecated.message
						: "");
				err_type->deprecated = true;
				errors++;
				break;
			default:
				config_proc_error(term_node, err_type,
						  "Cannot set value for type(%d) yet",
						  item->type);
				err_type->internal = true;
				errors = ++err_type->errors;
				break;
				}
			node = next_node;
		}
	}

	/* Check for any errors in parsing params.
	 * It will set to default value if parsing fails.
	 * For params like Export_Id if parsing fails, we
	 * will end up setting it to 1, which could cause issue
	 * if we set it to 1 for multiple exports.
	 */
	if (err_type->errors > prev_errors)
		errors = err_type->errors;

	if (relax)
		return errors;

	/* We've been marking config nodes as being "seen" during the
	 * scans.  Report the bogus and typo inflicted bits.
	 */
	glist_for_each(ns, &blk->u.nterm.sub_nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->found)
			node->found = false;
		else {
			config_proc_error(node, err_type,
					  "Unknown parameter (%s)",
					  node->u.nterm.name);
			err_type->bogus = true;
			errors++;
		}
	}
	return errors;
}

/**
 * @brief Process a block
 *
 * The item arg supplies two function pointers that
 * are defined as follows:
 *
 * init
 *  This function manages memory for the sub-block's processing.
 *  It has two arguments, a pointer to the link_mem param struct and
 *  a pointer to the self_struct param struct.
 *  If the self_struct argument is NULL, it returns a pointer to a usable
 *  self_struct param struct.  This can either be allocate memory
 *  or a pointer to existing memory.
 *
 *  If the self_struct argument is not NULL, it is the pointer it returned above.
 *  The function reverts whatever it did above.
 *
 * commit
 *  This function attaches the build param struct to its link_mem.
 *  Before it does the attach, it will do validation of input if required.
 *  Returns 0 if validation passes and the attach is successful.  Otherwise,
 *  it returns an error which will trigger a release of resources acquired
 *  by the self_struct_init.
 *
 * Both of these functions are called in the context of the link_mem parse.
 * It is assumed that the link_mem has already been initialized including
 * the init of any glists before this function is called.
 * The self_struct does its own init of things like glist in param_mem.
 *
 * @param node - parse node of the subblock
 * @param item - config_item describing block
 * @param link_mem - pointer to the link_mem structure
 * @param err_type [OUT] pointer to error type return
 *
 * @ return true on success, false on errors.
 */

static bool proc_block(struct config_node *node,
		      struct config_item *item,
		      void *link_mem,
		      struct config_error_type *err_type)
{
	void *param_struct;
	int errors = 0;

	assert(item->type == CONFIG_BLOCK);

	if (node->type != TYPE_BLOCK) {
		config_proc_error(node, err_type,
				  "%s is not a block!",
				  item->name);
		err_type->invalid = true;
		err_type->errors++;
		return false;
	}
	param_struct = item->u.blk.init(link_mem, NULL);
	if (param_struct == NULL) {
		config_proc_error(node, err_type,
				  "Could not init block for %s",
				  item->name);
		err_type->init = true;
		err_type->errors++;
		return false;
	}
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): do_block_init %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	if (!do_block_init(node, item->u.blk.params,
			   param_struct, err_type)) {
		config_proc_error(node, err_type,
				  "Could not initialize parameters for %s",
				  item->name);
		err_type->init = true;
		goto err_out;
	}
	if (item->u.blk.display != NULL)
		item->u.blk.display("DEFAULTS", node,
				      link_mem, param_struct);
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): do_block_load %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	errors = do_block_load(node,
			   item->u.blk.params,
			   (item->flags & CONFIG_RELAX) ? true : false,
			   param_struct, err_type);
	if (errors > 0 && !cur_exp_config_error_is_harmless(err_type)) {
		config_proc_error(node, err_type,
				  "%d errors while processing parameters for %s",
				  errors,
				  item->name);
		goto err_out;
	}
	if (item->u.blk.check && item->u.blk.check(param_struct, err_type)) {
		goto err_out;
	}
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): commit %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	errors = item->u.blk.commit(node, link_mem, param_struct, err_type);
	if (errors > 0 && !cur_exp_config_error_is_harmless(err_type)) {
		config_proc_error(node, err_type,
				  "%d validation errors in block %s",
				  errors,
				  item->name);
		goto err_out;
	}
	if (item->u.blk.display != NULL)
		item->u.blk.display("RESULT", node, link_mem, param_struct);

	if (err_type->dispose) {
		/* We had a config update case where this block must be
		 * disposed of. Need to clear the flag so the next config
		 * block processed gets a clear slate.
		 */
		LogFullDebug(COMPONENT_CONFIG,
			     "Releasing block %p/%p", link_mem, param_struct);
		(void)item->u.blk.init(link_mem, param_struct);
		err_type->dispose = false;
	}

	return true;

err_out:
	LogFullDebug(COMPONENT_CONFIG,
		     "Releasing block %p/%p", link_mem, param_struct);
	(void)item->u.blk.init(link_mem, param_struct);
	err_type->dispose = false;
	return false;
}

/**
 * @brief Find the root of the parse tree given a node.
 *
 * @param node [IN] pointer to a TYPE_BLOCK node.
 *
 * @return the root of the tree.  Errors are asserted.
 */

config_file_t get_parse_root(void *node)
{
	struct config_node *parent;
	struct config_root *root;

	parent = (struct config_node *)node;
	assert(parent->type == TYPE_BLOCK);
	while (parent->u.nterm.parent != NULL) {
		parent = parent->u.nterm.parent;
		assert(parent->type == TYPE_ROOT ||
		       parent->type == TYPE_BLOCK);
	}
	assert(parent->type == TYPE_ROOT);
	root = container_of(parent, struct config_root, root);
	return (config_file_t)root;
}

uint64_t get_config_generation(struct config_root *root)
{
	return root->generation;
}

uint64_t get_parse_root_generation(void *node)
{
	struct config_root *root = (struct config_root *)get_parse_root(node);

	return get_config_generation(root);
}

/**
 * @brief Data structures for walking parse trees
 *
 * These structures hold the result of the parse of a block
 * description string that is passed to find_config_nodes
 *
 * expr_parse is for blocks
 *
 * expr_parse_arg is for indexing/matching parameters.
 */

struct expr_parse_arg {
	char *name;
	char *value;
	struct expr_parse_arg *next;
};

struct expr_parse {
	char *name;
	struct expr_parse_arg *arg;
	struct expr_parse *next;
};

/**
 * @brief Skip 0 or more white space chars
 *
 * @return pointer to first non-white space char
 */

static inline char *skip_white(char *sp)
{
	while (isspace(*sp))
		sp++;
	return sp;
}

/**
 * @brief Find the end of a token, i.e. [A-Za-z0-9_]+
 *
 * @return pointer to first unmatched char.
 */

static inline char *end_of_token(char *sp)
{
	while (isalnum(*sp) || *sp == '_')
		sp++;
	return sp;
}

/**
 * @brief Find end of a value string.
 *
 * @return pointer to first white or syntax token
 */

static inline char *end_of_value(char *sp)
{
	while (*sp != '\0') {
		if (isspace(*sp) || *sp == ',' || *sp == ')' || *sp == '(')
			break;
		sp++;
	 }
	return sp;
}

/**
 * @brief Release storage for arg list
 */

static void free_expr_parse_arg(struct expr_parse_arg *argp)
{
	struct expr_parse_arg *nxtarg, *arg = NULL;

	if (argp == NULL)
		return;
	for (arg = argp; arg != NULL; arg = nxtarg) {
		nxtarg = arg->next;
		gsh_free(arg->name);
		gsh_free(arg->value);
		gsh_free(arg);
	}
}

/**
 * @brief Release storage for the expression parse tree
 */

static void free_expr_parse(struct expr_parse *snode)
{
	struct expr_parse *nxtnode, *node = NULL;

	if (snode == NULL)
		return;
	for (node = snode; node != NULL; node = nxtnode) {
		nxtnode = node->next;
		gsh_free(node->name);
		free_expr_parse_arg(node->arg);
		gsh_free(node);
	}
}

/**
 * @brief Parse "token = string" or "token1 = string1, ..."
 *
 * @param arg_str [IN] pointer to first char to parse
 * @param argp    [OUT] list of parsed expr_parse_arg
 *
 * @return pointer to first unmatching char or NULL on parse error
 */

static char *parse_args(char *arg_str, struct expr_parse_arg **argp)
{
	char *sp, *name, *val;
	int saved_char;
	struct expr_parse_arg *arg = NULL;

	sp = skip_white(arg_str);
	if (*sp == '\0')
		return NULL;
	name = sp;		/* name matches [A-Za-z_][A-Za-z0-9_]* */
	if (!(isalpha(*sp) || *sp == '_'))
		return NULL;
	sp = end_of_token(sp);
	if (isspace(*sp))
		*sp++ = '\0';
	sp = skip_white(sp);
	if (*sp != '=')
		return NULL;
	*sp++ = '\0';		/* name = ... */
	sp = skip_white(sp);
	if (*sp == '\0')
		return NULL;
	val = sp;
	sp = end_of_value(sp);
	if (*sp == '\0')
		return NULL;
	if (isspace(*sp)) {	/* name = val */
		*sp++ = '\0';
		sp = skip_white(sp);
	}
	if (*sp == '\0')
		return NULL;
	saved_char = *sp;
	*sp = '\0';
	arg = gsh_calloc(1, sizeof(struct expr_parse_arg));
	arg->name = gsh_strdup(name);
	arg->value = gsh_strdup(val);
	*argp = arg;
	if (saved_char != ',') {
		*sp = saved_char;
		return sp;
	}
	sp++;			/* name = val , ... */
	return parse_args(sp, &arg->next);
}

/**
 * @brief Parse "token ( .... )" and "token ( .... ) . token ( ... )"
 *
 * @param str   [IN] pointer to first char to be parsed
 * @param node  [OUT] reference pointer to returned expr_parse list
 *
 * @return pointer to first char after parse or NULL on errors
 */

static char *parse_block(char *str, struct expr_parse **node)
{
	char *sp, *name;
	struct expr_parse_arg *arg = NULL;
	struct expr_parse *new_node;

	sp = skip_white(str);
	if (*sp == '\0')
		return NULL;
	name = sp;		/* name matches [A-Za-z_][A-Za-z0-9_]* */
	if (!(isalpha(*sp) || *sp != '_'))
		return NULL;
	if (*sp == '_')
		sp++;
	sp = end_of_token(sp);
	if (isspace(*sp))
		*sp++ = '\0';
	sp = skip_white(sp);
	if (*sp != '(')
		return NULL;
	*sp++ = '\0';	/* name ( ... */
	sp = parse_args(sp, &arg);
	if (sp == NULL)
		goto errout;
	sp = skip_white(sp);
	if (*sp == ')') {	/* name ( ... ) */
		new_node = gsh_calloc(1, sizeof(struct expr_parse));
		new_node->name = gsh_strdup(name);
		new_node->arg = arg;
		*node = new_node;
		return sp + 1;
	}

errout:
	free_expr_parse_arg(arg);
	return NULL;
}

/**
 * @brief Parse a treewalk expression
 *
 * A full expression describes the path down a parse tree with
 * each node described as "block_name ( qualifiers )".
 * Each node in the path is a series of block name and qualifiers
 * separated by '.'.
 *
 * The parse errors are detected as either ret val == NULL
 * or *retval != '\0', i.e. pointing to a garbage char
 *
 * @param expr   [IN] pointer to the expression to be parsed
 * @param expr_node [OUT] pointer to expression parse tree
 *
 * @return pointer to first char past parse or NULL for errors.
 */

static char *parse_expr(char *expr, struct expr_parse **expr_node)
{
	char *sp;
	struct expr_parse *node = NULL, *prev_node = NULL, *root_node = NULL;
	char *lexpr = gsh_strdupa(expr);

	sp = lexpr;
	while (sp != NULL && *sp != '\0') {
		sp = parse_block(sp, &node);	/* block is name ( ... ) */
		if (root_node == NULL)
			root_node = node;
		else
			prev_node->next = node;
		prev_node = node;
		if (sp == NULL)
			break;
		sp = skip_white(sp);
		if (*sp == '.') {
			sp++;		/* boock '.' block ... */
		} else if (*sp != '\0') {
			sp = NULL;
			break;
		}
	}
	*expr_node = root_node;
	if (sp != NULL)
		return expr + (sp - lexpr);
	else
		return NULL;
}

static inline bool match_one_term(char *value, struct config_node *node)
{
	struct config_node *term_node;
	struct glist_head *ts;

	glist_for_each(ts, &node->u.nterm.sub_nodes) {
		term_node = glist_entry(ts, struct config_node,	node);
		assert(term_node->type == TYPE_TERM);
		if (strcasecmp(value, term_node->u.term.varvalue) == 0)
			return true;
	}
	return false;
}

/**
 * @brief Select block based on the evaluation of the qualifier args
 *
 * use each qualifier to match.  The token name is found in the block.
 * once found, the token value is compared.  '*' matches anything. else
 * it is a case-insensitive string compare.
 *
 * @param blk   [IN] pointer to config_node describing the block
 * @param expr  [IN] expr_parse node to match
 *
 * @return true if matched, else false
 */

static bool match_block(struct config_node *blk,
			struct expr_parse *expr)
{
	struct glist_head *ns;
	struct config_node *sub_node;
	struct expr_parse_arg *arg;
	bool found = false;

	assert(blk->type == TYPE_BLOCK);
	for (arg = expr->arg; arg != NULL; arg = arg->next) {
		glist_for_each(ns, &blk->u.nterm.sub_nodes) {
			sub_node = glist_entry(ns, struct config_node, node);
			if (sub_node->type == TYPE_STMT &&
			    strcasecmp(arg->name,
				       sub_node->u.nterm.name) == 0) {
				found = true;
				if (expr->next == NULL &&
				    strcasecmp(arg->value, "*") == 0)
					continue;
				if (!match_one_term(arg->value, sub_node))
					return false;
			}
		}
	}
	return found;
}

/**
 * @brief Find nodes in parse tree using expression
 *
 * Lookup signature describes the nested block it is of the form:
 * block_name '(' param_name '=' param_value ')' ...
 * where block_name and param_name are alphanumerics and param_value is
 * and arbitrary token.
 * This can name a subblock (the ...) part by a '.' sub-block_name...
 * as in:

 *   some_block(indexing_param = foo).the_subblock(its_index = baz)
 *
 * NOTE: This will return ENOENT not only if the the comparison
 * fails but also if the search cannot find the token. In other words,
 * the match succeeds only if there is a node in the parse tree and it
 * matches.
 *
 * @param config    [IN] root of parse tree
 * @param expr_str  [IN] expression description of block
 * @param node_list [OUT] pointer to store node list
 * @param err_type  [OUT] error processing
 *
 * @return 0 on success, errno for errors
 */

int find_config_nodes(config_file_t config, char *expr_str,
		      struct config_node_list **node_list,
		      struct config_error_type *err_type)
{
	struct config_root *tree = (struct config_root *)config;
	struct glist_head *ns;
	struct config_node *sub_node;
	struct config_node *top;
	struct expr_parse *expr, *expr_head = NULL;
	struct config_node_list *list = NULL, *list_tail = NULL;
	char *ep;
	int rc = EINVAL;
	bool found = false;

	if (tree->root.type != TYPE_ROOT) {
		config_proc_error(&tree->root, err_type,
				  "Expected to start at parse tree root for (%s)",
				  expr_str);
		goto out;
	}
	top = &tree->root;
	ep = parse_expr(expr_str, &expr_head);
	if (ep == NULL || *ep != '\0')
		goto out;
	expr = expr_head;
	*node_list = NULL;
again:
	glist_for_each(ns, &top->u.nterm.sub_nodes) {
#ifdef DS_ONLY_WAS
		/* recent changes to parsing may prevent this,
		 * but retain code here for future reference.
		 * -- WAS
		 */
		if (ns == NULL) {
			config_proc_error(top, err_type,
				 "Missing sub_node for (%s)",
				 expr_str);
			break;
		}
#endif
		sub_node = glist_entry(ns, struct config_node, node);
		if (strcasecmp(expr->name, sub_node->u.nterm.name) == 0 &&
		    sub_node->type == TYPE_BLOCK &&
		    match_block(sub_node, expr)) {
			if (expr->next != NULL) {
				top = sub_node;
				expr = expr->next;
				goto again;
			}
			list = gsh_calloc(1, sizeof(struct config_node_list));
			list->tree_node = sub_node;
			if (*node_list == NULL)
				*node_list = list;
			else
				list_tail->next = list;
			list_tail = list;
			found = true;
		}
	}
	if (found)
		rc = 0;
	else
		rc = ENOENT;
out:
	free_expr_parse(expr_head);
	return rc;
}

/**
 * @brief Fill configuration structure from a parse tree node
 *
 * If param == NULL, there is no link_mem and the self_struct storage
 * is allocated and attached to its destination dynamically.
 *
 * @param tree_node   [IN] A CONFIG_BLOCK node in the parse tree
 * @param conf_blk    [IN] pointer to configuration description
 * @param param       [IN] pointer to struct to fill or NULL
 * @param unique      [IN] bool if true, more than one is an error
 * @param err_type    [OUT] error type return
 *
 * @returns -1 on errors, 0 for success
 */

int load_config_from_node(void *tree_node,
			  struct config_block *conf_blk,
			  void *param,
			  bool unique,
			  struct config_error_type *err_type)
{
	struct config_node *node = (struct config_node *)tree_node;
	char *blkname = conf_blk->blk_desc.name;

	if (node == NULL) {
		config_proc_error(NULL, err_type,
				  "Missing tree_node for (%s)",
				  blkname);
		err_type->missing = true;
		return -1;
	}
	if (node->type == TYPE_BLOCK) {
		if (strcasecmp(node->u.nterm.name, blkname) != 0) {
			config_proc_error(node, err_type,
					  "Looking for block (%s), got (%s)",
					  blkname, node->u.nterm.name);
			err_type->invalid = true;
			err_type->errors++;
			return -1;
		}
	} else {
		config_proc_error(node, err_type,
				  "Unrecognized parse tree node type for block (%s)",
				  blkname);
		err_type->invalid = true;
		err_type->errors++;
		return -1;
	}
	if (!proc_block(node, &conf_blk->blk_desc, param, err_type)) {
		config_proc_error(node, err_type,
				  "Errors found in configuration block %s",
				  blkname);
		return -1;
	}
	return 0;
}

/**
 * @brief Fill configuration structure from parse tree
 *
 * If param == NULL, there is no link_mem and the self_struct storage
 * is allocated and attached to its destination dynamically.
 *
 * @param config   [IN] root of parse tree
 * @param conf_blk [IN] pointer to configuration description
 * @param param    [IN] pointer to struct to fill or NULL
 * @param unique   [IN] bool if true, more than one is an error
 * @param err_type [OUT] pointer to error type return
 *
 * @returns number of blocks found. -1 on errors, errors are in err_type
 */

int load_config_from_parse(config_file_t config,
			   struct config_block *conf_blk,
			   void *param,
			   bool unique,
			   struct config_error_type *err_type)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node = NULL;
	struct glist_head *ns;
	char *blkname = conf_blk->blk_desc.name;
	int found = 0;
	int prev_errs = err_type->errors;
	void *blk_mem = NULL;

	if (tree == NULL) {
		config_proc_error(NULL, err_type,
				  "Missing parse tree root for (%s)",
				  blkname);
		err_type->missing = true;
		return -1;
	}
	if (tree->root.type != TYPE_ROOT) {
		config_proc_error(&tree->root, err_type,
				  "Expected to start at parse tree root for (%s)",
				  blkname);
		err_type->internal = true;
		return -1;
	}
	if (param != NULL) {
		blk_mem = conf_blk->blk_desc.u.blk.init(NULL, param);
		if (blk_mem == NULL) {
			config_proc_error(&tree->root, err_type,
					  "Top level block init failed for (%s)",
					  blkname);
			err_type->internal = true;
			return -1;
		}
	}
	glist_for_each(ns, &tree->root.u.nterm.sub_nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->type == TYPE_BLOCK &&
		    strcasecmp(blkname, node->u.nterm.name) == 0) {
			if (found > 0 &&
			    (conf_blk->blk_desc.flags & CONFIG_UNIQUE)) {
				config_proc_error(node, err_type,
						  "Only one %s block allowed",
						  blkname);
			} else {
				/* Reset cur_exp_create_err which may be used
				 * if an EXPORT block is processed. */
				err_type->cur_exp_create_err = false;

				if (!proc_block(node,
						&conf_blk->blk_desc,
						blk_mem,
						err_type))
					config_proc_error(node, err_type,
							  "Errors processing block (%s)",
							  blkname);
				else
					found++;

				/* If EXPORT block was handled and export
				 * creation failed then set
				 * all_exp_create_err = true */
				if (strcmp(blkname, "EXPORT") == 0 &&
				    err_type->cur_exp_create_err == true) {
					err_type->all_exp_create_err = true;
				}
			}
		}
	}
	if (found == 0) {
		/* Found nothing but we have to do the allocate and init
		 * at least. Use a fake, not NULL link_mem */
		blk_mem = param != NULL ?
			param : conf_blk->blk_desc.u.blk.init((void *)~0UL,
							      NULL);
		assert(blk_mem != NULL);
		if (!do_block_init(&tree->root,
				   conf_blk->blk_desc.u.blk.params,
				   blk_mem, err_type)) {
			config_proc_error(&tree->root, err_type,
					  "Could not initialize defaults for block %s",
					  blkname);
			err_type->init = true;
		}
	}
	if (err_type->errors > prev_errs) {
		char *errstr = err_type_str(err_type);

		config_proc_error(node, err_type,
			 "%d %s errors found block %s",
			 err_type->errors - prev_errs,
			 errstr != NULL ? errstr : "unknown",
			 blkname);
		if (errstr != NULL)
			gsh_free(errstr);
	}
	return found;
}
