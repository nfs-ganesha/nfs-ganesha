// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @addtogroup idmapper
 * @{
 */

/**
 * @file idmapper.c
 * @brief Id mapping functions
 */

#include "config.h"
#include <unistd.h>		/* for using gethostname */
#include <stdlib.h>		/* for using exit */
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef USE_NFSIDMAP
#include <nfsidmap.h>
#include "nfs_exports.h"
#endif				/* USE_NFSIDMAP */
#ifdef _MSPAC_SUPPORT
#include <wbclient.h>
#endif
#include "common_utils.h"
#include "gsh_rpc.h"
#include "gsh_types.h"
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "nfs_core.h"
#include "idmapper.h"
#include "server_stats_private.h"

static struct gsh_buffdesc owner_domain;

/* winbind auth stats information */
struct auth_stats winbind_auth_stats;
pthread_rwlock_t winbind_auth_lock = PTHREAD_RWLOCK_INITIALIZER;

/*group cache auth stats information */
struct auth_stats gc_auth_stats;
pthread_rwlock_t gc_auth_lock = PTHREAD_RWLOCK_INITIALIZER;

/* Cleanup on shutdown */
struct cleanup_list_element idmapper_cleanup_element;

/**
 * @brief Cleanup idmapper on shutdown
 */
void idmapper_cleanup(void)
{
	gsh_free(owner_domain.addr);

	idmapper_clear_cache();
}

/*DNS auth stats information */
struct auth_stats dns_auth_stats;
pthread_rwlock_t dns_auth_lock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief Initialize the ID Mapper
 *
 * @return true on success, false on failure
 */

bool idmapper_init(void)
{
#ifdef USE_NFSIDMAP
	if (!nfs_param.nfsv4_param.use_getpwnam) {
		if (nfs4_init_name_mapping(nfs_param.nfsv4_param.idmapconf)
		    != 0) {
			return false;
		}
		owner_domain.addr = gsh_malloc(NFS4_MAX_DOMAIN_LEN + 1);

		if (nfs4_get_default_domain
		    (NULL, owner_domain.addr, NFS4_MAX_DOMAIN_LEN) != 0) {
			gsh_free(owner_domain.addr);
			return false;
		}
		owner_domain.len = strlen(owner_domain.addr);
	}
#endif				/* USE_NFSIDMAP */
	if (nfs_param.nfsv4_param.use_getpwnam) {
		owner_domain.addr = gsh_strdup(nfs_param.nfsv4_param
					       .domainname);
		owner_domain.len = strlen(nfs_param.nfsv4_param.domainname);
	}

	idmapper_cache_init();

	idmapper_cleanup_element.clean = idmapper_cleanup;
	RegisterCleanup(&idmapper_cleanup_element);

	return true;
}

/**
 * @brief Encode a UID or GID as a string
 *
 * @param[in,out] xdrs  XDR stream to which to encode
 * @param[in]     id    UID or GID
 * @param[in]     group True if this is a GID, false for a UID
 *
 * @retval true on success.
 * @retval false on failure.
 */

static bool xdr_encode_nfs4_princ(XDR *xdrs, uint32_t id, bool group)
{
	const struct gsh_buffdesc *found;
	uint32_t not_a_size_t;
	bool success = false;

	if (nfs_param.nfsv4_param.only_numeric_owners) {
		/* 2**32 is 10 digits long in decimal */
		struct gsh_buffdesc name;
		char namebuf[11];

		name.addr = namebuf;
		name.len = sprintf(namebuf, "%"PRIu32, id);
		not_a_size_t = name.len;
		return inline_xdr_bytes(xdrs, (char **)&name.addr,
					&not_a_size_t, UINT32_MAX);
	}

	PTHREAD_RWLOCK_rdlock(group ? &idmapper_group_lock :
			      &idmapper_user_lock);
	if (group)
		success = idmapper_lookup_by_gid(id, &found);
	else
		success = idmapper_lookup_by_uid(id, &found, NULL);

	if (likely(success)) {
		not_a_size_t = found->len;

		/* Fully qualified owners are always stored in the
		   hash table, no matter what our lookup method. */
		success =
		    inline_xdr_bytes(xdrs, (char **)&found->addr, &not_a_size_t,
				     UINT32_MAX);
		PTHREAD_RWLOCK_unlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);
		return success;
	} else {
		PTHREAD_RWLOCK_unlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);
		int rc;
		int size;
		bool looked_up = false;
		char *namebuff = NULL;
		struct gsh_buffdesc new_name;

		if (nfs_param.nfsv4_param.use_getpwnam) {
			if (group)
				size = sysconf(_SC_GETGR_R_SIZE_MAX);
			else
				size = sysconf(_SC_GETPW_R_SIZE_MAX);
			if (size == -1)
				size = PWENT_BEST_GUESS_LEN;
			new_name.len = size;
			size += owner_domain.len + 2;
		} else {
			size = NFS4_MAX_DOMAIN_LEN + 2;
		}

		namebuff = alloca(size);

		new_name.addr = namebuff;

		if (nfs_param.nfsv4_param.use_getpwnam) {
			char *cursor;
			bool nulled;

			if (group) {
				struct group g;
				struct group *gres;

				rc = getgrgid_r(id, &g, namebuff, new_name.len,
						&gres);
				nulled = (gres == NULL);
			} else {
				struct passwd p;
				struct passwd *pres;

				rc = getpwuid_r(id, &p, namebuff, new_name.len,
						&pres);
				nulled = (pres == NULL);
			}

			if ((rc == 0) && !nulled) {
				new_name.len = strlen(namebuff);
				cursor = namebuff + new_name.len;
				*(cursor++) = '@';
				++new_name.len;
				memcpy(cursor, owner_domain.addr,
				       owner_domain.len);
				new_name.len += owner_domain.len;
				looked_up = true;
			} else {
				LogInfo(COMPONENT_IDMAPPER,
					"%s failed with code %d.",
					(group ? "getgrgid_r" : "getpwuid_r"),
					rc);
			}
		} else {
#ifdef USE_NFSIDMAP
			if (group) {
				rc = nfs4_gid_to_name(id, owner_domain.addr,
						      namebuff,
						      NFS4_MAX_DOMAIN_LEN + 1);
			} else {
				rc = nfs4_uid_to_name(id, owner_domain.addr,
						      namebuff,
						      NFS4_MAX_DOMAIN_LEN + 1);
			}
			if (rc == 0) {
				new_name.len = strlen(namebuff);
				looked_up = true;
			} else {
				LogInfo(COMPONENT_IDMAPPER,
					"%s failed with code %d.",
					(group ? "nfs4_gid_to_name" :
					"nfs4_uid_to_name"), rc);
			}
#else				/* USE_NFSIDMAP */
			looked_up = false;
#endif				/* !USE_NFSIDMAP */
		}

		if (!looked_up) {
			if (nfs_param.nfsv4_param.allow_numeric_owners) {
				LogInfo(COMPONENT_IDMAPPER,
					"Lookup for %d failed, using numeric %s",
					id, (group ? "group" : "owner"));
				/* 2**32 is 10 digits long in decimal */
				new_name.len = sprintf(namebuff, "%"PRIu32, id);
			} else {
				LogInfo(COMPONENT_IDMAPPER,
					"Lookup for %d failed, using nobody.",
					id);
				memcpy(new_name.addr, "nobody", 6);
				new_name.len = 6;
			}
		}

		/* Add to the cache and encode the result. */
		PTHREAD_RWLOCK_wrlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);
		if (group)
			success = idmapper_add_group(&new_name, id);
		else
			success = idmapper_add_user(&new_name, id, NULL, false);

		PTHREAD_RWLOCK_unlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);
		if (unlikely(!success)) {
			LogMajor(COMPONENT_IDMAPPER, "%s failed.",
				 group ? "idmapper_add_group" :
				 "idmaper_add_user");
		}
		not_a_size_t = new_name.len;
		return inline_xdr_bytes(xdrs, (char **)&new_name.addr,
					&not_a_size_t, UINT32_MAX);
	}
}

/**
 * @brief Encode a UID as a string
 *
 * @param[in,out] xdrs  XDR stream to which to encode
 * @param[in]     uid   UID
 *
 * @retval true on success.
 * @retval false on failure.
 */

bool xdr_encode_nfs4_owner(XDR *xdrs, uid_t uid)
{
	return xdr_encode_nfs4_princ(xdrs, uid, false);
}

/**
 * @brief Encode a GID as a string
 *
 * @param[in,out] xdrs  XDR stream to which to encode
 * @param[in]     gid   GID
 *
 * @retval true on success.
 * @retval false on failure.
 */

bool xdr_encode_nfs4_group(XDR *xdrs, gid_t gid)
{
	return xdr_encode_nfs4_princ(xdrs, gid, true);
}

/**
 * @brief Handle unqualified names
 *
 * @param[in]  name C string of name
 * @param[in]  len  Length of name
 * @param[out] id   ID found
 * @param[in]  anon ID to use in case of nobody
 *
 * @return true on success, false on just phoning it in.
 */

static bool atless2id(char *name, size_t len, uint32_t *id,
		      const uint32_t anon)
{
	if ((len == 6) && (!memcmp(name, "nobody", 6))) {
		*id = anon;
		return true;
	} else if (nfs_param.nfsv4_param.allow_numeric_owners) {
		char *end = NULL;
		*id = strtol(name, &end, 10);
		if (!(end && *end != '\0'))
			return true;
	}

	/* Nothing else without an @ is allowed. */
	return false;
}

/**
 * @brief Return gid given a group name
 *
 * @param[in]  name  group name
 * @param[out] gid   address for gid to be filled in
 *
 * @return 0 on success and errno on failure.
 *
 * NOTE: If a group name doesn't exist, getgrnam_r returns 0 with the
 * result pointer set to NULL. We turn that into ENOENT error! Also,
 * getgrnam_r fails with ERANGE if there is a group with a large number
 * of users that it can't fill all those users into the supplied buffer.
 * This need not be the group we are asking for! ERANGE is handled here,
 * so this function never ends up returning ERANGE back to the caller.
 */
static int name_to_gid(const char *name, gid_t *gid)
{
	struct group g;
	struct group *gres = NULL;
	char *buf;
	size_t buflen = sysconf(_SC_GETGR_R_SIZE_MAX);

	/* Upper bound on the buffer length. Just to bailout if there is
	 * a bug in getgrname_r returning ERANGE incorrectly. 64MB
	 * should be good enough for now.
	 */
	size_t maxlen = 64 * 1024 * 1024;
	int err;

	if (buflen == -1)
		buflen = PWENT_BEST_GUESS_LEN;

	do {
		buf = gsh_malloc(buflen);

		err = getgrnam_r(name, &g, buf, buflen, &gres);
		if (err == ERANGE) {
			buflen *= 16;
			gsh_free(buf);
		}
	} while (buflen <= maxlen && err == ERANGE);

	if (err == 0) {
		if (gres == NULL)
			err = ENOENT;
		else
			*gid = gres->gr_gid;
	}

	if (err != ERANGE)
		gsh_free(buf);

	return err;
}

/**
 * @brief Lookup a name using PAM
 *
 * @param[in]  name       C string of name
 * @param[in]  len        Length of name
 * @param[out] id         ID found
 * @param[in]  anon       ID to use in case of nobody
 * @param[in]  group      Whether this a group lookup
 * @param[out] gss_gid    Found GID
 * @param[out] gss_uid    Found UID
 * @apram[out] gotgss_gid Found a GID.
 * @param[in]  at         Location of the @
 *
 * @return true on success, false not making the grade
 */
static bool pwentname2id(char *name, size_t len, uint32_t *id,
			 const uint32_t anon, bool group, gid_t *gid,
			 bool *got_gid, char *at)
{
	if (at != NULL) {
		if (strcmp(at + 1, owner_domain.addr) != 0) {
			/* We won't map what isn't even in the right domain */
			return false;
		}
		*at = '\0';
	}
	if (group) {
		int err;

		err = name_to_gid(name, id);
		if (err == 0)
			return true;
		else if (err != ENOENT) {
			LogWarn(COMPONENT_IDMAPPER,
				"getgrnam_r %s failed, error: %d", name, err);
			return false;
		}
#ifndef USE_NFSIDMAP
		else {
			char *end = NULL;
			gid_t gid;

			gid = strtol(name, &end, 10);
			if (end && *end != '\0')
				return 0;

			*id = gid;
			return true;
		}
#endif
	} else {
		struct passwd p;
		struct passwd *pres;
		int size = sysconf(_SC_GETPW_R_SIZE_MAX);
		char *buf;

		if (size == -1)
			size = PWENT_BEST_GUESS_LEN;

		buf = alloca(size);

		if (getpwnam_r(name, &p, buf, size, &pres) != 0) {
			LogInfo(COMPONENT_IDMAPPER, "getpwnam_r %s failed",
				name);
			return false;
		} else if (pres != NULL) {
			*id = pres->pw_uid;
			*gid = pres->pw_gid;
			*got_gid = true;
			return true;
		}
#ifndef USE_NFSIDMAP
		else {
			char *end = NULL;
			uid_t uid;

			uid = strtol(name, &end, 10);
			if (end && *end != '\0')
				return 0;

			*id = uid;
			*got_gid = false;
			return true;
		}
#endif
	}
	return false;
}

/**
 * @brief Lookup a name NFS ID Mapper
 *
 * @param[in]  name       C string of name
 * @param[in]  len        Length of name
 * @param[out] id         ID found
 * @param[in]  anon       ID to use in case of nobody
 * @param[in]  group      Whether this a group lookup
 * @param[out] gss_gid    Found GID
 * @param[out] gss_uid    Found UID
 * @apram[out] gotgss_gid Found a GID.
 * @param[in]  at         Location of the @
 *
 * @return true on success, false not making the grade
 */

static bool idmapname2id(char *name, size_t len, uint32_t *id,
			 const uint32_t anon, bool group, gid_t *gid,
			 bool *got_gid, char *at)
{
#ifdef USE_NFSIDMAP
	int rc;

	if (group)
		rc = nfs4_name_to_gid(name, id);
	else
		rc = nfs4_name_to_uid(name, id);

	if (rc == 0) {
		return true;
	} else {
		LogInfo(COMPONENT_IDMAPPER,
			"%s %s failed with %d, using anonymous.",
			(group ? "nfs4_name_to_gid" : "nfs4_name_to_uid"), name,
			-rc);
		return false;
	}
#else				/* USE_NFSIDMAP */
	return false;
#endif				/* USE_NFSIDMAP */
}

/**
 * @brief Convert a name to an ID
 *
 * @param[in]  name  The name of the user
 * @param[out] id    The resulting id
 * @param[in]  group True if this is a group name
 * @param[in]  anon  ID to return if look up fails
 *
 * @return true if successful, false otherwise
 */

static bool name2id(const struct gsh_buffdesc *name, uint32_t *id, bool group,
		    const uint32_t anon)
{
	bool success;

	PTHREAD_RWLOCK_rdlock(group ? &idmapper_group_lock :
			      &idmapper_user_lock);
	if (group)
		success = idmapper_lookup_by_gname(name, id);
	else
		success = idmapper_lookup_by_uname(name, id, NULL, false);
	PTHREAD_RWLOCK_unlock(group ? &idmapper_group_lock :
			      &idmapper_user_lock);

	if (success)
		return true;
	else {
		gid_t gid;
		bool got_gid = false;
		/* Something we can mutate and count on as terminated */
		char *namebuff = alloca(name->len + 1);
		char *at;
		bool looked_up = false;

		memcpy(namebuff, name->addr, name->len);
		*(namebuff + name->len) = '\0';
		at = memchr(namebuff, '@', name->len);

		if (at == NULL) {
			if (pwentname2id
			    (namebuff, name->len, id, anon, group, &gid,
			     &got_gid, NULL))
				looked_up = true;
			else if (atless2id(namebuff, name->len, id, anon))
				looked_up = true;
			else
				return false;
		} else if (nfs_param.nfsv4_param.use_getpwnam) {
			looked_up =
			    pwentname2id(namebuff, name->len, id, anon, group,
					 &gid, &got_gid, at);
		} else {
			looked_up =
			    idmapname2id(namebuff, name->len, id, anon, group,
					 &gid, &got_gid, at);
		}

		if (!looked_up) {
			LogInfo(COMPONENT_IDMAPPER,
				"All lookups failed for %s, using anonymous.",
				namebuff);
			*id = anon;
		}

		PTHREAD_RWLOCK_wrlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);
		if (group)
			success = idmapper_add_group(name, *id);
		else
			success =
			    idmapper_add_user(name, *id, got_gid ? &gid : NULL,
					      false);

		PTHREAD_RWLOCK_unlock(group ? &idmapper_group_lock :
				      &idmapper_user_lock);

		if (!success)
			LogMajor(COMPONENT_IDMAPPER, "%s(%s %u) failed",
				 (group ? "gidmap_add" : "uidmap_add"),
				 namebuff, *id);
		return true;
	}
}

/**
 * @brief Convert a name to a uid
 *
 * @param[in]  name The name of the user
 * @param[out] uid  The resulting UID
 * @param[in]  anon The anonymous UID for this export
 *
 * @return true if successful, false otherwise
 *
 */
bool name2uid(const struct gsh_buffdesc *name, uid_t *uid, const uid_t anon)
{
	return name2id(name, uid, false, anon);
}

/**
 * @brief Convert a name to a GID
 *
 * @param[in]  name The name of the user
 * @param[out] gid  The resulting GID
 * @param[in]  anon The anonymous GID for this export
 *
 * @return true  if successful, false otherwise
 */
bool name2gid(const struct gsh_buffdesc *name, gid_t *gid, const gid_t anon)
{
	return name2id(name, gid, true, anon);
}

void winbind_stats_update(struct timespec *s_time, struct timespec *e_time)
{
	nsecs_elapsed_t resp_time;

	resp_time = timespec_diff(s_time, e_time);

	PTHREAD_RWLOCK_wrlock(&winbind_auth_lock);
	(void)atomic_inc_uint64_t(&winbind_auth_stats.total);
	(void)atomic_add_uint64_t(&winbind_auth_stats.latency,
					resp_time);
	if (winbind_auth_stats.max < resp_time)
		winbind_auth_stats.max = resp_time;
	if (winbind_auth_stats.min == 0 ||
	    winbind_auth_stats.min > resp_time)
		winbind_auth_stats.min = resp_time;
	PTHREAD_RWLOCK_unlock(&winbind_auth_lock);
}

void gc_stats_update(struct timespec *s_time, struct timespec *e_time)
{
	nsecs_elapsed_t resp_time;

	resp_time = timespec_diff(s_time, e_time);

	PTHREAD_RWLOCK_wrlock(&gc_auth_lock);
	(void)atomic_inc_uint64_t(&gc_auth_stats.total);
	(void)atomic_add_uint64_t(&gc_auth_stats.latency,
					resp_time);
	if (gc_auth_stats.max < resp_time)
		gc_auth_stats.max = resp_time;
	if (gc_auth_stats.min == 0 ||
	    gc_auth_stats.min > resp_time)
		gc_auth_stats.min = resp_time;
	PTHREAD_RWLOCK_unlock(&gc_auth_lock);
}

void dns_stats_update(struct timespec *s_time, struct timespec *e_time)
{
	nsecs_elapsed_t resp_time;

	resp_time = timespec_diff(s_time, e_time);

	PTHREAD_RWLOCK_wrlock(&dns_auth_lock);
	(void)atomic_inc_uint64_t(&dns_auth_stats.total);
	(void)atomic_add_uint64_t(&dns_auth_stats.latency,
					resp_time);
	if (dns_auth_stats.max < resp_time)
		dns_auth_stats.max = resp_time;
	if (dns_auth_stats.min == 0 ||
	    dns_auth_stats.min > resp_time)
		dns_auth_stats.min = resp_time;
	PTHREAD_RWLOCK_unlock(&dns_auth_lock);
}


void reset_auth_stats(void)
{
	PTHREAD_RWLOCK_wrlock(&winbind_auth_lock);
	winbind_auth_stats.total = 0;
	winbind_auth_stats.latency = 0;
	winbind_auth_stats.max = 0;
	winbind_auth_stats.min = 0;
	PTHREAD_RWLOCK_unlock(&winbind_auth_lock);

	PTHREAD_RWLOCK_wrlock(&gc_auth_lock);
	gc_auth_stats.total = 0;
	gc_auth_stats.latency = 0;
	gc_auth_stats.max = 0;
	gc_auth_stats.min = 0;
	PTHREAD_RWLOCK_unlock(&gc_auth_lock);

	PTHREAD_RWLOCK_wrlock(&dns_auth_lock);
	dns_auth_stats.total = 0;
	dns_auth_stats.latency = 0;
	dns_auth_stats.max = 0;
	dns_auth_stats.min = 0;
	PTHREAD_RWLOCK_unlock(&dns_auth_lock);
}

#ifdef _HAVE_GSSAPI
#ifdef _MSPAC_SUPPORT
/**
 * @brief Convert a principal (as returned by @c gss_display_name) to a UID
 *
 * @param[in]     name The principal of the user
 * @param[out]    uid The resulting UID
 * @param[in,out] gd   GSS data
 *
 * @return true if successful, false otherwise
 */
bool principal2uid(char *principal, uid_t *uid, gid_t *gid,
		   struct svc_rpc_gss_data *gd)
#else
/**
 * @brief Convert a principal (as returned by @c gss_display_name) to a UID
 *
 * @param[in]     name The principal of the user
 * @param[out]    uid The resulting UID
 *
 * @return true if successful, false otherwise
 */
bool principal2uid(char *principal, uid_t *uid, gid_t *gid)
#endif
{
#ifdef USE_NFSIDMAP
#ifdef _MSPAC_SUPPORT
	struct timespec s_time, e_time;
	bool stats = false;
	struct passwd *pwd;
#endif
	uid_t gss_uid = -1;
	gid_t gss_gid = -1;
	const gid_t *gss_gidres = NULL;
	int rc;
	bool success;
	struct gsh_buffdesc princbuff = {
		.addr = principal,
		.len = strlen(principal)
	};
#endif

	if (nfs_param.nfsv4_param.use_getpwnam)
		return false;
#ifdef USE_NFSIDMAP
#ifdef _MSPAC_SUPPORT
	if (nfs_param.core_param.enable_AUTHSTATS)
		stats = true;
#endif

	PTHREAD_RWLOCK_rdlock(&idmapper_user_lock);
	success =
	    idmapper_lookup_by_uname(&princbuff, &gss_uid, &gss_gidres, true);

	/* We do need uid and gid. If gid is not in the cache, treat it as a
	 * failure.
	 */
	if (success && gss_gidres != NULL)
		gss_gid = *gss_gidres;
	else
		success = false;
	PTHREAD_RWLOCK_unlock(&idmapper_user_lock);
	if (unlikely(!success)) {
		if ((princbuff.len >= 4)
		    && (!memcmp(princbuff.addr, "nfs/", 4)
			|| !memcmp(princbuff.addr, "root/", 5)
			|| !memcmp(princbuff.addr, "host/", 5))) {
			/* NFSv4 specific features: RPCSEC_GSS will
			 * provide user like
			 *
			 * nfs/<host>
			 * root/<host>
			 * host/<host>
			 * choice is made to map them to root */
			/* This is a "root" request made from the
			   hostbased nfs principal, use root */
			*uid = 0;
			*gid = 0;
			return true;
		}
		/* nfs4_gss_princ_to_ids required to extract uid/gid
		   from gss creds */
		rc = nfs4_gss_princ_to_ids("krb5", principal, &gss_uid,
					   &gss_gid);
		if (rc) {
#ifdef _MSPAC_SUPPORT
			bool found_uid = false;
			bool found_gid = false;

			if (gd->flags & SVC_RPC_GSS_FLAG_MSPAC) {
				struct wbcAuthUserParams params;
				wbcErr wbc_err;
				struct wbcAuthUserInfo *info;
				struct wbcAuthErrorInfo *error = NULL;

				memset(&params, 0, sizeof(params));
				params.level = WBC_AUTH_USER_LEVEL_PAC;
				params.password.pac.data =
				    (uint8_t *) gd->pac.ms_pac.value;
				params.password.pac.length =
				    gd->pac.ms_pac.length;

				now(&s_time);
				wbc_err =
				    wbcAuthenticateUserEx(&params, &info,
							  &error);
				now(&e_time);
				if (stats)
					winbind_stats_update(&s_time, &e_time);
				if (!WBC_ERROR_IS_OK(wbc_err)) {
					LogInfo(COMPONENT_IDMAPPER,
						"wbcAuthenticateUserEx returned %s",
						wbcErrorString(wbc_err));
					return false;
				}

				if (error) {
					LogInfo(COMPONENT_IDMAPPER,
						"nt_status: %s, display_string %s",
						error->nt_string,
						error->display_string);
					wbcFreeMemory(error);
					return false;
				}

				now(&s_time);
				wbc_err =
					wbcGetpwsid(&info->sids[0].sid,
						    &pwd);
				now(&e_time);
				if (stats)
					winbind_stats_update(&s_time, &e_time);
				if (!WBC_ERROR_IS_OK(wbc_err)) {
					LogInfo(COMPONENT_IDMAPPER,
						"wbcGetpwsid returned %s",
						wbcErrorString(wbc_err));
					wbcFreeMemory(info);
					return false;
				}

				gss_uid = pwd->pw_uid;
				gss_gid = pwd->pw_gid;

				wbcFreeMemory(info);
				found_uid = true;
				found_gid = true;
			}
#endif				/* _MSPAC_SUPPORT */
#ifdef _MSPAC_SUPPORT
			if ((found_uid == true) && (found_gid == true))
				goto principal_found;
#endif

			return false;
		}
#ifdef _MSPAC_SUPPORT
 principal_found:
#endif

		PTHREAD_RWLOCK_wrlock(&idmapper_user_lock);
		success =
		    idmapper_add_user(&princbuff, gss_uid, &gss_gid, true);
		PTHREAD_RWLOCK_unlock(&idmapper_user_lock);

		if (!success) {
			LogMajor(COMPONENT_IDMAPPER,
				 "idmapper_add_user(%s, %d, %d) failed",
				 principal, gss_uid, gss_gid);
		}
	}

	*uid = gss_uid;
	*gid = gss_gid;

	return true;
#else				/* !USE_NFSIDMAP */
	assert(!"prohibited by configuration");
	return false;
#endif
}

#ifdef USE_DBUS

/**
 * DBUS method to collect Auth stats for group cache and winbind
 */
static bool all_auth_stats(DBusMessageIter *args, DBusMessage *reply,
				DBusError *error)
{
	bool success = true, stats_exist = false;
	char *errormsg = "OK";
	DBusMessageIter iter, struct_iter;
	double res = 0.0;

	dbus_message_iter_init_append(reply, &iter);
	if (!nfs_param.core_param.enable_AUTHSTATS) {
		success = false;
		errormsg = "auth related stats disabled";
		gsh_dbus_status_reply(&iter, success, errormsg);
		return true;
	}
	gsh_dbus_status_reply(&iter, success, errormsg);

	gsh_dbus_append_timestamp(&iter, &auth_stats_time);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT,
		NULL, &struct_iter);

	/* group cache stats */
	PTHREAD_RWLOCK_rdlock(&gc_auth_lock);
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_UINT64, &gc_auth_stats.total);
	if (gc_auth_stats.total > 0) {
		stats_exist = true;
		res = (double) gc_auth_stats.latency /
			gc_auth_stats.total * 0.000001;
	}
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) gc_auth_stats.max * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) gc_auth_stats.min * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	PTHREAD_RWLOCK_unlock(&gc_auth_lock);

	stats_exist = false;
	res = 0.0;

	/* winbind stats */
	PTHREAD_RWLOCK_rdlock(&winbind_auth_lock);
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_UINT64, &winbind_auth_stats.total);
	if (winbind_auth_stats.total > 0) {
		stats_exist = true;
		res = (double) winbind_auth_stats.latency /
			winbind_auth_stats.total * 0.000001;
	}
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) winbind_auth_stats.max * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) winbind_auth_stats.min * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	PTHREAD_RWLOCK_unlock(&winbind_auth_lock);

	stats_exist = false;
	res = 0.0;

	/* DNS stats */
	PTHREAD_RWLOCK_rdlock(&dns_auth_lock);
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_UINT64, &dns_auth_stats.total);
	if (dns_auth_stats.total > 0) {
		stats_exist = true;
		res = (double) dns_auth_stats.latency /
			dns_auth_stats.total * 0.000001;
	}
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) dns_auth_stats.max * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	if (stats_exist)
		res = (double) dns_auth_stats.min * 0.000001;
	dbus_message_iter_append_basic(&struct_iter,
		DBUS_TYPE_DOUBLE, &res);
	PTHREAD_RWLOCK_unlock(&dns_auth_lock);
	dbus_message_iter_close_container(&iter, &struct_iter);

	return true;
}

struct gsh_dbus_method auth_statistics = {
	.name = "GetAuthStats",
	.method = all_auth_stats,
	.args = {STATUS_REPLY,
		 TIMESTAMP_REPLY,
		 AUTH_REPLY,
		 END_ARG_LIST}
};
#endif
#endif

/** @} */
