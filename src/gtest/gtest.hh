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
/* Ganesha headers */
#include "nfs_lib.h"

/* LTTng headers */
#include <lttng/lttng.h>

/* gperf headers */
#include <gperftools/profiler.h>
}

#ifndef GTEST_GTEST_HH
#define GTEST_GTEST_HH

namespace gtest {

  class Environment* env;

  class Environment : public ::testing::Environment {
  public:
    Environment() : Environment(NULL, NULL, -1, NULL) {}
    Environment(char* ganesha_conf, char* lpath, int dlevel, char *_ses_name) :
	    ganesha(nfs_libmain, ganesha_conf, lpath, dlevel),
	    session_name(_ses_name), handle(NULL) {
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

    struct lttng_handle* getLTTng() {
      return handle;
    }

    std::thread ganesha;
    char *session_name;
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
} // namespase gtest

#endif /* GTEST_GTEST_HH */
