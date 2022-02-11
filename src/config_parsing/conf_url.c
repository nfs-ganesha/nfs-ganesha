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

#include "config.h"
#include <regex.h>
#include <dlfcn.h>
#include "log.h"
#include "sal_functions.h"

#include "conf_url.h"

static pthread_rwlock_t url_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static struct glist_head url_providers;
static regex_t url_regex;

/** @brief register handler for new url type
 */
int register_url_provider(struct gsh_url_provider *nurl_p)
{
	struct gsh_url_provider *url_p;
	struct glist_head *gl;
	int code = 0;

	PTHREAD_RWLOCK_wrlock(&url_rwlock);
	glist_for_each(gl, &url_providers) {
		url_p = glist_entry(gl, struct gsh_url_provider, link);
		if (!strcasecmp(url_p->name, nurl_p->name)) {
			code = EEXIST;
			break;
		}
	}
	nurl_p->url_init();
	glist_add_tail(&url_providers, &nurl_p->link);

	PTHREAD_RWLOCK_unlock(&url_rwlock);
	return code;
}

/* simplistic URL syntax */
#define CONFIG_URL_REGEX \
	"^\"?(rados)://([^\"]+)\"?"

/** @brief url regex initializer
 */
static void init_url_regex(void)
{
	int r;

	r = regcomp(&url_regex, CONFIG_URL_REGEX, REG_EXTENDED);
	if (!!r) {
		LogFatal(COMPONENT_INIT,
			"Error initializing config url regex");
	}
}

#ifdef RADOS_URLS
static struct {
	void *dl;
	void (*pkginit)(void);
	int (*setup_watch)(void);
	void (*shutdown_watch)(void);
} rados_urls = { NULL,};

static void load_rados_config(void)
{
	rados_urls.dl = dlopen("libganesha_rados_urls.so",
#if defined(LINUX) && !defined(SANITIZE_ADDRESS)
			      RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#elif defined(FREEBSD) || defined(SANITIZE_ADDRESS)
			      RTLD_NOW | RTLD_LOCAL);
#endif

	if (rados_urls.dl) {
		rados_urls.pkginit = dlsym(rados_urls.dl,
					   "conf_url_rados_pkginit");
		rados_urls.setup_watch = dlsym(rados_urls.dl,
					       "rados_url_setup_watch");
		rados_urls.shutdown_watch = dlsym(rados_urls.dl,
						  "rados_url_shutdown_watch");

		if (!rados_urls.pkginit || !rados_urls.setup_watch ||
		    !rados_urls.shutdown_watch) {
			dlclose(rados_urls.dl);
			rados_urls.dl = NULL;
			LogCrit(COMPONENT_CONFIG, "Unknown urls backend");
		}
	} else {
		LogWarn(COMPONENT_CONFIG, "Missing RADOS URLs backend library");
	}
}

#endif

/** @brief package initializer
 */
void config_url_init(void)
{
	glist_init(&url_providers);

/* init well-known URL providers */
#ifdef RADOS_URLS
	if (!rados_urls.dl)
		load_rados_config();

	if (rados_urls.pkginit)
		rados_urls.pkginit();
#endif
	init_url_regex();
}

/** @brief package shutdown
 */
void config_url_shutdown(void)
{
	struct gsh_url_provider *url_p;

	PTHREAD_RWLOCK_wrlock(&url_rwlock);
	while ((url_p = glist_first_entry(
			      &url_providers, struct gsh_url_provider, link))) {
		glist_del(&url_p->link);
		url_p->url_shutdown();
	}
	PTHREAD_RWLOCK_unlock(&url_rwlock);

	regfree(&url_regex);

#ifdef RADOS_URLS
	if (rados_urls.dl)
		dlclose(rados_urls.dl);
	rados_urls.dl = NULL;
#endif
}

int gsh_rados_url_setup_watch(void)
{
#ifdef RADOS_URLS
	return rados_urls.setup_watch ? rados_urls.setup_watch() : 0;
#else
	return 0;
#endif
}

void gsh_rados_url_shutdown_watch(void)
{
#ifdef RADOS_URLS
	if (rados_urls.shutdown_watch)
		rados_urls.shutdown_watch();
#else
	return; /* non-empty fn to avoid compile warning/error */
#endif
}

static inline char *match_dup(regmatch_t *m, char *in)
{
	char *s = NULL;

	if (m->rm_so >= 0) {
		int size;

		size = m->rm_eo - m->rm_so + 1;
		s = (char *)gsh_malloc(size);
		(void) snprintf(s, size, "%s", in + m->rm_so);
	}
	return s;
}

/** @brief generic url dispatch
 */
int config_url_fetch(const char *url, FILE **f, char **fbuf)
{
	struct gsh_url_provider *url_p;
	struct glist_head *gl;
	regmatch_t match[3];
	char *url_type = NULL, *m_url = NULL;
	int code = EINVAL;

	code = regexec(&url_regex, url, 3, match, 0);
	if (likely(!code)) {
		/* matched */
		regmatch_t *m;

		m = &(match[1]);
		url_type = match_dup(m, (char *)url);
		m = &(match[2]);
		m_url = match_dup(m, (char *)url);
		if (!(url_type && m_url)) {
			LogWarn(COMPONENT_CONFIG,
				"%s: Failed to match %s as a config URL",
				__func__, url);
			goto out;
		}
	} else if (code == REG_NOMATCH) {
		LogWarn(COMPONENT_CONFIG,
			"%s: Failed to match %s as a config URL",
			__func__, url);
		goto out;
	} else {
		char ebuf[100];

		regerror(code, &url_regex, ebuf, sizeof(ebuf));
		LogWarn(COMPONENT_CONFIG,
			"%s: Error in regexec: %s",
			__func__, ebuf);
		goto out;
	}

	PTHREAD_RWLOCK_rdlock(&url_rwlock);
	glist_for_each(gl, &url_providers) {
		url_p = glist_entry(gl, struct gsh_url_provider, link);
		if (!strcasecmp(url_type, url_p->name)) {
			code = url_p->url_fetch(m_url, f, fbuf);
			break;
		}
	}
	PTHREAD_RWLOCK_unlock(&url_rwlock);
out:
	gsh_free(url_type);
	gsh_free(m_url);

	return code;
}

/** @brief return resources allocated by url_fetch
 */
void config_url_release(FILE *f, char *fbuf)
{
	fclose(f);
	free(fbuf);
}
