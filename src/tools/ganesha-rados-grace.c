/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates.
 * Author: Jeff Layton <jlayton@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * rados-grace: tool for managing coordinated grace period database
 *
 * This tool allows an administrator to make direct changes to the rados_grace
 * database. See the rados_grace support library sources for more info about
 * the internals.
 */
#include "config.h"
#include <stdio.h>
#include <stdint.h>
#include <endian.h>
#include <rados/librados.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>
#include <getopt.h>
#include <rados_grace.h>

static int
cluster_connect(rados_ioctx_t *io_ctx, const char *pool, const char *ns,
		bool create)
{
	int ret;
	rados_t clnt;

	ret = rados_create(&clnt, NULL);
	if (ret < 0) {
		fprintf(stderr, "rados_create: %d\n", ret);
		return ret;
	}

	ret = rados_conf_read_file(clnt, NULL);
	if (ret < 0) {
		fprintf(stderr, "rados_conf_read_file: %d\n", ret);
		return ret;
	}

	ret = rados_connect(clnt);
	if (ret < 0) {
		fprintf(stderr, "rados_connect: %d\n", ret);
		return ret;
	}

	if (create) {
		ret = rados_pool_create(clnt, pool);
		if (ret < 0 && ret != -EEXIST) {
			fprintf(stderr, "rados_pool_create: %d\n", ret);
			return ret;
		}
	}

	ret = rados_ioctx_create(clnt, pool, io_ctx);
	if (ret < 0) {
		fprintf(stderr, "rados_ioctx_create: %d\n", ret);
		return ret;
	}

	rados_ioctx_set_namespace(*io_ctx, ns);
	return 0;
}

static const struct option long_options[] = {
	{"ns", 1, NULL, 'n'},
	{"oid", 1, NULL, 'o'},
	{"pool", 1, NULL, 'p'},
	{NULL, 0, NULL, 0}
};

static void usage(char * const *argv)
{
	fprintf(stderr,
		"Usage:\n%s [ --ns namespace ] [ --oid obj_id ] [ --pool pool_id ] dump|add|start|join|lift|remove|enforce|noenforce|member [ nodeid ... ]\n",
		argv[0]);
}

int main(int argc, char * const *argv)
{
	int			ret, nodes = 0;
	rados_ioctx_t		io_ctx;
	const char		*cmd = "dump";
	uint64_t		cur, rec;
	char			*pool = DEFAULT_RADOS_GRACE_POOL;
	char			*oid = DEFAULT_RADOS_GRACE_OID;
	char			*ns = NULL;
	char			c;
	const char * const	*nodeids;
	bool			do_add;

	while ((c = getopt_long(argc, argv, "n:o:p:", long_options,
				NULL)) != -1) {
		switch (c) {
		case 'n':
			ns = optarg;
			break;
		case 'o':
			oid = optarg;
			break;
		case 'p':
			pool = optarg;
			break;
		default:
			usage(argv);
			return 1;
		}
	}

	if (argc > optind) {
		cmd = argv[optind];
		++optind;
		nodes = argc - optind;
		nodeids = (const char * const *)&argv[optind];
	}

	do_add = !strcmp(cmd, "add");
	ret = cluster_connect(&io_ctx, pool, ns, do_add);
	if (ret) {
		fprintf(stderr, "Can't connect to cluster: %d\n", ret);
		return 1;
	}

	if (!strcmp(cmd, "dump")) {
		ret = rados_grace_dump(io_ctx, oid, stdout);
		goto out;
	}

	if (!nodes) {
		fprintf(stderr, "Need at least one nodeid.\n");
		ret = -EINVAL;
		goto out;
	}

	if (do_add) {
		ret = rados_grace_create(io_ctx, oid);
		if (ret < 0 && ret != -EEXIST) {
			fprintf(stderr, "Can't create grace db: %d\n", ret);
			return 1;
		}
		ret = rados_grace_add(io_ctx, oid, nodes, nodeids);
	} else if (!strcmp(cmd, "start")) {
		ret = rados_grace_join_bulk(io_ctx, oid, nodes, nodeids, &cur,
					    &rec, true);
	} else if (!strcmp(cmd, "join")) {
		uint64_t cur, rec;

		ret = rados_grace_join_bulk(io_ctx, oid, nodes, nodeids, &cur,
					    &rec, false);
	} else if (!strcmp(cmd, "lift")) {
		ret = rados_grace_lift_bulk(io_ctx, oid, nodes, nodeids, &cur,
					    &rec, false);
	} else if (!strcmp(cmd, "remove")) {
		ret = rados_grace_lift_bulk(io_ctx, oid, nodes, nodeids, &cur,
					    &rec, true);
	} else if (!strcmp(cmd, "enforce")) {
		ret = rados_grace_enforcing_toggle(io_ctx, oid, nodes, nodeids,
						   &cur, &rec, true);
	} else if (!strcmp(cmd, "noenforce")) {
		ret = rados_grace_enforcing_toggle(io_ctx, oid, nodes, nodeids,
						   &cur, &rec, false);
	} else if (!strcmp(cmd, "member")) {
		ret = rados_grace_member_bulk(io_ctx, oid, nodes, nodeids);
	} else {
		usage(argv);
		ret = -EINVAL;
	}
out:
	if (ret) {
		fprintf(stderr, "Failure: %d\n", ret);
		return 1;
	}
	return 0;
}
