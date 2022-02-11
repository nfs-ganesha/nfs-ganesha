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
	PTHREAD_MUTEX_lock(&gdata->lock);
	gdata->refcount++;
	PTHREAD_MUTEX_unlock(&gdata->lock);
}

void uid2grp_release_group_data(struct group_data *gdata)
{
	unsigned int refcount;

	PTHREAD_MUTEX_lock(&gdata->lock);
	refcount = --gdata->refcount;
	PTHREAD_MUTEX_unlock(&gdata->lock);

	if (refcount == 0) {
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
	long buff_size;
	int retval;

	memcpy(namebuff, name->addr, name->len);
	*(namebuff + name->len) = '\0';

	buff_size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (buff_size == -1) {
		LogMajor(COMPONENT_IDMAPPER, "sysconf failure: %d", errno);
		return NULL;
	}

	buff = alloca(buff_size);
	retval = getpwnam_r(namebuff, &p, buff, buff_size, &pp);
	if (retval != 0) {
		LogEvent(COMPONENT_IDMAPPER,
			 "getpwnam_r for %s failed, error %d",
			 namebuff, retval);
		return gdata;
	}
	if (pp == NULL) {
		LogEvent(COMPONENT_IDMAPPER,
			 "No matching password record found for name %s",
			 namebuff);
		return gdata;
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
		if (nfs_param.core_param.max_uid_to_grp_reqs)
			sem_post(&uid2grp_sem);
		return NULL;
	}

	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_post(&uid2grp_sem);

	PTHREAD_MUTEX_init(&gdata->lock, NULL);
	gdata->epoch = time(NULL);
	gdata->refcount = 0;
	return gdata;
}

/* Allocate and fill in group_data structure */
static struct group_data *uid2grp_allocate_by_uid(uid_t uid)
{
	struct passwd p;
	struct passwd *pp;
	struct group_data *gdata = NULL;
	char *buff;
	long buff_size;
	int retval;

	buff_size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (buff_size == -1) {
		LogMajor(COMPONENT_IDMAPPER, "sysconf failure: %d", errno);
		return NULL;
	}

	buff = alloca(buff_size);
	retval = getpwuid_r(uid, &p, buff, buff_size, &pp);
	if (retval != 0) {
		LogEvent(COMPONENT_IDMAPPER,
			 "getpwuid_r for uid %u failed, error %d", uid, retval);
		return gdata;
	}
	if (pp == NULL) {
		LogInfo(COMPONENT_IDMAPPER,
			"No matching password record found for uid %u", uid);
		return gdata;
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
		if (nfs_param.core_param.max_uid_to_grp_reqs)
			sem_post(&uid2grp_sem);
		return NULL;
	}

	if (nfs_param.core_param.max_uid_to_grp_reqs)
		sem_post(&uid2grp_sem);

	PTHREAD_MUTEX_init(&gdata->lock, NULL);
	gdata->epoch = time(NULL);
	gdata->refcount = 0;
	return gdata;
}

/**
 * @brief Get supplementary groups given uname
 *
 * @param[in]  name  The name of the user
 * @param[out]  group_data
 *
 * @return true if successful, false otherwise
 */
#define uid2grp_expired(gdata) (time(NULL) - (gdata)->epoch > \
		nfs_param.core_param.manage_gids_expiration)
bool name2grp(const struct gsh_buffdesc *name, struct group_data **gdata)
{
	bool success = false;
	uid_t uid = -1;

	PTHREAD_RWLOCK_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uname(name, &uid, gdata);

	/* Handle common case first */
	if (success && !uid2grp_expired(*gdata)) {
		uid2grp_hold_group_data(*gdata);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		return success;
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	if (success) {
		/* Cache entry is expired */
		PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
		uid2grp_remove_by_uname(name);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	}

	*gdata = uid2grp_allocate_by_name(name);
	PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
	if (*gdata)
		uid2grp_add_user(*gdata);
	success = uid2grp_lookup_by_uname(name, &uid, gdata);
	if (success)
		uid2grp_hold_group_data(*gdata);
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	return success;
}

/**
 * @brief Get supplementary groups given uid
 *
 * @param[in]  uid  The uid of the user
 * @param[out]  group_data
 *
 * @return true if successful, false otherwise
 */
bool uid2grp(uid_t uid, struct group_data **gdata)
{
	bool success = false;

	PTHREAD_RWLOCK_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uid(uid, gdata);

	/* Handle common case first */
	if (success && !uid2grp_expired(*gdata)) {
		uid2grp_hold_group_data(*gdata);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
		return success;
	}
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	if (success) {
		/* Cache entry is expired */
		PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
		uid2grp_remove_by_uid(uid);
		PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);
	}

	*gdata = uid2grp_allocate_by_uid(uid);
	PTHREAD_RWLOCK_wrlock(&uid2grp_user_lock);
	if (*gdata)
		uid2grp_add_user(*gdata);
	success = uid2grp_lookup_by_uid(uid, gdata);
	if (success)
		uid2grp_hold_group_data(*gdata);
	PTHREAD_RWLOCK_unlock(&uid2grp_user_lock);

	return success;
}

/*
 * All callers of uid2grp() and uname2grp must call this
 * when they are done accessing supplementary groups
 */
void uid2grp_unref(struct group_data *gdata)
{
	uid2grp_release_group_data(gdata);
}

/** @} */
