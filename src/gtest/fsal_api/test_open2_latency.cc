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

#define TEST_ROOT "open2_latency"
#define TEST_FILE "open2_latency_file"
#define LOOP_COUNT 100000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  class Open2EmptyLatencyTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      gtest::GaneshaFSALBaseTest::SetUp();

      fsal_prepare_attrs(&attrs_in, 0);
    }

    virtual void TearDown() {
      fsal_release_attrs(&attrs_in);

      gtest::GaneshaFSALBaseTest::TearDown();
    }

    struct attrlist attrs_in;
  };

  class Open2LoopLatencyTest : public Open2EmptyLatencyTest {
  protected:

    virtual void SetUp() {
      Open2EmptyLatencyTest::SetUp();

      for (int i = 0; i < LOOP_COUNT; ++i) {
	file_state[i] = op_ctx->fsal_export->exp_ops.alloc_state(
                                               op_ctx->fsal_export,
                                               STATE_TYPE_SHARE,
                                               NULL);
	ASSERT_NE(file_state[i], nullptr);
      }
    }

    virtual void TearDown() {
      for (int i = 0; i < LOOP_COUNT; ++i) {
	op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export,
						file_state[i]);
      }

      Open2EmptyLatencyTest::TearDown();
    }

    struct fsal_obj_handle *obj[LOOP_COUNT];
    struct state_t *file_state[LOOP_COUNT];
  };

} /* namespace */

TEST_F(Open2EmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct fsal_obj_handle *obj;
  bool caller_perm_check = false;
  struct state_t *file_state;

  file_state = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state, nullptr);

  // create and open a file for test
  status = test_root->obj_ops->open2(test_root, file_state, FSAL_O_RDWR,
             FSAL_UNCHECKED, TEST_FILE, &attrs_in, NULL, &obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  // close and  delete the file created for test
  status = obj->obj_ops->close2(obj, file_state);
  EXPECT_EQ(status.major, 0);

  status = fsal_remove(test_root, TEST_FILE);
  ASSERT_EQ(status.major, 0);
  obj->obj_ops->put_ref(obj);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state);
}

TEST_F(Open2EmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *obj;
  bool caller_perm_check = false;
  struct state_t *file_state;
  struct fsal_obj_handle *sub_hdl;

  file_state = op_ctx->fsal_export->exp_ops.alloc_state(op_ctx->fsal_export,
							STATE_TYPE_SHARE,
							NULL);
  ASSERT_NE(file_state, nullptr);

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  status = nfs_export_get_root_entry(a_export, &sub_hdl);
  ASSERT_EQ(status.major, 0);

  // create and open a file for test
  status = sub_hdl->obj_ops->open2(sub_hdl, file_state, FSAL_O_RDWR,
             FSAL_UNCHECKED, TEST_FILE, &attrs_in, NULL, &obj, NULL,
	     &caller_perm_check);
  ASSERT_EQ(status.major, 0);

  // close and delete the file created for test
  status = obj->obj_ops->close2(obj, file_state);
  EXPECT_EQ(status.major, 0);

  status = fsal_remove(sub_hdl, TEST_FILE);
  ASSERT_EQ(status.major, 0);
  obj->obj_ops->put_ref(obj);
  op_ctx->fsal_export->exp_ops.free_state(op_ctx->fsal_export, file_state);
}

TEST_F(Open2LoopLatencyTest, FSAL_OPEN2)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct timespec s_time, e_time;

  now(&s_time);

  // create and open a files for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = fsal_open2(test_root, file_state[i], FSAL_O_RDWR, FSAL_UNCHECKED,
               fname, &attrs_in, NULL, &obj[i], NULL);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per fsal_open2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  // close and delete the files created for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = fsal_close(obj[i]);
    EXPECT_EQ(status.major, 0);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
    obj[i]->obj_ops->put_ref(obj[i]);
  }
}

TEST_F(Open2LoopLatencyTest, LOOP)
{
  fsal_status_t status;
  char fname[NAMELEN];
  bool caller_perm_check = false;
  struct timespec s_time, e_time;

  now(&s_time);

  // create and open a files for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = test_root->obj_ops->open2(test_root, file_state[i], FSAL_O_RDWR,
               FSAL_UNCHECKED, fname, &attrs_in, NULL, &obj[i], NULL,
               &caller_perm_check);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per open2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  // close and delete the files created for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = obj[i]->obj_ops->close2(obj[i], file_state[i]);
    EXPECT_EQ(status.major, 0);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
    obj[i]->obj_ops->put_ref(obj[i]);
  }
}

TEST_F(Open2LoopLatencyTest, LOOP_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  char fname[NAMELEN];
  bool caller_perm_check = false;
  struct timespec s_time, e_time;

  sub_hdl = mdcdb_get_sub_handle(test_root);
  ASSERT_NE(sub_hdl, nullptr);

  status = nfs_export_get_root_entry(a_export, &sub_hdl);
  ASSERT_EQ(status.major, 0);

  now(&s_time);

  // create and open a files for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = sub_hdl->obj_ops->open2(sub_hdl, file_state[i], FSAL_O_RDWR,
               FSAL_UNCHECKED, fname, &attrs_in, NULL, &obj[i], NULL,
               &caller_perm_check);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per open2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  // close and delete the files created for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = obj[i]->obj_ops->close2(obj[i], file_state[i]);
    EXPECT_EQ(status.major, 0);

    status = fsal_remove(sub_hdl, fname);
    ASSERT_EQ(status.major, 0);
    obj[i]->obj_ops->put_ref(obj[i]);
  }
}

TEST_F(Open2LoopLatencyTest, OPEN_ONLY)
{
  fsal_status_t status;
  char fname[NAMELEN];
  bool caller_perm_check = false;
  struct timespec s_time, e_time;

  // create files for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = fsal_create(root_entry, fname, REGULAR_FILE, &attrs, NULL, &obj[i],
               NULL);
    ASSERT_EQ(status.major, 0);
    ASSERT_NE(obj[i], nullptr);
    obj[i]->obj_ops->put_ref(obj[i]);
  }

  now(&s_time);

  // open a files for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = test_root->obj_ops->open2(test_root, file_state[i], FSAL_O_RDWR,
               FSAL_UNCHECKED, fname, &attrs_in, NULL, &obj[i], NULL,
               &caller_perm_check);
    ASSERT_EQ(status.major, 0);
  }

  now(&e_time);

  fprintf(stderr, "Average time per open2: %" PRIu64 " ns\n",
          timespec_diff(&s_time, &e_time) / LOOP_COUNT);

  // close and delete the files created for test
  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "f-%08x", i);

    status = obj[i]->obj_ops->close2(obj[i], file_state[i]);
    EXPECT_EQ(status.major, 0);

    status = fsal_remove(test_root, fname);
    ASSERT_EQ(status.major, 0);
    obj[i]->obj_ops->put_ref(obj[i]);
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
