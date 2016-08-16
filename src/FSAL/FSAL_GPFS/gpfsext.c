/**------------------------------------------------------------------------
 * @file gpfsext.c
 * @brief Use ioctl to call into the GPFS kernel module.
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

		VALGRIND_MAKE_MEM_DEFINED(arg->handle->f_handle,
					  arg->handle->handle_size);
		break;
	}
	case OPENHANDLE_GET_HANDLE:
	{
		struct get_handle_arg *arg = (void *)args->arg2;

		VALGRIND_MAKE_MEM_DEFINED(arg->out_fh->f_handle,
					  arg->out_fh->handle_size);
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

		VALGRIND_MAKE_MEM_DEFINED(arg->new_fh->f_handle,
					  arg->new_fh->handle_size);
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

/**
 *  @param op Operation
 *  @param *oarg Arguments
 *
 *  @return Result
*/
int gpfs_ganesha(int op, void *oarg)
{
	int rc;
	static int gpfs_fd = -1;
	struct kxArgs args;

	if (gpfs_fd < 0) {
		gpfs_fd = open(GPFS_DEVNAMEX, O_RDONLY);
		if (gpfs_fd < 0)
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
	rc = ioctl(gpfs_fd, kGanesha, &args);

	return rc;
}
