#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <search.h>

#include <limits.h>
#include <ctype.h>

#include "nodelist.h"
#include "nodelist_internals.h"

#define MAX_LONG_INT_STRING_SIZE      128

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if two nodes shares the same pattern
 *
 * \param first_list first nodes list
 * \param second_list second nodes list
 *
 * \retval  0 nodes lists patterns are not the same
 * \retval  1 nodes lists patterns are the same
*/
int
nodelist_nodelist_equal_patterns(nodelist_nodelist_t * first_list,
                                 nodelist_nodelist_t * second_list)
{
  int fstatus = 1;
  fstatus = nodelist_nodepattern_equals(&(first_list->pattern), &(second_list->pattern));
  return fstatus;
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if two nodes list intersect in a non recursive way
 *
 * \param first_list first nodes list
 * \param second_list second nodes list
 *
 * \retval  0 nodes lists don't intersect
 * \retval  1 nodes lists intersect
*/
int
nodelist_nodelist_non_recursive_intersects(nodelist_nodelist_t * first_list,
                                           nodelist_nodelist_t * second_list)
{

  int fstatus = 0;
  int same_pattern = 0;

  /* first list must not be empty */
  if(first_list->rangelist.ranges_nb <= 0)
    return fstatus;

  /* test if the two lists represent the same node name pattern (prefix...suffix) */
  same_pattern = nodelist_nodelist_equal_patterns(first_list, second_list);

  /* we go further only if they represent the same node name pattern */
  if(same_pattern)
    {
      /* now we just have to test if ranges array intersect */
      fstatus = nodelist_rangelist_intersects(&(first_list->rangelist),
                                              &(second_list->rangelist));
    }

  return fstatus;
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if two nodes list intersect
 *
 * \param first_list first nodes list
 * \param second_list second nodes list
 *
 * \retval  0 nodes lists don't intersect
 * \retval  1 nodes lists intersect
*/
int
nodelist_nodelist_intersects(nodelist_nodelist_t * first_list,
                             nodelist_nodelist_t * second_list)
{

  int fstatus = 0;
  nodelist_nodelist_t *list1;
  nodelist_nodelist_t *list2;

  list2 = second_list;
  /* we have to check that at least one sub list of the second arg list */
  /* intersects with one sublist of the fist arg */
  while(list2 != NULL)
    {
      list1 = first_list;
      while(list1 != NULL)
        {
          fstatus = nodelist_nodelist_non_recursive_intersects(list1, list2);
          /* intersects! break! */
          if(fstatus)
            break;
          list1 = list1->next;
        }
      /* intersects! break! */
      if(fstatus)
        break;
      list2 = list2->next;
    }

  return fstatus;
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if the second list is included in the first in a non recursive way
 *
 * \param first_list first nodes list
 * \param second_list second nodes list
 *
 * \retval  0 the second list is not included
 * \retval  1 the second list is included
*/
int
nodelist_nodelist_non_recursive_includes(nodelist_nodelist_t * first_list,
                                         nodelist_nodelist_t * second_list)
{
  int fstatus = 0;
  int same_pattern = 0;
  /* lists must not be empty */
  if(first_list->rangelist.ranges_nb <= 0 || second_list->rangelist.ranges_nb <= 0)
    return fstatus;
  /* test if the two lists represent the same node name pattern (prefix...suffix) */
  same_pattern = nodelist_nodelist_equal_patterns(first_list, second_list);

  /* we go further only if they represent the same node name pattern */
  if(same_pattern)
    {
      /* now we juste have to test if second list ranges are included in the first list ranges */
      fstatus =
          nodelist_rangelist_includes(&(first_list->rangelist),
                                      &(second_list->rangelist));
    }
  return fstatus;
}

/*!
 * \ingroup NODELIST_COMMON
 * \brief Check if the second list is included in the first
 *
 * \param first_list first nodes list
 * \param second_list second nodes list
 *
 * \retval  0 the second list is not included
 * \retval  1 the second list is included
*/
int
nodelist_nodelist_includes(nodelist_nodelist_t * first_list,
                           nodelist_nodelist_t * second_list)
{
  int fstatus = 0;
  nodelist_nodelist_t *list1;
  nodelist_nodelist_t *list2;

  list2 = second_list;
  /* we have to check that every node list in the second arg */
  /* are included in one list of the fist arg */
  while(list2 != NULL)
    {
      list1 = first_list;
      while(list1 != NULL)
        {
          fstatus = nodelist_nodelist_non_recursive_includes(list1, list2);
          /* included! go to next second list sublist */
          if(fstatus)
            break;
          list1 = list1->next;
        }
      /* this second list sublist is not included, so break with fstatus=0 */
      if(fstatus == 0)
        break;
      list2 = list2->next;
    }
  return fstatus;
}

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
int nodelist_nodelist_add_ids(nodelist_nodelist_t * nodelist, char *idlist)
{
  int fstatus = 0;
  fstatus = nodelist_rangelist_add_list(&(nodelist->rangelist), idlist);
  return fstatus;
}

int nodelist_common_split_nodelist_entry(char *list, char **p_prefix, char **p_idlist,
                                         char **p_suffix)
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
  prefix = (char *)malloc((strlen(list) + 1) * sizeof(char));
  if(prefix != NULL)
    {
      while(*prefix_end != '[' && !isdigit(*prefix_end) && prefix_end < list_end)
        prefix_end++;
      if(prefix_end != prefix_begin && prefix_end <= list_end)
        {
          memcpy(prefix, list, (prefix_end - prefix_begin) * sizeof(char));
          prefix[prefix_end - prefix_begin] = '\0';
        }
      else
        {
          xfree(prefix);
        }
    }
  else
    {
      fstatus = -1;
    }

  /* get idlist */
  /* search for idlist begin */
  if(prefix != NULL)
    idlist_begin = prefix_end;
  else
    idlist_begin = list;
  while(*idlist_begin == '[' && idlist_begin < list_end)
    idlist_begin++;
  idlist_end = idlist_begin;
  if(idlist_begin < list_end)
    {
      /* search for idlist end */
      while((isdigit(*idlist_end) || *idlist_end == ',' || *idlist_end == '-')
            && idlist_end < list_end)
        {
          idlist_end++;
        }
      /* remove trailing dash, like in node%d-eth */
      if(*(idlist_end - 1) == '-')
        {
          idlist_end--;
          while(*(idlist_end - 1) == '-')
            idlist_end--;
        }
      /* dump idlist */
      idlist = (char *)malloc((idlist_end - idlist_begin + 1) * sizeof(char));
      if(idlist != NULL)
        {
          memcpy(idlist, idlist_begin, (idlist_end - idlist_begin) * sizeof(char));
          idlist[idlist_end - idlist_begin] = '\0';
        }
      else
        {
          fstatus = -1;
        }
    }

  /* get suffix */
  /* search for suffix begin */
  suffix_begin = idlist_end;
  while(*suffix_begin == ']' && suffix_begin < list_end)
    suffix_begin++;
  /* dump suffix */
  if(suffix_begin != list_end)
    {
      suffix = (char *)malloc((list_end - suffix_begin + 1) * sizeof(char));
      if(suffix != NULL)
        {
          memcpy(suffix, suffix_begin, (list_end - suffix_begin) * sizeof(char));
          suffix[list_end - suffix_begin] = '\0';
        }
      else
        {
          fstatus = -1;
        }
    }

  if(fstatus != 0)
    {
      xfree(prefix);
      xfree(idlist);
      xfree(suffix);
    }

  *p_prefix = prefix;
  *p_idlist = idlist;
  *p_suffix = suffix;

  return fstatus;
}

int nodelist_nodelist_init(nodelist_nodelist_t * nodelist, char **lists, int lists_nb)
{

  int fstatus = 0;
  int i;
  char *list;

  int operation = 1;            /* 1=add 2=remove */

  nodelist->next = NULL;
  fstatus += nodelist_rangelist_init(&(nodelist->rangelist));
  fstatus += nodelist_nodepattern_init(&(nodelist->pattern));

  if(fstatus == 0)
    {
      for(i = 0; i < lists_nb; i++)
        {
          list = lists[i];

          /* set operation if required */
          if(strlen(list) == 1)
            {
              if(*list == '-')
                {
                  operation = 2;
                  continue;
                }
              if(*list == '+')
                {
                  operation = 1;
                  continue;
                }
              else
                operation = 1;
            }

          /* do action */
          switch (operation)
            {

            case 1:
              fstatus += nodelist_nodelist_add_nodes(nodelist, list);
              break;

            case 2:
              nodelist_nodelist_remove_nodes(nodelist, list);
              break;

            }

          /* setting default operation */
          operation = 1;
        }
    }

  if(fstatus)
    return -1;
  else
    return fstatus;
}

int nodelist_nodelist_free_contents(nodelist_nodelist_t * nodelist)
{

  int fstatus = -1;

  if(nodelist->next != NULL)
    nodelist_nodelist_free_contents(nodelist->next);
  nodelist->next = NULL;
  nodelist_nodepattern_free_contents(&(nodelist->pattern));
  nodelist_rangelist_free_contents(&(nodelist->rangelist));

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
int
nodelist_nodelist_copy(nodelist_nodelist_t * dest_list, nodelist_nodelist_t * src_list)
{
  int fstatus = -1;

  nodelist_nodelist_t **pwldest;
  nodelist_nodelist_t **pwlsrc;

  nodelist_nodelist_free_contents(dest_list);

  pwldest = &dest_list;
  if(nodelist_nodelist_init(*pwldest, NULL, 0) == 0)
    {

      if(src_list->pattern.prefix == NULL && src_list->pattern.suffix == NULL)
        {
          // second list is empty... so initialization will be sufficient
          fstatus = 0;
        }
      else
        {

          pwlsrc = &src_list;
          while(*pwlsrc != NULL)
            {
              fstatus = -1;

              if(nodelist_nodepattern_init_by_copy
                 (&((*pwldest)->pattern), &((*pwlsrc)->pattern)) != 0)
                {
                  // unable to copy pattern, break
                  fstatus = -2;
                  break;
                }
              else
                {
                  if((*pwlsrc)->pattern.basic != 1)
                    {
                      // add ids
                      if(nodelist_rangelist_init_by_copy
                         (&((*pwldest)->rangelist), &((*pwlsrc)->rangelist)) != 0)
                        {
                          // unable to copy range list, break
                          fstatus = -3;
                          break;
                        }
                    }
                  pwldest = &((*pwldest)->next);
                  fstatus = 0;
                }

              pwlsrc = &((*pwlsrc)->next);
            }

          if(fstatus != 0)
            nodelist_nodelist_free_contents(dest_list);

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
int nodelist_nodelist_is_empty(nodelist_nodelist_t * nodelist)
{

  if(nodelist->pattern.prefix == NULL && nodelist->pattern.suffix == NULL)
    {
      return 1;
    }
  else
    {
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
int
nodelist_nodelist_add_nodelist(nodelist_nodelist_t * nodelist,
                               nodelist_nodelist_t * second_list)
{
  int fstatus = -1;

  nodelist_nodelist_t **pwldest;
  nodelist_nodelist_t **pwlsrc;

  /* If second list is emty, nothing to add */
  if(nodelist_nodelist_is_empty(second_list))
    {
      return 0;
    }

  /* If nodelist is empty, duplicate second_list! */
  if(nodelist_nodelist_is_empty(nodelist))
    {
      fstatus = nodelist_nodelist_copy(nodelist, second_list);
    }
  /* we have to add each second list sublist to the first one */
  else
    {

      pwlsrc = &second_list;
      while(*pwlsrc != NULL)
        {

          /* try to add src sublist to an existant dest list sublist */
          pwldest = &nodelist;
          while(*pwldest != NULL)
            {

              /* if patterns equal, try to add ids and break */
              if(nodelist_nodepattern_equals
                 ((&(*pwldest)->pattern), &((*pwlsrc)->pattern)))
                {
                  if((*pwldest)->pattern.padding < (*pwlsrc)->pattern.padding)
                    nodelist_nodepattern_set_padding(&((*pwldest)->pattern),
                                                     (*pwlsrc)->pattern.padding);
                  fstatus =
                      nodelist_rangelist_add_rangelist(&((*pwldest)->rangelist),
                                                       &((*pwlsrc)->rangelist));
                  break;
                }

              pwldest = &((*pwldest)->next);    /* increment dst sublist */
            }

          /* add a new sublist to dest list if no equivalent pattern list was found */
          if(*pwldest == NULL)
            {
              *pwldest = (nodelist_nodelist_t *) malloc(sizeof(nodelist_nodelist_t));
              if(*pwldest != NULL)
                {
                  fstatus = nodelist_nodelist_init(*pwldest, NULL, 0);
                  if(fstatus == 0)
                    {
                      fstatus =
                          nodelist_nodepattern_init_by_copy(&((*pwldest)->pattern),
                                                            &((*pwlsrc)->pattern));
                      if(fstatus == 0)
                        {
                          fstatus =
                              nodelist_rangelist_add_rangelist(&((*pwldest)->rangelist),
                                                               &((*pwlsrc)->rangelist));
                        }
                    }
                }
            }

          /* fstatus != 0 means that an error occured, break */
          if(fstatus != 0)
            {
              break;
            }

          pwlsrc = &((*pwlsrc)->next);  /* increment src sublist */
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
int
nodelist_nodelist_remove_nodelist(nodelist_nodelist_t * nodelist,
                                  nodelist_nodelist_t * second_list)
{
  int fstatus = -1;

  int add_flag;

  nodelist_nodelist_t worklist;
  nodelist_nodelist_t **pwldest;
  nodelist_nodelist_t **pwlsrc;

  /* If second list is emty, nothing to remove */
  if(nodelist_nodelist_is_empty(second_list))
    {
      return 0;
    }

  /* If nodelist is empty, nothing to remove */
  if(nodelist_nodelist_is_empty(nodelist))
    {
      return 0;
    }
  /* we have to remove each second list sublist from the first one */
  else
    {

      /* initialize work list by copying the first nodelist */
      fstatus = nodelist_nodelist_init(&worklist, NULL, 0);
      if(fstatus == 0)
        {

          pwldest = &nodelist;
          while(*pwldest != NULL)
            {

              add_flag = 1;
              pwlsrc = &second_list;
              while(*pwlsrc != NULL)
                {

                  /* if patterns equal, try to remove ids and break */
                  if(nodelist_nodepattern_equals
                     ((&(*pwldest)->pattern), &((*pwlsrc)->pattern)))
                    {
                      add_flag = 0;
                      if((*pwldest)->pattern.basic == 0)
                        {
                          fstatus +=
                              nodelist_rangelist_remove_rangelist(&
                                                                  ((*pwldest)->rangelist),
                                                                  &((*pwlsrc)->
                                                                    rangelist));
                        }
                      else
                        fstatus = 0;
                      fprintf(stdout, "fstatus %d\n", fstatus);
                      break;
                    }

                  pwlsrc = &((*pwlsrc)->next);  /* increment src sublist */
                }

              if(fstatus)
                break;

              if(add_flag == 1)
                {
                  fstatus += nodelist_nodelist_add_nodelist(&worklist, *pwldest);
                }

              if(fstatus)
                break;

              pwldest = &((*pwldest)->next);    /* increment dest sublist */
            }

          if(fstatus == 0)
            {
              fstatus = nodelist_nodelist_copy(nodelist, &worklist);
            }

          nodelist_nodelist_free_contents(&worklist);
        }

    }

  return fstatus;
}

int nodelist_nodelist_add_nodes(nodelist_nodelist_t * nodelist, char *list)
{

  int fstatus = -1;
  int status;

  char *prefix;
  char *idlist;
  char *suffix;

  int token_nb, i;
  char *token;

  nodelist_nodelist_t wlist;

  if(nodelist_common_string_get_tokens_quantity(list, ",", &token_nb) == 0)
    {
      token = NULL;

      for(i = 1; i <= token_nb; i++)
        {
          if(nodelist_common_string_get_token(list, ",", i, &token) == 0)
            {

              status =
                  nodelist_common_split_nodelist_entry(token, &prefix, &idlist, &suffix);
              if(status)
                {
                  fstatus = -1;
                }
              else
                {
                  fstatus = nodelist_nodelist_init(&wlist, NULL, 0);
                  if(fstatus == 0)
                    {
                      nodelist_nodepattern_set_prefix(&(wlist.pattern), prefix);
                      nodelist_nodepattern_set_suffix(&(wlist.pattern), suffix);
                      if(idlist != NULL)
                        {
                          wlist.pattern.basic = 0;
                          fstatus = nodelist_nodelist_add_ids(&wlist, idlist);
                          nodelist_nodepattern_set_padding(&(wlist.pattern), fstatus);
                          fstatus = 0;
                        }

                      fstatus = nodelist_nodelist_add_nodelist(nodelist, &wlist);

                      nodelist_nodelist_free_contents(&wlist);
                    }

                  xfree(prefix);
                  xfree(suffix);
                  xfree(idlist);
                }

              free(token);
            }
          token = NULL;
        }

    }

  return fstatus;

}

int nodelist_nodelist_remove_nodes(nodelist_nodelist_t * nodelist, char *list)
{

  int fstatus = -1;
  int status;

  char *prefix;
  char *idlist;
  char *suffix;

  int token_nb, i;
  char *token;

  nodelist_nodelist_t wlist;

  if(nodelist_common_string_get_tokens_quantity(list, ",", &token_nb) == 0)
    {
      token = NULL;
      for(i = 1; i <= token_nb; i++)
        {
          if(nodelist_common_string_get_token(list, ",", i, &token) == 0)
            {

              status =
                  nodelist_common_split_nodelist_entry(token, &prefix, &idlist, &suffix);
              if(status)
                {
                  fstatus = -1;
                }
              else
                {
                  fstatus = nodelist_nodelist_init(&wlist, NULL, 0);
                  if(fstatus == 0)
                    {
                      nodelist_nodepattern_set_prefix(&(wlist.pattern), prefix);
                      nodelist_nodepattern_set_suffix(&(wlist.pattern), suffix);
                      if(idlist != NULL)
                        {
                          wlist.pattern.basic = 0;
                          fstatus = nodelist_nodelist_add_ids(&wlist, idlist);
                          nodelist_nodepattern_set_padding(&(wlist.pattern), fstatus);
                          fstatus = 0;
                        }

                      fstatus = nodelist_nodelist_remove_nodelist(nodelist, &wlist);

                      nodelist_nodelist_free_contents(&wlist);
                    }

                  xfree(prefix);
                  xfree(suffix);
                  xfree(idlist);
                }

              free(token);
            }
          token = NULL;
        }

    }

  return fstatus;

}

int nodelist_nodelist_add_nodes_range(nodelist_nodelist_t * nodelist, long int from_id,
                                      long int to_id)
{

  int fstatus = -1;

  nodelist_range_t r;

  if(from_id <= to_id)
    {
      r.from = from_id;
      r.to = to_id;
    }
  else
    {
      r.from = to_id;
      r.to = from_id;
    }

  fstatus = nodelist_rangelist_add_range(&(nodelist->rangelist), &r);

  return fstatus;

}

long int nodelist_nodelist_non_recursive_nodes_quantity(nodelist_nodelist_t * nodelist)
{

  long int quantity;
  long int i;

  quantity = 0;
  if(nodelist->pattern.basic == 1)
    {
      quantity++;
    }
  else
    {
      for(i = 0; i < nodelist->rangelist.ranges_nb; i++)
        {
          quantity +=
              (nodelist->rangelist.array[i].to - nodelist->rangelist.array[i].from + 1);
        }
    }

  return quantity;
}

long int nodelist_nodelist_nodes_quantity(nodelist_nodelist_t * nodelist)
{

  long int quantity;

  nodelist_nodelist_t *nlist;

  quantity = 0;
  nlist = nodelist;
  while(nlist != NULL)
    {
      quantity += nodelist_nodelist_non_recursive_nodes_quantity(nlist);
      nlist = nlist->next;
    }

  return quantity;
}

int nodelist_nodelist_get_extended_string(nodelist_nodelist_t * nodelist, char **p_string)
{

  int fstatus = 0;

  nodelist_nodelist_t *nlist;

  char *node_string;
  size_t node_string_size;

  char *output_string;
  size_t output_string_size = 1024;

  long int i, j;

  char id_print_format[128];

  char *prefix;
  char *suffix;

  output_string = (char *)malloc(output_string_size * sizeof(char));
  if(output_string)
    {
      output_string[0] = '\0';

      /* node list sublist loop */
      nlist = nodelist;
      while(nlist != NULL)
        {

          /* build sublist padded id format */
          prefix = nlist->pattern.prefix;
          suffix = nlist->pattern.suffix;
          snprintf(id_print_format, 128, "%%s%%0.%uu%%s", nlist->pattern.padding);

          node_string_size = 0;
          if(prefix != NULL)
            node_string_size += strlen(prefix);
          if(suffix != NULL)
            node_string_size += strlen(suffix);
          node_string_size += MAX_LONG_INT_STRING_SIZE;
          node_string = (char *)malloc(node_string_size * sizeof(char));
          if(node_string != NULL)
            {

              if(nlist->pattern.basic == 1)
                {
                  /* add basic node */
                  snprintf(node_string, node_string_size, "%s%s",
                           (prefix == NULL) ? "" : prefix,
                           (suffix == NULL) ? "" : suffix);
                  if(nodelist_common_string_appends_and_extends
                     (&output_string, &output_string_size, node_string_size, node_string,
                      ","))
                    {
                      fstatus = -1;
                    }
                  else
                    {
                      fstatus = 0;
                    }
                }
              else
                {
                  /* add enumerated nodes */
                  for(i = 0; i < nlist->rangelist.ranges_nb; i++)
                    {
                      for(j = nlist->rangelist.array[i].from;
                          j <= nlist->rangelist.array[i].to; j++)
                        {

                          snprintf(node_string, node_string_size, id_print_format,
                                   (prefix == NULL) ? "" : prefix, j,
                                   (suffix == NULL) ? "" : suffix);
                          if(nodelist_common_string_appends_and_extends
                             (&output_string, &output_string_size, node_string_size,
                              node_string, ","))
                            {
                              fstatus = -1;
                            }
                          else
                            {
                              fstatus = 0;
                            }
                        }
                    }
                }

              free(node_string);
            }

          nlist = nlist->next;
        }

      if(fstatus != 0)
        {
          free(output_string);
        }
      else
        {
          *p_string = output_string;
        }
    }

  return fstatus;
}

int nodelist_nodelist_get_compacted_string(nodelist_nodelist_t * nodelist,
                                           char **p_string)
{

  int fstatus = -1;

  nodelist_nodelist_t *nlist;

  int brackets_flag;

  char *range_string;
  size_t range_string_size;

  char *ranges_string;
  size_t ranges_string_size;

  char *output_string;
  size_t output_string_size = 1024;

  long int nodes_nb;
  long int i;

  long int from, to;

  char id_print_format[128];
  char id_range_print_format[128];

  char *prefix;
  char *suffix;

  /* initialize output string */
  output_string = (char *)malloc(output_string_size * sizeof(char));
  if(output_string)
    {
      output_string[0] = '\0';

      /* node list sublist loop */
      nlist = nodelist;
      while(nlist != NULL)
        {

          prefix = nlist->pattern.prefix;
          suffix = nlist->pattern.suffix;

          nodes_nb = nodelist_nodelist_non_recursive_nodes_quantity(nlist);
          if(nodes_nb == 0)
            {
              free(output_string);
              return fstatus;
            }
          else if(nodes_nb == 1)
            brackets_flag = 0;
          else
            brackets_flag = 1;

          if(nlist->pattern.basic == 1)
            {
              /* in case of basic node, just add it */
              ranges_string_size = 1;   // \0
              if(prefix != NULL)
                ranges_string_size += strlen(prefix);
              if(suffix != NULL)
                ranges_string_size += strlen(suffix);
              ranges_string = (char *)malloc(ranges_string_size * sizeof(char));
              if(ranges_string != NULL)
                {
                  snprintf(ranges_string, ranges_string_size, "%s%s",
                           (prefix == NULL) ? "" : prefix,
                           (suffix == NULL) ? "" : suffix);
                  fstatus = 0;
                }
            }
          else
            {
              /* enumerated sublist */
              snprintf(id_print_format, 128, "%%0.%uu", nlist->pattern.padding);
              snprintf(id_range_print_format, 128, "%%0.%uu-%%0.%uu",
                       nlist->pattern.padding, nlist->pattern.padding);

              range_string_size = 0;
              range_string_size = 2 * MAX_LONG_INT_STRING_SIZE + 2;

              ranges_string_size = 1024;
              ranges_string = (char *)malloc(ranges_string_size * sizeof(char));
              if(ranges_string != NULL)
                {
                  ranges_string[0] = '\0';
                  /* add prefix */
                  if(prefix != NULL)
                    nodelist_common_string_appends_and_extends(&ranges_string,
                                                               &ranges_string_size,
                                                               strlen(prefix), prefix,
                                                               "");
                  if(brackets_flag)
                    nodelist_common_string_appends_and_extends(&ranges_string,
                                                               &ranges_string_size, 1,
                                                               "[", "");
                  range_string = (char *)malloc(range_string_size * sizeof(char));
                  if(range_string != NULL)
                    {
                      range_string[0] = '\0';
                      for(i = 0; i < nlist->rangelist.ranges_nb; i++)
                        {
                          from = nlist->rangelist.array[i].from;
                          to = nlist->rangelist.array[i].to;
                          if(from == to)
                            snprintf(range_string, range_string_size, id_print_format,
                                     from);
                          else
                            snprintf(range_string, range_string_size,
                                     id_range_print_format, from, to);
                          if(i == 0)
                            fstatus =
                                nodelist_common_string_appends_and_extends(&ranges_string,
                                                                           &ranges_string_size,
                                                                           range_string_size,
                                                                           range_string,
                                                                           "");
                          else
                            fstatus =
                                nodelist_common_string_appends_and_extends(&ranges_string,
                                                                           &ranges_string_size,
                                                                           range_string_size,
                                                                           range_string,
                                                                           ",");
                          if(fstatus)
                            {
                              fstatus = -1;
                              break;
                            }
                          else
                            {
                              fstatus = 0;
                            }
                        }
                      free(range_string);
                    }
                  if(brackets_flag)
                    nodelist_common_string_appends_and_extends(&ranges_string,
                                                               &ranges_string_size, 1,
                                                               "]", "");
                  /* add suffix */
                  if(suffix != NULL)
                    nodelist_common_string_appends_and_extends(&ranges_string,
                                                               &ranges_string_size,
                                                               strlen(suffix), suffix,
                                                               "");
                }
            }

          /* add current list to global list */
          if(ranges_string == NULL)
            {
              fstatus = -1;
              break;
            }
          if(nodelist_common_string_appends_and_extends
             (&output_string, &output_string_size, ranges_string_size, ranges_string,
              ","))
            {
              fstatus = -1;
              free(ranges_string);
              break;
            }

          /* go to next sublist */
          nlist = nlist->next;
        }

      if(fstatus != 0)
        {
          free(output_string);
        }
      else
        {
          *p_string = output_string;
        }
    }

  return fstatus;
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
int nodelist_nodepattern_init(nodelist_nodepattern_t * np)
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
int
nodelist_nodepattern_init_by_copy(nodelist_nodepattern_t * np,
                                  nodelist_nodepattern_t * npin)
{
  int fstatus = -1;
  np->padding = npin->padding;
  np->basic = npin->basic;
  np->prefix = NULL;
  np->suffix = NULL;
  if(npin->prefix != NULL)
    {
      np->prefix = strdup(npin->prefix);
      if(np->prefix == NULL)
        {
          nodelist_nodepattern_free_contents(np);
          return fstatus;
        }
    }
  if(npin->suffix != NULL)
    {
      np->suffix = strdup(npin->suffix);
      if(np->suffix == NULL)
        {
          nodelist_nodepattern_free_contents(np);
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
int nodelist_nodepattern_free_contents(nodelist_nodepattern_t * np)
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
int nodelist_nodepattern_set_padding(nodelist_nodepattern_t * np, int padding)
{
  int fstatus = -1;
  if(np != NULL)
    {
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
int nodelist_nodepattern_set_prefix(nodelist_nodepattern_t * np, char *prefix)
{
  int fstatus = -1;
  if(np != NULL && prefix != NULL)
    {
      xfree(np->prefix);
      np->prefix = strdup(prefix);
      if(np->prefix != NULL)
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
int nodelist_nodepattern_set_suffix(nodelist_nodepattern_t * np, char *suffix)
{
  int fstatus = -1;
  if(np != NULL && suffix != NULL)
    {
      xfree(np->suffix);
      np->suffix = strdup(suffix);
      if(np->suffix != NULL)
        fstatus = 0;
    }
  return fstatus;
}

/*!
 * \brief Set bridge node pattern basic flag
 *
 * \param np pointer on a bridge node pattern structure
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nodelist_nodepattern_set_basic(nodelist_nodepattern_t * np)
{
  int fstatus = -1;
  if(np != NULL)
    {
      np->basic = 1;
      fstatus = 0;
    }
  return fstatus;
}

/*!
 * \brief Unset bridge node pattern basic flag
 *
 * \param np pointer on a bridge node pattern structure
 *
 * \retval  0 operation successfully done
 * \retval -1 operation failed
*/
int nodelist_nodepattern_unset_basic(nodelist_nodepattern_t * np)
{
  int fstatus = -1;
  if(np != NULL)
    {
      np->basic = 0;
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
int
nodelist_nodepattern_equals(nodelist_nodepattern_t * np1, nodelist_nodepattern_t * np2)
{
  int fstatus = -1;
  if(np1 != NULL && np2 != NULL)
    {
      fstatus = 0;
/*       /\* same padding ? *\/ */
/*       if(np1->padding!=np2->padding) */
/* 	return fstatus; */
      /* same basic flag ? */
      if(np1->basic != np2->basic)
        return fstatus;
      /* same prefix or lack of prefix ? */
      if(np1->prefix != NULL && np2->prefix != NULL)
        {
          if(strcmp(np1->prefix, np2->prefix) != 0)
            return fstatus;
        }
      else if(np1->prefix == NULL && np2->prefix != NULL)
        {
          return fstatus;
        }
      else if(np1->prefix != NULL && np2->prefix == NULL)
        {
          return fstatus;
        }
      /* same suffix or lack of suffix ? */
      if(np1->suffix != NULL && np2->suffix != NULL)
        {
          if(strcmp(np1->suffix, np2->suffix) != 0)
            return fstatus;
        }
      else if(np1->suffix == NULL && np2->suffix != NULL)
        {
          return fstatus;
        }
      else if(np1->suffix != NULL && np2->suffix == NULL)
        {
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
