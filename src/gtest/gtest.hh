// SPDX-License-Identifier: LGPL-3.0-or-later
// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (C) 2018 Red Hat, Inc.
 * Contributor : Daniel Gryniewicz <dang@redhat.com>
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

#include "gtest/gtest.h"

extern "C" {
/* Manually forward this, an 9P is not C++ safe */
void admin_halt(void);
/* Don't include rpcent.h; it has C++ issues, and is unneeded */
#define _RPC_RPCENT_H
/* Ganesha headers */
#include "nfs_lib.h"
#include "fsal.h"
#include "export_mgr.h"
#include "nfs_exports.h"

/* LTTng headers */
#include <lttng/lttng.h>

/* gperf headers */
#include <gperftools/profiler.h>
}

#ifndef GTEST_GTEST_HH
#define GTEST_GTEST_HH

#define NAMELEN 24

#define gtws_subcall(call) do { \
	struct fsal_export *_saveexp = op_ctx->fsal_export; \
	op_ctx->fsal_export = _saveexp->sub_export; \
	call; \
	op_ctx->fsal_export = _saveexp; \
} while (0)

namespace gtest {

  class Environment* env;

  class Environment : public ::testing::Environment {
  public:
    Environment() : Environment(NULL, NULL, -1, NULL) {}
    Environment(char* ganesha_conf, char* lpath, int dlevel, char *_ses_name,
                const char *_test_root_name = nullptr,
                uint16_t _export_id = 77) :
	    ganesha(nfs_libmain, ganesha_conf, lpath, dlevel),
	    session_name(_ses_name), test_root_name(_test_root_name),
	    export_id(_export_id), handle(NULL) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
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

    struct lttng_handle* getLTTng() {
      return handle;
    }

    const char *get_test_root_name() {
      return test_root_name;
    }

    uint16_t get_export_id() {
      return export_id;
    }

    std::thread ganesha;
    char *session_name;
    const char *test_root_name;
    uint16_t export_id;
    struct lttng_handle* handle;
  };

  class GaneshaBaseTest : public ::testing::Test {
  protected:
    virtual void enableEvents(char *event_list) {
      struct lttng_event ev;
      int ret;

      if (!env->getLTTng()) {
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
	ret = lttng_enable_event_with_exclusions(env->getLTTng(), &ev, NULL,
						 NULL, 0, NULL);
	ASSERT_GE(ret, 0);
      } else {
	char *event_name;
	event_name = strtok(event_list, ",");
	while (event_name != NULL) {
	  /* Copy name and type of the event */
	  strncpy(ev.name, event_name, LTTNG_SYMBOL_NAME_LEN);
	  ev.name[sizeof(ev.name) - 1] = '\0';

	  ret = lttng_enable_event_with_exclusions(env->getLTTng(), &ev, NULL,
						   NULL, 0, NULL);
	  ASSERT_GE(ret, 0);

	  /* Next event */
	  event_name = strtok(NULL, ",");
	}
      }
    }

    virtual void disableEvents(char *event_list) {
      struct lttng_event ev;
      int ret;

      if (!env->getLTTng()) {
	/* No LTTng this run */
	return;
      }

      memset(&ev, 0, sizeof(ev));
      ev.type = LTTNG_EVENT_ALL;
      ev.loglevel = -1;
      if (!event_list) {
	/* Do them all */
	ret = lttng_disable_event_ext(env->getLTTng(), &ev, NULL, NULL);
	ASSERT_GE(ret, 0);
      } else {
	char *event_name;
	event_name = strtok(event_list, ",");
	while (event_name != NULL) {
	  /* Copy name and type of the event */
	  strncpy(ev.name, event_name, LTTNG_SYMBOL_NAME_LEN);
	  ev.name[sizeof(ev.name) - 1] = '\0';

	  ret = lttng_disable_event_ext(env->getLTTng(), &ev, NULL, NULL);
	  ASSERT_GE(ret, 0);

	  /* Next event */
	  event_name = strtok(NULL, ",");
	}
      }
    }

    virtual void SetUp() { }

    virtual void TearDown() { }
  };

  class GaneshaFSALBaseTest : public gtest::GaneshaBaseTest {

  protected:
    static fsal_errors_t readdir_callback(void *opaque,
                                          struct fsal_obj_handle *obj,
                                          const struct fsal_attrlist *attr,
                                          uint64_t mounted_on_fileid,
                                          uint64_t cookie,
                                          enum cb_state cb_state)
      {
      return ERR_FSAL_NO_ERROR;
      }

    virtual void SetUp() {
      fsal_status_t status;
      struct fsal_attrlist attrs_out;

      gtest::GaneshaBaseTest::SetUp();

      a_export = get_gsh_export(env->get_export_id());
      ASSERT_NE(a_export, nullptr);

      status = nfs_export_get_root_entry(a_export, &root_entry);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(root_entry, nullptr);

      /* Ganesha call paths need real or forged context info */
      init_op_context_simple(&op_context, a_export, a_export->fsal_export);

      // create root directory for test
      FSAL_SET_MASK(attrs.valid_mask,
                    ATTR_MODE | ATTR_OWNER | ATTR_GROUP);
      attrs.mode = 0777; /* XXX */
      attrs.owner = 667;
      attrs.group = 766;
      fsal_prepare_attrs(&attrs_out, 0);

      status = fsal_create(root_entry, env->get_test_root_name(), DIRECTORY,
                           &attrs, NULL, &test_root, &attrs_out);
      ASSERT_EQ(status.major, 0);
      ASSERT_NE(test_root, nullptr);

      fsal_release_attrs(&attrs_out);
    }

    virtual void TearDown() {
      fsal_status_t status;

      status = test_root->obj_ops->unlink(root_entry, test_root,
                                         env->get_test_root_name());
      EXPECT_EQ(0, status.major);
      test_root->obj_ops->put_ref(test_root);
      test_root = NULL;

      root_entry->obj_ops->put_ref(root_entry);
      root_entry = NULL;

      a_export = NULL;
      release_op_context();

      gtest::GaneshaBaseTest::TearDown();
    }

    void create_and_prime_many(int count,
                               struct fsal_obj_handle **objs = NULL,
			       struct fsal_obj_handle *directory = NULL) {
      fsal_status_t status;
      char fname[NAMELEN];
      struct fsal_attrlist attrs_out;
      unsigned int num_entries;
      bool eod_met;
      attrmask_t attrmask = 0;
      uint32_t tracker;
      struct fsal_obj_handle *obj;

      if (directory == NULL)
	directory = test_root;

      /* create a bunch of dirents */
      for (int i = 0; i < count; ++i) {
        fsal_prepare_attrs(&attrs_out, 0);
        sprintf(fname, "f-%08x", i);

        status = fsal_create(directory, fname, REGULAR_FILE, &attrs, NULL,
                             &obj, &attrs_out);
        ASSERT_EQ(status.major, 0);
        ASSERT_NE(obj, nullptr);

        fsal_release_attrs(&attrs_out);

        if (objs != NULL)
          objs[i] = obj;
        else
          obj->obj_ops->put_ref(obj);
      }

      /* Prime the cache */
      status = fsal_readdir(directory, 0, &num_entries, &eod_met, attrmask,
                            readdir_callback, &tracker);
    }

    void remove_many(int count, struct fsal_obj_handle **objs = NULL,
		     struct fsal_obj_handle *directory = NULL) {
      fsal_status_t status;
      char fname[NAMELEN];

      if (directory == NULL)
	directory = test_root;

      for (int i = 0; i < count; ++i) {
        sprintf(fname, "f-%08x", i);

	if (objs != NULL)
          objs[i]->obj_ops->put_ref(objs[i]);
        status = fsal_remove(directory, fname);
        EXPECT_EQ(status.major, 0);
      }
    }

    struct req_op_context op_context;
    struct fsal_attrlist attrs;

    struct gsh_export* a_export = nullptr;
    struct fsal_obj_handle *root_entry = nullptr;
    struct fsal_obj_handle *test_root = nullptr;
  };
} // namespase gtest

#endif /* GTEST_GTEST_HH */
