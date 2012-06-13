/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
 * Copyright CEA/DAM/DIF  (2008)
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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

/* Proxy handle methods */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include <assert.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "nlm_list.h"
#include "FSAL/fsal_commonlib.h"
#include "pxy_fsal_methods.h"
#include "fsal_nfsv4_macros.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

#define FSAL_PROXY_NFS_V4 4

/* Use this to estimate storage requirements for fattr4 blob */
struct pxy_fattr_storage {
        fattr4_type type;
        fattr4_change change_time;
        fattr4_size size;
        fattr4_fsid fsid;
        fattr4_filehandle filehandle;
        fattr4_fileid fileid;
        fattr4_mode mode;
        fattr4_numlinks numlinks;
        fattr4_owner owner;
        fattr4_owner_group owner_group;
        fattr4_space_used space_used;
        fattr4_time_access time_access;
        fattr4_time_metadata time_metadata;
        fattr4_time_modify time_modify;
        fattr4_rawdev rawdev;
        char padowner[MAXNAMLEN];
        char padgroup[MAXNAMLEN];
        char padfh[NFS4_FHSIZE];
};

#define FATTR_BLOB_SZ sizeof(struct pxy_fattr_storage)

struct pxy_obj_handle {
        struct fsal_obj_handle obj;
        nfs_fh4 fh4;
};

static struct pxy_obj_handle *
pxy_alloc_handle(struct fsal_export *exp, const nfs_fh4 *fh,
                 const fsal_attrib_list_t *attr);

static fsal_status_t nfsstat4_to_fsal(nfsstat4 nfsstatus)
{
        switch (nfsstatus) {
        case NFS4ERR_SAME:
        case NFS4ERR_NOT_SAME:
        case NFS4_OK:
              ReturnCode(ERR_FSAL_NO_ERROR, (int)nfsstatus); 
        case NFS4ERR_PERM:
              ReturnCode(ERR_FSAL_PERM, (int)nfsstatus);
        case NFS4ERR_NOENT:
                ReturnCode(ERR_FSAL_NOENT, (int)nfsstatus);
        case NFS4ERR_IO:
                ReturnCode(ERR_FSAL_IO, (int)nfsstatus);
        case NFS4ERR_NXIO:
                ReturnCode(ERR_FSAL_NXIO, (int)nfsstatus);
        case NFS4ERR_EXPIRED:
        case NFS4ERR_LOCKED:
        case NFS4ERR_SHARE_DENIED:
        case NFS4ERR_LOCK_RANGE:
        case NFS4ERR_OPENMODE:
        case NFS4ERR_FILE_OPEN:
        case NFS4ERR_ACCESS:
        case NFS4ERR_DENIED:
                ReturnCode(ERR_FSAL_ACCESS, (int)nfsstatus);
        case NFS4ERR_EXIST:
                ReturnCode(ERR_FSAL_EXIST, (int)nfsstatus);
        case NFS4ERR_XDEV:
                ReturnCode(ERR_FSAL_XDEV, (int)nfsstatus);
        case NFS4ERR_NOTDIR:
                ReturnCode(ERR_FSAL_NOTDIR, (int)nfsstatus);
        case NFS4ERR_ISDIR:
                ReturnCode(ERR_FSAL_ISDIR, (int)nfsstatus);
        case NFS4ERR_FBIG:
                ReturnCode(ERR_FSAL_FBIG, 0);
        case NFS4ERR_NOSPC:
                ReturnCode(ERR_FSAL_NOSPC, (int)nfsstatus);
        case NFS4ERR_ROFS:
                ReturnCode(ERR_FSAL_ROFS, (int)nfsstatus);
        case NFS4ERR_MLINK:
                ReturnCode(ERR_FSAL_MLINK, (int)nfsstatus);
        case NFS4ERR_NAMETOOLONG:
                ReturnCode(ERR_FSAL_NAMETOOLONG, (int)nfsstatus);
        case NFS4ERR_NOTEMPTY:
                ReturnCode(ERR_FSAL_NOTEMPTY, (int)nfsstatus);
        case NFS4ERR_DQUOT:
                ReturnCode(ERR_FSAL_DQUOT, (int)nfsstatus);
        case NFS4ERR_STALE:
                ReturnCode(ERR_FSAL_STALE, (int)nfsstatus);
        case NFS4ERR_NOFILEHANDLE:
        case NFS4ERR_BADHANDLE:
                ReturnCode(ERR_FSAL_BADHANDLE, (int)nfsstatus);
        case NFS4ERR_BAD_COOKIE:
                ReturnCode(ERR_FSAL_BADCOOKIE, (int)nfsstatus);
        case NFS4ERR_NOTSUPP:
                ReturnCode(ERR_FSAL_NOTSUPP, (int)nfsstatus);
        case NFS4ERR_TOOSMALL:
                 ReturnCode(ERR_FSAL_TOOSMALL, (int)nfsstatus);
        case NFS4ERR_SERVERFAULT:
                 ReturnCode(ERR_FSAL_SERVERFAULT, (int)nfsstatus);
        case NFS4ERR_BADTYPE:
                 ReturnCode(ERR_FSAL_BADTYPE, (int)nfsstatus);
        case NFS4ERR_GRACE:
        case NFS4ERR_DELAY:
                ReturnCode(ERR_FSAL_DELAY, (int)nfsstatus);
        case NFS4ERR_FHEXPIRED:
                ReturnCode(ERR_FSAL_FHEXPIRED, (int)nfsstatus);
        case NFS4ERR_WRONGSEC:
                ReturnCode(ERR_FSAL_SEC, (int)nfsstatus);
        case NFS4ERR_SYMLINK:
                ReturnCode(ERR_FSAL_SYMLINK, (int)nfsstatus);
        case NFS4ERR_ATTRNOTSUPP:
                ReturnCode(ERR_FSAL_ATTRNOTSUPP, (int)nfsstatus);
        case NFS4ERR_INVAL:
        case NFS4ERR_CLID_INUSE:
        case NFS4ERR_MOVED:
        case NFS4ERR_RESOURCE:
        case NFS4ERR_MINOR_VERS_MISMATCH:
        case NFS4ERR_STALE_CLIENTID:
        case NFS4ERR_STALE_STATEID:
        case NFS4ERR_OLD_STATEID:
        case NFS4ERR_BAD_STATEID:
        case NFS4ERR_BAD_SEQID:
        case NFS4ERR_RESTOREFH:
        case NFS4ERR_LEASE_MOVED:
        case NFS4ERR_NO_GRACE:
        case NFS4ERR_RECLAIM_BAD:
        case NFS4ERR_RECLAIM_CONFLICT:
        case NFS4ERR_BADXDR:
        case NFS4ERR_BADCHAR:
        case NFS4ERR_BADNAME:
        case NFS4ERR_BAD_RANGE:
        case NFS4ERR_BADOWNER:
        case NFS4ERR_OP_ILLEGAL:
        case NFS4ERR_LOCKS_HELD:
        case NFS4ERR_LOCK_NOTSUPP:
        case NFS4ERR_DEADLOCK:
        case NFS4ERR_ADMIN_REVOKED:
        case NFS4ERR_CB_PATH_DOWN:
        default:
                ReturnCode(ERR_FSAL_INVAL, (int)nfsstatus);
        }
}

static void pxy_create_getattr_bitmap(uint32_t *pbitmap)
{
        bitmap4 bm = {.bitmap4_val = pbitmap, .bitmap4_len = 2};
        uint32_t tmpattrlist[] = {
                FATTR4_TYPE,
                FATTR4_CHANGE,
                FATTR4_SIZE,
                FATTR4_FSID,
                FATTR4_FILEID,
                FATTR4_MODE,
                FATTR4_NUMLINKS,
                FATTR4_OWNER,
                FATTR4_OWNER_GROUP,
                FATTR4_SPACE_USED,
                FATTR4_TIME_ACCESS,
                FATTR4_TIME_METADATA,
                FATTR4_TIME_MODIFY,
                FATTR4_RAWDEV
        };
        uint32_t attrlen = ARRAY_SIZE(tmpattrlist);

        memset(pbitmap, 0, sizeof(uint32_t) * bm.bitmap4_len);

        nfs4_list_to_bitmap4(&bm, &attrlen, tmpattrlist);
}

/* Until readdir callback can take more information do not ask for more then
 * just type */
static void pxy_create_readdir_bitmap(uint32_t *pbitmap)
{
        bitmap4 bm = {.bitmap4_val = pbitmap, .bitmap4_len = 2};
        uint32_t tmpattrlist[] = {
                FATTR4_TYPE,
        };
        uint32_t attrlen = ARRAY_SIZE(tmpattrlist);

        memset(pbitmap, 0, sizeof(uint32_t) * bm.bitmap4_len);

        nfs4_list_to_bitmap4(&bm, &attrlen, tmpattrlist);
}

static struct
{
        fsal_attrib_mask_t mask;
        int fattr_bit;
} fsal_mask2bit[] =
{
        {FSAL_ATTR_SIZE, FATTR4_SIZE},
        {FSAL_ATTR_MODE, FATTR4_MODE},
        {FSAL_ATTR_OWNER, FATTR4_OWNER},
        {FSAL_ATTR_GROUP, FATTR4_OWNER_GROUP},
        {FSAL_ATTR_ATIME, FATTR4_TIME_ACCESS_SET},
        {FSAL_ATTR_MTIME, FATTR4_TIME_MODIFY_SET},
        {FSAL_ATTR_CTIME, FATTR4_TIME_METADATA}
};

/*
 * Create bitmap which list attributes which are both specified by
 * the attrs and considered as 'settable'.
 */
static void
pxy_create_settable_bitmap(const fsal_attrib_list_t * attrs, bitmap4 *bm)
{
        uint32_t tmpattrlist[ARRAY_SIZE(fsal_mask2bit)];
        uint32_t attrlen = 0;
        int i;

        for(i=0; i < ARRAY_SIZE(fsal_mask2bit); i++) {
                if(FSAL_TEST_MASK(attrs->asked_attributes, fsal_mask2bit[i].mask))
                        tmpattrlist[attrlen++] = fsal_mask2bit[i].fattr_bit;
        }
        nfs4_list_to_bitmap4(bm, &attrlen, tmpattrlist);
}                               /* fsal_interval_proxy_fsalattr2bitmap4 */

static CLIENT *rpc_client;

static int
pxy_create_rpc_clnt(const proxyfs_specific_initinfo_t *ctx)
{
        int sock;
        struct sockaddr_in addr_rpc;
        struct timeval timeout = TIMEOUTRPC;
        int rc;
        int priv_port = 0 ; 
        char addr[INET_ADDRSTRLEN];

        memset(&addr_rpc, 0, sizeof(addr_rpc));
        addr_rpc.sin_port = ctx->srv_port;
        addr_rpc.sin_family = AF_INET;
        addr_rpc.sin_addr.s_addr = ctx->srv_addr;

        if(!strcmp(ctx->srv_proto, "udp")) {
                if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
                        return 0;

                rpc_client = clntudp_bufcreate(&addr_rpc, ctx->srv_prognum,
                                               FSAL_PROXY_NFS_V4,
                                               (struct timeval){ 25, 0},
                                               &sock,
                                               ctx->srv_sendsize,
                                               ctx->srv_recvsize);

        } else if(!strcmp(ctx->srv_proto, "tcp")) {
                if(ctx->use_privileged_client_port) {
                        if((sock = rresvport(&priv_port)) < 0) {
                                LogCrit(COMPONENT_FSAL, "Cannot create a tcp socket on a privileged port");
                                return 0;
                        }
                } else {
                        if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
                                LogCrit(COMPONENT_FSAL, "Cannot create a tcp socket - %d", errno);
                                return 0;
                        }
                }

                if(connect(sock, (struct sockaddr *)&addr_rpc,
                           sizeof(addr_rpc)) < 0) {
                        LogCrit(COMPONENT_FSAL,
                                "Cannot connect to server addr=%s port=%u",
                                inet_ntop(AF_INET, &ctx->srv_addr, addr, sizeof(addr)),
                                ntohs(ctx->srv_port));

                        return 0;
                }

                rpc_client = clnttcp_create(&addr_rpc, ctx->srv_prognum,
                                            FSAL_PROXY_NFS_V4,
                                            &sock,
                                            ctx->srv_sendsize,
                                            ctx->srv_recvsize);
        } else {
                return 0;
        }

        if(rpc_client == NULL) {
                LogCrit(COMPONENT_FSAL,
                        "Cannot contact program %u on %s:%u via %s",
                         ctx->srv_prognum,
                         inet_ntop(AF_INET, &ctx->srv_addr, addr, sizeof(addr)),
                         ntohs(ctx->srv_port), ctx->srv_proto);

                return 0;
        }

        if((rpc_client->cl_auth = authunix_create_default()) == NULL)
                return 0;

        rc = clnt_call(rpc_client, NFSPROC4_NULL,
                       (xdrproc_t) xdr_void, (caddr_t) NULL,
                       (xdrproc_t) xdr_void, (caddr_t) NULL, timeout);
        return (rc == RPC_SUCCESS);
        /* TODO - set client id */
}

static pthread_mutex_t rpc_call_lock = PTHREAD_MUTEX_INITIALIZER;

static int
pxy_nfsv4_call(struct fsal_export *exp, uint32_t cnt, nfs_argop4 *argoparray, nfs_resop4 *resoparray)
{
        struct pxy_export *pxyexp =
                container_of(exp, struct pxy_export, exp);
        int renewed = 0;
        int rc;
        struct timeval timeout = TIMEOUTRPC;
        COMPOUND4args arg = {
                .argarray.argarray_val = argoparray,
                .argarray.argarray_len = cnt
        };
        COMPOUND4res res = {
                .resarray.resarray_val = resoparray,
                .resarray.resarray_len = cnt
        };

        pthread_mutex_lock(&rpc_call_lock);
        if(rpc_client == NULL)
                pxy_create_rpc_clnt(pxyexp->info);
        do {
#if 0
                if(FSAL_proxy_change_user(pcontext) == NULL)
                        return -1;
#endif

                rc = clnt_call(rpc_client, NFSPROC4_COMPOUND, 
                               (xdrproc_t)xdr_COMPOUND4args, (caddr_t)&arg, 
                               (xdrproc_t)xdr_COMPOUND4res,  (caddr_t)&res,
                               timeout);
                if(rc == RPC_SUCCESS ) {
                        rc = res.status;
                        break;
                }

                LogEvent(COMPONENT_FSAL, "Call failed, reconnecting to the remote server");
                do {
                        renewed = pxy_create_rpc_clnt(pxyexp->info);
                        if (renewed) {
                                LogEvent(COMPONENT_FSAL,
                                         "Cannot reconnect, will sleep for %d seconds",
                                         pxyexp->info->retry_sleeptime);
                                sleep(pxyexp->info->retry_sleeptime);
                        }
                } while(!renewed);
        } while(1);

        pthread_mutex_unlock(&rpc_call_lock);
        return rc;
}

/*
 * NULL parent pointer is only used by lookup_path when it starts 
 * from the root handle and has its own export pointer, everybody
 * else is supposed to provide a real handle * pointer and matching
 * export
 */
static fsal_status_t
pxy_lookup_impl(struct fsal_obj_handle *parent,
                struct fsal_export *export,
	        const char *path,
	        struct fsal_obj_handle **handle)
{
        int rc;
        uint32_t opcnt = 0;
        uint32_t bitmap_val[2];
        fsal_attrib_list_t attributes;
        GETATTR4resok *atok;
        GETFH4resok *fhok;
#define FSAL_LOOKUP_NB_OP_ALLOC 4
        nfs_argop4 argoparray[FSAL_LOOKUP_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_LOOKUP_NB_OP_ALLOC];
        uint32_t bitmap_res[2];
        char fattr_blob[FATTR_BLOB_SZ];
        char padfilehandle[NFS4_FHSIZE];
        struct pxy_obj_handle *pxy_hdl;

        if(!handle)
                ReturnCode(ERR_FSAL_INVAL, 0);

        if(!parent) {
                COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, argoparray);
        } else {
                struct pxy_obj_handle *pxy_obj = 
                        container_of(parent, struct pxy_obj_handle, obj);
                switch (parent->type) {
                case DIRECTORY:
                        break;

                case FS_JUNCTION:
                        ReturnCode(ERR_FSAL_XDEV, 0);

                default:
                        ReturnCode(ERR_FSAL_NOTDIR, 0);
                }
                
                COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, pxy_obj->fh4);
        }

        if(path) {
                if(!strcmp(path, ".")) {
                        if(!parent)
                                ReturnCode(ERR_FSAL_FAULT, 0);
                } else if(!strcmp(path, "..")) {
                        if(!parent)
                                ReturnCode(ERR_FSAL_FAULT, 0);
                        COMPOUNDV4_ARG_ADD_OP_LOOKUPP(opcnt, argoparray);
                } else {
                        COMPOUNDV4_ARG_ADD_OP_LOOKUP(opcnt, argoparray, path);
                }
        }

        pxy_create_getattr_bitmap(bitmap_val);

        fhok = &resoparray[opcnt].nfs_resop4_u.opgetfh.GETFH4res_u.resok4;
        COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argoparray);

        atok = &resoparray[opcnt].nfs_resop4_u.opgetattr.GETATTR4res_u.resok4;
        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, bitmap_val);

        atok->obj_attributes.attrmask.bitmap4_val = bitmap_res;
        atok->obj_attributes.attrmask.bitmap4_len = 2;
        atok->obj_attributes.attr_vals.attrlist4_val = fattr_blob;
        atok->obj_attributes.attr_vals.attrlist4_len = sizeof(fattr_blob);

        fhok->object.nfs_fh4_val = (char *)padfilehandle;
        fhok->object.nfs_fh4_len = sizeof(padfilehandle);

        rc = pxy_nfsv4_call(export, opcnt, argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        if(nfs4_Fattr_To_FSAL_attr(&attributes,
                                   &atok->obj_attributes) != NFS4_OK)
                ReturnCode(ERR_FSAL_INVAL, 0);

        pxy_hdl = pxy_alloc_handle(export, &fhok->object, &attributes);
        if (pxy_hdl == NULL)
                ReturnCode(ERR_FSAL_FAULT, 0);
        *handle = &pxy_hdl->obj;
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_lookup(struct fsal_obj_handle *parent,
	   const char *path,
	   struct fsal_obj_handle **handle)
{
        if(!parent)
                ReturnCode(ERR_FSAL_INVAL, 0);
        return pxy_lookup_impl(parent, parent->export, path, handle);
}

static fsal_status_t
pxy_create(struct fsal_obj_handle *dir_hdl,
	   fsal_name_t *name,
	   fsal_attrib_list_t *attrib,
	   struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t 
pxy_mkdir(struct fsal_obj_handle *dir_hdl,
	  fsal_name_t *name,
	  fsal_attrib_list_t *attrib,
	  struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_mknod(struct fsal_obj_handle *dir_hdl,
	  fsal_name_t *name,
	  object_file_type_t nodetype, 
	  fsal_dev_t *dev, 
	  fsal_attrib_list_t *attrib,
          struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_symlink(struct fsal_obj_handle *dir_hdl,
	    fsal_name_t *name,
	    fsal_path_t *link_path,
	    fsal_attrib_list_t *attrib,
	    struct fsal_obj_handle **handle)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_readlink(struct fsal_obj_handle *obj_hdl,
	     char *link_content,
	     uint32_t *link_len,
	     fsal_boolean_t refresh)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_link(struct fsal_obj_handle *obj_hdl,
	 struct fsal_obj_handle *destdir_hdl,
	 fsal_name_t *name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

typedef fsal_status_t (*fsal_readdir_cb)(const char *name,
					 unsigned int dtype,
					 struct fsal_obj_handle *dir_hdl,
					 void *dir_state,
					 struct fsal_cookie *cookie);

static bool_t
xdr_readdirres(XDR *x, nfs_resop4 *rdres)
{
        return xdr_nfs_resop4(x, rdres) && xdr_nfs_resop4(x, rdres + 1);
}

/*
 * Trying to guess how many entries can fit into a readdir buffer
 * is complicated and usually results in either gross over-allocation
 * of the memory for results or under-allocation (on large directories)
 * and buffer overruns - just pay the price of allocating the memory
 * inside XDR decoding and free it when done
 */
static fsal_status_t
pxy_do_readdir(struct pxy_obj_handle *ph, nfs_cookie4 *cookie,
               fsal_readdir_cb cb, void *cbarg, fsal_boolean_t *eof)
{
        uint32_t bitmap_val[2];
        uint32_t opcnt = 0;
        int rc;
        entry4 *e4;
#define FSAL_READDIR_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_READDIR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_READDIR_NB_OP_ALLOC];
        READDIR4resok *rdok;
        fsal_status_t st = {ERR_FSAL_NO_ERROR, 0};

        pxy_create_readdir_bitmap(bitmap_val);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);
        rdok = &resoparray[opcnt].nfs_resop4_u.opreaddir.READDIR4res_u.resok4;
        rdok->reply.entries = NULL;
        COMPOUNDV4_ARG_ADD_OP_READDIR(opcnt, argoparray, *cookie, bitmap_val);

        rc = pxy_nfsv4_call(ph->obj.export, opcnt, argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        *eof = rdok->reply.eof;

        for(e4 = rdok->reply.entries; e4; e4 = e4->nextentry) {
                fsal_attrib_list_t attr;
                char name[MAXNAMLEN+1];
                struct fsal_cookie fc;

                /* UTF8 name does not include trailing 0 */
                if(e4->name.utf8string_len > sizeof(name) - 1)
                        ReturnCode(ERR_FSAL_SERVERFAULT, E2BIG);
                memcpy(name, e4->name.utf8string_val, e4->name.utf8string_len);
                name[e4->name.utf8string_len] = '\0';

                if(nfs4_Fattr_To_FSAL_attr(&attr, &e4->attrs))
                        ReturnCode(ERR_FSAL_FAULT, 0);

                fc.size = sizeof(e4->cookie),
                memcpy(fc.cookie, &e4->cookie, sizeof(e4->cookie));
                *cookie = e4->cookie;
                
                st = cb(name, attr.type, &ph->obj, cbarg, &fc);
                if(FSAL_IS_ERROR(st))
                        break;
        }
        pthread_mutex_lock(&rpc_call_lock);
        clnt_freeres(rpc_client, (xdrproc_t)xdr_readdirres, resoparray);
        pthread_mutex_unlock(&rpc_call_lock);
        return st;
}


/* What to do about verifier if server needs one? */
static fsal_status_t
pxy_readdir(struct fsal_obj_handle *dir_hdl,
	    uint32_t entry_cnt,
	    struct fsal_cookie *whence,
            void *cbarg,
            fsal_readdir_cb cb,
            fsal_boolean_t *eof)
{
        nfs_cookie4 cookie = 0;
        struct pxy_obj_handle *ph;

        if(!dir_hdl || !cb || !eof)
                ReturnCode(ERR_FSAL_INVAL, 0);
        if(whence) {
               if(whence->size != sizeof(cookie))
                       ReturnCode(ERR_FSAL_INVAL, 0);
               memcpy(&cookie, whence->cookie, sizeof(cookie));
        }

        ph = container_of(dir_hdl, struct pxy_obj_handle, obj);

        do {
                fsal_status_t st;

                st = pxy_do_readdir(ph,  &cookie, cb, cbarg, eof);
                if(FSAL_IS_ERROR(st)) {
                        return st;
                }
        } while (*eof == FALSE);

        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_rename(struct fsal_obj_handle *olddir_hdl,
	   fsal_name_t *old_name,
	   struct fsal_obj_handle *newdir_hdl,
	   fsal_name_t *new_name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_getattrs_impl(struct fsal_export *exp,
                  nfs_fh4 *filehandle,
                  fsal_attrib_list_t *obj_attr)
{
        int rc;
        uint32_t opcnt = 0;
        uint32_t bitmap_val[2];
        uint32_t bitmap_res[2];
#define FSAL_GETATTR_NB_OP_ALLOC 2
        nfs_argop4 argoparray[FSAL_GETATTR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_GETATTR_NB_OP_ALLOC];
        GETATTR4resok *atok;
        char fattr_blob[FATTR_BLOB_SZ]; 

        pxy_create_getattr_bitmap(bitmap_val);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, *filehandle);

        atok = &resoparray[opcnt].nfs_resop4_u.opgetattr.GETATTR4res_u.resok4;
        atok->obj_attributes.attrmask.bitmap4_val = bitmap_res;
        atok->obj_attributes.attrmask.bitmap4_len = 2;
        atok->obj_attributes.attr_vals.attrlist4_val = fattr_blob;
        atok->obj_attributes.attr_vals.attrlist4_len = sizeof(fattr_blob);

        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, bitmap_val);

        rc = pxy_nfsv4_call(exp, opcnt, argoparray, resoparray);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        if(nfs4_Fattr_To_FSAL_attr(obj_attr,
                                   &atok->obj_attributes) != NFS4_OK)
                ReturnCode(ERR_FSAL_INVAL, 0);

        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
pxy_getattrs(struct fsal_obj_handle *obj_hdl,
	     fsal_attrib_list_t *obj_attr)
{
        struct pxy_obj_handle *ph;
        fsal_status_t st;

        if(!obj_hdl || !obj_attr)
                ReturnCode(ERR_FSAL_FAULT, EINVAL);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);
        st = pxy_getattrs_impl(obj_hdl->export, &ph->fh4, obj_attr);
        if(!FSAL_IS_ERROR(st)) {
                obj_hdl->attributes = *obj_attr;
        }
        return st;
}

/*
 * Couple of things to note:
 * 1. We assume that checks for things like cansettime are done
 *    by the caller.
 * 2. attrs can be modified in this function but caller cannot
 *    assume that the attributes are up-to-date
 */
static fsal_status_t
pxy_setattrs(struct fsal_obj_handle *obj_hdl,
	     fsal_attrib_list_t *attrs)
{
        int rc;
        fattr4 input_attr;
        uint32_t bm_val[2];
        bitmap4 bmap = {.bitmap4_val = bm_val, .bitmap4_len = 2};
        uint32_t bitmap_res[2];
        uint32_t opcnt = 0;
        struct pxy_obj_handle *ph;
        char fattr_blob[FATTR_BLOB_SZ];
        GETATTR4resok *atok;
        fsal_attrib_list_t attrs_after;

#define FSAL_SETATTR_NB_OP_ALLOC 3
        nfs_argop4 argoparray[FSAL_SETATTR_NB_OP_ALLOC];
        nfs_resop4 resoparray[FSAL_SETATTR_NB_OP_ALLOC];

        if(!obj_hdl || !attrs)
                ReturnCode(ERR_FSAL_FAULT, EINVAL);

        if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_MODE))
                attrs->mode &= ~obj_hdl->export->ops->fs_umask(obj_hdl->export);

        ph = container_of(obj_hdl, struct pxy_obj_handle, obj);

        pxy_create_settable_bitmap(attrs, &bmap);

        if(nfs4_FSALattr_To_Fattr(NULL, attrs, &input_attr, NULL,
                                  NULL, &bmap) == -1)
                ReturnCode(ERR_FSAL_INVAL, EINVAL);

        COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argoparray, ph->fh4);

        resoparray[opcnt].nfs_resop4_u.opsetattr.attrsset.bitmap4_val = bitmap_res;
        resoparray[opcnt].nfs_resop4_u.opsetattr.attrsset.bitmap4_len = 2;
        COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, argoparray, input_attr);

        pxy_create_getattr_bitmap(bm_val);

        atok = &resoparray[opcnt].nfs_resop4_u.opgetattr.GETATTR4res_u.resok4;
        atok->obj_attributes.attrmask.bitmap4_val = bm_val;
        atok->obj_attributes.attrmask.bitmap4_len = 2;
        atok->obj_attributes.attr_vals.attrlist4_val = fattr_blob;
        atok->obj_attributes.attr_vals.attrlist4_len = sizeof(fattr_blob);

        COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argoparray, bm_val);

        rc = pxy_nfsv4_call(obj_hdl->export, opcnt, argoparray, resoparray);
        nfs4_Fattr_Free(&input_attr);
        if(rc != NFS4_OK)
                return nfsstat4_to_fsal(rc);

        rc = nfs4_Fattr_To_FSAL_attr(&attrs_after, &atok->obj_attributes);
        if(rc != NFS4_OK) {
                LogWarn(COMPONENT_FSAL, 
                        "Attribute conversion fails with %d, "
                        "ignoring attibutes after making changes", rc);
        } else {
                obj_hdl->attributes = attrs_after;
        }

        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static fsal_boolean_t 
pxy_handle_is(struct fsal_obj_handle *obj_hdl,
	      object_file_type_t type)
{
        return obj_hdl->type == type;
}

static fsal_boolean_t 
pxy_compare_hdl(struct fsal_obj_handle *a,
	        struct fsal_obj_handle *b)
{
        struct pxy_obj_handle *pa, *pb;

        if(!b)
                return FALSE;

        pa = container_of(a, struct pxy_obj_handle, obj);
        pb = container_of(b, struct pxy_obj_handle, obj);

        if(pa->fh4.nfs_fh4_len != pb->fh4.nfs_fh4_len)
                return FALSE;

	return memcmp(pa->fh4.nfs_fh4_val, pb->fh4.nfs_fh4_val,
                      pa->fh4.nfs_fh4_len);
}

static fsal_status_t
pxy_truncate(struct fsal_obj_handle *obj_hdl,
	     fsal_size_t length)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t
pxy_unlink(struct fsal_obj_handle *dir_hdl,
	   fsal_name_t *name)
{
        ReturnCode(ERR_FSAL_PERM, EPERM);
}

static fsal_status_t 
pxy_handle_digest(struct fsal_obj_handle *obj_hdl,
		  fsal_digesttype_t output_type,
		  struct fsal_handle_desc *fh_desc)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
        size_t fhs;
        void *data;
        uint32_t u32;

	/* sanity checks */
	if( !fh_desc || !fh_desc->start)
		ReturnCode(ERR_FSAL_FAULT, 0);

	switch(output_type) {
	case FSAL_DIGEST_NFSV2:
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
                fhs = ph->fh4.nfs_fh4_len;
                data = ph->fh4.nfs_fh4_val;
		break;
	case FSAL_DIGEST_FILEID2:
                fhs = FSAL_DIGEST_SIZE_FILEID2;
                u32 = ph->obj.attributes.fileid;
                data = &u32;
		break;
	case FSAL_DIGEST_FILEID3:
                fhs = FSAL_DIGEST_SIZE_FILEID3;
                data = &ph->obj.attributes.fileid;
		break;
	case FSAL_DIGEST_FILEID4:
                fhs = FSAL_DIGEST_SIZE_FILEID4;
                data = &ph->obj.attributes.fileid;
		break;
	default:
		ReturnCode(ERR_FSAL_SERVERFAULT, 0);
	}

        if(fh_desc->len < fhs)
                ReturnCode(ERR_FSAL_TOOSMALL, 0);
        memcpy(fh_desc->start, data, fhs);
        fh_desc->len = fhs;
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

static void
pxy_handle_to_key(struct fsal_obj_handle *obj_hdl,
		  struct fsal_handle_desc *fh_desc)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
	fh_desc->start = (caddr_t)ph->fh4.nfs_fh4_val;
	fh_desc->len =  ph->fh4.nfs_fh4_len;;
}

static fsal_status_t
pxy_hdl_release(struct fsal_obj_handle *obj_hdl)
{
        struct pxy_obj_handle *ph =
                container_of(obj_hdl, struct pxy_obj_handle, obj);
        pthread_mutex_lock(&obj_hdl->lock);
        if(obj_hdl->refs != 0) { 
                LogCrit(COMPONENT_FSAL,
                        "Tried to release busy handle @ %p with %d refs",
                        obj_hdl, obj_hdl->refs);
                pthread_mutex_unlock(&obj_hdl->lock);
                ReturnCode(ERR_FSAL_DELAY, EBUSY);
        }
        fsal_detach_handle(obj_hdl->export, &obj_hdl->handles);
        pthread_mutex_unlock(&obj_hdl->lock);
        pthread_mutex_destroy(&obj_hdl->lock);
        free(ph);
	ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

struct fsal_obj_ops pxy_obj_ops = {
	.get = fsal_handle_get,
	.put = fsal_handle_put,
	.release = pxy_hdl_release,
	.lookup = pxy_lookup,
	.readdir = pxy_readdir,
	.create = pxy_create,
	.mkdir = pxy_mkdir,
	.mknode = pxy_mknod,
	.symlink = pxy_symlink,
	.readlink = pxy_readlink,
	.test_access = fsal_test_access,
	.getattrs = pxy_getattrs,
	.setattrs = pxy_setattrs,
	.link = pxy_link,
	.rename = pxy_rename,
	.unlink = pxy_unlink,
	.truncate = pxy_truncate,
	.open = pxy_open,
	.read = pxy_read,
	.write = pxy_write,
	.commit = pxy_commit,
	.lock_op = pxy_lock_op,
	.share_op = pxy_share_op,
	.close = pxy_close,
	.rcp = pxy_rcp,
	.getextattrs = pxy_getextattrs,
	.list_ext_attrs = pxy_list_ext_attrs,
	.getextattr_id_by_name = pxy_getextattr_id_by_name,
	.getextattr_value_by_name = pxy_getextattr_value_by_name,
	.getextattr_value_by_id = pxy_getextattr_value_by_id,
	.setextattr_value = pxy_setextattr_value,
	.setextattr_value_by_id = pxy_setextattr_value_by_id,
	.getextattr_attrs = pxy_getextattr_attrs,
	.remove_extattr_by_id = pxy_remove_extattr_by_id,
	.remove_extattr_by_name = pxy_remove_extattr_by_name,
	.handle_is = pxy_handle_is,
	.lru_cleanup = pxy_lru_cleanup,
	.compare = pxy_compare_hdl,
	.handle_digest = pxy_handle_digest,
	.handle_to_key = pxy_handle_to_key
};

static struct pxy_obj_handle *
pxy_alloc_handle(struct fsal_export *exp, const nfs_fh4 *fh,
                 const fsal_attrib_list_t *attr)
{
        struct pxy_obj_handle *n = malloc(sizeof(*n) + fh->nfs_fh4_len);

        if (n) {
                n->fh4 = *fh;
                n->fh4.nfs_fh4_val = (char *)(n+1);
                memcpy(n->fh4.nfs_fh4_val, fh->nfs_fh4_val, fh->nfs_fh4_len);
                n->obj.attributes = *attr;
                if(fsal_obj_handle_init(&n->obj, &pxy_obj_ops, exp,
                                        attr->type)) {
                        free(n);
                        n = NULL;
                }
        }
        return n;
}

/* export methods that create object handles
 */

fsal_status_t
pxy_lookup_path(struct fsal_export *exp_hdl,
		const char *path,
		struct fsal_obj_handle **handle)
{
        struct fsal_obj_handle *next;
        struct fsal_obj_handle *parent = NULL;
        char *saved;
        char *pcopy;
        char *p;

        if(!path || path[0] != '/')
                ReturnCode(ERR_FSAL_INVAL, EINVAL);

        pcopy = strdup(path);
        if(!pcopy)
                ReturnCode(ERR_FSAL_NOMEM, ENOMEM);

        p = strtok_r(pcopy, "/", &saved);
        do {
                fsal_status_t st = pxy_lookup_impl(parent, exp_hdl, p, &next);
                if(FSAL_IS_ERROR(st)) {
                        free(pcopy);
                        return st;
                }

                if(p) {
                        p = strtok_r(NULL, "/", &saved);
                        parent = next;
                }
        } while(p);

        free(pcopy);
        *handle = next;
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
pxy_create_handle(struct fsal_export *exp_hdl,
		  struct fsal_handle_desc *hdl_desc,
		  struct fsal_obj_handle **handle)
{
        fsal_status_t st;
        nfs_fh4 fh4;
        fsal_attrib_list_t attr;
        struct pxy_obj_handle *ph;

        if(!exp_hdl || !hdl_desc || !handle || (hdl_desc->len > NFS4_FHSIZE))
                ReturnCode(ERR_FSAL_INVAL, 0);

        fh4.nfs_fh4_val = hdl_desc->start;
        fh4.nfs_fh4_len = hdl_desc->len;

        st = pxy_getattrs_impl(exp_hdl, &fh4, &attr);
        if (FSAL_IS_ERROR(st))
                return st;
        
        ph = pxy_alloc_handle(exp_hdl, &fh4, &attr);
        if(!ph)
                ReturnCode(ERR_FSAL_FAULT, 0);

        *handle = &ph->obj;
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

