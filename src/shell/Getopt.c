/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    Getopt.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 17:03:35 $
 * \version $Revision: 1.4 $
 * \brief   GANESHA's version of getopt, to avoid portability issues.
 *
 * This source code is an adaptation of
 * the AT&T public domain source for getopt(3).
 *
 * $Log: Getopt.c,v $
 * Revision 1.4  2006/01/18 17:03:35  leibovic
 * Removing some warnings.
 *
 * Revision 1.2  2005/03/09 15:43:25  leibovic
 * Multi-OS compiling.
 *
 * Revision 1.1  2005/02/02 09:05:30  leibovic
 * Adding our own version of getopt, to avoid portability issues.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "Getopt.h"

#include <string.h>

/* LINTLIBRARY */

#ifndef NULL
#define NULL    0
#endif

#define EOF    (-1)
#define ERR(s, c)    if(Opterr){\
    extern int write(int, void *, unsigned);\
    char errbuf[2];\
    errbuf[0] = (char)c; errbuf[1] = '\n';\
    (void) write(2, argv[0], (unsigned)strlen(argv[0]));\
    (void) write(2, s, (unsigned)strlen(s));\
    (void) write(2, errbuf, 2);}

int Opterr = 1;
int Optind = 1;
int Optopt;
char *Optarg;

int Getopt(int argc, char *argv[], char *opts)
{
  static int sp = 1;
  register int c;
  register char *cp;

  if(sp == 1)
    {
      if(Optind >= argc || argv[Optind][0] != '-' || argv[Optind][1] == '\0')
        return (EOF);
      else if(strcmp(argv[Optind], "--") == 0)
        {
          Optind++;
          return (EOF);
        }
    }
  Optopt = c = argv[Optind][sp];
  if(c == ':' || (cp = index(opts, c)) == NULL)
    {
      ERR(": illegal option -- ", c);
      if(argv[Optind][++sp] == '\0')
        {
          Optind++;
          sp = 1;
        }
      return ('?');
    }
  if(*++cp == ':')
    {
      if(argv[Optind][sp + 1] != '\0')
        Optarg = &argv[Optind++][sp + 1];
      else if(++Optind >= argc)
        {
          ERR(": option requires an argument -- ", c);
          sp = 1;
          return ('?');
        }
      else
        Optarg = argv[Optind++];
      sp = 1;
    }
  else
    {
      if(argv[Optind][++sp] == '\0')
        {
          sp = 1;
          Optind++;
        }
      Optarg = NULL;
    }
  return (c);
}
