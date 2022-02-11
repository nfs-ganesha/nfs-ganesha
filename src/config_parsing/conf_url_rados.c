// SPDX-License-Identifier: LGPL-3.0-or-later
/* ----------------------------------------------------------------------------
 * Copyright (C) 2017, Red Hat, Inc.
 * contributeur : Matt Benjamin  mbenjamin@redhat.com
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
 * ---------------------------------------
 */

#include "conf_url.h"
#include "conf_url_rados.h"
#include <stdio.h>
#include <stdbool.h>
#include <regex.h>
#include "log.h"
#include "sal_functions.h"
#include <string.h>

static regex_t url_regex;
static rados_t cluster;
static bool initialized;
static rados_ioctx_t rados_watch_io_ctx;
static uint64_t rados_watch_cookie;
static char *rados_watch_oid;

static struct rados_url_parameter {
	/** Path to ceph.conf */
	char *ceph_conf;
	/** Userid (?) */
	char *userid;
	/** watch URL */
	char *watch_url;
} rados_url_param;

static struct config_item rados_url_params[] = {
	CONF_ITEM_PATH("ceph_conf", 1, MAXPATHLEN, NULL,
		       rados_url_parameter, ceph_conf),
	CONF_ITEM_STR("userid", 1, MAXPATHLEN, NULL,
		       rados_url_parameter, userid),
	CONF_ITEM_STR("watch_url", 1, MAXPATHLEN, NULL,
		       rados_url_parameter, watch_url),
	CONFIG_EOL
};

static void *rados_url_param_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL)
		return &rados_url_param;
	else
		return NULL;
}

struct config_block rados_url_param_blk = {
	.dbus_interface_name = "org.ganesha.nfsd.config.rados_urls",
	.blk_desc.name = "RADOS_URLS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = rados_url_param_init,
	.blk_desc.u.blk.params = rados_url_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static int rados_urls_set_param_from_conf(void *tree_node,
					  struct config_error_type *err_type)
{
	(void) load_config_from_node(tree_node,
				&rados_url_param_blk,
				NULL,
				true,
				err_type);

	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing RADOS_URLS config block");
		return -1;
	}

	LogFullDebug(COMPONENT_CONFIG,
		"%s parsed RADOS_URLS block, have ceph_conf=%s "
		" userid=%s",
		__func__,
		rados_url_param.ceph_conf,
		rados_url_param.userid);

	return 0;
}


/* decompose RADOS URL into (<pool>/(<namespace>/))object
 *
 *  verified to match each of the following:
 *
 *  #define URL1 "my_rados_object"
 *  #define URL2 "mypool_baby/myobject_baby"
 *  #define URL3 "mypool-baby/myobject-baby"
 */

#define RADOS_URL_REGEX \
	"([-a-zA-Z0-9_&=.]+)/?([-a-zA-Z0-9_&=.]+)?/?([-a-zA-Z0-9_&=/.]+)?"

/** @brief url regex initializer
 */
static void init_url_regex(void)
{
	int r;

	r = regcomp(&url_regex, RADOS_URL_REGEX, REG_EXTENDED);
	if (!!r) {
		LogFatal(COMPONENT_INIT,
			"Error initializing rados url regex");
	}
}

static void cu_rados_url_early_init(void)
{
	init_url_regex();
}

extern struct config_error_type err_type;

static int rados_url_client_setup(void)
{
	int ret;

	if (initialized)
		return 0;

	ret = rados_create(&cluster, rados_url_param.userid);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed in rados_create",
			__func__);
		return ret;
	}

	ret = rados_conf_read_file(cluster, rados_url_param.ceph_conf);
	if (ret < 0) {
		LogEvent(COMPONENT_CLIENTID, "%s: Failed to read ceph_conf",
			__func__);
		rados_shutdown(cluster);
		return ret;
	}

	ret = rados_connect(cluster);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to connect to cluster",
			__func__);
		rados_shutdown(cluster);
		return ret;
	}

	init_url_regex();
	initialized = true;
	return 0;
}

static void cu_rados_url_init(void)
{
	int ret;
	void *node;

	node = config_GetBlockNode("RADOS_URLS");
	if (node) {
		ret = rados_urls_set_param_from_conf(node, &err_type);
		if (ret < 0) {
			LogEvent(COMPONENT_CONFIG,
				"%s: Failed to parse RADOS_URLS %d",
				__func__, ret);
		}
	} else {
		LogWarn(COMPONENT_CONFIG,
			"%s: RADOS_URLS config block not found",
			__func__);
	}

	rados_url_client_setup();
}

static void cu_rados_url_shutdown(void)
{
	if (initialized) {
		rados_shutdown(cluster);
		regfree(&url_regex);
		initialized = false;
	}
}

static inline char *match_dup(regmatch_t *m, const char *in)
{
	char *s = NULL;

	if (m->rm_so >= 0) {
		int size;

		size = m->rm_eo - m->rm_so + 1;
		s = (char *)gsh_malloc(size);
		memcpy(s, in + m->rm_so, size - 1);
		s[size - 1] = '\0';
	}
	return s;
}

static int rados_url_parse(const char *url, char **pool, char **ns, char **obj)
{
	int ret;
	regmatch_t match[4];

	ret = regexec(&url_regex, url, 4, match, 0);
	if (likely(!ret)) {
		regmatch_t *m;
		char *x1, *x2, *x3;

		m = &(match[1]);
		x1 = match_dup(m, url);
		m = &(match[2]);
		x2 = match_dup(m, url);
		m = &(match[3]);
		x3 = match_dup(m, url);

		*pool = NULL;
		*ns = NULL;
		*obj = NULL;

		if (x1) {
			if (!x2) {
				/*
				 * object only
				 *
				 * FIXME: should we reject this case? I don't
				 * think there is such a thing as a default
				 * pool
				 */
				*obj = x1;
			} else {
				*pool = x1;
				if (!x3) {
					*obj = x2;
				} else {
					*ns = x2;
					*obj = x3;
				}
			}
		}
	} else if (ret == REG_NOMATCH) {
		LogWarn(COMPONENT_CONFIG,
			"%s: Failed to match %s as a config URL",
			__func__, url);
	} else {
		char ebuf[100];

		regerror(ret, &url_regex, ebuf, sizeof(ebuf));
		LogWarn(COMPONENT_CONFIG,
			"%s: Error in regexec: %s",
			__func__, ebuf);
	}
	return ret;
}

static int cu_rados_url_fetch(const char *url, FILE **f, char **fbuf)
{
	rados_ioctx_t io_ctx;

	char *pool_name = NULL;
	char *object_name = NULL;
	char *rados_ns = NULL;

	char *streambuf = NULL; /* not optional (buggy open_memstream) */
	FILE *stream = NULL;
	char buf[1024];

	size_t streamsz;
	uint64_t off1 = 0;
	int ret;

	if (!initialized) {
		cu_rados_url_init();
	}

	ret = rados_url_parse(url, &pool_name, &rados_ns, &object_name);
	if (ret)
		goto out;

	ret = rados_ioctx_create(cluster, pool_name, &io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to create ioctx",
			__func__);
		cu_rados_url_shutdown();
		goto out;
	}
	rados_ioctx_set_namespace(io_ctx, rados_ns);

	do {
		int nread, wrt, nwrt;
		uint64_t off2 = 0;

		nread = ret = rados_read(io_ctx, object_name, buf, 1024, off1);
		if (ret < 0) {
			LogEvent(COMPONENT_CONFIG,
				"%s: Failed reading %s/%s %s", __func__,
				pool_name, object_name, strerror(ret));
			goto err;
		}
		off1 += nread;
		if (!stream) {
			streamsz = 1024;
			stream = open_memstream(&streambuf, &streamsz);
		}
		do {
			wrt = fwrite(buf+off2, 1, nread, stream);
			if (wrt > 0) {
				nwrt = MIN(nread, 1024);
				nread -= nwrt;
				off2 += nwrt;
			}
		} while (wrt > 0 && nread > 0);
	} while (ret > 0);

	if (likely(stream)) {
		/* rewind */
		fseek(stream, 0L, SEEK_SET);
		/* return--caller will release */
		*f = stream;
		*fbuf = streambuf;
	}

err:
	rados_ioctx_destroy(io_ctx);

out:
	/* allocated or NULL */
	gsh_free(pool_name);
	gsh_free(rados_ns);
	gsh_free(object_name);

	return ret;
}

static struct gsh_url_provider rados_url_provider = {
	.name = "rados",
	.url_init = cu_rados_url_early_init,
	.url_shutdown = cu_rados_url_shutdown,
	.url_fetch = cu_rados_url_fetch
};

void conf_url_rados_pkginit(void)
{
	register_url_provider(&rados_url_provider);
}

static void rados_url_watchcb(void *arg, uint64_t notify_id, uint64_t handle,
			      uint64_t notifier_id, void *data, size_t data_len)
{
	int ret;

	/* ACK it to keep things moving */
	ret = rados_notify_ack(rados_watch_io_ctx, rados_watch_oid, notify_id,
				rados_watch_cookie, NULL, 0);
	if (ret < 0)
		LogEvent(COMPONENT_CONFIG, "rados_notify_ack failed: %d", ret);

	/* Send myself a SIGHUP */
	kill(getpid(), SIGHUP);
}

int rados_url_setup_watch(void)
{
	int ret;
	void *node;
	char *pool = NULL, *ns = NULL, *obj = NULL;
	char *url;

	/* No RADOS_URLs block? Just return */
	node = config_GetBlockNode("RADOS_URLS");
	if (!node)
		return 0;

	ret = rados_urls_set_param_from_conf(node, &err_type);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to parse RADOS_URLS %d",
			 __func__, ret);
		return ret;
	}

	/* No watch parameter? Just return */
	if (rados_url_param.watch_url == NULL)
		return 0;

	if (strncmp(rados_url_param.watch_url, "rados://", 8)) {
		LogEvent(COMPONENT_CONFIG,
			 "watch_url doesn't start with rados://");
		return -1;
	}

	url = rados_url_param.watch_url + 8;

	/* Parse the URL */
	ret = rados_url_parse(url, &pool, &ns, &obj);
	if (ret)
		return ret;

	ret = rados_url_client_setup();
	if (ret)
		goto out;

	/* Set up an ioctx */
	ret = rados_ioctx_create(cluster, pool, &rados_watch_io_ctx);
	if (ret < 0) {
		LogEvent(COMPONENT_CONFIG, "%s: Failed to create ioctx",
			__func__);
		goto out;
	}
	rados_ioctx_set_namespace(rados_watch_io_ctx, ns);

	ret = rados_watch3(rados_watch_io_ctx, obj, &rados_watch_cookie,
			   rados_url_watchcb, NULL, 30, NULL);
	if (ret) {
		rados_ioctx_destroy(rados_watch_io_ctx);
		LogEvent(COMPONENT_CONFIG,
			 "Failed to set watch on RADOS_URLS object: %d", ret);
	} else {
		rados_watch_oid = obj;
		obj = NULL;
	}
out:
	gsh_free(pool);
	gsh_free(ns);
	gsh_free(obj);

	return ret;
}

void rados_url_shutdown_watch(void)
{
	int ret;

	if (rados_watch_oid) {
		ret = rados_unwatch2(rados_watch_io_ctx, rados_watch_cookie);
		if (ret)
			LogEvent(COMPONENT_CONFIG,
				 "Failed to unwatch RADOS_URLS object: %d",
				 ret);

		rados_ioctx_destroy(rados_watch_io_ctx);
		rados_watch_io_ctx = NULL;
		gsh_free(rados_watch_oid);
		rados_watch_oid = NULL;
		/* Leave teardown of client to the %url parser shutdown */
	}
}
