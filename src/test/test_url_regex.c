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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <regex.h>

/* decompose RADOS URL into (<pool>/)object */
#define RADOS_URL_REGEX \
	"([-a-zA-Z0-9_&=.]+)/?([-a-zA-Z0-9_&=/.]+)?"

#define URL1 "my_rados_object"
#define URL2 "mypool_baby/myobject_baby"
#define URL3 "mypool-baby/myobject-baby"
#define URL4 "mypool.baby/myobject.conf"

/* match general URL with optional enclosing quotes */
#define CONFIG_URL_REGEX \
	"^\"?(rados)://([^\"]+)\"?"

#define CONF_URL1 "rados://mypool-baby/myobject-baby"
#define CONF_URL2 "\"rados://mypool-baby/myobject-baby\""
#define CONF_URL3 "\"rados://mypool/myobject.conf\""

#define gsh_malloc malloc

static regex_t url_regex;
static regex_t conf_url_regex;

static inline char *match_dup(regmatch_t *m, char *in)
{
	char *s = NULL;

	if (m->rm_so >= 0) {
		int size;

		size = m->rm_eo - m->rm_so + 1;
		s = (char *)gsh_malloc(size);
		snprintf(s, size, "%s", in + m->rm_so);
	}
	return s;
}

void split_pool(char *url)
{
	regmatch_t match[3];
	char *x0, *x1, *x2;
	int code;

	printf("%s url: %s\n", __func__, url);

	code = regexec(&url_regex, url, 3, match, 0);
	if (!code) {
		/* matched */
		regmatch_t *m = &(match[0]);
		/* matched url pattern is NUL-terminated */
		x0 = match_dup(m, url);
		printf("match0: %s\n", x0);
		m = &(match[1]);
		x1 = match_dup(m, url);
		printf("match1: %s\n", x1);
		m = &(match[2]);
		x2 = match_dup(m, url);
		printf("match2: %s\n", x2);
		free(x0);
		free(x1);
		free(x2);

	} else if (code == REG_NOMATCH) {
		printf("%s: Failed to match %s as a config URL\n",
			__func__, url);
	} else {
		char ebuf[100];

		regerror(code, &url_regex, ebuf, sizeof(ebuf));
		printf("%s: Error in regexec: %s\n",
			__func__, ebuf);
	}

}

void split_url(char *url)
{
	regmatch_t match[3];
	char *x0, *x1, *x2;
	int code;

	printf("%s url: %s\n", __func__, url);

	code = regexec(&conf_url_regex, url, 3, match, 0);
	if (!code) {
		/* matched */
		regmatch_t *m = &(match[0]);
		/* matched url pattern is NUL-terminated */
		x0 = match_dup(m, url);
		printf("match0: %s\n", x0);
		m = &(match[1]);
		x1 = match_dup(m, url);
		printf("match1: %s\n", x1);
		m = &(match[2]);
		x2 = match_dup(m, url);
		printf("match2: %s\n", x2);
		free(x0);
		free(x1);
		free(x2);

	} else if (code == REG_NOMATCH) {
		printf("%s: Failed to match %s as a config URL\n",
			__func__, url);
	} else {
		char ebuf[100];

		regerror(code, &url_regex, ebuf, sizeof(ebuf));
		printf("%s: Error in regexec: %s\n",
			__func__, ebuf);
	}

}

int main(int argc, char **argv)
{
	printf("hi\n");

	int r;

	r = regcomp(&url_regex, RADOS_URL_REGEX, REG_EXTENDED);
	if (!!r) {
		char ebuf[100];

		regerror(r, &url_regex, ebuf, sizeof(ebuf));
		printf("Error initializing rados url regex %s",
			ebuf);
		exit(1);
	}

	split_pool(URL1);
	split_pool(URL2);
	split_pool(URL3);
	split_pool(URL4);

	r = regcomp(&conf_url_regex, CONFIG_URL_REGEX, REG_EXTENDED);
	if (!!r) {
		char ebuf[100];

		regerror(r, &url_regex, ebuf, sizeof(ebuf));
		printf("Error initializing rados url regex %s",
			ebuf);
		exit(1);
	}

	split_url(CONF_URL1);
	split_url(CONF_URL2);
	split_url(CONF_URL3);

	return 0;
}
