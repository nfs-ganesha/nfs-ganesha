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

#define TEST_ROOT "unlink_latency"
#define TEST_FILE "unlink_test_file"
#define DIR_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class UnlinkEmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      gtest::GaneshaFSALBaseTest::SetUp();
    }

    virtual void TearDown() {
      gtest::GaneshaFSALBaseTest::TearDown();
    }
  };

  class UnlinkFullLatencyTest : public UnlinkEmptyLatencyTest {
  protected:

    virtual void SetUp() {
      UnlinkEmptyLatencyTest::SetUp();

      create_and_prime_many(DIR_COUNT, NULL);
    }

    virtual void TearDown() {
      remove_many(DIR_COUNT, NULL);

      UnlinkEmptyLatencyTest::TearDown();
      }

  };

} /* namespace */

TEST_F(UnlinkEmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct fsal_obj_handle *obj = nullptr;
  struct fsal_obj_handle *lookup = nullptr;

  /* Create file to unlink for the test */
  status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
                  &obj, NULL);
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(obj, nullptr);

  status = test_root->obj_ops->unlink(test_root, obj, TEST_FILE);
  EXPECT_EQ(status.major, 0);

  status = test_root->obj_ops->lookup(test_root, TEST_FILE, &lookup, NULL);
  EXPECT_EQ(status.major, ERR_FSAL_NOENT);
  EXPECT_EQ(lookup, nullptr);

  obj->obj_ops->put_ref(obj);
}

TEST_F(UnlinkEmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  bool caller_perm_check = false;
  struct fsal_obj_handle *sub_hdl = nullptr;
  struct fsal_obj_handle *sub_hdl_obj = nullptr;
  struct fsal_obj_handle *lookup = nullptr;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  gtws_subcall(
	       status = sub_hdl->obj_ops->open2(sub_hdl, NULL, FSAL_O_RDWR,
					       FSAL_UNCHECKED, TEST_FILE, NULL,
					       NULL, &sub_hdl_obj, NULL,
					       &caller_perm_check)
	      );
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(sub_hdl_obj, nullptr);

  status = sub_hdl_obj->obj_ops->close(sub_hdl_obj);
  EXPECT_EQ(status.major, 0);

  status = sub_hdl->obj_ops->unlink(sub_hdl, sub_hdl_obj, TEST_FILE);
  EXPECT_EQ(status.major, 0);

  status = sub_hdl->obj_ops->lookup(sub_hdl, TEST_FILE, &lookup, NULL);
  EXPECT_EQ(status.major, ERR_FSAL_NOENT);
  EXPECT_EQ(lookup, nullptr);

  sub_hdl_obj->obj_ops->put_ref(sub_hdl_obj);
}

TEST_F(UnlinkEmptyLatencyTest, FSALREMOVE)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  /* Create files to unlink for the test */
  status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
                  &obj, NULL);
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(obj, nullptr);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = test_root->obj_ops->link(obj, test_root, fname);
    ASSERT_EQ(status.major, 0);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = fsal_remove(test_root, fname);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per fsal_remove: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove file created for running the test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);

  obj->obj_ops->put_ref(obj);
}

TEST_F(UnlinkFullLatencyTest, BIG)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  /* Create files to unlink for the test */
  status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
                  &obj, NULL);
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(obj, nullptr);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = test_root->obj_ops->link(obj, test_root, fname);
    ASSERT_EQ(status.major, 0);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = test_root->obj_ops->unlink(test_root, obj, fname);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per unlink: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);


  /* Remove file created for running the test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);

  obj->obj_ops->put_ref(obj);
}

TEST_F(UnlinkFullLatencyTest, BIG_BYPASS)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct fsal_obj_handle *sub_root;
  struct fsal_obj_handle *sub_obj;
  struct timespec s_time, e_time;

  /* Create files to unlink for the test */
  status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
                  &obj, NULL);
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(obj, nullptr);

  sub_root = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_root, nullptr);

  sub_obj = mdcdb_get_sub_handle(obj);
  ASSERT_NE(sub_obj, nullptr);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = sub_root->obj_ops->link(sub_obj, sub_root, fname);
    ASSERT_EQ(status.major, 0);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = sub_root->obj_ops->unlink(sub_root, sub_obj, fname);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per unlink: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove file created for running the test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);

  obj->obj_ops->put_ref(obj);
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
