#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include "nodelist.h"

/* Forward declaration */
int _nodelist_common_string_get_token_common(char *string, char *separators_list,
                                             int *p_token_nb, int token_id,
                                             char **p_token);

/*
  ----------------------------------------------------------------------------------------------------------------
  nodelist_common_string_get_tokens
  -------------------------------
*/
int nodelist_common_string_get_token(char *string, char *separators_list, int token_id,
                                     char **p_token)
{
  int fstatus = -1;
  int token_nb = 0;
  fstatus =
      _nodelist_common_string_get_token_common(string, separators_list, &token_nb,
                                               token_id, p_token);
  if(*p_token != NULL)
    fstatus = 0;
  else
    fstatus = -1;
  return fstatus;
}

/*
  ----------------------------
  nodelist_common_string_get_tokens
  ----------------------------------------------------------------------------------------------------------------
*/

/*
  ----------------------------------------------------------------------------------------------------------------
  nodelist_common_string_get_tokens_quantity
  -------------------------------------
*/
int nodelist_common_string_get_tokens_quantity(char *string, char *separators_list,
                                               int *p_token_nb)
{
  int fstatus = -1;
  fstatus =
      _nodelist_common_string_get_token_common(string, separators_list, p_token_nb, 0,
                                               NULL);
  return fstatus;
}

/*
  -------------------------------------
  nodelist_common_string_get_tokens_quantity
  ----------------------------------------------------------------------------------------------------------------
*/

/*
  ----------------------------------------------------------------------------------------------------------------
  appends_and_extends_string
  --------------------------

  Appends a char* giving a char* to append and an optionnal separator (NULL if no separator)

  char** p_io_string : pointer on a char* that will be appended and maybe extended if no enough memory allocated
  size_t* p_current_length : pointer on a size_t structure that contains the current size of the char* that will be write
  size_t inc_length : the incrementation step that could be use to extend char* memory allocation
  char* string2append : string to append
  char* separator : separator to put between current char* and sting to append

  return 0 if it succeeds
  -1 otherwise
  ----------------------------------------------------------------------------------------------------------------
*/
int nodelist_common_string_appends_and_extends(char **p_io_string,
                                               size_t * p_current_length,
                                               size_t inc_length, char *string2append,
                                               char *separator)
{
  int fstatus = -1;

  size_t new_output_length;
  size_t new_string_length;

  size_t output_string_length;
  size_t separator_length;
  size_t append_length;

  char *default_separator = " ";
  char *local_separator;

  if(*p_io_string != NULL && string2append != NULL)
    {

      if(separator != NULL)
        local_separator = separator;
      else
        local_separator = default_separator;

      if(strlen(*p_io_string) == 0)
        local_separator = "";

      output_string_length = strlen(*p_io_string);
      separator_length = strlen(local_separator);
      append_length = strlen(string2append);
      new_string_length = output_string_length + separator_length + append_length;
      if(new_string_length > *p_current_length)
        {
          new_output_length = *p_current_length;
          while(new_string_length > new_output_length)
            new_output_length += inc_length;
          *p_io_string =
              (char *)realloc(*p_io_string, (new_output_length + 1) * sizeof(char));
          if(*p_io_string != NULL)
            *p_current_length = new_output_length;
          else
            *p_current_length = 0;
        }

      if(*p_io_string != NULL)
        {

          strncpy(*p_io_string + output_string_length, local_separator, separator_length);
          strncpy(*p_io_string + output_string_length + separator_length, string2append,
                  append_length);
          *(*p_io_string + output_string_length + separator_length + append_length) =
              '\0';
          fstatus = 0;
        }

    }

  return fstatus;
}

/*
  --------------------------
  appends_and_extends_string
  ----------------------------------------------------------------------------------------------------------------
*/

/*
  ----------------------------------------------------------------------------------------------------------------
  nodelist_common_extended2condensed_nodelist
  -------------------------------------------

  Construit une liste de noeuds condensee a partir d'une liste de noeuds etendue

  code retour :
  le nombre de noeud si ok
  -1 si erreur

  Rq :  il est necessaire de liberer la memoire associee a *p_dst_list via un appel a free une fois son 
  utilisation terminee
  ----------------------------------------------------------------------------------------------------------------
*/
int nodelist_common_extended2condensed_nodelist(char *src_list, char **p_dst_list)
{

  int fstatus = -1, status;

  nodelist_nodelist_t nodelist;

  status = nodelist_nodelist_init(&nodelist, &src_list, 1);
  if(status == 0)
    {
      if(nodelist_nodelist_get_compacted_string(&nodelist, p_dst_list) == 0)
        fstatus = nodelist_nodelist_nodes_quantity(&nodelist);
      else
        fstatus = -1;
      nodelist_nodelist_free_contents(&nodelist);
    }
  else
    {
      fstatus = -1;
    }

  return fstatus;

}

/*
  -------------------------------------------
  nodelist_common_extended2condensed_nodelist
  ----------------------------------------------------------------------------------------------------------------
*/

/*
  ----------------------------------------------------------------------------------------------------------------
  nodelist_common_condensed2extended_nodelist
  -------------------------------------------

  Construit une liste de noeuds etendue a partir d'une liste de noeuds condensee

  code retour :
  le nombre de noeud si ok
  -1 si erreur

  Rq :  il est necessaire de liberer la memoire associee a *p_dst_list via un appel a free une fois son 
  utilisation terminee
  ----------------------------------------------------------------------------------------------------------------
*/
int nodelist_common_condensed2extended_nodelist(char *src_list, char **p_dst_list)
{

  int fstatus, status;

  nodelist_nodelist_t nodelist;

  status = nodelist_nodelist_init(&nodelist, &src_list, 1);
  if(status == 0)
    {
      if(nodelist_nodelist_get_extended_string(&nodelist, p_dst_list) == 0)
        fstatus = nodelist_nodelist_nodes_quantity(&nodelist);
      else
        fstatus = -1;
      nodelist_nodelist_free_contents(&nodelist);
    }
  else
    {
      fstatus = -1;
    }

  return fstatus;
}

/*
  -------------------------------------------
  nodelist_common_condensed2extended_nodelist
  ----------------------------------------------------------------------------------------------------------------
*/

/*
  ----------------------------------------------------------------------------------------------------------------
  PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE PRIVATE 
  ----------------------------------------------------------------------------------------------------------------
*/
static char *get_next_token(char *workingstr, char separator)
{
  char *current = workingstr;
  int in_bracket = 0;

  while(*current)
    {
      if(!in_bracket && (*current == '['))
        in_bracket = 1;
      else if(in_bracket && (*current == ']'))
        in_bracket = 0;
      else if(!in_bracket && (*current == separator))
        return current;

      current++;
    }
  return NULL;

}                               /* get_next_token */

int _nodelist_common_string_get_token_common(char *string, char *separators_list,
                                             int *p_token_nb, int token_id,
                                             char **p_token)
{
  int fstatus = -1;

  int i;

  size_t string_length;
  size_t separators_list_length;

  char *working_string;

  char *current_pointer;
  char *best_pointer;
  char *old_pointer;

  size_t copy_length;

  int local_token_nb;
  int end_of_loop;

  /*
     First we check that pointers are not NULL
   */
  if(string != NULL && separators_list != NULL)
    {
      string_length = strlen(string);
      separators_list_length = strlen(separators_list);
      /*
         Then, that their lengths are not null
       */
      if(string_length != 0 && separators_list_length != 0)
        {
          /*
             Then, the separators research loop start
           */
          working_string = string;
          old_pointer = working_string;
          local_token_nb = 1;
          end_of_loop = 0;
          while(!end_of_loop)
            {
              best_pointer = NULL;
              /*
                 Search the first occurence of a separator
               */
              for(i = 0; i < separators_list_length; i++)
                {
                  current_pointer =
                      get_next_token(working_string, *(separators_list + i));
                  if(best_pointer == NULL)
                    {
                      best_pointer = current_pointer;
                    }
                  else if(best_pointer > current_pointer && current_pointer != NULL)
                    {
                      best_pointer = current_pointer;
                    }
                }
              /*
                 If this token must be extracted, extract it
               */
              if(token_id == local_token_nb && (*p_token) == NULL)
                {
                  if(best_pointer == NULL)
                    copy_length = strlen(old_pointer);
                  else
                    copy_length = (size_t) (best_pointer - old_pointer);
                  *p_token = (char *)malloc((copy_length + 1) * sizeof(char));
                  if(*p_token != NULL)
                    {
                      (*p_token)[copy_length] = '\0';
                      strncpy(*p_token, old_pointer, copy_length);
                      fstatus++;
                    }
                  else
                    {
                      fstatus = -2;
                    }
                }
              /*
                 If no more occurences, break the loop
               */
              if(best_pointer == NULL)
                {
                  end_of_loop = 1;
                }
              /*
                 Otherwise, increment token counter and adjust working string
               */
              else
                {
                  local_token_nb++;
                  working_string = best_pointer + 1;
                  old_pointer = working_string;
                }
            }
          *p_token_nb = local_token_nb;
          fstatus++;
        }
    }

  return fstatus;
}
