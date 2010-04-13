/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 * * --------------------------
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser* Ce logiciel est un serveur implementant le protocole NFS.
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
 *---------------------------------
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
 * therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"

static struct glist_head nlm_lock_list;
static pthread_mutex_t nlm_lock_list_mutex;

fsal_lockdesc_t *nlm_lock_to_fsal_lockdesc(struct nlm4_lock *nlm_lock, bool_t exclusive)
{
  fsal_lockdesc_t *fldesc;

  fldesc = (fsal_lockdesc_t *) Mem_Alloc(sizeof(fsal_lockdesc_t));
  if(!fldesc)
    return NULL;

  if(exclusive)
    fldesc->flock.l_type = F_WRLCK;
  else
    fldesc->flock.l_type = F_RDLCK;
  fldesc->flock.l_whence = SEEK_SET;
  fldesc->flock.l_start = nlm_lock->l_offset;
  fldesc->flock.l_len = nlm_lock->l_len;
  return fldesc;
}

static netobj *copy_netobj(netobj * dst, netobj * src)
{
  dst->n_bytes = (char *)Mem_Alloc(src->n_len);
  if(!dst->n_bytes)
    return NULL;
  dst->n_len = src->n_len;
  memcpy(dst->n_bytes, src->n_bytes, src->n_len);
  return dst;
}

static void net_obj_free(netobj * obj)
{
  if(obj->n_bytes)
    Mem_Free(obj->n_bytes);
}

static int netobj_compare(netobj obj1, netobj obj2)
{
  if(obj1.n_len != obj2.n_len)
    return 1;
  if(memcmp(obj1.n_bytes, obj2.n_bytes, obj1.n_len))
    return 1;
  return 0;
}

static nlm_lock_t *nlm4_lock_to_nlm_lock(struct nlm4_lock *nlm_lock, int exclusive)
{
  nlm_lock_t *nlmb;
  nlmb = (nlm_lock_t *) Mem_Calloc(1, sizeof(nlm_lock_t));
  if(!nlmb)
    return NULL;
  nlmb->caller_name = strdup(nlm_lock->caller_name);
  if(!copy_netobj(&nlmb->fh, &nlm_lock->fh))
    goto err_out;
  if(!copy_netobj(&nlmb->oh, &nlm_lock->oh))
    goto err_out;
  nlmb->svid = nlm_lock->svid;
  nlmb->offset = nlm_lock->l_offset;
  nlmb->len = nlm_lock->l_len;
  nlmb->exclusive = exclusive;
  return nlmb;
 err_out:
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
  return NULL;
}

nlm_lock_t *nlm_add_to_locklist(struct nlm4_lock * nlm_lock, int exclusive)
{
  nlm_lock_t *nlmb;
  nlmb = nlm4_lock_to_nlm_lock(nlm_lock, exclusive);
  if(!nlmb)
    return NULL;
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_add_tail(&nlm_lock_list, &nlmb->lock_list);
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  return nlmb;
}

void nlm_remove_from_locklist(nlm_lock_t * nlmb)
{
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_del(&nlmb->lock_list);
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
}

void nlm_init_locklist(void)
{
  init_glist(&nlm_lock_list);
  pthread_mutex_init(&nlm_lock_list_mutex, NULL);
}

nlm_lock_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock, int exclusive, int state)
{
  nlm_lock_t *nlmb;
  struct glist_head *glist;
  pthread_mutex_lock(&nlm_lock_list_mutex);
  glist_for_each(glist, &nlm_lock_list)
  {
    nlmb = glist_entry(glist, nlm_lock_t, lock_list);
    if(strcmp(nlmb->caller_name, nlm_lock->caller_name))
      continue;
    if(netobj_compare(nlmb->fh, nlm_lock->fh))
      continue;
    if(netobj_compare(nlmb->oh, nlm_lock->oh))
      continue;
    if(nlmb->svid != nlm_lock->svid)
      continue;
    if(state == NLM4_GRANTED)
      {
        /*
         * We don't check the below flag when looking for
         * lock in the lock list with state granted. Lookup
         * with state granted happens for unlock operation
         * and RFC says it should only match caller_name, fh,oh
         * and svid
         */
        break;
      }
    if(nlmb->offset != nlm_lock->l_offset)
      continue;
    if(nlmb->len != nlm_lock->l_len)
      continue;
    if(nlmb->exclusive != exclusive)
      continue;
    if(nlmb->state != state)
      continue;
    /* We have matched all atribute of the nlm4_lock */
    break;
  }
  pthread_mutex_unlock(&nlm_lock_list_mutex);
  if(glist == &nlm_lock_list)
    return NULL;
  return nlmb;
}

static nlm_lock_t *nlm_lock_t_dup(nlm_lock_t * orig_nlmb)
{
  nlm_lock_t *nlmb;
  nlmb = (nlm_lock_t *) Mem_Calloc(1, sizeof(nlm_lock_t));
  if(!nlmb)
    return NULL;
  nlmb->caller_name = strdup(orig_nlmb->caller_name);
  if(!copy_netobj(&nlmb->fh, &orig_nlmb->fh))
    goto err_out;
  if(!copy_netobj(&nlmb->oh, &orig_nlmb->oh))
    goto err_out;
  nlmb->svid = orig_nlmb->svid;
  nlmb->offset = orig_nlmb->offset;
  nlmb->len = orig_nlmb->len;
  nlmb->state = orig_nlmb->state;
  nlmb->exclusive = orig_nlmb->exclusive;
  return nlmb;
 err_out:
  free(nlmb->caller_name);
  net_obj_free(&nlmb->fh);
  net_obj_free(&nlmb->oh);
  Mem_Free(nlmb);
  return NULL;

}

void nlm_delete_lock_entry(nlm_lock_t * nlmb, struct nlm4_lock *nlm_lock)
{
  nlm_lock_t *nlmb_left = NULL;
  nlm_lock_t *nlmb_right = NULL;
  if(nlm_lock->l_offset > nlmb->offset)
    {
      nlmb_left = nlm_lock_t_dup(nlmb);
      /* FIXME handle error */
      nlmb_left->len = nlmb->offset - nlm_lock->l_offset;
    }
  if((nlm_lock->l_offset + nlm_lock->l_len) < (nlmb->offset + nlmb->len))
    {
      nlmb_right = nlm_lock_t_dup(nlmb);
      /* FIXME handle error */
      nlmb_right->offset = nlm_lock->l_offset + nlm_lock->l_len;
      nlmb_right->len = (nlmb->offset + nlmb->len) -
          (nlm_lock->l_offset + nlm_lock->l_len);
    }
  /* Delete the old entry and add the two new entries */
  nlm_remove_from_locklist(nlmb);
  pthread_mutex_lock(&nlm_lock_list_mutex);
  if(nlmb_left)
    glist_add_tail(&nlm_lock_list, &(nlmb_left->lock_list));
  if(nlmb_right)
    glist_add_tail(&nlm_lock_list, &(nlmb_right->lock_list));
  pthread_mutex_unlock(&nlm_lock_list_mutex);
}
