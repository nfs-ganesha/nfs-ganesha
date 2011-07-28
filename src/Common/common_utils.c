/*
 * vim:expandtab:shiftwidth=4:tabstop=8:
 */

/**
 * Common tools for printing, parsing, ....
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common_utils.h"

#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_int(char *str)
{

  int i;
  int out = 0;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '9'))
        return -1;              /* error */
      else
        {
          out *= 10;
          out += (int)(str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  return out;

}

/**
 * This function converts an octal to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_octal(char *str)
{

  int i;
  int out = 0;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '7'))
        return -1;              /* error */
      else
        {
          out *= 8;
          out += (int)(str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  return out;

}

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A non null value on error.
 *         Else, 0.
 */
int s_read_int64(char *str, unsigned long long *out64)
{

  int i;
  unsigned long long out = 0;

  if(!out64)
    return -1;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '9'))
        return -1;              /* error */
      else
        {
          out *= 10;
          out += (unsigned long long)(str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  *out64 = out;

  return 0;
}

int s_read_size(char *str, size_t * p_size)
{
  int i;
  size_t out = 0;

  if(!p_size)
    return -1;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '9'))
        return -1;              /* error */
      else
        {
          out *= 10;
          out += (size_t) (str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  *p_size = out;

  return 0;

}

/**
 * string to boolean convertion.
 * \return 1 for TRUE, 0 for FALSE, -1 on error
 */
int StrToBoolean(char *str)
{
  if(str == NULL)
    return -1;

  if(!strcasecmp(str, "1") || !strcasecmp(str, "TRUE") || !strcasecmp(str, "YES"))
    return 1;

  if(!strcasecmp(str, "0") || !strcasecmp(str, "FALSE") || !strcasecmp(str, "NO"))
    return 0;

  return -1;
}

/**
 * snprintmem:
 * Print the content of a handle, a cookie,...
 * to an hexa string.
 *
 * \param target (output):
 *        The target buffer where memory is to be printed in ASCII.
 * \param tgt_size (input):
 *        Size (in bytes) of the target buffer.
 * \param source (input):
 *        The buffer to be printed.
 * \param mem_size (input):
 *        Size of the buffer to be printed.
 *
 * \return The number of bytes written in the target buffer.
 */
int snprintmem(char *target, int tgt_size, caddr_t source, int mem_size)
{

  unsigned char *c = '\0';      /* the current char to be printed */
  char *str = target;           /* the current position in target buffer */
  int wrote = 0;

  for(c = (unsigned char *)source; c < ((unsigned char *)source + mem_size); c++)
    {
      int tmp_wrote = 0;

      if(wrote >= tgt_size)
        {
          target[tgt_size - 1] = '\0';
          break;
        }

      tmp_wrote = snprintf(str, tgt_size - wrote, "%.2X", (unsigned char)*c);
      str += tmp_wrote;
      wrote += tmp_wrote;

    }

  return wrote;

}

/* test if a letter is hexa */
#define IS_HEXA( c )  ( (((c) >= '0') && ((c) <= '9')) || (((c) >= 'A') && ((c) <= 'F')) || (((c) >= 'a') && ((c) <= 'f')) )

/* converts an hexa letter */
#define HEXA2BYTE( c ) ((unsigned char)(((c) >= '0') && ((c) <= '9')?((c) - '0'):\
                        (((c) >= 'A') && ((c) <= 'F')?((c)-'A'+10) :\
                        (((c) >= 'a') && ((c) <= 'f')?((c)-'a'+10) : /*error :*/ 0 ))))

/**
 * snscanmem:
 * Read the content of a string and convert it to a handle, a cookie,...
 *
 * \param target (output):
 *        The target address where memory is to be written.
 * \param tgt_size (input):
 *        Size (in bytes) of the target memory buffer.
 * \param str_source (input):
 *        A hexadecimal string that represents
 *        the data to be stored into memory.
 *
 * \return - The number of bytes read in the source string.
 *         - -1 on error.
 */
int sscanmem(caddr_t target, int tgt_size, const char *str_source)
{

  unsigned char *p_mem;         /* the current byte to be set */

  const char *p_src;            /* pointer to the current char to be read. */

  int nb_read = 0;

  p_src = str_source;

  for(p_mem = (unsigned char *)target; p_mem < ((unsigned char *)target + tgt_size);
      p_mem++)
    {

      unsigned char tmp_val;

      /* we must read 2 bytes (written in hexa) to have 1 target byte value. */
      if((*p_src == '\0') || (*(p_src + 1) == '\0'))
        {
          /* error, the source string is too small */
          return -1;
        }

      /* they must be hexa values */
      if(!IS_HEXA(*p_src) || !IS_HEXA(*(p_src + 1)))
        {
          return -1;
        }

      /* we read hexa values. */
      tmp_val = (HEXA2BYTE(*p_src) << 4) + HEXA2BYTE(*(p_src + 1));

      /* we had them to the target buffer */
      (*p_mem) = tmp_val;

      p_src += 2;
      nb_read += 2;

    }

  return nb_read;

}

/**
 * 
 * find_space : return TRUE is argument is a space character.
 *
 * return TRUE is argument is a space character.
 *
 * @param c character to test.
 * 
 * @return TRUE is argument is a space character.
 *
 */
int find_space(char c)
{
  return isspace(c);
}                               /* find_space */

/**
 * 
 * find_comma : return TRUE is argument is ','. 
 *
 * return TRUE is argument is ','
 *
 * @param c character to test.
 * 
 * @return TRUE is argument is ','. 
 *
 */
int find_comma(char c)
{
  return (c == ',') ? 1 : 0;
}                               /* find_comma */

/**
 * 
 * find_colon : return TRUE is argument is ':'. 
 *
 * return TRUE is argument is ':'
 *
 * @param c character to test.
 * 
 * @return TRUE is argument is ':'. 
 *
 */
int find_colon(char c)
{
  return (c == ':') ? 1 : 0;
}                               /* find_colon */

/**
 * 
 * find_endLine : return TRUE if character is a end of line.
 *
 * return TRUE if character is a end of line.
 *
 * @param c character to test.
 * 
 * @return TRUE if character is a end of line.
 *
 */
int find_endLine(char c)
{
  return (c == '\0' || c == '\n') ? 1 : 0;
}                               /* find_endLine */

/**
 * 
 * find_slash : return TRUE is argument is '/'. 
 *
 * return TRUE is argument is '/'
 *
 * @param c character to test.
 * 
 * @return TRUE is argument is '/'. 
 *
 */
int find_slash(char c)
{
  return (c == '/') ? 1 : 0;
}                               /* find_slash */
