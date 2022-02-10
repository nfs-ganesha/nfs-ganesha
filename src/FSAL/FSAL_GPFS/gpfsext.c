// SPDX-License-Identifier: LGPL-3.0-or-later
/**------------------------------------------------------------------------
 * @file gpfsext.c
 * @brief Use ioctl to call into the GPFS kernel module.
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
 *
 * NAME:        gpfs_ganesha()
 *
 * FUNCTION:    Use ioctl to call into the GPFS kernel module.
 *              If GPFS isn't loaded they receive ENOSYS.
 *
 * Returns:      0      Successful
 *              -1      Failure
 *
 * Errno:       ENOSYS  No quality of service function available
 *              ENOENT  File not found
 *              EINVAL  Not a GPFS file
 *              ESTALE  cached fs information was invalid
 *-------------------------------------------------------------------------*/

#include "config.h"
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _VALGRIND_MEMCHECK
#include <valgrind/memcheck.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include "include/gpfs.h"
#endif
#include "fsal.h"

#include "include/gpfs_nfs.h"
#include "gsh_config.h"

struct kxArgs {
	signed long arg1;
	signed long arg2;
};

#ifdef _VALGRIND_MEMCHECK
static void valgrind_kganesha(struct kxArgs *args)
{
	int op = (int)args->arg1;

	switch (op) {
	case OPENHANDLE_STATFS_BY_FH:
	{
		struct statfs_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->buf, sizeof(*arg->buf));
		break;
	}
	case OPENHANDLE_READ_BY_FD:
	{
		struct read_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->bufP, arg->length);
		break;
	}
	case OPENHANDLE_NAME_TO_HANDLE:
	{
		struct name_handle_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->handle,
					  sizeof(struct gpfs_file_handle));
		break;
	}
	case OPENHANDLE_GET_HANDLE:
	{
		struct get_handle_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->out_fh,
					  sizeof(struct gpfs_file_handle));
		break;
	}
	case OPENHANDLE_STAT_BY_NAME:
	{
		struct stat_name_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->buf, sizeof(*arg->buf));
		break;
	}
	case OPENHANDLE_CREATE_BY_NAME:
	{
		struct create_name_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->new_fh,
					  sizeof(struct gpfs_file_handle));
		break;
	}
	case OPENHANDLE_READLINK_BY_FH:
	{
		struct readlink_fh_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->buffer, arg->size);
		break;
	}
	case OPENHANDLE_GET_XSTAT:
	{
		struct xstat_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->buf, sizeof(*arg->buf));
		VALGRIND_MAKE_MEM_DEFINED(arg->fsid, sizeof(*arg->fsid));
		if (arg->acl) {
			struct gpfs_acl *gacl;
			size_t outlen;

			/*
			 * arg->acl points to an IN/OUT buffer. First
			 * few fields are initialized by the caller and
			 * the rest are filled in by the ioctl call.
			 */
			gacl = arg->acl;
			outlen = gacl->acl_len -
				offsetof(struct gpfs_acl, acl_nace);
			VALGRIND_MAKE_MEM_DEFINED(&gacl->acl_nace, outlen);
		}
		break;
	}
	case OPENHANDLE_WRITE_BY_FD:
	{
		struct write_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->stability_got,
					  sizeof(*arg->stability_got));
		break;
	}
	default:
		break;
	}
}
#endif

int gpfs_op2index(int op)
{
	if ((op < GPFS_MIN_OP) || (op > GPFS_MAX_OP) ||
	    (op == 103 || op == 104 || op == 105))
		return GPFS_STAT_PH_INDEX;
	else
		return (op - GPFS_MIN_OP);
}

/**
 *  @param op Operation
 *  @param *oarg Arguments
 *
 *  @return Result
*/
int gpfs_ganesha(int op, void *oarg)
{
	int rc, idx;
	static int gpfs_fd = -2;
	struct kxArgs args;
	struct timespec start_time;
	struct timespec stop_time;
	nsecs_elapsed_t resp_time;

	if (gpfs_fd < 0) {
		/* If we enable fsal_trace in the config, the following
		 * LogFatal would call us here again for fsal tracing!
		 * Since we can't log as we are unable to open the device,
		 * just exit.
		 *
		 * Also, exit handler _dl_fini() will call gpfs_unload
		 * which will call release_log_facility that tries to
		 * acquire log_rwlock a second time! So do an immediate
		 * exit.
		 */
		if (gpfs_fd == -1) /* failed in a prior invocation */
			_exit(1);

		assert(gpfs_fd == -2);
		gpfs_fd = open(GPFS_DEVNAMEX, O_RDONLY);
		if (gpfs_fd == -1)
			LogFatal(COMPONENT_FSAL,
				"open of %s failed with errno %d",
				GPFS_DEVNAMEX, errno);
		(void)fcntl(gpfs_fd, F_SETFD, FD_CLOEXEC);
	}

	args.arg1 = op;
	args.arg2 = (long)oarg;
#ifdef _VALGRIND_MEMCHECK
	valgrind_kganesha(&args);
#endif
	if (nfs_param.core_param.enable_FSALSTATS) {
		/* Collect FSAL stats */
		now(&start_time);
		rc = ioctl(gpfs_fd, kGanesha, &args);
		now(&stop_time);
		resp_time = timespec_diff(&start_time, &stop_time);

		/* record FSAL stats */
		idx = gpfs_op2index(op);
		(void)atomic_inc_uint64_t(&gpfs_stats.op_stats[idx].num_ops);
		(void)atomic_add_uint64_t(&gpfs_stats.op_stats[idx].resp_time,
				  resp_time);
		if (gpfs_stats.op_stats[idx].resp_time_max < resp_time)
			gpfs_stats.op_stats[idx].resp_time_max = resp_time;
		if (gpfs_stats.op_stats[idx].resp_time_min == 0 ||
		    gpfs_stats.op_stats[idx].resp_time_min > resp_time)
			gpfs_stats.op_stats[idx].resp_time_min = resp_time;

	} else {
		rc = ioctl(gpfs_fd, kGanesha, &args);
	}
	return rc;
}
