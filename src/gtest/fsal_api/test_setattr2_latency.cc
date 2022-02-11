// SPDX-License-Identifier: LGPL-3.0-or-later
// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Girjesh Rajoria <grajoria@redhat.com>
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

#include "gtest.hh"

extern "C" {
/* Manually forward this, as 9P is not C++ safe */
void admin_halt(void);
/* Ganesha headers */
#include "export_mgr.h"
#include "nfs_exports.h"
#include "sal_data.h"
#include "fsal.h"
#include "common_utils.h"
/* For MDCACHE bypass.  Use with care */
#include "../FSAL/Stackable_FSALs/FSAL_MDCACHE/mdcache_debug.h"
}

#define TEST_ROOT "setattr2_latency"
#define TEST_FILE "setattr2_test_file"
#define DIR_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class Setattr2EmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct fsal_attrlist attrs_out;

      gtest::GaneshaFSALBaseTest::SetUp();

      status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL, &test_file,
                             &attrs_out);
      ASSERT_EQ(status.major, 0);

      fsal_release_attrs(&attrs_out);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = fsal_remove(test_root, TEST_FILE);
      EXPECT_EQ(status.major, 0);
      test_file->obj_ops->put_ref(test_file);
      test_file = NULL;

      gtest::GaneshaFSALBaseTest::TearDown();
    }

    struct fsal_obj_handle *test_file = nullptr;
  };

  class Setattr2FullLatencyTest : public Setattr2EmptyLatencyTest {
  protected:

    virtual void SetUp() {
      Setattr2EmptyLatencyTest::SetUp();

      create_and_prime_many(DIR_COUNT, NULL);
    }

    virtual void TearDown() {
      remove_many(DIR_COUNT, NULL);

      Setattr2EmptyLatencyTest::TearDown();
    }

  };

} /* namespace */

TEST_F(Setattr2EmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;

  status = test_file->obj_ops->setattr2(test_file, false, NULL, &attrs);
  EXPECT_EQ(status.major, 0);
}

TEST_F(Setattr2EmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;

  sub_hdl = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_hdl, nullptr);

  status = sub_hdl->obj_ops->setattr2(sub_hdl, false, NULL, &attrs);
  EXPECT_EQ(status.major, 0);
}

TEST_F(Setattr2EmptyLatencyTest, FSAL_SETATTR)
{
  fsal_status_t status;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = fsal_setattr(test_file, false, NULL, &attrs);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per fsal_setattr: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(Setattr2FullLatencyTest, BIG_CACHED)
{
  fsal_status_t status;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = test_file->obj_ops->setattr2(test_file, false, NULL, &attrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per setattr2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(Setattr2FullLatencyTest, BIG_UNCACHED)
{
  fsal_status_t status;
  struct fsal_obj_handle *obj[DIR_COUNT];
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  for (int i = 0; i < DIR_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = test_root->obj_ops->lookup(test_root, fname, &obj[i], NULL);
    ASSERT_EQ(status.major, 0);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = obj[i % DIR_COUNT]->obj_ops->setattr2(obj[i % DIR_COUNT], false, NULL, &attrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per setattr2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  for (int i = 0; i < DIR_COUNT; ++i) {
    obj[i]->obj_ops->put_ref(obj[i]);
  }
}

TEST_F(Setattr2FullLatencyTest, BIG_BYPASS_CACHED)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  struct timespec s_time, e_time;

  sub_hdl = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_hdl, nullptr);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = sub_hdl->obj_ops->setattr2(sub_hdl, false, NULL, &attrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per setattr2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(Setattr2FullLatencyTest, BIG_BYPASS_UNCACHED)
{
  fsal_status_t status;
  struct fsal_obj_handle *obj[DIR_COUNT];
  struct fsal_obj_handle *sub_hdl[DIR_COUNT];
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  for (int i = 0; i < DIR_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = test_root->obj_ops->lookup(test_root, fname, &obj[i], NULL);
    ASSERT_EQ(status.major, 0);
    sub_hdl[i] = mdcdb_get_sub_handle(obj[i]);
    ASSERT_NE(sub_hdl[i], nullptr);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = sub_hdl[i % DIR_COUNT]->obj_ops->setattr2(sub_hdl[i % DIR_COUNT], false, NULL, &attrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per setattr2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  for (int i = 0; i < DIR_COUNT; ++i) {
    obj[i]->obj_ops->put_ref(obj[i]);
  }
}

int main(int argc, char *argv[])
{
  int code = 0;
  char* session_name = NULL;

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
