/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 *
 * \file    fsal_creds.c
 * \brief   FSAL credentials handling functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 *
 * @{
 */

/**
 * Parse FS specific option string
 * to build the export entry option.
 */
fsal_status_t CEPHFSAL_BuildExportContext(
                       fsal_export_context_t * export_context,
                       fsal_path_t * export_path,
                       char *fs_specific_options)
{
    cephfsal_export_context_t* ceph_export_context =
        (cephfsal_export_context_t*) export_context;
    char *argv[2];
    int argc=1;
    int rc;

    char procname[]="FSAL_CEPH";

    /* allocates ceph_mount_info */
    rc = ceph_create(&ceph_export_context->cmount, NULL);
    if (rc)
        Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_BuildExportContext);

    ceph_conf_read_file(ceph_export_context->cmount, NULL);

    /* The mountspec we pass to Ceph's init */
    if (snprintf(ceph_export_context->mount, FSAL_MAX_PATH_LEN, "%s:%s",
                 global_spec_info.cephserver, export_path->path) >=
        FSAL_MAX_PATH_LEN)
        Return(ERR_FSAL_NAMETOOLONG, 0, INDEX_FSAL_BuildExportContext);

    /* This has worked so far. */
    argv[0] = procname;
    argv[1] = ceph_export_context->mount;

    ceph_conf_parse_argv(ceph_export_context->cmount, argc,
                         (const char**) argv);

    rc = ceph_mount(ceph_export_context->cmount, NULL);
    if (rc)
        Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_InitClientContext);

  /* Save pointer to fsal_staticfsinfo_t in export context */
  export_context->fe_static_fs_info = &global_fs_info;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context (in, gpfsfsal_export_context_t)
 */

fsal_status_t CEPHFSAL_CleanUpExportContext(
                              fsal_export_context_t *export_context)
{
  cephfsal_export_context_t* ceph_export_context =
    (cephfsal_export_context_t*) export_context;

  /* clean up and dispose Ceph mount */
  ceph_shutdown(ceph_export_context->cmount);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

/* @} */

