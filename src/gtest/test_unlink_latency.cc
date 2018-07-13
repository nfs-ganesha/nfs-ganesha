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
#include "gtest/gtest.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/program_options.hpp>

extern "C" {
/* Manually forward this, as 9P is not C++ safe */
void admin_halt(void);
/* Ganesha headers */
#include "nfs_lib.h"
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
#define NAMELEN 16

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;

  int ganesha_server() {
    /* XXX */
    return nfs_libmain(
      ganesha_conf,
      lpath,
      dlevel
      );
  }

  class Environment : public ::testing::Environment {
  public:
    Environment() : ganesha(ganesha_server) {
      using namespace std::literals;
      std::this_thread::sleep_for(5s);
    }

    virtual ~Environment() {
      admin_halt();
      ganesha.join();
    }

    virtual void SetUp() { }

    virtual void TearDown() {
    }

    std::thread ganesha;
  };

  class UnlinkEmptyLatencyTest : public ::testing::Test {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;

      a_export = get_gsh_export(export_id);
      ASSERT_NE(a_export, nullptr);

      status = nfs_export_get_root_entry(a_export, &root_entry);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(root_entry, nullptr);

      /* Ganesha call paths need real or forged context info */
      memset(&user_credentials, 0, sizeof(struct user_cred));
      memset(&req_ctx, 0, sizeof(struct req_op_context));
      memset(&attrs, 0, sizeof(attrs));

      req_ctx.ctx_export = a_export;
      req_ctx.fsal_export = a_export->fsal_export;
      req_ctx.creds = &user_credentials;

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

      status = fsal_remove(root_entry, TEST_ROOT);
      EXPECT_EQ(status.major, 0);
      test_root->obj_ops->put_ref(test_root);
      test_root = NULL;

      root_entry->obj_ops->put_ref(root_entry);
      root_entry = NULL;

      put_gsh_export(a_export);
      a_export = NULL;
    }

    struct req_op_context req_ctx;
    struct user_cred user_credentials;
    struct attrlist attrs;

    struct gsh_export* a_export = nullptr;
    struct fsal_obj_handle *root_entry = nullptr;
    struct fsal_obj_handle *test_root = nullptr;
  };

  class UnlinkFullLatencyTest : public UnlinkEmptyLatencyTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_obj_handle *obj;
      struct attrlist attrs_out;

      UnlinkEmptyLatencyTest::SetUp();

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
  struct fsal_obj_handle *obj;
  struct fsal_obj_handle *sub_hdl = nullptr;
  struct fsal_obj_handle *sub_hdl_obj = nullptr;
  struct fsal_obj_handle *lookup = nullptr;

  /* Create files to unlink for the test */
  status = fsal_create(test_root, TEST_FILE, REGULAR_FILE, &attrs, NULL,
                  &obj, NULL);
  ASSERT_EQ(status.major, 0);
  ASSERT_NE(obj, nullptr);

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  sub_hdl_obj = mdcdb_get_sub_handle(obj);
  ASSERT_NE(sub_hdl_obj, nullptr);

  status = sub_hdl->obj_ops->unlink(sub_hdl, sub_hdl_obj, TEST_FILE);
  EXPECT_EQ(status.major, 0);

  status = sub_hdl->obj_ops->lookup(sub_hdl, TEST_FILE, &lookup, NULL);
  EXPECT_EQ(status.major, ERR_FSAL_NOENT);
  EXPECT_EQ(lookup, nullptr);

  obj->obj_ops->put_ref(obj);
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


  obj->obj_ops->put_ref(obj);

  /* Remove file created for running the test */
  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);
}

TEST_F(UnlinkFullLatencyTest, BIG_BYPASS)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct fsal_obj_handle *sub_hdl;
  struct fsal_obj_handle *sub_hdl_obj;
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

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  sub_hdl_obj = mdcdb_get_sub_handle(obj);
  ASSERT_NE(sub_hdl_obj, nullptr);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "fl-%08x", i);

    status = sub_hdl->obj_ops->unlink(sub_hdl, sub_hdl_obj, fname);
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

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new Environment);

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
