/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2014 Panasas Inc.
 * Author: Daniel Gryniewicz dang@cohortfs.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* panfs_int.h
 * Internal PanFS structs/defines
 */

#ifndef __PANFS_INT_H
#define __PANFS_INT_H

typedef int pan_bool_t;
#define PAN_FALSE ((pan_bool_t)0)
#define PAN_TRUE ((pan_bool_t) 1)

typedef uint64_t pan_stor_dev_id_t;
typedef uint32_t pan_stor_obj_grp_id_t;
typedef uint64_t pan_stor_obj_uniq_t;

/* Object ID */
struct pan_stor_obj_id_s {
	pan_stor_dev_id_t	dev_id;
	pan_stor_obj_uniq_t	obj_id;
	pan_stor_obj_grp_id_t	grp_id;
};
typedef struct pan_stor_obj_id_s pan_stor_obj_id_t;

/* Object map hint */
typedef uint64_t pan_sm_obj_map_hint_comp_t;
typedef pan_sm_obj_map_hint_comp_t pan_sm_obj_map_hint_comp_a[2];
struct pan_sm_obj_map_hint_s {
	pan_sm_obj_map_hint_comp_a	comp;
};
typedef struct pan_sm_obj_map_hint_s pan_sm_obj_map_hint_t;

/* Time */
typedef struct pan_timespec_s pan_timespec_t;
struct pan_timespec_s {
	uint32_t	ts_sec;
	uint32_t	ts_nsec;
};

/* User */
typedef uint32_t pan_identity_type_t;

#define PAN_IDENTITY_UNKNOWN ((pan_identity_type_t) 0UL)
#define PAN_IDENTITY_UNIX_USER ((pan_identity_type_t) 1UL)
#define PAN_IDENTITY_WIN_USER ((pan_identity_type_t) 2UL)
#define PAN_IDENTITY_PAN_USER ((pan_identity_type_t) 3UL)
#define PAN_IDENTITY_UNIX_GROUP ((pan_identity_type_t) 4UL)
#define PAN_IDENTITY_WIN_GROUP ((pan_identity_type_t) 5UL)
#define PAN_IDENTITY_PAN_GROUP ((pan_identity_type_t) 6UL)
#define PAN_IDENTITY_MGR ((pan_identity_type_t) 7UL)
#define PAN_IDENTITY_BLADE ((pan_identity_type_t) 8UL)
#define PAN_IDENTITY_MAX_VALID ((pan_identity_type_t) 8UL)
#define PAN_IDENTITY_SAVED_UNKNOWN (PAN_IDENTITY_MAX_VALID + 1)
#define PAN_SID_SUB_AUTH_MAX ((uint32_t) 7UL)
#define PAN_SID_HEADER_LEN ((uint32_t) 8UL)

typedef uint8_t pan_id_auth_t[6];
typedef uint32_t pan_sub_auths_t[7];
typedef uint8_t pan_brick_serial_t[32];

struct pan_sid_s {
	uint8_t		sid_rev_num;
	uint8_t		num_auths;
	pan_id_auth_t	id_auth;
	pan_sub_auths_t	sub_auths;
};
typedef struct pan_sid_s pan_sid_t;

#define PAN_IDENTITY_UNKNOWN_NULL ((uint32_t) 0UL)
#define PAN_IDENTITY_UNKNOWN_USER ((uint32_t) 1UL)
#define PAN_IDENTITY_UNKNOWN_GROUP ((uint32_t) 2UL)
#define PAN_IDENTITY_UNKNOWN_VOLUME ((uint32_t) 3UL)
#define PAN_IDENTITY_EVERYONE_GROUP_ID ((uint32_t) 1UL)
#define PAN_IDENTITY_TEMP_PRIMARY_GROUP_ID ((uint32_t) 2UL)
#define PAN_IDENTITY_CIFS_ADMIN_GROUP_ID ((uint32_t) 3UL)
#define PAN_IDENTITY_TEMP_OWNER_ID ((uint32_t) 1UL)
struct pan_identity_s {
	pan_identity_type_t type;
	union {
		uint32_t		unknown;
		uint32_t		uid;
		uint32_t		gid;
		pan_sid_t		user_sid;
		pan_sid_t		group_sid;
		uint32_t		pan_uid;
		uint32_t		pan_gid;
		uint64_t		mgr_id;
		pan_brick_serial_t	blade_serial;
	} u;
};
typedef struct pan_identity_s pan_identity_t;

/* Local ACL representation for ls -al */
struct pan_fs_client_llapi_access_acl_local_s {
	uid_t	owner;
	gid_t	group;
	mode_t	mode;
};
struct pan_fs_client_llapi_access_s {
	struct pan_fs_client_llapi_access_acl_local_s	local_acl;
};
typedef struct pan_fs_client_llapi_access_s pan_fs_client_llapi_access_t;

/* File layout */
struct pan_agg_simple_header_s {
	uint8_t	unused;
};
typedef struct pan_agg_simple_header_s pan_agg_simple_header_t;
struct pan_agg_raid1_header_s {
	uint16_t	num_comps;
};
typedef struct pan_agg_raid1_header_s pan_agg_raid1_header_t;
struct pan_agg_raid0_header_s {
	uint16_t	num_comps;
	uint32_t	stripe_unit;
};
typedef struct pan_agg_raid0_header_s pan_agg_raid0_header_t;
struct pan_agg_raid5_left_header_s {
	uint16_t	num_comps;
	uint32_t	stripe_unit0;
	uint32_t	stripe_unit1;
	uint32_t	stripe_unit2;
};
typedef struct pan_agg_raid5_left_header_s pan_agg_raid5_left_header_t;
struct pan_agg_policy_raid5_left_header_s {
	uint8_t	stripe_width_policy;
	uint8_t	stripe_unit_policy;
};
typedef struct pan_agg_policy_raid5_left_header_s
	pan_agg_policy_raid5_left_header_t;
struct pan_agg_grp_raid5_left_header_s {
	uint16_t	num_comps;
	uint32_t	stripe_unit;
	uint16_t	rg_width;
	uint16_t	rg_depth;
	uint8_t		group_layout_policy;
};
typedef struct pan_agg_grp_raid5_left_header_s pan_agg_grp_raid5_left_header_t;
struct pan_agg_grp_raidn_left_header_s {
	uint16_t	num_comps;
	uint32_t	stripe_unit;
	uint16_t	rg_width;
	uint16_t	rg_depth;
	uint8_t		max_faults;
	uint8_t		encoding;
};
typedef struct pan_agg_grp_raidn_left_header_s pan_agg_grp_raidn_left_header_t;
#define PAN_AGG_NULL_MAP ((uint8_t) 0x00)
#define PAN_AGG_SIMPLE ((uint8_t) 0x01)
#define PAN_AGG_RAID1 ((uint8_t) 0x02)
#define PAN_AGG_RAID0 ((uint8_t) 0x03)
#define PAN_AGG_RAID5_LEFT ((uint8_t) 0x04)
#define PAN_AGG_POLICY_RAID5_LEFT ((uint8_t) 0x05)
#define PAN_AGG_GRP_RAID5_LEFT ((uint8_t) 0x06)
#define PAN_AGG_GRP_RAIDN_LEFT ((uint8_t) 0x07)
#define PAN_AGG_MINTYPE ((uint8_t) 0x01)
#define PAN_AGG_MAXTYPE ((uint8_t) 0x07)

struct pan_agg_layout_hdr_s {
	uint8_t	type;
	uint8_t	pad[3];
	union {
		uint64_t				null;
		pan_agg_simple_header_t			simple;
		pan_agg_raid1_header_t			raid1;
		pan_agg_raid0_header_t			raid0;
		pan_agg_raid5_left_header_t		raid5_left;
		pan_agg_policy_raid5_left_header_t	policy_raid5_left;
		pan_agg_grp_raid5_left_header_t		grp_raid5_left;
		pan_agg_grp_raidn_left_header_t		grp_raidn_left;
	} hdr;
};
typedef struct pan_agg_layout_hdr_s pan_agg_layout_hdr_t;

/* ACE */
struct pan_fs_ace_s {
	pan_identity_t	identity;
	uint32_t	permissions;
	uint16_t	info;
};
typedef struct pan_fs_ace_s pan_fs_ace_t;

/* ACL defines */
#define PAN_FS_ACL_VERSION_MIN ((uint32_t) 1UL)
#define PAN_FS_ACL_VERSION_MAX ((uint32_t) 2UL)
#define PAN_FS_ACL_VERSION ((uint32_t) 2UL)
#define PAN_FS_ACL_LEN_MAX ((uint16_t) 128U)

/* Object flags */
#define PAN_FS_OBJ_F_NONE ((uint64_t)0)
#define PAN_FS_OBJ_F_FS_ARCHIVE ((uint64_t)1)
#define PAN_FS_OBJ_F_FS_HIDDEN ((uint64_t)2)
#define PAN_FS_OBJ_F_FS_SYSTEM ((uint64_t)4)
#define PAN_FS_OBJ_F_FS_DO_NOT_CACHE ((uint64_t)8)
#define PAN_FS_OBJ_F_FS_SETUID ((uint64_t)16)
#define PAN_FS_OBJ_F_FS_SETGID ((uint64_t)32)
#define PAN_FS_OBJ_F_FS_STICKY ((uint64_t)64)
#define PAN_FS_OBJ_F_FS_READONLY ((uint64_t)128)
#define PAN_FS_OBJ_F_FS_CW_OPEN ((uint64_t)256)
#define PAN_FS_OBJ_F_FS_TIER0 ((uint64_t)512)
#define PAN_FS_OBJ_F_FM_DIR_REALM_ROOT ((uint64_t)4294967296)
#define PAN_FS_OBJ_F_FM_DIR_VOLUME_ROOT ((uint64_t)8589934592)
#define PAN_FS_OBJ_F_FM_DIR_DO_NOT_HASH ((uint64_t)17179869184)
#define PAN_FS_OBJ_F_FM_DIR_83_NAMES ((uint64_t)34359738368)
#define PAN_FS_OBJ_F_FM_ACL_V2 ((uint64_t)68719476736)
#define PAN_FS_OBJ_F_FM_RESERVED ((uint64_t)18446744069414584320)

#endif /* __PANFS_INT_H */
