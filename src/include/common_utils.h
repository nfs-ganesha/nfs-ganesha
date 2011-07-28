/*
 * vim:expandtab:shiftwidth=4:tabstop=8:
 */

/**
 * Common tools for printing, parsing, ....
 *
 *
 */

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include <sys/types.h>          /* for caddr_t */

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_int(char *str);

/**
 * This function converts an octal to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_octal(char *str);

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A non null value on error.
 *         Else, 0.
 */
int s_read_int64(char *str, unsigned long long *out64);

int s_read_size(char *str, size_t * p_size);

/**
 * string to boolean convertion.
 * \return 1 for TRUE, 0 for FALSE, -1 on error
 */
int StrToBoolean(char *str);

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
int snprintmem(char *target, int tgt_size, caddr_t source, int mem_size);

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
int sscanmem(caddr_t target, int tgt_size, const char *str_source);

/* String parsing functions */

int find_space(char c);
int find_comma(char c);
int find_colon(char c);
int find_endLine(char c);
int find_slash(char c);

#endif
