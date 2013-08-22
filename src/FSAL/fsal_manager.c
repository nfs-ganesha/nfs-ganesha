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
#include <pthread.h>
#include <dlfcn.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "config_parsing.h"
#include "pnfs_utils.h"

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
	config_item_t fsal_block, block;
	config_item_t item;
	char *key, *value;
	int fb, fsal_cnt, item_cnt;

        /* Call something in common_pnfs.c */
        pnfs_common_dummy();

	fsal_block = config_FindItemByName(config, CONF_LABEL_FSAL);
	if(fsal_block == NULL) {
		LogFatal(COMPONENT_INIT,
			 "Cannot find item \"%s\" in configuration",
			 CONF_LABEL_FSAL);
		return 1;
	}
	if(config_ItemType(fsal_block) != CONFIG_ITEM_BLOCK) {
		LogFatal(COMPONENT_INIT,
			 "\"%s\" is not a block",
			 CONF_LABEL_FSAL);
		return 1;
	}
	load_state = idle;  /* .init was a long time ago... */

	fsal_cnt = config_GetNbItems(fsal_block);
	for(fb = 0; fb < fsal_cnt; fb++) {
		block = config_GetItemByIndex(fsal_block, fb);
		if(config_ItemType(block) == CONFIG_ITEM_BLOCK) {
			char *fsal_name;
                        int i;

			fsal_name = config_GetBlockName(block);
			item_cnt = config_GetNbItems(block);
			for(i = 0; i < item_cnt; i++) {
				item = config_GetItemByIndex(block, i);
				if(config_GetKeyValue(item, &key, &value) != 0) {
					LogFatal(COMPONENT_INIT,
						 "Error fetching [%d]"
						 " from config section \"%s\"",
						 i, CONF_LABEL_NFS_CORE);
					return 1;
				}
				if(strcasecmp(key, "FSAL_Shared_Library") == 0) {
					struct fsal_module *fsal_hdl;
					int rc;

					LogDebug(COMPONENT_INIT,
						 "Loading module w/ name=%s"
						 " and library=%s", fsal_name, value);
					rc = load_fsal(value, fsal_name, &fsal_hdl);
					if(rc < 0) {
						LogCrit(COMPONENT_INIT,
							"Failed to load (%s)"
							" because: %s",	value, strerror(rc));
					}
				}
			}
		} else { /* a FSAL global parameter */
			item = block;
			if(config_GetKeyValue(item, &key, &value) != 0) {
				LogFatal(COMPONENT_INIT,
					 "Error fetching [%d]"
					 " from config section \"%s\"",
					 fb, CONF_LABEL_NFS_CORE);
				return 1;
			}
			if(strcasecmp(key, "LogLevel") == 0) {
				LogDebug(COMPONENT_INIT, "LogLevel = %s", value);
			} else {
				LogDebug(COMPONENT_INIT, "Some odd key/value: %s = %s",
					 key, value);
			}
		}
	}
	if(glist_empty(&fsal_list)) {
		LogFatal(COMPONENT_INIT,
			 "No fsal modules loaded");
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
	LogDebug(COMPONENT_INIT,
		 "Probing to see if %s is already loaded",path);
	dl = dlopen(path, RTLD_NOLOAD);
	if(dl != NULL) {
		retval = EEXIST;
		LogDebug(COMPONENT_INIT, "Already exists ...");
		goto errout;
	}
#endif

	load_state = loading;
	pthread_mutex_unlock(&fsal_lock);

	LogDebug(COMPONENT_INIT,
		 "Loading FSAL %s with %s", name, path);
#ifdef LINUX
	dl = dlopen(path, RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
#elif FREEBSD
	dl = dlopen(path, RTLD_NOW|RTLD_LOCAL);
#endif

	pthread_mutex_lock(&fsal_lock);
	if(dl == NULL) {
#ifdef ELIBACC
		retval = ELIBACC; /* hand craft a meaningful error */
#else
		retval = EPERM;   /* ELIBACC does not exist on MacOS */
#endif
		dl_error = strdup(dlerror());
		LogCrit(COMPONENT_INIT,
			"Could not dlopen module:%s Error:%s", path, dl_error);
		goto errout;
	}
	(void)dlerror(); /* clear it */

/* now it is the module's turn to register itself */

	if(load_state == loading) { /* constructor didn't fire */
		void (*module_init)(void);
		char *sym_error;

		module_init = dlsym(dl, "fsal_init");
		sym_error = (char *)dlerror();
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
			gsh_free(oldname);
	}
	*fsal_hdl_p = fsal;
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

errout:
	load_state = idle;
	pthread_mutex_unlock(&fsal_lock);
	LogMajor(COMPONENT_INIT,
		 "Failed to load module (%s) because: %s",
		 path, strerror(retval));
        free(dl_path);
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
				"Initialized %s",
				fsal->name);
		} else {
			LogCrit(COMPONENT_INIT,
				 "Initialization failed for %s",
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

int register_fsal(struct fsal_module *fsal_hdl,
		  const char *name,
		  uint32_t major_version,
		  uint32_t minor_version)
{
	pthread_mutexattr_t attrs;
	extern struct fsal_ops def_fsal_ops;

	if((major_version != FSAL_MAJOR_VERSION) ||
	   (minor_version > FSAL_MINOR_VERSION)) {
		so_error = EINVAL;
		LogCrit(COMPONENT_INIT,
			"FSAL \"%s\" failed to register because "
			"of version mismatch core = %d.%d, fsal = %d.%d",
			name,
			FSAL_MAJOR_VERSION,
			FSAL_MINOR_VERSION,
			major_version,
			minor_version);
		load_state = error;
		return so_error;
	}
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

/* allocate and init ops vector to system wide defaults
 * from FSAL/default_methods.c
 */
	fsal_hdl->ops = gsh_malloc(sizeof(struct fsal_ops));
	if(fsal_hdl->ops == NULL) {
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
	if(load_state == loading)
		load_state = registered;
	pthread_mutex_unlock(&fsal_lock);
	return 0;

errout:
	if(fsal_hdl->path)
		gsh_free(fsal_hdl->path);
	if(fsal_hdl->name)
		gsh_free(fsal_hdl->name);
	if(fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
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
		gsh_free(fsal_hdl->path);
	if(fsal_hdl->name)
		gsh_free(fsal_hdl->name);
	if(fsal_hdl->ops)
		gsh_free(fsal_hdl->ops);
	retval = 0;
out:
	return retval;
}

/** @} */
