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
 * @addtogroup uid2grp
 * @{
 */

/**
 * @file uid2grp.c
 * @brief Uid to group list conversion
 */

#include "config.h"
#include "gsh_rpc.h"
#include "nfs_core.h"
#include <unistd.h>		/* for using gethostname */
#include <stdlib.h>		/* for using exit */
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>
#include <stdbool.h>
#include "common_utils.h"
#include "uid2grp.h"
#include "idmapper.h"
#ifdef USE_NFSIDMAP
#include <nfsidmap.h>
#endif

/* Switch to enable or disable idmapping */
extern bool idmapping_enabled;

sem_t uid2grp_sem;

/* group_data has a reference counter. If it goes to zero, it implies
 * that it is out of the cache (AVL trees) and should be freed. The
 * reference count is 1 when we put it into AVL trees. We decrement when
 * we take it out of AVL trees. Also incremented when we pass this to
 * out siders (uid2grp and friends) and decremented when they are done
 * (in uid2grp_unref()).
 *
 * When a group_data needs to be removed or expired after a certain
 * timeout, we take it out of the cache (AVL trees). When everyone using
 * the group_data are done, the refcount will go to zero at which point
 * we free group_data as well as the buffer holding supplementary
 * groups.
 */
void uid2grp_hold_group_data(struct group_data *gdata)
{
	PTHREAD_MUTEX_lock(&gdata->gd_lock);
	gdata->refcount++;
	PTHREAD_MUTEX_unlock(&gdata->gd_lock);
}

void uid2grp_release_group_data(struct group_data *gdata)
{
	unsigned int refcount;

	PTHREAD_MUTEX_lock(&gdata->gd_lock);
	refcount = --gdata->refcount;
	PTHREAD_MUTEX_unlock(&gdata->gd_lock);

	if (refcount == 0) {
		PTHREAD_MUTEX_destroy(&gdata->gd_lock);
		gsh_free(gdata->groups);
		gsh_free(gdata);
	} else if (refcount == (unsigned int)-1) {
		LogAlways(COMPONENT_IDMAPPER, "negative refcount on gdata: %p",
			  gdata);
	}
}

/* Allocate supplementary groups buffer */
static bool my_getgrouplist_alloc(char *user,
				  gid_t gid,
				  struct group_data *gdata)
{
	int ngroups = 1000;
	gid_t *groups = NULL;
	struct timespec s_time, e_time;
	bool stats = nfs_param.core_param.enable_AUTHSTATS;

	/* We call getgrouplist() with ngroups set to 1000 first. This should
	 * reduce the number of getgrouplist() calls made to 1, for most cases.
	 * However, getgrouplist() return -1 if the actual number of groups the
	 * user is in, is more than 1000 (very rare) and ngroups will be set to
	 * the actual number of groups the user is in. We can then make a second
	 * query to fetch all the groups when ngroups is greater than 1000.
	 *
	 * The manpage doesn't say anything about errno value, it was usually
	 * zero but was set to 34 (ERANGE) under some environments. ngroups was
	 * set correctly no matter what the errno value is!
	 * We assume that ngroups is correctly set, no matter what the
	 * errno value is. The man page says, "The ngroups argument
	 * is a value-result argument: on  return  it always contains
	 * the  number  of  groups found for user."
	 */
	groups = gsh_malloc(ngroups * sizeof(gid_t));

	now(&s_time);
	if (getgrouplist(user, gid, groups, &ngroups) == -1) {
		LogEvent(COMPONENT_IDMAPPER,
			 "getgrouplist for user: %s failed retrying", user);

		gsh_free(groups);

		/* Try with the actual ngroups if user is part of more than 1000
		 * groups. */
		groups = gsh_malloc(ngroups * sizeof(gid_t));

		now(&s_time);
		if (getgrouplist(user, gid, groups, &ngroups) == -1) {
			LogWarn(COMPONENT_IDMAPPER,
				"getgrouplist for user:%s failed, ngroups: %d",
				user, ngroups);
			gsh_free(groups);
			return false;
		}

		now(&e_time);
		if (stats) {
			gc_stats_update(&s_time, &e_time);
			stats = false;
		}
	}

	if (ngroups != 0) {
		/* Resize the buffer, if it fails, gsh_realloc will
		 * abort.
		 */
		groups = gsh_realloc(groups, ngroups * sizeof(gid_t));
	} else {
		/* We need to free groups because later code may not. */
		gsh_free(groups);
		groups = NULL;
	}

	now(&e_time);
	if (stats)
		gc_stats_update(&s_time, &e_time);
	gdata->groups = groups;
	gdata->nbgroups = ngroups;

	return true;
}

/* Allocate and fill in group_data structure */
static struct group_data *uid2grp_allocate_by_name(
		const struct gsh_buffdesc *name)
{
	struct passwd p;
	struct passwd *pp;
	char *namebuff = alloca(name->len + 1);
	struct group_data *gdata = NULL;
	char *buff;
	size_t buff_size;
	int retval;

	memcpy(namebuff, name->addr, name->len);
	*(namebuff + name->len) = '\0';

	buff_size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (buff_size == -1) {
		LogMajor(COMPONENT_IDMAPPER, "sysconf failure: %d", errno);
		buff_size = PWENT_BEST_GUESS_LEN;
	}

	while (buff_size <= PWENT_MAX_SIZE) {
		buff = gsh_malloc(buff_size);
		retval = getpwnam_r(namebuff, &p, buff, buff_size, &pp);

		if (retval != ERANGE)
			break;
		gsh_free(buff);
		buff_size *= 16;
	}

	if (retval == ERANGE) {
		LogWarn(COMPONENT_IDMAPPER,
			"Received ERANGE when fetching pw-entry from name: %s",
			namebuff);
		return NULL;
	}

	if (retval != 0) {
		LogEvent(COMPONENT_IDMAPPER,
			 "getpwnam_r for %s failed, error %d",
			 namebuff, retval);
		goto out;
	}
	if (pp == NULL) {
		LogEvent(COMPONENT_IDMAPPER,
			 "No matching password record found for name %s",
			 namebuff);
		goto out;
	}

	gdata = gsh_malloc(sizeof(struct group_data) + strlen(p.pw_name));

	gdata->uname.len = strlen(p.pw_name);
	gdata->uname.addr = (char *)gdata + sizeof(struct group_data);
	memcpy(gdata->uname.addr, p.pw_name, gdata->uname.len);
	gdata->uid = p.pw_uid;
	gdata->gid = p.pw_gid;

	/* Throttle getgrouplist queries to Directory Server if required. */
	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_wait(&uid2grp_sem);

	if (!my_getgrouplist_alloc(p.pw_name, p.pw_gid, gdata)) {
		gsh_free(gdata);
		gdata = NULL;
		if (nfs_param.core_param.max_uid_to_grp_reqs)
			sem_post(&uid2grp_sem);
		goto out;
	}

	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_post(&uid2grp_sem);

	PTHREAD_MUTEX_init(&gdata->gd_lock, NULL);
	gdata->epoch = time(NULL);
	gdata->refcount = 0;

out:
	gsh_free(buff);
	return gdata;
}

/* Allocate and fill in group_data structure */
static struct group_data *uid2grp_allocate_by_uid(uid_t uid)
{
	struct passwd p;
	struct passwd *pp;
	struct group_data *gdata = NULL;
	char *buff;
	size_t buff_size;
	int retval;

	buff_size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (buff_size == -1) {
		LogMajor(COMPONENT_IDMAPPER, "sysconf failure: %d", errno);
		buff_size = PWENT_BEST_GUESS_LEN;
	}

	while (buff_size <= PWENT_MAX_SIZE) {
		buff = gsh_malloc(buff_size);
		retval = getpwuid_r(uid, &p, buff, buff_size, &pp);

		if (retval != ERANGE)
			break;
		gsh_free(buff);
		buff_size *= 16;
	}

	if (retval == ERANGE) {
		LogWarn(COMPONENT_IDMAPPER,
			"Received ERANGE when fetching pw-entry from uid: %u",
			uid);
		return NULL;
	}

	if (retval != 0) {
		LogEvent(COMPONENT_IDMAPPER,
			 "getpwuid_r for uid %u failed, error %d", uid, retval);
		goto out;
	}
	if (pp == NULL) {
		LogInfo(COMPONENT_IDMAPPER,
			"No matching password record found for uid %u", uid);
		goto out;
	}

	gdata = gsh_malloc(sizeof(struct group_data) + strlen(p.pw_name));

	gdata->uname.len = strlen(p.pw_name);
	gdata->uname.addr = (char *)gdata + sizeof(struct group_data);
	memcpy(gdata->uname.addr, p.pw_name, gdata->uname.len);
	gdata->uid = p.pw_uid;
	gdata->gid = p.pw_gid;

	/* Throttle getgrouplist queries to Directory Server if required. */
	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_wait(&uid2grp_sem);

	if (!my_getgrouplist_alloc(p.pw_name, p.pw_gid, gdata)) {
		gsh_free(gdata);
		gdata = NULL;
		if (nfs_param.core_param.max_uid_to_grp_reqs)
			sem_post(&uid2grp_sem);
		goto out;
	}

	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_post(&uid2grp_sem);

	PTHREAD_MUTEX_init(&gdata->gd_lock, NULL);
	gdata->epoch = time(NULL);
	gdata->refcount = 0;

out:
	gsh_free(buff);
	return gdata;
}

/**
 * @brief Allocate supplementary groups using principal
 *
 * @note This function uses libnfsidmap internally
 *
 * @param[in]  principal The principal name
 * @param[in]  uid       The uid of user represented by the principal
 * @param[in]  gid       The gid of user represented by the principal
 *
 * @return group_data with fetched groups. It can be NULL on lookup or
 * allocation failures.
 */
static struct group_data *uid2grp_allocate_by_principal(char *principal,
	uid_t uid, gid_t gid)
{
#ifdef USE_NFSIDMAP
	struct group_data *grpdata = NULL;
	const int default_ngroups = 1000;
	int ngroups = default_ngroups;
	gid_t *groups = NULL;
	int ret;

#ifdef _MSPAC_SUPPORT
	/* TODO */
	LogWarn(COMPONENT_IDMAPPER,
		"Unsupported code path for principal %s", principal);
	return NULL;
#endif

	/* We call nfs4_gss_princ_to_grouplist() with ngroups set to 1000 first.
	 * This should reduce number of nfs4_gss_princ_to_grouplist() calls made
	 * to 1, for most cases. However, nfs4_gss_princ_to_grouplist() returns
	 * -ERANGE if the actual number of groups the user is in, is more
	 * than 1000 (very rare) and ngroups will be set to the actual number of
	 * groups the user is in. We can then make a second query to fetch all
	 * the groups when ngroups is greater than 1000.
	 */
	groups = gsh_malloc(ngroups * sizeof(gid_t));
	ret = nfs4_gss_princ_to_grouplist("krb5", principal, groups, &ngroups);

	if (ret == -ERANGE) {
		/* Try with the actual ngroups since user is part of more than
		 * 1000 groups
		 */
		gsh_free(groups);
		groups = gsh_malloc(ngroups * sizeof(gid_t));

		ret = nfs4_gss_princ_to_grouplist("krb5", principal, groups,
			&ngroups);
		if (ret) {
			LogWarn(COMPONENT_IDMAPPER,
				"Could not re-resolve principal %s to groups using nfsidmap, err: %d",
				principal, ret);
			gsh_free(groups);
			return NULL;
		}
	} else if (ret) {
		LogWarn(COMPONENT_IDMAPPER,
			"Could not resolve principal %s to groups using nfsidmap, err: %d",
			principal, ret);
		gsh_free(groups);
		return NULL;
	}
	LogDebug(COMPONENT_IDMAPPER,
		"Resolved principal %s to %d groups using nfsidmap",
		principal, ngroups);

	/* Resize or free the buffer as appropriate */
	if (ngroups == 0) {
		gsh_free(groups);
		groups = NULL;
	} else if (ngroups < default_ngroups) {
		groups = gsh_realloc(groups, ngroups * sizeof(gid_t));
	}

	grpdata = gsh_malloc(sizeof(struct group_data) + strlen(principal) + 1);
	/* We populate principal as the uname here */
	grpdata->uname.len = strlen(principal);
	grpdata->uname.addr = (char *)grpdata + sizeof(struct group_data);
	memcpy(grpdata->uname.addr, principal, grpdata->uname.len);
	/* Null-terminate the uname string */
	((char *) grpdata->uname.addr)[grpdata->uname.len] = 0;
	grpdata->uid = uid;
	grpdata->gid = gid;
	grpdata->groups = groups;
	grpdata->nbgroups = ngroups;
	PTHREAD_MUTEX_init(&grpdata->gd_lock, NULL);
	grpdata->epoch = time(NULL);
	grpdata->refcount = 0;

	return grpdata;
#else
	LogWarn(COMPONENT_IDMAPPER, "Invalid code path");
	return NULL;
#endif
}

/**
 * @brief Add a user entry to the cache
 *
 * @param[in] group_data user entry with allocated supplementary groups
 */
static void add_user_groups_to_cache(struct group_data **gdata)
{
	/* Do not add to cache if idmapping is disabled */
	if (!idmapping_enabled) {
		LogWarn(COMPONENT_IDMAPPER,
			"Idmapping is disabled, add-to-cache skipped");
		return;
	}

	PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);

	/* Recheck if idmapping is enabled because it is possible that after
	 * cache cleanup completes (resulting from the disabling of idmapping),
	 * there are waiting requests with older idmapping data that might end
	 * up writing their data to the cache. We need to stop them.
	*/
	if (!idmapping_enabled) {
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		LogWarn(COMPONENT_IDMAPPER,
			"Idmapping is disabled, add-to-cache skipped");
		return;
	}
	uid2grp_add_user(*gdata);
	uid2grp_hold_group_data(*gdata);
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
}

/**
 * @brief Get supplementary groups given uname
 *
 * @param[in]  name       The name of the user
 * @param[out] group_data The group data of the user
 *
 * @return true if successful, false otherwise
 */
bool name2grp(const struct gsh_buffdesc *name, struct group_data **gdata)
{
	bool success = false;
	uid_t uid = -1;

	if (!idmapping_enabled) {
		LogWarn(COMPONENT_IDMAPPER,
			"Idmapping is disabled, name-to-group skipped");
		return false;
	}

	PTHREAD_RWLOCK_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uname(name, &uid, gdata);

	/* Return success if we find non-expired group-data in cache */
	if (success && !uid2grp_is_group_data_expired(*gdata)) {
		uid2grp_hold_group_data(*gdata);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		return success;
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	/* We could not find non-expired group-data in cache, fetch it afresh */
	*gdata = uid2grp_allocate_by_name(name);
	if (*gdata) {
		/* This will also remove existing expired cache entry */
		add_user_groups_to_cache(gdata);
		return true;
	}

	/*
	 * At this point, we could not find non-expired group-data in cache,
	 * and we also weren't able to fetch fresh group-data.
	 * If the group-data in cache is expired, we stll want to remove it.
	 */
	if (success) {
		/* Remove expired cache entry */
		PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
		uid2grp_remove_expired_by_uname(name);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	}
	return false;
}

/**
 * @brief Get supplementary groups given uid
 *
 * @param[in]   uid        The uid of the user
 * @param[out]  group_data The group data of the user
 *
 * @return true if successful, false otherwise
 */
bool uid2grp(uid_t uid, struct group_data **gdata)
{
	bool success = false;

	if (!idmapping_enabled) {
		LogWarn(COMPONENT_IDMAPPER,
			"Idmapping is disabled, uid-to-group skipped");
		return false;
	}

	PTHREAD_RWLOCK_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uid(uid, gdata);

	/* Return success if we find non-expired group-data in cache */
	if (success && !uid2grp_is_group_data_expired(*gdata)) {
		uid2grp_hold_group_data(*gdata);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		return success;
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	/* We could not find non-expired group-data in cache, fetch it afresh */
	*gdata = uid2grp_allocate_by_uid(uid);
	if (*gdata) {
		/* This will also remove existing expired cache entry */
		add_user_groups_to_cache(gdata);
		return true;
	}

	/*
	 * At this point, we could not find non-expired group-data in cache,
	 * and we also weren't able to fetch fresh group-data.
	 * If the group-data in cache is expired, we still want to remove it.
	 */
	if (success) {
		/* Remove expired cache entry */
		PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
		uid2grp_remove_expired_by_uid(uid);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	}
	return false;
}

/**
 * @brief Get supplementary groups given principal
 *
 * @note This function internally uses libnfsidmap functions
 *
 * @param[in]  principal The principal name
 * @param[in]  uid       The uid of user represented by the principal
 * @param[in]  gid       The gid of user represented by the principal
 * @param[out] gdata     Filled group_data structure containing user groups
 *
 * @return true if successful, false otherwise
 */
bool principal2grp(char *principal, struct group_data **gdata,
	const uid_t uid, const gid_t gid)
{
	bool success = false;
	uid_t unused_cached_uid = -1;
	struct gsh_buffdesc princbuff = {
		.addr = principal,
		.len = strlen(principal)
	};
	LogDebug(COMPONENT_IDMAPPER, "Resolve principal %s to groups",
		principal);

	if (!idmapping_enabled) {
		LogWarn(COMPONENT_IDMAPPER,
			"Idmapping is disabled, principal-to-group skipped");
		return false;
	}

	PTHREAD_RWLOCK_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uname(&princbuff, &unused_cached_uid,
		gdata);

	/* Return success if we find non-expired group-data in cache */
	if (success && !uid2grp_is_group_data_expired(*gdata)) {
		uid2grp_hold_group_data(*gdata);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		return true;
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	/* We could not find non-expired group-data in cache, fetch it afresh */
	*gdata = uid2grp_allocate_by_principal(principal, uid, gid);
	if (*gdata) {
		/* This will also remove existing expired cache entry */
		add_user_groups_to_cache(gdata);
		return true;
	}

	/*
	 * At this point, we could not find non-expired group-data in cache,
	 * and we also weren't able to fetch fresh group-data.
	 * If the group-data in cache is expired, we still want to remove it.
	 */
	if (success) {
		/* Remove expired cache entry */
		PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
		uid2grp_remove_expired_by_uname(&princbuff);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	}
	return false;
}

/*
 * All callers of uid2grp(), uname2grp() and principal2grp() must call
 * this when they are done accessing supplementary groups
 */
void uid2grp_unref(struct group_data *gdata)
{
	uid2grp_release_group_data(gdata);
}

/** @} */
