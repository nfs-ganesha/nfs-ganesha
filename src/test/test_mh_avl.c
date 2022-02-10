// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "CUnit/Basic.h"

#include "abstract_mem.h"
#include "avltree.h"
#include "murmur3.h"

#define DEBUG 1

/* STATICS  we use across multiple tests */
struct avltree avl_tree_1;

/* dirent-like structure */
typedef struct avl_unit_val {
	struct avltree_node node_n;
	struct avltree_node node_hk;
	struct {
		uint64_t k;
		uint32_t p;	/* nprobes , eff. metric */
	} hk;
	char *name;
	uint64_t fsal_cookie;
} avl_unit_val_t;

static inline int avl_unit_hk_cmpf(const struct avltree_node *lhs,
				   const struct avltree_node *rhs)
{
	avl_unit_val_t *lk, *rk;

	lk = avltree_container_of(lhs, avl_unit_val_t, node_hk);
	rk = avltree_container_of(rhs, avl_unit_val_t, node_hk);

	if (lk->hk.k < rk->hk.k)
		return -1;

	if (lk->hk.k == rk->hk.k)
		return 0;

	return 1;
}

avl_unit_val_t *avl_unit_new_val(const char *name)
{
	avl_unit_val_t *v = gsh_malloc(sizeof(avl_unit_val_t));

	memset(v, 0, sizeof(avl_unit_val_t));
	v->name = (char *)name;

	return v;
}

static int qp_avl_insert(struct avltree *t, avl_unit_val_t *v)
{
	/*
	 * Insert with quadatic, linear probing.  A unique k is assured for
	 * any k whenever size(t) < max(uint64_t).
	 *
	 * First try quadratic probing, with coeff. 2 (since m = 2^n.)
	 * A unique k is not assured, since the codomain is not prime.
	 * If this fails, fall back to linear probing from hk.k+1.
	 *
	 * On return, the stored key is in v->hk.k, the iteration
	 * count in v->hk.p.
	 **/
	struct avltree_node *tmpnode;
	uint32_t j, j2;
	uint32_t hk[4];

	assert(avltree_size(t) < UINT64_MAX);

	MurmurHash3_x64_128(v->name, strlen(v->name), 67, hk);
	memcpy(&v->hk.k, hk, 8);

	for (j = 0; j < UINT64_MAX; j++) {
		v->hk.k = (v->hk.k + (j * 2));
		tmpnode = avltree_insert(&v->node_hk, t);
		if (!tmpnode) {
			/* success, note iterations and return */
			v->hk.p = j;
			return 0;
		}
	}

	/* warn debug */

	memcpy(&v->hk.k, hk, 8);
	for (j2 = 1 /* tried j=0 */; j2 < UINT64_MAX; j2++) {
		v->hk.k = v->hk.k + j2;
		tmpnode = avltree_insert(&v->node_hk, t);
		if (!tmpnode) {
			/* success, note iterations and return */
			v->hk.p = j + j2;
			return 0;
		}
		j2++;
	}

	/* warn crit  */
	return -1;
}

static avl_unit_val_t *qp_avl_lookup_s(struct avltree *t, avl_unit_val_t *v,
				       int maxj)
{
	struct avltree_node *node;
	avl_unit_val_t *v2;
	uint32_t j;
	uint32_t hk[4];

	assert(avltree_size(t) < UINT64_MAX);

	MurmurHash3_x64_128(v->name, strlen(v->name), 67, hk);
	memcpy(&v->hk.k, hk, 8);

	for (j = 0; j < maxj; j++) {
		v->hk.k = (v->hk.k + (j * 2));
		node = avltree_lookup(&v->node_hk, t);
		if (node) {
			/* it's almost but not entirely certain that node is
			 * related to v.  in the general case, j is also not
			 * constrained to be v->hk.p */
			v2 = avltree_container_of(node, avl_unit_val_t,
						  node_hk);
			if (!strcmp(v->name, v2->name))
				return v2;
		}
	}

	/* warn crit  */
	return NULL;
}

static struct dir_data {
	char *name;
} dir_data[] = {
	{ ".gitignore" }, { "Makefile" }, { "Makefile.gate" }, { "acpi-ext.c" },
	{ "acpi-processor.c" }, { "acpi.c" }, { "asm-offsets.c" },
	{ "audit.c" }, { "brl_emu.c" }, { "cpufreq" }, { "crash.c" },
	{ "crash_dump.c" }, { "cyclone.c" }, { "dma-mapping.c" }, { "efi.c" },
	{ "efi_stub.S" }, { "entry.S" }, { "entry.h" }, { "err_inject.c" },
	{ "esi.c" }, { "esi_stub.S" }, { "fsys.S" },
	{ "fsyscall_gtod_data.h" }, { "ftrace.c" }, { "gate-data.S" },
	{ "gate.S" }, { "gate.lds.S" }, { "head.S" }, { "ia64_ksyms.c" },
	{ "init_task.c" }, { "iosapic.c" }, { "irq.c" }, { "irq_ia64.c" },
	{ "irq_lsapic.c" }, { "ivt.S" }, { "jprobes.S" }, { "kprobes.c" },
	{ "machine_kexec.c" }, { "machvec.c" }, { "mca.c" }, { "mca_asm.S" },
	{ "mca_drv.c" }, { "mca_drv.h" }, { "mca_drv_asm.S" }, { "minstate.h" },
	{ "module.c" }, { "msi_ia64.c" }, { "nr-irqs.c" }, { "numa.c" },
	{ "pal.S" }, { "palinfo.c" }, { "paravirt.c" }, { "paravirt_inst.h" },
	{ "paravirt_patch.c" }, { "paravirt_patchlist.c" },
	{ "paravirt_patchlist.h" }, { "paravirtentry.S" }, { "patch.c" },
	{ "pci-dma.c" }, { "pci-swiotlb.c" }, { "perfmon.c" },
	{ "perfmon_default_smpl.c" }, { "perfmon_generic.h" },
	{ "perfmon_itanium.h" }, { "perfmon_mckinley.h" },
	{ "perfmon_montecito.h" }, { "process.c" }, { "ptrace.c" },
	{ "relocate_kernel.S" }, { "sal.c" }, { "salinfo.c" }, { "setup.c" },
	{ "sigframe.h" }, { "signal.c" }, { "smp.c" }, { "smpboot.c" },
	{ "sys_ia64.c" }, { "time.c" }, { "topology.c" }, { "traps.c" },
	{ "unaligned.c" }, { "uncached.c" }, { "unwind.c" },
	{ "unwind_decoder.c" }, { "unwind_i.h" }, { "vmlinux.lds.S" }, { 0 } };

void avl_unit_free_val(avl_unit_val_t *v)
{
	gsh_free(v);
}

void avl_unit_clear_tree(struct avltree *t)
{
	avl_unit_val_t *v;
	struct avltree_node *node, *next_node;

	if (avltree_size(t) < 1)
		return;

	node = avltree_first(t);
	while (node) {
		next_node = avltree_next(node);
		v = avltree_container_of(node, avl_unit_val_t, node_hk);
		avltree_remove(&v->node_hk, &avl_tree_1);
		gsh_free(v->name);
		avl_unit_free_val(v);
		node = next_node;
	}
}

/* dne */
void avltree_destroy(struct avltree *t)
{
	/* return */
}

void avl_unit_clear_and_destroy_tree(struct avltree *t)
{
	avl_unit_clear_tree(t);
	avltree_destroy(t);
}

/*
 *  BEGIN SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

void avl_setup(void)
{
	/* nothing */
}

void avl_teardown(void)
{
	/* nothing */
}

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

	avltree_init(&avl_tree_1, avl_unit_hk_cmpf, 0 /* flags */);

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
 *  END SUITE INITIALIZATION and CLEANUP FUNCTIONS
 */

/*
 *  BEGIN BASIC TESTS
 */

void inserts_tree_1(void)
{
	avl_unit_val_t *v;
	char *s;
	int ix, code;

	ix = 0;
	while ((s = dir_data[ix].name) != NULL) {
		v = avl_unit_new_val(gsh_strdup(s));
		code = qp_avl_insert(&avl_tree_1, v);
		if (code == -1)
			abort();
		if (v->hk.p > 0)
			printf("%d positive p %d %s\n", ix, v->hk.p, s);
		++ix;
	}
}

void check_tree_1(void)
{
	int code = 0;

	CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_1(void)
{
	avl_unit_val_t *v2, *v;
	char *s;
	int ix;

	ix = 0;
	while ((s = dir_data[ix].name) != NULL) {
		v = avl_unit_new_val(s);

		/* lookup mapping */
		v2 = qp_avl_lookup_s(&avl_tree_1, v, 1);
		if (!v2) {
			abort();
		} else {
			/*
			printf("%d %d %s\n", ix, v2->hk.p, v2->name);
			*/
		}
		++ix;
	}

	/* free v */
	avl_unit_free_val(v);
}

void deletes_tree_1(void)
{
	avl_unit_clear_tree(&avl_tree_1);
	avltree_init(&avl_tree_1, avl_unit_hk_cmpf, 0 /* flags */);
}

void inserts_tree_2(void)
{
	avl_unit_val_t *v;
	char s[256];
	int ix, code;

	for (ix = 0; ix < 100000; ++ix) {
		sprintf(s, "file%d", ix);
		v = avl_unit_new_val(gsh_strdup(s));
		code = qp_avl_insert(&avl_tree_1, v);
		if (code == -1)
			abort();
		if (v->hk.p > 0)
			printf("%d positive p %d %s\n", ix, v->hk.p, s);
	}
}

void check_tree_2(void)
{
	int code = 0;

	CU_ASSERT_EQUAL(code, 0);
}

void lookups_tree_2(void)
{
	avl_unit_val_t *v2, *v;
	char s[256];
	int ix;

	/* attempts 100K mits, 100K misses */
	for (ix = 0; ix < 200000; ++ix) {
		sprintf(s, "file%d", ix);
		v = avl_unit_new_val(s);
		v2 = qp_avl_lookup_s(&avl_tree_1, v, 1);
		if (!v2) {
			if (ix < 100000) {
				abort();
			} else {
				/*
				printf("%d %d %s\n", ix, v2->hk.p, v2->name);
				*/
			}
		}
	}

	/* free v */
	avl_unit_free_val(v);
}

void deletes_tree_2(void)
{
	avl_unit_clear_tree(&avl_tree_1);
}

/* The main() function for setting up and running the tests.
 * Returns a CUE_SUCCESS on successful running, another
 * CUnit error code on failure.
 */
int main(int argc, char *argv[])
{
	/* initialize the CUnit test registry...  get this party started */
	if (CU_initialize_registry() != CUE_SUCCESS)
		return CU_get_error();

	/* General avl_tree test. */
	CU_TestInfo avl_tree_unit_1_arr[] = {
		{"Tree insertions 1.", inserts_tree_1}
		,
		{"Tree check 1.", check_tree_1}
		,
		{"Tree lookups 1.", lookups_tree_1}
		,
		{"Tree deletes 1.", deletes_tree_1}
		,
		{"Tree insertions 2.", inserts_tree_2}
		,
		{"Tree check 2.", check_tree_2}
		,
		{"Tree lookups 2.", lookups_tree_2}
		,
		{"Tree deletes 2.", deletes_tree_2}
		,
		CU_TEST_INFO_NULL,
	};

	CU_SuiteInfo suites[] = {
		{"Rb tree operations 1", init_suite1, clean_suite1,
		 avl_setup, avl_teardown, avl_tree_unit_1_arr}
		,
		CU_SUITE_INFO_NULL,
	};

	CU_register_suites(suites);

	/* Initialize the avl_tree package */
	avl_unit_PkgInit();

	/* Run all tests using the CUnit Basic interface */
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_cleanup_registry();

	return CU_get_error();
}
