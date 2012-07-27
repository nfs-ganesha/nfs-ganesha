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

/* fsal_commonlib.c
 * Common functions for and private to fsal modules.
 * The prime requirement for functions to be here is that they operate only
 * on the public part of the fsal api and are therefore sharable by all fsal
 * implementations.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <libgen.h>             /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/quota.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"

/* fsal_module to fsal_export helpers
 */

/* fsal_attach_export
 * called from the FSAL's create_export method with a reference on the fsal.
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
			    struct glist_head *obj_link)
{
	int retval = 0;

	pthread_mutex_lock(&fsal_hdl->lock);
	if(fsal_hdl->refs > 0) {
		glist_add(&fsal_hdl->exports, obj_link);
	} else {
		LogCrit(COMPONENT_CONFIG,
			"Attaching export with out holding a reference!. hdl= = 0x%p",
			fsal_hdl);
		retval = EINVAL;
	}
	pthread_mutex_unlock(&fsal_hdl->lock);
	return retval;
}

/* fsal_detach_export
 * called by an export when it is releasing itself.
 * does not require a reference to be taken.  The list has 
 * kept the fsal "busy".
 */

void fsal_detach_export(struct fsal_module *fsal_hdl,
			    struct glist_head *obj_link)
{
	pthread_mutex_lock(&fsal_hdl->lock);
	glist_del(obj_link);
	pthread_mutex_unlock(&fsal_hdl->lock);
}

/* fsal_export to fsal_obj_handle helpers
 */

int fsal_attach_handle(struct fsal_export *exp_hdl,
                       struct glist_head *obj_link)
{
        int retval = 0;

        pthread_mutex_lock(&exp_hdl->lock);
        if(exp_hdl->refs > 0) {
                glist_add(&exp_hdl->handles, obj_link);
        } else {
                LogCrit(COMPONENT_FSAL,
                        "Attaching object handle with out holding a reference!. hdl= = 0x%p",
                        exp_hdl);
                retval = EINVAL;
        }
        pthread_mutex_unlock(&exp_hdl->lock);
        return retval;
}

void fsal_detach_handle(struct fsal_export *exp_hdl,
                        struct glist_head *obj_link)
{
	pthread_mutex_lock(&exp_hdl->lock);
	glist_del(obj_link);
	pthread_mutex_unlock(&exp_hdl->lock);
}

void fsal_export_init(struct fsal_export *exp, struct export_ops *exp_ops,
                      struct exportlist__ *exp_entry)
{
	pthread_mutexattr_t attrs;

	init_glist(&exp->handles);
	init_glist(&exp->exports);
	pthread_mutexattr_init(&attrs);
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
	pthread_mutex_init(&exp->lock, &attrs);

	exp->ops = exp_ops;
	exp->refs = 1;  /* we exit with a reference held */
	exp->exp_entry = exp_entry;
}

int fsal_obj_handle_init(struct fsal_obj_handle *obj,
                         struct fsal_obj_ops *ops,
                         struct fsal_export *exp,
                         object_file_type_t type)
{
        int retval;
	pthread_mutexattr_t attrs;

	obj->refs = 1;  /* we start out with a reference */
	obj->ops = ops;
	obj->export = exp;
        obj->type = type;
	init_glist(&obj->handles);
	pthread_mutexattr_init(&attrs);
	pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
	pthread_mutex_init(&obj->lock, &attrs);

	/* lock myself before attaching to the export.
	 * keep myself locked until done with creating myself.
	 */

	pthread_mutex_lock(&obj->lock);
	retval = fsal_attach_handle(exp, &obj->handles);
	pthread_mutex_unlock(&obj->lock);
        return retval;
}

int fsal_attach_ds(struct fsal_export *exp_hdl,
                   struct glist_head *ds_link)
{
        int retval = 0;

        pthread_mutex_lock(&exp_hdl->lock);
        if (exp_hdl->refs > 0) {
                glist_add(&exp_hdl->ds_handles, ds_link);
        } else {
                LogCrit(COMPONENT_FSAL,
                        "Attaching ds handle without holding a reference!. "
                        "hdl= = 0x%p", exp_hdl);
                retval = EINVAL;
        }
        pthread_mutex_unlock(&exp_hdl->lock);
        return retval;
}

void fsal_detach_ds(struct fsal_export *exp_hdl,
                    struct glist_head *ds_link)
{
        pthread_mutex_lock(&exp_hdl->lock);
        glist_del(ds_link);
        pthread_mutex_unlock(&exp_hdl->lock);
}

int
fsal_ds_handle_init(struct fsal_ds_handle *ds,
                    struct fsal_ds_ops *ops,
                    struct fsal_export *exp)
{
        int retval = 0;
        pthread_mutexattr_t attrs;

        ds->refs = 1;  /* we start out with a reference */
        ds->ops = ops;
        ds->export = exp;
        init_glist(&ds->ds_handles);
        pthread_mutexattr_init(&attrs);
        pthread_mutexattr_settype(&attrs, PTHREAD_MUTEX_ADAPTIVE_NP);
        pthread_mutex_init(&ds->lock, &attrs);

        /* lock myself before attaching to the export.
	 * keep myself locked until done with creating myself.
	 */

        pthread_mutex_lock(&ds->lock);
        retval = fsal_attach_handle(exp, &ds->ds_handles);
        pthread_mutex_unlock(&ds->lock);
        return retval;
}

int fsal_ds_handle_uninit(struct fsal_ds_handle *ds)
{
        pthread_mutex_lock(&ds->lock);
        if (ds->refs) {
                return EINVAL;
        }
        fsal_detach_ds(ds->export, &ds->ds_handles);
        pthread_mutex_unlock(&ds->lock);
        pthread_mutex_destroy(&ds->lock);
        ds->ops = NULL; /*poison myself */
        ds->export = NULL;

        return 0;
}
