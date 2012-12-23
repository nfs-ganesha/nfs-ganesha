/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
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
 * ------------- 
 */



#include <pthread.h>


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* #include "compat.h" */

#if 0
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#endif

#include "redblack.h"

#undef _REENTRANT

#ifdef _REENTRANT
#define LOCK(o)                 pthread_mutex_lock(&(o)->mutex)
#define UNLOCK_RETURN(o, ret)   return (pthread_mutex_unlock(&(o)->mutex), (ret))
#else
#define LOCK(o)
#define UNLOCK_RETURN(o, ret)   return ret
#endif


static struct redblack_node *node_lookup (struct redblack_tree * tree, void *record);
static struct redblack_node *node_lookup_op (struct redblack_tree *tree, void *record, enum cmp_op op, enum cmp_lean lean);
static struct redblack_node *list_next (struct redblack_node * node);
static struct redblack_node *list_prev (struct redblack_node * node);
static void insert_color (struct redblack_node * node, struct redblack_tree * root);
static void erase (struct redblack_node * node, struct redblack_tree * root);
static void link_node (struct redblack_node * node, struct redblack_node * parent, struct redblack_node ** link);
static void rotate_left (struct redblack_node * node, struct redblack_tree * root);
static void rotate_right (struct redblack_node * node, struct redblack_tree * root);
static void erase_color (struct redblack_node * node, struct redblack_node * parent, struct redblack_tree * root);

void redblack_tree_new (struct redblack_tree * tree, int ofs, int duplicates,
                        redblack_cmp_cb_t cmp_cb, void *user_data)
{
    memset (tree, '\0', sizeof (*tree));
#ifdef _REENTRANT
    pthread_mutex_init (&tree->mutex, NULL);
#endif
    tree->ofs = ofs;
    tree->cmp_cb = cmp_cb;
    tree->duplicates = duplicates;
    tree->user_data = user_data;
}

long redblack_tree_count (struct redblack_tree * tree)
{
    long ret;

    LOCK (tree);
    {
        ret = tree->count;
    }
    UNLOCK_RETURN (tree, ret);
}

static void redblack_tree_free_ (struct redblack_tree * tree, struct redblack_node *p, void (*free_cb) (void *))
{
    redblack_tree_free_ (tree, p->left, free_cb);
    redblack_tree_free_ (tree, p->right, free_cb);
    (*free_cb) ((void *) (((char *) p) - tree->ofs));
}

int redblack_tree_free (struct redblack_tree * tree, void (*free_cb) (void *))
{
    LOCK (tree);
    {
        redblack_tree_free_ (tree, tree->node, free_cb);
    }
    UNLOCK_RETURN (tree, 0);
}

int redblack_tree_add (struct redblack_tree * tree, void *record)
{
    int ret = 1;

    LOCK (tree);
    {
        struct redblack_node **p = &tree->node;
        struct redblack_node *parent = NULL, *node;
        void *i;
        void *user_data = tree->user_data;

        if (tree->duplicates) { /* for performance, take this 'if' out of the while loop */
            while (*p) {
                int cmp;

                parent = *p;
                assert (parent->color == redblack_TREE_RED || parent->color == redblack_TREE_BLACK);
                i = (void *) (((char *) parent) - tree->ofs);

                cmp = (*tree->cmp_cb) (i, record, user_data);
                if (cmp > 0) {
                    p = &(*p)->left;
                } else {
                    p = &(*p)->right;
                }
            }
        } else {
            while (*p) {
                int cmp;

                parent = *p;
                assert (parent->color == redblack_TREE_RED || parent->color == redblack_TREE_BLACK);
                i = (void *) (((char *) parent) - tree->ofs);

                cmp = (*tree->cmp_cb) (i, record, user_data);
                if (cmp > 0) {
                    p = &(*p)->left;
                } else if (cmp < 0) {
                    p = &(*p)->right;
                } else {
                    ret = 0;
                    goto done;
                }
            }
        }
        tree->count++;

        node = (struct redblack_node *) (((char *) record) + tree->ofs);
        link_node (node, parent, p);
        insert_color (node, tree);

        if (tree->next_access && tree->next_access == list_next (node))
            tree->next_access = node;
        if (tree->prev_access && tree->prev_access == list_prev (node))
            tree->prev_access = node;
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_find (struct redblack_tree * tree, void *record)
{
    void *ret = NULL;

    LOCK (tree);
    {
        struct redblack_node *node;

        node = node_lookup (tree, record);
        if (!node)
            goto done;

        tree->next_access = list_next (node);
        tree->prev_access = list_prev (node);

        ret = ((void *) (((char *) node) - tree->ofs));
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_find_op (struct redblack_tree *tree, void *record, enum cmp_op op, enum cmp_lean lean)
{
    void *ret = NULL;

    LOCK (tree);
    {
	struct redblack_node *node;

	node = node_lookup_op (tree, record, op, lean);
	if (!node)
	    goto done;

	tree->next_access = list_next (node);
	tree->prev_access = list_prev (node);

	ret = ((void *) (((char *) node) - tree->ofs));
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_delete (struct redblack_tree * tree, void *record)
{
    void *ret = NULL;

    assert (record);

    LOCK (tree);
    {
        struct redblack_node *node;

        node = (struct redblack_node *) (((char *) record) + tree->ofs);
        assert (node->color == redblack_TREE_RED || node->color == redblack_TREE_BLACK);

        if (tree->next_access == node)
            tree->next_access = list_next (node);
        if (tree->prev_access == node)
            tree->prev_access = list_prev (node);

        tree->count--;

        erase (node, tree);
        ret = ((void *) (((char *) node) - tree->ofs));
    }

    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_first (struct redblack_tree * tree)
{
    void *ret = NULL;

    LOCK (tree);
    {
        struct redblack_node *n = tree->node;

        tree->next_access = tree->prev_access = NULL;

        if (!n)
            goto done;

        while (n->left)
            n = n->left;

        tree->next_access = list_next (n);

        ret = ((void *) (((char *) n) - tree->ofs));
    }

  done:
    UNLOCK_RETURN (tree, ret);
}


void *redblack_to_array (struct redblack_tree * tree, int member_size, int *n_members, void (*member_copy) (void *, void *))
{
    int i;
    void *ret;
    *n_members = tree->count;
    if (!tree->count)
        return NULL;
    ret = malloc (member_size * tree->count);
    LOCK (tree);
    {
        struct redblack_node *n = tree->node;
        void *r, *t;

        assert (n);

        while (n->left)
            n = n->left;

        for (i = 0; i < tree->count; i++) {
            assert (n);
            r = ((void *) (((char *) n) - tree->ofs));
            t = (char *) ret + member_size * i;
            (*member_copy) (t, r);
            n = list_next (n);
        }
        assert (!n);
    }
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_last (struct redblack_tree * tree)
{
    void *ret = NULL;

    LOCK (tree);
    {
        struct redblack_node *n = tree->node;

        tree->next_access = tree->prev_access = NULL;

        if (!n)
            goto done;

        while (n->right)
            n = n->right;

        tree->prev_access = list_prev (n);

        ret = ((void *) (((char *) n) - tree->ofs));
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_next (struct redblack_tree * tree)
{
    void *ret = NULL;

    LOCK (tree);
    {
        if (!tree->next_access)
            goto done;

        ret = ((void *) (((char *) tree->next_access) - tree->ofs));
        tree->prev_access = tree->next_access;
        tree->next_access = list_next (tree->next_access);
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

void *redblack_tree_prev (struct redblack_tree * tree)
{
    void *ret = NULL;

    LOCK (tree);
    {
        if (!tree->prev_access)
            goto done;

        ret = ((void *) (((char *) tree->prev_access) - tree->ofs));
        tree->next_access = tree->prev_access;
        tree->prev_access = list_prev (tree->prev_access);
    }

  done:
    UNLOCK_RETURN (tree, ret);
}

static struct redblack_node *node_lookup (struct redblack_tree * tree, void *record)
{
    struct redblack_node *n = tree->node;
    void *user_data = tree->user_data;
    void *i;

    while (n) {
        int cmp;

        assert (n->color == redblack_TREE_RED || n->color == redblack_TREE_BLACK);

        i = (void *) (((char *) n) - tree->ofs);

        cmp = (*tree->cmp_cb) (i, record, user_data);

        if (cmp > 0)
            n = n->left;
        else if (cmp < 0)
            n = n->right;
        else
            return n;
    }
    return NULL;
}

static struct redblack_node *node_lookup_op (struct redblack_tree *tree, void *record, enum cmp_op op, enum cmp_lean lean)
{
    int cmp = 0;
    struct redblack_node *n = tree->node, *r = NULL;
    void *user_data = tree->user_data;
    void *i;

    while (n) {
        assert (n->color == redblack_TREE_RED || n->color == redblack_TREE_BLACK);

        i = (void *) (((char *) n) - tree->ofs);

        cmp = (*tree->cmp_cb) (i, record, user_data);

        r = n;

        if (cmp > 0 || ((cmp == 0 && lean == CMP_LEAN_LEFT)))
            n = n->left;
        else if (cmp < 0 || (cmp == 0 && lean >= CMP_LEAN_RIGHT))
            n = n->right;
    }

    if (!r)
        return NULL;

    while (cmp == 0 && op == CMP_OP_LT && (n = list_prev (r))) {
        r = n;
        i = (void *) (((char *) r) - tree->ofs);
        cmp = (*tree->cmp_cb) (i, record, user_data);
    }
    while (cmp > 0 && op == CMP_OP_LE && (n = list_prev (r))) {
        r = n;
        i = (void *) (((char *) r) - tree->ofs);
        cmp = (*tree->cmp_cb) (i, record, user_data);
    }
    while (cmp < 0 && op == CMP_OP_GE && (n = list_next (r))) {
        r = n;
        i = (void *) (((char *) r) - tree->ofs);
        cmp = (*tree->cmp_cb) (i, record, user_data);
    }
    while (cmp == 0 && op == CMP_OP_GT && (n = list_next (r))) {
        r = n;
        i = (void *) (((char *) r) - tree->ofs);
        cmp = (*tree->cmp_cb) (i, record, user_data);
    }
    return r;
}

static struct redblack_node *list_next (struct redblack_node * node)
{
    if (!node)
        return NULL;
    assert (node->color == redblack_TREE_RED || node->color == redblack_TREE_BLACK);
    if (node->right) {
        node = node->right;
        while (node->left)
            node = node->left;
    } else {
        while (node->parent && node == node->parent->right)
            node = node->parent;
        node = node->parent;
    }
    return node;
}

static struct redblack_node *list_prev (struct redblack_node * node)
{
    if (!node)
        return NULL;
    assert (node->color == redblack_TREE_RED || node->color == redblack_TREE_BLACK);
    if (node->left) {
        node = node->left;
        while (node->right)
            node = node->right;
    } else {
        while (node->parent && node == node->parent->left)
            node = node->parent;
        node = node->parent;
    }
    return node;
}

static void insert_color (struct redblack_node * node, struct redblack_tree * root)
{
    struct redblack_node *parent, *gparent;

    while ((parent = node->parent) && parent->color == redblack_TREE_RED) {
        gparent = parent->parent;

        if (parent == gparent->left) {
            {
                register struct redblack_node *uncle = gparent->right;

                if (uncle && uncle->color == redblack_TREE_RED) {
                    uncle->color = redblack_TREE_BLACK;
                    parent->color = redblack_TREE_BLACK;
                    gparent->color = redblack_TREE_RED;
                    node = gparent;
                    continue;
                }
            }

            if (parent->right == node) {
                register struct redblack_node *tmp;

                rotate_left (parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->color = redblack_TREE_BLACK;
            gparent->color = redblack_TREE_RED;
            rotate_right (gparent, root);
        } else {
            {
                register struct redblack_node *uncle = gparent->left;

                if (uncle && uncle->color == redblack_TREE_RED) {
                    uncle->color = redblack_TREE_BLACK;
                    parent->color = redblack_TREE_BLACK;
                    gparent->color = redblack_TREE_RED;
                    node = gparent;
                    continue;
                }
            }

            if (parent->left == node) {
                register struct redblack_node *tmp;

                rotate_right (parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->color = redblack_TREE_BLACK;
            gparent->color = redblack_TREE_RED;
            rotate_left (gparent, root);
        }
    }

    root->node->color = redblack_TREE_BLACK;
}

static void erase (struct redblack_node * node, struct redblack_tree * root)
{
    struct redblack_node *child, *parent;
    int color;

    assert (node->color == redblack_TREE_RED || node->color == redblack_TREE_BLACK);
    if (!node->left)
        child = node->right;
    else if (!node->right)
        child = node->left;
    else {
        struct redblack_node *old = node, *left;

        node = node->right;
        while ((left = node->left))
            node = left;
        child = node->right;
        parent = node->parent;
        color = node->color;

        if (child)
            child->parent = parent;
        if (parent) {
            if (parent->left == node)
                parent->left = child;
            else
                parent->right = child;
        } else
            root->node = child;

        if (node->parent == old)
            parent = node;
        node->parent = old->parent;
        node->color = old->color;
        node->right = old->right;
        node->left = old->left;

        if (old->parent) {
            if (old->parent->left == old)
                old->parent->left = node;
            else
                old->parent->right = node;
        } else
            root->node = node;

        old->left->parent = node;
        if (old->right)
            old->right->parent = node;
        goto color;
    }

    parent = node->parent;
    color = node->color;

    if (child)
        child->parent = parent;
    if (parent) {
        if (parent->left == node)
            parent->left = child;
        else
            parent->right = child;
    } else
        root->node = child;

  color:
    if (color == redblack_TREE_BLACK)
        erase_color (child, parent, root);
}

static void link_node (struct redblack_node * node, struct redblack_node * parent, struct redblack_node ** link)
{
    node->parent = parent;
    node->color = redblack_TREE_RED;
    node->left = node->right = NULL;

    *link = node;
}

static void rotate_left (struct redblack_node * node, struct redblack_tree * root)
{
    struct redblack_node *right = node->right;

    if ((node->right = right->left))
        right->left->parent = node;
    right->left = node;

    if ((right->parent = node->parent)) {
        if (node == node->parent->left)
            node->parent->left = right;
        else
            node->parent->right = right;
    } else
        root->node = right;
    node->parent = right;
}

static void rotate_right (struct redblack_node * node, struct redblack_tree * root)
{
    struct redblack_node *left = node->left;

    if ((node->left = left->right))
        left->right->parent = node;
    left->right = node;

    if ((left->parent = node->parent)) {
        if (node == node->parent->right)
            node->parent->right = left;
        else
            node->parent->left = left;
    } else
        root->node = left;
    node->parent = left;
}

static void erase_color (struct redblack_node * node, struct redblack_node * parent, struct redblack_tree * root)
{
    struct redblack_node *other;

    while ((!node || node->color == redblack_TREE_BLACK) && node != root->node) {
        if (parent->left == node) {
            other = parent->right;
            if (other->color == redblack_TREE_RED) {
                other->color = redblack_TREE_BLACK;
                parent->color = redblack_TREE_RED;
                rotate_left (parent, root);
                other = parent->right;
            }
            if ((!other->left || other->left->color == redblack_TREE_BLACK)
                && (!other->right || other->right->color == redblack_TREE_BLACK)) {
                other->color = redblack_TREE_RED;
                node = parent;
                parent = node->parent;
            } else {
                if (!other->right || other->right->color == redblack_TREE_BLACK) {
                    register struct redblack_node *o_left;

                    if ((o_left = other->left))
                        o_left->color = redblack_TREE_BLACK;
                    other->color = redblack_TREE_RED;
                    rotate_right (other, root);
                    other = parent->right;
                }
                other->color = parent->color;
                parent->color = redblack_TREE_BLACK;
                if (other->right)
                    other->right->color = redblack_TREE_BLACK;
                rotate_left (parent, root);
                node = root->node;
                break;
            }
        } else {
            other = parent->left;
            if (other->color == redblack_TREE_RED) {
                other->color = redblack_TREE_BLACK;
                parent->color = redblack_TREE_RED;
                rotate_right (parent, root);
                other = parent->left;
            }
            if ((!other->left || other->left->color == redblack_TREE_BLACK)
                && (!other->right || other->right->color == redblack_TREE_BLACK)) {
                other->color = redblack_TREE_RED;
                node = parent;
                parent = node->parent;
            } else {
                if (!other->left || other->left->color == redblack_TREE_BLACK) {
                    register struct redblack_node *o_right;

                    if ((o_right = other->right))
                        o_right->color = redblack_TREE_BLACK;
                    other->color = redblack_TREE_RED;
                    rotate_left (other, root);
                    other = parent->left;
                }
                other->color = parent->color;
                parent->color = redblack_TREE_BLACK;
                if (other->left)
                    other->left->color = redblack_TREE_BLACK;
                rotate_right (parent, root);
                node = root->node;
                break;
            }
        }
    }
    if (node)
        node->color = redblack_TREE_BLACK;
}

/*
 *  U N I T   T E S T S
 */

#ifdef STANDALONE

#define KV_MAGIC   0x44546856

struct key_value {
    unsigned int magic;
    char key[32];
    char value[32];
    struct redblack_node rbt;
};

static int my_cmp (void *_a, void *_b, void *_xx)
{
    struct key_value *a, *b;
    int *xx = (int *) _xx;

    a = (struct key_value *) _a;
    b = (struct key_value *) _b;

    assert (*xx == 12345);
    assert (a->magic == KV_MAGIC);
    assert (b->magic == KV_MAGIC);

    return strcmp (a->key, b->key);
}

int main (int argc, char **argv)
{
    int i, tot = 0;
    int xx = 12345;
    struct key_value *kv;
    struct redblack_tree rbt;

    printf ("\n");

    redblack_tree_new (&rbt, offsetof (struct key_value, rbt), 1, my_cmp, &xx);

#define XXXX            (1 * 1024 * 1024)

    for (i = 0; i < XXXX; i++) {
        kv = (struct key_value *) malloc (sizeof (*kv));
        memset (kv, 0, sizeof (*kv));
        kv->magic = KV_MAGIC;
        snprintf (kv->key, sizeof (kv->key), "%15d", i);
        snprintf (kv->value, sizeof (kv->value), "VALUE%15d", i);

        redblack_tree_add (&rbt, kv);
    }

    assert (redblack_tree_count (&rbt) == XXXX);

    for (i = 0; i < XXXX; i++) {
        struct key_value a, *p;

        a.magic = KV_MAGIC;
        snprintf (a.key, sizeof (a.key), "%15d", i);
        snprintf (a.value, sizeof (a.value), "VALUE%15d", i);

        p = redblack_tree_find (&rbt, &a);
        assert (!strcmp (p->value, a.value));
    }

    for (kv = redblack_tree_first (&rbt); kv; kv = redblack_tree_next (&rbt)) {
        assert (kv->magic == KV_MAGIC);
        redblack_tree_delete (&rbt, kv);
        memset (kv, -1, sizeof (*kv));
        free (kv);
        tot++;
    }

    assert (tot == XXXX);
    assert (redblack_tree_count (&rbt) == 0);

    printf ("\n");

    for (i = 0; i < 65536; i++) {
        unsigned int n;

        n = (i ^ (i >> 2) ^ (i << 5) ^ 0x3485);
        n = (n * n) % 65537;

        kv = (struct key_value *) malloc (sizeof (*kv));
        memset (kv, 0, sizeof (*kv));
        kv->magic = KV_MAGIC;
        snprintf (kv->key, sizeof (kv->key), "%05d", n);
        snprintf (kv->value, sizeof (kv->value), "VALUE%05d", n);

        redblack_tree_add (&rbt, kv);
    }

    for (kv = redblack_tree_first (&rbt); kv; kv = redblack_tree_next (&rbt)) {
        printf ("%s: %s\n", kv->key, kv->value);
        assert (kv->magic == KV_MAGIC);
        redblack_tree_delete (&rbt, kv);
        memset (kv, -1, sizeof (*kv));
        free (kv);
    }

    printf ("\n");

    for (i = 0; i < 10; i++) {
        kv = (struct key_value *) malloc (sizeof (*kv));
        memset (kv, 0, sizeof (*kv));
        kv->magic = KV_MAGIC;
        snprintf (kv->key, sizeof (kv->key), "%05d", i);
        snprintf (kv->value, sizeof (kv->value), "VALUE%05d", i);

        redblack_tree_add (&rbt, kv);
    }

    rbt.duplicates = 0;

    for (kv = redblack_tree_first (&rbt); kv; kv = redblack_tree_next (&rbt)) {
        printf ("%s: %s\n", kv->key, kv->value);
        if (!strcmp (kv->key, "00005")) {
            struct key_value *n;

            n = (struct key_value *) malloc (sizeof (*n));
            memset (n, 0, sizeof (*n));
            n->magic = KV_MAGIC;
            snprintf (n->key, sizeof (n->key), "%05d.", 5);
            snprintf (n->value, sizeof (n->value), "VALUE%05d.", 5);
            redblack_tree_add (&rbt, n);
        }
        assert (kv->magic == KV_MAGIC);
        redblack_tree_delete (&rbt, kv);
        memset (kv, -1, sizeof (*kv));
        free (kv);
    }

    printf ("\n");

    for (i = 0; i < 10; i++) {
        kv = (struct key_value *) malloc (sizeof (*kv));
        memset (kv, 0, sizeof (*kv));
        kv->magic = KV_MAGIC;
        snprintf (kv->key, sizeof (kv->key), "%05d", i);
        snprintf (kv->value, sizeof (kv->value), "VALUE%05d", i);

        redblack_tree_add (&rbt, kv);
    }

    for (kv = redblack_tree_first (&rbt); kv; kv = redblack_tree_next (&rbt)) {
        struct key_value n, *p;

        printf ("%s: %s\n", kv->key, kv->value);

        n.magic = KV_MAGIC;
        snprintf (n.key, sizeof (n.key), "%05d", atoi (kv->key) + 1);
        p = redblack_tree_find (&rbt, &n);
        assert (kv->magic == KV_MAGIC);
        redblack_tree_delete (&rbt, kv);
        memset (kv, -1, sizeof (*kv));
        free (kv);

    }

    printf ("\n");

    for (i = 0; i < 10; i++) {
        kv = (struct key_value *) malloc (sizeof (*kv));
        memset (kv, 0, sizeof (*kv));
        kv->magic = KV_MAGIC;
        snprintf (kv->key, sizeof (kv->key), "%05d", i);
        snprintf (kv->value, sizeof (kv->value), "VALUE%05d", i);

        redblack_tree_add (&rbt, kv);
    }

    for (kv = redblack_tree_last (&rbt); kv; kv = redblack_tree_prev (&rbt)) {
        struct key_value n, *p;

        printf ("%s: %s\n", kv->key, kv->value);

        n.magic = KV_MAGIC;
        snprintf (n.key, sizeof (n.key), "%05d", atoi (kv->key) - 1);
        p = redblack_tree_find (&rbt, &n);
        assert (kv->magic == KV_MAGIC);
        redblack_tree_delete (&rbt, kv);
        memset (kv, -1, sizeof (*kv));
        free (kv);

    }

    return 0;
}

#endif
