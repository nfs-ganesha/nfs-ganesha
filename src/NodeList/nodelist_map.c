#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include "abstract_mem.h"
#include "nodelist.h"


#define MAX_LONG_INT_STRING_SIZE      128

/* Forward declaration */
int _nl_common_string_get_token_common(char *string,
				       char *separators_list,
				       int *p_token_nb, int token_id,
				       char **p_token);

int nl_map(nl_nl_t *nodelist,
	  int (*map_function)(char *, void *),
	  void *other_params)
{

	int fstatus = 0;

	nl_nl_t *nlist;

	char *node_string;
	size_t node_string_size;


	long int i, j;

	char id_print_format[128];

	char *prefix;
	char *suffix;

	/* node list sublist loop */
	nlist = nodelist;
	while (nlist != NULL) {

		/* build sublist padded id format */
		prefix = nlist->pattern.prefix;
		suffix = nlist->pattern.suffix;
		snprintf(id_print_format, 128, "%%s%%0.%uu%%s",
			 nlist->pattern.padding);

		node_string_size = 0;
		if (prefix != NULL)
			node_string_size += strlen(prefix);
		if (suffix != NULL)
			node_string_size += strlen(suffix);
		node_string_size += MAX_LONG_INT_STRING_SIZE;
		node_string =
		    gsh_malloc(node_string_size * sizeof(char));
		if (node_string != NULL) {

			if (nlist->pattern.basic == 1) {
				/* add basic node */
				snprintf(node_string, node_string_size,
					 "%s%s",
					 (prefix == NULL) ? "" : prefix,
					 (suffix ==
					  NULL) ? "" : suffix);
				fstatus = map_function(node_string,
						       other_params);
			} else {
				/* add enumerated nodes */
				for (i = 0;
				     i < nlist->rangelist.ranges_nb;
				     i++) {
					for (j =
					     nlist->rangelist.array[i].
					     from;
					     j <=
					     nlist->rangelist.array[i].
					     to; j++) {
						snprintf(node_string,
							 node_string_size,
							 id_print_format,
							 (prefix ==
							  NULL) ? "" :
							 prefix, j,
							 (suffix ==
							  NULL) ? "" :
							 suffix);
						fstatus = map_function(
								node_string,
								other_params);
					}
				}
			}

			gsh_free(node_string);
		}
		if (fstatus != 0)
			break;

		nlist = nlist->next;
	}

	return fstatus;
}


int nl_map_condensed(char *src_list,
		     int (*map_function)(char *, void *),
		     void *other_params)
{

	int fstatus, status;

	nl_nl_t nodelist;

	status = nl_nl_init(&nodelist, &src_list, 1);
	if (status == 0) {
		if (nl_map(&nodelist, map_function, other_params)
		    == 0)
			fstatus = nl_nl_nodes_quantity(&nodelist);
		else
			fstatus = -1;
		nl_nl_free_contents(&nodelist);
	} else {
		fstatus = -1;
	}

	return fstatus;
}



