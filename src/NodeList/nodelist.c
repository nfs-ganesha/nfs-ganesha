#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <search.h>

#include <limits.h>
#include <ctype.h>

#include "abstract_mem.h"
#include "nodelist.h"
#include "nodelist_internals.h"

#define MAX_LONG_INT_STRING_SIZE      128

/*!
 * \ingroup NODELIST_COMMON
 * \brief Add ids list to nodes list
 *
 * \param list the input nodes list
 * \param idlist the ids list to add (string representation)
 *
 * \retval  n padding length if operation successfully done
 * \retval -1 error during the operation
*/
static inline int nl_nl_add_ids(nl_nl_t *nodelist, char *idlist)
{
	return nl_rangelist_add_list(&nodelist->rangelist, idlist);
}

void nl_common_split_nl_entry(char *list, char **p_prefix,
			      char **p_idlist, char **p_suffix)
{
	char *list_end;

	char *prefix = NULL;
	char *prefix_begin;
	char *prefix_end;
	char *suffix = NULL;
	char *suffix_begin;

	char *idlist = NULL;
	char *idlist_begin;
	char *idlist_end;

	/* create working copy of input list */
	list_end = list + (strlen(list));
	prefix_begin = list;
	prefix_end = list;

	/* get prefix */
	while (*prefix_end != '[' && !isdigit(*prefix_end)
	       && prefix_end < list_end)
		prefix_end++;
	if (prefix_end != prefix_begin && prefix_end <= list_end) {
		prefix = gsh_malloc(prefix_end - prefix_begin + 1);
		memcpy(prefix, list, prefix_end - prefix_begin);
		prefix[prefix_end - prefix_begin] = '\0';
	}

	/* get idlist */
	/* search for idlist begin */
	if (prefix != NULL)
		idlist_begin = prefix_end;
	else
		idlist_begin = list;
	while (*idlist_begin == '[' && idlist_begin < list_end)
		idlist_begin++;
	idlist_end = idlist_begin;
	if (idlist_begin < list_end) {
		/* search for idlist end */
		while ((isdigit(*idlist_end) || *idlist_end == ','
			|| *idlist_end == '-')
		       && idlist_end < list_end) {
			idlist_end++;
		}
		/* remove trailing dash, like in node%d-eth */
		if (*(idlist_end - 1) == '-') {
			idlist_end--;
			while (*(idlist_end - 1) == '-')
				idlist_end--;
		}
		/* dump idlist */
		idlist = gsh_malloc(idlist_end - idlist_begin + 1);
		memcpy(idlist, idlist_begin, idlist_end - idlist_begin);
		idlist[idlist_end - idlist_begin] = '\0';
	}

	/* get suffix */
	/* search for suffix begin */
	suffix_begin = idlist_end;
	while (*suffix_begin == ']' && suffix_begin < list_end)
		suffix_begin++;
	/* dump suffix */
	if (suffix_begin != list_end) {
		suffix = gsh_malloc(list_end - suffix_begin + 1);
		memcpy(suffix, suffix_begin, list_end - suffix_begin);
		suffix[list_end - suffix_begin] = '\0';
	}

	*p_prefix = prefix;
	*p_idlist = idlist;
	*p_suffix = suffix;
}

void nl_nl_init(nl_nl_t *nodelist, char **lists, int lists_nb)
{
	int i;
	char *list;

	int operation = 1;	/* 1=add 2=remove */

	nodelist->next = NULL;
	nl_rangelist_init(&(nodelist->rangelist));
	nl_nodepattern_init(&(nodelist->pattern));

	for (i = 0; i < lists_nb; i++) {
		list = lists[i];

		/* set operation if required */
		if (strlen(list) == 1) {
			if (*list == '-') {
				operation = 2;
				continue;
			}
			if (*list == '+') {
				operation = 1;
				continue;
			} else
				operation = 1;
		}

		/* do action */
		switch (operation) {

		case 1:
			nl_nl_add_nodes(nodelist, list);
			break;

		case 2:
			nl_nl_remove_nodes(nodelist, list);
			break;

		}

		/* setting default operation */
		operation = 1;
	}
}

void nl_nl_free_contents(nl_nl_t *nodelist)
{
	if (nodelist->next != NULL)
		nl_nl_free_contents(nodelist->next);
	nodelist->next = NULL;
	nl_nodepattern_free_contents(&(nodelist->pattern));
	nl_rangelist_free_contents(&(nodelist->rangelist));
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Copy a node list into an other one
 *
 * \param dest_list the input/output nodes list
 * \param src_list the second list to copy into the first one
 *
*/
void nl_nl_copy(nl_nl_t *dest_list, nl_nl_t *src_list)
{
	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	nl_nl_free_contents(dest_list);

	pwldest = &dest_list;
	nl_nl_init(*pwldest, NULL, 0);

	if (src_list->pattern.prefix == NULL &&
	    src_list->pattern.suffix == NULL) {
		/* second list is empty... so initialization will
		 * be sufficient
		 */
		return;
	}

	pwlsrc = &src_list;
	while (*pwlsrc != NULL) {
		nl_nodepattern_init_by_copy(
		    &(*pwldest)->pattern, &(*pwlsrc)->pattern);

		if ((*pwlsrc)->pattern.basic != 1) {
			/* add ids */
			nl_rangelist_init_by_copy(&(*pwldest)->rangelist,
						  &(*pwlsrc)->rangelist);
		}

		pwldest = &((*pwldest)->next);

		pwlsrc = &((*pwlsrc)->next);
	}
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if a node list is empty
 *
 * \param nodelist the input node list
 *
 * \retval  1 if empty
 * \retval  0 if not empty
*/
int nl_nl_is_empty(nl_nl_t *nodelist)
{

	if (nodelist->pattern.prefix == NULL
	    && nodelist->pattern.suffix == NULL) {
		return 1;
	} else {
		return 0;
	}

}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Add a node list to an other one
 *
 * \param nodelist the input/output nodes list
 * \param nodelist the second list to add to the first one
 *
*/
void nl_nl_add_nodelist(nl_nl_t *nodelist, nl_nl_t *second_list)
{
	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	/* If second list is emty, nothing to add */
	if (nl_nl_is_empty(second_list))
		return;

	/* If nodelist is empty, duplicate second_list! */
	if (nl_nl_is_empty(nodelist)) {
		nl_nl_copy(nodelist, second_list);
		return;
	}

	/* we have to add each second list sublist to the first one */
	pwlsrc = &second_list;
	while (*pwlsrc != NULL) {

		/* try to add src sublist to an existing dest list sublist */
		pwldest = &nodelist;
		while (*pwldest != NULL) {

			/* if patterns equal, try to add ids and break */
			if (nl_nodepattern_equals(&(*pwldest)->pattern,
						  &(*pwlsrc)->pattern)) {
				if ((*pwldest)->pattern.padding <
				    (*pwlsrc)->pattern.padding) {
					nl_nodepattern_set_padding(
					    &(*pwldest)->pattern,
					    (*pwlsrc)->pattern.padding);
				}

				nl_rangelist_add_rangelist(
					&(*pwldest)->rangelist,
					&(*pwlsrc)->rangelist);
				break;
			}

			/* increment dst sublist */
			pwldest = &(*pwldest)->next;
		}

		/* add a new sublist to dest list if no
		 * equivalent pattern list was found
		 */
		if (*pwldest == NULL) {
			*pwldest = gsh_malloc(sizeof(nl_nl_t));
			nl_nl_init(*pwldest, NULL, 0);

			nl_nodepattern_init_by_copy(&((*pwldest)->pattern),
						    &((*pwlsrc)->pattern));

			nl_rangelist_add_rangelist(&(*pwldest)->rangelist,
						   &(*pwlsrc)->rangelist);
		}

		pwlsrc = &((*pwlsrc)->next);	/* increment src sublist */
	}
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Remove a node list from an other one
 *
 * \param nodelist the input/output nodes list
 * \param nodelist the second list to remove from the first one
 *
*/
void nl_nl_remove_nodelist(nl_nl_t *nodelist, nl_nl_t *second_list)
{
	int add_flag;

	nl_nl_t worklist;
	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	/* If second list is emty, nothing to remove */
	if (nl_nl_is_empty(second_list))
		return;

	/* If nodelist is empty, nothing to remove */
	if (nl_nl_is_empty(nodelist))
		return;

	/* we have to remove each second list sublist from the first one */

	/* initialize work list by copying the first nodelist */
	nl_nl_init(&worklist, NULL, 0);

	pwldest = &nodelist;
	while (*pwldest != NULL) {

		add_flag = 1;
		pwlsrc = &second_list;
		while (*pwlsrc != NULL) {

			/* if patterns equal, try to remove ids and break */
			if (nl_nodepattern_equals(&(*pwldest)->pattern,
						  &(*pwlsrc)->pattern)) {
				add_flag = 0;
				if ((*pwldest)->pattern.basic == 0) {
					nl_rangelist_remove_rangelist(
					    &(*pwldest)->rangelist,
					    &(*pwlsrc)->rangelist);
				}
				break;
			}

			pwlsrc = &((*pwlsrc)->next);	/* incr src sublist */
		}

		if (add_flag == 1)
			nl_nl_add_nodelist(&worklist, *pwldest);

		pwldest = &((*pwldest)->next);	/* incr dest sublist */
	}

	nl_nl_copy(nodelist, &worklist);

	nl_nl_free_contents(&worklist);
}

void nl_nl_add_nodes(nl_nl_t *nodelist, char *list)
{
	char *prefix;
	char *idlist;
	char *suffix;

	int token_nb, i;
	char *token;

	nl_nl_t wlist;

	if (nl_common_string_get_tokens_quantity(list, ",", &token_nb) == 0) {

		for (i = 1; i <= token_nb; i++) {
			token = NULL;
			if (nl_common_string_get_token(list, ",", i, &token)
			    == 0) {

				nl_common_split_nl_entry(token, &prefix,
							 &idlist, &suffix);
				nl_nl_init(&wlist, NULL, 0);

				nl_nodepattern_set_prefix
					(&wlist.pattern, prefix);
				nl_nodepattern_set_suffix
					(&wlist.pattern, suffix);
				if (idlist != NULL) {
					int padding;

					wlist.pattern.basic = 0;
					padding = nl_nl_add_ids(
						&wlist, idlist);
					nl_nodepattern_set_padding(
						&wlist.pattern, padding);
				}

				nl_nl_add_nodelist(nodelist, &wlist);

				nl_nl_free_contents(&wlist);

				xfree(prefix);
				xfree(suffix);
				xfree(idlist);

				gsh_free(token);
			}
		}
	}
}

void nl_nl_remove_nodes(nl_nl_t *nodelist, char *list)
{
	char *prefix;
	char *idlist;
	char *suffix;

	int token_nb, i;
	char *token;

	nl_nl_t wlist;

	if (nl_common_string_get_tokens_quantity(list, ",", &token_nb) == 0) {
		for (i = 1; i <= token_nb; i++) {
			token = NULL;

			if (nl_common_string_get_token(list, ",", i, &token)
			    == 0) {

				nl_common_split_nl_entry(token, &prefix,
							 &idlist, &suffix);
				nl_nl_init(&wlist, NULL, 0);

				nl_nodepattern_set_prefix(
					&wlist.pattern, prefix);
				nl_nodepattern_set_suffix(
					&wlist.pattern, suffix);
				if (idlist != NULL) {
					int padding;

					wlist.pattern.basic = 0;
					padding = nl_nl_add_ids(
						&wlist, idlist);
					nl_nodepattern_set_padding(
					    &wlist.pattern, padding);
				}

				nl_nl_remove_nodelist(nodelist, &wlist);

				nl_nl_free_contents(&wlist);

				xfree(prefix);
				xfree(suffix);
				xfree(idlist);

				gsh_free(token);
			}
		}
	}
}

long int nl_nl_non_recursive_nodes_quantity(nl_nl_t *nodelist)
{

	long int quantity;
	long int i;

	quantity = 0;
	if (nodelist->pattern.basic == 1) {
		quantity++;
	} else {
		for (i = 0; i < nodelist->rangelist.ranges_nb; i++) {
			quantity +=
			    (nodelist->rangelist.array[i].to -
			     nodelist->rangelist.array[i].from + 1);
		}
	}

	return quantity;
}

long int nl_nl_nodes_quantity(nl_nl_t *nodelist)
{

	long int quantity;

	nl_nl_t *nlist;

	quantity = 0;
	nlist = nodelist;
	while (nlist != NULL) {
		quantity +=
		    nl_nl_non_recursive_nodes_quantity(nlist);
		nlist = nlist->next;
	}

	return quantity;
}


/*! \addtogroup NODELIST_NODEPATTERN
 *  @{
 */
/*!
 * \brief Initialize a bridge node pattern structure
 *
 * by default, padding is set to 0, prefix and suffix to NULL
 * and the node pattern is basic
 *
 * \param np pointer on a bridge node pattern structure to initialize
 *
*/
void nl_nodepattern_init(nl_nodepattern_t *np)
{
	np->padding = 0;
	np->prefix = NULL;
	np->suffix = NULL;
	np->basic = 1;
}

/*!
 * \brief Initialize a bridge node pattern structure by dumping an other one
 *
 * by default, padding is set to 0, prefix and suffix to NULL
 * and the node pattern is basic
 *
 * \param np pointer on a bridge node pattern structure to initialize
 * \param npin pointer on a bridge node pattern to copy
 *
*/
void nl_nodepattern_init_by_copy(nl_nodepattern_t *np, nl_nodepattern_t *npin)
{
	np->padding = npin->padding;
	np->basic = npin->basic;
	np->prefix = NULL;
	np->suffix = NULL;
	if (npin->prefix != NULL)
		np->prefix = gsh_strdup(npin->prefix);

	if (npin->suffix != NULL)
		np->suffix = gsh_strdup(npin->suffix);
}

/*!
 * \brief Clean a bridge node pattern structure
 *
 * \param np pointer on a bridge node pattern structure to free
 *
*/
void nl_nodepattern_free_contents(nl_nodepattern_t *np)
{
	np->padding = 0;
	xfree(np->prefix);
	xfree(np->suffix);
	np->basic = 1;
}

/*!
 * \brief Test if two bridge node patterns are identical (paddinf is not tested)
 *
 * \param np1 pointer on the first bridge node pattern structure
 * \param np2 pointer on the first bridge node pattern structure
 *
 * \retval  1 if the two pattern are identical
 * \retval  0 if they are not identical
 * \retval -1 operation failed
*/
int nl_nodepattern_equals(nl_nodepattern_t *np1,
				nl_nodepattern_t *np2)
{
	int fstatus = -1;
	if (np1 != NULL && np2 != NULL) {
		fstatus = 0;
		/* same basic flag ? */
		if (np1->basic != np2->basic)
			return fstatus;
		/* same prefix or lack of prefix ? */
		if (np1->prefix != NULL && np2->prefix != NULL) {
			if (strcmp(np1->prefix, np2->prefix) != 0)
				return fstatus;
		} else if (np1->prefix == NULL && np2->prefix != NULL) {
			return fstatus;
		} else if (np1->prefix != NULL && np2->prefix == NULL) {
			return fstatus;
		}
		/* same suffix or lack of suffix ? */
		if (np1->suffix != NULL && np2->suffix != NULL) {
			if (strcmp(np1->suffix, np2->suffix) != 0)
				return fstatus;
		} else if (np1->suffix == NULL && np2->suffix != NULL) {
			return fstatus;
		} else if (np1->suffix != NULL && np2->suffix == NULL) {
			return fstatus;
		}
		/* ok, they are the same pattern */
		fstatus = 1;
	}
	return fstatus;
}

/*!
 * @}
*/
