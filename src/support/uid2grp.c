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
#include "ganesha_rpc.h"
#include "nfs_core.h"
#include "nfs_tools.h"
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

static bool my_getgrouplist_alloc(char *user,
				  gid_t gid,
				  struct group_data *gdata,
				  bool renew)
{
	int nbgrp = 0;
	gid_t *groups = NULL;

	/* Step 1 : call getgrouplist with a 0 size
	 * getgrouplist() will return the needed size.
	 * This call WILL fail (see manpages), but errno
	 * won't be set and nbgrp will contain the right value */
	getgrouplist(user, gid, NULL, &nbgrp);
	if (errno != 0) {
		/* This case is actually an error */
		LogEvent(COMPONENT_IDMAPPER, "getgrouplist %s failed",
			 user);
		return false;
	}
	gdata->nbgroups = nbgrp;

	/* Step 2: allocate gdata->groups with the right size then
	 * call getgrouplist() a second time to get the actual group list */
	groups = (gid_t *) gsh_malloc(nbgrp * sizeof(gid_t));
	if (groups == NULL)
		return false;

	if (getgrouplist
	    (user, gid, groups, &gdata->nbgroups) == -1) {
		LogEvent(COMPONENT_IDMAPPER, "getgrouplist %s failed", user);
		gsh_free(gdata->groups);
		return false;
	}

	if (renew) {
		pthread_mutex_lock(&gdata->lock);
		/* If refcount==1, I am the only one who use this group list
		 * so I can refresh it immediatly. Otherwise, keep it for
		 * later use */
		if (gdata->refcount == 1) {
			gsh_free(gdata->groups);
			gdata->groups = groups;
		} else
			gdata->new_groups = groups; /* will be used later */
		pthread_mutex_unlock(&gdata->lock);
	} else {
		gdata->groups = groups;
		gdata->new_groups = NULL;
		/* first refcount is 1: entry is created "in use" */
		gdata->refcount = 1;
		pthread_mutex_init(&gdata->lock, NULL);
	}

	gdata->epoch = time(NULL);

	return true;
}


bool pwentname2grp(char *namebuff,
		   uid_t *puid,
		   struct group_data *gdata,
		   bool renew)
{
	char *buff;
	struct passwd p;
	struct passwd *pp;

	buff = alloca(sysconf(_SC_GETPW_R_SIZE_MAX));
	if (buff == NULL) {
		LogMajor(COMPONENT_IDMAPPER,
			 "Can't allocate memory for getpwnam_r");
		return false;
	}

	if ((getpwnam_r(namebuff, &p, buff, MAXPATHLEN, &pp) != 0)
	    || (pp == NULL)) {
		LogEvent(COMPONENT_IDMAPPER, "getpwnam_r %s failed", namebuff);
		return false;
	}

	if (!my_getgrouplist_alloc(p.pw_name, p.pw_gid, gdata, renew))
		return false;

	/* Set puid */
	*puid = p.pw_uid;

	/* Set uid/gid */
	gdata->uid = p.pw_uid;
	gdata->gid = p.pw_gid;

	return true;
}

bool pwentuid2grp(uid_t uid, struct gsh_buffdesc *name,
		  struct group_data *gdata,
		  bool renew)
{
	char *buff;
	struct passwd p;
	struct passwd *pp;

	buff = alloca(sysconf(_SC_GETPW_R_SIZE_MAX));
	if (buff == NULL) {
		LogMajor(COMPONENT_IDMAPPER,
			 "Can't allocate memory for getpwuid_r");
		return false;
	}

	if ((getpwuid_r(uid, &p, buff, MAXPATHLEN, &pp) != 0) || (pp == NULL)) {
		LogEvent(COMPONENT_IDMAPPER, "getpwuid_r %u failed", uid);
		return false;
	}

	if (!my_getgrouplist_alloc(p.pw_name, p.pw_gid, gdata, renew))
		return false;

	/* Set puid */
	name->addr = p.pw_name;
	name->len = strlen(p.pw_name);

	/* Set uid/gid */
	gdata->uid = p.pw_uid;
	gdata->gid = p.pw_gid;

	return true;
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
bool name2grp(const struct gsh_buffdesc *name, struct group_data **gdata)
{
	bool success = false;
	bool renew = false;
	uid_t uid = -1;

	pthread_rwlock_rdlock(&uid2grp_user_lock);

	success = uid2grp_lookup_by_uname(name, &uid, gdata);

	pthread_rwlock_unlock(&uid2grp_user_lock);

	if (success) {
		/* Check for expiration */
		if ((time(NULL) - (*gdata)->epoch) >
			nfs_param.core_param.manage_gids_expiration)
			renew = true;

		pthread_mutex_lock(&(*gdata)->lock);
		if (((*gdata)->refcount != 0) &&
		    ((*gdata)->new_groups != NULL)) {
			free((*gdata)->groups);
			(*gdata)->groups = (*gdata)->new_groups;
			(*gdata)->new_groups = NULL;
		}
		pthread_mutex_unlock(&(*gdata)->lock);

		/* increment refcount */
		(*gdata)->refcount = (*gdata)->refcount+1;
	}

	if (!success || renew) {
		/* Something we can mutate and count on as terminated */
		char *namebuff = alloca(name->len + 1);

		memcpy(namebuff, name->addr, name->len);
		*(namebuff + name->len) = '\0';

		if (pwentname2grp(namebuff, &uid, *gdata, renew)) {
			/* if entry is renewed, do not add it to the cache
			 * we just refreshed them "in place" */
			if (!renew) {
				pthread_rwlock_wrlock(&uid2grp_user_lock);
				success = uid2grp_add_user(name, uid, *gdata);
				pthread_rwlock_unlock(&uid2grp_user_lock);

				if (!success) {
					LogMajor(COMPONENT_IDMAPPER,
						"name2grp %s failed",
						 namebuff);
					return false;
				}
			}
		}
	}

	return true;
}

bool uid2grp(uid_t uid, struct group_data **gdata)
{
	bool success = false;
	bool renew = false;

	struct gsh_buffdesc name;
	struct gsh_buffdesc *pname = &name;

	pthread_rwlock_rdlock(&uid2grp_user_lock);

	success = uid2grp_lookup_by_uid(uid, &pname, gdata);

	pthread_rwlock_unlock(&uid2grp_user_lock);


	if (success) {

		/* Check for expiration */
		if ((time(NULL) - (*gdata)->epoch) >
			nfs_param.core_param.manage_gids_expiration)
			renew = true;

		pthread_mutex_lock(&(*gdata)->lock);
		if (((*gdata)->refcount != 0) &&
		    ((*gdata)->new_groups != NULL)) {
			free((*gdata)->groups);
			(*gdata)->groups = (*gdata)->new_groups;
			(*gdata)->new_groups = NULL;
		}
		pthread_mutex_unlock(&(*gdata)->lock);

		/* increment refcount */
		(*gdata)->refcount = (*gdata)->refcount + 1;
	}

	if (!success || renew) {
		if (pwentuid2grp(uid, &name, *gdata, renew)) {
			/* if entry is renewed, do not add it to the cache
			 * we just refreshed them "in place" */
			if (!renew) {
				pthread_rwlock_wrlock(&uid2grp_user_lock);
				success = uid2grp_add_user(&name, uid, *gdata);
				pthread_rwlock_unlock(&uid2grp_user_lock);

				if (!success) {
					LogMajor(COMPONENT_IDMAPPER,
					"uid2grp %u failed", uid);
					return false;
				}
			}
		}
	}

	return true;
}

void uid2grp_unref(uid_t uid)
{
	bool success = false;

	struct gsh_buffdesc name;
	struct gsh_buffdesc *pname = &name;

	struct group_data *gdata = NULL;


	pthread_rwlock_rdlock(&uid2grp_user_lock);
	success = uid2grp_lookup_by_uid(uid, &pname, &gdata);
	pthread_rwlock_unlock(&uid2grp_user_lock);

	if (success) {
		pthread_mutex_lock(&gdata->lock);
		if (gdata->refcount > 0)
			gdata->refcount = gdata->refcount - 1;
		pthread_mutex_unlock(&gdata->lock);
	}
}
/** @} */
