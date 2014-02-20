#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include "abstract_mem.h"
#include "nodelist.h"

/* Forward declaration */
int _nl_common_string_get_token_common(char *string,
				       char *separators_list,
				       int *p_token_nb, int token_id,
				       char **p_token);

int nl_common_string_get_token(char *string, char *separators_list,
				     int token_id, char **p_token)
{
	int fstatus = -1;
	int token_nb = 0;
	fstatus =
	    _nl_common_string_get_token_common(string, separators_list,
						     &token_nb, token_id,
						     p_token);
	if (*p_token != NULL)
		fstatus = 0;
	else
		fstatus = -1;
	return fstatus;
}

int nl_common_string_get_tokens_quantity(char *string,
					       char *separators_list,
					       int *p_token_nb)
{
	int fstatus = -1;
	fstatus =
	    _nl_common_string_get_token_common(string, separators_list,
						     p_token_nb, 0, NULL);
	return fstatus;
}

static char *get_next_token(char *workingstr, char separator)
{
	char *current = workingstr;
	int in_bracket = 0;

	while (*current) {
		if (!in_bracket && (*current == '['))
			in_bracket = 1;
		else if (in_bracket && (*current == ']'))
			in_bracket = 0;
		else if (!in_bracket && (*current == separator))
			return current;

		current++;
	}
	return NULL;

}				/* get_next_token */

int _nl_common_string_get_token_common(char *string,
					     char *separators_list,
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
	if (string != NULL && separators_list != NULL) {
		string_length = strlen(string);
		separators_list_length = strlen(separators_list);
		/*
		   Then, that their lengths are not null
		 */
		if (string_length != 0 && separators_list_length != 0) {
			/*
			   Then, the separators research loop start
			 */
			working_string = string;
			old_pointer = working_string;
			local_token_nb = 1;
			end_of_loop = 0;
			while (!end_of_loop) {
				best_pointer = NULL;
				/*
				   Search the first occurence of a separator
				 */
				for (i = 0; i < separators_list_length; i++) {
					current_pointer =
					    get_next_token(working_string,
							   *(separators_list +
							     i));
					if (best_pointer == NULL) {
						best_pointer = current_pointer;
					} else if (best_pointer >
						   current_pointer
						   && current_pointer != NULL) {
						best_pointer = current_pointer;
					}
				}
				/*
				   If this token must be extracted, extract it
				 */
				if (token_id == local_token_nb
				    && (*p_token) == NULL) {
					if (best_pointer == NULL)
						copy_length =
						    strlen(old_pointer);
					else
						copy_length =
						    (size_t) (best_pointer -
							      old_pointer);
					*p_token =
					    gsh_malloc((copy_length + 1) *
						       sizeof(char));
					if (*p_token != NULL) {
						(*p_token)[copy_length] = '\0';
						strncpy(*p_token, old_pointer,
							copy_length);
						fstatus++;
					} else {
						fstatus = -2;
					}
				}
				/*
				   If no more occurences, break the loop
				 */
				if (best_pointer == NULL)
					end_of_loop = 1;

				/*  Otherwise, increment token counter
				 * and adjust working string */
				else {
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
