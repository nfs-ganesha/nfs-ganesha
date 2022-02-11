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

#define TEST_ROOT "mkdir_latency"
#define FILE_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class MkdirEmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      gtest::GaneshaFSALBaseTest::SetUp();
    }

    virtual void TearDown() {
      gtest::GaneshaFSALBaseTest::TearDown();
    }
  };

  class MkdirFullLatencyTest : public MkdirEmptyLatencyTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_obj_handle *obj;
      struct fsal_attrlist attrs_out;

      MkdirEmptyLatencyTest::SetUp();

      /* create a bunch of dirents */
      for (int i = 0; i < FILE_COUNT; ++i) {
        fsal_prepare_attrs(&attrs_out, 0);
        sprintf(fname, "f-%08x", i);

        status = fsal_create(test_root, fname, REGULAR_FILE, &attrs, NULL, &obj,
                             &attrs_out);
        ASSERT_EQ(status.major, 0);
        ASSERT_NE(obj, nullptr);

	fsal_release_attrs(&attrs_out);
        obj->obj_ops->put_ref(obj);
      }
    }

    virtual void TearDown() {
      fsal_status_t status;
      char fname[NAMELEN];

      for (int i = 0; i < FILE_COUNT; ++i) {
        sprintf(fname, "f-%08x", i);

        status = fsal_remove(test_root, fname);
        EXPECT_EQ(status.major, 0);
      }

      MkdirEmptyLatencyTest::TearDown();
    }

  };

} /* namespace */

TEST_F(MkdirEmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct fsal_obj_handle *mkdir;
  struct fsal_obj_handle *lookup;

  status = test_root->obj_ops->mkdir(test_root, TEST_ROOT, &attrs, &mkdir, NULL);
  EXPECT_EQ(status.major, 0);
  test_root->obj_ops->lookup(test_root, TEST_ROOT, &lookup, NULL);
  EXPECT_EQ(lookup, mkdir);

  mkdir->obj_ops->put_ref(mkdir);
  lookup->obj_ops->put_ref(lookup);

  /* Remove directory created while running test */
  status = fsal_remove(test_root, TEST_ROOT);
  ASSERT_EQ(status.major, 0);
}

TEST_F(MkdirEmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  struct fsal_obj_handle *mkdir;
  struct fsal_obj_handle *lookup;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  gtws_subcall(
    status = sub_hdl->obj_ops->mkdir(sub_hdl, TEST_ROOT, &attrs, &mkdir, NULL)
    );

  EXPECT_EQ(status.major, 0);
  sub_hdl->obj_ops->lookup(sub_hdl, TEST_ROOT, &lookup, NULL);
  EXPECT_EQ(lookup, mkdir);

  lookup->obj_ops->put_ref(lookup);

  /* Remove directory created while running test */
  status = sub_hdl->obj_ops->unlink(sub_hdl, mkdir, TEST_ROOT);
  ASSERT_EQ(status.major, 0);

  mkdir->obj_ops->put_ref(mkdir);
}

TEST_F(MkdirEmptyLatencyTest, LOOP)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = test_root->obj_ops->mkdir(test_root, fname, &attrs, &obj, NULL);
    EXPECT_EQ(status.major, 0);
    obj->obj_ops->put_ref(obj);
  }

  now(&e_time);

  fprintf(stderr, "Average time per mkdir: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove directories created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

TEST_F(MkdirEmptyLatencyTest, FSALCREATE)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = fsal_create(test_root, fname, DIRECTORY, &attrs, NULL, &obj, NULL);
    EXPECT_EQ(status.major, 0);
    obj->obj_ops->put_ref(obj);
  }

  now(&e_time);

  fprintf(stderr, "Average time per fsal_create: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove directories created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
   }
}

TEST_F(MkdirFullLatencyTest, BIG)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = test_root->obj_ops->mkdir(test_root, fname, &attrs, &obj, NULL);
    ASSERT_EQ(status.major, 0) << " failed to mkdir " << fname;
    obj->obj_ops->put_ref(obj);
  }

  now(&e_time);

  fprintf(stderr, "Average time per mkdir: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove directories created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

TEST_F(MkdirFullLatencyTest, BIG_BYPASS)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *sub_hdl;
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    gtws_subcall(
      status = sub_hdl->obj_ops->mkdir(sub_hdl, fname, &attrs, &obj, NULL)
      );
    ASSERT_EQ(status.major, 0) << " failed to mkdir " << fname;
    obj->obj_ops->put_ref(obj);
  }

  now(&e_time);

  fprintf(stderr, "Average time per mkdir: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove directories created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i);

    sub_hdl->obj_ops->lookup(sub_hdl, fname, &obj, NULL);
    status = sub_hdl->obj_ops->unlink(sub_hdl, obj, fname);
    ASSERT_EQ(status.major, 0);

    obj->obj_ops->put_ref(obj);
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
