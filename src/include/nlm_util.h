/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
#include "list.h"

struct nlm_lock {
  char *caller_name;
  netobj fh;
  netobj oh;
  int32_t svid;
  uint64_t offset;
  uint64_t len;
  int state;
  int exclusive;
  struct glist_head lock_list;
};

typedef struct nlm_lock nlm_lock_t;

extern fsal_lockdesc_t *nlm_lock_to_fsal_lockdesc(struct nlm4_lock *nlm_lock,
						  bool_t exclusive);
extern nlm_lock_t *nlm_add_to_locklist(struct nlm4_lock *nlm_lock, int exclusive);
extern void nlm_remove_from_locklist(nlm_lock_t * nlmb);
extern void nlm_init_locklist(void);
extern nlm_lock_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock,
				       int exclusive, int state);
