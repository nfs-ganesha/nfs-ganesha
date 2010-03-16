#ifndef _NODELIST_INTERNALS_H
#define _NODELIST_INTERNALS_H

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "nodelist.h"

int nodelist_nodelist_non_recursive_intersects(nodelist_nodelist_t * first_list,
                                               nodelist_nodelist_t * second_list);
int nodelist_rangelist_includes(nodelist_rangelist_t * a1, nodelist_rangelist_t * a2);
int nodelist_range_intersects(nodelist_range_t * r1, nodelist_range_t * r2);
int nodelist_rangelist_intersects(nodelist_rangelist_t * a1, nodelist_rangelist_t * a2);
int nodelist_nodelist_remove_nodes(nodelist_nodelist_t * nodelist, char *list);
int nodelist_rangelist_add_rangelist(nodelist_rangelist_t * array,
                                     nodelist_rangelist_t * rlin);
int nodelist_rangelist_remove_rangelist(nodelist_rangelist_t * array,
                                        nodelist_rangelist_t * rlin);

#endif
