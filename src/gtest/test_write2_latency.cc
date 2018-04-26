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
#include <string.h>
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
/* Manually forward this, an 9P is not C++ safe */
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
/* #include "../FSAL/Stackable_FSALs/FSAL_MDCACHE/mdcache_file.c" */
}

#define TEST_ROOT "write2_latency"
#define TEST_FILE "test_file"
#define LOOP_COUNT 1000000
#define OFFSET 0

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

  class Write2EmptyLatencyTest : public ::testing::Test {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;
      bool caller_perm_check = false;

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

      status = test_root->obj_ops.open2(test_root, &test_file_state,
                      FSAL_O_RDWR, FSAL_UNCHECKED, TEST_FILE, NULL, NULL,
                      &test_file, NULL, &caller_perm_check);
      ASSERT_EQ(status.major, 0);

      fsal_release_attrs(&attrs_out);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = test_file->obj_ops.close2(test_file, &test_file_state);
      EXPECT_EQ(0, status.major);

      status = fsal_remove(test_root, TEST_FILE);
      EXPECT_EQ(status.major, 0);
      test_file->obj_ops.put_ref(test_file);
      test_file = NULL;

      status = test_root->obj_ops.unlink(root_entry, test_root, TEST_ROOT);
      EXPECT_EQ(0, status.major);
      test_root->obj_ops.put_ref(test_root);
      test_root = NULL;

      root_entry->obj_ops.put_ref(root_entry);
      root_entry = NULL;

      put_gsh_export(a_export);
      a_export = NULL;
    }

    struct req_op_context req_ctx;
    struct user_cred user_credentials;
    struct attrlist attrs;
    struct export_perms exp_perms;

    struct gsh_export* a_export = nullptr;
    struct fsal_obj_handle *root_entry = nullptr;
    struct fsal_obj_handle *test_root = nullptr;
    struct fsal_obj_handle *test_file = nullptr;
    struct state_t test_file_state;
  };

  static void write_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
                        void *write_data, void *caller_data)
  {
    /* Fixup ERR_FSAL_SHARE_DENIED status */
    if (ret.major == ERR_FSAL_SHARE_DENIED)
      ret = fsalstat(ERR_FSAL_LOCKED, 0);

    EXPECT_EQ(ret.major, 0);
  }

} /* namespace */

TEST_F(Write2EmptyLatencyTest, SIMPLE)
{
  char *databuffer;
  struct fsal_io_arg write_arg;
  int bytes = 64;
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = false;

  test_file->obj_ops.write2(test_file, true, write_cb, &write_arg, NULL);

  free(databuffer);
}

TEST_F(Write2EmptyLatencyTest, SIMPLE_BYPASS)
{
  struct fsal_obj_handle *sub_hdl;
  char *databuffer;
  struct fsal_io_arg write_arg;
  int bytes = 64;
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = false;

  sub_hdl = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_hdl, nullptr);

  sub_hdl->obj_ops.write2(sub_hdl, true, write_cb, &write_arg, NULL);

  free(databuffer);
}

TEST_F(Write2EmptyLatencyTest, SMALL_STABLE_WRITE)
{
  char *databuffer;
  struct fsal_io_arg write_arg;
  int bytes = 64;
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = true;

  test_file->obj_ops.write2(test_file, true, write_cb, &write_arg, NULL);

  free(databuffer);
}

TEST_F(Write2EmptyLatencyTest, LARGE_UNSTABLE_WRITE)
{
  char *databuffer;
  struct fsal_io_arg write_arg;
  int bytes = (2*1024*1024);
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = false;

  test_file->obj_ops.write2(test_file, true, write_cb, &write_arg, NULL);

  free(databuffer);
}

TEST_F(Write2EmptyLatencyTest, LARGE_STABLE_WRITE)
{
  char *databuffer;
  struct fsal_io_arg write_arg;
  int bytes = (2*1024*1024);
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = true;

  test_file->obj_ops.write2(test_file, true, write_cb, &write_arg, NULL);

  free(databuffer);
}

TEST_F(Write2EmptyLatencyTest, LOOP)
{
  char *databuffer;
  struct fsal_io_arg write_arg;
  struct timespec s_time, e_time;
  int bytes = 64;
  databuffer = (char *) malloc(bytes);

  memset(databuffer, 'a', bytes);

  write_arg.info = NULL;
  write_arg.state = NULL;
  write_arg.offset = OFFSET;
  write_arg.iov_count = 1;
  write_arg.iov[0].iov_len = bytes;
  write_arg.iov[0].iov_base = databuffer;
  write_arg.io_amount = 0;
  write_arg.fsal_stable = false;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i, write_arg.offset += 64) {
    test_file->obj_ops.write2(test_file, true, write_cb, &write_arg, NULL);
  }

  now(&e_time);

  fprintf(stderr, "Average time per write2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  free(databuffer);
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
