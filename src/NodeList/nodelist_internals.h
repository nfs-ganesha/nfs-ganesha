#ifndef _NODELIST_INTERNALS_H
#define _NODELIST_INTERNALS_H

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "nodelist.h"

int nl_nl_non_recursive_intersects(nl_nl_t *first_list,
				   nl_nl_t *
				   second_list);
int nl_rangelist_includes(nl_rangelist_t *a1,
			  nl_rangelist_t *a2);
int nl_range_intersects(nl_range_t *r1, nl_range_t *r2);
int nl_rangelist_intersects(nl_rangelist_t *a1,
			    nl_rangelist_t *a2);
int nl_nl_remove_nodes(nl_nl_t *nodelist, char *list);
int nl_rangelist_add_rangelist(nl_rangelist_t *array,
			       nl_rangelist_t *rlin);
int nl_rangelist_remove_rangelist(nl_rangelist_t *array,
				  nl_rangelist_t *rlin);

#endif
