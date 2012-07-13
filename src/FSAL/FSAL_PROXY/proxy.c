/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
 *
 * Copyright CEA/DAM/DIF  (2008)
 *
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "pxy_fsal_methods.h"

static proxyfs_specific_initinfo_t default_pxy_params = {
        .retry_sleeptime = 10, /* Time to sleep when retrying */
        .srv_prognum = 100003, /* Default NFS prognum         */
        .srv_timeout = 60,     /* RPC Client timeout          */
        .srv_proto = "tcp",    /* Protocol to use */
        .srv_sendsize = 32768, /* Default Buffer Send Size    */
        .srv_recvsize = 32768, /* Default Buffer Send Size    */
        .keytab = "etc/krb5.keytab", /* Path to krb5 keytab file */
        .cred_lifetime = 86400,      /* 24h is a good default    */
#ifdef _HANDLE_MAPPING
        .hdlmap.databases_directory = "/var/ganesha/handlemap",
        .hdlmap.temp_directory = "/var/ganesha/tmp",
        .hdlmap.database_count = 8,
        .hdlmap.hashtable_size = 103,
#endif
};

/* defined the set of attributes supported with POSIX */
#define SUPPORTED_ATTRIBUTES (                                       \
          ATTR_SUPPATTR | ATTR_TYPE     | ATTR_SIZE      | \
          ATTR_FSID     |  ATTR_FILEID  | \
          ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
          ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
          ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
          ATTR_CHGTIME  )

/* filesystem info for VFS */
static struct fsal_staticfsinfo_t proxy_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL,
	.maxlink = _POSIX_LINK_MAX,
        .maxnamelen = 1024,
        .maxpathlen = 1024,
	.no_trunc = TRUE,
	.chown_restricted = TRUE,
	.case_preserving = TRUE,
	.fh_expire_type = FSAL_EXPTYPE_PERSISTENT,
	.link_support = TRUE,
	.symlink_support = TRUE,
	.lock_support = TRUE,
	.named_attr = TRUE,
	.unique_handles = TRUE,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW,
	.cansettime = TRUE,
	.homogenous = TRUE,
	.supported_attrs = SUPPORTED_ATTRIBUTES,
	.xattr_access_rights = 0400,
	.dirs_have_sticky_bit = TRUE
};

static int
pxy_key_to_param(const char *key, const char *val,
                 fsal_init_info_t *info, const char *name)
{
        struct pxy_fsal_module *pxy =
                container_of(info, struct pxy_fsal_module, init);
        proxyfs_specific_initinfo_t *init_info = &pxy->special;

        if(!strcasecmp(key, "Srv_Addr")) {
                if(isdigit(val[0])) {
                        init_info->srv_addr = inet_addr(val);
                } else {
                        struct hostent *hp;
                        if((hp = gethostbyname(val)) == NULL) {
                                LogCrit(COMPONENT_CONFIG,
                                        "Cannot resolve host name '%s'", val);
                                return 1;
                        }
                        memcpy(&init_info->srv_addr, hp->h_addr, hp->h_length);
                }
        } else if(!strcasecmp(key, "NFS_Port")) {
                init_info->srv_port = htons((unsigned short)atoi(val));
        } else if(!strcasecmp(key, "NFS_Service")) {
                init_info->srv_prognum = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "NFS_SendSize")) {
                init_info->srv_sendsize = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "NFS_RecvSize")) {
                init_info->srv_recvsize = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "Use_Privileged_Client_Port")) {
                init_info->use_privileged_client_port = StrToBoolean(val) ;
        } else if(!strcasecmp(key, "Retry_SleepTime")) {
                init_info->retry_sleeptime = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "NFS_Proto")) {
                /* value should be either "udp" or "tcp" */
                if(strncasecmp(val, "udp", MAXNAMLEN) &&
                   strncasecmp(val, "tcp", MAXNAMLEN)) {
                      LogCrit(COMPONENT_CONFIG, "Unexpected value '%s' for %s",
                              val, key);
                      return 1;
                }
                strncpy(init_info->srv_proto, val, MAXNAMLEN);
#ifdef _USE_GSSRPC
        } else if(!strcasecmp(key, "Active_krb5")) {
                init_info->active_krb5 = StrToBoolean(val);
        } else if(!strcasecmp(key, "Remote_PrincipalName")) {
                strncpy(init_info->remote_principal, val, MAXNAMLEN);
        } else if(!strcasecmp(key, "KeytabPath")) {
                strncpy(init_info->keytab, val, MAXPATHLEN);
        } else if(!strcasecmp(key, "Credential_LifeTime")) {
                init_info->cred_lifetime = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "Sec_Type")) {
                if(!strcasecmp(vale, "krb5"))
                        init_info->sec_type = RPCSEC_GSS_SVC_NONE;
                else if(!strcasecmp(val, "krb5i"))
                        init_info->sec_type = RPCSEC_GSS_SVC_INTEGRITY;
                else if(!strcasecmp(val, "krb5p"))
                        init_info->sec_type = RPCSEC_GSS_SVC_PRIVACY;
                else {
                        LogCrit(COMPONENT_CONFIG,
                                "Unexpected value '%s' for %s", val, key);
                        return 1;
                }
#endif
        } else if(!strcasecmp(key, "Enable_Handle_Mapping")) {
                init_info->enable_handle_mapping = StrToBoolean(val);

                if(init_info->enable_handle_mapping == -1) {
                        LogCrit(COMPONENT_CONFIG,
                                "Unexpected value '%s' for %s: expected boolean",
                                val, key);
                        return 1;
                }
#ifdef _HANDLE_MAPPING
        } else if(!strcasecmp(key, "HandleMap_DB_Dir")) {
                strncpy(init_info->hdlmap.databases_directory, val, MAXPATHLEN);
        } else if(!strcasecmp(key, "HandleMap_Tmp_Dir")) {
                strncpy(init_info->hdlmap.temp_directory, val, MAXPATHLEN);
        } else if(!strcasecmp(key, "HandleMap_DB_Count")) {
                init_info->hdlmap.database_count = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "HandleMap_HashTable_Size")) {
                init_info->hdlmap.hashtable_size = (unsigned int)atoi(val);
#endif
        } else {
                LogCrit(COMPONENT_CONFIG,
                        "Unknown key: %s in %s", key, name);
                return 1;
        }
        return 0;
}

static int
load_pxy_config(const char *name, config_file_t config,
                struct pxy_fsal_module *pxy)
{
        int err;
        int cnt, i;
        char *key_name;
        char *key_value;
        config_item_t block;
        int errcnt = 0;

        block = config_FindItemByName(config, name);
        if(block == NULL)
                return 0;

        LogWarn(COMPONENT_CONFIG,
                "Use of configuration block '%s' is depricated, "
                "consider switching to PROXY block inside FSAL",
                name);
                      
        if(config_ItemType(block) != CONFIG_ITEM_BLOCK) {
                LogCrit(COMPONENT_CONFIG,
                        "\"%s\" is expected to be a block", name); 
                return 1;
        }

        cnt = config_GetNbItems(block);

        for(i = 0; i < cnt; i++) {
                config_item_t item;

                item = config_GetItemByIndex(block, i);

                err = config_GetKeyValue(item, &key_name, &key_value);
                if(err) {
                        LogCrit(COMPONENT_CONFIG,
                                "Cannot read key[%d] from section \"%s\" of configuration file.",
                                 i, name);
                        errcnt++;
                }

                errcnt += pxy_key_to_param(key_name, key_value,
                                           &pxy->init, name);
        }

        return errcnt;
}

static fsal_status_t
pxy_init_config(struct fsal_module *fsal_hdl,
                config_file_t config_struct)
{
        fsal_status_t st;
        int rc;
        struct pxy_fsal_module *pxy =
                container_of(fsal_hdl, struct pxy_fsal_module, module);

        default_pxy_params.srv_addr = INADDR_LOOPBACK;
        default_pxy_params.srv_port = htons(2049);

        pxy->special = default_pxy_params;
        pxy->fsinfo = proxy_info;

        st = fsal_load_config("PROXY", config_struct, &pxy->init,
                              &pxy->fsinfo, pxy_key_to_param);
        if (FSAL_IS_ERROR(st))
                return st;

        if (load_pxy_config("NFSv4_Proxy", config_struct, pxy))
                return fsalstat(ERR_FSAL_INVAL, EINVAL);

#ifdef _HANDLE_MAPPING
        if((rc = HandleMap_Init(&pxy->special.hdlmap)) < 0)
                return fsalstat(ERR_FSAL_INVAL, -rc);
#endif

        rc = pxy_init_rpc(pxy);
        if(rc)
                return fsalstat(ERR_FSAL_FAULT, rc);
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static struct pxy_fsal_module PROXY;

void pxy_export_ops_init(struct export_ops *ops);
void pxy_handle_ops_init(struct fsal_obj_ops *ops);


MODULE_INIT void 
pxy_init(void)
{
	if(register_fsal(&PROXY.module, "PROXY",
			 FSAL_MAJOR_VERSION,
			 FSAL_MINOR_VERSION) != 0)
		return;
	PROXY.module.ops->init_config = pxy_init_config;
	PROXY.module.ops->create_export = pxy_create_export;
	pxy_export_ops_init(PROXY.module.exp_ops);
	pxy_handle_ops_init(PROXY.module.obj_ops);
}

MODULE_FINI void 
pxy_unload(void)
{
	unregister_fsal(&PROXY.module);
}
