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

#define TEST_ROOT "nfs4_lookup_latency"
#define FILE_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* event_list = nullptr;
  char* profile_out = nullptr;

  class LookupEmptyLatencyTest : public gtest::GaeshaNFS4BaseTest {
  };

  class LookupFullLatencyTest : public gtest::GaeshaNFS4BaseTest {

  protected:

    virtual void SetUp() {
      GaeshaNFS4BaseTest::SetUp();

      create_and_prime_many(FILE_COUNT, objs);
    }

    virtual void TearDown() {
      remove_many(FILE_COUNT, objs);

      GaeshaNFS4BaseTest::TearDown();
    }

    struct fsal_obj_handle *objs[FILE_COUNT];
  };

} /* namespace */

TEST_F(LookupEmptyLatencyTest, SIMPLE)
{
  int rc;

  setCurrentFH(root_entry);
  setup_lookup(0, TEST_ROOT);

  enableEvents(event_list);

  rc = nfs4_op_lookup(&ops[0], data, &resp);

  EXPECT_EQ(rc, NFS4_OK);
  EXPECT_EQ(test_root, data->current_obj);

  disableEvents(event_list);
}

TEST_F(LookupEmptyLatencyTest, LOOP)
{
  int rc;
  struct timespec s_time, e_time;

  setup_lookup(0, TEST_ROOT);

  enableEvents(event_list);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    setCurrentFH(root_entry);
    rc = nfs4_op_lookup(&ops[0], data, &resp);
    EXPECT_EQ(rc, NFS4_OK);
    EXPECT_EQ(test_root, data->current_obj);
  }

  now(&e_time);

  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

}

TEST_F(LookupFullLatencyTest, BIG_SINGLE)
{
  int rc;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  enableEvents(event_list);

  now(&s_time);

  sprintf(fname, "f-%08x", FILE_COUNT / 5);

  setup_lookup(0, fname);

  setCurrentFH(test_root);
  rc = nfs4_op_lookup(&ops[0], data, &resp);
  EXPECT_EQ(rc, NFS4_OK);
  EXPECT_EQ(objs[FILE_COUNT / 5], data->current_obj);

  now(&e_time);

  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time));
}

TEST_F(LookupFullLatencyTest, BIG)
{
  int rc;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  enableEvents(event_list);
  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    int n = i % FILE_COUNT;

    sprintf(fname, "f-%08x", n);

    setup_lookup(0, fname);
    setCurrentFH(test_root);
    rc = nfs4_op_lookup(&ops[0], data, &resp);
    EXPECT_EQ(rc, NFS4_OK);
    EXPECT_EQ(objs[n], data->current_obj);
    cleanup_lookup(0);
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();
  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
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
