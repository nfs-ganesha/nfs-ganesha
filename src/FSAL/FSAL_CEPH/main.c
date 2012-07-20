/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * -------------
 */

/**
 * @file   main.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Thu Jul  5 14:48:33 2012
 *
 * @brief Impelmentation of FSAL module founctions for Ceph
 *
 * This file implements the module functions for the Ceph FSAL, for
 * initialization, teardown, configuration, and creation of exports.
 */


#include <stdlib.h>
#include <assert.h>
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"

/**
 * A local copy of the handle for this module, so it can be disposed
 * of.
 */
static struct fsal_module *module = NULL;

/**
 * The name of this module.
 */
static const char *module_name = "Ceph";

/**
 * @brief Create a new export under this FSAL
 *
 * This function creates a new export object for the Ceph FSAL.
 *
 * @todo ACE: We do not handle re-exports of the same cluster in a
 * sane way.  Currently we create multiple handles and cache objects
 * pointing to the same one.  This is not necessarily wrong, but it is
 * inefficient.  It may also not be something we expect to use enough
 * to care about.
 *
 * @param[in]     module_in  The supplied module handle
 * @param[in]     path       The path to export
 * @param[in]     options    Export specific options for the FSAL
 * @param[in,out] list_entry Our entry in the export list
 * @param[in]     next_fsal  Next stacked FSAL
 * @param[out]    pub_export Newly created FSAL export object
 *
 * @return FSAL status.
 */

static fsal_status_t
create_export(struct fsal_module *module,
              const char *path,
              const char *options,
              struct exportlist__ *list_entry,
              struct fsal_module *next_fsal,
              struct fsal_export **pub_export)
{
        /* The status code to return */
        fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
        /* True if we have called fsal_export_init */
        bool_t initialized = FALSE;
        /* The internal export object */
        struct export *export = NULL;
        /* A fake argument list for Ceph */
        const char *argv[] = {"FSAL_CEPH", path};
        /* Return code from Ceph calls */
        int ceph_status = 0;

        if ((path == NULL) ||
            (strlen(path) == 0)) {
                status.major = ERR_FSAL_INVAL;
                LogCrit(COMPONENT_FSAL,
                        "No path to export.");
                goto error;
        }

        if (next_fsal != NULL) {
                status.major = ERR_FSAL_INVAL;
                LogCrit(COMPONENT_FSAL,
                        "Stacked FSALs unsupported.");
                goto error;
        }

        export = gsh_calloc(1, sizeof(struct export));
        if (export == NULL) {
                status.major = ERR_FSAL_NOMEM;
                LogCrit(COMPONENT_FSAL,
                        "Unable to allocate export object for %s.",
                        path);
                goto error;
        }


        fsal_export_init(&export->export,
                         module->exp_ops,
                         list_entry);

        initialized = TRUE;

        /* allocates ceph_mount_info */
        ceph_status = ceph_create(&export->cmount, NULL);
        if (ceph_status != 0) {
                status.major = ERR_FSAL_SERVERFAULT;
                LogCrit(COMPONENT_FSAL,
                        "Unable to create Ceph handle");
                goto error;
        }

        ceph_status = ceph_conf_read_file(export->cmount, NULL);
        if (ceph_status == 0) {
                ceph_status = ceph_conf_parse_argv(export->cmount, 2,
                                                   argv);
        }

        if (ceph_status != 0) {
                status.major = ERR_FSAL_SERVERFAULT;
                LogCrit(COMPONENT_FSAL,
                        "Unable to read Ceph configuration");
                goto error;
        }

        ceph_status = ceph_mount(export->cmount, NULL);
        if (ceph_status != 0) {
                status.major = ERR_FSAL_SERVERFAULT;
                LogCrit(COMPONENT_FSAL,
                        "Unable to mount Ceph cluster.");
                goto error;
        }

        if (fsal_attach_export(module,
                               &export->export.exports) != 0) {
                status.major = ERR_FSAL_SERVERFAULT;
                LogCrit(COMPONENT_FSAL,
                        "Unable to attach export.");
                goto error;
        }

        export->export.fsal = module;

        *pub_export = &export->export;
        return status;

error:

        if (export->cmount != NULL) {
                ceph_shutdown(export->cmount);
                export->cmount = NULL;
        }

        if (initialized) {
                pthread_mutex_destroy(&export->export.lock);
                initialized = FALSE;
        }

        if (export != NULL) {
                gsh_free(export);
                export = NULL;
        }

        return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a Ceph cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.  Currently, we have no private, per-module data or
 * initialization.
 */

MODULE_INIT void
init(void)
{
        /* register_fsal seems to expect zeroed memory. */
        module = gsh_calloc(1, sizeof(struct fsal_module));
        if (module == NULL) {
                LogCrit(COMPONENT_FSAL,
                        "Unable to allocate memory for Ceph FSAL module.");
                return;
        }

        if (register_fsal(module, module_name, FSAL_MAJOR_VERSION,
                          FSAL_MINOR_VERSION) != 0) {
                /* The register_fsal function prints its own log
                   message if it fails*/
                gsh_free(module);
                LogCrit(COMPONENT_FSAL,
                        "Ceph module failed to register.");
        }

        /* Set up module operations */
        module->ops->create_export = create_export;

        export_ops_init(module->exp_ops);
        handle_ops_init(module->obj_ops);
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.
 * The Ceph FSAL has no other resources to release on the per-FSAL
 * level.
 */

MODULE_FINI void
finish(void)
{
        if (unregister_fsal(module) != 0) {
                LogCrit(COMPONENT_FSAL,
                        "Unable to unload FSAL.  Dying with extreme "
                        "prejudice.");
                abort();
        }

        gsh_free(module);
        module = NULL;
}

