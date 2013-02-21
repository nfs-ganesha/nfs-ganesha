/*
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 */

/**
 * @defgroup idmapper ID Mapper
 *
 * The ID Mapper module provides mapping between numerical user and
 * group IDs and NFSv4 style owner and group strings.
 *
 * @{
 */


/**
 * @file idmapper.c
 * @brief Id mapping functions
 */

#ifndef IDMAPPER_H
#define IDMAPPER_H
#include <stdbool.h>
#include <stdint.h>

extern hash_table_t *ht_pwnam;
extern hash_table_t *ht_grnam;
extern hash_table_t *ht_pwuid;
extern hash_table_t *ht_grgid;
extern hash_table_t *ht_uidgid;

/**
 * @brief Used to select the cache to populate or on which to get
 * stats
 */

typedef enum {
	UIDMAP_TYPE = 1,
	GIDMAP_TYPE = 2
} idmap_type_t;

/**
 * @brief Overload mapping of uid/gid to buffdata values
 *
 * To save allocating space, uids and gids are overlayed into the value pointer
 * (.addr) of the hashbuffer_t.  This union accomplishes that mapping.
 * When used, the length (.len) is expected to be zero: This is not a pointer.
 */

union idmap_val {
	void *id_as_pointer;
	uint32_t real_id;
};


bool idmapper_init(void);
uint64_t namemapper_rbt_hash_func(hash_parameter_t *,
				  struct gsh_buffdesc *);

uint32_t namemapper_value_hash_func(hash_parameter_t *,
				    struct gsh_buffdesc *);
int idmap_populate(char *path, idmap_type_t);

int idmap_gid_init(nfs_idmap_cache_parameter_t);
int idmap_gname_init(nfs_idmap_cache_parameter_t);

int idmap_uid_init(nfs_idmap_cache_parameter_t);
int idmap_uname_init(nfs_idmap_cache_parameter_t);
int uidgidmap_init(nfs_idmap_cache_parameter_t);

int idmapper_hash_func(hash_parameter_t *hparam,
		       struct gsh_buffdesc *key,
		       uint32_t *index,
		       uint64_t *rbthash);
int display_idmapper_val(struct gsh_buffdesc *, char *);
int display_idmapper_key(struct gsh_buffdesc *, char *);

int compare_idmapper(struct gsh_buffdesc *, struct gsh_buffdesc *);
int compare_namemapper(struct gsh_buffdesc *, struct gsh_buffdesc *);

int idmap_add(hash_table_t *, const struct gsh_buffdesc*, uint32_t);
int uidmap_add(const struct gsh_buffdesc *, uid_t);
int gidmap_add(const struct gsh_buffdesc *, gid_t);

int namemap_add(hash_table_t *, uint32_t, const struct gsh_buffdesc *);
int unamemap_add(uid_t, const struct gsh_buffdesc *);
int gnamemap_add(gid_t, const struct gsh_buffdesc *);
int uidgidmap_add(uid_t, gid_t);

int idmap_get(hash_table_t *, const struct gsh_buffdesc *, uint32_t *);
int uidmap_get(const struct gsh_buffdesc *, uid_t *);
int gidmap_get(const struct gsh_buffdesc *, gid_t *);

int uidgidmap_get(uid_t, gid_t *);

int idmap_remove(hash_table_t *, const struct gsh_buffdesc *);
int uidmap_remove(const struct gsh_buffdesc *);
int gidmap_remove(const struct gsh_buffdesc *);

int namemap_remove(hash_table_t *, uint32_t);
int unamemap_remove(uid_t);
int gnamemap_remove(gid_t);
int uidgidmap_remove(uid_t);

int uidgidmap_clear(void);
int idmap_clear(void);
int namemap_clear(void);

void idmap_get_stats(idmap_type_t,
		     hash_stat_t *,
		     hash_stat_t *);
int nfs_read_uidmap_conf(config_file_t,
			 nfs_idmap_cache_parameter_t *);
int nfs_read_gidmap_conf(config_file_t,
			 nfs_idmap_cache_parameter_t *);

bool xdr_encode_nfs4_owner(XDR *, uid_t);
bool xdr_encode_nfs4_group(XDR *, gid_t);

bool name2uid(const struct gsh_buffdesc *, uid_t *, const uid_t);
bool name2gid(const struct gsh_buffdesc *, gid_t *, const gid_t);


#ifdef _HAVE_GSSAPI
#ifdef _MSPAC_SUPPORT
bool principal2uid(char *, uid_t *, struct svc_rpc_gss_data *);
#else
bool principal2uid(char *, uid_t *);
#endif
#endif

#endif /* IDMAPPER_H */
/** @} */
