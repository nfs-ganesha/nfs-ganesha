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
 *
 * ------------- 
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
        .srv_timeout = 2,      /* RPC Client timeout          */
        .srv_proto = "tcp",    /* Protocol to use */
        .srv_sendsize = 32768, /* Default Buffer Send Size    */
        .srv_recvsize = 32768, /* Default Buffer Send Size    */
        .keytab = "etc/krb5.keytab", /* Path to krb5 keytab file */
        .cred_lifetime = 86400,      /* 24h is a good default    */
#ifdef _HANDLE_MAPPING
        .hdlmap_dbdir = "/var/ganesha/handlemap",
        .hdlmap_tmpdir = "/var/ganesha/tmp",
        .hdlmap_dbcount = 8,
        .hdlmap_hashsize = 103j
        .hdlmap_nb_entry_prealloc = 16384,
        .hdlmap_nb_db_op_prealloc = 1024,
#endif
};

/* defined the set of attributes supported with POSIX */
#define SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     |  FSAL_ATTR_FILEID  | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_RAWDEV    | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME  )

/* filesystem info for VFS */
static fsal_staticfsinfo_t proxy_info = {
	.maxfilesize = 0xFFFFFFFFFFFFFFFFLL,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = FSAL_MAX_NAME_LEN,
	.maxpathlen = FSAL_MAX_PATH_LEN,
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
                strncpy(init_info->hdlmap_dbdir, val, MAXPATHLEN);
        } else if(!strcasecmp(key, "HandleMap_Tmp_Dir")) {
                strncpy(init_info->hdlmap_tmpdir, val, MAXPATHLEN);
        } else if(!strcasecmp(key, "HandleMap_DB_Count")) {
                init_info->hdlmap_dbcount = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "HandleMap_HashTable_Size")) {
                init_info->hdlmap_hashsize = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "HandleMap_Nb_Entries_Prealloc")) {
                init_info->hdlmap_nb_entry_prealloc = (unsigned int)atoi(val);
        } else if(!strcasecmp(key, "HandleMap_Nb_DB_Operations_Prealloc")) {
                init_info->hdlmap_nb_db_op_prealloc = (unsigned int)atoi(val);
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
        struct pxy_fsal_module *pxy =
                container_of(fsal_hdl, struct pxy_fsal_module, module);

        default_pxy_params.srv_addr = htonl(0x7F000001);
        default_pxy_params.srv_port = htons(2049);

        pxy->special = default_pxy_params;
        pxy->fsinfo = proxy_info;

        st = fsal_load_config("PROXY", config_struct, &pxy->init,
                              &pxy->fsinfo, pxy_key_to_param);
        if (FSAL_IS_ERROR(st))
                return st;

        if (load_pxy_config("NFSv4_Proxy", config_struct, pxy))
                ReturnCode(ERR_FSAL_INVAL, EINVAL);
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static void pxy_dump_config(struct fsal_module *fsal_hdl, int log_fd)
{
}

static struct pxy_fsal_module PROXY = {
	.pxy_ops.init_config = pxy_init_config,
	.pxy_ops.dump_config = pxy_dump_config,
	.pxy_ops.create_export = pxy_create_export,
};

MODULE_INIT void 
pxy_init(void)
{
        PROXY.module.ops = &PROXY.pxy_ops;

	register_fsal(&PROXY.module, "PROXY");
}

MODULE_FINI void 
pxy_unload(void)
{
	unregister_fsal(&PROXY.module);
}
