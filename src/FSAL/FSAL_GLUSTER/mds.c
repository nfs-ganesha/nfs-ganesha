/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2014
 * Author: Jiffin Tony Thottan jthottan@redhat.com
 *         Anand Subramanian   anands@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_up.h"
#include "gsh_rpc.h"
#include "gluster_internal.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "fsal_convert.h"
#include "pnfs_utils.h"
#include "nfs_exports.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#define get16bits(d) (*((const uint16_t *) (d)))
#define MAX_DS_COUNT 100

/**
 * @brief Get layout types supported by export
 *
 * We just return a pointer to the single type and set the count to 1.
 *
 * @param[in]  export_pub Public export handle
 * @param[out] count      Number of layout types in array
 * @param[out] types      Static array of layout types that must not be
 *                        freed or modified and must not be dereferenced
 *                        after export reference is relinquished
 */

static void fs_layouttypes(struct fsal_export *export_pub, int32_t *count,
			   const layouttype4 **types)
{
	/* Only supported layout type is file */
	static const layouttype4 supported_layout_type = LAYOUT4_NFSV4_1_FILES;
	*types = &supported_layout_type;
	*count = 1;
}

/**
 * @brief Get layout block size for export
 *
 * This function just returns the Gluster default.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 4 MB.
 */

static uint32_t fs_layout_blocksize(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Maximum number of segments we will use
 *
 * Since current clients only support 1, that's what we'll use.
 *
 * @param[in] export_pub Public export handle
 *
 * @return 1
 */

static uint32_t fs_maximum_segments(struct fsal_export *export_pub)
{
	return 1;
}

/**
 * @brief Size of the buffer needed for a loc_body
 *
 * Just a handle plus a bit.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a loc_body
 */

static size_t fs_loc_body_size(struct fsal_export *export_pub)
{
	return 0x100;
}

/**
 * @brief Size of the buffer needed for a ds_addr
 *
 * This one is huge, due to the striping pattern.
 *
 * @param[in] export_pub Public export handle
 *
 * @return Size of the buffer needed for a ds_addr
 */

size_t fs_da_addr_size(struct fsal_module *fsal_hdl)
{
	return 0x1400;
}


int glfs_get_ds_addr(struct glfs *fs, struct glfs_object *object,
			uint32_t *ds_addr);
/**
 * @brief Grant a layout segment.
 *
 * Grants whole layout of the file requested.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */

static nfsstat4 pnfs_layout_get(struct fsal_obj_handle          *obj_pub,
				struct req_op_context           *req_ctx,
				XDR                             *loc_body,
				const struct fsal_layoutget_arg *arg,
				struct fsal_layoutget_res       *res)
{

	struct glusterfs_export *export =
		container_of(req_ctx->fsal_export,
			     struct glusterfs_export, export);

	struct glusterfs_handle *handle =
		container_of(obj_pub, struct glusterfs_handle, handle);
	int    rc = 0;
	/* Structure containing the storage parameters of the file within
	   glusterfs. */
	struct glfs_file_layout file_layout;
	/* Utility parameter */
	nfl_util4               util = 0;
	/* Stores Data server address */
	struct pnfs_deviceid    deviceid = DEVICE_ID_INIT_ZERO(FSAL_ID_GLUSTER);
	nfsstat4                nfs_status = NFS4_OK;
	/* Descriptor for DS handle */
	struct gsh_buffdesc     ds_desc;
	/* DS wire handle send to client */
	struct glfs_ds_wire     ds_wire;

	/* Supports only LAYOUT4_NFSV4_1_FILES layouts */
	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogMajor(COMPONENT_PNFS, "Unsupported layout type: %x",
			 arg->type);

		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	memset(&file_layout, 0, sizeof(struct glfs_file_layout));

	/**
	 * Currently whole file is given as file layout,
	 *
	 * Stripe type is dense which is supported right now.
	 * Stripe length is max possible length of file that
	 * can be accessed by the client to perform a read or
	 * write.
	 */

	file_layout.stripe_type  = NFL4_UFLG_DENSE;

	file_layout.stripe_length = 0x100000;

	util |= file_layout.stripe_type | file_layout.stripe_length;

	/* @todo need to handle IPv6 here */
	rc = glfs_get_ds_addr(export->gl_fs->fs, handle->glhandle,
				&deviceid.device_id4);

	if (rc) {
		LogMajor(COMPONENT_PNFS, "Invalid hostname for DS");
		return NFS4ERR_INVAL;
	}

	/** @todo: When more than one client tries access the same layout
	 *       for the write operation, then last write will overwrite
	 *       for the write operation, then last write will overwrite
	 *       the previous ones, the MDS should intelligently deal
	 *       those scenarios
	 */



	/* We return exactly one wirehandle, filling in the necessary
	 * information for the DS server to speak to the gluster bricks
	 * For this, wire handle stores gfid and file layout
	 */

	rc = glfs_h_extract_handle(handle->glhandle, ds_wire.gfid,
				   GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		rc = errno;
		LogMajor(COMPONENT_PNFS, "Invalid glfs_object");
		return posix2nfs4_error(rc);
	}

	ds_wire.layout   = file_layout;
	ds_desc.addr     = &ds_wire;
	ds_desc.len      = sizeof(struct glfs_ds_wire);
	nfs_status = FSAL_encode_file_layout(loc_body, &deviceid, util, 0, 0,
					     &req_ctx->ctx_export->export_id, 1,
					     &ds_desc);
	if (nfs_status) {
		LogMajor(COMPONENT_PNFS,
			  "Failed to encode nfsv4_1_file_layout.");
		goto out;
	}

	/* We grant only one segment, and we want it back
	 * when the file is closed.
	 */
	res->return_on_close    = true;
	res->last_segment       = true;

out:
	return nfs_status;
}

/**
 * @brief Potentially return one layout segment
 *
 * Since we don't make any reservations, in this version, or get any
 * pins to release, always succeed
 *
 * @param[in] obj_pub  Public object handle
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body Nothing for us
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */

static nfsstat4 pnfs_layout_return(struct fsal_obj_handle *obj_pub,
				   struct req_op_context *req_ctx,
				   XDR *lrf_body,
				   const struct fsal_layoutreturn_arg *arg)
{

	if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
		LogDebug(COMPONENT_PNFS, "Unsupported layout type: %x",
				  arg->lo_type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	return NFS4_OK;
}

/**
 * @brief Commit a segment of a layout
 *
 * Update the size and time for a file accessed through a layout.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */

static nfsstat4 pnfs_layout_commit(struct fsal_obj_handle *obj_pub,
				   struct req_op_context *req_ctx,
				   XDR *lou_body,
				   const struct fsal_layoutcommit_arg *arg,
				   struct fsal_layoutcommit_res *res)
{
	/* Old stat, so we don't truncate file or reverse time */
	struct stat old_stat;
	/* new stat to set time and size */
	struct stat new_stat;
	struct glusterfs_export *glfs_export =
		container_of(op_ctx->fsal_export,
				struct glusterfs_export, export);
	struct glusterfs_handle *objhandle   =
		container_of(obj_pub, struct glusterfs_handle, handle);
	/* Mask to determine exactly what gets set */
	int   mask                           = 0;
	int   rc                             = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	if (arg->type != LAYOUT4_NFSV4_1_FILES) {
		LogMajor(COMPONENT_PNFS, "Unsupported layout type: %x",
					   arg->type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	/* Gets previous status of file in the MDS */
	rc = glfs_h_stat(glfs_export->gl_fs->fs,
			 objhandle->glhandle, &old_stat);

	if (rc != 0) {
		LogMajor(COMPONENT_PNFS,
			 "Commit layout, stat unsucessfully completed");
		return NFS4ERR_INVAL;
	}
	memset(&new_stat, 0, sizeof(struct stat));
	/* Set the new attributes for the file if it is changed */
	if (arg->new_offset) {
		if (old_stat.st_size < arg->last_write + 1) {
			new_stat.st_size = arg->last_write + 1;
			res->size_supplied = true;
			res->new_size = arg->last_write + 1;
			rc = glfs_h_truncate(glfs_export->gl_fs->fs,
					     objhandle->glhandle,
					     res->new_size);
			if (rc != 0) {
				LogMajor(COMPONENT_PNFS,
					 "Commit layout, size changed unsucessfully completed");
				return NFS4ERR_INVAL;
			}
		}
	}

	if ((arg->time_changed) &&
		(arg->new_time.seconds > old_stat.st_mtime))
		new_stat.st_mtime = arg->new_time.seconds;
	else
		new_stat.st_mtime = time(NULL);


	mask |= GLAPI_SET_ATTR_MTIME;

	SET_GLUSTER_CREDS(glfs_export, &op_ctx->creds->caller_uid,
			  &op_ctx->creds->caller_gid,
			  op_ctx->creds->caller_glen,
			  op_ctx->creds->caller_garray,
			  op_ctx->client->addr.addr,
			  op_ctx->client->addr.len);

	rc = glfs_h_setattrs(glfs_export->gl_fs->fs,
			     objhandle->glhandle,
			     &new_stat,
			     mask);

	SET_GLUSTER_CREDS(glfs_export, NULL, NULL, 0, NULL, NULL, 0);

	if ((rc != 0) || (status.major != ERR_FSAL_NO_ERROR)) {
		LogMajor(COMPONENT_PNFS,
			 "commit layout, setattr unsucessflly completed");
		return NFS4ERR_INVAL;
	}
	res->commit_done = true;

	return NFS4_OK;
}

/**
 * @brief Describes the DS information for the client
 *
 * @param[in]  export_pub   Public export handle
 * @param[out] da_addr_body Stream we write the result to
 * @param[in]  type         Type of layout that gave the device
 * @param[in]  deviceid     The device to look up
 *
 * @return Valid error codes in RFC 5661, p. 365.
 */
nfsstat4 getdeviceinfo(struct fsal_module *fsal_hdl,
		       XDR *da_addr_body, const layouttype4 type,
		       const struct pnfs_deviceid *deviceid)
{
	nfsstat4                    nfs_status = 0;
	/* Stores IP address of DS */
	fsal_multipath_member_t     host;

	/* Entire file layout will be situated inside ONE DS
	 * And whole file is provided to the DS, so the starting
	 * index for that file is zero
	 */
	unsigned int num_ds = 1;
	uint32_t stripes = 1;
	uint32_t stripe_ind = 0;


	if (type != LAYOUT4_NFSV4_1_FILES) {
		LogMajor(COMPONENT_PNFS, "Unsupported layout type: %x", type);
		return NFS4ERR_UNKNOWN_LAYOUTTYPE;
	}

	if (!inline_xdr_u_int32_t(da_addr_body, &stripes)) {
		LogMajor(COMPONENT_PNFS,
			 "Failed to encode length of stripe_indices array: %"
			  PRIu32 ".", stripes);
		return NFS4ERR_SERVERFAULT;
	}

	if (!inline_xdr_u_int32_t(da_addr_body, &stripe_ind)) {
		LogMajor(COMPONENT_PNFS,
			 "Failed to encode ds for the stripe:  %"
			 PRIu32 ".", stripe_ind);
		return NFS4ERR_SERVERFAULT;
	}

	if (!inline_xdr_u_int32_t(da_addr_body, &num_ds)) {
		LogMajor(COMPONENT_PNFS,
			 "Failed to encode length of multipath_ds_list array: %u",
			 num_ds);
		return NFS4ERR_SERVERFAULT;
	}
	memset(&host, 0, sizeof(fsal_multipath_member_t));
	host.addr = ntohl(deviceid->device_id4);
	host.port = 2049;
	host.proto = 6;
	nfs_status = FSAL_encode_v4_multipath(da_addr_body, 1, &host);

	if (nfs_status != NFS4_OK) {
		LogMajor(COMPONENT_PNFS,
			 "Failed to encode data server address");
	return nfs_status;
	}

	/** @todo: Here information about Data-Server where file resides
	 *        is only send from MDS.If that Data-Server is down then
	 *        read or write will performed through MDS.
	 *        Instead should we send the information about all
	 *        the available data-servers, so that these fops will
	 *        always performed through Data-Servers.
	 *        (Like in replicated volume contains more than ONE DS)
	 */
	return NFS4_OK;
}

/**
 * @brief Get list of available devices
 *
 * We do not support listing devices and just set EOF without doing
 * anything.
 *
 * @param[in]     export_pub Export handle
 * @param[in]     type      Type of layout to get devices for
 * @param[in]     cb        Function taking device ID halves
 * @param[in,out] res       In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 365-6.
 */
static nfsstat4 getdevicelist(struct fsal_export *export_pub, layouttype4 type,
			      void *opaque,
			      bool (*cb)(void *opaque, const uint64_t id),
			      struct fsal_getdevicelist_res *res)
{
	res->eof = true;
	return NFS4_OK;
}


void handle_ops_pnfs(struct fsal_obj_ops *ops)
{
	ops->layoutget     = pnfs_layout_get;
	ops->layoutreturn  = pnfs_layout_return;
	ops->layoutcommit  = pnfs_layout_commit;
}

void fsal_ops_pnfs(struct fsal_ops *ops)
{
	ops->getdeviceinfo = getdeviceinfo;
	ops->fs_da_addr_size = fs_da_addr_size;
}

void export_ops_pnfs(struct export_ops *ops)
{
	ops->getdevicelist              = getdevicelist;
	ops->fs_layouttypes             = fs_layouttypes;
	ops->fs_layout_blocksize        = fs_layout_blocksize;
	ops->fs_maximum_segments        = fs_maximum_segments;
	ops->fs_loc_body_size           = fs_loc_body_size;
}

/* *
 * Calculates  a hash value for a given string buffer
 */
uint32_t superfasthash(const unsigned char *data, uint32_t len)
{
	uint32_t hash = len, tmp;
	int32_t rem;

	rem = len & 3;
	len >>= 2;

	/* Main loop */
	for (; len > 0; len--) {
		hash  += get16bits(data);
		tmp    = (get16bits(data+2) << 11) ^ hash;
		hash   = (hash << 16) ^ tmp;
		data  += 2*sizeof(uint16_t);
		hash  += hash >> 11;
	}

	/* Handle end cases */
	switch (rem) {
	case 3:
		hash += get16bits(data);
		hash ^= hash << 16;
		hash ^= data[sizeof(uint16_t)] << 18;
		hash += hash >> 11;
		break;
	case 2:
		hash += get16bits(data);
		hash ^= hash << 11;
		hash += hash >> 17;
		break;
	case 1:
		hash += *data;
		hash ^= hash << 10;
		hash += hash >> 1;
	}

	/* Force "avalanching" of final 127 bits */
	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}
/**
 * It will extract hostname from pathinfo.PATH_INFO_KEYS gives
 * details about all the servers and path in that server where
 * file resides.
 * First it selects the DS based on distributed hashing, then
 * with the help of some basic string manipulations, the hostname
 * can be fetched from the pathinfo
 *
 * Returns zero and valid hostname on success
 */

int
select_ds(struct glfs_object *object, char *pathinfo, char *hostname,
	  size_t size)
{
	/* Represents starting of each server in the list*/
	const char posix[10]    = "POSIX";
	/* Array of pathinfo of available dses */
	char	*ds_path_info[MAX_DS_COUNT];
	/* Key for hashing */
	unsigned char key[16];
	/* Starting of first brick path in the pathinfo */
	char	*tmp		= NULL;
	/* Stores starting of hostname */
	char    *start          = NULL;
	/* Stores ending of hostname */
	char    *end            = NULL;
	int     ret             = -1;
	int     i               = 0;
	/* counts no of available ds */
	int	no_of_ds	= 0;

	if (!pathinfo || !size)
		goto out;

	tmp = pathinfo;
	while ((tmp = strstr(tmp, posix))) {
		ds_path_info[no_of_ds] = tmp;
		tmp++;
		no_of_ds++;
		/* *
		 * If no of dses reaches maxmium count, then
		 * perform load balance on current list
		 */
		if (no_of_ds == MAX_DS_COUNT)
			break;
	}

	if (no_of_ds == 0) {
		LogCrit(COMPONENT_PNFS,
			"Invalid pathinfo(%s) attribute found while selecting DS.",
			pathinfo);
		goto out;
	}

	ret = glfs_h_extract_handle(object, key, GFAPI_HANDLE_LENGTH);
	if (ret < 0)
		goto out;

	/* Pick DS from the list */
	if (no_of_ds == 1)
		ret = 0;
	else
		ret = superfasthash(key, 16) % no_of_ds;

	start = strchr(ds_path_info[ret], ':');
	if (!start)
		goto out;
	end = start + 1;
	end = strchr(end, ':');
	if (start == end)
		goto out;

	memset(hostname, 0, size);

	while (++start != end)
		hostname[i++] = *start;
	ret = 0;
	LogDebug(COMPONENT_PNFS, "hostname %s", hostname);

out:
	return ret;
}

/*
 * The data server address will be send from here
 *
 * The information about the first server present
 * in the PATH_INFO_KEY will be returned, since
 * entire file is consistent over the servers
 * (Striped volumes are not considered right now)
 *
 * On success, returns zero with ip address of
 * the server will be send
 */
int
glfs_get_ds_addr(struct glfs *fs, struct glfs_object *object, uint32_t *ds_addr)
{
	int             ret                    = 0;
	char            pathinfo[1024]         = {0, };
	char            hostname[256]          = {0, };
	char		scratch[SOCK_NAME_MAX] = {0, };
	struct addrinfo hints                  = {0, };
	struct addrinfo *res                   = NULL;
	const char      *pathinfokey           = "trusted.glusterfs.pathinfo";

	ret = glfs_h_getxattrs(fs, object, pathinfokey, pathinfo,
			       sizeof(pathinfo));

	LogDebug(COMPONENT_PNFS, "pathinfo %s", pathinfo);

	ret = select_ds(object, pathinfo, hostname, sizeof(hostname));
	if (ret) {
		LogMajor(COMPONENT_PNFS, "No DS found");
		goto out;
	}

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_INET;
	ret = getaddrinfo(hostname, NULL, &hints, &res);
	/* we trust getaddrinfo() never returns EAI_AGAIN! */
	if (ret != 0) {
		*ds_addr = 0;
		LogMajor(COMPONENT_PNFS, "error %s\n", gai_strerror(ret));
		goto out;
	}
	sprint_sockip((sockaddr_t *)res->ai_addr, scratch, sizeof(scratch));
	LogDebug(COMPONENT_PNFS, "ip address : %s", scratch);
	*ds_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr;
out:
	freeaddrinfo(res);
	return ret;
}
