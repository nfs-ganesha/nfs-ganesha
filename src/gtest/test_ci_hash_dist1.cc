// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2015 Red Hat, Inc.
 * Contributor : Matt Benjamin <mbenjamin@redhat.com>
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
/* Ganesha headers */
#include "nfs_lib.h"
#include "export_mgr.h"
#include "nfs_exports.h"
#include "sal_data.h"
#include "fsal.h"
}

namespace bf = boost::filesystem;

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;

  struct req_op_context req_ctx;
  struct user_cred user_credentials;
  struct attrlist object_attributes;

  struct gsh_export* a_export = nullptr;
  struct fsal_obj_handle *root_entry = nullptr;
  struct fsal_obj_handle *test_root = nullptr;

#if 0
  std::uniform_int_distribution<uint8_t> uint_dist;
  std::mt19937 rng;

  p->cksum = XXH64(p->data, 65536, 8675309);
#endif

  int ganesha_server() {
    /* XXX */
    return nfs_libmain(
      ganesha_conf,
      lpath,
      dlevel
      );
  }

} /* namespace */

TEST(CI_HASH_DIST1, INIT)
{
  fsal_status_t status;

  a_export = get_gsh_export(export_id);
  ASSERT_NE(a_export, nullptr);

  status = nfs_export_get_root_entry(a_export, &root_entry);
  ASSERT_NE(root_entry, nullptr);

  /* Ganesha call paths need real or forged context info */
  memset(&user_credentials, 0, sizeof(struct user_cred));
  memset(&req_ctx, 0, sizeof(struct req_op_context));
  memset(&object_attributes, 0, sizeof(object_attributes));

  req_ctx.ctx_export = a_export;
  req_ctx.fsal_export = a_export->fsal_export;
  req_ctx.creds = &user_credentials;

  /* stashed in tls */
  op_ctx = &req_ctx;
}

TEST(CI_HASH_DIST1, CREATE_ROOT)
{
  fsal_status_t status;
  struct attrlist *attrs_out = nullptr;

  // create root directory for test
  FSAL_SET_MASK(object_attributes.request_mask,
		ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
  object_attributes.mode = 777; /* XXX */
  object_attributes.owner = 667;
  object_attributes.group = 766;

  status = root_entry->obj_ops->mkdir(root_entry, "ci_hash_dist1",
				    &object_attributes, &test_root,
				    attrs_out);
  ASSERT_NE(test_root, nullptr);
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
    po::store(po::parse_command_line(argc, argv, opts), vm);
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

    std::thread ganesha(ganesha_server);
    std::this_thread::sleep_for(5s);

    code  = RUN_ALL_TESTS();
    ganesha.join();
  }

  catch(po::error& e) {
    cout << "Error parsing opts " << e.what() << endl;
  }

  catch(...) {
    cout << "Unhandled exception in main()" << endl;
  }

  return code;
}
