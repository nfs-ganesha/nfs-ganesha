/*
 * Copyright © from 2012 Panasas Inc.
 * Author: Boaz Harrosh <bharrosh@panasas.com>
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
 * License along with this library; if not, email to the Free Software
 * Foundation, Inc., licensing@fsf.org
 *
 * -------------
 */

#include <pthread.h>
#include <stdlib.h>

#include "mds.h"
#include "panfs_um_pnfs.h"
#include "fsal_up.h"

#include "../vfs_methods.h"

/**
 * @file   FSAL_VFS/pnfs_panfs/mds.c
 * @author Boaz Harrosh <bharrosh@panasas.com>
 *
 * @brief pNFS Metadata Server Operations for the PanFS FSAL
 *
 * This file implements the layoutget, layoutreturn, layoutcommit,
 * getdeviceinfo operations support for the PanFS FSAL.
 *
 * In general all this file does is Translates from export_pub / fsal_obj_handle
 * Into an "fd" and calles the appropreate panfs_um_pnfs.h function.
 *
 * This file is edited with the LINUX coding style: (Will be enforced)
 *	- Tab characters of 8 spaces wide
 *	- Lines not longer then 80 chars
 *	- etc ... (See linux Documentation/CodingStyle.txt)
 */

static void _XDR_2_ioctlxdr_read_begin(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	pixdr->xdr_buff = xdr_inline_encode(xdr, 0);
	pixdr->xdr_alloc_len = xdr_size_inline(xdr);
	pixdr->xdr_len = 0;
	LogDebug(COMPONENT_FSAL,
		 "alloc_len=%d xdr_buff=%p",
		 pixdr->xdr_alloc_len,
		 pixdr->xdr_buff);
}

/* We need to update the XDR with the encoded bytes */
static void _XDR_2_ioctlxdr_read_end(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	void *p = xdr_inline_encode(xdr, pixdr->xdr_len);

	LogDebug(COMPONENT_FSAL,
		 "xdr_len=%d xdr_buff_end=%p",
		 pixdr->xdr_len,
		 p);
}

static void _XDR_2_ioctlxdr_write(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	if (xdr) {
		pixdr->xdr_len = xdr_getpos(xdr);
		xdr_setpos(xdr, 0);
		/* return the head of the buffer, and reset the pos again */
		pixdr->xdr_buff = xdr_inline_decode(xdr, pixdr->xdr_len);
	} else {
		/* ensure NULL for next test */
		pixdr->xdr_buff = NULL;
	}
	if (!pixdr->xdr_buff) {
		/* ensure 0 for next assignment */
		pixdr->xdr_len = 0;
	}
	pixdr->xdr_alloc_len = pixdr->xdr_len;
	LogDebug(COMPONENT_FSAL,
		 "xdr_len=%d xdr_buff=%p", pixdr->xdr_len, pixdr->xdr_buff);
}

/*
 * Given a PanFS fsal_export. Return the export's root directory file-descriptor
 */
static inline int _get_root_fd(struct fsal_module *fsal_hdl)
{
	struct fsal_export *exp_hdl;

	exp_hdl = glist_first_entry(&fsal_hdl->exports,
				    struct fsal_export,
				    exports);

	return vfs_get_root_fd(exp_hdl);
}

/*
 * Given a PanFS fsal_obj_handle. Return the file-descriptor of this object.
 * The passed obj_hdl must be of a regular file that was pre-opened for
 * read/write.
 */
static inline int _get_obj_fd(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if (myself->u.file.fd.openflags != FSAL_O_CLOSED)
		return myself->u.file.fd.fd;
	else
		return -1;
}

/*================================= fsal ops ===============================*/
static
size_t fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	LogFullDebug(COMPONENT_FSAL, "Ret => ~0UL");
	return ~0UL;
}

static
nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl, XDR *da_addr_body,
		       const layouttype4 type,
		       const struct pnfs_deviceid *deviceid)
{
	struct pan_ioctl_xdr pixdr;
	int fd = _get_root_fd(fsal_hdl);
	nfsstat4 ret;

	_XDR_2_ioctlxdr_read_begin(da_addr_body, &pixdr);
	ret = panfs_um_getdeviceinfo(fd, &pixdr, type, deviceid);
	if (!ret)
		_XDR_2_ioctlxdr_read_end(da_addr_body, &pixdr);
	LogFullDebug(COMPONENT_FSAL,
		     "deviceid(%"PRIx64") ret => %d",
		     deviceid->devid, ret);
	return ret;
}

/*================================= export ops ===============================*/
static
nfsstat4 getdevicelist(struct fsal_export *exp_hdl, layouttype4 type,
		       void *opaque, bool(*cb) (void *opaque,
						const uint64_t id),
		       struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	LogFullDebug(COMPONENT_FSAL, "ret => %d", NFS4_OK);
	return NFS4_OK;
}

static
void fs_layouttypes(struct fsal_export *exp_hdl, int32_t *count,
		    const layouttype4 **types)
{
	static const layouttype4 supported_layout_type = LAYOUT4_OSD2_OBJECTS;

	*types = &supported_layout_type;
	*count = 1;
	LogFullDebug(COMPONENT_FSAL, "count = 1");
}

uint32_t fs_layout_blocksize(struct fsal_export *exp_hdl)
{
	LogFullDebug(COMPONENT_FSAL,
		     "ret => 9 * 64 * 1024");	/* Should not be called */
	return 9 * 64 * 1024;
}

static
uint32_t fs_maximum_segments(struct fsal_export *exp_hdl)
{
	LogFullDebug(COMPONENT_FSAL, "ret => 1");
	return 1;
}

/*
 * @return ~0UL means client's maximum
 */
static
size_t fs_loc_body_size(struct fsal_export *exp_hdl)
{
	LogFullDebug(COMPONENT_FSAL, "ret => ~0UL");
	return ~0UL;
}

/*================================= handle ops ===============================*/
static
nfsstat4 layoutget(struct fsal_obj_handle *obj_hdl,
		   struct req_op_context *req_ctx, XDR *loc_body,
		   const struct fsal_layoutget_arg *arg,
		   struct fsal_layoutget_res *res)
{
	struct vfs_fsal_obj_handle *myself = container_of(obj_hdl,
							  typeof(*myself),
							  obj_handle);
	struct pan_ioctl_xdr pixdr;
	uint64_t clientid = req_ctx->clientid ? *req_ctx->clientid : 0;
	nfsstat4 ret;

	res->last_segment = true;
	_XDR_2_ioctlxdr_read_begin(loc_body, &pixdr);

	/* Take read lock on object to protect file descriptor. */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

	ret = panfs_um_layoutget(_get_obj_fd(obj_hdl), &pixdr, clientid,
				 myself, arg, res);
	if (!ret)
		_XDR_2_ioctlxdr_read_end(loc_body, &pixdr);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	LogDebug(COMPONENT_FSAL,
		 "layout[0x%lx,0x%lx,0x%x] ret => %d", res->segment.offset,
		 res->segment.length, res->segment.io_mode, ret);
	return ret;
}

static
nfsstat4 layoutreturn(struct fsal_obj_handle *obj_hdl,
		      struct req_op_context *req_ctx, XDR *lrf_body,
		      const struct fsal_layoutreturn_arg *arg)
{
	struct pan_ioctl_xdr pixdr;
	nfsstat4 ret;

	LogDebug(COMPONENT_FSAL,
		 "reclaim=%d return_type=%d fsal_seg_data=%p dispose=%d last_segment=%d ncookies=%zu",
		 arg->circumstance, arg->return_type, arg->fsal_seg_data,
		 arg->dispose, arg->last_segment, arg->ncookies);

	_XDR_2_ioctlxdr_write(lrf_body, &pixdr);

	/* Take read lock on object to protect file descriptor. */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

	ret = panfs_um_layoutreturn(_get_obj_fd(obj_hdl), &pixdr, arg);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	LogDebug(COMPONENT_FSAL,
		 "layout[0x%lx,0x%lx,0x%x] ret => %d",
		 arg->cur_segment.offset, arg->cur_segment.length,
		 arg->cur_segment.io_mode, ret);
	return ret;
}

static
nfsstat4 layoutcommit(struct fsal_obj_handle *obj_hdl,
		      struct req_op_context *req_ctx, XDR *lou_body,
		      const struct fsal_layoutcommit_arg *arg,
		      struct fsal_layoutcommit_res *res)
{
	struct pan_ioctl_xdr pixdr;
	nfsstat4 ret;

	_XDR_2_ioctlxdr_write(lou_body, &pixdr);

	/* Take read lock on object to protect file descriptor. */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

	ret = panfs_um_layoutcommit(_get_obj_fd(obj_hdl), &pixdr, arg, res);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	LogDebug(COMPONENT_FSAL,
		 "layout[0x%lx,0x%lx,0x%x] last_write=0x%lx ret => %d",
		 arg->segment.offset, arg->segment.length, arg->segment.io_mode,
		 arg->last_write, ret);
	return ret;
}

static void initiate_recall(struct vfs_fsal_obj_handle *myself,
			    struct pnfs_segment *seg, void *r_cookie)
{
	struct pnfs_segment up_segment = *seg;
	struct gsh_buffdesc handle = {
		.addr = myself->handle->handle_data,
		.len = myself->handle->handle_len
	};
	up_segment.io_mode = LAYOUTIOMODE4_ANY; /*TODO: seg->io_mode */

	/* For layoutrecall up_ops are probably set to default recieved at
	 * vfs_create_export
	 */
	myself->up_ops->layoutrecall(myself->up_ops->export,
				     &handle, LAYOUT4_OSD2_OBJECTS,
				     false, &up_segment, r_cookie, NULL);

}

struct _recall_thread {
	pthread_t thread;
	int fd;
	/** @toodo: not sure if this needs to be volatile */
	volatile bool stop;
};

static void *callback_thread(void *callback_info)
{
	struct _recall_thread *_rt = callback_info;
	enum { E_MAX_EVENTS = 128 };
	struct pan_cb_layoutrecall_event events[E_MAX_EVENTS];
	int err = 0;

	rcu_register_thread();
	while (!_rt->stop) {
		int num_events = 0;
		int e;

		err =
		    panfs_um_recieve_layoutrecall(_rt->fd, events, E_MAX_EVENTS,
						  &num_events);

		if (err) {
			LogDebug(COMPONENT_FSAL,
				 "callback thread: => %d (%s)", err,
				 strerror(err));
			break;
		}

		for (e = 0; e < num_events; ++e) {
			struct vfs_fsal_obj_handle *myself =
			    events[e].recall_file_info;
			struct pnfs_segment seg = events[e].seg;
			void *r_cookie = events[e].cookie;

			LogDebug(COMPONENT_FSAL,
				 "%d] layout[0x%lx,0x%lx,0x%x] myself=%p r_cookie=%p",
				 e, seg.offset, seg.length, seg.io_mode, myself,
				 r_cookie);

			initiate_recall(myself, &seg, r_cookie);
		}
	}
	rcu_unregister_thread();
	return (void *)(long)err;
}

static int _start_callback_thread(int root_fd, void **pnfs_data)
{
	struct _recall_thread *_rt;
	int err;

	_rt = gsh_calloc(1, sizeof(*_rt));

	_rt->fd = root_fd;

	err = pthread_create(&_rt->thread, NULL, &callback_thread, _rt);
	if (err) {
		LogCrit(COMPONENT_FSAL,
			"Could not create callback thread %d: %s",
			err, strerror(err));
		goto error;
	}

	*pnfs_data = _rt;
	LogDebug(COMPONENT_FSAL,
		 "Started callback thread 0x%lx", (long)_rt->thread);
	return 0;

 error:
	gsh_free(_rt);
	return err;
}

static void _stop_callback_thread(void *td)
{
	struct _recall_thread *_rt = td;
	void *tret;

	_rt->stop = true;
	panfs_um_cancel_recalls(_rt->fd, 0);
	pthread_join(_rt->thread, &tret);
	LogDebug(COMPONENT_FSAL,
		 "Stopped callback thread. Join ret => %ld", (long)tret);
	gsh_free(_rt);
}

/*============================== initialization ==============================*/
void export_ops_pnfs(struct export_ops *ops)
{
	ops->getdevicelist = getdevicelist;
	ops->fs_layouttypes = fs_layouttypes;
	ops->fs_layout_blocksize = fs_layout_blocksize;
	ops->fs_maximum_segments = fs_maximum_segments;
	ops->fs_loc_body_size = fs_loc_body_size;
	LogFullDebug(COMPONENT_FSAL, "Init'd export vector");
}

void handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = layoutget;
	ops->layoutreturn = layoutreturn;
	ops->layoutcommit = layoutcommit;
	LogDebug(COMPONENT_FSAL, "Init'd handle vector");
}

void fsal_ops_pnfs(struct fsal_ops *ops)
{
	ops->getdeviceinfo = getdeviceinfo;
	ops->fs_da_addr_size = fs_da_addr_size;
	LogDebug(COMPONENT_FSAL, "Init'd fsal vector");
}

int pnfs_panfs_init(int root_fd, void **pnfs_data /*OUT*/)
{
	int err = _start_callback_thread(root_fd, pnfs_data);
	return err;
}

void pnfs_panfs_fini(void *pnfs_data)
{
	if (!pnfs_data)
		return;
	_stop_callback_thread(pnfs_data);
}
