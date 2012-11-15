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

#include "mds.h"
#include "panfs_um_pnfs.h"

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

/*#define CONFIG_PANFS_DEBUG y
*/

#define PANFS_ERR(fmt, a...) printf("fsal_panfs: " fmt, ##a)

#ifdef CONFIG_PANFS_DEBUG
#define DBG_PRNT(fmt, a...) \
	printf("fsal_panfs @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define DBG_PRNT(fmt, a...) \
	do { if (0) printf(fmt, ##a); } while (0)
#endif

/* FIXME: We assume xdrmem. How to do this generic I don't know */
static void _XDR_2_ioctlxdr_read_begin(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	pixdr->xdr_buff = xdr->x_private;
	pixdr->xdr_alloc_len = xdr->x_handy;
	pixdr->xdr_len = 0;
	DBG_PRNT("alloc_len=%d x_private=%p\n",
		pixdr->xdr_alloc_len, xdr->x_private);
}

/* We need to update the XDR with the encoded bytes */
static void _XDR_2_ioctlxdr_read_end(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	xdr->x_handy -= pixdr->xdr_len;
	xdr->x_private = (char *)xdr->x_private + pixdr->xdr_len;
	DBG_PRNT("xdr_len=%d x_private=%p\n", pixdr->xdr_len, xdr->x_private);
}

static void _XDR_2_ioctlxdr_write(XDR *xdr, struct pan_ioctl_xdr *pixdr)
{
	pixdr->xdr_len = xdr ? xdr_getpos(xdr) : 0;
	if (pixdr->xdr_len && xdr->x_base) {
		pixdr->xdr_buff = xdr->x_base;
		pixdr->xdr_alloc_len = pixdr->xdr_len;
	} else {
		pixdr->xdr_buff = NULL;
		pixdr->xdr_alloc_len = pixdr->xdr_len = 0;
	}
	DBG_PRNT("xdr_len=%d xdr_buff=%p\n", pixdr->xdr_len, pixdr->xdr_buff);
}

/*
 * Given a PanFS fsal_export. Return the export's root directory file-descriptor
 */
static inline
int _get_root_fd(struct fsal_export *exp_hdl)
{
	return vfs_get_root_fd(exp_hdl);
}

/*
 * Given a PanFS fsal_obj_handle. Return the file-descriptor of this object.
 * The passed obj_hdl must be of a regular file that was pre-opened for
 * read/write.
 */
static inline
int _get_obj_fd(struct fsal_obj_handle *obj_hdl)
{
	struct vfs_fsal_obj_handle *myself;

	myself = container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	if(myself->u.file.fd >= 0 && myself->u.file.openflags != FSAL_O_CLOSED)
		return myself->u.file.fd;
	else
		return -1;
}

/*================================= export ops ===============================*/
/*
 * @return ~0UL means client's maximum
 */
static
size_t fs_da_addr_size(struct fsal_export *exp_hdl)
{
	DBG_PRNT("\n");
	return ~0UL;
}

static
nfsstat4 getdeviceinfo(
		struct fsal_export *exp_hdl,
		XDR *da_addr_body,
		const layouttype4 type,
		const struct pnfs_deviceid *deviceid)
{
	struct pan_ioctl_xdr pixdr;
	int fd = _get_root_fd(exp_hdl);
	nfsstat4 ret;

	_XDR_2_ioctlxdr_read_begin(da_addr_body, &pixdr);
	ret = panfs_um_getdeviceinfo(fd, &pixdr, type, deviceid);
	if (!ret)
		_XDR_2_ioctlxdr_read_end(da_addr_body, &pixdr);
	DBG_PRNT("ret => %d\n", ret);
	return ret;
}

static
nfsstat4 getdevicelist(
		struct fsal_export *exp_hdl,
		layouttype4 type,
		void *opaque,
		bool (*cb)(void *opaque, const uint64_t id),
		struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	DBG_PRNT("ret => %d\n", NFS4_OK);
	return NFS4_OK;
}

static
void fs_layouttypes(struct fsal_export *exp_hdl,
                    size_t *count, const layouttype4 **types)
{
        static const layouttype4 supported_layout_type = LAYOUT4_OSD2_OBJECTS;

        *types = &supported_layout_type;
        *count = 1;
	DBG_PRNT("\n");
}

uint32_t fs_layout_blocksize(struct fsal_export *exp_hdl)
{
	DBG_PRNT("\n"); /* Should not be called */
	return 9 * 64 * 1024;
}

static
uint32_t fs_maximum_segments(struct fsal_export *exp_hdl)
{
	DBG_PRNT("\n");
	return 1;
}

/*
 * @return ~0UL means client's maximum
 */
static
size_t fs_loc_body_size(struct fsal_export *exp_hdl)
{
	DBG_PRNT("\n");
	return ~0UL;
}

/*================================= handle ops ===============================*/
static
nfsstat4 layoutget(
	struct fsal_obj_handle *obj_hdl,
	struct req_op_context *req_ctx,
	XDR *loc_body,
	const struct fsal_layoutget_arg *arg,
	struct fsal_layoutget_res *res)
{
	struct pan_ioctl_xdr pixdr;
	nfsstat4 ret;

	_XDR_2_ioctlxdr_read_begin(loc_body, &pixdr);
	ret = panfs_um_layoutget(_get_obj_fd(obj_hdl), &pixdr, arg, res);
	if (!ret)
		_XDR_2_ioctlxdr_read_end(loc_body, &pixdr);
	DBG_PRNT("ret => %d\n", ret);
	return ret;
}

static
nfsstat4 layoutreturn(
	struct fsal_obj_handle *obj_hdl,
	struct req_op_context *req_ctx,
	XDR *lrf_body,
	const struct fsal_layoutreturn_arg *arg)
{
	struct pan_ioctl_xdr pixdr;
	nfsstat4 ret;

	DBG_PRNT("reclaim=%d return_type=%d fsal_seg_data=%p synthetic=%d dispose=%d last_segment=%d ncookies=%zu\n",
		arg->reclaim, arg->return_type, arg->fsal_seg_data,
		arg->synthetic, arg->dispose, arg->last_segment,
		arg->ncookies);

	_XDR_2_ioctlxdr_write(lrf_body, &pixdr);
	ret = panfs_um_layoutreturn(_get_obj_fd(obj_hdl), &pixdr, arg);
	DBG_PRNT("ret => %d\n", ret);
	return ret;
}

static
nfsstat4 layoutcommit(
		struct fsal_obj_handle *obj_hdl,
		struct req_op_context *req_ctx,
		XDR *lou_body,
		const struct fsal_layoutcommit_arg *arg,
		struct fsal_layoutcommit_res *res)
{
	struct pan_ioctl_xdr pixdr;
	nfsstat4 ret;

	_XDR_2_ioctlxdr_write(lou_body, &pixdr);
	ret = panfs_um_layoutcommit(_get_obj_fd(obj_hdl),  &pixdr, arg, res);
	DBG_PRNT("ret => %d\n", ret);
	return ret;
}

/*============================== initialization ==============================*/
void
export_ops_pnfs(struct export_ops *ops)
{
	ops->getdeviceinfo = getdeviceinfo;
	ops->getdevicelist = getdevicelist;
	ops->fs_layouttypes = fs_layouttypes;
	ops->fs_layout_blocksize = fs_layout_blocksize;
	ops->fs_maximum_segments = fs_maximum_segments;
	ops->fs_loc_body_size = fs_loc_body_size;
	ops->fs_da_addr_size = fs_da_addr_size;
	DBG_PRNT("\n");
}

void
handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget = layoutget;
	ops->layoutreturn = layoutreturn;
	ops->layoutcommit = layoutcommit;
	DBG_PRNT("\n");
}
