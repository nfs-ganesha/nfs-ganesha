/**
 * \file    parse_type.c
 * \author  Cédric CABESSA
 * \brief   parse_type.c: transform number to string.
 * This file provide function to transform real and bigint to
 * a string and conversely.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "parse_type.h"

/**
 * Convert a number to string
 * @param str pointer on the output string.
 * @param num input number.
 * @return 0 on success.
 */
int real2str(char *str, double num)
{
  int n;

  n = snprintf(str, PRINT_LEN, "+%.5e", num);

  return n == PRINT_LEN + 1;
}

/**
 * Convert a string to a number
 * @param pnum pointer on the output number.
 * @param str input string.
 * @return 0 on success.
 */
int str2real(double *pnum, char *str)
{
  *pnum = strtod(str, NULL);

  return *pnum == 0 && str[0] != '0';
}

int big2str(char *str, int64_t num)
{
  int n;
  n = snprintf(str, PRINT_LEN, "%lld", (long long) num);
  return n == PRINT_LEN + 1;
}

int str2big(int64_t * pnum, char *str)
{
  *pnum = strtoll(str, NULL, 10);
  return *pnum == 0 && str[0] != '0';
}
