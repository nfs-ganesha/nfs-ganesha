#include <limits.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "CUnit/Basic.h"

#include <afs/afsint.h>
#include "avltree.h"


#define DEBUG 1

/* STATICS  we use across multiple tests */
struct avltree avl_tree_1;
struct avltree avl_tree_2;
struct avltree avl_tree_100;
struct avltree avl_tree_10000;


typedef struct avl_unit_val
{
    int refs;
    struct avltree_node node_k;
    unsigned long key;
    unsigned long val;
} avl_unit_val_t;

int
avl_unit_cmpf(const struct avltree_node *lhs,
              const struct avltree_node *rhs)
{
    avl_unit_val_t *lk, *rk;

    lk = avltree_container_of(lhs, avl_unit_val_t, node_k);
    rk = avltree_container_of(rhs, avl_unit_val_t, node_k);

    if (lk->key < rk->key)
	return (-1);

    if (lk->key == rk->key)
	return (0);

    return (1);
}

avl_unit_val_t*
avl_unit_new_val(unsigned long intval)
{
    avl_unit_val_t *v = malloc(sizeof(avl_unit_val_t));
    memset(v, 0, sizeof(avl_unit_val_t));
    v->val = (intval + 1);

    return v;
}

void
avl_unit_free_val(avl_unit_val_t *v)
{
    free(v);
}

void
avl_unit_clear_tree(struct avltree *t)
{
    avl_unit_val_t *v;
    struct avltree_node *node, *next_node;


    if (avltree_size(t) < 1)
	return;

    node = avltree_first(t);
    while (node) {
        next_node = avltree_next(node);
        v = avltree_container_of(node, avl_unit_val_t, node_k);
        avltree_remove(&v->node_k, &avl_tree_1);
        avl_unit_free_val(v);
        node = next_node;
    }
}

/* dne */
void avltree_destroy(struct avltree *t)
{
    return;
}

void
avl_unit_clear_and_destroy_tree(struct avltree *t)
{
    avl_unit_clear_tree(t);
    avltree_destroy(t);
}

/*
 *  BEGIN SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

void avl_unit_PkgInit(void)
{
    /* nothing */
}

/* 
 * The suite initialization function.
 * Initializes resources to be shared across tests.
 * Returns zero on success, non-zero otherwise.
 *
 */
int init_suite1(void)
{

    avltree_init(&avl_tree_1, avl_unit_cmpf, 0 /* flags */);

    return 0;
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite1(void)
{
    if (avltree_size(&avl_tree_1) > 0)
	avl_unit_clear_tree(&avl_tree_1);

    avltree_destroy(&avl_tree_1);

    return 0;
}

/* 
 * The suite initialization function.
 * Initializes resources to be shared across tests.
 * Returns zero on success, non-zero otherwise.
 *
 */
int init_suite2(void)
{
    avltree_init(&avl_tree_2, avl_unit_cmpf, 0 /* flags */);

    return 0;
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite2(void)
{

    avltree_destroy(&avl_tree_2);

    return 0;
}

/* 
 * The suite initialization function.
 * Initializes resources to be shared across tests.
 * Returns zero on success, non-zero otherwise.
 *
 */
int init_suite100(void)
{
    avltree_init(&avl_tree_100, avl_unit_cmpf, 0 /* flags */);

    return 0;
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite100(void)
{

    avltree_destroy(&avl_tree_100);

    return 0;
}

/* 
 * The suite initialization function.
 * Initializes resources to be shared across tests.
 * Returns zero on success, non-zero otherwise.
 *
 */
int init_suite10000(void)
{
    avltree_init(&avl_tree_10000, avl_unit_cmpf, 0 /* flags */);

    return 0;
}

/* The suite cleanup function.
 * Closes the temporary resources used by the tests.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite10000(void)
{
    avltree_destroy(&avl_tree_10000);

    return 0;
}


/*
 *  END SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */


/* 
 *  BEGIN BASIC TESTS
 */

void inserts_tree_1(void)
{
    avl_unit_val_t *v;
    int ix;

    for (ix = 1; ix < 2; ++ix) {

	/* new k, v */
	v = avl_unit_new_val(ix);

	/* if actual key cannot be marshalled as a pointer */
	v->key = ix;

	/* insert mapping */
	avltree_insert(&v->node_k, &avl_tree_1);
    }
}

void check_tree_1(void)
{
    int code = 0;
    CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_1(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 2; ++ix) {

	/* reuse v */
	v->key = ix;

	/* lookup mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_1);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT((unsigned long) v2->val == (ix+1));
    }

    /* free v */
    avl_unit_free_val(v);
}

void deletes_tree_1(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 2; ++ix) {

	/* reuse key */
	v->key = ix;

	/* find mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_1);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT(v2->val == (ix+1));

	/* and remove it */
	avltree_remove(&v2->node_k, &avl_tree_1);
	avl_unit_free_val(v2);
    }

    /* free search k */
    avl_unit_free_val(v);
}

//xx
void inserts_tree_2(void)
{
    avl_unit_val_t *v;
    int ix;

    for (ix = 1; ix < 4; ++ix) {

	/* new k, v */
	v = avl_unit_new_val(ix);

	/* if actual key cannot be marshalled as a pointer */
	v->key = ix;

	/* insert mapping */
        avltree_insert(&v->node_k, &avl_tree_2);
    }
}

void inserts_tree_2r(void)
{
    avl_unit_val_t *v;
    int ix;

    for (ix = 3; ix > 0; --ix) {

	/* new k, v */
	v = avl_unit_new_val(ix);

	/* if actual key cannot be marshalled as a pointer */
	v->key = ix;

	/* insert mapping */
        avltree_insert(&v->node_k, &avl_tree_2);
    }
}


void check_tree_2(void)
{
    int code = 0;
    CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_2(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 4; ++ix) {

	/* reuse v */
	v->key = ix;

	/* lookup mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_2);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT((unsigned long) v2->val == (ix+1));
    }

    /* free v */
    avl_unit_free_val(v);
}

void deletes_tree_2(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 4; ++ix) {

	/* reuse key */
	v->key = ix;

	/* find mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_2);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT(v2->val == (ix+1));

	/* and remove it */
        avltree_remove(&v2->node_k, &avl_tree_2);
	avl_unit_free_val(v2);
    }

    /* free search k */
    avl_unit_free_val(v);
}

void inserts_tree_100(void)
{
    avl_unit_val_t *v;
    int ix;

    for (ix = 1; ix < 101; ++ix) {

	/* new k, v */
	v = avl_unit_new_val(ix);

	/* if actual key cannot be marshalled as a pointer */
	v->key = ix;

	/* insert mapping */
        avltree_insert(&v->node_k, &avl_tree_100);
    }
}

void check_tree_100(void)
{
    int code = 0;
    CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_100(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 2; ++ix) {

	/* reuse v */
	v->key = ix;

	/* lookup mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_100);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT((unsigned long) v2->val == (ix+1));
    }

    /* free v */
    avl_unit_free_val(v);
}

void trav_tree_100(void)
{
    int ntrav = 0;
    struct avltree_node *node;
    avl_unit_val_t *v;

    node = avltree_first(&avl_tree_100);
    while (node) {
	ntrav++;
        v = avltree_container_of(node, avl_unit_val_t, node_k);
	if ((ntrav % 10) == 0)
	    printf("Node at %p key: %lu val: %lu (%d)\n",
		   v, v->key, v->val, ntrav);
        node = avltree_next(node);
    }
    CU_ASSERT_EQUAL(ntrav, 100);
}

void deletes_tree_100(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 101; ++ix) {

	/* reuse key */
	v->key = ix;

	/* find mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_100);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT(v2->val == (ix+1));

	/* and remove it */
        avltree_remove(&v2->node_k, &avl_tree_100);
	avl_unit_free_val(v2);
    }

    /* free search k */
    avl_unit_free_val(v);
}

void inserts_tree_10000(void)
{
    avl_unit_val_t *v;
    int ix;

    for (ix = 1; ix < 10001; ++ix) {

	/* new k, v */
	v = avl_unit_new_val(ix);

	/* if actual key cannot be marshalled as a pointer */
	v->key = ix;

	/* insert mapping */
        avltree_insert(&v->node_k, &avl_tree_10000);
    }
}

void check_tree_10000(void)
{
    int code = 0;
    CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_10000(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 2; ++ix) {

	/* reuse v */
	v->key = ix;

	/* lookup mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_10000);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT((unsigned long) v2->val == (ix+1));
    }

    /* free v */
    avl_unit_free_val(v);
}

void trav_tree_10000(void)
{
    int ntrav = 0;
    struct avltree_node *node;
    avl_unit_val_t *v;

    node = avltree_first(&avl_tree_10000);
    while (node) {
	ntrav++;
        v = avltree_container_of(node, avl_unit_val_t, node_k);
	if ((ntrav % 1000) == 0)
	    printf("Node at %p key: %lu val: %lu (%d)\n",
		   v, v->key, v->val, ntrav);
        node = avltree_next(node);
    }
    CU_ASSERT_EQUAL(ntrav, 10000);
}

void deletes_tree_10000(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v2, *v = avl_unit_new_val(0);
    int ix;

    for (ix = 1; ix < 10001; ++ix) {

	/* reuse key */
	v->key = ix;

	/* find mapping */
	node = avltree_lookup(&v->node_k, &avl_tree_10000);
        v2 = avltree_container_of(node, avl_unit_val_t, node_k);
	CU_ASSERT(v2->val == (ix+1));

	/* and remove it */
        avltree_remove(&v2->node_k, &avl_tree_10000);
	avl_unit_free_val(v2);
    }

    /* free search k */
    avl_unit_free_val(v);
}

void insert_long_val(struct avltree *t, unsigned long l)
{
    avl_unit_val_t *v;

    /* new k, v */
    v = avl_unit_new_val(l);

    /* if actual key cannot be marshalled as a pointer */
    v->key = l;

    /* insert mapping */
    avltree_insert(&v->node_k, t);
}

void insert_long_val_safe(struct avltree *t, unsigned long l)
{
    struct avltree_node *node;
    avl_unit_val_t *v;

    /* new k, v */
    v = avl_unit_new_val(l);
    v->key = l;

    node = avltree_lookup(&v->node_k, t);
    if (node == NULL)
        avltree_insert(&v->node_k, t);
    else
	avl_unit_free_val(v);
}


void delete_long_val(struct avltree *t, unsigned long l)
{
    struct avltree_node *node;
    avl_unit_val_t *v, *v2;

    /* new key, v */
    v = avl_unit_new_val(l);
    v->key = l;

    /* find mapping */
    node = avltree_lookup(&v->node_k, t);
    v2 = avltree_container_of(node, avl_unit_val_t, node_k);
    CU_ASSERT(v2->key == l);

    /* delete mapping */
    avltree_remove(&v2->node_k, t);

    /* free original v */
    avl_unit_free_val(v2);

    /* free search k, v */
    avl_unit_free_val(v);
}

void check_delete_1(void)
{
    struct avltree_node *node;
    avl_unit_val_t *v, *v2;

    avl_unit_clear_and_destroy_tree(&avl_tree_1);

    avltree_init(&avl_tree_1, avl_unit_cmpf, 0 /* flags */);

    insert_long_val(&avl_tree_1, 4);
    insert_long_val(&avl_tree_1, 1);
    insert_long_val(&avl_tree_1, 10010);
    insert_long_val(&avl_tree_1, 267);
    insert_long_val(&avl_tree_1, 3382);
    insert_long_val(&avl_tree_1, 22);
    insert_long_val(&avl_tree_1, 82);
    insert_long_val(&avl_tree_1, 3);

    node = avltree_first(&avl_tree_1);
    v2 = avltree_container_of(node, avl_unit_val_t, node_k);
    CU_ASSERT(v2->val == (1+1));

    delete_long_val(&avl_tree_1, 1);

    /* new key */
    v = avl_unit_new_val(4);
    v->key = 4;
    node = avltree_lookup(&v->node_k, &avl_tree_1);
    v2 = avltree_container_of(node, avl_unit_val_t, node_k);

    CU_ASSERT(v2->val == (4+1));

    delete_long_val(&avl_tree_1, 267);

    v->key = 3382;
    node = avltree_lookup(&v->node_k, &avl_tree_1);
    v2 = avltree_container_of(node, avl_unit_val_t, node_k);

    CU_ASSERT(v2->val == (3382+1));
    
    avl_unit_free_val(v);

}

void check_min_1(void)
{

    avl_unit_val_t *v;
    struct avltree_node *node;

    avl_unit_clear_and_destroy_tree(&avl_tree_1);

    avltree_init(&avl_tree_1, avl_unit_cmpf, 0 /* flags */);

    insert_long_val(&avl_tree_1, 4);
    insert_long_val(&avl_tree_1, 10);
    insert_long_val(&avl_tree_1, 10010);
    insert_long_val(&avl_tree_1, 267);
    insert_long_val(&avl_tree_1, 3382);
    insert_long_val(&avl_tree_1, 22);
    insert_long_val(&avl_tree_1, 82);

    node = avltree_first(&avl_tree_1);
    v = avltree_container_of(node, avl_unit_val_t, node_k);
    CU_ASSERT(v->val == (4+1));

    /* insert new min */
    insert_long_val(&avl_tree_1, 3);
    node = avltree_first(&avl_tree_1);
    v = avltree_container_of(node, avl_unit_val_t, node_k);
    CU_ASSERT(v->val == (3+1));

    /* delete current min */
    delete_long_val(&avl_tree_1, 3);
    node = avltree_first(&avl_tree_1);
    v = avltree_container_of(node, avl_unit_val_t, node_k);
    CU_ASSERT(v->val == (4+1));
}

void check_min_2(void)
{
    avl_unit_val_t *v;
    unsigned long mval, rv;
    struct avltree_node *node;
    int ix;

    srand(time(0));

    avl_unit_clear_and_destroy_tree(&avl_tree_1);

    avltree_init(&avl_tree_1, avl_unit_cmpf, 0 /* flags */);

    mval = ULONG_MAX;
    for (ix = 0; ix < 100000; ix++) {
	rv = rand();
	/* in solaris avl, inserting an value that compares equal
	 * to an already inserted value is illegal */
	insert_long_val_safe(&avl_tree_1, rv);
	if ((mval < 0) || (rv < mval))
	    mval = rv;
    }

    node = avltree_first(&avl_tree_1);
    v = avltree_container_of(node, avl_unit_val_t, node_k);
    printf("rv: %lu mval: %lu val: %lu\n", rv, mval, v->val-1);
    CU_ASSERT(v->val == (mval+1));
}


/* The main() function for setting up and running the tests.
 * Returns a CUE_SUCCESS on successful running, another
 * CUnit error code on failure.
 */
int main()
{ 
    /* initialize the CUnit test registry...  get this party started */
    if (CUE_SUCCESS != CU_initialize_registry())
       return CU_get_error();

    /* General avl_tree test. */
    CU_TestInfo avl_tree_unit_1_arr[] = {
      { "Tree insertions 1.", inserts_tree_1 },
      { "Tree check 1.", check_tree_1 },
      { "Tree lookups 1.", lookups_tree_1 },
      { "Tree deletes 1.", deletes_tree_1 },
      CU_TEST_INFO_NULL,
    };

    CU_TestInfo avl_tree_unit_2_arr[] = {
      { "Tree insertions 2.", inserts_tree_2 },
      { "Tree check 2.", check_tree_2 },
      { "Tree lookups 2.", lookups_tree_2 },
      { "Tree deletes 2.", deletes_tree_2 },
      CU_TEST_INFO_NULL,
    };

    CU_TestInfo avl_tree_unit_2r_arr[] = {
      { "Tree insertions 2.", inserts_tree_2r },
      { "Tree check 2.", check_tree_2 },
      { "Tree lookups 2.", lookups_tree_2 },
      { "Tree deletes 2.", deletes_tree_2 },
      CU_TEST_INFO_NULL,
    };

    CU_TestInfo avl_tree_unit_100_arr[] = {
      { "Tree insertions 100.", inserts_tree_100 },
      { "Tree check 100.", check_tree_100 },
      { "Tree lookups 100.", lookups_tree_100 },
      { "Tree traverse 100.", trav_tree_100 },
      { "Tree deletes 100.", deletes_tree_100 },
      CU_TEST_INFO_NULL,
    };

    CU_TestInfo avl_tree_unit_10000_arr[] = {
      { "Tree insertions 10000.", inserts_tree_10000 },
      { "Tree lookups 10000.", lookups_tree_10000 },
      { "Tree check 10000.", check_tree_10000 },
      { "Tree traverse 10000.", trav_tree_10000 },
      { "Tree deletes 10000.", deletes_tree_10000 },
      CU_TEST_INFO_NULL,
    };

    CU_TestInfo avl_tree_unit_min_1_arr[] = {
      { "Check min after inserts, deletes.", check_min_1 },
      { "Check lookup after delete.", check_delete_1 },
#if 1 /* skews perf */
      { "Random min check.", check_min_2 },
#endif
      CU_TEST_INFO_NULL,
    };

    CU_SuiteInfo suites[] = {
      { "Rb tree operations 1", init_suite1, clean_suite1,
	avl_tree_unit_1_arr },
      { "Rb tree operations 2", init_suite2, clean_suite2,
	avl_tree_unit_2_arr },
      { "Rb tree operations 2 R", init_suite2, clean_suite2,
	avl_tree_unit_2r_arr },
      { "Rb tree operations 100", init_suite100, clean_suite100,
	avl_tree_unit_100_arr },
      { "Rb tree operations 10000", init_suite10000, clean_suite10000,
	avl_tree_unit_10000_arr },
      { "Check min 1", init_suite1, clean_suite1,
	avl_tree_unit_min_1_arr },
      CU_SUITE_INFO_NULL,
    };
  
    CU_ErrorCode error = CU_register_suites(suites);

    /* Initialize the avl_tree package */
    avl_unit_PkgInit();
    
    /* Run all tests using the CUnit Basic interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();

    return CU_get_error();
}
