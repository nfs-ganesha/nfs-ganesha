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

/* LTTng headers */
#include <lttng/lttng.h>

/* gperf headers */
#include <gperftools/profiler.h>
}

#define TEST_ROOT "lookup_latency"
#define DIR_COUNT 100000
#define LOOP_COUNT 1000000
#define NAMELEN 16

namespace {

  char* ganesha_conf = nullptr;
  char* lpath = nullptr;
  int dlevel = -1;
  uint16_t export_id = 77;
  static struct lttng_handle* handle = nullptr;
  char* event_list = nullptr;
  char* profile_out = nullptr;

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
    Environment() : Environment(NULL) {}
    Environment(char *_ses_name) : ganesha(ganesha_server), session_name(_ses_name) {
      using namespace std::literals;
      std::this_thread::sleep_for(5s);
    }

    virtual ~Environment() {
      admin_halt();
      ganesha.join();
    }

    virtual void SetUp() {
      struct lttng_domain dom;

      if (!session_name) {
	/* Don't setup LTTng */
	return;
      }

      /* Set up LTTng */
      memset(&dom, 0, sizeof(dom));
      dom.type = LTTNG_DOMAIN_UST;
      dom.buf_type = LTTNG_BUFFER_PER_UID;

      handle = lttng_create_handle(session_name, &dom);
    }

    virtual void TearDown() {
      if (handle) {
	lttng_destroy_handle(handle);
	handle = NULL;
      }
    }

    std::thread ganesha;
    char *session_name;
  };

  class GaneshaBaseTest : public ::testing::Test {
  protected:
    virtual void enableEvents(char *event_list) {
      struct lttng_event ev;
      int ret;

      if (!handle) {
	/* No LTTng this run */
	return;
      }

      memset(&ev, 0, sizeof(ev));
      ev.type = LTTNG_EVENT_TRACEPOINT;
      ev.loglevel_type = LTTNG_EVENT_LOGLEVEL_ALL;
      ev.loglevel = -1;

      if (!event_list) {
	/* Do them all */
	strcpy(ev.name, "*");
	ret = lttng_enable_event_with_exclusions(handle,
			    &ev, NULL, NULL, 0, NULL);
	ASSERT_GE(ret, 0);
      } else {
	char *event_name;
	event_name = strtok(event_list, ",");
	while (event_name != NULL) {
	  /* Copy name and type of the event */
	  strncpy(ev.name, event_name, LTTNG_SYMBOL_NAME_LEN);
	  ev.name[sizeof(ev.name) - 1] = '\0';

	  ret = lttng_enable_event_with_exclusions(handle,
				&ev, NULL, NULL, 0, NULL);
	  ASSERT_GE(ret, 0);

	  /* Next event */
	  event_name = strtok(NULL, ",");
	}
      }
    }

    virtual void disableEvents(char *event_list) {
      struct lttng_event ev;
      int ret;

      if (!handle) {
	/* No LTTng this run */
	return;
      }

      memset(&ev, 0, sizeof(ev));
      ev.type = LTTNG_EVENT_ALL;
      ev.loglevel = -1;
      if (!event_list) {
	/* Do them all */
	ret = lttng_disable_event_ext(handle, &ev, NULL, NULL);
	ASSERT_GE(ret, 0);
      } else {
	char *event_name;
	event_name = strtok(event_list, ",");
	while (event_name != NULL) {
	  /* Copy name and type of the event */
	  strncpy(ev.name, event_name, LTTNG_SYMBOL_NAME_LEN);
	  ev.name[sizeof(ev.name) - 1] = '\0';

	  ret = lttng_disable_event_ext(handle, &ev, NULL, NULL);
	  ASSERT_GE(ret, 0);

	  /* Next event */
	  event_name = strtok(NULL, ",");
	}
      }
    }

    virtual void SetUp() { }

    virtual void TearDown() { }
  };

  class LookupEmptyLatencyTest : public GaneshaBaseTest {
  protected:

    virtual void SetUp() {
      fsal_status_t status;
      struct attrlist attrs_out;

      GaneshaBaseTest::SetUp();

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

      status = test_root->obj_ops->unlink(root_entry, test_root, TEST_ROOT);
      EXPECT_EQ(0, status.major);
      test_root->obj_ops->put_ref(test_root);
      test_root = NULL;

      root_entry->obj_ops->put_ref(root_entry);
      root_entry = NULL;

      put_gsh_export(a_export);
      a_export = NULL;

      GaneshaBaseTest::TearDown();
    }

    struct req_op_context req_ctx;
    struct user_cred user_credentials;
    struct attrlist attrs;

    struct gsh_export* a_export = nullptr;
    struct fsal_obj_handle *root_entry = nullptr;
    struct fsal_obj_handle *test_root = nullptr;
  };

  class LookupFullLatencyTest : public LookupEmptyLatencyTest {

  private:
    static fsal_errors_t readdir_callback(void *opaque,
					  struct fsal_obj_handle *obj,
					  const struct attrlist *attr,
					  uint64_t mounted_on_fileid,
					  uint64_t cookie,
					  enum cb_state cb_state)
      {
      return ERR_FSAL_NO_ERROR;
      }

  protected:

    virtual void SetUp() {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_obj_handle *obj;
      struct attrlist attrs_out;
      unsigned int num_entries;
      bool eod_met;
      attrmask_t attrmask = 0;
      uint32_t tracker;

      LookupEmptyLatencyTest::SetUp();

      /* create a bunch of dirents */
      for (int i = 0; i < DIR_COUNT; ++i) {
	fsal_prepare_attrs(&attrs_out, 0);
	sprintf(fname, "d-%08x", i);

	status = fsal_create(test_root, fname, REGULAR_FILE, &attrs, NULL, &obj,
			     &attrs_out);
	ASSERT_EQ(status.major, 0);
	ASSERT_NE(obj, nullptr);

	fsal_release_attrs(&attrs_out);
	obj->obj_ops->put_ref(obj);
      }

      /* Prime the cache */
      status = fsal_readdir(test_root, 0, &num_entries, &eod_met, attrmask,
			    readdir_callback, &tracker);
    }

    virtual void TearDown() {
      fsal_status_t status;
      char fname[NAMELEN];

      for (int i = 0; i < DIR_COUNT; ++i) {
	sprintf(fname, "d-%08x", i);

	status = fsal_remove(test_root, fname);
	EXPECT_EQ(status.major, 0);
      }

      LookupEmptyLatencyTest::TearDown();
    }

  };

} /* namespace */

TEST_F(LookupEmptyLatencyTest, SIMPLE)
{
  fsal_status_t status;
  struct fsal_obj_handle *lookup;

  enableEvents(event_list);

  status = root_entry->obj_ops->lookup(root_entry, TEST_ROOT, &lookup, NULL);
  EXPECT_EQ(status.major, 0);
  EXPECT_EQ(test_root, lookup);

  disableEvents(event_list);

  lookup->obj_ops->put_ref(lookup);
}

TEST_F(LookupEmptyLatencyTest, SIMPLE_BYPASS)
{
  fsal_status_t status;
  struct fsal_obj_handle *sub_hdl;
  struct fsal_obj_handle *lookup;

  enableEvents(event_list);

  sub_hdl = mdcdb_get_sub_handle(root_entry);
  ASSERT_NE(sub_hdl, nullptr);
  status = sub_hdl->obj_ops->lookup(sub_hdl, TEST_ROOT, &lookup, NULL);
  EXPECT_EQ(status.major, 0);
  EXPECT_EQ(mdcdb_get_sub_handle(test_root), lookup);

  disableEvents(event_list);

  /* Lookup on sub-FSAL did not refcount, so no need to put it */
}

TEST_F(LookupEmptyLatencyTest, LOOP)
{
  fsal_status_t status;
  struct fsal_obj_handle *lookup;
  struct timespec s_time, e_time;

  enableEvents(event_list);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = root_entry->obj_ops->lookup(root_entry, TEST_ROOT, &lookup, NULL);
    EXPECT_EQ(status.major, 0);
    EXPECT_EQ(test_root, lookup);
  }

  now(&e_time);

  disableEvents(event_list);

  /* Have the put_ref()'s outside the latency loop */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    lookup->obj_ops->put_ref(lookup);
  }

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
	  timespec_diff(&s_time, &e_time) / LOOP_COUNT);

}

TEST_F(LookupEmptyLatencyTest, FSALLOOKUP)
{
  fsal_status_t status;
  struct fsal_obj_handle *lookup;
  struct timespec s_time, e_time;

  enableEvents(event_list);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    status = fsal_lookup(root_entry, TEST_ROOT, &lookup, NULL);
    EXPECT_EQ(status.major, 0);
    EXPECT_EQ(test_root, lookup);
  }

  now(&e_time);

  disableEvents(event_list);

  /* Have the put_ref()'s outside the latency loop */
  for (int i = 0; i < LOOP_COUNT; ++i) {
    lookup->obj_ops->put_ref(lookup);
  }

  fprintf(stderr, "Average time per fsal_lookup: %" PRIu64 " ns\n",
	  timespec_diff(&s_time, &e_time) / LOOP_COUNT);

}

TEST_F(LookupFullLatencyTest, BIG_SINGLE)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  enableEvents(event_list);

  now(&s_time);

  sprintf(fname, "d-%08x", DIR_COUNT / 5);

  status = test_root->obj_ops->lookup(test_root, fname, &obj, NULL);
  ASSERT_EQ(status.major, 0) << " failed to lookup " << fname;
  obj->obj_ops->put_ref(obj);

  now(&e_time);

  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
	  timespec_diff(&s_time, &e_time));
}

TEST_F(LookupFullLatencyTest, BIG)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  enableEvents(event_list);
  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i % DIR_COUNT);

    status = test_root->obj_ops->lookup(test_root, fname, &obj, NULL);
    ASSERT_EQ(status.major, 0) << " failed to lookup " << fname;
    obj->obj_ops->put_ref(obj);
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();
  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
	  timespec_diff(&s_time, &e_time) / LOOP_COUNT);
}

TEST_F(LookupFullLatencyTest, BIG_BYPASS)
{
  fsal_status_t status;
  char fname[NAMELEN];
  struct fsal_obj_handle *sub_hdl;
  struct fsal_obj_handle *obj;
  struct timespec s_time, e_time;

  sub_hdl = mdcdb_get_sub_handle(test_root);

  enableEvents(event_list);
  if (profile_out)
    ProfilerStart(profile_out);

  now(&s_time);

  for (int i = 0; i < LOOP_COUNT; ++i) {
    sprintf(fname, "d-%08x", i % DIR_COUNT);

    status = sub_hdl->obj_ops->lookup(sub_hdl, fname, &obj, NULL);
    ASSERT_EQ(status.major, 0) << " failed to lookup " << fname;
    /* Don't need to put_ref(obj) because sub-FSAL doesn't support refcounts */
  }

  now(&e_time);

  if (profile_out)
    ProfilerStop();
  disableEvents(event_list);

  fprintf(stderr, "Average time per lookup: %" PRIu64 " ns\n",
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
    ::testing::AddGlobalTestEnvironment(new Environment(session_name));

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
