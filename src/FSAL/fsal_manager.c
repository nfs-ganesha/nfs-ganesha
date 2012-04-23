/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/*
 * FSAL module manager
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "config_parsing.h"

/* List of loaded fsal modules
 * static to be private to the functions in this module
 * fsal_lock is taken whenever the list is walked.
 */

pthread_mutex_t fsal_lock = PTHREAD_MUTEX_INITIALIZER;
static GLIST_HEAD(fsal_list);

/*
 * vars for passing status/errors between shared object
 * and this module.  Only accessed under lock
 */

static char *dl_error;
static int so_error;
static struct fsal_module *new_fsal;

static enum load_state {
	init,		/* server start state when program .init sections can run */
	idle,		/* switch from init->idle early in main()*/
	loading,	/* dlopen is doing its thing. set by load_fsal() just prior */
	registered,	/* signal by registration by module that all is well */
	error		/* signal by registration by module that all is not well */
} load_state = init;

/* start_fsals
 * Called at server initialization
 * probe config file and install fsal modules
 * return 1 on error, 0 on success.  Maybe TRUE/FALSE???
 */

int start_fsals(config_file_t config)
{
	config_item_t block;
	config_item_t item;
	char *key, *value;
	int i, item_cnt;

	block = config_FindItemByName(config, CONF_LABEL_NFS_CORE);
	if(block == NULL) {
		LogFatal(COMPONENT_INIT,
			 "start_fsals: Cannot find item \"%s\" in configuration",
			 CONF_LABEL_NFS_CORE);
		return 1;
	}
	if(config_ItemType(block) != CONFIG_ITEM_BLOCK) {
		LogFatal(COMPONENT_INIT,
			 "start_fsals: \"%s\" is not a block",
			 CONF_LABEL_NFS_CORE);
		return 1;
	}
	load_state = idle;  /* .init was a long time ago... */

	item_cnt = config_GetNbItems(block);
	for(i = 0; i < item_cnt; i++) {
		item = config_GetItemByIndex(block, i);
		if(config_GetKeyValue(item, &key, &value) == 0) {

		} else {
			LogFatal(COMPONENT_INIT,
				 "start_fsals: Error fetching [%d]"
				 " from config section \"%s\"",
				 i, CONF_LABEL_NFS_CORE);
			return 1;
		}
		if(strcasecmp(key, "FSAL_Shared_Library") == 0) {
			struct fsal_module *fsal_hdl;
			int rc;
			char *name = NULL;
			char *so_name = rindex(value, ':');

                        if(so_name != NULL) {
                          so_name++;
                          if (so_name - value == 0) {
                            LogCrit(COMPONENT_INIT, "start_fsals: Failed to"
                                    " load (%s) because parameter is in wrong"
                                    " format (name:lib)", value);
                            continue;
                          }
                          name = strndup(value, so_name - value - 1);
                        } else {
                          LogCrit(COMPONENT_INIT, "start_fsals: Failed to"
                                  " load (%s) because parameter is in wrong"
                                  " format (name:lib)", value);
                          continue;
                        }

			LogDebug(COMPONENT_INIT,
				     "start_fsals: Loading module w/ name=%s"
                                     " and library=%s", name, so_name);
			rc = load_fsal(so_name, name, &fsal_hdl);
                        free(name);
			if(rc < 0) {
				LogCrit(COMPONENT_INIT,
					"start_fsals: Failed to load (%s)"
                                        " because: %s",	so_name, strerror(rc));
			}
		}
	}
	if( !glist_empty(&fsal_list)) {
		struct fsal_module *loaded_fsal;
		struct glist_head *entry;

		pthread_mutex_lock(&fsal_lock);
		glist_for_each(entry, &fsal_list) {
			loaded_fsal = glist_entry(entry, struct fsal_module, fsals);
			LogInfo(COMPONENT_INIT,
				"start_fsals: loaded (%s) as \"%s\"",
				loaded_fsal->ops->get_name(loaded_fsal),
				loaded_fsal->ops->get_lib_name(loaded_fsal));
		}
		pthread_mutex_unlock(&fsal_lock);
		return 0;
	} else {
		LogFatal(COMPONENT_INIT,
			 "start_fsals: No fsal modules loaded");
	}
	return 1;
}

/* load_fsal
 * Load the fsal's shared object and name it if the fsal
 * has not already done so.
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

int load_fsal(const char *path, const char *name, struct fsal_module **fsal_hdl_p)
{
	void *dl;
	int retval = EBUSY; /* already loaded */
	char *dl_path;
	struct fsal_module *fsal;

	dl_path = strdup(path);
	if(dl_path == NULL)
		return ENOMEM;
	pthread_mutex_lock(&fsal_lock);
	if(load_state != idle)
		goto errout;
	if(dl_error) {
		free(dl_error);
		dl_error = NULL;
	}

#ifdef LINUX
	/* recent linux/glibc can probe to see if it already there */
	LogMajor(COMPONENT_INIT,
		 "load_fsal: dlopen(%s, RTLD_NOLOAD)\n",path);
	dl = dlopen(path, RTLD_NOLOAD);
	if(dl != NULL) {
		retval = EEXIST;
		LogCrit(COMPONENT_INIT, "Already exists ...");
		goto errout;
	}
#endif

	load_state = loading;
	pthread_mutex_unlock(&fsal_lock);

	LogMajor(COMPONENT_INIT,
		 "load_fsal: dlopen(%s, RTLD_LAZY|RTLD_LOCAL)\n",path);
	dl = dlopen(path, RTLD_LAZY|RTLD_LOCAL);

	pthread_mutex_lock(&fsal_lock);
	if(dl == NULL) {
		retval = errno;
		dl_error = strdup(dlerror());
		LogCrit(COMPONENT_INIT, "Could not dlopen module:%s Error:%s", path, dl_error);
		goto errout;
	}
	(void)dlerror(); /* clear it */

/* now it is the module's turn to register itself */

	if(load_state == loading) { /* constructor didn't fire */
		void (*module_init)(void);
		char *sym_error;

		*(void **)(&module_init) = dlsym(dl, "fsal_init");
		sym_error = dlerror();
		if(sym_error != NULL) {
			dl_error = strdup(sym_error);
			so_error = ENOENT;
			LogCrit(COMPONENT_INIT, "Could not execute symbol fsal_init"
                                " from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		if((void *)module_init == NULL) {
			so_error = EFAULT;
			LogCrit(COMPONENT_INIT, "Could not execute symbol fsal_init"
                                " from module:%s Error:%s", path, dl_error);
			goto errout;
		}
		pthread_mutex_unlock(&fsal_lock);

		(*module_init)(); /* try registering by hand this time */

		pthread_mutex_lock(&fsal_lock);
	}
	if(load_state == error) { /* we are in registration hell */
		dlclose(dl);
		retval = so_error; /* this is the registration error */
		LogCrit(COMPONENT_INIT, "Could not execute symbol fsal_init"
                        " from module:%s Error:%s", path, dl_error);
		goto errout;
	}
	if(load_state != registered) {
		retval = EPERM;
		LogCrit(COMPONENT_INIT, "Could not execute symbol fsal_init"
                        " from module:%s Error:%s", path, dl_error);
		goto errout;
	}

/* we now finish things up, doing things the module can't see */

	fsal = new_fsal; /* recover handle from .ctor  and poison again */
	new_fsal = NULL;
	fsal->path = dl_path;
	fsal->dl_handle = dl;
	so_error = 0;
	if(name != NULL) { /* does config want to set name? */
		char *oldname = NULL;

		if(fsal->name != NULL) {
			if(strlen(fsal->name) >= strlen(name)) {
				strcpy(fsal->name, name);
			} else {
				oldname = fsal->name;
				fsal->name = strdup(name);
			}
		} else {
			fsal->name = strdup(name);
		}
		if(fsal->name == NULL) {
			fsal->name = oldname;
			oldname = NULL;
		}
		if(oldname != NULL)
			free(oldname);
	}
	*fsal_hdl_p = fsal;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

errout:
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	LogMajor(COMPONENT_INIT,
		 "load_fsal: Failed to load module (%s) because: %s\n",
		 path, strerror(retval));
	return retval;
}

/* Initialize all the loaded FSALs now that the config file
 * has been parsed.
 */

int init_fsals(config_file_t config_struct)
{
	struct fsal_module *fsal;
	struct glist_head *entry;
	fsal_status_t fsal_status;
	int retval = 0;

	pthread_mutex_lock(&fsal_lock);
	glist_for_each(entry, &fsal_list) {
		fsal = glist_entry(entry, struct fsal_module, fsals);

		pthread_mutex_lock(&fsal->lock);
		fsal->refs++;		/* reference it */
		pthread_mutex_unlock(&fsal->lock);

		fsal_status = fsal->ops->init_config(fsal, config_struct);

		pthread_mutex_lock(&fsal->lock);
		fsal->refs--; /* now 'put_fsal' on it */
		pthread_mutex_unlock(&fsal->lock);

		if( !FSAL_IS_ERROR(fsal_status)) {
			LogInfo(COMPONENT_INIT,
				"init_fsals: initialized %s",
				fsal->name);
		} else {
			LogCrit(COMPONENT_INIT,
				 "init_fsals: initialization failed for %s",
				 fsal->name);
			retval = EINVAL;
			goto out;
		}
	}
out:
	pthread_mutex_unlock(&fsal_lock);
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
		if(strcmp(name, fsal->name) == 0) {
			fsal->refs++;
			pthread_mutex_unlock(&fsal->lock);
			pthread_mutex_unlock(&fsal_lock);
			return fsal;
		}
	}
	pthread_mutex_unlock(&fsal_lock);
	return NULL;
}

/* put_fsal
 * put the fsal back that we got with lookup_fsal.
 * Indicates that we are no longer interested in it (for now)
 */

static int put(struct fsal_module *fsal_hdl)
{
	int retval = EINVAL; /* too many 'puts" */

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs > 0) {
		fsal_hdl->refs--;
		retval = 0;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return retval;
}

/* get_name
 * return the name of the loaded fsal.
 * Must be called while holding a reference.
 * Return a pointer to the name, possibly NULL;
 * Note! do not dereference after doing a 'put'.
 */

static const char *get_name(struct fsal_module *fsal_hdl)
{
	char *name;

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs <= 0) {
		LogCrit(COMPONENT_CONFIG,
			"lib_of_fsal: Called without reference!");
		name = NULL;
	} else {
		name = fsal_hdl->name;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return name;
}

/* fsal_get_lib_name
 * return the pathname loaded for the fsal.
 * Must be called while holding a reference.
 * Return a pointer to the library path, possibly NULL;
 * Note! do not dereference after doing a 'put'.
 */

static const char *get_lib_name(struct fsal_module *fsal_hdl)
{
	char *path;

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs <= 0) {
		LogCrit(COMPONENT_CONFIG,
			"lib_of_fsal: Called without reference!");
		path = NULL;
	} else {
		path = fsal_hdl->path;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return path;
}

/* unload fsal
 * called while holding the last remaining reference
 * remove from list and dlclose the module
 * if references are held, return EBUSY
 * if it is a static, return EACCES
 */

static int unload_fsal(struct fsal_module *fsal_hdl)
{
	int retval = EBUSY; /* someone still has a reference */

	pthread_mutex_lock(&fsal_lock);
	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs != 0 || !glist_empty(&fsal_hdl->exports))
		goto err;
	if(fsal_hdl->dl_handle == NULL) {
		retval = EACCES;  /* cannot unload static linked fsals */
		goto err;
	}
	glist_del(&fsal_hdl->fsals);
	pthread_mutex_unlock(&fsal_hdl->lock);
	pthread_mutex_destroy(&fsal_hdl->lock);
	fsal_hdl->refs = 0;

	retval = dlclose(fsal_hdl->dl_handle);
	return retval;

err:
	pthread_mutex_unlock(&fsal_hdl->lock);
	pthread_mutex_unlock(&fsal_lock);
	return retval;
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

int register_fsal(struct fsal_module *fsal_hdl, const char *name)
{
	pthread_mutexattr_t attrs;

	pthread_mutex_lock(&fsal_lock);
	so_error = 0;
	if( !(load_state == loading || load_state == init)) {
		so_error = EACCES;
		goto errout;
	}
	new_fsal = fsal_hdl;
	if(name != NULL) {
		new_fsal->name = strdup(name);
		if(new_fsal->name == NULL) {
			so_error = ENOMEM;
			goto errout;
		}
	}
/* we set the base class method pointers here. the .init constructor sets
 * instance pointers, e.g. "create_export" from the fsal itself
 */
	fsal_hdl->ops->get_name = get_name;
	fsal_hdl->ops->get_lib_name = get_lib_name;
	fsal_hdl->ops->put = put;
	fsal_hdl->ops->unload = unload_fsal;
	pthread_mutexattr_init(&attrs);
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
	pthread_mutex_init(&fsal_hdl->lock, &attrs);
	init_glist(&fsal_hdl->fsals);
	init_glist(&fsal_hdl->exports);
	glist_add_tail(&fsal_list, &fsal_hdl->fsals);
	if(load_state == loading)
		load_state = registered;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

errout:
	load_state = error;
	pthread_mutex_unlock(&fsal_lock);
	LogCrit(COMPONENT_INIT,
		"FSAL \"%s\" failed to register because: %s",
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

	if(fsal_hdl->refs != 0) { /* this would be very bad */
		goto out;
	}
	if(fsal_hdl->path)
		free(fsal_hdl->path);
	if(fsal_hdl->name)
		free(fsal_hdl->name);
	retval = 0;
out:
	return retval;
}

