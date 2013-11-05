/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#include "config.h"

#include "fsal.h"

#include "fsal_internal.h"
#include "abstract_mem.h"

#include <pthread.h>

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;

libzfswrap_handle_t *p_zhd;
