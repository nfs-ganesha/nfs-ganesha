// File : Cache_inode.i
%module Cache_inode
%{
#include "cache_inode.h"
#include "cache_content.h"
#include "BuddyMalloc.h"
%}

/* typemaps for create */
%typemap(in) fsal_accessmode_t {
  $1 = (fsal_accessmode_t)SvIV($input);
}

/* typemaps for read/write */
%typemap(in) fsal_size_t * {
  SV* tempsv;
  fsal_size_t size;
  if (!SvROK($input)) {
    croak("expected a reference\n");
  }
  tempsv = SvRV($input);
  if (!SvIOK(tempsv)) {
    croak("expected a int reference\n");
  }
  size = SvNV(tempsv);
  $1 = &size;
}

%typemap(in) fsal_boolean_t * {
  SV* tempsv;
  fsal_boolean_t bool;
  if (!SvROK($input)) {
    croak("expected a reference\n");
  }
  tempsv = SvRV($input);
  if (!SvIOK(tempsv)) {
    croak("expected a int reference\n");
  }
  bool = SvNV(tempsv);
  $1 = &bool;
}

%typemap(argout) fsal_size_t *, fsal_boolean_t * {
  SV * tempsv;
  tempsv = SvRV($input);
  sv_setiv(tempsv, *$1);
}

/* typemaps for readdir */
%typemap(in) cache_inode_endofdir_t *, unsigned int * pnbfound, unsigned int * pend_cookie {
  SV* tempsv;
  cache_inode_endofdir_t eod;
  if (!SvROK($input)) {
    croak("expected a reference\n");
  }
  tempsv = SvRV($input);
  if (!SvIOK(tempsv)) {
    croak("expected a int reference\n");
  }
  eod = SvNV(tempsv);
  $1 = &eod;
}

%typemap(argout) cache_inode_endofdir_t *, unsigned int * pnbfound, unsigned int * pend_cookie {
  SV * tempsv;
  tempsv = SvRV($input);
  sv_setiv(tempsv, *$1);
}

%include "cache_inode.h"
%include "BuddyMalloc.h"

%inline %{

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* Connectathon */
#define	CHMOD_RW	0666
#define	CHMOD_RWX	0777
#define CHMOD_NONE 0

/* global */
extern int fsal_init(char * filename,
                     int flag_v,
                     FILE * output
           );

extern int cacheinode_init(char * filename,
                           int flag_v,
                           FILE * output
           );

FILE * get_output()
{
  return stdout;
}

cache_entry_t * get_new_entry()
{
  cache_entry_t * entry = (cache_entry_t*)malloc(sizeof(cache_entry_t));
  return entry;
}

void free_entry(cache_entry_t * entry)
{
  free(entry);
}

void copy_entry(cache_entry_t * e1, cache_entry_t * e2)
{
  memcpy(e2, e1, sizeof(cache_entry_t));
}

/* @see commands_Cache_inode.c (shell) */
typedef struct cmdCacheInode_thr_info__
{
  int is_thread_init;
  fsal_export_context_t exp_context;
  fsal_op_context_t context ;
  cache_inode_status_t cache_status ;
  int is_client_init;
  cache_entry_t * pentry  ;
  char current_path[FSAL_MAX_PATH_LEN]; /* current path */
  cache_inode_client_t client ;
  cache_content_client_t dc_client ;

} cmdCacheInode_thr_info_t;

/* parameters to call Cache_inode functions */
extern cmdCacheInode_thr_info_t * RetrieveInitializedContext();

extern hash_table_t * ht ;

cache_inode_status_t * get_CacheStatus(cmdCacheInode_thr_info_t * pthr)
{
  return &(pthr->cache_status);
}

fsal_op_context_t * get_Context(cmdCacheInode_thr_info_t * pthr)
{
  return &(pthr->context);
}

cache_inode_client_t * get_Client(cmdCacheInode_thr_info_t * pthr)
{
  return &(pthr->client);
}

/* readdir */
cache_inode_dir_entry_t * get_new_dir_entry_array(int size)
{
  cache_inode_dir_entry_t * array = (cache_inode_dir_entry_t*)malloc(size*sizeof(cache_inode_dir_entry_t));
  return array;
}

fsal_name_t * get_name_from_dir_entry_array(cache_inode_dir_entry_t * array, int i)
{
  return &(array[i].name);
}

cache_entry_t * get_entry_from_dir_entry_array(cache_inode_dir_entry_t * array, int i)
{
  return array[i].pentry;
}

void free_dir_entry_array(cache_inode_dir_entry_t * array)
{
  free(array);
}

unsigned int * get_new_cookie_array(int size)
{
  unsigned int * array = (unsigned int*)malloc(size*sizeof(unsigned int));
  return array;
}

void free_cookie_array(unsigned int * array)
{
  free(array);
}

%}
