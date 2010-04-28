/*
 * Header des outils de mesure du temps
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
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
