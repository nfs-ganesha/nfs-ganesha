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

#include <stdio.h>
#include "list.h"

struct myteststruct {
  int value;
  struct glist_head glist;
};

struct glist_head mytestglist;

static void print_glist(struct glist_head *head)
{
  struct myteststruct *entry;
  struct glist_head *glist;

  glist_for_each(glist, head)
  {
    entry = glist_entry(glist, struct myteststruct, glist);
    printf("The value is %d\n", entry->value);
  }
}

int main(int argc, char *argv[])
{
  struct myteststruct node1;
  struct myteststruct node2;
  struct myteststruct node3;
  struct myteststruct node4;
  init_glist(&mytestglist);
  node1.value = 10;
  node2.value = 11;
  node3.value = 12;
  glist_add(&mytestglist, &node1.glist);
  glist_add(&mytestglist, &node2.glist);
  glist_add(&mytestglist, &node3.glist);

  print_glist(&mytestglist);
  printf("Now test tail add\n");
  node4.value = 13;
  glist_add_tail(&mytestglist, &node4.glist);
  print_glist(&mytestglist);
  printf("Delete test\n");
  glist_del(&node2.glist);
  print_glist(&mytestglist);
  return 0;
}
