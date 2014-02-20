#ifndef _NODELIST_H
#define _NODELIST_H

#include "avltree.h"

/* define a macro that make an advanced free on pointer */
#define xfree(a)				\
	if (a != NULL) {			\
		gsh_free(a);			\
		a = NULL;			\
	}

/*! \addtogroup NODELIST_RANGE
 *  @{
 */
/*!
 * \ingroup NODELIST_RANGE
 * \brief structure that represent a range of long int value
 */
typedef struct nl_range {
	long int from;
	long int to;
} nl_range_t;
/*!
 * \ingroup NODELIST_RANGE
 * \brief set bridge range values
 *
 * \param r1 input range
 * \param v1 from value
 * \param v2 to value
 *
 * \retval  0 should not return an other value
*/
int nl_range_set(nl_range_t *r1, long int v1, long int v2);
/*!
 * \ingroup NODELIST_RANGE
 * \brief Indicate if the range is a valid one. That is to say if from value
 * is lower that to value
 *
 * \param r1 input range
 *
 * \retval  1 if the range is valid
 * \retval  0 if the range is not valid
*/
int nl_range_check(nl_range_t *r1);

/*!
 * @ingroup NODELIST_RANGE
 * @brief Indicate if the first range equals, is placed before
 * or is placed after the second one
 *
 * \param r1 one of the two input ranges
 * \param r2 one of the two input ranges
 *
 * \retval  1 if the second one end before the start of the first one
 * \retval  0 if the two ranges are equals
 * \retval -1 if the second one start after the end of the first one
*/
int nl_range_compare(nl_range_t *r1, nl_range_t *r2);
/*!
 * \ingroup NODELIST_RANGE
 * \brief Indicate if it exists an non empty intersection
 * between the two input ranges
 *
 * \param r1 one of the two input ranges
 * \param r2 one of the two input ranges
 *
 * \retval  1 an intersection exists
 * \retval  0 otherwise
*/
int nl_range_intersects(nl_range_t *r1, nl_range_t *r2);
/*!
 * \ingroup NODELIST_RANGE
 * \brief Gives the range that is common to the two input ranges
 *
 * \param r1 one of the two input ranges
 * \param r2 one of the two input ranges
 * \param r3 output range
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_range_intersection(nl_range_t *r1, nl_range_t *r2,
				nl_range_t *r3);
/*!
 * \ingroup NODELIST_RANGE
 * \brief Indicate if the two input ranges are contiguous
 *
 * \param r1 one of the two input ranges
 * \param r2 one of the two input ranges
 *
 * \retval  0 if not continuous
 * \retval  1 if continuous and r1 is before r2
 * \retval  2 if continuous and r2 is before r1
*/
int nl_range_contiguous(nl_range_t *r1, nl_range_t *r2);
/*!
 * \ingroup NODELIST_RANGE
 * \brief Indicate if one of the two input range is included in the other
 * one
 *
 * \param r1 one of the two input ranges
 * \param r2 one of the two input ranges
 *
 * \retval  0 if no inclusion detected
 * \retval  1 if r2 is included in r1 (r1 is the bigger one)
 * \retval  2 if r1 is included in r2 (r2 is the bigger one)
*/
int nl_range_includes(nl_range_t *r1, nl_range_t *r2);
/**
 * *ingroup NODELIST_RANGE
 * brief Gives a nl_range that represent the union of the two nl_ranges
 * given in input. The two ranges must intersect or be continuous otherwise
 * operation will failed
 *
 * @param[in] r1 one of the two input ranges
 * @param[in] r2 one of the two input ranges
 * @param[out] rout output range
 *
 * @retval  0 operation successfully done
 * @retval -1 operation failed
*/
int nl_range_union(nl_range_t *r1, nl_range_t *r2,
			 nl_range_t *rout);
/*!
 * @}
*/

/*! \addtogroup NODELIST_RANGES_ARRAY
 *  @{
 */
/*!
 * \ingroup NODELIST_RANGES_ARRAY
 * \brief structure that represent a range of long int value
 */
typedef struct nl_rangelist {
	long int ranges_nb;
	nl_range_t *array;
	size_t pre_allocated_ranges;
} nl_rangelist_t;
int nl_rangelist_init(nl_rangelist_t *array);
int nl_rangelist_init_by_copy(nl_rangelist_t *array,
				    nl_rangelist_t *a2c);
/*!
 * \ingroup BATCH_MANAGER
 * \brief Free a bridge ranges array structure contents
 *
 * \param array pointer on a bridge ranges array structure to finalize
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_rangelist_free_contents(nl_rangelist_t *array);
/*!
 * \ingroup BATCH_MANAGER
 * \brief Increment a bridge ranges array storage zone
 *
 * \param array pointer on a bridge ranges array structure to increment
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_rangelist_incremente_size(nl_rangelist_t *array);
/*!
 * \ingroup BATCH_MANAGER
 * \brief Add a range to a bridge ranges array
 * The range will be merge with already existing ranges if required
 * and resulting ranges will be sorted
 *
 * \param array pointer on a bridge ranges array structure to use for add-on
 * \param r range that will be add to the array
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_rangelist_add_range(nl_rangelist_t *array,
				 nl_range_t *r);
/*!
 * \ingroup BATCH_MANAGER
 * \brief Add a list of values to a bridge ranges array
 * The range will be merge with already existing ranges if required
 * and resulting ranges will be sorted
 *
 * \param array pointer on a bridge ranges array structure to use for add-on
 * \param list values list that must be added (pattern [*,*-*...])
 *
 * \retval  n padding if operation successfully done
 * \retval -1 operation failed
*/
int nl_rangelist_add_list(nl_rangelist_t *array, char *list);
/*!
 * \ingroup BATCH_MANAGER
 * \brief Sort a bridge ranges array
 *
 * \param array pointer on a bridge ranges array structure to sort
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
 */
int nl_rangelist_sort(nl_rangelist_t *array);
/*!
 * @}
*/

/*! \addtogroup NODELIST_IDS_LIST
 *  @{
 */
/*!
 * \ingroup NODELIST_IDS_LIST
 * \brief structure that represent a range of long int value
 */
typedef struct nl_idlist {

	long int id_nb;

	nl_rangelist_t rangelist;	/*!<
					 * ranges array of this list
					 */
} nl_idlist_t;
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Initialize a bridge ids list structure
 *
 * \param idlist pointer on a bridge ids list structure to initialize
 * \param lists array of strings containing ids to add to this list
 * \param lists_nb quanity of string in the array
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_idlist_init(nl_idlist_t *idlist, char **lists,
			 int lists_nb);
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Free a bridge ids list structure
 *
 * \param idlist pointer on a bridge ids list structure to finalize
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_idlist_free_contents(nl_idlist_t *idlist);
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Add a ids list to a bridge ids list structure
 *
 * \param idlist pointer on a bridge ids list structure
 * \param list ids list to add to this bridge ids list
 *
 * \retval  n padding length if operation successfully done
 * \retval -1 operation failed
*/
int nl_idlist_add_ids(nl_idlist_t *idlist, char *list);
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Get ids quantity
 *
 * \param idlist pointer on a bridge ids list structure
 *
 * \retval quantity of ids in this bridge ids list
*/
long int nl_idlist_ids_quantity(nl_idlist_t *idlist);
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Get extended ids string
 *
 * \param idlist pointer on a bridge ids list structure
 * \param p_string pointer on a string that will be allocated
 * and filled with ids names
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_idlist_get_extended_string(nl_idlist_t *idlist,
					char **p_string);
/*!
 * \ingroup NODELIST_IDLIST
 * \brief Get compacted ids string
 *
 * \param idlist pointer on a bridge ids list structure
 * \param p_string pointer on a string that will be allocated
 * and filled with compacted ids list
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_idlist_get_compacted_string(nl_idlist_t *idlist,
					 char **p_string);
/*!
 * @}
*/

/*! \addtogroup NODELIST_NODEPATTERN
 *  @{
 */
/*!
 * \brief structure that represent a nodename pattern
 *
 * can be use for basic node or enumartion node (prefixXXXsuffix pattern)
 */
typedef struct nl_nodepattern {
	int padding;		/*!< padding length */
	char *prefix;		/*!< nodename prefix */
	char *suffix;		/*!< nodename suffix */
	int basic;		/*!< basic node flag 0=no 1=yes */
} nl_nodepattern_t;
/*!
 * \brief Initialize a bridge node pattern structure
 *
 * by default, padding is set to 0, prefix and suffix to NULL
 * and the node pattern is basic
 *
 * \param np pointer on a bridge node pattern structure to initialize
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_init(nl_nodepattern_t *np);
/*!
 * \brief Initialize a bridge node pattern structure by dumping an other one
 *
 * by default, padding is set to 0, prefix and suffix to NULL
 * and the node pattern is basic
 *
 * \param np pointer on a bridge node pattern structure to initialize
 * \param npin pointer on a bridge node pattern to copy
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_init_by_copy(nl_nodepattern_t *np,
				      nl_nodepattern_t *npin);
/*!
 * \brief Clean a bridge node pattern structure
 *
 * \param np pointer on a bridge node pattern structure to free
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_free_contents(nl_nodepattern_t *np);
/*!
 * \brief Set bridge node pattern padding
 *
 * \param np pointer on a bridge node pattern structure to free
 * \param padding padding value of the pattern
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_set_padding(nl_nodepattern_t *np, int padding);
int nl_nodepattern_set_prefix(nl_nodepattern_t *np, char *prefix);
int nl_nodepattern_set_suffix(nl_nodepattern_t *np, char *suffix);
int nl_nodepattern_equals(nl_nodepattern_t *np1,
				nl_nodepattern_t *np2);
/*!
 * @}
*/

/*! \addtogroup NODELIST_NODELIST
 *  @{
 */
/*!
 * \ingroup NODELIST_NODELIST
 * \brief structure that represent a range of long int value
 */
typedef struct nl_nodelist {
	nl_nodepattern_t pattern;
	nl_rangelist_t rangelist;
	struct nl_nodelist *next;
} nl_nl_t;
/*!
 * \ingroup NODELIST_NODELIST
 * \brief Initialize a bridge nodes list structure
 *
 * \param nodelist pointer on a bridge nodes list structure to initialize
 * \param lists array of strings containing nodes to add to this list
 * \param lists_nb quanity of string in the array
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nl_init(nl_nl_t *nodelist, char **lists,
			   int lists_nb);
/*!
 * \ingroup NODELIST_NODELIST
 * \brief Free a bridge nodes list structure
 *
 * \param nodelist pointer on a bridge nodes list structure to finalize
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nl_free_contents(nl_nl_t *nodelist);
/*!
 * \ingroup NODELIST_NODELIST
 * \brief Add a nodes list to a bridge nodes list structure
 *
 * \param nodelist pointer on a bridge nodes list structure
 * \param list nodes list to add to this bridge nodes list
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nl_add_nodes(nl_nl_t *nodelist, char *list);
/*!
 * \ingroup NODELIST_NODELIST
 * \brief Check a nodes list versus another one according to required operation
 * operation can be inclusion or intersection
 *
 * \param nodelist pointer on the first bridge nodes list structure
 * \param nodelist pointer on the second bridge nodes list structure
 * \param operation on of VERSUS_OPERATION_INCLUDE or VERSUS_OPERATION_INTERSECT
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
#define VERSUS_OPERATION_INCLUDE         1
#define VERSUS_OPERATION_INTERSECT       2
/*!
 * \ingroup NODELIST_NODELIST
 * \brief Get nodes quantity
 *
 * \param nodelist pointer on a bridge nodes list structure
 *
 * \retval quantity of nodes in this bridge nodes list
*/
long int nl_nl_nodes_quantity(nl_nl_t *nodelist);

/*!
 * \ingroup NODELIST_NODELIST
 * \brief Get an compacted nodes list based on a extended one
 *
 * \param src_list extended (or not) nodes list
 * \param p_dst_list compacted nodes list
 *
 * \retval  n nodes quantity if operation successfully done
 * \retval -1 operation failed
*/

/*!
 * @}
*/

/*!
 * \ingroup NODELIST_COMMON
 * \brief Get number of tokens included in a string
 *
 * \param string input string
 * \param separators_list string containing allowed token 's separators
 * \param p_token_nb pointer on an integer that will be set
 * to tokens quantity found in the string
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_common_string_get_tokens_quantity(char *string,
					       char *separators_list,
					       int *p_token_nb);

/*!
 * \ingroup NODELIST_COMMON
 * \brief Get a specific token included in a string
 *
 * \param string input string
 * \param separators_list string containing allowed token 's separators
 * \param token id the id of the token in the list
 * \param p_token pointer on a string that will be
 * set according to the token value (must be free later)
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_common_string_get_token(char *string, char *separators_list,
				     int token_id, char **p_token);

int nl_map_condensed(char *src_list,
		     int (*map_function)(char *, void *),
		     void *otherparams);


#endif
