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



struct redblack_node;

enum cmp_op {
    CMP_OP_LT,
    CMP_OP_LE,
    CMP_OP_GE,
    CMP_OP_GT
};

enum cmp_lean {
    CMP_LEAN_LEFT,
    CMP_LEAN_RIGHT
};

enum rbt_color_t {
    redblack_TREE_RED = 0x562EAB4C,
    redblack_TREE_BLACK = 0x0B5EEEBF
};

typedef int (*redblack_cmp_cb_t) (const void *, const void *, void *);

struct redblack_tree {
#if 0
    pthread_mutex_t mutex;
#endif
    struct redblack_node *node;
    struct redblack_node *next_access;
    struct redblack_node *prev_access;
    int ofs;
    long count;
    redblack_cmp_cb_t cmp_cb;
    int duplicates;
    void *user_data;
};

struct redblack_node {
    struct redblack_node *parent;
    enum rbt_color_t color;
    struct redblack_node *right;
    struct redblack_node *left;
};

void redblack_tree_new (struct redblack_tree * tree, int ofs, int duplicates, redblack_cmp_cb_t cmp_cb, void *user_data);
int redblack_tree_free (struct redblack_tree * tree, void (*free_cb) (void *));
int redblack_tree_add (struct redblack_tree * tree, void *record);
void *redblack_tree_find (struct redblack_tree * tree, void *record);
void *redblack_tree_find_op (struct redblack_tree *tree, void *record, enum cmp_op op, enum cmp_lean lean);
void *redblack_tree_delete (struct redblack_tree * tree, void *record);
void *redblack_tree_first (struct redblack_tree * tree);
void *redblack_tree_last (struct redblack_tree * tree);
void *redblack_tree_next (struct redblack_tree * tree);
void *redblack_tree_prev (struct redblack_tree * tree);
long redblack_tree_count (struct redblack_tree * tree);
void *redblack_to_array (struct redblack_tree * tree, int member_size, int *n_members, void (*member_copy) (void *, void *));

