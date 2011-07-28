/*
 *  pnfs_osd_xdr.h
 *
 *  pNFS-osd on-the-wire data structures
 *
 *  Copyright (C) 2007-2009 Panasas Inc.
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
 *  Boaz Harrosh <bharrosh@panasas.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  See the file COPYING included with this distribution for more details.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __PNFS_OSD_XDR_H__
#define __PNFS_OSD_XDR_H__

/* FIXME: Needed from pnfs_xdr.h
	struct pnfs_deviceid
	struct nfs4_string
*/
#included <pnfs_xdr.h>

#define PNFS_OSD_OSDNAME_MAXSIZE 256

/*
 * draft-ietf-nfsv4-minorversion-22
 * draft-ietf-nfsv4-pnfs-obj-12
 */

/* Layout Structure */

enum pnfs_osd_raid_algorithm4 {
	PNFS_OSD_RAID_0		= 1,
	PNFS_OSD_RAID_4		= 2,
	PNFS_OSD_RAID_5		= 3,
	PNFS_OSD_RAID_PQ	= 4     /* Reed-Solomon P+Q */
};

/*   struct pnfs_osd_data_map4 {
 *       uint32_t                    odm_num_comps;
 *       length4                     odm_stripe_unit;
 *       uint32_t                    odm_group_width;
 *       uint32_t                    odm_group_depth;
 *       uint32_t                    odm_mirror_cnt;
 *       pnfs_osd_raid_algorithm4    odm_raid_algorithm;
 *   };
 */
struct pnfs_osd_data_map {
	u32	odm_num_comps;
	u64	odm_stripe_unit;
	u32	odm_group_width;
	u32	odm_group_depth;
	u32	odm_mirror_cnt;
	u32	odm_raid_algorithm;
};

/*   struct pnfs_osd_objid4 {
 *       deviceid4       oid_device_id;
 *       uint64_t        oid_partition_id;
 *       uint64_t        oid_object_id;
 *   };
 */
struct pnfs_osd_objid {
	struct pnfs_deviceid	oid_device_id;
	u64			oid_partition_id;
	u64			oid_object_id;
};

/* For printout. I use "dev(%llx:%llx)", _DEVID_LO(), _DEVID_HI BE style */
#define _DEVID_LO(oid_device_id) \
	(unsigned long long)be64_to_cpup((__be64 *)oid_device_id.data)

#define _DEVID_HI(oid_device_id) \
	(unsigned long long)be64_to_cpup(((__be64 *)oid_device_id.data) + 1)

enum pnfs_osd_version {
	PNFS_OSD_MISSING              = 0,
	PNFS_OSD_VERSION_1            = 1,
	PNFS_OSD_VERSION_2            = 2
};

struct pnfs_osd_opaque_cred {
	u32 cred_len;
	void *cred;
};

enum pnfs_osd_cap_key_sec {
	PNFS_OSD_CAP_KEY_SEC_NONE     = 0,
	PNFS_OSD_CAP_KEY_SEC_SSV      = 1,
};

/*   struct pnfs_osd_object_cred4 {
 *       pnfs_osd_objid4         oc_object_id;
 *       pnfs_osd_version4       oc_osd_version;
 *       pnfs_osd_cap_key_sec4   oc_cap_key_sec;
 *       opaque                  oc_capability_key<>;
 *       opaque                  oc_capability<>;
 *   };
 */
struct pnfs_osd_object_cred {
	struct pnfs_osd_objid		oc_object_id;
	u32				oc_osd_version;
	u32				oc_cap_key_sec;
	struct pnfs_osd_opaque_cred	oc_cap_key;
	struct pnfs_osd_opaque_cred	oc_cap;
};
/*   struct pnfs_osd_layout4 {
 *       pnfs_osd_data_map4      olo_map;
 *       uint32_t                olo_comps_index;
 *       pnfs_osd_object_cred4   olo_components<>;
 *   };
 */
struct pnfs_osd_layout {
	struct pnfs_osd_data_map	olo_map;
	u32				olo_comps_index;
	u32				olo_num_comps;
	struct pnfs_osd_object_cred	*olo_comps;
};

/* Device Address */
enum pnfs_osd_targetid_type {
	OBJ_TARGET_ANON = 1,
	OBJ_TARGET_SCSI_NAME = 2,
	OBJ_TARGET_SCSI_DEVICE_ID = 3,
};

/*   union pnfs_osd_targetid4 switch (pnfs_osd_targetid_type4 oti_type) {
 *       case OBJ_TARGET_SCSI_NAME:
 *           string              oti_scsi_name<>;
 *
 *       case OBJ_TARGET_SCSI_DEVICE_ID:
 *           opaque              oti_scsi_device_id<>;
 *
 *       default:
 *           void;
 *   };
 *
 *   union pnfs_osd_targetaddr4 switch (bool ota_available) {
 *       case TRUE:
 *           netaddr4            ota_netaddr;
 *       case FALSE:
 *           void;
 *   };
 *
 *   struct pnfs_osd_deviceaddr4 {
 *       pnfs_osd_targetid4      oda_targetid;
 *       pnfs_osd_targetaddr4    oda_targetaddr;
 *       uint64_t                oda_lun;
 *       opaque                  oda_systemid<>;
 *       pnfs_osd_object_cred4   oda_root_obj_cred;
 *       opaque                  oda_osdname<>;
 *   };
 */
struct pnfs_osd_targetid {
	u32				oti_type;
	struct nfs4_string		oti_scsi_device_id;
};

enum { PNFS_OSD_TARGETID_MAX = 1 + PNFS_OSD_OSDNAME_MAXSIZE / 4 };

/*   struct netaddr4 {
 *       // see struct rpcb in RFC1833
 *       string r_netid<>;    // network id
 *       string r_addr<>;     // universal address
 *   };
 */
struct pnfs_osd_net_addr {
	struct nfs4_string	r_netid;
	struct nfs4_string	r_addr;
};

struct pnfs_osd_targetaddr {
	u32				ota_available;
	struct pnfs_osd_net_addr	ota_netaddr;
};

enum {
	NETWORK_ID_MAX = 16 / 4,
	UNIVERSAL_ADDRESS_MAX = 64 / 4,
	PNFS_OSD_TARGETADDR_MAX = 3 +  NETWORK_ID_MAX + UNIVERSAL_ADDRESS_MAX,
};

struct pnfs_osd_deviceaddr {
	struct pnfs_osd_targetid	oda_targetid;
	struct pnfs_osd_targetaddr	oda_targetaddr;
	u8				oda_lun[8];
	struct nfs4_string		oda_systemid;
	struct pnfs_osd_object_cred	oda_root_obj_cred;
	struct nfs4_string		oda_osdname;
};

enum {
	ODA_OSDNAME_MAX = PNFS_OSD_OSDNAME_MAXSIZE / 4,
	PNFS_OSD_DEVICEADDR_MAX =
		PNFS_OSD_TARGETID_MAX + PNFS_OSD_TARGETADDR_MAX +
		2 /*oda_lun*/ +
		1 + OSD_SYSTEMID_LEN +
		1 + ODA_OSDNAME_MAX,
};

/* LAYOUTCOMMIT: layoutupdate */

/*   union pnfs_osd_deltaspaceused4 switch (bool dsu_valid) {
 *       case TRUE:
 *           int64_t     dsu_delta;
 *       case FALSE:
 *           void;
 *   };
 *
 *   struct pnfs_osd_layoutupdate4 {
 *       pnfs_osd_deltaspaceused4    olu_delta_space_used;
 *       bool                        olu_ioerr_flag;
 *   };
 */
struct pnfs_osd_layoutupdate {
	u32	dsu_valid;
	s64	dsu_delta;
	u32	olu_ioerr_flag;
};

/* LAYOUTRETURN: I/O Rrror Report */

enum pnfs_osd_errno {
	PNFS_OSD_ERR_EIO		= 1,
	PNFS_OSD_ERR_NOT_FOUND		= 2,
	PNFS_OSD_ERR_NO_SPACE		= 3,
	PNFS_OSD_ERR_BAD_CRED		= 4,
	PNFS_OSD_ERR_NO_ACCESS		= 5,
	PNFS_OSD_ERR_UNREACHABLE	= 6,
	PNFS_OSD_ERR_RESOURCE		= 7
};

/*   struct pnfs_osd_ioerr4 {
 *       pnfs_osd_objid4     oer_component;
 *       length4             oer_comp_offset;
 *       length4             oer_comp_length;
 *       bool                oer_iswrite;
 *       pnfs_osd_errno4     oer_errno;
 *   };
 */
struct pnfs_osd_ioerr {
	struct pnfs_osd_objid	oer_component;
	u64			oer_comp_offset;
	u64			oer_comp_length;
	u32			oer_iswrite;
	u32			oer_errno;
};

static inline int
pnfs_osd_objid_xdr_sz(void)
{
	return (NFS4_DEVICEID4_SIZE / 4) + 2 + 2;
}

static inline unsigned
pnfs_osd_ioerr_xdr_sz(void)
{
	return pnfs_osd_objid_xdr_sz() + 2 + 2 + 1 + 1;
}

/* OSD XDR API */

/* Layout helpers */

/* Layout decoding is done in two stages:
 * 1. First Call pnfs_osd_xdr_decode_layout_map to read in only the header part
 *    of the layout. @iter members need not be initialized.
 *    Returned:
 *             @layout members are set. (@layout->olo_comps set to NULL).
 *             @iter->alloc_bytes contains the requested size for the buffer
 *              needed at @layout->olo_comps.
 *             Zero on success, or negative error if passed xdr is broken.
 *    (FIXME: until API is converted to struct xdr errors are not detected)
 *
 * 2. 2nd Call pnfs_osd_xdr_decode_layout_comps()
 *    There are two possibilities here:
 *    A. contiguous buffer (easiest, dynamic allocation)
 *       Decode everything into one buffer. Just set @layout->olo_comps to
 *       a buffer as big as @iter->alloc_bytes. And call
 *       pnfs_osd_xdr_decode_layout_comps().
 *       In this case the return code is always success (0)
 *       Example:
 *		struct pnfs_osd_xdr_decode_layout_iter iter;
 *		struct pnfs_osd_layout layout;
 *
 *		pnfs_osd_xdr_decode_layout_map(&layout, &iter, xdr);
 *		... check error return code;
 *		layout->olo_comp = kzalloc(iter->alloc_bytes, ...);
 *		pnfs_osd_xdr_decode_layout_comps(&layout, &iter, xdr);
 *
 *    B. Few comps at a time (more complicated, static allocation)
 *       In this case the user communicates decoding in the @iter members
 *       @iter->alloc_bytes - Is the number of bytes at @iter->extra_bytes
 *       @iter->num_comps   - Is the numbers of comps to decode.
 *                            On return this is updated to num_comps actually
 *                            decoded.
 *       @iter->cur_comp    - Is where to decode the struct pnfs_osd_object_cred
 *                            On return this is updated to After the last
 *                            decoded comp.
 *       @iter->extra_bytes  - Is where to place variable string information
 *                             that belong to the decoded comps.
 *                             This can be NULL in which case the string pointed
 *                             by ->cur_comp will point to the actuall passed in
 *                             XDR buffer. User must make sure to copy or
 *                             discard these strings before the XDR buffer is
 *                             returned.
 *       In system B, the function returns when @iter->num_comps,or all the
 *       layout, was decoded, which ever triggers first.
 *    Returned:
 *       1 if there is more to decode or 0 if we are done.
 *       Example:
 *		struct pnfs_osd_xdr_decode_layout_iter iter;
 *		struct pnfs_osd_layout layout;
 *
 *		pnfs_osd_xdr_decode_layout_map(&layout, &iter, xdr);
 *		do {
 *			struct pnfs_osd_object_cred one_comp;
 *
 *			iter->num_comps = 1;
 *			iter->cur_comp = &one_comp;
 *			// We will use the creds below and discard afterwords
 *			iter->extra_bytes = NULL;
 *			more = pnfs_osd_xdr_decode_layout_comps(&layout, &iter,
 *								xdr);
 *			... use the one_comp structure, Specially copy the creds
 *			to internal buffers ...
 *		} while (more);
 */

struct pnfs_osd_xdr_decode_layout_iter {
	unsigned decoded_comps;		      /* How many were already decoded*/
	unsigned alloc_bytes;                     /* Num bytes at extra_bytes */
	void *extra_bytes;                /* place string information in here */
	unsigned num_comps;                       /* Num comps at cur_comp    */
	struct pnfs_osd_object_cred *cur_comp;    /* decode into this comp    */
	void *p;           /*FIXME: Will be removed when converted to xdr API */
};

extern int pnfs_objects_decode_layout_map(struct pnfs_osd_layout *layout,
	struct pnfs_osd_xdr_decode_layout_iter *iter, u32 *xdr);

extern int pnfs_objects_decode_layout_comps(
	const struct pnfs_osd_layout *layout,
	struct pnfs_osd_xdr_decode_layout_iter *iter, u32 *xdr);

/* Layout encoding */
extern int pnfs_objects_encode_layout(
	struct exp_xdr_stream *xdr,
	struct pnfs_osd_layout *layout);

/* Device Info helpers */

/* First pass calculate total size for space needed */
extern size_t pnfs_osd_xdr_deviceaddr_incore_sz(u32 *p);

/* Note: some strings pointed to inside @deviceaddr might point
 * to space inside @p. @p should stay valid while @deviceaddr
 * is in use.
 * It is assumed that @deviceaddr points to bigger memory of size
 * calculated in first pass by pnfs_osd_xdr_deviceaddr_incore_sz()
 */
extern void pnfs_objects_decode_deviceaddr(
	struct pnfs_osd_deviceaddr *deviceaddr, u32 *p);

/* For Servers */
extern int pnfs_objects_encode_deviceaddr(
	struct exp_xdr_stream *xdr, struct pnfs_osd_deviceaddr *devaddr);

/* layoutupdate (layout_commit) xdr helpers */
extern int
pnfs_objects_encode_layoutupdate(struct xdr_stream *xdr,
				 struct pnfs_osd_layoutupdate *lou);
extern __be32 *
pnfs_objects_decode_layoutupdate(struct pnfs_osd_layoutupdate *lou, __be32 *p);

/* osd_ioerror encoding/decoding (layout_return) */
extern unsigned pnfs_objects_ioerr_xdr_sz(void);
extern int
pnfs_objects_encode_ioerr(struct xdr_stream *xdr, struct pnfs_osd_ioerr *ioerr);
extern __be32 *
pnfs_objects_decode_ioerr(struct pnfs_osd_ioerr *ioerr, __be32 *p);

#endif /* __PNFS_OSD_XDR_H__ */
