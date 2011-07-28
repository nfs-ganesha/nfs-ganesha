/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
 */

#include <stdio.h>
#include "nlm_list.h"

struct myteststruct
{
  int value;
  struct glist_head glist;
};

struct glist_head mytestglist;
struct glist_head mytestglist_new;

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
  struct myteststruct node1_new;
  struct myteststruct node2_new;
  init_glist(&mytestglist);
  init_glist(&mytestglist_new);
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
  node1_new.value = 15;
  node2_new.value = 16;
  glist_add(&mytestglist_new, &node1_new.glist);
  glist_add(&mytestglist_new, &node2_new.glist);
  printf("Add the below two list\n");
  printf("list1\n");
  print_glist(&mytestglist);
  printf("list2\n");
  print_glist(&mytestglist_new);
  glist_add_list_tail(&mytestglist, &mytestglist_new);
  printf("combined list\n");
  print_glist(&mytestglist);
  return 0;
}
