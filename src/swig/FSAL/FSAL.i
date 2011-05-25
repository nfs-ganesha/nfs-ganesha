// File : FSAL.i
%module FSAL
%{
#include "fsal.h"
#include "BuddyMalloc.h"
%}

/* typemaps for mkdir /create */
%typemap(in) fsal_accessmode_t {
  $1 = (fsal_accessmode_t)SvIV($input);
}

/* typemaps for readdir */
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

%typemap(in) fsal_count_t * {
  SV* tempsv;
  fsal_count_t count;
  if (!SvROK($input)) {
    croak("expected a reference\n");
  }
  tempsv = SvRV($input);
  if (!SvIOK(tempsv)) {
    croak("expected a int reference\n");
  }
  count = SvNV(tempsv);
  $1 = &count;
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

%typemap(in) fsal_off_t {
  SV* tempsv;
  int i = SvIV($input);
  $1 = (fsal_off_t)i;
}

%typemap(argout) fsal_boolean_t *, fsal_count_t *, fsal_size_t * {
  SV * tempsv;
  tempsv = SvRV($input);
  sv_setiv(tempsv, *$1);
}

%include "fsal.h"
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

/* #define to get */
int get_fsal_max_name_len()
{
  return FSAL_MAX_NAME_LEN;
}

int get_fsal_max_path_len()
{
  return FSAL_MAX_PATH_LEN;
}

int get_err_fsal_no_err()
{
  return ERR_FSAL_NO_ERROR;
}

/* global */
extern int fsal_init(char * filename,
                     int flag_v,
                     FILE * output
           );

FILE * get_output()
{
  return stdout;
}

/* @see commands_FSAL.c (shell) */
typedef struct cmdfsal_thr_info__
{
  int is_thread_ok;
  fsal_handle_t current_dir ;
  char current_path[FSAL_MAX_PATH_LEN];
  fsal_op_context_t     context;
  fsal_export_context_t exp_context;
  int opened;
  fsal_file_t current_fd;
} cmdfsal_thr_info_t;

/* parameters to call FSAL functions */
extern cmdfsal_thr_info_t * GetFSALCmdContext();
extern int Init_Thread_Context( FILE * output,
                                cmdfsal_thr_info_t  * context );

void copy_fsal_handle_t(fsal_handle_t *fh, fsal_handle_t * f) {
  *f = *fh;
}

/* mkdir */
int are_accessmode_equal(fsal_accessmode_t * m1, int m2)
{
  return (*m1 == (unsigned int)m2) ? TRUE : FALSE;
}

int get_int_from_accessmode(fsal_accessmode_t * m)
{
  return (int)*m;
}

/* getattrs */
int get_mask_attr_mode()
{
  return FSAL_ATTR_MODE;
}

int get_mask_attr_size()
{
  return FSAL_ATTR_SIZE;
}

int get_mask_attr_numlinks()
{
  return FSAL_ATTR_NUMLINKS;
}

/* readdir */
int get_mask_attr_type()
{
  return FSAL_ATTR_TYPE;
}

fsal_mdsize_t get_new_buffersize()
{
  return (fsal_mdsize_t) FSAL_READDIR_SIZE * sizeof(fsal_dirent_t);
}

fsal_dirent_t * get_new_dirent_buffer()
{
  return (fsal_dirent_t *)malloc(FSAL_READDIR_SIZE * sizeof(fsal_dirent_t));
}

void free_dirent_buffer(fsal_dirent_t * buff)
{
  free(buff);
}

/* write */
caddr_t get_caddr_from_string(char * s)
{
  return (caddr_t)s;
}

/* read */
caddr_t get_new_caddr(int i)
{
  char * s = (caddr_t)malloc(i*sizeof(char));
  return s;
}

void free_caddr(caddr_t s)
{
  free(s);
}

char * get_string_from_caddr(caddr_t s)
{
  return (char *)s;
}

/* print tools */
void print_friendly_fsal_handle_t(char * str_name, fsal_handle_t *f) 
{
  char tracebuff[256];      
  snprintHandle(tracebuff,256,f);
  printf("%s (@%s)\n",str_name,tracebuff);
}
%}
