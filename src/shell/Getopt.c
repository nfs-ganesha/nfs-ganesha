/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
 * Revision 1.3  2005/11/28 17:02:59  deniel
 * Added CeCILL headers
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
