/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file fsal_manager.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief FSAL module manager
 */

#include "config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <dlfcn.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "config_parsing.h"
#include "pnfs_utils.h"
#include "fsal_private.h"

/* List of loaded fsal modules
 * static to be private to the functions in this module
 * fsal_lock is taken whenever the list is walked.
 */

pthread_mutex_t fsal_lock = PTHREAD_MUTEX_INITIALIZER;
GLIST_HEAD(fsal_list);

/*
 * vars for passing status/errors between shared object
 * and this module.  Only accessed under lock
 */

static char *dl_error;
static int so_error;
static struct fsal_module *new_fsal;

static enum load_state {
	init,		/* in server start state. .init sections can run */
	idle,		/* switch from init->idle early in main() */
	loading,	/* in dlopen(). set by load_fsal() just prior */
	registered,	/* signal by registration that all is well */
	error		/* signal by registration that all is not well */
} load_state = init;

/**
 * @brief Start the PSEUDOFS FSAL
 *
 * The pseudofs fsal is static (always present) so it needs its own
 * startup.  This is a stripped down version of load_fsal() that is
 * done very early in server startup.
 */

static void load_fsal_pseudo(void)
{
	char *dl_path;
	struct fsal_module *fsal;

	dl_path = gsh_strdup("Builtin-PseudoFS");
	if (dl_path == NULL)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	pthread_mutex_lock(&fsal_lock);

	if (load_state != idle)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	if (dl_error) {
		gsh_free(dl_error);
		dl_error = NULL;
	}

	load_state = loading;

	pthread_mutex_unlock(&fsal_lock);

	/* now it is the module's turn to register itself */
	pseudo_fsal_init();

	pthread_mutex_lock(&fsal_lock);

	if (load_state != registered)
		LogFatal(COMPONENT_INIT, "Couldn't Register FSAL_PSEUDO");

	/* we now finish things up, doing things the module can't see */

	fsal = new_fsal;   /* recover handle from .ctor and poison again */
	new_fsal = NULL;
	fsal->path = dl_path;
	fsal->dl_handle = NULL;
	so_error = 0;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
}

/**
 * @brief Start_fsals
 *
 * Called early server initialization.  Set load_state to idle
 * at this point as a check on dynamic loading not starting too early.
 */

void start_fsals(void)
{

	/* .init was a long time ago... */
	load_state = idle;

	/* Load FSAL_PSEUDO */
	load_fsal_pseudo();
}

/**
 * @brief Load the fsal's shared object.
 *
 * The dlopen() will trigger a .init constructor which will
 * do the actual registration.
 * after a successful load, the returned handle needs to be "put"
 * back after any other initialization is done.
 *
 * Return 0 on success and *fsal_hdl_p points to it.
 *	When finished, put_fsal_handle() on the handle to free it.
 *
 * Errors:
 *	EBUSY == the loader is busy (should not happen)
 *	EEXIST == the module is already loaded
 *	ENOLCK == register_fsal without load_fsal holding the lock.
 *	EINVAL == wrong loading state for registration
 *	ENOMEM == out of memory
 *	ENOENT == could not find "module_init" function
 *	EFAULT == module_init has a bad address
 *	other general dlopen errors are possible, all of them bad
 */

static const char *pathfmt = "%s/libfsal%s.so";

int load_fsal(const char *name,
	      struct fsal_module **fsal_hdl_p)
{
	void *dl;
	int retval = EBUSY;	/* already loaded */
	char *dl_path;
	struct fsal_module *fsal;
	char *bp;
	char *path = alloca(strlen(nfs_param.core_param.ganesha_modules_loc)
			    + strlen(name)
			    + strlen(pathfmt));

	sprintf(path, pathfmt,
		nfs_param.core_param.ganesha_modules_loc,
		name);
	bp = rindex(path, '/');
	bp++; /* now it is the basename, lcase it */
	while (*bp != '\0') {
		if (isupper(*bp))
			*bp = tolower(*bp);
		bp++;
	}
	dl_path = gsh_strdup(path);
	if (dl_path == NULL)
		return ENOMEM;
	pthread_mutex_lock(&fsal_lock);
	if (load_state != idle)
		goto errout;
	if (dl_error) {
		gsh_free(dl_error);
		dl_error = NULL;
	}
#ifdef LINUX
	/* recent linux/glibc can probe to see if it already there */
	LogDebug(COMPONENT_INIT, "Probing to see if %s is already loaded",
		 path);
	dl = dlopen(path, RTLD_NOLOAD);
	if (dl != NULL) {
		retval = EEXIST;
		LogDebug(COMPONENT_INIT, "Already exists ...");
		goto errout;
	}
#endif

	load_state = loading;
	pthread_mutex_unlock(&fsal_lock);

	LogDebug(COMPONENT_INIT, "Loading FSAL %s with %s", name, path);
#ifdef LINUX
	dl = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#elif FREEBSD
	dl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif

	pthread_mutex_lock(&fsal_lock);
	if (dl == NULL) {
#ifdef ELIBACC
		retval = ELIBACC;	/* hand craft a meaningful error */
#else
		retval = EPERM;	/* ELIBACC does not exist on MacOS */
#endif
		dl_error = gsh_strdup(dlerror());
		LogCrit(COMPONENT_INIT, "Could not dlopen module:%s Error:%s",
			path, dl_error);
		goto errout;
	}
	dlerror();	/* clear it */

/* now it is the module's turn to register itself */

	if (load_state == loading) {	/* constructor didn't fire */
		void (*module_init) (void);
		char *sym_error;

		module_init = dlsym(dl, "fsal_init");
		sym_error = (char *)dlerror();
		if (sym_error != NULL) {
			dl_error = gsh_strdup(sym_error);
			so_error = ENOENT;
			LogCrit(COMPONENT_INIT,
				"Could not execute symbol fsal_init"
				" from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		if ((void *)module_init == NULL) {
			so_error = EFAULT;
			LogCrit(COMPONENT_INIT,
				"Could not execute symbol fsal_init"
				" from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		pthread_mutex_unlock(&fsal_lock);

		(*module_init) ();	/* try registering by hand this time */

		pthread_mutex_lock(&fsal_lock);
	}
	if (load_state == error) {	/* we are in registration hell */
		dlclose(dl);
		retval = so_error;	/* this is the registration error */
		LogCrit(COMPONENT_INIT,
			"Could not execute symbol fsal_init"
			" from module:%s Error:%s", path, dl_error);
		goto errout;
	}
	if (load_state != registered) {
		retval = EPERM;
		LogCrit(COMPONENT_INIT,
			"Could not execute symbol fsal_init"
			" from module:%s Error:%s", path, dl_error);
		goto errout;
	}

/* we now finish things up, doing things the module can't see */

	fsal = new_fsal;   /* recover handle from .ctor and poison again */
	new_fsal = NULL;
	fsal->refs++; /* take initial ref so we can pass it back... */
	fsal->path = dl_path;
	fsal->dl_handle = dl;
	so_error = 0;
	*fsal_hdl_p = fsal;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

 errout:
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	LogMajor(COMPONENT_INIT, "Failed to load module (%s) because: %s",
		 path,
		 strerror(retval));
	gsh_free(dl_path);
	return retval;
}

/* lookup_fsal
 * Acquire a handle to the named fsal and take a reference to it.
 * this must be done before using any methods.
 * once done, release it with put_fsal
 * point.
 * Return NULL if not found.
 * don't forget to 'put' it back
 */

struct fsal_module *lookup_fsal(const char *name)
{
	struct fsal_module *fsal;
	struct glist_head *entry;

	pthread_mutex_lock(&fsal_lock);
	glist_for_each(entry, &fsal_list) {
		fsal = glist_entry(entry, struct fsal_module, fsals);
		pthread_mutex_lock(&fsal->lock);
		if (strcasecmp(name, fsal->name) == 0) {
			fsal->refs++;
			pthread_mutex_unlock(&fsal->lock);
			pthread_mutex_unlock(&fsal_lock);
			return fsal;
		}
		pthread_mutex_unlock(&fsal->lock);
	}
	pthread_mutex_unlock(&fsal_lock);
	return NULL;
}

/* functions only called by modules at ctor/dtor time
 */

/* register_fsal
 * Register the fsal in the system.  This can be called from three places:
 *
 *  + the server program's .init section if the fsal was statically linked
 *  + the shared object's .init section when load_fsal() dynamically loads it.
 *  + from the shared object's 'fsal_init' function if dlopen does not support
 *    .init/.fini sections.
 *
 * We use an ADAPTIVE_NP mutex because the initial spinlock is low impact
 * for protecting the list add/del atomicity.  Does FBSD have this?
 *
 * Any other case is an error.
 * Change load_state only for dynamically loaded modules.
 */

/** @todo implement api versioning and pass the major,minor here
 */

int register_fsal(struct fsal_module *fsal_hdl, const char *name,
		  uint32_t major_version, uint32_t minor_version)
{
	pthread_mutexattr_t attrs;

	if ((major_version != FSAL_MAJOR_VERSION)
	    || (minor_version > FSAL_MINOR_VERSION)) {
		so_error = EINVAL;
		LogCrit(COMPONENT_INIT,
			"FSAL \"%s\" failed to register because "
			"of version mismatch core = %d.%d, fsal = %d.%d", name,
			FSAL_MAJOR_VERSION, FSAL_MINOR_VERSION, major_version,
			minor_version);
		load_state = error;
		return so_error;
	}
	pthread_mutex_lock(&fsal_lock);
	so_error = 0;
	if (!(load_state == loading || load_state == init)) {
		so_error = EACCES;
		goto errout;
	}
	new_fsal = fsal_hdl;
	if (name != NULL) {
		new_fsal->name = gsh_strdup(name);
		if (new_fsal->name == NULL) {
			so_error = ENOMEM;
			goto errout;
		}
	}

/* allocate and init ops vector to system wide defaults
 * from FSAL/default_methods.c
 */
	fsal_hdl->ops = gsh_malloc(sizeof(struct fsal_ops));
	if (fsal_hdl->ops == NULL) {
		so_error = ENOMEM;
		goto errout;
	}
	memcpy(fsal_hdl->ops, &def_fsal_ops, sizeof(struct fsal_ops));

	pthread_mutexattr_init(&attrs);
#if defined(__linux__)
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
#endif
	pthread_mutex_init(&fsal_hdl->lock, &attrs);
	glist_init(&fsal_hdl->fsals);
	glist_init(&fsal_hdl->exports);
	glist_add_tail(&fsal_list, &fsal_hdl->fsals);
	if (load_state == loading)
		load_state = registered;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

 errout:
	if (fsal_hdl->path)
		gsh_free(fsal_hdl->path);
	if (fsal_hdl->name)
		gsh_free(fsal_hdl->name);
	if (fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
	load_state = error;
	pthread_mutex_unlock(&fsal_lock);
	LogCrit(COMPONENT_INIT, "FSAL \"%s\" failed to register because: %s",
		name, strerror(so_error));
	return so_error;
}

/* unregister_fsal
 * verify that the fsal is not busy
 * release all its resources owned at this level.  Mutex is already freed.
 * Called from the module's MODULE_FINI
 */

int unregister_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EBUSY;

	if (fsal_hdl->refs != 0) {	/* this would be very bad */
		goto out;
	}
	if (fsal_hdl->path)
		gsh_free(fsal_hdl->path);
	if (fsal_hdl->name)
		gsh_free(fsal_hdl->name);
	if (fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
	retval = 0;
 out:
	return retval;
}

/** @} */
