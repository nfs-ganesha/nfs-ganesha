// SPDX-License-Identifier: LGPL-3.0-or-later
// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Matt Benjamin <mbenjamin@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include <sys/types.h>
#include <iostream>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <random>
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {
/* Don't include rpcent.h; it has C++ issues, and is unneeded */
#define _RPC_RPCENT_H

#include <intrinsic.h>
#include <misc/rbtree_x.h>
#include <misc/queue.h>
#include "nfs_core.h"

/* LTTng headers */
#include <lttng/lttng.h>

/* gperf headers */
#include <gperftools/profiler.h>

  struct rbt_item {
    struct opr_rbtree_node xid_node;
    uint32_t xid;
    /* defeat some caching */
    char pad[65536];
  };

  int
  rbt_item_xid_cmpf(const struct opr_rbtree_node *lhs,
		    const struct opr_rbtree_node *rhs)
  {
    struct rbt_item *lk, *rk;

    lk = opr_containerof(lhs, struct rbt_item, xid_node);
    rk = opr_containerof(rhs, struct rbt_item, xid_node);

    if (lk->xid < rk->xid)
      return (-1);

    if (lk->xid == rk->xid)
      return (0);

    return (1);
  }

} /* extern "C" */

namespace {

  char* profile_out = nullptr; //"/tmp/profile.out";

  struct opr_rbtree call_replies;
  struct rbt_item *rbt_arr1;

  bool verbose = false;
  static constexpr uint32_t item_wsize = 100000;
  static constexpr uint32_t num_calls = 1000000;

  uint32_t xid_ix;

  class RBTLatency1 : public ::testing::Test {

    virtual void SetUp() {
      rbt_arr1 = new rbt_item[item_wsize];

      opr_rbtree_init(&call_replies, rbt_item_xid_cmpf);

      /* fill window */
      for (xid_ix = 0; xid_ix < item_wsize; ++xid_ix) {
	rbt_item *item = &rbt_arr1[xid_ix];

	if (verbose) {
	  std::cout << "INIT"
		    << " insert next_xid: " << xid_ix
		    << std::endl;
	}

	item->xid = xid_ix; // yes, don't usually have xid 0
	opr_rbtree_insert(&call_replies, &item->xid_node);
      }
    }

    virtual void TearDown() {
      delete[] rbt_arr1;
    }

  };

} /* namespace */

TEST_F(RBTLatency1, RUN1)
{
  rbt_item *item;
  struct opr_rbtree_node *nv;
  struct rbt_item item_k;
  struct timespec s_time, e_time;

  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  uint32_t prev_xid = 0;
  uint32_t next_xid = xid_ix;
  for (uint32_t call_ctr = 0; call_ctr < num_calls; ++call_ctr) {
    /* delete 1 and insert 1 */

    if (verbose) {
      std::cout
	<< " remove prev_xid: " << prev_xid
	<< " insert next_xid: " << next_xid
	<< std::endl;
    }

    /* lookup at oldest position */
    item_k.xid = prev_xid;
    nv = opr_rbtree_lookup(&call_replies, &item_k.xid_node);
    item = opr_containerof(nv, struct rbt_item, xid_node);

    /* remove it */
    opr_rbtree_remove(&call_replies, &item->xid_node);

    /* reinsert at highest position */
    item->xid = next_xid;
    opr_rbtree_insert(&call_replies, &item->xid_node);

    ++next_xid;
    ++prev_xid;
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();

  uint64_t dt = timespec_diff(&s_time, &e_time);
  uint64_t reqs_s = num_calls / (double(dt) / 1000000000);

  fprintf(stderr, "total run time: %" PRIu64 " (" PRIu64 " reqs %" PRIu64
	  " reqs/s) \n", dt, reqs_s);

}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
