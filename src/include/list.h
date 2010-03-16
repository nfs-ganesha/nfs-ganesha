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

#include <stddef.h>

struct glist_head {
	struct glist_head *next;
	struct glist_head *prev;
};

/*FIXME!!! grand hack due to mysql conflict name glist*/

static inline void init_glist(struct glist_head *head)
{
	head->next = head;
	head->prev = head;
}

/* Add the new element between left and right */
static inline void __glist_add(struct glist_head *left, struct glist_head *right,
			struct glist_head *new)
{
	new->prev   = left;
	new->next   = right;
	left->next  = new;
	right->prev = new;
}

static inline void glist_add_tail(struct glist_head *head, struct glist_head *new)
{

	__glist_add(head->prev, head, new);
}

/* add after the specified entry*/
static inline void glist_add(struct glist_head *head, struct glist_head *new)
{
	__glist_add(head, head->next, new);
}

static inline void glist_del(struct glist_head *node)
{
	struct glist_head *left = node->prev;
	struct glist_head *right = node->next;
	left->next = right;
	right->prev = left;
	node->next = NULL;
	node->prev = NULL;
}

#define glist_for_each(node, head) \
	for(node = (head)->next; node != head; node = node->next)

#define container_of(addr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (addr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define glist_entry(node, type, member) \
	container_of(node, type, member)
