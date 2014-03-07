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
 * \retval  1 error during the operation
*/
int nl_nl_add_ids(nl_nl_t *nodelist, char *idlist)
{
	int fstatus = 0;
	fstatus = nl_rangelist_add_list(&(nodelist->rangelist), idlist);
	return fstatus;
}

int nl_common_split_nl_entry(char *list, char **p_prefix,
					 char **p_idlist, char **p_suffix)
{

	int fstatus = 0;

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
	prefix = gsh_malloc((strlen(list) + 1) * sizeof(char));
	if (prefix != NULL) {
		while (*prefix_end != '[' && !isdigit(*prefix_end)
		       && prefix_end < list_end)
			prefix_end++;
		if (prefix_end != prefix_begin && prefix_end <= list_end) {
			memcpy(prefix, list,
			       (prefix_end - prefix_begin) * sizeof(char));
			prefix[prefix_end - prefix_begin] = '\0';
		} else {
			xfree(prefix);
		}
	} else {
		fstatus = -1;
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
		idlist =
		    gsh_malloc((idlist_end - idlist_begin + 1) *
			       sizeof(char));
		if (idlist != NULL) {
			memcpy(idlist, idlist_begin,
			       (idlist_end - idlist_begin) * sizeof(char));
			idlist[idlist_end - idlist_begin] = '\0';
		} else {
			fstatus = -1;
		}
	}

	/* get suffix */
	/* search for suffix begin */
	suffix_begin = idlist_end;
	while (*suffix_begin == ']' && suffix_begin < list_end)
		suffix_begin++;
	/* dump suffix */
	if (suffix_begin != list_end) {
		suffix =
		    gsh_malloc((list_end - suffix_begin + 1) *
			       sizeof(char));
		if (suffix != NULL) {
			memcpy(suffix, suffix_begin,
			       (list_end - suffix_begin) * sizeof(char));
			suffix[list_end - suffix_begin] = '\0';
		} else {
			fstatus = -1;
		}
	}

	if (fstatus != 0) {
		xfree(prefix);
		xfree(idlist);
		xfree(suffix);
	}

	*p_prefix = prefix;
	*p_idlist = idlist;
	*p_suffix = suffix;

	return fstatus;
}

int nl_nl_init(nl_nl_t *nodelist, char **lists,
			   int lists_nb)
{

	int fstatus = 0;
	int i;
	char *list;

	int operation = 1;	/* 1=add 2=remove */

	nodelist->next = NULL;
	fstatus += nl_rangelist_init(&(nodelist->rangelist));
	fstatus += nl_nodepattern_init(&(nodelist->pattern));

	if (fstatus == 0) {
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
				fstatus +=
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

	if (fstatus)
		return -1;
	else
		return fstatus;
}

int nl_nl_free_contents(nl_nl_t *nodelist)
{

	int fstatus = -1;

	if (nodelist->next != NULL)
		nl_nl_free_contents(nodelist->next);
	nodelist->next = NULL;
	nl_nodepattern_free_contents(&(nodelist->pattern));
	nl_rangelist_free_contents(&(nodelist->rangelist));

	fstatus = 0;

	return fstatus;

}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Copy a node list into an other one
 *
 * \param dest_list the input/output nodes list
 * \param src_list the second list to copy into the first one
 *
 * \retval  0 success
 * \retval -1 failure
*/
int nl_nl_copy(nl_nl_t *dest_list,
			   nl_nl_t *src_list)
{
	int fstatus = -1;

	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	nl_nl_free_contents(dest_list);

	pwldest = &dest_list;
	if (nl_nl_init(*pwldest, NULL, 0) == 0) {

		if (src_list->pattern.prefix == NULL
		    && src_list->pattern.suffix == NULL) {
			/* second list is empty... so initialization will be sufficient */
			fstatus = 0;
		} else {

			pwlsrc = &src_list;
			while (*pwlsrc != NULL) {
				fstatus = -1;

				if (nl_nodepattern_init_by_copy
				    (&((*pwldest)->pattern),
				     &((*pwlsrc)->pattern)) != 0) {
					/* unable to copy pattern, break */
					fstatus = -2;
					break;
				} else {
					if ((*pwlsrc)->pattern.basic != 1) {
						/* add ids */
						if (nl_rangelist_init_by_copy(&((*pwldest)->rangelist), &((*pwlsrc)->rangelist)) != 0) {
							/* unable to copy range list, break */
							fstatus = -3;
							break;
						}
					}
					pwldest = &((*pwldest)->next);
					fstatus = 0;
				}

				pwlsrc = &((*pwlsrc)->next);
			}

			if (fstatus != 0)
				nl_nl_free_contents(dest_list);

		}

	}

	return fstatus;
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
 * \retval  0 success
 * \retval -1 failure
*/
int nl_nl_add_nodelist(nl_nl_t *nodelist,
				   nl_nl_t *second_list)
{
	int fstatus = -1;

	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	/* If second list is emty, nothing to add */
	if (nl_nl_is_empty(second_list))
		return 0;

	/* If nodelist is empty, duplicate second_list! */
	if (nl_nl_is_empty(nodelist))
		fstatus = nl_nl_copy(nodelist, second_list);

	/* we have to add each second list sublist to the first one */
	else {

		pwlsrc = &second_list;
		while (*pwlsrc != NULL) {

			/* try to add src sublist to an existant dest list sublist */
			pwldest = &nodelist;
			while (*pwldest != NULL) {

				/* if patterns equal, try to add ids and break */
				if (nl_nodepattern_equals
				    ((&(*pwldest)->pattern),
				     &((*pwlsrc)->pattern))) {
					if ((*pwldest)->pattern.padding <
					    (*pwlsrc)->pattern.padding)
						nl_nodepattern_set_padding
						    (&((*pwldest)->pattern),
						     (*pwlsrc)->pattern.
						     padding);
					fstatus =
					    nl_rangelist_add_rangelist(&
									     ((*pwldest)->rangelist), &((*pwlsrc)->rangelist));
					break;
				}

				pwldest = &((*pwldest)->next);	/* increment dst sublist */
			}

			/* add a new sublist to dest list if no equivalent pattern list was found */
			if (*pwldest == NULL) {
				*pwldest =
				    gsh_malloc(sizeof(nl_nl_t));
				if (*pwldest != NULL) {
					fstatus =
					    nl_nl_init(*pwldest,
								   NULL, 0);
					if (fstatus == 0) {
						fstatus =
						    nl_nodepattern_init_by_copy
						    (&((*pwldest)->pattern),
						     &((*pwlsrc)->pattern));
						if (fstatus == 0) {
							fstatus =
							    nl_rangelist_add_rangelist
							    (&
							     ((*pwldest)->
							      rangelist),
							     &((*pwlsrc)->
							       rangelist));
						}
					}
				}
			}

			/* fstatus != 0 means that an error occured, break */
			if (fstatus != 0)
				break;

			pwlsrc = &((*pwlsrc)->next);	/* increment src sublist */
		}

	}

	return fstatus;
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Remove a node list from an other one
 *
 * \param nodelist the input/output nodes list
 * \param nodelist the second list to remove from the first one
 *
 * \retval  0 success
 * \retval -1 failure
*/
int nl_nl_remove_nodelist(nl_nl_t *nodelist,
				      nl_nl_t *second_list)
{
	int fstatus = -1;

	int add_flag;

	nl_nl_t worklist;
	nl_nl_t **pwldest;
	nl_nl_t **pwlsrc;

	/* If second list is emty, nothing to remove */
	if (nl_nl_is_empty(second_list))
		return 0;

	/* If nodelist is empty, nothing to remove */
	if (nl_nl_is_empty(nodelist))
		return 0;

	/* we have to remove each second list sublist from the first one */
	else {

		/* initialize work list by copying the first nodelist */
		fstatus = nl_nl_init(&worklist, NULL, 0);
		if (fstatus == 0) {

			pwldest = &nodelist;
			while (*pwldest != NULL) {

				add_flag = 1;
				pwlsrc = &second_list;
				while (*pwlsrc != NULL) {

					/* if patterns equal, try to remove ids and break */
					if (nl_nodepattern_equals
					    ((&(*pwldest)->pattern),
					     &((*pwlsrc)->pattern))) {
						add_flag = 0;
						if ((*pwldest)->pattern.basic ==
						    0) {
							fstatus +=
							    nl_rangelist_remove_rangelist
							    (&
							     ((*pwldest)->
							      rangelist),
							     &((*pwlsrc)->rangelist));
						} else
							fstatus = 0;
						fprintf(stdout, "fstatus %d\n",
							fstatus);
						break;
					}

					pwlsrc = &((*pwlsrc)->next);	/* incr src sublist */
				}

				if (fstatus)
					break;

				if (add_flag == 1) {
					fstatus +=
					    nl_nl_add_nodelist
					    (&worklist, *pwldest);
				}

				if (fstatus)
					break;

				pwldest = &((*pwldest)->next);	/* incr dest sublist */
			}

			if (fstatus == 0) {
				fstatus =
				    nl_nl_copy(nodelist, &worklist);
			}

			nl_nl_free_contents(&worklist);
		}

	}

	return fstatus;
}

int nl_nl_add_nodes(nl_nl_t *nodelist, char *list)
{

	int fstatus = -1;
	int status;

	char *prefix;
	char *idlist;
	char *suffix;

	int token_nb, i;
	char *token;

	nl_nl_t wlist;

	if (nl_common_string_get_tokens_quantity(list, ",", &token_nb) ==
	    0) {
		token = NULL;

		for (i = 1; i <= token_nb; i++) {
			if (nl_common_string_get_token
			    (list, ",", i, &token) == 0) {

				status =
				    nl_common_split_nl_entry(token,
									 &prefix,
									 &idlist,
									 &suffix);
				if (status) {
					fstatus = -1;
				} else {
					fstatus =
					    nl_nl_init(&wlist, NULL,
								   0);
					if (fstatus == 0) {
						nl_nodepattern_set_prefix
						    (&(wlist.pattern), prefix);
						nl_nodepattern_set_suffix
						    (&(wlist.pattern), suffix);
						if (idlist != NULL) {
							wlist.pattern.basic = 0;
							fstatus =
							    nl_nl_add_ids
							    (&wlist, idlist);
							nl_nodepattern_set_padding
							    (&(wlist.pattern),
							     fstatus);
							fstatus = 0;
						}

						fstatus =
						    nl_nl_add_nodelist
						    (nodelist, &wlist);

						nl_nl_free_contents
						    (&wlist);
					}

					xfree(prefix);
					xfree(suffix);
					xfree(idlist);
				}

				gsh_free(token);
			}
			token = NULL;
		}

	}

	return fstatus;

}

int nl_nl_remove_nodes(nl_nl_t *nodelist, char *list)
{

	int fstatus = -1;
	int status;

	char *prefix;
	char *idlist;
	char *suffix;

	int token_nb, i;
	char *token;

	nl_nl_t wlist;

	if (nl_common_string_get_tokens_quantity(list, ",", &token_nb) ==
	    0) {
		token = NULL;
		for (i = 1; i <= token_nb; i++) {
			if (nl_common_string_get_token
			    (list, ",", i, &token) == 0) {

				status =
				    nl_common_split_nl_entry(token,
									 &prefix,
									 &idlist,
									 &suffix);
				if (status) {
					fstatus = -1;
				} else {
					fstatus =
					    nl_nl_init(&wlist, NULL,
								   0);
					if (fstatus == 0) {
						nl_nodepattern_set_prefix
						    (&(wlist.pattern), prefix);
						nl_nodepattern_set_suffix
						    (&(wlist.pattern), suffix);
						if (idlist != NULL) {
							wlist.pattern.basic = 0;
							fstatus =
							  nl_nl_add_ids
							  (&wlist, idlist);
							nl_nodepattern_set_padding
							    (&(wlist.pattern),
							     fstatus);
							fstatus = 0;
						}

						fstatus =
						    nl_nl_remove_nodelist
						    (nodelist, &wlist);

						nl_nl_free_contents
						    (&wlist);
					}

					xfree(prefix);
					xfree(suffix);
					xfree(idlist);
				}

				gsh_free(token);
			}
			token = NULL;
		}

	}

	return fstatus;

}

long int nl_nl_non_recursive_nodes_quantity(nl_nl_t *
							nodelist)
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
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_init(nl_nodepattern_t *np)
{
	np->padding = 0;
	np->prefix = NULL;
	np->suffix = NULL;
	np->basic = 1;
	return 0;
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
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_init_by_copy(nl_nodepattern_t *np,
				      nl_nodepattern_t *npin)
{
	int fstatus = -1;
	np->padding = npin->padding;
	np->basic = npin->basic;
	np->prefix = NULL;
	np->suffix = NULL;
	if (npin->prefix != NULL) {
		np->prefix = gsh_strdup(npin->prefix);
		if (np->prefix == NULL) {
			nl_nodepattern_free_contents(np);
			return fstatus;
		}
	}
	if (npin->suffix != NULL) {
		np->suffix = gsh_strdup(npin->suffix);
		if (np->suffix == NULL) {
			nl_nodepattern_free_contents(np);
			return fstatus;
		}
	}
	fstatus = 0;
	return fstatus;
}

/*!
 * \brief Clean a bridge node pattern structure
 *
 * \param np pointer on a bridge node pattern structure to free
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_free_contents(nl_nodepattern_t *np)
{
	np->padding = 0;
	xfree(np->prefix);
	xfree(np->suffix);
	np->basic = 1;
	return 0;
}

/*!
 * \brief Set bridge node pattern padding
 *
 * \param np pointer on a bridge node pattern structure to free
 * \param padding padding value of the pattern
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_set_padding(nl_nodepattern_t *np, int padding)
{
	int fstatus = -1;
	if (np != NULL) {
		np->padding = padding;
		fstatus = 0;
	}
	return fstatus;
}

/*!
 * \brief Set bridge node pattern prefix
 *
 * \param np pointer on a bridge node pattern structure
 * \param prefix node pattern prefix
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nl_nodepattern_set_prefix(nl_nodepattern_t *np, char *prefix)
{
	int fstatus = -1;
	if (np != NULL && prefix != NULL) {
		xfree(np->prefix);
		np->prefix = gsh_strdup(prefix);
		if (np->prefix != NULL)
			fstatus = 0;
	}
	return fstatus;
}

int nl_nodepattern_set_suffix(nl_nodepattern_t *np, char *suffix)
{
	int fstatus = -1;
	if (np != NULL && suffix != NULL) {
		xfree(np->suffix);
		np->suffix = gsh_strdup(suffix);
		if (np->suffix != NULL)
			fstatus = 0;
	}
	return fstatus;
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
