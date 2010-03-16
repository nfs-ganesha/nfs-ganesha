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
 * \file    $RCSfile: SemN.c,v $
 * \author  $Author: deniel $ 
 * \date    $Date: 2005/11/28 17:02:56 $
 * \brief   Portable system tools.
 *
 * Implements system utilities (like semaphores)
 * so that they are POSIX and work on
 * most plateforms.
 *
 * CVS history :
 *
 * $Log: SemN.c,v $
 * Revision 1.3  2005/11/28 17:02:56  deniel
 * Added CeCILL headers
 *
 * Revision 1.2  2005/03/15 14:23:47  leibovic
 * Changing init param.
 *
 * Revision 1.1  2004/08/16 09:41:21  deniel
 * Ajout des semaphores a N entrees de Thomas (fait au depart pour hpss_find)
 *
 * Revision 1.3  2004/06/28 09:26:53  leibovic
 * gestion des signaux
 *
 * Revision 1.2  2004/06/24 09:41:18  leibovic
 * ajout des fonctionnalite user et group.
 *
 * Revision 1.1  2004/06/11 12:18:05  leibovic
 * rename tools en systools
 *
 * Revision 1.2  2004/06/07 14:17:10  leibovic
 * Developpement du module de gestion des options de la ligne de commande.
 *
 * Revision 1.1  2004/06/03 14:54:22  leibovic
 * Developpement de semaphores "portables"
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include "SemN.h"
#include <stdio.h>

#define MODULE "SemN"

int semaphore_init(semaphore_t * sem, int value)
{

  int retval;

  if (!sem)
    return EINVAL;

  if (retval = pthread_mutex_init(&sem->mutex, NULL))
    return retval;

  if (retval = pthread_cond_init(&sem->cond, NULL))
    return retval;

  sem->count = value;

  return 0;

}

int semaphore_destroy(semaphore_t * sem)
{

  if (!sem)
    return EINVAL;

  pthread_cond_destroy(&sem->cond);
  pthread_mutex_destroy(&sem->mutex);

  return 0;

}

int semaphore_P(semaphore_t * sem)
{

  if (!sem)
    return EINVAL;

  /* enters into the critical section */
  pthread_mutex_lock(&sem->mutex);

  sem->count--;
  /* If there are no more tokens : wait */
  while (sem->count < 0)
    pthread_cond_wait(&sem->cond, &sem->mutex);

  /* leaves the critical section */
  pthread_mutex_unlock(&sem->mutex);

}

int semaphore_V(semaphore_t * sem)
{

  /* enters into the critical section */
  pthread_mutex_lock(&sem->mutex);

  sem->count++;

  /* If a thread was waiting, gives it a token */
  if (sem->count <= 0)
    pthread_cond_signal(&sem->cond);

  /* leaves the critical section */
  pthread_mutex_unlock(&sem->mutex);

}
