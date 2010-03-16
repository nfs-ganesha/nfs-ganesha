/*
 *
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
 *
 */

#ifndef _ERR_RPC_H
#define _ERR_RPC_H

#include "log_functions.h"

static family_error_t __attribute__ ((__unused__)) tab_error_rpc[] =
{
#define ERR_SVCUDP_CREATE  0
  {
  ERR_SVCUDP_CREATE, "ERR_SVCUDP_CREATE", "svcudp_create impossible"},
#define ERR_SVCTCP_CREATE  1
  {
  ERR_SVCTCP_CREATE, "ERR_SVCTCP_CREATE", "svctcp_create impossible"},
#define ERR_SVC_CREATE     2
  {
  ERR_SVC_CREATE, "ERR_SVC_CREATE", "svc_create impossible"},
#define ERR_SVC_REGISTER   3
  {
  ERR_SVC_REGISTER, "ERR_SVC_REGISTER", "svc_register impossible"},
#define ERR_CLNTUDP_CREATE 4
  {
  ERR_CLNTUDP_CREATE, "ERR_CLNTUDP_CREATE", "clntudp_create impossible"},
#define ERR_CLNTTCP_CREATE 5
  {
  ERR_CLNTTCP_CREATE, "ERR_CLNTTCP_CREATE", "clnttcp_create impossible"},
#define ERR_GETRPCBYNAME   6
  {
  ERR_GETRPCBYNAME, "ERR_GETRPCBYNAME", "getrpcbyname impossible"},
#define ERR_IOCTL_I_POP    7
  {
  ERR_IOCTL_I_POP, "ERR_IOCTL_I_POP", "ioctl I_POP impossible"},
#define ERR_IOCTL_I_PUSH   8
  {
  ERR_IOCTL_I_PUSH, "ERR_IOCTL_I_PUSH", "ioctl I_PUSH impossible"},
#define ERR_SVC_GETCALLER  9
  {
  ERR_SVC_GETCALLER, "ERR_SVC_GETCALLER", "svc_getcaller impossible"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif
