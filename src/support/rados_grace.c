/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2018 Red Hat, Inc. and/or its affiliates.
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

/* Each cluster node needs a slot here */
#define MAX_ITEMS			1024

/* Flags for the omap value flags field */

/* Does this node currently require a grace period? */
#define RADOS_GRACE_NEED_GRACE		0x1

/* Is this node currently enforcing its grace period locally? */
#define RADOS_GRACE_ENFORCING		0x2

static void rados_grace_notify(rados_ioctx_t io_ctx, const char *oid)
{
	static char *buf;
	static size_t len;

	/* FIXME: we don't really want or need this to be synchronous */
	rados_notify2(io_ctx, oid, "", 0, 3000, &buf, &len);
	rados_buffer_free(buf);
}

int
rados_grace_create(rados_ioctx_t io_ctx, const char *oid)
{
	int			ret;
	rados_write_op_t	op = NULL;
	uint64_t		cur = htole64(1);	// starting epoch
	uint64_t		rec = htole64(0);	// no recovery yet
	char			buf[sizeof(uint64_t) * 2];

	/*
	 * 2 uint64_t's
	 *
	 * The first denotes the current epoch serial number, the epoch serial
	 * number under which new recovery records should be created.
	 *
	 * The second number denotes the epoch from which clients are allowed
	 * to reclaim.
	 *
	 * An epoch of zero is never allowed, so if rec=0, then the grace
	 * period is no longer in effect, and can't be joined.
	 */
	memcpy(buf, (char *)&cur, sizeof(cur));
	memcpy(buf + sizeof(cur), (char *)&rec, sizeof(rec));

	op = rados_create_write_op();
	/* Create the object */
	rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);

	/* Set serial numbers if we created the object */
	rados_write_op_write_full(op, buf, sizeof(buf));

	ret = rados_write_op_operate(op, io_ctx, oid, NULL, 0);
	rados_release_write_op(op);
	return ret;
}

int
rados_grace_dump(rados_ioctx_t io_ctx, const char *oid, FILE *stream)
{
	int ret;
	rados_omap_iter_t iter;
	rados_read_op_t op;
	char *key_out = NULL;
	char *val_out = NULL;
	unsigned char more = '\0';
	size_t len_out = 0;
	char buf[sizeof(uint64_t) * 2];
	uint64_t	cur, rec;

	op = rados_create_read_op();
	rados_read_op_read(op, 0, sizeof(buf), buf, &len_out, NULL);
	rados_read_op_omap_get_vals2(op, "", "", MAX_ITEMS, &iter, &more, NULL);
	ret = rados_read_op_operate(op, io_ctx, oid, 0);
	if (ret < 0) {
		fprintf(stream, "%s: ret=%d", __func__, ret);
		goto out;
	}

	if (len_out != sizeof(buf)) {
		ret = -ENOTRECOVERABLE;
		goto out;
	}

	if (more) {
		ret = -ENOTRECOVERABLE;
		goto out;
	}

	cur = le64toh(*(uint64_t *)buf);
	rec = le64toh(*(uint64_t *)(buf + sizeof(uint64_t)));
	fprintf(stream, "cur=%lu rec=%lu\n", cur, rec);
	fprintf(stream,
		"======================================================\n");
	for (;;) {
		char need = ' ', enforcing = ' ';

		rados_omap_get_next(iter, &key_out, &val_out, &len_out);
		if (key_out == NULL || val_out == NULL)
			break;
		if (*val_out & RADOS_GRACE_NEED_GRACE)
			need = 'N';
		if (*val_out & RADOS_GRACE_ENFORCING)
			enforcing = 'E';
		fprintf(stream, "%s\t%c%c\n", key_out, need, enforcing);
	}
	rados_omap_get_end(iter);
out:
	rados_release_read_op(op);
	return ret;
}

int
rados_grace_epochs(rados_ioctx_t io_ctx, const char *oid,
			uint64_t *cur, uint64_t *rec)
{
	int ret;
	rados_read_op_t op;
	size_t len_out = 0;
	char buf[sizeof(uint64_t) * 2];

	op = rados_create_read_op();
	rados_read_op_read(op, 0, sizeof(buf), buf, &len_out, NULL);
	ret = rados_read_op_operate(op, io_ctx, oid, 0);
	if (ret < 0)
		goto out;

	ret = -ENOTRECOVERABLE;
	if (len_out != sizeof(buf))
		goto out;

	*cur = le64toh(*(uint64_t *)buf);
	*rec = le64toh(*(uint64_t *)(buf + sizeof(uint64_t)));
	ret = 0;
out:
	rados_release_read_op(op);
	return ret;
}

int
rados_grace_enforcing_toggle(rados_ioctx_t io_ctx, const char *oid, int nodes,
			const char * const *nodeids, uint64_t *pcur,
			uint64_t *prec, bool enable)
{
	int			i, ret;
	char			*flags = NULL;
	char			**vals = NULL;
	size_t			*lens = NULL;
	bool			*match = NULL;
	uint64_t		cur, rec, ver;

	/* allocate an array of flag bytes */
	flags = calloc(nodes, 1);
	if (!flags) {
		ret = -ENOMEM;
		goto out;
	}

	/* pointers to each val byte */
	vals = calloc(nodes, sizeof(char *));
	if (!vals) {
		ret = -ENOMEM;
		goto out;
	}

	/* lengths */
	lens = calloc(nodes, sizeof(size_t));
	if (!lens) {
		ret = -ENOMEM;
		goto out;
	}

	match = calloc(nodes, sizeof(bool));
	if (!match) {
		ret = -ENOMEM;
		goto out;
	}

	/* each val is one flag byte */
	for (i = 0; i < nodes; ++i) {
		vals[i] = &flags[i];
		lens[i] = 1;
	}

	do {
		rados_write_op_t	wop;
		rados_read_op_t		rop;
		rados_omap_iter_t	iter;
		size_t			len = 0;
		unsigned char		more = 0;
		char			buf[sizeof(uint64_t) * 2];

		/* read epoch blob */
		rop = rados_create_read_op();
		rados_read_op_read(rop, 0, sizeof(buf), buf, &len, NULL);
		rados_read_op_omap_get_vals2(rop, "", "", MAX_ITEMS, &iter,
						&more, NULL);
		ret = rados_read_op_operate(rop, io_ctx, oid, 0);
		if (ret < 0) {
			rados_release_read_op(rop);
			break;
		}

		if (more || (len != sizeof(buf))) {
			ret = -ENOTRECOVERABLE;
			rados_release_read_op(rop);
			break;
		}

		ver = rados_get_last_version(io_ctx);

		/*
		 * Walk the returned kv pairs and flip on any existing flags
		 * in the matching nodeid (if there is one)
		 */
		for (;;) {
			char *key, *val;

			rados_omap_get_next(iter, &key, &val, &len);
			if (!key)
				break;
			for (i = 0; i < nodes; ++i) {
				if (strcmp(key, nodeids[i]))
					continue;
				flags[i] = *val;
				match[i] = true;
				if (enable)
					flags[i] |= RADOS_GRACE_ENFORCING;
				else
					flags[i] &= ~RADOS_GRACE_ENFORCING;
				break;
			}
		}
		rados_omap_get_end(iter);
		rados_release_read_op(rop);

		/* Ensure that all given nodes have a key in the omap */
		for (i = 0; i < nodes; ++i) {
			if (!match[i]) {
				ret = -ENOKEY;
				goto out;
			}
		}

		/* Get old epoch numbers and version */
		cur = le64toh(*(uint64_t *)buf);
		rec = le64toh(*(uint64_t *)(buf + sizeof(uint64_t)));

		/* Attempt to update object */
		wop = rados_create_write_op();

		/* Ensure that nothing has changed */
		rados_write_op_assert_version(wop, ver);

		/* Set omap values to given ones */
		rados_write_op_omap_set(wop, nodeids,
				(const char * const*)vals, lens, nodes);

		ret = rados_write_op_operate(wop, io_ctx, oid, NULL, 0);
		rados_release_write_op(wop);
		if (ret >= 0)
			rados_grace_notify(io_ctx, oid);
	} while (ret == -ERANGE);

	if (!ret) {
		*pcur = cur;
		*prec = rec;
	}
out:
	free(match);
	free(lens);
	free(flags);
	free(vals);
	return ret;
}

int
rados_grace_enforcing_check(rados_ioctx_t io_ctx, const char *oid,
			    const char *nodeid)
{
	int			ret;
	rados_read_op_t		rop;
	rados_omap_iter_t	iter;
	unsigned char		more = 0;

	rop = rados_create_read_op();
	rados_read_op_omap_get_vals2(rop, "", "", MAX_ITEMS, &iter,
					&more, NULL);
	ret = rados_read_op_operate(rop, io_ctx, oid, 0);
	if (ret < 0) {
		rados_release_read_op(rop);
		goto out;
	}

	if (more) {
		ret = -ENOTRECOVERABLE;
		rados_release_read_op(rop);
		goto out;
	}

	ret = -ENOKEY;
	for (;;) {
		char		*key, *val;
		size_t		len = 0;

		rados_omap_get_next(iter, &key, &val, &len);
		if (!key)
			break;
		/* If anyone isn't enforcing, then return an err */
		if (!(*val & RADOS_GRACE_ENFORCING)) {
			ret = -EL2NSYNC;
			break;
		}

		/* Only return 0 if this node is in the omap */
		if (!strcmp(nodeid, key))
			ret = 0;
	}
	rados_omap_get_end(iter);
	rados_release_read_op(rop);
out:
	return ret;
}

int
rados_grace_join_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
			const char * const *nodeids, uint64_t *pcur,
			uint64_t *prec, bool start)
{
	int			i, ret;
	char			*flags = NULL;
	char			**vals = NULL;
	size_t			*lens = NULL;
	bool			*match = NULL;
	uint64_t		cur, rec, ver;

	/* flag bytes */
	flags = malloc(nodes);
	if (!flags) {
		ret = -ENOMEM;
		goto out;
	}

	/* pointers to each val byte */
	vals = calloc(nodes, sizeof(char *));
	if (!vals) {
		ret = -ENOMEM;
		goto out;
	}

	/* lengths */
	lens = calloc(nodes, sizeof(size_t));
	if (!lens) {
		ret = -ENOMEM;
		goto out;
	}

	match = calloc(nodes, sizeof(bool));
	if (!match) {
		ret = -ENOMEM;
		goto out;
	}

	/* each val is one flag byte */
	for (i = 0; i < nodes; ++i) {
		vals[i] = &flags[i];
		lens[i] = 1;
	}

	do {
		rados_write_op_t	wop;
		rados_read_op_t		rop;
		rados_omap_iter_t	iter;
		size_t			len = 0;
		unsigned char		more = 0;
		char			buf[sizeof(uint64_t) * 2];

		/* read epoch blob */
		rop = rados_create_read_op();
		rados_read_op_read(rop, 0, sizeof(buf), buf, &len, NULL);
		rados_read_op_omap_get_vals2(rop, "", "", MAX_ITEMS, &iter,
						&more, NULL);
		ret = rados_read_op_operate(rop, io_ctx, oid, 0);
		if (ret < 0) {
			rados_release_read_op(rop);
			break;
		}

		if (more || (len != sizeof(buf))) {
			ret = -ENOTRECOVERABLE;
			rados_release_read_op(rop);
			break;
		}

		ver = rados_get_last_version(io_ctx);

		/*
		 * Walk the returned kv pairs and flip on any existing flags
		 * in the matching nodeid (if there is one)
		 */
		memset(flags, RADOS_GRACE_NEED_GRACE|RADOS_GRACE_ENFORCING,
			nodes);
		for (;;) {
			char *key, *val;

			rados_omap_get_next(iter, &key, &val, &len);
			if (!key)
				break;
			for (i = 0; i < nodes; ++i) {
				if (!strcmp(key, nodeids[i])) {
					flags[i] |= *val;
					match[i] = true;
					break;
				}
			}
		}
		rados_omap_get_end(iter);
		rados_release_read_op(rop);

		/* Ensure that all given nodes have a key in the omap */
		for (i = 0; i < nodes; ++i) {
			if (!match[i]) {
				ret = -ENOKEY;
				goto out;
			}
		}

		/* Get old epoch numbers and version */
		cur = le64toh(*(uint64_t *)buf);
		rec = le64toh(*(uint64_t *)(buf + sizeof(uint64_t)));

		/* Only start a new grace period if start bool is set */
		/* FIXME: do we need this with real membership? */
		if (rec == 0 && !start)
			break;

		/* Attempt to update object */
		wop = rados_create_write_op();

		/* Ensure that nothing has changed */
		rados_write_op_assert_version(wop, ver);

		/* Update the object data iff rec == 0 */
		if (rec == 0) {
			uint64_t tc, tr;

			rec = cur;
			++cur;
			tc = htole64(cur);
			tr = htole64(rec);
			memcpy(buf, (char *)&tc, sizeof(tc));
			memcpy(buf + sizeof(tc), (char *)&tr, sizeof(tr));
			rados_write_op_write_full(wop, buf, sizeof(buf));
		}

		/* Set omap values to given ones */
		rados_write_op_omap_set(wop, nodeids,
				(const char * const*)vals, lens, nodes);

		ret = rados_write_op_operate(wop, io_ctx, oid, NULL, 0);
		rados_release_write_op(wop);
		if (ret >= 0)
			rados_grace_notify(io_ctx, oid);
	} while (ret == -ERANGE);

	if (!ret) {
		*pcur = cur;
		*prec = rec;
	}
out:
	free(match);
	free(lens);
	free(flags);
	free(vals);
	return ret;
}

int
rados_grace_lift_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
			const char * const *nodeids, uint64_t *pcur,
			uint64_t *prec, bool remove)
{
	int			ret;
	char			*flags = NULL;
	char			**vals = NULL;
	size_t			*lens = NULL;
	const char		**keys = NULL;
	bool			*match = NULL;
	uint64_t		cur, rec;

	keys = calloc(nodes, sizeof(char *));
	if (!keys) {
		ret = -ENOMEM;
		goto out;
	}

	match = calloc(nodes, sizeof(bool));
	if (!match) {
		ret = -ENOMEM;
		goto out;
	}

	/* We don't need these arrays if we're just removing the keys */
	if (!remove) {
		flags = calloc(nodes, 1);
		if (!flags) {
			ret = -ENOMEM;
			goto out;
		}

		/* pointers to each val byte */
		vals = calloc(nodes, sizeof(char *));
		if (!vals) {
			ret = -ENOMEM;
			goto out;
		}

		lens = calloc(nodes, sizeof(size_t));
		if (!lens) {
			ret = -ENOMEM;
			goto out;
		}
	}

	do {
		int			i, k, need;
		rados_write_op_t	wop;
		rados_read_op_t		rop;
		rados_omap_iter_t	iter;
		char			*key, *val;
		size_t			len;
		char			buf[sizeof(uint64_t) * 2];
		unsigned char		more = '\0';
		uint64_t		ver;
		bool			enforcing;

		/* read epoch blob and omap keys */
		rop = rados_create_read_op();
		rados_read_op_read(rop, 0, sizeof(buf), buf, &len, NULL);
		rados_read_op_omap_get_vals2(rop, "", "", MAX_ITEMS, &iter,
						&more, NULL);
		ret = rados_read_op_operate(rop, io_ctx, oid, 0);
		if (ret < 0) {
			rados_release_read_op(rop);
			break;
		}

		if (more) {
			ret = -ENOTRECOVERABLE;
			rados_release_read_op(rop);
			break;
		}

		if (len != sizeof(buf)) {
			ret = -ENOTRECOVERABLE;
			rados_release_read_op(rop);
			break;
		}

		/* Get old epoch numbers and version */
		ver = rados_get_last_version(io_ctx);
		cur = le64toh(*(uint64_t *)buf);
		rec = le64toh(*(uint64_t *)(buf + sizeof(uint64_t)));

		/*
		 * Walk omap keys, see if it's in nodeids array. Add any that
		 * are and have NEED_GRACE set to the "keys" array along with
		 * the the flags field with the NEED_GRACE flag cleared.
		 *
		 * If the remove flag is set, then just remove them from the
		 * omap and don't bother with changing the flags.
		 */
		need = 0;
		k = 0;
		enforcing = true;
		for (;;) {
			ret = rados_omap_get_next(iter, &key, &val, &len);
			if (!key)
				break;

			/* Make note if anyone is not enforcing */
			if (!(*val & RADOS_GRACE_ENFORCING))
				enforcing = false;

			if (*val & RADOS_GRACE_NEED_GRACE)
				++need;

			for (i = 0; i < nodes; ++i) {
				if (strcmp(key, nodeids[i]))
					continue;
				match[i] = true;
				if (!remove && !(*val & RADOS_GRACE_NEED_GRACE))
					break;
				keys[k] = nodeids[i];
				/*
				 * For the removal case, we just need the
				 * keys.
				 */
				if (!remove) {
					flags[k] =
						*val & ~RADOS_GRACE_NEED_GRACE;
					vals[k] = &flags[k];
					lens[k] = 1;
				}
				++k;
				break;
			}
		};
		rados_omap_get_end(iter);
		rados_release_read_op(rop);

		/*
		 * We can't lift if we're in a grace period and there are
		 * cluster members that haven't started enforcement yet. Wait
		 * until they catch up.
		 */
		if (rec && !enforcing)
			goto out;

		/* Ensure that all given nodes have a key in the omap */
		for (i = 0; i < nodes; ++i) {
			if (!match[i]) {
				ret = -ENOKEY;
				goto out;
			}
		}

		/* No matching keys? Nothing to do. */
		if (k == 0)
			break;

		/* Attempt to update object */
		wop = rados_create_write_op();

		/* Ensure that nothing has changed */
		rados_write_op_assert_version(wop, ver);

		/* Set or remove any keys we matched earlier */
		if (remove)
			rados_write_op_omap_rm_keys(wop, keys, k);
		else
			rados_write_op_omap_set(wop, keys,
						(const char * const *)vals,
						lens, k);

		/*
		 * If number of omap records we're setting or removing is the
		 * same as the number of hosts that have NEED_GRACE set, then
		 * fully lift the grace period.
		 */
		if (need == k) {
			uint64_t tc, tr;

			rec = 0;
			tr = htole64(rec);
			tc = htole64(cur);
			memcpy(buf, (char *)&tc, sizeof(tc));
			memcpy(buf + sizeof(tc), (char *)&tr, sizeof(tr));
			rados_write_op_write_full(wop, buf, sizeof(buf));
		}

		ret = rados_write_op_operate(wop, io_ctx, oid, NULL, 0);
		rados_release_write_op(wop);
		if (ret >= 0)
			rados_grace_notify(io_ctx, oid);
	} while (ret == -ERANGE);
out:
	if (!ret) {
		*pcur = cur;
		*prec = rec;
	}
	free(match);
	free(vals);
	free(lens);
	free(flags);
	free(keys);
	return ret;
}

int
rados_grace_add(rados_ioctx_t io_ctx, const char *oid, int nodes,
		const char * const *nodeids)
{
	int			i, ret;
	char			*flags = NULL;
	char			**vals = NULL;
	size_t			*lens = NULL;
	uint64_t		ver;

	/* allocate an array of flag bytes */
	flags = calloc(nodes, 1);
	if (!flags) {
		ret = -ENOMEM;
		goto out;
	}

	/* pointers to each val byte */
	vals = calloc(nodes, sizeof(char *));
	if (!vals) {
		ret = -ENOMEM;
		goto out;
	}

	/* lengths */
	lens = calloc(nodes, sizeof(size_t));
	if (!lens) {
		ret = -ENOMEM;
		goto out;
	}

	/* each val is one flag byte */
	for (i = 0; i < nodes; ++i) {
		flags[i] = RADOS_GRACE_ENFORCING;
		vals[i] = &flags[i];
		lens[i] = 1;
	}

	do {
		rados_write_op_t	wop;
		rados_read_op_t		rop;
		rados_omap_iter_t	iter;
		unsigned char		more = 0;

		/* read epoch blob */
		rop = rados_create_read_op();
		rados_read_op_omap_get_vals2(rop, "", "", MAX_ITEMS, &iter,
						&more, NULL);
		ret = rados_read_op_operate(rop, io_ctx, oid, 0);
		if (ret < 0) {
			rados_release_read_op(rop);
			break;
		}

		if (more) {
			ret = -ENOTRECOVERABLE;
			rados_release_read_op(rop);
			break;
		}

		ver = rados_get_last_version(io_ctx);

		/* Ensure no nodes in the list already exist */
		for (;;) {
			char *key, *val;
			size_t len;

			rados_omap_get_next(iter, &key, &val, &len);
			if (!key)
				break;
			for (i = 0; i < nodes; ++i) {
				if (!strcmp(key, nodeids[i])) {
					ret = -EEXIST;
					rados_omap_get_end(iter);
					rados_release_read_op(rop);
					goto out;
				}
			}
		}
		rados_omap_get_end(iter);
		rados_release_read_op(rop);

		/* Attempt to update object */
		wop = rados_create_write_op();

		/* Ensure that nothing has changed */
		rados_write_op_assert_version(wop, ver);

		/* Set omap values to given ones */
		rados_write_op_omap_set(wop, nodeids,
				(const char * const*)vals, lens, nodes);

		ret = rados_write_op_operate(wop, io_ctx, oid, NULL, 0);
		rados_release_write_op(wop);
		if (ret >= 0)
			rados_grace_notify(io_ctx, oid);
	} while (ret == -ERANGE);
out:
	free(lens);
	free(flags);
	free(vals);
	return ret;
}

int
rados_grace_member_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
			 const char * const *nodeids)
{
	int			ret, rval, cnt;
	rados_read_op_t		rop;
	rados_omap_iter_t	iter;

	/* read epoch blob */
	rop = rados_create_read_op();
	rados_read_op_omap_get_vals_by_keys(rop, nodeids, nodes, &iter, &rval);
	ret = rados_read_op_operate(rop, io_ctx, oid, 0);
	if (ret < 0) {
		rados_release_read_op(rop);
		return ret;
	}

	/* Count the returned keys */
	cnt = 0;
	for (;;) {
		char *key, *val;
		size_t len;

		rados_omap_get_next(iter, &key, &val, &len);
		if (!key)
			break;
		++cnt;
	}
	rados_omap_get_end(iter);
	rados_release_read_op(rop);
	return (cnt == nodes) ? 0 : -ENOENT;
}
