/**
 *
 * \file    cmd_tools.h
 * \author  $Author: leibovic $
 * \date    $Date $
 * \version $Revision $
 * \brief   Header file for functions used by several layers.
 *
 *
 * $Log: cmd_tools.h,v $
 * Revision 1.14  2006/01/24 13:49:33  leibovic
 * Adding missing includes.
 *
 * Revision 1.13  2006/01/20 14:44:14  leibovic
 * altgroups support.
 *
 * Revision 1.12  2006/01/17 14:56:22  leibovic
 * Adaptation de HPSS 6.2.
 *
 * Revision 1.11  2005/09/28 09:08:00  leibovic
 * thread-safe version of localtime.
 *
 * Revision 1.10  2005/08/12 11:56:58  leibovic
 * coquille.
 *
 * Revision 1.9  2005/08/12 11:21:27  leibovic
 * Now, set cat concatenate strings.
 *
 * Revision 1.8  2005/05/11 15:53:37  leibovic
 * Adding time function.
 *
 * Revision 1.7  2005/05/09 12:23:54  leibovic
 * Version 2 of ganeshell.
 *
 * Revision 1.6  2005/04/25 12:57:48  leibovic
 * Implementing setattr.
 *
 * Revision 1.5  2005/04/14 11:21:56  leibovic
 * Changing command syntax.
 *
 * Revision 1.4  2005/04/13 09:28:05  leibovic
 * Adding unlink and mkdir calls.
 *
 * Revision 1.3  2005/03/04 08:01:32  leibovic
 * removing snprintmem (moved to FSAL layer)à.
 *
 * Revision 1.2  2004/12/17 16:05:27  leibovic
 * Replacing %X with snprintmem for handles printing.
 *
 * Revision 1.1  2004/12/09 15:46:22  leibovic
 * Tools externalisation.
 *
 *
 */

#ifndef _CMD_TOOLS_H
#define _CMD_TOOLS_H

#ifdef _LINUX
#include <stddef.h>
#endif

#ifdef _APPLE
#include <stddef.h>
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "fsal.h"
#include <time.h>

#ifdef _SOLARIS
#ifndef offsetof
#define offsetof(type, member) ( (int) & ((type*)0) -> member )
#endif
#endif

/* thread-safe and PORTABLE version of localtime... */
struct tm *Localtime_r(const time_t * p_time, struct tm *p_tm);

/** 
 * my_atoi:
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int my_atoi(char *str);

/** 
 * atomode:
 * This function converts a string to a unix access mode.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the access mode integer.
 */
int atomode(char *str);

int ato64(char *str, unsigned long long *out64);

time_t atotime(char *str);
char *time2str(time_t time_in, char *str_out);

/**
 * split_path:
 * splits a path 'dir/dir/dir/obj' in two strings :
 * 'dir/dir/dir' and 'obj'.
 * 
 * \param in_path (in/out char *)
 * \param p_path (out char **)
 * \param p_file (out char **)
 */
void split_path(char *in_path, char **p_path, char **p_file);

/**
 * clean_path:
 * Transform a path to a cannonical path.
 * Remove //,  /./,  /../, final /
 * from a POSIX-like path.
 * 
 * \param str (in/out char*) The path to be transformed.
 * \param len (in int)       The max path length.
 * \return Nothing.
 */
void clean_path(char *str, int len);

/**
 * concat:
 * concatenates 2 strings with a limitation
 * of the size of the destination string.
 * 
 * \param str1 (in/out char*) The destination string.
 * \param str2 (in char*)    The string to be added at the end of str1.
 * \param max_len (in int)       The max str1 length.
 * \return NULL on error, the destination string, else.
 */
char *concat(char *str1, char *str2, size_t max_len);

/**
 * print_fsal_status:
 * this function prints an fsal_status_t to a given output file.
 *
 * \param output (in FILE *) The output where the status is to be printed.
 * \param status (in status) The status to be printed.
 *
 * \return Nothing.
 */
void print_fsal_status(FILE * output, fsal_status_t status);

/**
 * fsal_status_to_string:
 * this function converts an fsal_status_t to a a string buffer
 *
 * \param output (in char *) The output where the status is to be printed.
 * \param status (in status) The status to be printed.
 *
 * \return Nothing.
 */
void fsal_status_to_string(char * output, fsal_status_t status);

/**
 * print_fsal_attrib_mask:
 * Print an fsal_attrib_mask_t to a given output file.
 *
 * \param mask (in fsal_attrib_mask_t) The mask to be printed.
 * \param output (in FILE *) The output where the mask is to be printed.
 * \return Nothing.
 */
void print_fsal_attrib_mask(fsal_attrib_mask_t mask, FILE * output);

/**
 * strtype:
 * convert an FSAL object type to a string.
 *
 * \param type (in fsal_nodetype_t) The type to be printed.
 * \return The name (char *) for this FSAL object type.
 */
char *strtype(fsal_nodetype_t type);

/**
 * print_fsal_attributes:
 * print an fsal_attrib_list_t to a given output file.
 * 
 * \param attrs (in fsal_attrib_list_t) The attributes to be printed.
 * \param output (in FILE *) The file where the attributes are to be printed.
 * \return Nothing.
 */
void print_fsal_attributes(fsal_attrib_list_t attrs, FILE * output);

/**
 * print_item_line:
 * Prints a filesystem element on one line, like the Unix ls command.
 * 
 * \param out (in FILE*) The file where the item is to be printed.
 * \param attrib (in fsal_attrib_list_t *) the attributes for the
 *        item.
 * \param name (in char *) The name of the item to be printed.
 * \param target (in char *) It the item is a symbolic link,
 *        this contains the link target.
 * \return Nothing. 
 */
void print_item_line(FILE * out, fsal_attrib_list_t * attrib, char *name, char *target);

/*
 * Thoses structures are used to handle FSAL attributes.
 */

/** Type of attributes */
typedef enum shell_attr_type__
{

  ATTR_NONE = 0,                /* this special value is used
                                   to indicate the end of the attribute list */

  ATTR_32,                      /* 32 bits attribute */
  ATTR_64,                      /* 64 bits attribute */
  ATTR_OCTAL,                   /* octal number attribute */
  ATTR_TIME                     /* YYYYMMDDhhmmss attribute */
} shell_attr_type_t;

/** Attribute definition structure. */
typedef struct shell_attribute__
{

  char *attr_name;              /* name of the attribute. */
  shell_attr_type_t attr_type;  /* type of the attribute. */
  fsal_attrib_mask_t attr_mask; /* fsal mask constant */
  unsigned long int attr_offset;        /* fsal field offset */

} shell_attribute_t;

/**
 * This structure contains FSAL settable attributes.
 */
static shell_attribute_t __attribute__ ((__unused__)) shell_attr_list[] =
{
  {
  "SIZE", ATTR_64, FSAL_ATTR_SIZE, offsetof(fsal_attrib_list_t, filesize)},
  {
  "MODE", ATTR_OCTAL, FSAL_ATTR_MODE, offsetof(fsal_attrib_list_t, mode)},
  {
  "OWNER", ATTR_32, FSAL_ATTR_OWNER, offsetof(fsal_attrib_list_t, owner)},
  {
  "GROUP", ATTR_32, FSAL_ATTR_GROUP, offsetof(fsal_attrib_list_t, group)},
  {
  "ATIME", ATTR_TIME, FSAL_ATTR_ATIME, offsetof(fsal_attrib_list_t, atime)},
  {
  "CTIME", ATTR_TIME, FSAL_ATTR_CTIME, offsetof(fsal_attrib_list_t, ctime)},
  {
  "MTIME", ATTR_TIME, FSAL_ATTR_MTIME, offsetof(fsal_attrib_list_t, mtime)},
      /* end of the list */
  {
  NULL, ATTR_NONE, 0, 0}

};

/**
 * this function converts peers (attr_name=attr_value,attr_name=attr_value,...)
 * to a fsal_attrib_list_t to be used in the FSAL_setattr call).
 * \return 0 if no error occured,
 *         a non null value else.
 */
int MkFSALSetAttrStruct(char *attribute_list, fsal_attrib_list_t * fsal_set_attr_struct);

/* Time diffing function */
struct timeval time_diff(struct timeval time_from, struct timeval time_to);

/* Timeval printing function */
#define print_timeval(out,_tv_) fprintf(out,"%ld.%.6ld s\n",_tv_.tv_sec,_tv_.tv_usec)

int getugroups(int maxcount, gid_t * grouplist, char *username, gid_t gid);

#endif
