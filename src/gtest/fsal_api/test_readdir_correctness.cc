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

#define TEST_ROOT "readdir_correctness"
#define TEST_DIR "test_directory"
#define DIR_COUNT 100000

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  char* event_list = nullptr;
  char* profile_out = nullptr;

  typedef struct {
    struct gsh_buffdesc *keys;
    bool *hdl_found;
    std::string *names;
  } rd_state_t;

  class ReaddirEmptyCorrectnessTest : public gtest::GaneshaFSALBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;

      gtest::GaneshaFSALBaseTest::SetUp();

      status = fsal_create(test_root, TEST_DIR, DIRECTORY, &attrs, NULL,
		      &test_dir, &attrs_out);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(test_dir, nullptr);

      fsal_release_attrs(&attrs_out);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = test_root->obj_ops->unlink(test_root, test_dir, TEST_DIR);
      EXPECT_EQ(0, status.major);
      test_dir->obj_ops->put_ref(test_dir);
      test_dir = NULL;

      gtest::GaneshaFSALBaseTest::TearDown();
    }

    struct fsal_obj_handle *test_dir = nullptr;
  };

  class ReaddirFullCorrectnessTest : public ReaddirEmptyCorrectnessTest {
  protected:

    virtual void SetUp() {
      struct fsal_obj_handle *dir_hdls[DIR_COUNT];

      ReaddirEmptyCorrectnessTest::SetUp();

      create_and_prime_many(DIR_COUNT, dir_hdls, test_dir);

      for (int i = 0; i < DIR_COUNT; i++) {
	struct gsh_buffdesc fh_desc;
	std::stringstream sstr;

	dir_hdls[i]->obj_ops->handle_to_key(dir_hdls[i], &fh_desc);
	keyDup(&keys[i], &fh_desc);
	sstr << "f-" << std::setfill('0') << std::setw(8) << std::hex << i;
	names[i] = sstr.str();
	sstr.str("");
	sstr.clear();

	dir_hdls[i]->obj_ops->put_ref(dir_hdls[i]);
      }

      /* Clean up extra entries */
      mdcache_lru_release_entries(-1);
    }

    virtual void TearDown() {
      remove_many(DIR_COUNT, NULL, test_dir);

      for (int i = 0; i < DIR_COUNT; i++) {
	gsh_free(keys[i].addr);
      }

      ReaddirEmptyCorrectnessTest::TearDown();
    }

    void keyDup(struct gsh_buffdesc *dest, struct gsh_buffdesc *src) {
      dest->len = src->len;
      dest->addr = gsh_malloc(src->len);
      memcpy(dest->addr, src->addr, src->len);
    }

    struct gsh_buffdesc keys[DIR_COUNT];
    std::string names[DIR_COUNT];
    bool hdl_found[DIR_COUNT] = { false };

  };

  bool keyEQ(struct gsh_buffdesc *key1, struct gsh_buffdesc *key2) {
    if (key1->len != key2->len)
      return false;
    return !memcmp(key1->addr, key2->addr, key1->len);
  }

  enum fsal_dir_result trc_populate_dirent(const char *name,
      struct fsal_obj_handle *obj, struct attrlist *attrs, void *dir_state,
      fsal_cookie_t cookie) {
    rd_state_t *st = (rd_state_t*)dir_state;
    struct gsh_buffdesc fh_desc;

    obj->obj_ops->handle_to_key(obj, &fh_desc);

    for (int i = 0; i < DIR_COUNT; i++) {
      if (keyEQ(&st->keys[i], &fh_desc)) {
	EXPECT_EQ(false, st->hdl_found[i]) << st->names[i];
	st->hdl_found[i] = true;
	break;
      }
    }
    obj->obj_ops->put_ref(obj);
    return DIR_CONTINUE;
  }

  struct cb_data {
        u8 *cursor;
        unsigned int count;
        unsigned int max;
  };

} /* namespace */

TEST_F(ReaddirFullCorrectnessTest, BIG)
{
  fsal_status_t status;
  uint64_t whence = 0;
  bool eod = false;
  rd_state_t st;

  st.keys = keys;
  st.hdl_found = hdl_found;
  st.names = names;

  status = test_dir->obj_ops->readdir(test_dir, &whence, &st, trc_populate_dirent, 0, &eod);
  ASSERT_EQ(status.major, 0);

  for (int i=0; i < DIR_COUNT; i++) {
    ASSERT_EQ(true, hdl_found[i]) << names[i];
    hdl_found[i] = false;
  }

  status = test_dir->obj_ops->readdir(test_dir, &whence, &st, trc_populate_dirent, 0, &eod);
  ASSERT_EQ(status.major, 0);

  for (int i=0; i < DIR_COUNT; i++) {
    ASSERT_EQ(true, hdl_found[i]) << names[i];
    hdl_found[i] = false;
  }
}

#if 0
/* bypass won't work right now, because of the obj pointer comparisons */
TEST_F(ReaddirFullCorrectnessTest, BIG_BYPASS)
{
  fsal_status_t status;
  uint64_t whence = 0;
  bool eod = false;
  struct fsal_obj_handle *sub_hdl;
  rd_state_t st;

  st.dir_hdls = dir_hdls;
  st.hdl_found = hdl_found;

  sub_hdl = mdcdb_get_sub_handle(test_dir);
  ASSERT_NE(sub_hdl, nullptr);

  status = sub_hdl->obj_ops->readdir(sub_hdl, &whence, &st, trc_populate_dirent, 0, &eod);
  ASSERT_EQ(status.major, 0);

  for (int i = 0; i < DIR_COUNT; i++) {
    ASSERT_EQ(true, hdl_found[i]);
  }
}
#endif

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
