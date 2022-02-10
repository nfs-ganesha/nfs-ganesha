/* SPDX-License-Identifier: unknown license... */
/* NFSv3 ACLs definitions */

union attr3 switch (bool attributes_follow)
{
  case TRUE:
	fattr3 obj_attributes;
  case FALSE:
	void;
};

struct posix_acl_entry
{
  nfs3_uint32    e_tag;
  nfs3_uint32    e_id;
  nfs3_uint32    e_perm;
};

struct posix_acl
{
  nfs3_uint32        count;
  posix_acl_entry    entries[0];
};

struct getaclargs
{
  nfs_fh3       fhandle;
  nfs3_int32    mask;
};

struct getaclresok
{
  attr3          attr;
  nfs3_int32     mask;
  nfs3_uint32    acl_access_count;
  posix_acl      *acl_access;
  nfs3_uint32    acl_default_count;
  posix_acl      *acl_default;
};

union getaclres switch (nfsstat3 status)
{
  case NFS3_OK:
	getaclresok resok;
  default:
	void;
};

struct setaclargs
{
  nfs_fh3        fhandle;
  nfs3_int32     mask;
  nfs3_uint32    acl_access_count;
  posix_acl      *acl_access;
  nfs3_uint32    acl_default_count;
  posix_acl      *acl_default;
};

struct setaclresok
{
  attr3    attr;
};

union setaclres switch (nfsstat3 status)
{
  case NFS3_OK:
    setaclresok resok;
  default:
    void;
};

program NFSACLPROG
{
  version NFSACL_V3
  {
  	void       NFSACLPROC_NULL(void)          = 0;
  	getaclres  NFSACLPROC_GETACL(getaclargs)  = 1;
  	setaclres  NFSACLPROC_SETACL(setaclargs)  = 2;
  } = 3;
} = 100227;


