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

#include "gtest.hh"

#define TEST_ROOT "link_latency"
#define TEST_FILE "link_source"
#define TEST_FILE_LINK "link_to_link_source"
#define DIR_COUNT 100000
#define LOOP_COUNT 1000000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class LinkEmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;

      gtest::GaneshaFSALBaseTest::SetUp();

      fsal_prepare_attrs(&attrs_out, 0);

      status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
			   &test_file, &attrs_out);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(test_file, nullptr);

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

  class LinkFullLatencyTest : public LinkEmptyLatencyTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_obj_handle *obj;
      struct attrlist attrs_out;

      LinkEmptyLatencyTest::SetUp();

      /* create a bunch of dirents */
      for (int i = 0; i < DIR_COUNT; ++i) {
        fsal_prepare_attrs(&attrs_out, 0);
        sprintf(fname, "file-%08x", i);

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

      for (int i = 0; i < DIR_COUNT; ++i) {
        sprintf(fname, "file-%08x", i);

        status = fsal_remove(test_root, fname);
        EXPECT_EQ(status.major, 0);
      }

      LinkEmptyLatencyTest::TearDown();
      }

  };

} /* namespace */

TEST_F(LinkEmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct fsal_obj_handle *link;
  struct fsal_obj_handle *lookup;

  enableEvents(event_list);

  status = test_file->obj_ops->link(test_file, test_root, TEST_FILE_LINK);
  EXPECT_EQ(status.major, 0);
  test_root->obj_ops->lookup(test_root, TEST_FILE_LINK, &link, NULL);
  test_root->obj_ops->lookup(test_root, TEST_FILE, &lookup, NULL);
  EXPECT_EQ(lookup, link);

  link->obj_ops->put_ref(link);
  lookup->obj_ops->put_ref(lookup);

  /* Remove link created while running test */
  status = fsal_remove(test_root, TEST_FILE_LINK);
  ASSERT_EQ(status.major, 0);

  disableEvents(event_list);
}

TEST_F(LinkEmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_root;
  struct fsal_obj_handle *sub_file;
  struct fsal_obj_handle *link;
  struct fsal_obj_handle *lookup;

  sub_root = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_root, nullptr);
  sub_file = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_file, nullptr);

  status = sub_root->obj_ops->link(sub_file, sub_root, TEST_FILE_LINK);
  EXPECT_EQ(status.major, 0);
  sub_root->obj_ops->lookup(sub_root, TEST_FILE_LINK, &link, NULL);
  sub_root->obj_ops->lookup(sub_root, TEST_FILE, &lookup, NULL);
  EXPECT_EQ(lookup, link);

  link->obj_ops->put_ref(link);
  lookup->obj_ops->put_ref(lookup);

  /* Remove link created while running test */
  status = fsal_remove(sub_root, TEST_FILE_LINK);
  ASSERT_EQ(status.major, 0);
}

TEST_F(LinkEmptyLatencyTest, LOOP)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = test_root->obj_ops->link(test_file, test_root, fname);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per link: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove link created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

TEST_F(LinkEmptyLatencyTest, FSALLINK)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = fsal_link(test_file, test_root, fname);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per fsal_link: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove link created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

TEST_F(LinkFullLatencyTest, BIG)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = test_root->obj_ops->link(test_file, test_root, fname);
    ASSERT_EQ(status.major, 0) << " failed to link " << fname;
  }

  now(&e_time);

  fprintf(stderr, "Average time per link: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove link created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

TEST_F(LinkFullLatencyTest, BIG_BYPASS)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *sub_root;
  struct fsal_obj_handle *sub_file;
  struct timespec s_time, e_time;

  sub_root = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_root, nullptr);
  sub_file = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_file, nullptr);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = sub_root->obj_ops->link(sub_file, sub_root, fname);
    ASSERT_EQ(status.major, 0) << " failed to link " << fname;
  }

  now(&e_time);

  fprintf(stderr, "Average time per link: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  /* Remove link created while running test */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "link-%08x", i);

    status = fsal_remove(sub_root, fname);
    ASSERT_EQ(status.major, 0);
  }
}

int main(int argc, char *argv[])
{
  int code = 0;
  char* session_name = NULL;

  using namespace std;
  using namespace std::literals;
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
