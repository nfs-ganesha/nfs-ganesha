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
 * Outils de mesure du temps
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/test/MesureTemps.c,v 1.3 2005/11/28 17:03:05 deniel Exp $
 *
 * $Log: MesureTemps.c,v $
 *
 * Revision 1.2  2004/08/19 08:08:12  deniel
 * Mise au carre des tests sur les libs dynamiques et insertions des mesures
 * de temps dans les tests
 *
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include "MesureTemps.h"

void MesureTemps(struct Temps *resultatp, struct Temps *basep)
{
  struct timeval t;

  gettimeofday(&t, NULL);
  if(basep == NULL)
    {
      resultatp->secondes = t.tv_sec;
      resultatp->micro_secondes = t.tv_usec;
    }
  else
    {
      long tempo;

      resultatp->secondes = t.tv_sec - basep->secondes;
      if((tempo = t.tv_usec - basep->micro_secondes) < 0)
        {
          resultatp->secondes--;
          resultatp->micro_secondes = tempo + 1000000;
        }
      else
        {
          resultatp->micro_secondes = tempo;
        }
    }
}

char *ConvertiTempsChaine(struct Temps temps, char *resultat)
{
  static char chaine[100];
  char *ptr;

  if(resultat == NULL)
    {
      ptr = chaine;
    }
  else
    {
      ptr = resultat;
    }
  sprintf(ptr, "%u.%.6llu", (unsigned int)temps.secondes,
          (unsigned long long)temps.micro_secondes);
  return (ptr);
}

struct Temps *ConvertiChaineTemps(char *chaine, struct Temps *resultatp)
{
  static struct Temps temps;
  struct Temps *tp;
  char *ptr;

  if(resultatp == NULL)
    {
      tp = &temps;
    }
  else
    {
      tp = resultatp;
    }
  ptr = strchr(chaine, '.');
  if(ptr == NULL)
    {
      tp->secondes = atoi(chaine);
      tp->micro_secondes = 0;
    }
  else
    {
      *ptr = '\0';
      tp->secondes = atoi(chaine);
      tp->micro_secondes = atoi(ptr + 1);
    }
  return (tp);
}
