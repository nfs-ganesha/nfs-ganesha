/**
 * \file    parse_type.h
 * \author  Cédric CABESSA
 * \brief   parse_type.h: transform number to string
 * This file provide function to transform real and bigint to
 * a string and conversely.
 *
 */

#ifndef __PARSE_TYPE__
#define __PARSE_TYPE__

/** max length of the output string */
#define PRINT_LEN  20

/**
 * Convert a number to string
 * @param str pointer on the output string.
 * @param num input number.
 * @return 0 on success.
 */
int real2str(char *str, double num);

/**
 * Convert a string to a number
 * @param pnum pointer on the output number.
 * @param str input string.
 * @return 0 on success.
 */
int str2real(double *pnum, char *str);

int big2str(char *str, int64_t num);

int str2big(int64_t * pnum, char *str);

#endif                          /* __PARSE_DOUBLE__ */
