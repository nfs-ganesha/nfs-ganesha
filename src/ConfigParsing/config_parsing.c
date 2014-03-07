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
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "config_parsing.h"
#include "analyse.h"
#include "abstract_mem.h"
#include "conf_yacc.h"
#include "log.h"
#include "fsal_convert.h"

struct parser_state parser_state;

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 */
config_file_t config_ParseFile(char *file_path)
{

	struct parser_state *st = &parser_state;
	int rc;

	memset(st, 0, sizeof(struct parser_state));
	rc = ganeshun_yy_init_parser(file_path, st);
	if (rc) {
		return NULL;
	}
	rc = ganesha_yyparse(st);
	ganeshun_yylex_destroy(st->scanner);

	/* converts pointer to pointer */
	return rc ? NULL : (config_file_t) st->root_node;
}

/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char *config_GetErrorMsg()
{

	return "Help! Help! We're all gonna die!!!";

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
	free_parse_tree((struct config_root *)config);
	return;

}

static bool convert_bool(struct config_node *node,
			 bool *b)
{
	if (!strcasecmp(node->u.varvalue, "1") ||
	    !strcasecmp(node->u.varvalue, "yes") ||
	    !strcasecmp(node->u.varvalue, "true")) {
		*b = true;
		return true;
	}
	if (!strcasecmp(node->u.varvalue, "0") ||
	    !strcasecmp(node->u.varvalue, "no") ||
	    !strcasecmp(node->u.varvalue, "false")) {
		*b = false;
		return true;
	}
	LogMajor(COMPONENT_CONFIG,
		 "At (%s:%d): %s (%s) should be 'true' or 'false'",
		 node->filename,
		 node->linenumber,
		 node->name,
		 node->u.varvalue);
	return false;
}

static bool convert_int(struct config_node *node,
			int64_t min, int64_t max,
			int64_t *num)
{
	int64_t val;
	char *endptr;

	errno = 0;
	val = strtoll(node->u.varvalue, &endptr, 10);
	if (*node->u.varvalue != '\0' && *endptr == '\0') {
		if (errno != 0 || val < min || val > max) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) is out of range",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			return false;
		}
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s (%s) is not an integer",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		return false;
	}
	*num = val;
	return true;
}

static bool convert_uint(struct config_node *node,
			 uint64_t min, uint64_t max,
			 uint64_t *num)
{
	uint64_t val;
	char *endptr;

	errno = 0;
	val = strtoull(node->u.varvalue, &endptr, 10);
	if (*node->u.varvalue != '\0' && *endptr == '\0') {
		if (errno != 0 || val < min || val > max) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) is out of range",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			return false;
		}
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s (%s) is not an integer",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		return false;
	}
	*num = val;
	return true;
}

/**
 * @brief convert an fsid which is a "<64bit num>.<64bit num"
 */

static int convert_fsid(struct config_node *node, void *param)
{
	struct fsal_fsid__ *fsid = (struct fsal_fsid__ *)param;
	uint64_t major, minor;
	char *endptr, *sp;
	int errors = 0;

	errno = 0;
	major = strtoull(node->u.varvalue, &endptr, 10);
	if (*node->u.varvalue != '\0' && *endptr == '.') {
		if (errno != 0 || major < 0 || major >= UINT64_MAX) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) major is out of range",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			errors++;
		}
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s (%s) is not a filesystem id",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		errors++;
	}
	if (errors == 0) {
		sp = endptr + 1;
		minor = strtoull(sp, &endptr, 10);
		if (*sp != '\0' && *endptr == '\0') {
			if (errno != 0 || minor < 0 || minor >= UINT64_MAX) {
				LogMajor(COMPONENT_CONFIG,
					 "At (%s:%d): %s (%s) minor is out of range",
					 node->filename,
					 node->linenumber,
					 node->name,
					 node->u.varvalue);
				errors++;
			}
		} else {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s (%s) is not a filesystem id",
				 node->filename,
				 node->linenumber,
				 node->name,
				 node->u.varvalue);
			errors++;
		}
	}
	if (errors == 0) {
		fsid->major = major;
		fsid->minor = minor;
	}
	return errors;
}

/**
 * @brief Scan a list of CSV tokens.
 *
 * tokenize the CSV list here but  move to parser!
 */

static int convert_list(struct config_node *node,
			struct config_item *item,
			uint32_t *flags)
{
	struct config_item_list *tok;
	char *csv_list = alloca(strlen(node->u.varvalue) + 1);
	char *sp, *cp, *ep;
	bool found;
	int errors = 0;

	*flags = 0;
	strcpy(csv_list, node->u.varvalue);
	sp = csv_list;
	ep = sp + strlen(sp);
	while (sp < ep) {
		cp = index(sp, ',');
		if (cp != NULL)
			*cp++ = '\0';
		else
			cp = ep;
		tok = item->u.lst.tokens;
		found = false;
		while (tok->token != NULL) {
			if (strcasecmp(sp, tok->token) == 0) {
				*flags |= tok->value;
				found = true;
			}
			tok++;
		}
		if (!found) {
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): %s has unknown token (%s)",
				 node->filename,
				 node->linenumber,
				 node->name,
				 sp);
			errors++;
		}
		sp = cp;
	}
	return errors;
}

static int convert_enum(struct config_node *node,
			struct config_item *item,
			uint32_t *val)
{
	struct config_item_list *tok;
	bool found;
	int errors = 0;

	tok = item->u.lst.tokens;
	found = false;
	while (tok->token != NULL) {
		if (strcasecmp(node->u.varvalue, tok->token) == 0) {
			*val = tok->value;
			found = true;
		}
		tok++;
	}
	if (!found) {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %s has unknown token (%s)",
			 node->filename,
			 node->linenumber,
			 node->name,
			 node->u.varvalue);
		errors++;
	}
	return errors;
}

static int convert_inet_addr(struct config_node *node,
			     struct config_item *item,
			     int ai_family,
			     struct sockaddr *sock)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int rc;

	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_family = ai_family;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	rc = getaddrinfo(node->u.varvalue, NULL,
			 &hints, &res);
	if (rc == 0) {
		memcpy(sock, res->ai_addr, res->ai_addrlen);
		if (res->ai_next != NULL)
			LogInfo(COMPONENT_CONFIG,
				"At (%s:%d): Multiple addresses for %s = %s",
				node->filename,
				node->linenumber,
				node->name,
				node->u.varvalue);
	} else {
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): No IP address found for %s = %s because:%s",
			 node->filename,
			 node->linenumber,
			 item->name, node->u.varvalue,
			 gai_strerror(rc));
	}
	freeaddrinfo(res);
	return rc;
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
		if (strcasecmp(name, node->name) == 0) {
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
		if (strcasecmp(name, node->name) == 0) {
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
	case CONFIG_IPV4_ADDR:
		return "CONFIG_IPV4_ADDR";
	case CONFIG_IPV6_ADDR:
		return "CONFIG_IPV6_ADDR";
	case CONFIG_INET_PORT:
		return "CONFIG_INET_PORT";
	case CONFIG_BLOCK:
		return "CONFIG_BLOCK";
	case CONFIG_PROC:
		return "CONFIG_PROC";
	}
	return "unknown";
}

static int do_block_init(struct config_item *params,
			 void *param_struct)
{
	struct config_item *item;
	caddr_t *param_addr;
	struct sockaddr_in *sock;
	struct sockaddr_in6 *sock6;
	int rc;
	int errors = 0;

	for (item = params; item->name != NULL; item++) {
		param_addr = (caddr_t *)((uint64_t)param_struct + item->off);
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
		case CONFIG_IPV4_ADDR:
			sock = (struct sockaddr_in *)param_addr;
			memset(&sock->sin_addr, 0, sizeof(struct in_addr));
			sock->sin_family = AF_INET;
			rc = inet_pton(AF_INET,
				       item->u.ipv4.def, &sock->sin_addr);
			if (rc <= 0) {
				LogWarn(COMPONENT_CONFIG,
					"Cannot set IPv4 default for %s to %s",
					item->name, item->u.ipv4.def);
				errors++;
			}
			break;
		case CONFIG_IPV6_ADDR:
			sock6 = (struct sockaddr_in6 *)param_addr;
			memset(&sock6->sin6_addr, 0, sizeof(struct in6_addr));
			sock6->sin6_family = AF_INET6;
			rc = inet_pton(AF_INET6,
				       item->u.ipv6.def, &sock6->sin6_addr);
			if (rc <= 0) {
				LogWarn(COMPONENT_CONFIG,
					"Cannot set IPv4 default for %s to %s",
					item->name, item->u.ipv6.def);
				errors++;
			}
			break;
		case CONFIG_INET_PORT:
			*(uint16_t *)param_addr = htons(item->u.ui16.def);
			break;
		case CONFIG_BLOCK:
			(void) item->u.blk.init(NULL, param_addr); /* do parent init */
			break;
		default:
			LogCrit(COMPONENT_CONFIG,
				"Cannot set default for parameter %s, type(%d) yet",
				item->name, item->type);
			errors++;
			break;
		}
	}
	return errors;
}

/**
 * @brief This is the NOOP init for block parsing
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

int noop_conf_commit(void *node, void *link_mem, void *self_struct)
{
	return 0;
}

static int proc_block(struct config_node *node,
		       struct config_item *item,
		      void *link_mem);

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
			 void *param_struct)
{
	struct config_item *item;
	caddr_t *param_addr;
	struct sockaddr *sock;
	struct config_node *node, *next_node = NULL;
	struct glist_head *ns;
	int rc;
	int errors = 0;

	for (item = params; item->name != NULL; item++) {
		int64_t val;
		uint64_t uval;
		uint32_t flags;
		bool bval;

		node = lookup_node(&blk->u.blk.sub_nodes, item->name);
		while (node != NULL) {
			next_node = lookup_next_node(&blk->u.blk.sub_nodes,
						     &node->node, item->name);
			if (next_node != NULL &&
			    (item->flags & CONFIG_UNIQUE)) {
				LogMajor(COMPONENT_CONFIG,
					 "At (%s:%d): Parameter %s set more than once",
					 next_node->filename,
					 next_node->linenumber,
					 next_node->name);
				errors++;
				node = next_node;
				continue;
			}
			param_addr = (caddr_t *)((uint64_t)param_struct
						 + item->off);
			LogFullDebug(COMPONENT_CONFIG,
				     "%p name=%s type=%s",
				     param_addr, item->name, config_type_str(item->type));
			switch (item->type) {
			case CONFIG_NULL:
				break;
			case CONFIG_INT16:
				if (convert_int(node, item->u.i16.minval,
						item->u.i16.maxval,
						&val))
					*(int16_t *)param_addr = (int16_t)val;
				else
					errors++;
				break;
			case CONFIG_UINT16:
				if (convert_uint(node, item->u.ui16.minval,
						 item->u.ui16.maxval,
						 &uval))
					*(uint16_t *)param_addr
						= (uint16_t)uval;
				else
					errors++;
				break;
			case CONFIG_INT32:
				if (convert_int(node, item->u.i32.minval,
						item->u.i32.maxval,
						&val))
					*(int32_t *)param_addr = (int32_t)val;
				else
					errors++;
				break;
			case CONFIG_UINT32:
				if (convert_uint(node, item->u.ui32.minval,
						 item->u.ui32.maxval,
						 &uval)) {
					if (item->flags & CONFIG_MODE)
						uval = unix2fsal_mode(uval);
					*(uint32_t *)param_addr
						= (uint32_t)uval;
				} else
					errors++;
				break;
			case CONFIG_INT64:
				if (convert_int(node, item->u.i64.minval,
						item->u.i64.maxval,
						&val))
					*(int64_t *)param_addr = val;
				else
					errors++;
				break;
			case CONFIG_UINT64:
				if (convert_uint(node, item->u.ui64.minval,
						 item->u.ui64.maxval,
						 &uval))
					*(uint64_t *)param_addr = uval;
				else
					errors++;
				break;
			case CONFIG_FSID:
				rc = convert_fsid(node, param_addr);
				if (rc == 0) {
					if (item->u.fsid.set_off < UINT32_MAX) {
						caddr_t *mask_addr;
						mask_addr = (caddr_t *)
							((uint64_t)param_struct
							+ item->u.fsid.set_off);
						*(uint32_t *)mask_addr
							|= item->u.fsid.bit;
					}
				} else
					errors += rc;
				break;
			case CONFIG_STRING:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				*(char **)param_addr =
					gsh_strdup(node->u.varvalue);
				break;
			case CONFIG_PATH:
				if (*(char **)param_addr != NULL)
					gsh_free(*(char **)param_addr);
				/** @todo validate path with access() */
				*(char **)param_addr =
					gsh_strdup(node->u.varvalue);
				break;
			case CONFIG_BOOL:
				if (convert_bool(node, &bval))
					*(bool *)param_addr = bval;
				else
					errors++;
				break;
			case CONFIG_BOOLBIT:
				if (convert_bool(node, &bval)) {
					if (bval)
						*(uint32_t *)param_addr
							|= item->u.bit.bit;
					else
						*(uint32_t *)param_addr
							&= ~item->u.bit.bit;
					if (item->u.bit.set_off < UINT32_MAX) {
						caddr_t *mask_addr;
						mask_addr = (caddr_t *)
							((uint64_t)param_struct
							+ item->u.bit.set_off);
						*(uint32_t *)mask_addr
							|= item->u.bit.bit;
					}	
				} else
					errors++;
				break;
			case CONFIG_LIST:
				if (item->u.lst.def ==
				   (*(uint32_t *)param_addr & item->u.lst.mask))
					*(uint32_t *)param_addr &=
							~item->u.lst.mask;
				rc = convert_list(node, item, &flags);
				if (rc == 0) {
					*(uint32_t *)param_addr |= flags;
					if (item->u.lst.set_off < UINT32_MAX) {
						caddr_t *mask_addr;
						mask_addr = (caddr_t *)
							((uint64_t)param_struct
							+ item->u.lst.set_off);
						*(uint32_t *)mask_addr
							|= item->u.lst.mask;
					}	
				} else
					errors += rc;
				LogFullDebug(COMPONENT_CONFIG,
					     "%p CONFIG_LIST %s mask=%08x flags=%08x"
					     " value=%08"PRIx32,
					     param_addr,
					     item->name,
					     item->u.lst.mask, flags,
					     *(uint32_t *)param_addr);
				break;
			case CONFIG_ENUM:
				if (item->u.lst.def ==
				   (*(uint32_t *)param_addr & item->u.lst.mask))
					*(uint32_t *)param_addr &=
							~item->u.lst.mask;
				rc = convert_enum(node, item, &flags);
				if (rc == 0) {
					*(uint32_t *)param_addr |= flags;
					if (item->u.lst.set_off < UINT32_MAX) {
						caddr_t *mask_addr;
						mask_addr = (caddr_t *)
							((uint64_t)param_struct
							+ item->u.lst.set_off);
						*(uint32_t *)mask_addr
							|= item->u.lst.mask;
					}	
				} else
					errors += rc;
				LogFullDebug(COMPONENT_CONFIG,
					     "%p CONFIG_ENUM %s mask=%08x flags=%08x"
					     " value=%08"PRIx32,
					     param_addr,
					     item->name,
					     item->u.lst.mask, flags,
					     *(uint32_t *)param_addr);
				break;
			case CONFIG_IPV4_ADDR:
				sock = (struct sockaddr *)param_addr;

				rc = convert_inet_addr(node, item,
						       AF_INET, sock);
				if (rc != 0)
					errors++;
				break;
			case CONFIG_IPV6_ADDR:
				sock = (struct sockaddr *)param_addr;

				rc = convert_inet_addr(node, item,
						       AF_INET6, sock);
				if (rc != 0)
					errors++;
				break;
			case CONFIG_INET_PORT:
				if (convert_uint(node, item->u.ui16.minval,
						 item->u.ui16.maxval,
						 &uval))
					*(uint16_t *)param_addr =
						htons((uint16_t)uval);
				else
					errors++;
				break;
			case CONFIG_BLOCK:
				rc = proc_block(node, item, param_addr);
				if (rc != 0)
					errors++;
				break;
			default:
				LogCrit(COMPONENT_CONFIG,
					"Cannot set value for type(%d) yet",
					item->type);
				break;
			}
			node = next_node;
		}
	}
	if (relax)
		return errors;

	/* We've been marking config nodes as being "seen" during the
	 * scans.  Report the bogus and typo inflicted bits.
	 */
	glist_for_each(ns, &blk->u.blk.sub_nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->found)
			node->found = false;
		else
			LogMajor(COMPONENT_CONFIG,
				 "At (%s:%d): Unknown parameter (%s)",
				 node->filename,
				 node->linenumber,
				 node->name);
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
 *  self_struct param struct.  This can either be allocate memory or a pointer to
 *  existing memory.
 *
 *  If the self_struct argument is not NULL, it is the pointer it returned above.
 *  The function reverts whatever it did above.
 *
 * commit
 *  This function attaches the build param struct to its link_mem.
 *  Before it does the attach, it will do validation of input if required.
 *  Returns 0 if validation passes and the attach is successful.  Otherwise,
 *  it returns an error which will trigger a release of resourcs acquired
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
 *
 * @ return 0 on success, non-zero on errors
 */

static int proc_block(struct config_node *node,
		      struct config_item *item,
		      void *link_mem)
{
	void *param_struct;
	int rc = 0;

	assert(item->type == CONFIG_BLOCK);

	if (node->type != TYPE_BLOCK) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): %s is not a block!",
			 node->filename,
			 node->linenumber,
			item->name);
		return 1;
	}
	param_struct = item->u.blk.init(link_mem, NULL);
	if (param_struct == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not init block for %s",
			 node->filename,
			 node->linenumber,
			item->name);
		return 1;
	}
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): do_block_init %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	rc = do_block_init(item->u.blk.params, param_struct);
	if (rc != 0) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not initialize parameters for %s",
			node->filename,
			node->linenumber,
			item->name);
		goto err_out;
	}
	if (item->u.blk.display != NULL)
		item->u.blk.display("DEFAULTS", node, link_mem, param_struct);
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): do_block_load %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	rc = do_block_load(node,
			   item->u.blk.params,
			   (item->flags & CONFIG_RELAX) ? true : false,
			   param_struct);
	if (rc != 0) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Could not process parameters for %s",
			node->filename,
			node->linenumber,
			item->name);
		goto err_out;
	}
	LogFullDebug(COMPONENT_CONFIG,
		     "------ At (%s:%d): commit %s",
		     node->filename,
		     node->linenumber,
		     item->name);
	rc = item->u.blk.commit(node, link_mem, param_struct);
	if (rc != 0) {
		LogCrit(COMPONENT_CONFIG,
			"At (%s:%d): Validation of block %s failed",
			node->filename,
			node->linenumber,
			item->name);
		goto err_out;
	}
	if (item->u.blk.display != NULL)
		item->u.blk.display("RESULT", node, link_mem, param_struct);
	return 0;

err_out:
	(void)item->u.blk.init(link_mem, param_struct);
	return 1;
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
	while (parent->u.blk.parent != NULL) {
		parent = parent->u.blk.parent;
		assert(parent->type == TYPE_ROOT ||
		       parent->type == TYPE_BLOCK);
	}
	assert(parent->type == TYPE_ROOT);
	root = container_of(parent, struct config_root, root);
	return (config_file_t)root;
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
 *
 * @returns -1 on errors, 0 for success
 */

int load_config_from_node(void *tree_node,
			  struct config_block *conf_blk,
			  void *param,
			  bool unique)
{
	struct config_node *node = (struct config_node *)tree_node;
	char *blkname = conf_blk->blk_desc.name;
	int rc;

	if (node->type == TYPE_BLOCK) {
		if (strcasecmp(node->name, blkname) != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Looking for block (%s), got (%s)",
				blkname, node->name);
			return -1;
		}
	} else {
		LogCrit(COMPONENT_CONFIG,
			"Unrecognized parse tree node type for block (%s)",
			blkname);
		return -1;
	}
	rc = proc_block(node,
			&conf_blk->blk_desc,
			param);
	if (rc != 0) {
		char *file;
		int lineno;

		if (node->filename != NULL) {
			file = node->filename;
			lineno = node->linenumber;
		} else {
			file = "<unknown file>";
			lineno = 0;
		}			
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %d errors found in configuration block %s",
			 file, lineno,
			 rc, blkname);
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
 *
 * @returns -1 on errors, number of blocks found )(0 == none)
 */

int load_config_from_parse(config_file_t config,
			   struct config_block *conf_blk,
			   void *param,
			   bool unique)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node = NULL;
	struct glist_head *ns;
	char *blkname = conf_blk->blk_desc.name;
	int found = 0;
	int rc, cum_errs = 0;
	void *blk_mem = NULL;

	if (tree->root.type != TYPE_ROOT) {
		LogInfo(COMPONENT_CONFIG,
			"Expected to start at parse tree root for (%s)",
			blkname);
		return -1;
	}
	if (param != NULL) {
		blk_mem = conf_blk->blk_desc.u.blk.init(NULL, param);
		if (blk_mem == NULL) {
			LogMajor(COMPONENT_CONFIG,
				 "Top level block init failed for %s",
				 blkname);
			return -1;
		}
	}
	glist_for_each(ns, &tree->root.u.blk.sub_nodes) {
		node = glist_entry(ns, struct config_node, node);
		if (node->type == TYPE_BLOCK &&
		    strcasecmp(blkname, node->name) == 0) {
			if (found > 0 &&
			    (conf_blk->blk_desc.flags & CONFIG_UNIQUE)) {
				LogWarn(COMPONENT_CONFIG,
					"At (%s:%d): Only one %s block allowed",
					node->filename,
					node->linenumber,
					blkname);
			} else {
				found++;
				rc = proc_block(node,
						&conf_blk->blk_desc,
						blk_mem);
				if (rc != 0)
					cum_errs += rc;
			}
		}
	}
	if (param != NULL && found == 0) {
		LogWarn(COMPONENT_CONFIG,
			 "Block %s not found. Using defaults", blkname);
		rc = do_block_init(conf_blk->blk_desc.u.blk.params,
				   blk_mem);
		if (rc != 0) {
			LogCrit(COMPONENT_CONFIG,
				"Could not initialize defaults for block %s",
				blkname);
			cum_errs += rc;
		}
	}
	if (cum_errs != 0) {
		char *file;
		int lineno;

		if (node != NULL) {
			file = node->filename;
			lineno = node->linenumber;
		} else {
			file = "<unknown file>";
			lineno = 0;
		}			
		LogMajor(COMPONENT_CONFIG,
			 "At (%s:%d): %d errors found in configuration block %s",
			 file, lineno,
			 cum_errs, blkname);
		return -1;
	}
	return found;
}

/**
 * config_GetNbBlocks:
 * Indicates how many blocks are defined into the config file.
 */
int config_GetNbBlocks(config_file_t config)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->root.node))
		return 0;
	glist_for_each_safe(nsi, nsn, &tree->root.node) {
		node = glist_entry(nsi, struct config_node, node);
		if (node->type == TYPE_BLOCK)
			bcnt++;
		else
			scnt++;
	}
	return bcnt + scnt;
}

/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex(config_file_t config,
				     unsigned int block_no)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&tree->root.node))
		return NULL;
	glist_for_each_safe(nsi, nsn, &tree->root.node) {
		node = glist_entry(nsi, struct config_node, node);
		if (/* node->type == TYPE_BLOCK && */ block_no == cnt)
			return (config_item_t)node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* Return the name of a block */
char *config_GetBlockName(config_item_t block)
{
	struct config_node *curr_block = (struct config_node *) block;

	assert(curr_block->type == TYPE_BLOCK);

	return curr_block->name;
}

/* Indicates how many items are defines in a block */
int config_GetNbItems(config_item_t block)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int bcnt = 0, scnt = 0;
	struct glist_head *nsi, *nsn;

	assert(node->type == TYPE_BLOCK);
	if (glist_empty(&node->u.blk.sub_nodes))
		return 0;
	glist_for_each_safe(nsi, nsn, &node->u.blk.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (sub_node->type == TYPE_STMT)
			scnt++;
		else
			bcnt++;
	}
	return scnt + bcnt;
}

/* retrieves a given block from the config file, from its index
 */
config_item_t config_GetItemByIndex(config_item_t block,
				    unsigned int item_no)
{
	struct config_node *node = (struct config_node *)block;
	struct config_node *sub_node;
	int cnt = 0;
	struct glist_head *nsi, *nsn;

	if (glist_empty(&node->u.blk.sub_nodes))
		return NULL;
	glist_for_each_safe(nsi, nsn, &node->u.blk.sub_nodes) {
		sub_node = glist_entry(nsi, struct config_node, node);
		if (/* sub_node->type == TYPE_STMT &&  */item_no == cnt)
			return (config_item_t)sub_node;
		cnt++;
	}
	/* not found */
	return NULL;
}

/* indicates which type of item it is */
config_item_type config_ItemType(config_item_t item)
{
	struct config_node *node = (struct config_node *)item;

	if (node->type == TYPE_BLOCK)
		return CONFIG_ITEM_BLOCK;
	else if (node->type == TYPE_STMT)
		return CONFIG_ITEM_VAR;
	else
		return 0;
}

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(config_item_t item, char **var_name, char **var_value)
{
	struct config_node *node = (struct config_node *)item;

	assert(node->type == TYPE_STMT);

	*var_name = node->name;
	*var_value = node->u.varvalue;

	return 0;
}

static config_item_t find_by_name(struct config_node *node, char *name)
{
	struct glist_head *nsi, *nsn;
	char *separ;
	config_item_t found_item = NULL;

	if (node->type != TYPE_BLOCK || glist_empty(&node->u.blk.sub_nodes))
		return NULL;
	separ = strstr(name, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &node->u.blk.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, name) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	return found_item;
}

config_item_t config_FindItemByName(config_file_t config, const char *name)
{
	struct config_root *tree = (struct config_root *)config;
	struct config_node *node, *top_node;
	struct glist_head *nsi, *nsn;
	char *separ, *tmpname, *current;
	config_item_t found_item = NULL;

	assert(tree->root.type == TYPE_ROOT || tree->root.type == TYPE_BLOCK);
	top_node = &tree->root;
	if (glist_empty(&top_node->u.blk.sub_nodes))
		return NULL;
	tmpname = gsh_strdup(name);
	current = tmpname;
	separ = strstr(current, "::");
	if (separ != NULL) {
		*separ++ = '\0';
		*separ++ = '\0';
	}
	glist_for_each_safe(nsi, nsn, &top_node->u.blk.sub_nodes) {
		node = glist_entry(nsi, struct config_node, node);
		if (strcasecmp(node->name, current) == 0) {
			if (separ == NULL)
				found_item = (config_item_t)node;
			else
				found_item = find_by_name(node, separ);
			break;
		}
	}
	gsh_free(tmpname);
	return found_item;
}

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char *config_FindKeyValueByName(config_file_t config, const char *key_name)
{
	struct config_node *node;

	node = (struct config_node *) config_FindItemByName(config, key_name);

	assert(node->type == TYPE_STMT);
	return node->u.varvalue;

}

/* Directly returns the value of the key with the specified name
 * relative to the given block.  this is bad.  will segv on no name...
 */
char *config_GetKeyValueByName(config_item_t block, const char *key_name)
{
	struct config_node *node = (struct config_node *)block;
	char *name;

	name = gsh_strdup(key_name);
	if (name == NULL)
		return NULL;
	node = (struct config_node *)find_by_name(node, name);
	gsh_free(name);
	assert(node->type == TYPE_STMT);
	return node->u.varvalue;
}
