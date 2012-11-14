/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 * \file    fsal_strings.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 08:30:37 $
 * \version $Revision: 1.5 $
 * \brief   FSAL names handling functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <string.h>

const fsal_name_t FSAL_DOT = { ".", 1 };
const fsal_name_t FSAL_DOT_DOT = { "..", 2 };

/**
 * @defgroup FSALNameFunctions Name handling functions.
 *
 * Those functions handle FS object names.
 * 
 * @{
 */

/**
 * FSAL_str2name :
 * converts a char * to an fsal_name_t.
 * 
 * \param string (in, char *)
 *        Address of the string  to be converted.
 * \param in_str_maxlen (in, fsal_mdsize_t)
 *        Maximum size for the string to be converted.
 *        If 0, just use strlen
 *        This function can take a string from a len/buffer
 *        where the buffer is not null terminated as long
 *        as len is passed as in_str_maxlen.
 * \param name (out, fsal_name_t *)
 *        The structure to be filled with the name.
 *
 * \return major codes :
 *      - ERR_FSAL_FAULT
 *      - ERR_FSAL_NAMETOOLONG
 */

fsal_status_t FSAL_str2name(const char *string, /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_name_t * name  /* OUT */
    )
{
  unsigned int i;

  /* sanity checks */
  if(!name || !string)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* computes input strlen */
  if(in_str_maxlen == 0)
    i = strlen(string);
  else
    i = strnlen(string, in_str_maxlen);

  /* the value of i doesn't include terminating '\0',
   * thus, FSAL_MAX_NAME_LEN is excluded.
   */
  if(i >= sizeof(name->name))
    ReturnCode(ERR_FSAL_NAMETOOLONG, 0);

  (*name).len = i;

  /* copies the string */
  memcpy(name->name , string, name->len);

  /* sets the terminating \0 */
  name->name[i] = '\0';

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_str2path :
 * converts a char * to an fsal_path_t.
 * 
 * \param string (in, char *)
 *        Address of the string  to be converted.
 * \param in_str_maxlen (in, fsal_mdsize_t)
 *        Maximum size for the string to be converted.
 *        If 0, just use strlen
 *        This function can take a string from a len/buffer
 *        where the buffer is not null terminated as long
 *        as len is passed as in_str_maxlen.
 * \param p_path (out, fsal_path_t *)
 *        The structure to be filled with the name.
 *
 * \return major codes :
 *      - ERR_FSAL_FAULT
 *      - ERR_FSAL_NAMETOOLONG
 */

fsal_status_t FSAL_str2path(char *string,       /* IN */
                            fsal_mdsize_t in_str_maxlen,        /* IN */
                            fsal_path_t * p_path        /* OUT */
    )
{
  unsigned int i;

  /* sanity checks */
  if(!p_path || !string)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* computes input strlen */
  if(in_str_maxlen == 0)
    i = strlen(string);
  else
    i = strnlen(string, in_str_maxlen);

  /* the value of i doesn't include terminating '\0',
   * thus, FSAL_MAX_PATH_LEN is excluded.
   */
  if(i >= sizeof(p_path->path))
    ReturnCode(ERR_FSAL_NAMETOOLONG, 0);

  p_path->len = i;

  /* copies the string */
  memcpy(p_path->path, string, p_path->len);

  /* sets the terminating \0 */
  p_path->path[i] = '\0';

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_name2str :
 * converts an fsal_name_t to a char * .
 * 
 * \param p_name (in, fsal_name_t * )
 *        Pointer to the structure to be converted.
 * \param string (out, char *)
 *        Address of the string  to be filled.
 * \param out_str_maxlen (in, fsal_mdsize_t)
 *        Maximum size for the string to be filled.
 *
 * \return major codes :
 *      - ERR_FSAL_FAULT
 *      - ERR_FSAL_TOOSMALL
 */

fsal_status_t FSAL_name2str(fsal_name_t * p_name,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    )
{
  /* sanity checks */
  if(!string || !p_name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* the value of i doesn't include terminating '\0',
   * thus, last value is excluded.
   */
  if(p_name->len >= out_str_maxlen)
    ReturnCode(ERR_FSAL_TOOSMALL, 0);

  /* copies the value, including terminating null */
  memcpy(string, p_name->name, p_name->len + 1);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_path2str :
 * converts an fsal_path_t to a char * .
 * 
 * \param p_path (in, fsal_path_t * )
 *        The structure to be converted.
 * \param string (out, char *)
 *        Address of the string  to be filled.
 * \param out_str_maxlen (in, fsal_mdsize_t)
 *        Maximum size for the string to be filled.
 *
 * \return major codes :
 *      - ERR_FSAL_FAULT
 *      - ERR_FSAL_TOOSMALL
 */

fsal_status_t FSAL_path2str(fsal_path_t * p_path,       /* IN */
                            char *string,       /* OUT */
                            fsal_mdsize_t out_str_maxlen        /* IN */
    )
{
  /* sanity checks */
  if(!string || !p_path)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* the value of i doesn't include terminating '\0',
   * thus, last value is excluded.
   */
  if(p_path->len >= out_str_maxlen)
    ReturnCode(ERR_FSAL_TOOSMALL, 0);

  /* copies the value, including terminating null */
  memcpy(string, p_path->path, p_path->len + 1);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * FSAL_namecmp :
 * compares two FSAL_name_t.
 *
 * \return The same value as strcmp.
 */
int FSAL_namecmp(const fsal_name_t *p_name1,
                 const fsal_name_t *p_name2)
{

  return (strncmp(p_name1->name, p_name2->name, FSAL_MAX_NAME_LEN));

}

/**
 * FSAL_pathcmp :
 * compares two FSAL_path_t.
 * 
 * \return The same value as strcmp.
 */
int FSAL_pathcmp(fsal_path_t * p_path1, fsal_path_t * p_path2)
{

  return (strncmp(p_path1->path, p_path2->path, FSAL_MAX_PATH_LEN));

}

/**
 * FSAL_namecpy.
 * copies a name to another.
 *
 * \param p_tgt_name pointer to target name.
 * \param p_src_name pointer to source name.
 * \return major code ERR_FSAL_FAULT, if tgt_name is NULL.
 */
fsal_status_t FSAL_namecpy(fsal_name_t * p_tgt_name, fsal_name_t * p_src_name)
{
  /* sanity check */
  if(!p_tgt_name || !p_src_name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* copy */
  memcpy(p_tgt_name->name, p_src_name->name, p_src_name->len + 1);

  p_tgt_name->len = p_src_name->len;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_pathcpy.
 * copies a path to another.
 *
 * \param p_tgt_name pointer to the target name.
 * \param p_src_name pointer to the source name.
 * \return major code ERR_FSAL_FAULT, if tgt_name is NULL.
 */
fsal_status_t FSAL_pathcpy(fsal_path_t * p_tgt_path, fsal_path_t * p_src_path)
{
  /* sanity check */
  if(!p_tgt_path || !p_src_path)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* copy */
  memcpy(p_tgt_path->path, p_src_path->path, p_src_path->len + 1);

  p_tgt_path->len = p_src_path->len;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_buffdesc2name:
 * Convert a buffer descriptor to an fsal name.
 */
fsal_status_t FSAL_buffdesc2name(fsal_buffdesc_t * in_buf, fsal_name_t * out_name)
{

  if(!in_buf || !out_name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  return FSAL_str2name(in_buf->pointer, in_buf->len, out_name);

}

/**
 * FSAL_buffdesc2path:
 * Convert a buffer descriptor to an fsal path.
 */
fsal_status_t FSAL_buffdesc2path(fsal_buffdesc_t * in_buf, fsal_path_t * out_path)
{

  if(!in_buf || !out_path)
    ReturnCode(ERR_FSAL_FAULT, 0);

  return FSAL_str2path(in_buf->pointer, in_buf->len, out_path);

}

/**
 * FSAL_path2buffdesc:
 * Convert an fsal path to a buffer descriptor (utf8 like).
 *
 * \param in_path (input):
 *        The fsal path to be converted.
 * \param out_buff (output):
 *        Pointer to the buffer descriptor to be filled.
 *
 * \warning The buffer descriptor only contains pointers to
 *          the in_path structure. Thus, if the in_path structure
 *          is modified or destroyed, the out_buff will be affected.
 */

fsal_status_t FSAL_path2buffdesc(fsal_path_t * in_path, fsal_buffdesc_t * out_buff)
{

  if(!out_buff || !in_path)
    ReturnCode(ERR_FSAL_FAULT, 0);

  out_buff->pointer = in_path->path;
  out_buff->len = in_path->len;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_name2buffdesc:
 * Convert an fsal name to a buffer descriptor (utf8 like).
 *
 * \param in_name (input):
 *        The fsal name to be converted.
 * \param out_buff (output):
 *        Pointer to the buffer descriptor to be filled.
 *
 * \warning The buffer descriptor only contains pointers to
 *          the in_name structure. Thus, if the in_name structure
 *          is modified or destroyed, the out_buff will be affected.
 */

fsal_status_t FSAL_name2buffdesc(fsal_name_t * in_name, fsal_buffdesc_t * out_buff)
{

  if(!out_buff || !in_name)
    ReturnCode(ERR_FSAL_FAULT, 0);

  out_buff->pointer = in_name->name;
  out_buff->len = in_name->len;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/* @} */
