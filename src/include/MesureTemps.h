/*
 * Header des outils de mesure du temps
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
 *
 *
 *
 */

#ifndef MesureTemps_h
#define MesureTemps_h

struct Temps
{
  unsigned long secondes;
  unsigned long micro_secondes;
};

void MesureTemps(struct Temps *, struct Temps *);
char *ConvertiTempsChaine(struct Temps, char *);
struct Temps *ConvertiChaineTemps(char *, struct Temps *);

#endif
