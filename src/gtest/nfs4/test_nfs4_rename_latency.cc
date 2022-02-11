// SPDX-License-Identifier: LGPL-3.0-or-later
// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Frank Filz <ffilzlnx@mindspring.com>
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
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

#include "gtest_nfs4.hh"

extern "C" {
/* Manually forward this, an 9P is not C++ safe */
void admin_halt(void);
/* Ganesha headers */
//#include "sal_data.h"
//#include "common_utils.h"
/* For MDCACHE bypass.  Use with care */
#include "../FSAL/Stackable_FSALs/FSAL_MDCACHE/mdcache_debug.h"
}

#define TEST_ROOT "nfs4_rename_latency"
#define TEST_ROOT2 "nfs4_rename_latency2"
#define FILE_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* event_list = nullptr;
  char* profile_out = nullptr;

  class RenameEmptyLatencyTest : public gtest::GaeshaNFS4BaseTest {
  };

  class RenameFullLatencyTest : public gtest::GaeshaNFS4BaseTest {

  protected:

    virtual void SetUp() {
      GaeshaNFS4BaseTest::SetUp();

      create_and_prime_many(FILE_COUNT, objs);

      set_saved_export();
    }

    virtual void TearDown() {
      remove_many(FILE_COUNT, objs);

      GaeshaNFS4BaseTest::TearDown();
    }

    struct fsal_obj_handle *objs[FILE_COUNT];
  };

} /* namespace */

TEST_F(RenameEmptyLatencyTest, SIMPLE)
{
  int rc;

  setCurrentFH(root_entry);
  setSavedFH(root_entry);
  setup_rename(0, TEST_ROOT, TEST_ROOT2);

  enableEvents(event_list);

  rc = nfs4_op_rename(&ops[0], data, &resp);

  EXPECT_EQ(rc, NFS4_OK);

  disableEvents(event_list);

  swap_rename(0);

  rc = nfs4_op_rename(&ops[0], data, &resp);

  EXPECT_EQ(rc, NFS4_OK);
}

TEST_F(RenameEmptyLatencyTest, LOOP)
{
  int rc;
  struct timespec s_time, e_time;

  setCurrentFH(root_entry);
  setSavedFH(root_entry);
  setup_rename(0, TEST_ROOT, TEST_ROOT2);

  enableEvents(event_list);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    rc = nfs4_op_rename(&ops[0], data, &resp);
    EXPECT_EQ(rc, NFS4_OK);
    /* Set up so next time, we rename back... Even loop count value assures
     * that the file ends up having the original name.
     */
    swap_rename(0);
  }

  now(&e_time);

  disableEvents(event_list);

  fprintf(stderr, "Average time per rename: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

}

TEST_F(RenameFullLatencyTest, BIG_SINGLE)
{
  int rc;
  char fname[NAMELEN], fname2[NAMELEN];
  struct timespec s_time, e_time;

  enableEvents(event_list);

  now(&s_time);

  sprintf(fname, "f-%08x", FILE_COUNT / 5);
  sprintf(fname2, "r-%08x", FILE_COUNT / 5);

  setup_rename(0, fname, fname2);

  setCurrentFH(test_root);
  setSavedFH(test_root);
  rc = nfs4_op_rename(&ops[0], data, &resp);
  EXPECT_EQ(rc, NFS4_OK);

  now(&e_time);

  disableEvents(event_list);

  fprintf(stderr, "Average time per rename: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time));

  swap_rename(0);

  rc = nfs4_op_rename(&ops[0], data, &resp);

  EXPECT_EQ(rc, NFS4_OK);
}

TEST_F(RenameFullLatencyTest, BIG)
{
  int rc;
  char fname[NAMELEN], fname2[NAMELEN];
  struct timespec s_time, e_time;

  enableEvents(event_list);
  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    int n = i % FILE_COUNT;

    sprintf(fname, "f-%08x", n);
    sprintf(fname2, "r-%08x", n);

    if ((int)(i / FILE_COUNT) % 2 == 0) {
      /* On odd cycles, rename from original */
      setup_rename(0, fname, fname2);
    } else {
      /* On even cycles, rename back to original */
      setup_rename(0, fname2, fname);
    }
    setCurrentFH(test_root);
    setSavedFH(test_root);
    rc = nfs4_op_rename(&ops[0], data, &resp);
    EXPECT_EQ(rc, NFS4_OK);
    cleanup_rename(0);
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();
  disableEvents(event_list);

  fprintf(stderr, "Average time per rename: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

int main(int argc, char *argv[])
{
  int code = 0;
  char* session_name = NULL;
  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;

  using namespace std;
  namespace po = boost::program_options;

  po::options_description opts("program options");
  po::variables_map vm;

  try {

    opts.add_options()
      ("config", po::value<string>(),
       "path to Ganesha conf file")

      ("logfile", po::value<string>(),
       "log to the provided file path")

      ("export", po::value<uint16_t>(),
       "id of export on which to operate (must exist)")

      ("debug", po::value<string>(),
       "ganesha debug level")

      ("session", po::value<string>(),
       "LTTng session name")

      ("event-list", po::value<string>(),
       "LTTng event list, comma separated")

      ("profile", po::value<string>(),
       "Enable profiling and set output file.")
      ;

    po::variables_map::iterator vm_iter;
    po::command_line_parser parser{argc, argv};
    parser.options(opts).allow_unregistered();
    po::store(parser.run(), vm);
    po::notify(vm);

    // use config vars--leaves them on the stack
    vm_iter = vm.find("config");
    if (vm_iter != vm.end()) {
      ganesha_conf = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("logfile");
    if (vm_iter != vm.end()) {
      lpath = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("debug");
    if (vm_iter != vm.end()) {
      dlevel = ReturnLevelAscii(
         (char*) vm_iter->second.as<std::string>().c_str());
    }
    vm_iter = vm.find("export");
    if (vm_iter != vm.end()) {
      export_id = vm_iter->second.as<uint16_t>();
    }
    vm_iter = vm.find("session");
    if (vm_iter != vm.end()) {
      session_name = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("event-list");
    if (vm_iter != vm.end()) {
      event_list = (char*) vm_iter->second.as<std::string>().c_str();
    }
    vm_iter = vm.find("profile");
    if (vm_iter != vm.end()) {
      profile_out = (char*) vm_iter->second.as<std::string>().c_str();
    }

    ::testing::InitGoogleTest(&argc, argv);
    gtest::env = new gtest::Environment(ganesha_conf, lpath, dlevel,
                                        session_name, TEST_ROOT, export_id);
    ::testing::AddGlobalTestEnvironment(gtest::env);

    code  = RUN_ALL_TESTS();
  }

  catch(po::error& e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch(...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
