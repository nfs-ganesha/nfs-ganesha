// File : HashTable.i
%module HashTable
%{
#include "BuddyMalloc.h"
#include "HashData.h"
#include "HashTable.h"
%}

%include "HashData.h"
%include "HashTable.h"

%inline %{
void Swig_BuddyInit()
{
  BuddyInit(NULL);
}

extern void BuddyDumpMem(FILE * output);

/* Function used to convert a key/value to a string (struct hash_parameter_t -> key_to_str/val_to_str) */
int display_buff( hash_buffer_t * pbuff, char * str )
{
  return snprintf( str, HASHTABLE_DISPLAY_STRLEN, "%s", pbuff->pdata ) ;
}

/* Function used to compare two keys together */
int compare_string_buffer(  hash_buffer_t * buff1, hash_buffer_t * buff2 ) 
{
  /* Test if one of teh entries are NULL */
  if( buff1->pdata == NULL )
    return ( buff2->pdata == NULL ) ? 0 : 1 ;
  else
    {
      if( buff2->pdata == NULL )
        return -1 ; /* left member is the greater one */
      else
        return strcmp( buff1->pdata, buff2->pdata ) ;
    }
  /* This line should never be reached */
}

/* hash_func_key/hash_func_rbt */
extern unsigned int simple_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef );
extern unsigned int double_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef );
extern unsigned int rbt_hash_func( hash_parameter_t * p_hparam, hash_buffer_t * buffclef );

/* hash_parameter_t */

void set_hash_parameter_t_hash_func_key_simple(hash_parameter_t * ht)
{
  ht->hash_func_key = simple_hash_func;
}
void set_hash_parameter_t_hash_func_key_double(hash_parameter_t * ht)
{
  ht->hash_func_key = double_hash_func;
}

void set_hash_parameter_t_hash_func_rbt(hash_parameter_t * ht)
{
  ht->hash_func_rbt = rbt_hash_func;
}

void set_hash_parameter_t_compare_key(hash_parameter_t * ht) 
{
  ht->compare_key = compare_string_buffer;
}

void set_hash_parameter_t_key_to_str(hash_parameter_t * ht)
{
  ht->key_to_str = display_buff;
}

void set_hash_parameter_t_val_to_str(hash_parameter_t * ht)
{
  ht->val_to_str = display_buff;
}

/* hash_buffer_t */
void set_hash_buffer_t_pdata(hash_buffer_t * h, int length, char * data)
{
  h->len = length;
  h->pdata = data;
}

char * get_print_pdata(hash_buffer_t * h)
{
  char * s = (char*)malloc((h->len)*sizeof(char));
  sprintf(s, "%s", h->pdata);
  return s;
}

FILE * get_output()
{
  return stdout;
}

%}
