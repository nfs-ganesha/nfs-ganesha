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

#define TEST_ROOT "read2_latency"
#define TEST_FILE "read2_latency_file"
#define LOOP_COUNT 1000000
#define OFFSET 0

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class Read2EmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      bool caller_perm_check = false;

      gtest::GaneshaFSALBaseTest::SetUp();

      test_file_state = op_ctx->fsal_export->exp_ops.alloc_state(
						op_ctx->fsal_export,
						STATE_TYPE_SHARE,
						NULL);
      ASSERT_NE(test_file_state, nullptr);

      status = test_root->obj_ops->open2(test_root, test_file_state,
                      FSAL_O_RDWR, FSAL_UNCHECKED, TEST_FILE, &attrs, NULL,
                      &test_file, NULL, &caller_perm_check);
      ASSERT_EQ(status.major, 0);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = test_file->obj_ops->close2(test_file, test_file_state);
      EXPECT_EQ(0, status.major);

      op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export,
					      test_file_state);

      status = fsal_remove(test_root, TEST_FILE);
      EXPECT_EQ(status.major, 0);
      test_file->obj_ops->put_ref(test_file);
      test_file = NULL;

      gtest::GaneshaFSALBaseTest::TearDown();
    }

    struct fsal_obj_handle *test_file = nullptr;
    struct state_t *test_file_state;
  };

  static void callback(struct fsal_obj_handle *obj, fsal_status_t ret,
                        void *data, void *caller_data)
  {
    /* Fixup ERR_FSAL_SHARE_DENIED status */
    if (ret.major == ERR_FSAL_SHARE_DENIED)
      ret = fsalstat(ERR_FSAL_LOCKED, 0);

    EXPECT_EQ(ret.major, 0);
  }

} /* namespace */

TEST_F(Read2EmptyLatencyTest, SIMPLE)
{
  char *w_databuffer;
  char *r_databuffer;
  struct fsal_io_arg *write_arg;
  struct fsal_io_arg *read_arg;
  int bytes = 64;
  int ret = -1;

  w_databuffer = (char *) malloc(bytes);
  memset(w_databuffer, 'a', bytes);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  test_file->obj_ops->write2(test_file, true, callback, write_arg, NULL);

  r_databuffer = (char *) malloc(bytes);
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  test_file->obj_ops->read2(test_file, true, callback, read_arg, NULL);

  ret = memcmp(r_databuffer, w_databuffer, bytes);
  EXPECT_EQ(ret, 0);

  free(w_databuffer);
  free(r_databuffer);
}

TEST_F(Read2EmptyLatencyTest, SIMPLE_BYPASS)
{
  struct fsal_obj_handle *sub_hdl;
  char *w_databuffer;
  char *r_databuffer;
  struct fsal_io_arg *write_arg;
  struct fsal_io_arg *read_arg;
  int bytes = 64;

  w_databuffer = (char *) malloc(bytes);
  memset(w_databuffer, 'a', bytes);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  sub_hdl = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_hdl, nullptr);

  sub_hdl->obj_ops->write2(sub_hdl, true, callback, write_arg, NULL);

  r_databuffer = (char *) malloc(bytes);

  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  sub_hdl->obj_ops->read2(sub_hdl, true, callback, read_arg, NULL);

  free(w_databuffer);
  free(r_databuffer);
}

TEST_F(Read2EmptyLatencyTest, LARGE_DATA_READ)
{
  char *w_databuffer;
  char *r_databuffer;
  struct fsal_io_arg *write_arg;
  struct fsal_io_arg *read_arg;
  int bytes = (2*1024*1024);
  int ret = -1;

  w_databuffer = (char *) malloc(bytes);
  memset(w_databuffer, 'a', bytes);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  test_file->obj_ops->write2(test_file, true, callback, write_arg, NULL);

  r_databuffer = (char *) malloc(bytes);
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  test_file->obj_ops->read2(test_file, true, callback, read_arg, NULL);

  ret = memcmp(r_databuffer, w_databuffer, bytes);
  EXPECT_EQ(ret, 0);

  free(w_databuffer);
  free(r_databuffer);
}

TEST_F(Read2EmptyLatencyTest, LOOP)
{
  char *w_databuffer;
  char *r_databuffer;
  struct fsal_io_arg *write_arg;
  struct fsal_io_arg *read_arg;
  int bytes = 64*LOOP_COUNT;
  struct timespec s_time, e_time;

  w_databuffer = (char *) malloc(bytes);
  memset(w_databuffer, 'a', bytes);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					  sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  test_file->obj_ops->write2(test_file, true, callback, write_arg, NULL);

  bytes = 64;
  r_databuffer = (char *) malloc(bytes);
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
					 sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i, read_arg->offset += 64) {
    test_file->obj_ops->read2(test_file, true, callback, read_arg, NULL);
  }

  now(&e_time);

  fprintf(stderr, "Average time per read2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  free(w_databuffer);
  free(r_databuffer);
}

TEST_F(Read2EmptyLatencyTest, LOOP_BYPASS)
{
  struct fsal_obj_handle *sub_hdl;
  char *w_databuffer;
  char *r_databuffer;
  struct fsal_io_arg *write_arg;
  struct fsal_io_arg *read_arg;
  int bytes = 64*LOOP_COUNT;
  struct timespec s_time, e_time;

  w_databuffer = (char *) malloc(bytes);
  memset(w_databuffer, 'a', bytes);

  write_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
                                          sizeof(struct iovec));
  write_arg->info = NULL;
  write_arg->state = NULL;
  write_arg->offset = OFFSET;
  write_arg->iov_count = 1;
  write_arg->iov[0].iov_len = bytes;
  write_arg->iov[0].iov_base = w_databuffer;
  write_arg->io_amount = 0;
  write_arg->fsal_stable = false;

  sub_hdl = mdcdb_get_sub_handle(test_file);
  ASSERT_NE(sub_hdl, nullptr);

  sub_hdl->obj_ops->write2(sub_hdl, true, callback, write_arg, NULL);

  bytes = 64;
  r_databuffer = (char *) malloc(bytes);
  read_arg = (struct fsal_io_arg*)alloca(sizeof(struct fsal_io_arg) +
                                         sizeof(struct iovec));
  read_arg->info = NULL;
  read_arg->state = NULL;
  read_arg->offset = OFFSET;
  read_arg->iov_count = 1;
  read_arg->iov[0].iov_len = bytes;
  read_arg->iov[0].iov_base = r_databuffer;
  read_arg->io_amount = 0;

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i, read_arg->offset += 64) {
    sub_hdl->obj_ops->read2(sub_hdl, true, callback, read_arg, NULL);
  }

  now(&e_time);

  fprintf(stderr, "Average time per read2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  free(w_databuffer);
  free(r_databuffer);
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
