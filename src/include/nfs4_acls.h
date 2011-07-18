#ifndef _NFS4_ACLS_H
#define _NFS4_ACLS_H

/* Define the return value of ACL operation. */

typedef int fsal_acl_status_t;

#define NFS_V4_ACL_SUCCESS  0
#define NFS_V4_ACL_ERROR  1
#define NFS_V4_ACL_EXISTS  2
#define NFS_V4_ACL_INTERNAL_ERROR  3
#define NFS_V4_ACL_UNAPPROPRIATED_KEY  4
#define NFS_V4_ACL_HASH_SET_ERROR  5
#define NFS_V4_ACL_INIT_ENTRY_FAILED  6
#define NFS_V4_ACL_NOT_FOUND  7

fsal_ace_t *nfs4_ace_alloc(int nace);

void nfs4_ace_free(fsal_ace_t *pace);

void nfs4_acl_entry_inc_ref(fsal_acl_t *pacl);

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *pacldata, fsal_acl_status_t *pstatus);

void nfs4_acl_release_entry(fsal_acl_t *pacl, fsal_acl_status_t *pstatus);

int nfs4_acls_init();

#endif                          /* _NFS4_ACLS_H */

