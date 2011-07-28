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
 * Mesure de temps: Exemple d'utilisation
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/test/test_mesure_temps.c,v 1.4 2005/11/28 17:03:05 deniel Exp $
 *
 * $Log: test_mesure_temps.c,v $
 *
 * Revision 1.3  2005/08/12 07:11:17  deniel
 * Corrected cache_inode_readdir semantics
 *
 * Revision 1.2  2004/08/19 08:08:12  deniel
 * Mise au carre des tests sur les libs dynamiques et insertions des mesures
 * de temps dans les tests
 *
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/times.h>
#include "MesureTemps.h"

#define DUREE 3

int main(int argc, char *argv[])
{
  struct Temps debut, fin;

  printf("J'attends volontairement %d secondes pour verifier la routine de mesure\n",
         DUREE);

  MesureTemps(&debut, NULL);
  sleep(DUREE);
  MesureTemps(&fin, &debut);

  printf("duree allocation %s s\n", ConvertiTempsChaine(fin, NULL));

  exit(0);
}
