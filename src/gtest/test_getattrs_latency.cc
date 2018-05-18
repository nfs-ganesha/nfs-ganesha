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

#define TEST_ROOT "getattrs_latency"
#define DIR_COUNT 100000
#define LOOP_COUNT 1000000
#define NAMELEN 16

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class GetattrsEmptyLatencyTest : public gtest::GaneshaBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;

      gtest::GaneshaBaseTest::SetUp();

      a_export = get_gsh_export(export_id);
      ASSERT_NE(a_export, nullptr);

      status = nfs_export_get_root_entry(a_export, &root_entry);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(root_entry, nullptr);

      /* Ganesha call paths need real or forged context info */
      memset(&user_credentials, 0, sizeof(struct user_cred));
      memset(&req_ctx, 0, sizeof(struct req_op_context));
      memset(&attrs, 0, sizeof(attrs));
      memset(&exp_perms, 0, sizeof(struct export_perms));

      req_ctx.ctx_export = a_export;
      req_ctx.fsal_export = a_export->fsal_export;
      req_ctx.creds = &user_credentials;
      req_ctx.export_perms = &exp_perms;

      /* stashed in tls */
      op_ctx = &req_ctx;

      // create root directory for test
      FSAL_SET_MASK(attrs.valid_mask,
		    ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
      attrs.mode = 0777; /* XXX */
      attrs.owner = 667;
      attrs.group = 766;
      fsal_prepare_attrs(&attrs_out, 0);

      status = fsal_create(root_entry, TEST_ROOT, DIRECTORY, &attrs, NULL,
			   &test_root, &attrs_out);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(test_root, nullptr);

      fsal_release_attrs(&attrs_out);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = test_root->obj_ops.unlink(root_entry, test_root, TEST_ROOT);
      EXPECT_EQ(0, status.major);
      test_root->obj_ops.put_ref(test_root);
      test_root = NULL;

      root_entry->obj_ops.put_ref(root_entry);
      root_entry = NULL;

      put_gsh_export(a_export);
      a_export = NULL;

      gtest::GaneshaBaseTest::TearDown();
    }

    struct req_op_context req_ctx;
    struct user_cred user_credentials;
    struct attrlist attrs;
    struct export_perms exp_perms;

    struct gsh_export* a_export = nullptr;
    struct fsal_obj_handle *root_entry = nullptr;
    struct fsal_obj_handle *test_root = nullptr;
  };

  class GetattrsFullLatencyTest : public GetattrsEmptyLatencyTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_obj_handle *obj;
      struct attrlist attrs_out;

      GetattrsEmptyLatencyTest::SetUp();

      /* create a bunch of dirents */
      for (int i = 0; i < DIR_COUNT; ++i) {
        fsal_prepare_attrs(&attrs_out, 0);
        sprintf(fname, "f-%08x", i);

        status = fsal_create(test_root, fname, REGULAR_FILE, &attrs, NULL, &obj,
                             &attrs_out);
        ASSERT_EQ(status.major, 0);
        ASSERT_NE(obj, nullptr);

        fsal_release_attrs(&attrs_out);
        obj->obj_ops.put_ref(obj);
      }
    }

    virtual void TearDown() {
      fsal_status_t status;
      char fname[NAMELEN];

      for (int i = 0; i < DIR_COUNT; ++i) {
        sprintf(fname, "f-%08x", i);

        status = fsal_remove(test_root, fname);
        EXPECT_EQ(status.major, 0);
      }

      GetattrsEmptyLatencyTest::TearDown();
    }

  };

} /* namespace */

TEST_F(GetattrsEmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct attrlist outattrs;

  status = test_root->obj_ops.getattrs(test_root, &outattrs);
  EXPECT_EQ(status.major, 0);
}

TEST_F(GetattrsEmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  struct attrlist outattrs;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  status = sub_hdl->obj_ops.getattrs(sub_hdl, &outattrs);
  EXPECT_EQ(status.major, 0);
}

TEST_F(GetattrsEmptyLatencyTest, GET_OPTIONAL_ATTRS)
{
  fsal_status_t status;
  struct attrlist outattrs;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = get_optional_attrs(test_root, &outattrs);
    EXPECT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per get_optional_attrs: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(GetattrsFullLatencyTest, BIG_CACHED)
{
  fsal_status_t status;
  struct attrlist outattrs;
  struct timespec s_time, e_time;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = test_root->obj_ops.getattrs(test_root, &outattrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per getattrs: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(GetattrsFullLatencyTest, BIG_UNCACHED)
{
  fsal_status_t status;
  struct attrlist outattrs;
  struct fsal_obj_handle *obj[LOOP_COUNT];
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i % DIR_COUNT);

    status = test_root->obj_ops.lookup(test_root, fname, &obj[i], NULL);
    ASSERT_EQ(status.major, 0);
    ASSERT_NE(obj[i], nullptr);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = obj[i]->obj_ops.getattrs(obj[i], &outattrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per getattrs: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    obj[i]->obj_ops.put_ref(obj[i]);
  }
}

TEST_F(GetattrsFullLatencyTest, BIG_BYPASS_CACHED)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  struct attrlist outattrs;
  struct timespec s_time, e_time;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = sub_hdl->obj_ops.getattrs(sub_hdl, &outattrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per getattrs: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(GetattrsFullLatencyTest, BIG_BYPASS_UNCACHED)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl[LOOP_COUNT];
  struct attrlist outattrs;
  struct fsal_obj_handle *obj;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i % DIR_COUNT);

    status = test_root->obj_ops.lookup(test_root, fname, &obj, NULL);
    ASSERT_EQ(status.major, 0);
    ASSERT_NE(obj, nullptr);
    sub_hdl[i] = mdcdb_get_sub_handle(obj);
    obj->obj_ops.put_ref(obj);
  }

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = sub_hdl[i]->obj_ops.getattrs(sub_hdl[i], &outattrs);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per getattrs: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);
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
    gtest::env = new gtest::Environment(ganesha_conf, lpath, dlevel, session_name);
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
