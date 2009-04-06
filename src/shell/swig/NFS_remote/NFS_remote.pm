# This file was created automatically by SWIG 1.3.29.
# Don't modify this file, modify the SWIG interface instead.
package NFS_remote;
require Exporter;
require DynaLoader;
@ISA = qw(Exporter DynaLoader);
package NFS_remotec;
bootstrap NFS_remote;
package NFS_remote;
@EXPORT = qw( );

# ---------- BASE METHODS -------------

package NFS_remote;

sub TIEHASH {
    my ($classname,$obj) = @_;
    return bless $obj, $classname;
}

sub CLEAR { }

sub FIRSTKEY { }

sub NEXTKEY { }

sub FETCH {
    my ($self,$field) = @_;
    my $member_func = "swig_${field}_get";
    $self->$member_func();
}

sub STORE {
    my ($self,$field,$newval) = @_;
    my $member_func = "swig_${field}_set";
    $self->$member_func($newval);
}

sub this {
    my $ptr = shift;
    return tied(%$ptr);
}


# ------- FUNCTION WRAPPERS --------

package NFS_remote;

*rpc_init = *NFS_remotec::rpc_init;
*nfs_remote_solvepath = *NFS_remotec::nfs_remote_solvepath;
*nfs_remote_getattr = *NFS_remotec::nfs_remote_getattr;
*nfs_remote_access = *NFS_remotec::nfs_remote_access;
*nfs_remote_readlink = *NFS_remotec::nfs_remote_readlink;
*nfs_remote_readdirplus = *NFS_remotec::nfs_remote_readdirplus;
*nfs_remote_readdirplus_free = *NFS_remotec::nfs_remote_readdirplus_free;
*nfs_remote_readdir = *NFS_remotec::nfs_remote_readdir;
*nfs_remote_readdir_free = *NFS_remotec::nfs_remote_readdir_free;
*nfs_remote_create = *NFS_remotec::nfs_remote_create;
*nfs_remote_mkdir = *NFS_remotec::nfs_remote_mkdir;
*nfs_remote_rmdir = *NFS_remotec::nfs_remote_rmdir;
*nfs_remote_remove = *NFS_remotec::nfs_remote_remove;
*nfs_remote_setattr = *NFS_remotec::nfs_remote_setattr;
*nfs_remote_rename = *NFS_remotec::nfs_remote_rename;
*nfs_remote_link = *NFS_remotec::nfs_remote_link;
*nfs_remote_symlink = *NFS_remotec::nfs_remote_symlink;
*nfs_remote_mount = *NFS_remotec::nfs_remote_mount;
*xdr_nfspath2 = *NFS_remotec::xdr_nfspath2;
*xdr_filename2 = *NFS_remotec::xdr_filename2;
*xdr_fhandle2 = *NFS_remotec::xdr_fhandle2;
*xdr_nfsdata2 = *NFS_remotec::xdr_nfsdata2;
*xdr_nfscookie2 = *NFS_remotec::xdr_nfscookie2;
*xdr_nfsstat2 = *NFS_remotec::xdr_nfsstat2;
*xdr_ftype2 = *NFS_remotec::xdr_ftype2;
*xdr_nfstime2 = *NFS_remotec::xdr_nfstime2;
*xdr_fattr2 = *NFS_remotec::xdr_fattr2;
*xdr_fhstatus2 = *NFS_remotec::xdr_fhstatus2;
*xdr_diropargs2 = *NFS_remotec::xdr_diropargs2;
*xdr_DIROP2resok = *NFS_remotec::xdr_DIROP2resok;
*xdr_DIROP2res = *NFS_remotec::xdr_DIROP2res;
*xdr_ATTR2res = *NFS_remotec::xdr_ATTR2res;
*xdr_sattr2 = *NFS_remotec::xdr_sattr2;
*xdr_statinfo2 = *NFS_remotec::xdr_statinfo2;
*xdr_STATFS2res = *NFS_remotec::xdr_STATFS2res;
*xdr_READDIR2args = *NFS_remotec::xdr_READDIR2args;
*xdr_entry2 = *NFS_remotec::xdr_entry2;
*xdr_READDIR2resok = *NFS_remotec::xdr_READDIR2resok;
*xdr_READDIR2res = *NFS_remotec::xdr_READDIR2res;
*xdr_SYMLINK2args = *NFS_remotec::xdr_SYMLINK2args;
*xdr_LINK2args = *NFS_remotec::xdr_LINK2args;
*xdr_RENAME2args = *NFS_remotec::xdr_RENAME2args;
*xdr_CREATE2args = *NFS_remotec::xdr_CREATE2args;
*xdr_WRITE2args = *NFS_remotec::xdr_WRITE2args;
*xdr_READ2resok = *NFS_remotec::xdr_READ2resok;
*xdr_READ2res = *NFS_remotec::xdr_READ2res;
*xdr_READ2args = *NFS_remotec::xdr_READ2args;
*xdr_READLINK2res = *NFS_remotec::xdr_READLINK2res;
*xdr_SETATTR2args = *NFS_remotec::xdr_SETATTR2args;
*xdr_nfs3_uint64 = *NFS_remotec::xdr_nfs3_uint64;
*xdr_nfs3_int64 = *NFS_remotec::xdr_nfs3_int64;
*xdr_nfs3_uint32 = *NFS_remotec::xdr_nfs3_uint32;
*xdr_nfs3_int32 = *NFS_remotec::xdr_nfs3_int32;
*xdr_filename3 = *NFS_remotec::xdr_filename3;
*xdr_nfspath3 = *NFS_remotec::xdr_nfspath3;
*xdr_fileid3 = *NFS_remotec::xdr_fileid3;
*xdr_cookie3 = *NFS_remotec::xdr_cookie3;
*xdr_fhandle3 = *NFS_remotec::xdr_fhandle3;
*xdr_cookieverf3 = *NFS_remotec::xdr_cookieverf3;
*xdr_createverf3 = *NFS_remotec::xdr_createverf3;
*xdr_writeverf3 = *NFS_remotec::xdr_writeverf3;
*xdr_uid3 = *NFS_remotec::xdr_uid3;
*xdr_gid3 = *NFS_remotec::xdr_gid3;
*xdr_size3 = *NFS_remotec::xdr_size3;
*xdr_offset3 = *NFS_remotec::xdr_offset3;
*xdr_mode3 = *NFS_remotec::xdr_mode3;
*xdr_count3 = *NFS_remotec::xdr_count3;
*xdr_nfsstat3 = *NFS_remotec::xdr_nfsstat3;
*xdr_ftype3 = *NFS_remotec::xdr_ftype3;
*xdr_specdata3 = *NFS_remotec::xdr_specdata3;
*xdr_nfs_fh3 = *NFS_remotec::xdr_nfs_fh3;
*xdr_nfstime3 = *NFS_remotec::xdr_nfstime3;
*xdr_fattr3 = *NFS_remotec::xdr_fattr3;
*xdr_post_op_attr = *NFS_remotec::xdr_post_op_attr;
*xdr_wcc_attr = *NFS_remotec::xdr_wcc_attr;
*xdr_pre_op_attr = *NFS_remotec::xdr_pre_op_attr;
*xdr_wcc_data = *NFS_remotec::xdr_wcc_data;
*xdr_post_op_fh3 = *NFS_remotec::xdr_post_op_fh3;
*xdr_time_how = *NFS_remotec::xdr_time_how;
*xdr_set_mode3 = *NFS_remotec::xdr_set_mode3;
*xdr_set_uid3 = *NFS_remotec::xdr_set_uid3;
*xdr_set_gid3 = *NFS_remotec::xdr_set_gid3;
*xdr_set_size3 = *NFS_remotec::xdr_set_size3;
*xdr_set_atime = *NFS_remotec::xdr_set_atime;
*xdr_set_mtime = *NFS_remotec::xdr_set_mtime;
*xdr_sattr3 = *NFS_remotec::xdr_sattr3;
*xdr_diropargs3 = *NFS_remotec::xdr_diropargs3;
*xdr_GETATTR3args = *NFS_remotec::xdr_GETATTR3args;
*xdr_GETATTR3resok = *NFS_remotec::xdr_GETATTR3resok;
*xdr_GETATTR3res = *NFS_remotec::xdr_GETATTR3res;
*xdr_sattrguard3 = *NFS_remotec::xdr_sattrguard3;
*xdr_SETATTR3args = *NFS_remotec::xdr_SETATTR3args;
*xdr_SETATTR3resok = *NFS_remotec::xdr_SETATTR3resok;
*xdr_SETATTR3resfail = *NFS_remotec::xdr_SETATTR3resfail;
*xdr_SETATTR3res = *NFS_remotec::xdr_SETATTR3res;
*xdr_LOOKUP3args = *NFS_remotec::xdr_LOOKUP3args;
*xdr_LOOKUP3resok = *NFS_remotec::xdr_LOOKUP3resok;
*xdr_LOOKUP3resfail = *NFS_remotec::xdr_LOOKUP3resfail;
*xdr_LOOKUP3res = *NFS_remotec::xdr_LOOKUP3res;
*xdr_ACCESS3args = *NFS_remotec::xdr_ACCESS3args;
*xdr_ACCESS3resok = *NFS_remotec::xdr_ACCESS3resok;
*xdr_ACCESS3resfail = *NFS_remotec::xdr_ACCESS3resfail;
*xdr_ACCESS3res = *NFS_remotec::xdr_ACCESS3res;
*xdr_READLINK3args = *NFS_remotec::xdr_READLINK3args;
*xdr_READLINK3resok = *NFS_remotec::xdr_READLINK3resok;
*xdr_READLINK3resfail = *NFS_remotec::xdr_READLINK3resfail;
*xdr_READLINK3res = *NFS_remotec::xdr_READLINK3res;
*xdr_READ3args = *NFS_remotec::xdr_READ3args;
*xdr_READ3resok = *NFS_remotec::xdr_READ3resok;
*xdr_READ3resfail = *NFS_remotec::xdr_READ3resfail;
*xdr_READ3res = *NFS_remotec::xdr_READ3res;
*xdr_stable_how = *NFS_remotec::xdr_stable_how;
*xdr_WRITE3args = *NFS_remotec::xdr_WRITE3args;
*xdr_WRITE3resok = *NFS_remotec::xdr_WRITE3resok;
*xdr_WRITE3resfail = *NFS_remotec::xdr_WRITE3resfail;
*xdr_WRITE3res = *NFS_remotec::xdr_WRITE3res;
*xdr_createmode3 = *NFS_remotec::xdr_createmode3;
*xdr_createhow3 = *NFS_remotec::xdr_createhow3;
*xdr_CREATE3args = *NFS_remotec::xdr_CREATE3args;
*xdr_CREATE3resok = *NFS_remotec::xdr_CREATE3resok;
*xdr_CREATE3resfail = *NFS_remotec::xdr_CREATE3resfail;
*xdr_CREATE3res = *NFS_remotec::xdr_CREATE3res;
*xdr_MKDIR3args = *NFS_remotec::xdr_MKDIR3args;
*xdr_MKDIR3resok = *NFS_remotec::xdr_MKDIR3resok;
*xdr_MKDIR3resfail = *NFS_remotec::xdr_MKDIR3resfail;
*xdr_MKDIR3res = *NFS_remotec::xdr_MKDIR3res;
*xdr_symlinkdata3 = *NFS_remotec::xdr_symlinkdata3;
*xdr_SYMLINK3args = *NFS_remotec::xdr_SYMLINK3args;
*xdr_SYMLINK3resok = *NFS_remotec::xdr_SYMLINK3resok;
*xdr_SYMLINK3resfail = *NFS_remotec::xdr_SYMLINK3resfail;
*xdr_SYMLINK3res = *NFS_remotec::xdr_SYMLINK3res;
*xdr_devicedata3 = *NFS_remotec::xdr_devicedata3;
*xdr_mknoddata3 = *NFS_remotec::xdr_mknoddata3;
*xdr_MKNOD3args = *NFS_remotec::xdr_MKNOD3args;
*xdr_MKNOD3resok = *NFS_remotec::xdr_MKNOD3resok;
*xdr_MKNOD3resfail = *NFS_remotec::xdr_MKNOD3resfail;
*xdr_MKNOD3res = *NFS_remotec::xdr_MKNOD3res;
*xdr_REMOVE3args = *NFS_remotec::xdr_REMOVE3args;
*xdr_REMOVE3resok = *NFS_remotec::xdr_REMOVE3resok;
*xdr_REMOVE3resfail = *NFS_remotec::xdr_REMOVE3resfail;
*xdr_REMOVE3res = *NFS_remotec::xdr_REMOVE3res;
*xdr_RMDIR3args = *NFS_remotec::xdr_RMDIR3args;
*xdr_RMDIR3resok = *NFS_remotec::xdr_RMDIR3resok;
*xdr_RMDIR3resfail = *NFS_remotec::xdr_RMDIR3resfail;
*xdr_RMDIR3res = *NFS_remotec::xdr_RMDIR3res;
*xdr_RENAME3args = *NFS_remotec::xdr_RENAME3args;
*xdr_RENAME3resok = *NFS_remotec::xdr_RENAME3resok;
*xdr_RENAME3resfail = *NFS_remotec::xdr_RENAME3resfail;
*xdr_RENAME3res = *NFS_remotec::xdr_RENAME3res;
*xdr_LINK3args = *NFS_remotec::xdr_LINK3args;
*xdr_LINK3resok = *NFS_remotec::xdr_LINK3resok;
*xdr_LINK3resfail = *NFS_remotec::xdr_LINK3resfail;
*xdr_LINK3res = *NFS_remotec::xdr_LINK3res;
*xdr_READDIR3args = *NFS_remotec::xdr_READDIR3args;
*xdr_entry3 = *NFS_remotec::xdr_entry3;
*xdr_dirlist3 = *NFS_remotec::xdr_dirlist3;
*xdr_READDIR3resok = *NFS_remotec::xdr_READDIR3resok;
*xdr_READDIR3resfail = *NFS_remotec::xdr_READDIR3resfail;
*xdr_READDIR3res = *NFS_remotec::xdr_READDIR3res;
*xdr_READDIRPLUS3args = *NFS_remotec::xdr_READDIRPLUS3args;
*xdr_entryplus3 = *NFS_remotec::xdr_entryplus3;
*xdr_dirlistplus3 = *NFS_remotec::xdr_dirlistplus3;
*xdr_READDIRPLUS3resok = *NFS_remotec::xdr_READDIRPLUS3resok;
*xdr_READDIRPLUS3resfail = *NFS_remotec::xdr_READDIRPLUS3resfail;
*xdr_READDIRPLUS3res = *NFS_remotec::xdr_READDIRPLUS3res;
*xdr_FSSTAT3args = *NFS_remotec::xdr_FSSTAT3args;
*xdr_FSSTAT3resok = *NFS_remotec::xdr_FSSTAT3resok;
*xdr_FSSTAT3resfail = *NFS_remotec::xdr_FSSTAT3resfail;
*xdr_FSSTAT3res = *NFS_remotec::xdr_FSSTAT3res;
*xdr_FSINFO3args = *NFS_remotec::xdr_FSINFO3args;
*xdr_FSINFO3resok = *NFS_remotec::xdr_FSINFO3resok;
*xdr_FSINFO3resfail = *NFS_remotec::xdr_FSINFO3resfail;
*xdr_FSINFO3res = *NFS_remotec::xdr_FSINFO3res;
*xdr_PATHCONF3args = *NFS_remotec::xdr_PATHCONF3args;
*xdr_PATHCONF3resok = *NFS_remotec::xdr_PATHCONF3resok;
*xdr_PATHCONF3resfail = *NFS_remotec::xdr_PATHCONF3resfail;
*xdr_PATHCONF3res = *NFS_remotec::xdr_PATHCONF3res;
*xdr_COMMIT3args = *NFS_remotec::xdr_COMMIT3args;
*xdr_COMMIT3resok = *NFS_remotec::xdr_COMMIT3resok;
*xdr_COMMIT3resfail = *NFS_remotec::xdr_COMMIT3resfail;
*xdr_COMMIT3res = *NFS_remotec::xdr_COMMIT3res;
*xdr_mountstat3 = *NFS_remotec::xdr_mountstat3;
*xdr_dirpath = *NFS_remotec::xdr_dirpath;
*xdr_name = *NFS_remotec::xdr_name;
*xdr_groups = *NFS_remotec::xdr_groups;
*xdr_groupnode = *NFS_remotec::xdr_groupnode;
*xdr_exports = *NFS_remotec::xdr_exports;
*xdr_exportnode = *NFS_remotec::xdr_exportnode;
*xdr_mountlist = *NFS_remotec::xdr_mountlist;
*xdr_mountbody = *NFS_remotec::xdr_mountbody;
*xdr_mountres3_ok = *NFS_remotec::xdr_mountres3_ok;
*xdr_mountres3 = *NFS_remotec::xdr_mountres3;
*print_nfs_attributes = *NFS_remotec::print_nfs_attributes;
*new_cookie3 = *NFS_remotec::new_cookie3;
*new_p_cookieverf3 = *NFS_remotec::new_p_cookieverf3;
*new_pp_nfs_res_t = *NFS_remotec::new_pp_nfs_res_t;
*fopen = *NFS_remotec::fopen;
*fclose = *NFS_remotec::fclose;

############# Class : NFS_remote::shell_fh3_t ##############

package NFS_remote::shell_fh3_t;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_len_get = *NFS_remotec::shell_fh3_t_data_len_get;
*swig_data_len_set = *NFS_remotec::shell_fh3_t_data_len_set;
*swig_data_val_get = *NFS_remotec::shell_fh3_t_data_val_get;
*swig_data_val_set = *NFS_remotec::shell_fh3_t_data_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_shell_fh3_t(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_shell_fh3_t($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfsdata2 ##############

package NFS_remote::nfsdata2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_nfsdata2_len_get = *NFS_remotec::nfsdata2_nfsdata2_len_get;
*swig_nfsdata2_len_set = *NFS_remotec::nfsdata2_nfsdata2_len_set;
*swig_nfsdata2_val_get = *NFS_remotec::nfsdata2_nfsdata2_val_get;
*swig_nfsdata2_val_set = *NFS_remotec::nfsdata2_nfsdata2_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfsdata2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfsdata2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfstime2 ##############

package NFS_remote::nfstime2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_seconds_get = *NFS_remotec::nfstime2_seconds_get;
*swig_seconds_set = *NFS_remotec::nfstime2_seconds_set;
*swig_useconds_get = *NFS_remotec::nfstime2_useconds_get;
*swig_useconds_set = *NFS_remotec::nfstime2_useconds_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfstime2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfstime2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::fattr2 ##############

package NFS_remote::fattr2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_type_get = *NFS_remotec::fattr2_type_get;
*swig_type_set = *NFS_remotec::fattr2_type_set;
*swig_mode_get = *NFS_remotec::fattr2_mode_get;
*swig_mode_set = *NFS_remotec::fattr2_mode_set;
*swig_nlink_get = *NFS_remotec::fattr2_nlink_get;
*swig_nlink_set = *NFS_remotec::fattr2_nlink_set;
*swig_uid_get = *NFS_remotec::fattr2_uid_get;
*swig_uid_set = *NFS_remotec::fattr2_uid_set;
*swig_gid_get = *NFS_remotec::fattr2_gid_get;
*swig_gid_set = *NFS_remotec::fattr2_gid_set;
*swig_size_get = *NFS_remotec::fattr2_size_get;
*swig_size_set = *NFS_remotec::fattr2_size_set;
*swig_blocksize_get = *NFS_remotec::fattr2_blocksize_get;
*swig_blocksize_set = *NFS_remotec::fattr2_blocksize_set;
*swig_rdev_get = *NFS_remotec::fattr2_rdev_get;
*swig_rdev_set = *NFS_remotec::fattr2_rdev_set;
*swig_blocks_get = *NFS_remotec::fattr2_blocks_get;
*swig_blocks_set = *NFS_remotec::fattr2_blocks_set;
*swig_fsid_get = *NFS_remotec::fattr2_fsid_get;
*swig_fsid_set = *NFS_remotec::fattr2_fsid_set;
*swig_fileid_get = *NFS_remotec::fattr2_fileid_get;
*swig_fileid_set = *NFS_remotec::fattr2_fileid_set;
*swig_atime_get = *NFS_remotec::fattr2_atime_get;
*swig_atime_set = *NFS_remotec::fattr2_atime_set;
*swig_mtime_get = *NFS_remotec::fattr2_mtime_get;
*swig_mtime_set = *NFS_remotec::fattr2_mtime_set;
*swig_ctime_get = *NFS_remotec::fattr2_ctime_get;
*swig_ctime_set = *NFS_remotec::fattr2_ctime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_fattr2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_fattr2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::fhstatus2 ##############

package NFS_remote::fhstatus2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::fhstatus2_status_get;
*swig_status_set = *NFS_remotec::fhstatus2_status_set;
*swig_fhstatus2_u_get = *NFS_remotec::fhstatus2_fhstatus2_u_get;
*swig_fhstatus2_u_set = *NFS_remotec::fhstatus2_fhstatus2_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_fhstatus2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_fhstatus2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::fhstatus2_fhstatus2_u ##############

package NFS_remote::fhstatus2_fhstatus2_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_directory_get = *NFS_remotec::fhstatus2_fhstatus2_u_directory_get;
*swig_directory_set = *NFS_remotec::fhstatus2_fhstatus2_u_directory_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_fhstatus2_fhstatus2_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_fhstatus2_fhstatus2_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::diropargs2 ##############

package NFS_remote::diropargs2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_get = *NFS_remotec::diropargs2_dir_get;
*swig_dir_set = *NFS_remotec::diropargs2_dir_set;
*swig_name_get = *NFS_remotec::diropargs2_name_get;
*swig_name_set = *NFS_remotec::diropargs2_name_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_diropargs2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_diropargs2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::DIROP2resok ##############

package NFS_remote::DIROP2resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::DIROP2resok_file_get;
*swig_file_set = *NFS_remotec::DIROP2resok_file_set;
*swig_attributes_get = *NFS_remotec::DIROP2resok_attributes_get;
*swig_attributes_set = *NFS_remotec::DIROP2resok_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_DIROP2resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_DIROP2resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::DIROP2res ##############

package NFS_remote::DIROP2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::DIROP2res_status_get;
*swig_status_set = *NFS_remotec::DIROP2res_status_set;
*swig_DIROP2res_u_get = *NFS_remotec::DIROP2res_DIROP2res_u_get;
*swig_DIROP2res_u_set = *NFS_remotec::DIROP2res_DIROP2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_DIROP2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_DIROP2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::DIROP2res_DIROP2res_u ##############

package NFS_remote::DIROP2res_DIROP2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_diropok_get = *NFS_remotec::DIROP2res_DIROP2res_u_diropok_get;
*swig_diropok_set = *NFS_remotec::DIROP2res_DIROP2res_u_diropok_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_DIROP2res_DIROP2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_DIROP2res_DIROP2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ATTR2res ##############

package NFS_remote::ATTR2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::ATTR2res_status_get;
*swig_status_set = *NFS_remotec::ATTR2res_status_set;
*swig_ATTR2res_u_get = *NFS_remotec::ATTR2res_ATTR2res_u_get;
*swig_ATTR2res_u_set = *NFS_remotec::ATTR2res_ATTR2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ATTR2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ATTR2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ATTR2res_ATTR2res_u ##############

package NFS_remote::ATTR2res_ATTR2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_get = *NFS_remotec::ATTR2res_ATTR2res_u_attributes_get;
*swig_attributes_set = *NFS_remotec::ATTR2res_ATTR2res_u_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ATTR2res_ATTR2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ATTR2res_ATTR2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::sattr2 ##############

package NFS_remote::sattr2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mode_get = *NFS_remotec::sattr2_mode_get;
*swig_mode_set = *NFS_remotec::sattr2_mode_set;
*swig_uid_get = *NFS_remotec::sattr2_uid_get;
*swig_uid_set = *NFS_remotec::sattr2_uid_set;
*swig_gid_get = *NFS_remotec::sattr2_gid_get;
*swig_gid_set = *NFS_remotec::sattr2_gid_set;
*swig_size_get = *NFS_remotec::sattr2_size_get;
*swig_size_set = *NFS_remotec::sattr2_size_set;
*swig_atime_get = *NFS_remotec::sattr2_atime_get;
*swig_atime_set = *NFS_remotec::sattr2_atime_set;
*swig_mtime_get = *NFS_remotec::sattr2_mtime_get;
*swig_mtime_set = *NFS_remotec::sattr2_mtime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_sattr2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_sattr2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::statinfo2 ##############

package NFS_remote::statinfo2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_tsize_get = *NFS_remotec::statinfo2_tsize_get;
*swig_tsize_set = *NFS_remotec::statinfo2_tsize_set;
*swig_bsize_get = *NFS_remotec::statinfo2_bsize_get;
*swig_bsize_set = *NFS_remotec::statinfo2_bsize_set;
*swig_blocks_get = *NFS_remotec::statinfo2_blocks_get;
*swig_blocks_set = *NFS_remotec::statinfo2_blocks_set;
*swig_bfree_get = *NFS_remotec::statinfo2_bfree_get;
*swig_bfree_set = *NFS_remotec::statinfo2_bfree_set;
*swig_bavail_get = *NFS_remotec::statinfo2_bavail_get;
*swig_bavail_set = *NFS_remotec::statinfo2_bavail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_statinfo2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_statinfo2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::STATFS2res ##############

package NFS_remote::STATFS2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::STATFS2res_status_get;
*swig_status_set = *NFS_remotec::STATFS2res_status_set;
*swig_STATFS2res_u_get = *NFS_remotec::STATFS2res_STATFS2res_u_get;
*swig_STATFS2res_u_set = *NFS_remotec::STATFS2res_STATFS2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_STATFS2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_STATFS2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::STATFS2res_STATFS2res_u ##############

package NFS_remote::STATFS2res_STATFS2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_info_get = *NFS_remotec::STATFS2res_STATFS2res_u_info_get;
*swig_info_set = *NFS_remotec::STATFS2res_STATFS2res_u_info_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_STATFS2res_STATFS2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_STATFS2res_STATFS2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR2args ##############

package NFS_remote::READDIR2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_get = *NFS_remotec::READDIR2args_dir_get;
*swig_dir_set = *NFS_remotec::READDIR2args_dir_set;
*swig_cookie_get = *NFS_remotec::READDIR2args_cookie_get;
*swig_cookie_set = *NFS_remotec::READDIR2args_cookie_set;
*swig_count_get = *NFS_remotec::READDIR2args_count_get;
*swig_count_set = *NFS_remotec::READDIR2args_count_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::entry2 ##############

package NFS_remote::entry2;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fileid_get = *NFS_remotec::entry2_fileid_get;
*swig_fileid_set = *NFS_remotec::entry2_fileid_set;
*swig_name_get = *NFS_remotec::entry2_name_get;
*swig_name_set = *NFS_remotec::entry2_name_set;
*swig_cookie_get = *NFS_remotec::entry2_cookie_get;
*swig_cookie_set = *NFS_remotec::entry2_cookie_set;
*swig_nextentry_get = *NFS_remotec::entry2_nextentry_get;
*swig_nextentry_set = *NFS_remotec::entry2_nextentry_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_entry2(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_entry2($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR2resok ##############

package NFS_remote::READDIR2resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_entries_get = *NFS_remotec::READDIR2resok_entries_get;
*swig_entries_set = *NFS_remotec::READDIR2resok_entries_set;
*swig_eof_get = *NFS_remotec::READDIR2resok_eof_get;
*swig_eof_set = *NFS_remotec::READDIR2resok_eof_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR2resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR2resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR2res ##############

package NFS_remote::READDIR2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READDIR2res_status_get;
*swig_status_set = *NFS_remotec::READDIR2res_status_set;
*swig_READDIR2res_u_get = *NFS_remotec::READDIR2res_READDIR2res_u_get;
*swig_READDIR2res_u_set = *NFS_remotec::READDIR2res_READDIR2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR2res_READDIR2res_u ##############

package NFS_remote::READDIR2res_READDIR2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_readdirok_get = *NFS_remotec::READDIR2res_READDIR2res_u_readdirok_get;
*swig_readdirok_set = *NFS_remotec::READDIR2res_READDIR2res_u_readdirok_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR2res_READDIR2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR2res_READDIR2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK2args ##############

package NFS_remote::SYMLINK2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_from_get = *NFS_remotec::SYMLINK2args_from_get;
*swig_from_set = *NFS_remotec::SYMLINK2args_from_set;
*swig_to_get = *NFS_remotec::SYMLINK2args_to_get;
*swig_to_set = *NFS_remotec::SYMLINK2args_to_set;
*swig_attributes_get = *NFS_remotec::SYMLINK2args_attributes_get;
*swig_attributes_set = *NFS_remotec::SYMLINK2args_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK2args ##############

package NFS_remote::LINK2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_from_get = *NFS_remotec::LINK2args_from_get;
*swig_from_set = *NFS_remotec::LINK2args_from_set;
*swig_to_get = *NFS_remotec::LINK2args_to_get;
*swig_to_set = *NFS_remotec::LINK2args_to_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME2args ##############

package NFS_remote::RENAME2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_from_get = *NFS_remotec::RENAME2args_from_get;
*swig_from_set = *NFS_remotec::RENAME2args_from_set;
*swig_to_get = *NFS_remotec::RENAME2args_to_get;
*swig_to_set = *NFS_remotec::RENAME2args_to_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE2args ##############

package NFS_remote::CREATE2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_where_get = *NFS_remotec::CREATE2args_where_get;
*swig_where_set = *NFS_remotec::CREATE2args_where_set;
*swig_attributes_get = *NFS_remotec::CREATE2args_attributes_get;
*swig_attributes_set = *NFS_remotec::CREATE2args_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE2args ##############

package NFS_remote::WRITE2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::WRITE2args_file_get;
*swig_file_set = *NFS_remotec::WRITE2args_file_set;
*swig_beginoffset_get = *NFS_remotec::WRITE2args_beginoffset_get;
*swig_beginoffset_set = *NFS_remotec::WRITE2args_beginoffset_set;
*swig_offset_get = *NFS_remotec::WRITE2args_offset_get;
*swig_offset_set = *NFS_remotec::WRITE2args_offset_set;
*swig_totalcount_get = *NFS_remotec::WRITE2args_totalcount_get;
*swig_totalcount_set = *NFS_remotec::WRITE2args_totalcount_set;
*swig_data_get = *NFS_remotec::WRITE2args_data_get;
*swig_data_set = *NFS_remotec::WRITE2args_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ2resok ##############

package NFS_remote::READ2resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_get = *NFS_remotec::READ2resok_attributes_get;
*swig_attributes_set = *NFS_remotec::READ2resok_attributes_set;
*swig_data_get = *NFS_remotec::READ2resok_data_get;
*swig_data_set = *NFS_remotec::READ2resok_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ2resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ2resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ2res ##############

package NFS_remote::READ2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READ2res_status_get;
*swig_status_set = *NFS_remotec::READ2res_status_set;
*swig_READ2res_u_get = *NFS_remotec::READ2res_READ2res_u_get;
*swig_READ2res_u_set = *NFS_remotec::READ2res_READ2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ2res_READ2res_u ##############

package NFS_remote::READ2res_READ2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_readok_get = *NFS_remotec::READ2res_READ2res_u_readok_get;
*swig_readok_set = *NFS_remotec::READ2res_READ2res_u_readok_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ2res_READ2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ2res_READ2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ2args ##############

package NFS_remote::READ2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::READ2args_file_get;
*swig_file_set = *NFS_remotec::READ2args_file_set;
*swig_offset_get = *NFS_remotec::READ2args_offset_get;
*swig_offset_set = *NFS_remotec::READ2args_offset_set;
*swig_count_get = *NFS_remotec::READ2args_count_get;
*swig_count_set = *NFS_remotec::READ2args_count_set;
*swig_totalcount_get = *NFS_remotec::READ2args_totalcount_get;
*swig_totalcount_set = *NFS_remotec::READ2args_totalcount_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK2res ##############

package NFS_remote::READLINK2res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READLINK2res_status_get;
*swig_status_set = *NFS_remotec::READLINK2res_status_set;
*swig_READLINK2res_u_get = *NFS_remotec::READLINK2res_READLINK2res_u_get;
*swig_READLINK2res_u_set = *NFS_remotec::READLINK2res_READLINK2res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK2res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK2res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK2res_READLINK2res_u ##############

package NFS_remote::READLINK2res_READLINK2res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_get = *NFS_remotec::READLINK2res_READLINK2res_u_data_get;
*swig_data_set = *NFS_remotec::READLINK2res_READLINK2res_u_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK2res_READLINK2res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK2res_READLINK2res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR2args ##############

package NFS_remote::SETATTR2args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::SETATTR2args_file_get;
*swig_file_set = *NFS_remotec::SETATTR2args_file_set;
*swig_attributes_get = *NFS_remotec::SETATTR2args_attributes_get;
*swig_attributes_set = *NFS_remotec::SETATTR2args_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR2args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR2args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::specdata3 ##############

package NFS_remote::specdata3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_specdata1_get = *NFS_remotec::specdata3_specdata1_get;
*swig_specdata1_set = *NFS_remotec::specdata3_specdata1_set;
*swig_specdata2_get = *NFS_remotec::specdata3_specdata2_get;
*swig_specdata2_set = *NFS_remotec::specdata3_specdata2_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_specdata3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_specdata3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfs_fh3 ##############

package NFS_remote::nfs_fh3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_get = *NFS_remotec::nfs_fh3_data_get;
*swig_data_set = *NFS_remotec::nfs_fh3_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfs_fh3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfs_fh3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfs_fh3_data ##############

package NFS_remote::nfs_fh3_data;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_len_get = *NFS_remotec::nfs_fh3_data_data_len_get;
*swig_data_len_set = *NFS_remotec::nfs_fh3_data_data_len_set;
*swig_data_val_get = *NFS_remotec::nfs_fh3_data_data_val_get;
*swig_data_val_set = *NFS_remotec::nfs_fh3_data_data_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfs_fh3_data(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfs_fh3_data($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfstime3 ##############

package NFS_remote::nfstime3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_seconds_get = *NFS_remotec::nfstime3_seconds_get;
*swig_seconds_set = *NFS_remotec::nfstime3_seconds_set;
*swig_nseconds_get = *NFS_remotec::nfstime3_nseconds_get;
*swig_nseconds_set = *NFS_remotec::nfstime3_nseconds_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfstime3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfstime3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::fattr3 ##############

package NFS_remote::fattr3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_type_get = *NFS_remotec::fattr3_type_get;
*swig_type_set = *NFS_remotec::fattr3_type_set;
*swig_mode_get = *NFS_remotec::fattr3_mode_get;
*swig_mode_set = *NFS_remotec::fattr3_mode_set;
*swig_nlink_get = *NFS_remotec::fattr3_nlink_get;
*swig_nlink_set = *NFS_remotec::fattr3_nlink_set;
*swig_uid_get = *NFS_remotec::fattr3_uid_get;
*swig_uid_set = *NFS_remotec::fattr3_uid_set;
*swig_gid_get = *NFS_remotec::fattr3_gid_get;
*swig_gid_set = *NFS_remotec::fattr3_gid_set;
*swig_size_get = *NFS_remotec::fattr3_size_get;
*swig_size_set = *NFS_remotec::fattr3_size_set;
*swig_used_get = *NFS_remotec::fattr3_used_get;
*swig_used_set = *NFS_remotec::fattr3_used_set;
*swig_rdev_get = *NFS_remotec::fattr3_rdev_get;
*swig_rdev_set = *NFS_remotec::fattr3_rdev_set;
*swig_fsid_get = *NFS_remotec::fattr3_fsid_get;
*swig_fsid_set = *NFS_remotec::fattr3_fsid_set;
*swig_fileid_get = *NFS_remotec::fattr3_fileid_get;
*swig_fileid_set = *NFS_remotec::fattr3_fileid_set;
*swig_atime_get = *NFS_remotec::fattr3_atime_get;
*swig_atime_set = *NFS_remotec::fattr3_atime_set;
*swig_mtime_get = *NFS_remotec::fattr3_mtime_get;
*swig_mtime_set = *NFS_remotec::fattr3_mtime_set;
*swig_ctime_get = *NFS_remotec::fattr3_ctime_get;
*swig_ctime_set = *NFS_remotec::fattr3_ctime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_fattr3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_fattr3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::post_op_attr ##############

package NFS_remote::post_op_attr;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_follow_get = *NFS_remotec::post_op_attr_attributes_follow_get;
*swig_attributes_follow_set = *NFS_remotec::post_op_attr_attributes_follow_set;
*swig_post_op_attr_u_get = *NFS_remotec::post_op_attr_post_op_attr_u_get;
*swig_post_op_attr_u_set = *NFS_remotec::post_op_attr_post_op_attr_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_post_op_attr(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_post_op_attr($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::post_op_attr_post_op_attr_u ##############

package NFS_remote::post_op_attr_post_op_attr_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_get = *NFS_remotec::post_op_attr_post_op_attr_u_attributes_get;
*swig_attributes_set = *NFS_remotec::post_op_attr_post_op_attr_u_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_post_op_attr_post_op_attr_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_post_op_attr_post_op_attr_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::wcc_attr ##############

package NFS_remote::wcc_attr;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_size_get = *NFS_remotec::wcc_attr_size_get;
*swig_size_set = *NFS_remotec::wcc_attr_size_set;
*swig_mtime_get = *NFS_remotec::wcc_attr_mtime_get;
*swig_mtime_set = *NFS_remotec::wcc_attr_mtime_set;
*swig_ctime_get = *NFS_remotec::wcc_attr_ctime_get;
*swig_ctime_set = *NFS_remotec::wcc_attr_ctime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_wcc_attr(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_wcc_attr($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::pre_op_attr ##############

package NFS_remote::pre_op_attr;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_follow_get = *NFS_remotec::pre_op_attr_attributes_follow_get;
*swig_attributes_follow_set = *NFS_remotec::pre_op_attr_attributes_follow_set;
*swig_pre_op_attr_u_get = *NFS_remotec::pre_op_attr_pre_op_attr_u_get;
*swig_pre_op_attr_u_set = *NFS_remotec::pre_op_attr_pre_op_attr_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_pre_op_attr(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_pre_op_attr($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::pre_op_attr_pre_op_attr_u ##############

package NFS_remote::pre_op_attr_pre_op_attr_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_attributes_get = *NFS_remotec::pre_op_attr_pre_op_attr_u_attributes_get;
*swig_attributes_set = *NFS_remotec::pre_op_attr_pre_op_attr_u_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_pre_op_attr_pre_op_attr_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_pre_op_attr_pre_op_attr_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::wcc_data ##############

package NFS_remote::wcc_data;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_before_get = *NFS_remotec::wcc_data_before_get;
*swig_before_set = *NFS_remotec::wcc_data_before_set;
*swig_after_get = *NFS_remotec::wcc_data_after_get;
*swig_after_set = *NFS_remotec::wcc_data_after_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_wcc_data(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_wcc_data($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::post_op_fh3 ##############

package NFS_remote::post_op_fh3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_handle_follows_get = *NFS_remotec::post_op_fh3_handle_follows_get;
*swig_handle_follows_set = *NFS_remotec::post_op_fh3_handle_follows_set;
*swig_post_op_fh3_u_get = *NFS_remotec::post_op_fh3_post_op_fh3_u_get;
*swig_post_op_fh3_u_set = *NFS_remotec::post_op_fh3_post_op_fh3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_post_op_fh3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_post_op_fh3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::post_op_fh3_post_op_fh3_u ##############

package NFS_remote::post_op_fh3_post_op_fh3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_handle_get = *NFS_remotec::post_op_fh3_post_op_fh3_u_handle_get;
*swig_handle_set = *NFS_remotec::post_op_fh3_post_op_fh3_u_handle_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_post_op_fh3_post_op_fh3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_post_op_fh3_post_op_fh3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_mode3 ##############

package NFS_remote::set_mode3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_mode3_set_it_get;
*swig_set_it_set = *NFS_remotec::set_mode3_set_it_set;
*swig_set_mode3_u_get = *NFS_remotec::set_mode3_set_mode3_u_get;
*swig_set_mode3_u_set = *NFS_remotec::set_mode3_set_mode3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_mode3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_mode3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_mode3_set_mode3_u ##############

package NFS_remote::set_mode3_set_mode3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mode_get = *NFS_remotec::set_mode3_set_mode3_u_mode_get;
*swig_mode_set = *NFS_remotec::set_mode3_set_mode3_u_mode_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_mode3_set_mode3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_mode3_set_mode3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_uid3 ##############

package NFS_remote::set_uid3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_uid3_set_it_get;
*swig_set_it_set = *NFS_remotec::set_uid3_set_it_set;
*swig_set_uid3_u_get = *NFS_remotec::set_uid3_set_uid3_u_get;
*swig_set_uid3_u_set = *NFS_remotec::set_uid3_set_uid3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_uid3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_uid3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_uid3_set_uid3_u ##############

package NFS_remote::set_uid3_set_uid3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_uid_get = *NFS_remotec::set_uid3_set_uid3_u_uid_get;
*swig_uid_set = *NFS_remotec::set_uid3_set_uid3_u_uid_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_uid3_set_uid3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_uid3_set_uid3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_gid3 ##############

package NFS_remote::set_gid3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_gid3_set_it_get;
*swig_set_it_set = *NFS_remotec::set_gid3_set_it_set;
*swig_set_gid3_u_get = *NFS_remotec::set_gid3_set_gid3_u_get;
*swig_set_gid3_u_set = *NFS_remotec::set_gid3_set_gid3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_gid3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_gid3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_gid3_set_gid3_u ##############

package NFS_remote::set_gid3_set_gid3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_gid_get = *NFS_remotec::set_gid3_set_gid3_u_gid_get;
*swig_gid_set = *NFS_remotec::set_gid3_set_gid3_u_gid_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_gid3_set_gid3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_gid3_set_gid3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_size3 ##############

package NFS_remote::set_size3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_size3_set_it_get;
*swig_set_it_set = *NFS_remotec::set_size3_set_it_set;
*swig_set_size3_u_get = *NFS_remotec::set_size3_set_size3_u_get;
*swig_set_size3_u_set = *NFS_remotec::set_size3_set_size3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_size3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_size3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_size3_set_size3_u ##############

package NFS_remote::set_size3_set_size3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_size_get = *NFS_remotec::set_size3_set_size3_u_size_get;
*swig_size_set = *NFS_remotec::set_size3_set_size3_u_size_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_size3_set_size3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_size3_set_size3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_atime ##############

package NFS_remote::set_atime;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_atime_set_it_get;
*swig_set_it_set = *NFS_remotec::set_atime_set_it_set;
*swig_set_atime_u_get = *NFS_remotec::set_atime_set_atime_u_get;
*swig_set_atime_u_set = *NFS_remotec::set_atime_set_atime_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_atime(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_atime($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_atime_set_atime_u ##############

package NFS_remote::set_atime_set_atime_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_atime_get = *NFS_remotec::set_atime_set_atime_u_atime_get;
*swig_atime_set = *NFS_remotec::set_atime_set_atime_u_atime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_atime_set_atime_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_atime_set_atime_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_mtime ##############

package NFS_remote::set_mtime;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_set_it_get = *NFS_remotec::set_mtime_set_it_get;
*swig_set_it_set = *NFS_remotec::set_mtime_set_it_set;
*swig_set_mtime_u_get = *NFS_remotec::set_mtime_set_mtime_u_get;
*swig_set_mtime_u_set = *NFS_remotec::set_mtime_set_mtime_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_mtime(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_mtime($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::set_mtime_set_mtime_u ##############

package NFS_remote::set_mtime_set_mtime_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mtime_get = *NFS_remotec::set_mtime_set_mtime_u_mtime_get;
*swig_mtime_set = *NFS_remotec::set_mtime_set_mtime_u_mtime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_set_mtime_set_mtime_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_set_mtime_set_mtime_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::sattr3 ##############

package NFS_remote::sattr3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mode_get = *NFS_remotec::sattr3_mode_get;
*swig_mode_set = *NFS_remotec::sattr3_mode_set;
*swig_uid_get = *NFS_remotec::sattr3_uid_get;
*swig_uid_set = *NFS_remotec::sattr3_uid_set;
*swig_gid_get = *NFS_remotec::sattr3_gid_get;
*swig_gid_set = *NFS_remotec::sattr3_gid_set;
*swig_size_get = *NFS_remotec::sattr3_size_get;
*swig_size_set = *NFS_remotec::sattr3_size_set;
*swig_atime_get = *NFS_remotec::sattr3_atime_get;
*swig_atime_set = *NFS_remotec::sattr3_atime_set;
*swig_mtime_get = *NFS_remotec::sattr3_mtime_get;
*swig_mtime_set = *NFS_remotec::sattr3_mtime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_sattr3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_sattr3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::diropargs3 ##############

package NFS_remote::diropargs3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_get = *NFS_remotec::diropargs3_dir_get;
*swig_dir_set = *NFS_remotec::diropargs3_dir_set;
*swig_name_get = *NFS_remotec::diropargs3_name_get;
*swig_name_set = *NFS_remotec::diropargs3_name_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_diropargs3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_diropargs3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::GETATTR3args ##############

package NFS_remote::GETATTR3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::GETATTR3args_object_get;
*swig_object_set = *NFS_remotec::GETATTR3args_object_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_GETATTR3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_GETATTR3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::GETATTR3resok ##############

package NFS_remote::GETATTR3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::GETATTR3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::GETATTR3resok_obj_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_GETATTR3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_GETATTR3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::GETATTR3res ##############

package NFS_remote::GETATTR3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::GETATTR3res_status_get;
*swig_status_set = *NFS_remotec::GETATTR3res_status_set;
*swig_GETATTR3res_u_get = *NFS_remotec::GETATTR3res_GETATTR3res_u_get;
*swig_GETATTR3res_u_set = *NFS_remotec::GETATTR3res_GETATTR3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_GETATTR3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_GETATTR3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::GETATTR3res_GETATTR3res_u ##############

package NFS_remote::GETATTR3res_GETATTR3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::GETATTR3res_GETATTR3res_u_resok_get;
*swig_resok_set = *NFS_remotec::GETATTR3res_GETATTR3res_u_resok_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_GETATTR3res_GETATTR3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_GETATTR3res_GETATTR3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::sattrguard3 ##############

package NFS_remote::sattrguard3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_check_get = *NFS_remotec::sattrguard3_check_get;
*swig_check_set = *NFS_remotec::sattrguard3_check_set;
*swig_sattrguard3_u_get = *NFS_remotec::sattrguard3_sattrguard3_u_get;
*swig_sattrguard3_u_set = *NFS_remotec::sattrguard3_sattrguard3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_sattrguard3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_sattrguard3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::sattrguard3_sattrguard3_u ##############

package NFS_remote::sattrguard3_sattrguard3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_ctime_get = *NFS_remotec::sattrguard3_sattrguard3_u_obj_ctime_get;
*swig_obj_ctime_set = *NFS_remotec::sattrguard3_sattrguard3_u_obj_ctime_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_sattrguard3_sattrguard3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_sattrguard3_sattrguard3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR3args ##############

package NFS_remote::SETATTR3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::SETATTR3args_object_get;
*swig_object_set = *NFS_remotec::SETATTR3args_object_set;
*swig_new_attributes_get = *NFS_remotec::SETATTR3args_new_attributes_get;
*swig_new_attributes_set = *NFS_remotec::SETATTR3args_new_attributes_set;
*swig_guard_get = *NFS_remotec::SETATTR3args_guard_get;
*swig_guard_set = *NFS_remotec::SETATTR3args_guard_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR3resok ##############

package NFS_remote::SETATTR3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_wcc_get = *NFS_remotec::SETATTR3resok_obj_wcc_get;
*swig_obj_wcc_set = *NFS_remotec::SETATTR3resok_obj_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR3resfail ##############

package NFS_remote::SETATTR3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_wcc_get = *NFS_remotec::SETATTR3resfail_obj_wcc_get;
*swig_obj_wcc_set = *NFS_remotec::SETATTR3resfail_obj_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR3res ##############

package NFS_remote::SETATTR3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::SETATTR3res_status_get;
*swig_status_set = *NFS_remotec::SETATTR3res_status_set;
*swig_SETATTR3res_u_get = *NFS_remotec::SETATTR3res_SETATTR3res_u_get;
*swig_SETATTR3res_u_set = *NFS_remotec::SETATTR3res_SETATTR3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SETATTR3res_SETATTR3res_u ##############

package NFS_remote::SETATTR3res_SETATTR3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::SETATTR3res_SETATTR3res_u_resok_get;
*swig_resok_set = *NFS_remotec::SETATTR3res_SETATTR3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::SETATTR3res_SETATTR3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::SETATTR3res_SETATTR3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SETATTR3res_SETATTR3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SETATTR3res_SETATTR3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LOOKUP3args ##############

package NFS_remote::LOOKUP3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_what_get = *NFS_remotec::LOOKUP3args_what_get;
*swig_what_set = *NFS_remotec::LOOKUP3args_what_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LOOKUP3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LOOKUP3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LOOKUP3resok ##############

package NFS_remote::LOOKUP3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::LOOKUP3resok_object_get;
*swig_object_set = *NFS_remotec::LOOKUP3resok_object_set;
*swig_obj_attributes_get = *NFS_remotec::LOOKUP3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::LOOKUP3resok_obj_attributes_set;
*swig_dir_attributes_get = *NFS_remotec::LOOKUP3resok_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::LOOKUP3resok_dir_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LOOKUP3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LOOKUP3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LOOKUP3resfail ##############

package NFS_remote::LOOKUP3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_attributes_get = *NFS_remotec::LOOKUP3resfail_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::LOOKUP3resfail_dir_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LOOKUP3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LOOKUP3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LOOKUP3res ##############

package NFS_remote::LOOKUP3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::LOOKUP3res_status_get;
*swig_status_set = *NFS_remotec::LOOKUP3res_status_set;
*swig_LOOKUP3res_u_get = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_get;
*swig_LOOKUP3res_u_set = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LOOKUP3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LOOKUP3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LOOKUP3res_LOOKUP3res_u ##############

package NFS_remote::LOOKUP3res_LOOKUP3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_resok_get;
*swig_resok_set = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::LOOKUP3res_LOOKUP3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LOOKUP3res_LOOKUP3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LOOKUP3res_LOOKUP3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ACCESS3args ##############

package NFS_remote::ACCESS3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::ACCESS3args_object_get;
*swig_object_set = *NFS_remotec::ACCESS3args_object_set;
*swig_access_get = *NFS_remotec::ACCESS3args_access_get;
*swig_access_set = *NFS_remotec::ACCESS3args_access_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ACCESS3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ACCESS3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ACCESS3resok ##############

package NFS_remote::ACCESS3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::ACCESS3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::ACCESS3resok_obj_attributes_set;
*swig_access_get = *NFS_remotec::ACCESS3resok_access_get;
*swig_access_set = *NFS_remotec::ACCESS3resok_access_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ACCESS3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ACCESS3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ACCESS3resfail ##############

package NFS_remote::ACCESS3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::ACCESS3resfail_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::ACCESS3resfail_obj_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ACCESS3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ACCESS3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ACCESS3res ##############

package NFS_remote::ACCESS3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::ACCESS3res_status_get;
*swig_status_set = *NFS_remotec::ACCESS3res_status_set;
*swig_ACCESS3res_u_get = *NFS_remotec::ACCESS3res_ACCESS3res_u_get;
*swig_ACCESS3res_u_set = *NFS_remotec::ACCESS3res_ACCESS3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ACCESS3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ACCESS3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::ACCESS3res_ACCESS3res_u ##############

package NFS_remote::ACCESS3res_ACCESS3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::ACCESS3res_ACCESS3res_u_resok_get;
*swig_resok_set = *NFS_remotec::ACCESS3res_ACCESS3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::ACCESS3res_ACCESS3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::ACCESS3res_ACCESS3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_ACCESS3res_ACCESS3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_ACCESS3res_ACCESS3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK3args ##############

package NFS_remote::READLINK3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_symlink_get = *NFS_remotec::READLINK3args_symlink_get;
*swig_symlink_set = *NFS_remotec::READLINK3args_symlink_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK3resok ##############

package NFS_remote::READLINK3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_symlink_attributes_get = *NFS_remotec::READLINK3resok_symlink_attributes_get;
*swig_symlink_attributes_set = *NFS_remotec::READLINK3resok_symlink_attributes_set;
*swig_data_get = *NFS_remotec::READLINK3resok_data_get;
*swig_data_set = *NFS_remotec::READLINK3resok_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK3resfail ##############

package NFS_remote::READLINK3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_symlink_attributes_get = *NFS_remotec::READLINK3resfail_symlink_attributes_get;
*swig_symlink_attributes_set = *NFS_remotec::READLINK3resfail_symlink_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK3res ##############

package NFS_remote::READLINK3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READLINK3res_status_get;
*swig_status_set = *NFS_remotec::READLINK3res_status_set;
*swig_READLINK3res_u_get = *NFS_remotec::READLINK3res_READLINK3res_u_get;
*swig_READLINK3res_u_set = *NFS_remotec::READLINK3res_READLINK3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READLINK3res_READLINK3res_u ##############

package NFS_remote::READLINK3res_READLINK3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::READLINK3res_READLINK3res_u_resok_get;
*swig_resok_set = *NFS_remotec::READLINK3res_READLINK3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::READLINK3res_READLINK3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::READLINK3res_READLINK3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READLINK3res_READLINK3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READLINK3res_READLINK3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3args ##############

package NFS_remote::READ3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::READ3args_file_get;
*swig_file_set = *NFS_remotec::READ3args_file_set;
*swig_offset_get = *NFS_remotec::READ3args_offset_get;
*swig_offset_set = *NFS_remotec::READ3args_offset_set;
*swig_count_get = *NFS_remotec::READ3args_count_get;
*swig_count_set = *NFS_remotec::READ3args_count_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3resok ##############

package NFS_remote::READ3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_attributes_get = *NFS_remotec::READ3resok_file_attributes_get;
*swig_file_attributes_set = *NFS_remotec::READ3resok_file_attributes_set;
*swig_count_get = *NFS_remotec::READ3resok_count_get;
*swig_count_set = *NFS_remotec::READ3resok_count_set;
*swig_eof_get = *NFS_remotec::READ3resok_eof_get;
*swig_eof_set = *NFS_remotec::READ3resok_eof_set;
*swig_data_get = *NFS_remotec::READ3resok_data_get;
*swig_data_set = *NFS_remotec::READ3resok_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3resok_data ##############

package NFS_remote::READ3resok_data;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_len_get = *NFS_remotec::READ3resok_data_data_len_get;
*swig_data_len_set = *NFS_remotec::READ3resok_data_data_len_set;
*swig_data_val_get = *NFS_remotec::READ3resok_data_data_val_get;
*swig_data_val_set = *NFS_remotec::READ3resok_data_data_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3resok_data(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3resok_data($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3resfail ##############

package NFS_remote::READ3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_attributes_get = *NFS_remotec::READ3resfail_file_attributes_get;
*swig_file_attributes_set = *NFS_remotec::READ3resfail_file_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3res ##############

package NFS_remote::READ3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READ3res_status_get;
*swig_status_set = *NFS_remotec::READ3res_status_set;
*swig_READ3res_u_get = *NFS_remotec::READ3res_READ3res_u_get;
*swig_READ3res_u_set = *NFS_remotec::READ3res_READ3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READ3res_READ3res_u ##############

package NFS_remote::READ3res_READ3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::READ3res_READ3res_u_resok_get;
*swig_resok_set = *NFS_remotec::READ3res_READ3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::READ3res_READ3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::READ3res_READ3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READ3res_READ3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READ3res_READ3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3args ##############

package NFS_remote::WRITE3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::WRITE3args_file_get;
*swig_file_set = *NFS_remotec::WRITE3args_file_set;
*swig_offset_get = *NFS_remotec::WRITE3args_offset_get;
*swig_offset_set = *NFS_remotec::WRITE3args_offset_set;
*swig_count_get = *NFS_remotec::WRITE3args_count_get;
*swig_count_set = *NFS_remotec::WRITE3args_count_set;
*swig_stable_get = *NFS_remotec::WRITE3args_stable_get;
*swig_stable_set = *NFS_remotec::WRITE3args_stable_set;
*swig_data_get = *NFS_remotec::WRITE3args_data_get;
*swig_data_set = *NFS_remotec::WRITE3args_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3args_data ##############

package NFS_remote::WRITE3args_data;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_data_len_get = *NFS_remotec::WRITE3args_data_data_len_get;
*swig_data_len_set = *NFS_remotec::WRITE3args_data_data_len_set;
*swig_data_val_get = *NFS_remotec::WRITE3args_data_data_val_get;
*swig_data_val_set = *NFS_remotec::WRITE3args_data_data_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3args_data(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3args_data($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3resok ##############

package NFS_remote::WRITE3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_wcc_get = *NFS_remotec::WRITE3resok_file_wcc_get;
*swig_file_wcc_set = *NFS_remotec::WRITE3resok_file_wcc_set;
*swig_count_get = *NFS_remotec::WRITE3resok_count_get;
*swig_count_set = *NFS_remotec::WRITE3resok_count_set;
*swig_committed_get = *NFS_remotec::WRITE3resok_committed_get;
*swig_committed_set = *NFS_remotec::WRITE3resok_committed_set;
*swig_verf_get = *NFS_remotec::WRITE3resok_verf_get;
*swig_verf_set = *NFS_remotec::WRITE3resok_verf_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3resfail ##############

package NFS_remote::WRITE3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_wcc_get = *NFS_remotec::WRITE3resfail_file_wcc_get;
*swig_file_wcc_set = *NFS_remotec::WRITE3resfail_file_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3res ##############

package NFS_remote::WRITE3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::WRITE3res_status_get;
*swig_status_set = *NFS_remotec::WRITE3res_status_set;
*swig_WRITE3res_u_get = *NFS_remotec::WRITE3res_WRITE3res_u_get;
*swig_WRITE3res_u_set = *NFS_remotec::WRITE3res_WRITE3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::WRITE3res_WRITE3res_u ##############

package NFS_remote::WRITE3res_WRITE3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::WRITE3res_WRITE3res_u_resok_get;
*swig_resok_set = *NFS_remotec::WRITE3res_WRITE3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::WRITE3res_WRITE3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::WRITE3res_WRITE3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_WRITE3res_WRITE3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_WRITE3res_WRITE3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::createhow3 ##############

package NFS_remote::createhow3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mode_get = *NFS_remotec::createhow3_mode_get;
*swig_mode_set = *NFS_remotec::createhow3_mode_set;
*swig_createhow3_u_get = *NFS_remotec::createhow3_createhow3_u_get;
*swig_createhow3_u_set = *NFS_remotec::createhow3_createhow3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_createhow3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_createhow3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::createhow3_createhow3_u ##############

package NFS_remote::createhow3_createhow3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::createhow3_createhow3_u_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::createhow3_createhow3_u_obj_attributes_set;
*swig_verf_get = *NFS_remotec::createhow3_createhow3_u_verf_get;
*swig_verf_set = *NFS_remotec::createhow3_createhow3_u_verf_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_createhow3_createhow3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_createhow3_createhow3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE3args ##############

package NFS_remote::CREATE3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_where_get = *NFS_remotec::CREATE3args_where_get;
*swig_where_set = *NFS_remotec::CREATE3args_where_set;
*swig_how_get = *NFS_remotec::CREATE3args_how_get;
*swig_how_set = *NFS_remotec::CREATE3args_how_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE3resok ##############

package NFS_remote::CREATE3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_get = *NFS_remotec::CREATE3resok_obj_get;
*swig_obj_set = *NFS_remotec::CREATE3resok_obj_set;
*swig_obj_attributes_get = *NFS_remotec::CREATE3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::CREATE3resok_obj_attributes_set;
*swig_dir_wcc_get = *NFS_remotec::CREATE3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::CREATE3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE3resfail ##############

package NFS_remote::CREATE3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::CREATE3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::CREATE3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE3res ##############

package NFS_remote::CREATE3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::CREATE3res_status_get;
*swig_status_set = *NFS_remotec::CREATE3res_status_set;
*swig_CREATE3res_u_get = *NFS_remotec::CREATE3res_CREATE3res_u_get;
*swig_CREATE3res_u_set = *NFS_remotec::CREATE3res_CREATE3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::CREATE3res_CREATE3res_u ##############

package NFS_remote::CREATE3res_CREATE3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::CREATE3res_CREATE3res_u_resok_get;
*swig_resok_set = *NFS_remotec::CREATE3res_CREATE3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::CREATE3res_CREATE3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::CREATE3res_CREATE3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_CREATE3res_CREATE3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_CREATE3res_CREATE3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKDIR3args ##############

package NFS_remote::MKDIR3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_where_get = *NFS_remotec::MKDIR3args_where_get;
*swig_where_set = *NFS_remotec::MKDIR3args_where_set;
*swig_attributes_get = *NFS_remotec::MKDIR3args_attributes_get;
*swig_attributes_set = *NFS_remotec::MKDIR3args_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKDIR3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKDIR3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKDIR3resok ##############

package NFS_remote::MKDIR3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_get = *NFS_remotec::MKDIR3resok_obj_get;
*swig_obj_set = *NFS_remotec::MKDIR3resok_obj_set;
*swig_obj_attributes_get = *NFS_remotec::MKDIR3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::MKDIR3resok_obj_attributes_set;
*swig_dir_wcc_get = *NFS_remotec::MKDIR3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::MKDIR3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKDIR3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKDIR3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKDIR3resfail ##############

package NFS_remote::MKDIR3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::MKDIR3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::MKDIR3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKDIR3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKDIR3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKDIR3res ##############

package NFS_remote::MKDIR3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::MKDIR3res_status_get;
*swig_status_set = *NFS_remotec::MKDIR3res_status_set;
*swig_MKDIR3res_u_get = *NFS_remotec::MKDIR3res_MKDIR3res_u_get;
*swig_MKDIR3res_u_set = *NFS_remotec::MKDIR3res_MKDIR3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKDIR3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKDIR3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKDIR3res_MKDIR3res_u ##############

package NFS_remote::MKDIR3res_MKDIR3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::MKDIR3res_MKDIR3res_u_resok_get;
*swig_resok_set = *NFS_remotec::MKDIR3res_MKDIR3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::MKDIR3res_MKDIR3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::MKDIR3res_MKDIR3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKDIR3res_MKDIR3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKDIR3res_MKDIR3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::symlinkdata3 ##############

package NFS_remote::symlinkdata3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_symlink_attributes_get = *NFS_remotec::symlinkdata3_symlink_attributes_get;
*swig_symlink_attributes_set = *NFS_remotec::symlinkdata3_symlink_attributes_set;
*swig_symlink_data_get = *NFS_remotec::symlinkdata3_symlink_data_get;
*swig_symlink_data_set = *NFS_remotec::symlinkdata3_symlink_data_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_symlinkdata3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_symlinkdata3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK3args ##############

package NFS_remote::SYMLINK3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_where_get = *NFS_remotec::SYMLINK3args_where_get;
*swig_where_set = *NFS_remotec::SYMLINK3args_where_set;
*swig_symlink_get = *NFS_remotec::SYMLINK3args_symlink_get;
*swig_symlink_set = *NFS_remotec::SYMLINK3args_symlink_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK3resok ##############

package NFS_remote::SYMLINK3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_get = *NFS_remotec::SYMLINK3resok_obj_get;
*swig_obj_set = *NFS_remotec::SYMLINK3resok_obj_set;
*swig_obj_attributes_get = *NFS_remotec::SYMLINK3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::SYMLINK3resok_obj_attributes_set;
*swig_dir_wcc_get = *NFS_remotec::SYMLINK3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::SYMLINK3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK3resfail ##############

package NFS_remote::SYMLINK3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::SYMLINK3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::SYMLINK3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK3res ##############

package NFS_remote::SYMLINK3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::SYMLINK3res_status_get;
*swig_status_set = *NFS_remotec::SYMLINK3res_status_set;
*swig_SYMLINK3res_u_get = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_get;
*swig_SYMLINK3res_u_set = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::SYMLINK3res_SYMLINK3res_u ##############

package NFS_remote::SYMLINK3res_SYMLINK3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_resok_get;
*swig_resok_set = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::SYMLINK3res_SYMLINK3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_SYMLINK3res_SYMLINK3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_SYMLINK3res_SYMLINK3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::devicedata3 ##############

package NFS_remote::devicedata3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dev_attributes_get = *NFS_remotec::devicedata3_dev_attributes_get;
*swig_dev_attributes_set = *NFS_remotec::devicedata3_dev_attributes_set;
*swig_spec_get = *NFS_remotec::devicedata3_spec_get;
*swig_spec_set = *NFS_remotec::devicedata3_spec_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_devicedata3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_devicedata3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mknoddata3 ##############

package NFS_remote::mknoddata3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_type_get = *NFS_remotec::mknoddata3_type_get;
*swig_type_set = *NFS_remotec::mknoddata3_type_set;
*swig_mknoddata3_u_get = *NFS_remotec::mknoddata3_mknoddata3_u_get;
*swig_mknoddata3_u_set = *NFS_remotec::mknoddata3_mknoddata3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mknoddata3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mknoddata3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mknoddata3_mknoddata3_u ##############

package NFS_remote::mknoddata3_mknoddata3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_device_get = *NFS_remotec::mknoddata3_mknoddata3_u_device_get;
*swig_device_set = *NFS_remotec::mknoddata3_mknoddata3_u_device_set;
*swig_pipe_attributes_get = *NFS_remotec::mknoddata3_mknoddata3_u_pipe_attributes_get;
*swig_pipe_attributes_set = *NFS_remotec::mknoddata3_mknoddata3_u_pipe_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mknoddata3_mknoddata3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mknoddata3_mknoddata3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKNOD3args ##############

package NFS_remote::MKNOD3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_where_get = *NFS_remotec::MKNOD3args_where_get;
*swig_where_set = *NFS_remotec::MKNOD3args_where_set;
*swig_what_get = *NFS_remotec::MKNOD3args_what_get;
*swig_what_set = *NFS_remotec::MKNOD3args_what_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKNOD3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKNOD3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKNOD3resok ##############

package NFS_remote::MKNOD3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_get = *NFS_remotec::MKNOD3resok_obj_get;
*swig_obj_set = *NFS_remotec::MKNOD3resok_obj_set;
*swig_obj_attributes_get = *NFS_remotec::MKNOD3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::MKNOD3resok_obj_attributes_set;
*swig_dir_wcc_get = *NFS_remotec::MKNOD3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::MKNOD3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKNOD3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKNOD3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKNOD3resfail ##############

package NFS_remote::MKNOD3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::MKNOD3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::MKNOD3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKNOD3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKNOD3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKNOD3res ##############

package NFS_remote::MKNOD3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::MKNOD3res_status_get;
*swig_status_set = *NFS_remotec::MKNOD3res_status_set;
*swig_MKNOD3res_u_get = *NFS_remotec::MKNOD3res_MKNOD3res_u_get;
*swig_MKNOD3res_u_set = *NFS_remotec::MKNOD3res_MKNOD3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKNOD3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKNOD3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::MKNOD3res_MKNOD3res_u ##############

package NFS_remote::MKNOD3res_MKNOD3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::MKNOD3res_MKNOD3res_u_resok_get;
*swig_resok_set = *NFS_remotec::MKNOD3res_MKNOD3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::MKNOD3res_MKNOD3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::MKNOD3res_MKNOD3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_MKNOD3res_MKNOD3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_MKNOD3res_MKNOD3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::REMOVE3args ##############

package NFS_remote::REMOVE3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::REMOVE3args_object_get;
*swig_object_set = *NFS_remotec::REMOVE3args_object_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_REMOVE3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_REMOVE3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::REMOVE3resok ##############

package NFS_remote::REMOVE3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::REMOVE3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::REMOVE3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_REMOVE3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_REMOVE3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::REMOVE3resfail ##############

package NFS_remote::REMOVE3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::REMOVE3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::REMOVE3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_REMOVE3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_REMOVE3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::REMOVE3res ##############

package NFS_remote::REMOVE3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::REMOVE3res_status_get;
*swig_status_set = *NFS_remotec::REMOVE3res_status_set;
*swig_REMOVE3res_u_get = *NFS_remotec::REMOVE3res_REMOVE3res_u_get;
*swig_REMOVE3res_u_set = *NFS_remotec::REMOVE3res_REMOVE3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_REMOVE3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_REMOVE3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::REMOVE3res_REMOVE3res_u ##############

package NFS_remote::REMOVE3res_REMOVE3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::REMOVE3res_REMOVE3res_u_resok_get;
*swig_resok_set = *NFS_remotec::REMOVE3res_REMOVE3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::REMOVE3res_REMOVE3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::REMOVE3res_REMOVE3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_REMOVE3res_REMOVE3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_REMOVE3res_REMOVE3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RMDIR3args ##############

package NFS_remote::RMDIR3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::RMDIR3args_object_get;
*swig_object_set = *NFS_remotec::RMDIR3args_object_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RMDIR3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RMDIR3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RMDIR3resok ##############

package NFS_remote::RMDIR3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::RMDIR3resok_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::RMDIR3resok_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RMDIR3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RMDIR3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RMDIR3resfail ##############

package NFS_remote::RMDIR3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_wcc_get = *NFS_remotec::RMDIR3resfail_dir_wcc_get;
*swig_dir_wcc_set = *NFS_remotec::RMDIR3resfail_dir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RMDIR3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RMDIR3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RMDIR3res ##############

package NFS_remote::RMDIR3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::RMDIR3res_status_get;
*swig_status_set = *NFS_remotec::RMDIR3res_status_set;
*swig_RMDIR3res_u_get = *NFS_remotec::RMDIR3res_RMDIR3res_u_get;
*swig_RMDIR3res_u_set = *NFS_remotec::RMDIR3res_RMDIR3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RMDIR3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RMDIR3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RMDIR3res_RMDIR3res_u ##############

package NFS_remote::RMDIR3res_RMDIR3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::RMDIR3res_RMDIR3res_u_resok_get;
*swig_resok_set = *NFS_remotec::RMDIR3res_RMDIR3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::RMDIR3res_RMDIR3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::RMDIR3res_RMDIR3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RMDIR3res_RMDIR3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RMDIR3res_RMDIR3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME3args ##############

package NFS_remote::RENAME3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_from_get = *NFS_remotec::RENAME3args_from_get;
*swig_from_set = *NFS_remotec::RENAME3args_from_set;
*swig_to_get = *NFS_remotec::RENAME3args_to_get;
*swig_to_set = *NFS_remotec::RENAME3args_to_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME3resok ##############

package NFS_remote::RENAME3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fromdir_wcc_get = *NFS_remotec::RENAME3resok_fromdir_wcc_get;
*swig_fromdir_wcc_set = *NFS_remotec::RENAME3resok_fromdir_wcc_set;
*swig_todir_wcc_get = *NFS_remotec::RENAME3resok_todir_wcc_get;
*swig_todir_wcc_set = *NFS_remotec::RENAME3resok_todir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME3resfail ##############

package NFS_remote::RENAME3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fromdir_wcc_get = *NFS_remotec::RENAME3resfail_fromdir_wcc_get;
*swig_fromdir_wcc_set = *NFS_remotec::RENAME3resfail_fromdir_wcc_set;
*swig_todir_wcc_get = *NFS_remotec::RENAME3resfail_todir_wcc_get;
*swig_todir_wcc_set = *NFS_remotec::RENAME3resfail_todir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME3res ##############

package NFS_remote::RENAME3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::RENAME3res_status_get;
*swig_status_set = *NFS_remotec::RENAME3res_status_set;
*swig_RENAME3res_u_get = *NFS_remotec::RENAME3res_RENAME3res_u_get;
*swig_RENAME3res_u_set = *NFS_remotec::RENAME3res_RENAME3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::RENAME3res_RENAME3res_u ##############

package NFS_remote::RENAME3res_RENAME3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::RENAME3res_RENAME3res_u_resok_get;
*swig_resok_set = *NFS_remotec::RENAME3res_RENAME3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::RENAME3res_RENAME3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::RENAME3res_RENAME3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_RENAME3res_RENAME3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_RENAME3res_RENAME3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK3args ##############

package NFS_remote::LINK3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::LINK3args_file_get;
*swig_file_set = *NFS_remotec::LINK3args_file_set;
*swig_link_get = *NFS_remotec::LINK3args_link_get;
*swig_link_set = *NFS_remotec::LINK3args_link_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK3resok ##############

package NFS_remote::LINK3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_attributes_get = *NFS_remotec::LINK3resok_file_attributes_get;
*swig_file_attributes_set = *NFS_remotec::LINK3resok_file_attributes_set;
*swig_linkdir_wcc_get = *NFS_remotec::LINK3resok_linkdir_wcc_get;
*swig_linkdir_wcc_set = *NFS_remotec::LINK3resok_linkdir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK3resfail ##############

package NFS_remote::LINK3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_attributes_get = *NFS_remotec::LINK3resfail_file_attributes_get;
*swig_file_attributes_set = *NFS_remotec::LINK3resfail_file_attributes_set;
*swig_linkdir_wcc_get = *NFS_remotec::LINK3resfail_linkdir_wcc_get;
*swig_linkdir_wcc_set = *NFS_remotec::LINK3resfail_linkdir_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK3res ##############

package NFS_remote::LINK3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::LINK3res_status_get;
*swig_status_set = *NFS_remotec::LINK3res_status_set;
*swig_LINK3res_u_get = *NFS_remotec::LINK3res_LINK3res_u_get;
*swig_LINK3res_u_set = *NFS_remotec::LINK3res_LINK3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::LINK3res_LINK3res_u ##############

package NFS_remote::LINK3res_LINK3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::LINK3res_LINK3res_u_resok_get;
*swig_resok_set = *NFS_remotec::LINK3res_LINK3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::LINK3res_LINK3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::LINK3res_LINK3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_LINK3res_LINK3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_LINK3res_LINK3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR3args ##############

package NFS_remote::READDIR3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_get = *NFS_remotec::READDIR3args_dir_get;
*swig_dir_set = *NFS_remotec::READDIR3args_dir_set;
*swig_cookie_get = *NFS_remotec::READDIR3args_cookie_get;
*swig_cookie_set = *NFS_remotec::READDIR3args_cookie_set;
*swig_cookieverf_get = *NFS_remotec::READDIR3args_cookieverf_get;
*swig_cookieverf_set = *NFS_remotec::READDIR3args_cookieverf_set;
*swig_count_get = *NFS_remotec::READDIR3args_count_get;
*swig_count_set = *NFS_remotec::READDIR3args_count_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::entry3 ##############

package NFS_remote::entry3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fileid_get = *NFS_remotec::entry3_fileid_get;
*swig_fileid_set = *NFS_remotec::entry3_fileid_set;
*swig_name_get = *NFS_remotec::entry3_name_get;
*swig_name_set = *NFS_remotec::entry3_name_set;
*swig_cookie_get = *NFS_remotec::entry3_cookie_get;
*swig_cookie_set = *NFS_remotec::entry3_cookie_set;
*swig_nextentry_get = *NFS_remotec::entry3_nextentry_get;
*swig_nextentry_set = *NFS_remotec::entry3_nextentry_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_entry3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_entry3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::dirlist3 ##############

package NFS_remote::dirlist3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_entries_get = *NFS_remotec::dirlist3_entries_get;
*swig_entries_set = *NFS_remotec::dirlist3_entries_set;
*swig_eof_get = *NFS_remotec::dirlist3_eof_get;
*swig_eof_set = *NFS_remotec::dirlist3_eof_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_dirlist3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_dirlist3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR3resok ##############

package NFS_remote::READDIR3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_attributes_get = *NFS_remotec::READDIR3resok_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::READDIR3resok_dir_attributes_set;
*swig_cookieverf_get = *NFS_remotec::READDIR3resok_cookieverf_get;
*swig_cookieverf_set = *NFS_remotec::READDIR3resok_cookieverf_set;
*swig_reply_get = *NFS_remotec::READDIR3resok_reply_get;
*swig_reply_set = *NFS_remotec::READDIR3resok_reply_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR3resfail ##############

package NFS_remote::READDIR3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_attributes_get = *NFS_remotec::READDIR3resfail_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::READDIR3resfail_dir_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR3res ##############

package NFS_remote::READDIR3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READDIR3res_status_get;
*swig_status_set = *NFS_remotec::READDIR3res_status_set;
*swig_READDIR3res_u_get = *NFS_remotec::READDIR3res_READDIR3res_u_get;
*swig_READDIR3res_u_set = *NFS_remotec::READDIR3res_READDIR3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIR3res_READDIR3res_u ##############

package NFS_remote::READDIR3res_READDIR3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::READDIR3res_READDIR3res_u_resok_get;
*swig_resok_set = *NFS_remotec::READDIR3res_READDIR3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::READDIR3res_READDIR3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::READDIR3res_READDIR3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIR3res_READDIR3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIR3res_READDIR3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIRPLUS3args ##############

package NFS_remote::READDIRPLUS3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_get = *NFS_remotec::READDIRPLUS3args_dir_get;
*swig_dir_set = *NFS_remotec::READDIRPLUS3args_dir_set;
*swig_cookie_get = *NFS_remotec::READDIRPLUS3args_cookie_get;
*swig_cookie_set = *NFS_remotec::READDIRPLUS3args_cookie_set;
*swig_cookieverf_get = *NFS_remotec::READDIRPLUS3args_cookieverf_get;
*swig_cookieverf_set = *NFS_remotec::READDIRPLUS3args_cookieverf_set;
*swig_dircount_get = *NFS_remotec::READDIRPLUS3args_dircount_get;
*swig_dircount_set = *NFS_remotec::READDIRPLUS3args_dircount_set;
*swig_maxcount_get = *NFS_remotec::READDIRPLUS3args_maxcount_get;
*swig_maxcount_set = *NFS_remotec::READDIRPLUS3args_maxcount_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIRPLUS3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIRPLUS3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::entryplus3 ##############

package NFS_remote::entryplus3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fileid_get = *NFS_remotec::entryplus3_fileid_get;
*swig_fileid_set = *NFS_remotec::entryplus3_fileid_set;
*swig_name_get = *NFS_remotec::entryplus3_name_get;
*swig_name_set = *NFS_remotec::entryplus3_name_set;
*swig_cookie_get = *NFS_remotec::entryplus3_cookie_get;
*swig_cookie_set = *NFS_remotec::entryplus3_cookie_set;
*swig_name_attributes_get = *NFS_remotec::entryplus3_name_attributes_get;
*swig_name_attributes_set = *NFS_remotec::entryplus3_name_attributes_set;
*swig_name_handle_get = *NFS_remotec::entryplus3_name_handle_get;
*swig_name_handle_set = *NFS_remotec::entryplus3_name_handle_set;
*swig_nextentry_get = *NFS_remotec::entryplus3_nextentry_get;
*swig_nextentry_set = *NFS_remotec::entryplus3_nextentry_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_entryplus3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_entryplus3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::dirlistplus3 ##############

package NFS_remote::dirlistplus3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_entries_get = *NFS_remotec::dirlistplus3_entries_get;
*swig_entries_set = *NFS_remotec::dirlistplus3_entries_set;
*swig_eof_get = *NFS_remotec::dirlistplus3_eof_get;
*swig_eof_set = *NFS_remotec::dirlistplus3_eof_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_dirlistplus3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_dirlistplus3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIRPLUS3resok ##############

package NFS_remote::READDIRPLUS3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_attributes_get = *NFS_remotec::READDIRPLUS3resok_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::READDIRPLUS3resok_dir_attributes_set;
*swig_cookieverf_get = *NFS_remotec::READDIRPLUS3resok_cookieverf_get;
*swig_cookieverf_set = *NFS_remotec::READDIRPLUS3resok_cookieverf_set;
*swig_reply_get = *NFS_remotec::READDIRPLUS3resok_reply_get;
*swig_reply_set = *NFS_remotec::READDIRPLUS3resok_reply_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIRPLUS3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIRPLUS3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIRPLUS3resfail ##############

package NFS_remote::READDIRPLUS3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_dir_attributes_get = *NFS_remotec::READDIRPLUS3resfail_dir_attributes_get;
*swig_dir_attributes_set = *NFS_remotec::READDIRPLUS3resfail_dir_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIRPLUS3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIRPLUS3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIRPLUS3res ##############

package NFS_remote::READDIRPLUS3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::READDIRPLUS3res_status_get;
*swig_status_set = *NFS_remotec::READDIRPLUS3res_status_set;
*swig_READDIRPLUS3res_u_get = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_get;
*swig_READDIRPLUS3res_u_set = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIRPLUS3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIRPLUS3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::READDIRPLUS3res_READDIRPLUS3res_u ##############

package NFS_remote::READDIRPLUS3res_READDIRPLUS3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_resok_get;
*swig_resok_set = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::READDIRPLUS3res_READDIRPLUS3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_READDIRPLUS3res_READDIRPLUS3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_READDIRPLUS3res_READDIRPLUS3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSSTAT3args ##############

package NFS_remote::FSSTAT3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fsroot_get = *NFS_remotec::FSSTAT3args_fsroot_get;
*swig_fsroot_set = *NFS_remotec::FSSTAT3args_fsroot_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSSTAT3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSSTAT3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSSTAT3resok ##############

package NFS_remote::FSSTAT3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::FSSTAT3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::FSSTAT3resok_obj_attributes_set;
*swig_tbytes_get = *NFS_remotec::FSSTAT3resok_tbytes_get;
*swig_tbytes_set = *NFS_remotec::FSSTAT3resok_tbytes_set;
*swig_fbytes_get = *NFS_remotec::FSSTAT3resok_fbytes_get;
*swig_fbytes_set = *NFS_remotec::FSSTAT3resok_fbytes_set;
*swig_abytes_get = *NFS_remotec::FSSTAT3resok_abytes_get;
*swig_abytes_set = *NFS_remotec::FSSTAT3resok_abytes_set;
*swig_tfiles_get = *NFS_remotec::FSSTAT3resok_tfiles_get;
*swig_tfiles_set = *NFS_remotec::FSSTAT3resok_tfiles_set;
*swig_ffiles_get = *NFS_remotec::FSSTAT3resok_ffiles_get;
*swig_ffiles_set = *NFS_remotec::FSSTAT3resok_ffiles_set;
*swig_afiles_get = *NFS_remotec::FSSTAT3resok_afiles_get;
*swig_afiles_set = *NFS_remotec::FSSTAT3resok_afiles_set;
*swig_invarsec_get = *NFS_remotec::FSSTAT3resok_invarsec_get;
*swig_invarsec_set = *NFS_remotec::FSSTAT3resok_invarsec_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSSTAT3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSSTAT3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSSTAT3resfail ##############

package NFS_remote::FSSTAT3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::FSSTAT3resfail_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::FSSTAT3resfail_obj_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSSTAT3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSSTAT3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSSTAT3res ##############

package NFS_remote::FSSTAT3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::FSSTAT3res_status_get;
*swig_status_set = *NFS_remotec::FSSTAT3res_status_set;
*swig_FSSTAT3res_u_get = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_get;
*swig_FSSTAT3res_u_set = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSSTAT3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSSTAT3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSSTAT3res_FSSTAT3res_u ##############

package NFS_remote::FSSTAT3res_FSSTAT3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_resok_get;
*swig_resok_set = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::FSSTAT3res_FSSTAT3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSSTAT3res_FSSTAT3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSSTAT3res_FSSTAT3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSINFO3args ##############

package NFS_remote::FSINFO3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fsroot_get = *NFS_remotec::FSINFO3args_fsroot_get;
*swig_fsroot_set = *NFS_remotec::FSINFO3args_fsroot_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSINFO3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSINFO3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSINFO3resok ##############

package NFS_remote::FSINFO3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::FSINFO3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::FSINFO3resok_obj_attributes_set;
*swig_rtmax_get = *NFS_remotec::FSINFO3resok_rtmax_get;
*swig_rtmax_set = *NFS_remotec::FSINFO3resok_rtmax_set;
*swig_rtpref_get = *NFS_remotec::FSINFO3resok_rtpref_get;
*swig_rtpref_set = *NFS_remotec::FSINFO3resok_rtpref_set;
*swig_rtmult_get = *NFS_remotec::FSINFO3resok_rtmult_get;
*swig_rtmult_set = *NFS_remotec::FSINFO3resok_rtmult_set;
*swig_wtmax_get = *NFS_remotec::FSINFO3resok_wtmax_get;
*swig_wtmax_set = *NFS_remotec::FSINFO3resok_wtmax_set;
*swig_wtpref_get = *NFS_remotec::FSINFO3resok_wtpref_get;
*swig_wtpref_set = *NFS_remotec::FSINFO3resok_wtpref_set;
*swig_wtmult_get = *NFS_remotec::FSINFO3resok_wtmult_get;
*swig_wtmult_set = *NFS_remotec::FSINFO3resok_wtmult_set;
*swig_dtpref_get = *NFS_remotec::FSINFO3resok_dtpref_get;
*swig_dtpref_set = *NFS_remotec::FSINFO3resok_dtpref_set;
*swig_maxfilesize_get = *NFS_remotec::FSINFO3resok_maxfilesize_get;
*swig_maxfilesize_set = *NFS_remotec::FSINFO3resok_maxfilesize_set;
*swig_time_delta_get = *NFS_remotec::FSINFO3resok_time_delta_get;
*swig_time_delta_set = *NFS_remotec::FSINFO3resok_time_delta_set;
*swig_properties_get = *NFS_remotec::FSINFO3resok_properties_get;
*swig_properties_set = *NFS_remotec::FSINFO3resok_properties_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSINFO3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSINFO3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSINFO3resfail ##############

package NFS_remote::FSINFO3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::FSINFO3resfail_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::FSINFO3resfail_obj_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSINFO3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSINFO3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSINFO3res ##############

package NFS_remote::FSINFO3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::FSINFO3res_status_get;
*swig_status_set = *NFS_remotec::FSINFO3res_status_set;
*swig_FSINFO3res_u_get = *NFS_remotec::FSINFO3res_FSINFO3res_u_get;
*swig_FSINFO3res_u_set = *NFS_remotec::FSINFO3res_FSINFO3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSINFO3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSINFO3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::FSINFO3res_FSINFO3res_u ##############

package NFS_remote::FSINFO3res_FSINFO3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::FSINFO3res_FSINFO3res_u_resok_get;
*swig_resok_set = *NFS_remotec::FSINFO3res_FSINFO3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::FSINFO3res_FSINFO3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::FSINFO3res_FSINFO3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_FSINFO3res_FSINFO3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_FSINFO3res_FSINFO3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::PATHCONF3args ##############

package NFS_remote::PATHCONF3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_object_get = *NFS_remotec::PATHCONF3args_object_get;
*swig_object_set = *NFS_remotec::PATHCONF3args_object_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_PATHCONF3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_PATHCONF3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::PATHCONF3resok ##############

package NFS_remote::PATHCONF3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::PATHCONF3resok_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::PATHCONF3resok_obj_attributes_set;
*swig_linkmax_get = *NFS_remotec::PATHCONF3resok_linkmax_get;
*swig_linkmax_set = *NFS_remotec::PATHCONF3resok_linkmax_set;
*swig_name_max_get = *NFS_remotec::PATHCONF3resok_name_max_get;
*swig_name_max_set = *NFS_remotec::PATHCONF3resok_name_max_set;
*swig_no_trunc_get = *NFS_remotec::PATHCONF3resok_no_trunc_get;
*swig_no_trunc_set = *NFS_remotec::PATHCONF3resok_no_trunc_set;
*swig_chown_restricted_get = *NFS_remotec::PATHCONF3resok_chown_restricted_get;
*swig_chown_restricted_set = *NFS_remotec::PATHCONF3resok_chown_restricted_set;
*swig_case_insensitive_get = *NFS_remotec::PATHCONF3resok_case_insensitive_get;
*swig_case_insensitive_set = *NFS_remotec::PATHCONF3resok_case_insensitive_set;
*swig_case_preserving_get = *NFS_remotec::PATHCONF3resok_case_preserving_get;
*swig_case_preserving_set = *NFS_remotec::PATHCONF3resok_case_preserving_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_PATHCONF3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_PATHCONF3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::PATHCONF3resfail ##############

package NFS_remote::PATHCONF3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_obj_attributes_get = *NFS_remotec::PATHCONF3resfail_obj_attributes_get;
*swig_obj_attributes_set = *NFS_remotec::PATHCONF3resfail_obj_attributes_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_PATHCONF3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_PATHCONF3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::PATHCONF3res ##############

package NFS_remote::PATHCONF3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::PATHCONF3res_status_get;
*swig_status_set = *NFS_remotec::PATHCONF3res_status_set;
*swig_PATHCONF3res_u_get = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_get;
*swig_PATHCONF3res_u_set = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_PATHCONF3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_PATHCONF3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::PATHCONF3res_PATHCONF3res_u ##############

package NFS_remote::PATHCONF3res_PATHCONF3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_resok_get;
*swig_resok_set = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::PATHCONF3res_PATHCONF3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_PATHCONF3res_PATHCONF3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_PATHCONF3res_PATHCONF3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::COMMIT3args ##############

package NFS_remote::COMMIT3args;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_get = *NFS_remotec::COMMIT3args_file_get;
*swig_file_set = *NFS_remotec::COMMIT3args_file_set;
*swig_offset_get = *NFS_remotec::COMMIT3args_offset_get;
*swig_offset_set = *NFS_remotec::COMMIT3args_offset_set;
*swig_count_get = *NFS_remotec::COMMIT3args_count_get;
*swig_count_set = *NFS_remotec::COMMIT3args_count_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_COMMIT3args(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_COMMIT3args($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::COMMIT3resok ##############

package NFS_remote::COMMIT3resok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_wcc_get = *NFS_remotec::COMMIT3resok_file_wcc_get;
*swig_file_wcc_set = *NFS_remotec::COMMIT3resok_file_wcc_set;
*swig_verf_get = *NFS_remotec::COMMIT3resok_verf_get;
*swig_verf_set = *NFS_remotec::COMMIT3resok_verf_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_COMMIT3resok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_COMMIT3resok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::COMMIT3resfail ##############

package NFS_remote::COMMIT3resfail;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_file_wcc_get = *NFS_remotec::COMMIT3resfail_file_wcc_get;
*swig_file_wcc_set = *NFS_remotec::COMMIT3resfail_file_wcc_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_COMMIT3resfail(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_COMMIT3resfail($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::COMMIT3res ##############

package NFS_remote::COMMIT3res;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_status_get = *NFS_remotec::COMMIT3res_status_get;
*swig_status_set = *NFS_remotec::COMMIT3res_status_set;
*swig_COMMIT3res_u_get = *NFS_remotec::COMMIT3res_COMMIT3res_u_get;
*swig_COMMIT3res_u_set = *NFS_remotec::COMMIT3res_COMMIT3res_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_COMMIT3res(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_COMMIT3res($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::COMMIT3res_COMMIT3res_u ##############

package NFS_remote::COMMIT3res_COMMIT3res_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_resok_get = *NFS_remotec::COMMIT3res_COMMIT3res_u_resok_get;
*swig_resok_set = *NFS_remotec::COMMIT3res_COMMIT3res_u_resok_set;
*swig_resfail_get = *NFS_remotec::COMMIT3res_COMMIT3res_u_resfail_get;
*swig_resfail_set = *NFS_remotec::COMMIT3res_COMMIT3res_u_resfail_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_COMMIT3res_COMMIT3res_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_COMMIT3res_COMMIT3res_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::fhandle3 ##############

package NFS_remote::fhandle3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fhandle3_len_get = *NFS_remotec::fhandle3_fhandle3_len_get;
*swig_fhandle3_len_set = *NFS_remotec::fhandle3_fhandle3_len_set;
*swig_fhandle3_val_get = *NFS_remotec::fhandle3_fhandle3_val_get;
*swig_fhandle3_val_set = *NFS_remotec::fhandle3_fhandle3_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_fhandle3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_fhandle3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::groupnode ##############

package NFS_remote::groupnode;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_gr_name_get = *NFS_remotec::groupnode_gr_name_get;
*swig_gr_name_set = *NFS_remotec::groupnode_gr_name_set;
*swig_gr_next_get = *NFS_remotec::groupnode_gr_next_get;
*swig_gr_next_set = *NFS_remotec::groupnode_gr_next_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_groupnode(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_groupnode($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::exportnode ##############

package NFS_remote::exportnode;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_ex_dir_get = *NFS_remotec::exportnode_ex_dir_get;
*swig_ex_dir_set = *NFS_remotec::exportnode_ex_dir_set;
*swig_ex_groups_get = *NFS_remotec::exportnode_ex_groups_get;
*swig_ex_groups_set = *NFS_remotec::exportnode_ex_groups_set;
*swig_ex_next_get = *NFS_remotec::exportnode_ex_next_get;
*swig_ex_next_set = *NFS_remotec::exportnode_ex_next_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_exportnode(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_exportnode($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mountbody ##############

package NFS_remote::mountbody;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_ml_hostname_get = *NFS_remotec::mountbody_ml_hostname_get;
*swig_ml_hostname_set = *NFS_remotec::mountbody_ml_hostname_set;
*swig_ml_directory_get = *NFS_remotec::mountbody_ml_directory_get;
*swig_ml_directory_set = *NFS_remotec::mountbody_ml_directory_set;
*swig_ml_next_get = *NFS_remotec::mountbody_ml_next_get;
*swig_ml_next_set = *NFS_remotec::mountbody_ml_next_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mountbody(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mountbody($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mountres3_ok ##############

package NFS_remote::mountres3_ok;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fhandle_get = *NFS_remotec::mountres3_ok_fhandle_get;
*swig_fhandle_set = *NFS_remotec::mountres3_ok_fhandle_set;
*swig_auth_flavors_get = *NFS_remotec::mountres3_ok_auth_flavors_get;
*swig_auth_flavors_set = *NFS_remotec::mountres3_ok_auth_flavors_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mountres3_ok(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mountres3_ok($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mountres3_ok_auth_flavors ##############

package NFS_remote::mountres3_ok_auth_flavors;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_auth_flavors_len_get = *NFS_remotec::mountres3_ok_auth_flavors_auth_flavors_len_get;
*swig_auth_flavors_len_set = *NFS_remotec::mountres3_ok_auth_flavors_auth_flavors_len_set;
*swig_auth_flavors_val_get = *NFS_remotec::mountres3_ok_auth_flavors_auth_flavors_val_get;
*swig_auth_flavors_val_set = *NFS_remotec::mountres3_ok_auth_flavors_auth_flavors_val_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mountres3_ok_auth_flavors(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mountres3_ok_auth_flavors($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mountres3 ##############

package NFS_remote::mountres3;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_fhs_status_get = *NFS_remotec::mountres3_fhs_status_get;
*swig_fhs_status_set = *NFS_remotec::mountres3_fhs_status_set;
*swig_mountres3_u_get = *NFS_remotec::mountres3_mountres3_u_get;
*swig_mountres3_u_set = *NFS_remotec::mountres3_mountres3_u_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mountres3(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mountres3($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::mountres3_mountres3_u ##############

package NFS_remote::mountres3_mountres3_u;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_mountinfo_get = *NFS_remotec::mountres3_mountres3_u_mountinfo_get;
*swig_mountinfo_set = *NFS_remotec::mountres3_mountres3_u_mountinfo_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_mountres3_mountres3_u(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_mountres3_mountres3_u($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfs_arg_t ##############

package NFS_remote::nfs_arg_t;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_arg_getattr2_get = *NFS_remotec::nfs_arg_t_arg_getattr2_get;
*swig_arg_getattr2_set = *NFS_remotec::nfs_arg_t_arg_getattr2_set;
*swig_arg_setattr2_get = *NFS_remotec::nfs_arg_t_arg_setattr2_get;
*swig_arg_setattr2_set = *NFS_remotec::nfs_arg_t_arg_setattr2_set;
*swig_arg_lookup2_get = *NFS_remotec::nfs_arg_t_arg_lookup2_get;
*swig_arg_lookup2_set = *NFS_remotec::nfs_arg_t_arg_lookup2_set;
*swig_arg_readlink2_get = *NFS_remotec::nfs_arg_t_arg_readlink2_get;
*swig_arg_readlink2_set = *NFS_remotec::nfs_arg_t_arg_readlink2_set;
*swig_arg_read2_get = *NFS_remotec::nfs_arg_t_arg_read2_get;
*swig_arg_read2_set = *NFS_remotec::nfs_arg_t_arg_read2_set;
*swig_arg_write2_get = *NFS_remotec::nfs_arg_t_arg_write2_get;
*swig_arg_write2_set = *NFS_remotec::nfs_arg_t_arg_write2_set;
*swig_arg_create2_get = *NFS_remotec::nfs_arg_t_arg_create2_get;
*swig_arg_create2_set = *NFS_remotec::nfs_arg_t_arg_create2_set;
*swig_arg_remove2_get = *NFS_remotec::nfs_arg_t_arg_remove2_get;
*swig_arg_remove2_set = *NFS_remotec::nfs_arg_t_arg_remove2_set;
*swig_arg_rename2_get = *NFS_remotec::nfs_arg_t_arg_rename2_get;
*swig_arg_rename2_set = *NFS_remotec::nfs_arg_t_arg_rename2_set;
*swig_arg_link2_get = *NFS_remotec::nfs_arg_t_arg_link2_get;
*swig_arg_link2_set = *NFS_remotec::nfs_arg_t_arg_link2_set;
*swig_arg_symlink2_get = *NFS_remotec::nfs_arg_t_arg_symlink2_get;
*swig_arg_symlink2_set = *NFS_remotec::nfs_arg_t_arg_symlink2_set;
*swig_arg_mkdir2_get = *NFS_remotec::nfs_arg_t_arg_mkdir2_get;
*swig_arg_mkdir2_set = *NFS_remotec::nfs_arg_t_arg_mkdir2_set;
*swig_arg_rmdir2_get = *NFS_remotec::nfs_arg_t_arg_rmdir2_get;
*swig_arg_rmdir2_set = *NFS_remotec::nfs_arg_t_arg_rmdir2_set;
*swig_arg_readdir2_get = *NFS_remotec::nfs_arg_t_arg_readdir2_get;
*swig_arg_readdir2_set = *NFS_remotec::nfs_arg_t_arg_readdir2_set;
*swig_arg_statfs2_get = *NFS_remotec::nfs_arg_t_arg_statfs2_get;
*swig_arg_statfs2_set = *NFS_remotec::nfs_arg_t_arg_statfs2_set;
*swig_arg_getattr3_get = *NFS_remotec::nfs_arg_t_arg_getattr3_get;
*swig_arg_getattr3_set = *NFS_remotec::nfs_arg_t_arg_getattr3_set;
*swig_arg_setattr3_get = *NFS_remotec::nfs_arg_t_arg_setattr3_get;
*swig_arg_setattr3_set = *NFS_remotec::nfs_arg_t_arg_setattr3_set;
*swig_arg_lookup3_get = *NFS_remotec::nfs_arg_t_arg_lookup3_get;
*swig_arg_lookup3_set = *NFS_remotec::nfs_arg_t_arg_lookup3_set;
*swig_arg_access3_get = *NFS_remotec::nfs_arg_t_arg_access3_get;
*swig_arg_access3_set = *NFS_remotec::nfs_arg_t_arg_access3_set;
*swig_arg_readlink3_get = *NFS_remotec::nfs_arg_t_arg_readlink3_get;
*swig_arg_readlink3_set = *NFS_remotec::nfs_arg_t_arg_readlink3_set;
*swig_arg_read3_get = *NFS_remotec::nfs_arg_t_arg_read3_get;
*swig_arg_read3_set = *NFS_remotec::nfs_arg_t_arg_read3_set;
*swig_arg_write3_get = *NFS_remotec::nfs_arg_t_arg_write3_get;
*swig_arg_write3_set = *NFS_remotec::nfs_arg_t_arg_write3_set;
*swig_arg_create3_get = *NFS_remotec::nfs_arg_t_arg_create3_get;
*swig_arg_create3_set = *NFS_remotec::nfs_arg_t_arg_create3_set;
*swig_arg_mkdir3_get = *NFS_remotec::nfs_arg_t_arg_mkdir3_get;
*swig_arg_mkdir3_set = *NFS_remotec::nfs_arg_t_arg_mkdir3_set;
*swig_arg_symlink3_get = *NFS_remotec::nfs_arg_t_arg_symlink3_get;
*swig_arg_symlink3_set = *NFS_remotec::nfs_arg_t_arg_symlink3_set;
*swig_arg_mknod3_get = *NFS_remotec::nfs_arg_t_arg_mknod3_get;
*swig_arg_mknod3_set = *NFS_remotec::nfs_arg_t_arg_mknod3_set;
*swig_arg_remove3_get = *NFS_remotec::nfs_arg_t_arg_remove3_get;
*swig_arg_remove3_set = *NFS_remotec::nfs_arg_t_arg_remove3_set;
*swig_arg_rmdir3_get = *NFS_remotec::nfs_arg_t_arg_rmdir3_get;
*swig_arg_rmdir3_set = *NFS_remotec::nfs_arg_t_arg_rmdir3_set;
*swig_arg_rename3_get = *NFS_remotec::nfs_arg_t_arg_rename3_get;
*swig_arg_rename3_set = *NFS_remotec::nfs_arg_t_arg_rename3_set;
*swig_arg_link3_get = *NFS_remotec::nfs_arg_t_arg_link3_get;
*swig_arg_link3_set = *NFS_remotec::nfs_arg_t_arg_link3_set;
*swig_arg_readdir3_get = *NFS_remotec::nfs_arg_t_arg_readdir3_get;
*swig_arg_readdir3_set = *NFS_remotec::nfs_arg_t_arg_readdir3_set;
*swig_arg_readdirplus3_get = *NFS_remotec::nfs_arg_t_arg_readdirplus3_get;
*swig_arg_readdirplus3_set = *NFS_remotec::nfs_arg_t_arg_readdirplus3_set;
*swig_arg_fsstat3_get = *NFS_remotec::nfs_arg_t_arg_fsstat3_get;
*swig_arg_fsstat3_set = *NFS_remotec::nfs_arg_t_arg_fsstat3_set;
*swig_arg_fsinfo3_get = *NFS_remotec::nfs_arg_t_arg_fsinfo3_get;
*swig_arg_fsinfo3_set = *NFS_remotec::nfs_arg_t_arg_fsinfo3_set;
*swig_arg_pathconf3_get = *NFS_remotec::nfs_arg_t_arg_pathconf3_get;
*swig_arg_pathconf3_set = *NFS_remotec::nfs_arg_t_arg_pathconf3_set;
*swig_arg_commit3_get = *NFS_remotec::nfs_arg_t_arg_commit3_get;
*swig_arg_commit3_set = *NFS_remotec::nfs_arg_t_arg_commit3_set;
*swig_arg_compound4_get = *NFS_remotec::nfs_arg_t_arg_compound4_get;
*swig_arg_compound4_set = *NFS_remotec::nfs_arg_t_arg_compound4_set;
*swig_arg_mnt_get = *NFS_remotec::nfs_arg_t_arg_mnt_get;
*swig_arg_mnt_set = *NFS_remotec::nfs_arg_t_arg_mnt_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfs_arg_t(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfs_arg_t($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


############# Class : NFS_remote::nfs_res_t ##############

package NFS_remote::nfs_res_t;
use vars qw(@ISA %OWNER %ITERATORS %BLESSEDMEMBERS);
@ISA = qw( NFS_remote );
%OWNER = ();
%ITERATORS = ();
*swig_res_attr2_get = *NFS_remotec::nfs_res_t_res_attr2_get;
*swig_res_attr2_set = *NFS_remotec::nfs_res_t_res_attr2_set;
*swig_res_dirop2_get = *NFS_remotec::nfs_res_t_res_dirop2_get;
*swig_res_dirop2_set = *NFS_remotec::nfs_res_t_res_dirop2_set;
*swig_res_readlink2_get = *NFS_remotec::nfs_res_t_res_readlink2_get;
*swig_res_readlink2_set = *NFS_remotec::nfs_res_t_res_readlink2_set;
*swig_res_read2_get = *NFS_remotec::nfs_res_t_res_read2_get;
*swig_res_read2_set = *NFS_remotec::nfs_res_t_res_read2_set;
*swig_res_stat2_get = *NFS_remotec::nfs_res_t_res_stat2_get;
*swig_res_stat2_set = *NFS_remotec::nfs_res_t_res_stat2_set;
*swig_res_readdir2_get = *NFS_remotec::nfs_res_t_res_readdir2_get;
*swig_res_readdir2_set = *NFS_remotec::nfs_res_t_res_readdir2_set;
*swig_res_statfs2_get = *NFS_remotec::nfs_res_t_res_statfs2_get;
*swig_res_statfs2_set = *NFS_remotec::nfs_res_t_res_statfs2_set;
*swig_res_getattr3_get = *NFS_remotec::nfs_res_t_res_getattr3_get;
*swig_res_getattr3_set = *NFS_remotec::nfs_res_t_res_getattr3_set;
*swig_res_setattr3_get = *NFS_remotec::nfs_res_t_res_setattr3_get;
*swig_res_setattr3_set = *NFS_remotec::nfs_res_t_res_setattr3_set;
*swig_res_lookup3_get = *NFS_remotec::nfs_res_t_res_lookup3_get;
*swig_res_lookup3_set = *NFS_remotec::nfs_res_t_res_lookup3_set;
*swig_res_access3_get = *NFS_remotec::nfs_res_t_res_access3_get;
*swig_res_access3_set = *NFS_remotec::nfs_res_t_res_access3_set;
*swig_res_readlink3_get = *NFS_remotec::nfs_res_t_res_readlink3_get;
*swig_res_readlink3_set = *NFS_remotec::nfs_res_t_res_readlink3_set;
*swig_res_read3_get = *NFS_remotec::nfs_res_t_res_read3_get;
*swig_res_read3_set = *NFS_remotec::nfs_res_t_res_read3_set;
*swig_res_write3_get = *NFS_remotec::nfs_res_t_res_write3_get;
*swig_res_write3_set = *NFS_remotec::nfs_res_t_res_write3_set;
*swig_res_create3_get = *NFS_remotec::nfs_res_t_res_create3_get;
*swig_res_create3_set = *NFS_remotec::nfs_res_t_res_create3_set;
*swig_res_mkdir3_get = *NFS_remotec::nfs_res_t_res_mkdir3_get;
*swig_res_mkdir3_set = *NFS_remotec::nfs_res_t_res_mkdir3_set;
*swig_res_symlink3_get = *NFS_remotec::nfs_res_t_res_symlink3_get;
*swig_res_symlink3_set = *NFS_remotec::nfs_res_t_res_symlink3_set;
*swig_res_mknod3_get = *NFS_remotec::nfs_res_t_res_mknod3_get;
*swig_res_mknod3_set = *NFS_remotec::nfs_res_t_res_mknod3_set;
*swig_res_remove3_get = *NFS_remotec::nfs_res_t_res_remove3_get;
*swig_res_remove3_set = *NFS_remotec::nfs_res_t_res_remove3_set;
*swig_res_rmdir3_get = *NFS_remotec::nfs_res_t_res_rmdir3_get;
*swig_res_rmdir3_set = *NFS_remotec::nfs_res_t_res_rmdir3_set;
*swig_res_rename3_get = *NFS_remotec::nfs_res_t_res_rename3_get;
*swig_res_rename3_set = *NFS_remotec::nfs_res_t_res_rename3_set;
*swig_res_link3_get = *NFS_remotec::nfs_res_t_res_link3_get;
*swig_res_link3_set = *NFS_remotec::nfs_res_t_res_link3_set;
*swig_res_readdir3_get = *NFS_remotec::nfs_res_t_res_readdir3_get;
*swig_res_readdir3_set = *NFS_remotec::nfs_res_t_res_readdir3_set;
*swig_res_readdirplus3_get = *NFS_remotec::nfs_res_t_res_readdirplus3_get;
*swig_res_readdirplus3_set = *NFS_remotec::nfs_res_t_res_readdirplus3_set;
*swig_res_fsstat3_get = *NFS_remotec::nfs_res_t_res_fsstat3_get;
*swig_res_fsstat3_set = *NFS_remotec::nfs_res_t_res_fsstat3_set;
*swig_res_fsinfo3_get = *NFS_remotec::nfs_res_t_res_fsinfo3_get;
*swig_res_fsinfo3_set = *NFS_remotec::nfs_res_t_res_fsinfo3_set;
*swig_res_pathconf3_get = *NFS_remotec::nfs_res_t_res_pathconf3_get;
*swig_res_pathconf3_set = *NFS_remotec::nfs_res_t_res_pathconf3_set;
*swig_res_commit3_get = *NFS_remotec::nfs_res_t_res_commit3_get;
*swig_res_commit3_set = *NFS_remotec::nfs_res_t_res_commit3_set;
*swig_res_compound4_get = *NFS_remotec::nfs_res_t_res_compound4_get;
*swig_res_compound4_set = *NFS_remotec::nfs_res_t_res_compound4_set;
*swig_res_mnt1_get = *NFS_remotec::nfs_res_t_res_mnt1_get;
*swig_res_mnt1_set = *NFS_remotec::nfs_res_t_res_mnt1_set;
*swig_res_mntexport_get = *NFS_remotec::nfs_res_t_res_mntexport_get;
*swig_res_mntexport_set = *NFS_remotec::nfs_res_t_res_mntexport_set;
*swig_res_mnt3_get = *NFS_remotec::nfs_res_t_res_mnt3_get;
*swig_res_mnt3_set = *NFS_remotec::nfs_res_t_res_mnt3_set;
*swig_res_dump_get = *NFS_remotec::nfs_res_t_res_dump_get;
*swig_res_dump_set = *NFS_remotec::nfs_res_t_res_dump_set;
*swig_toto_get = *NFS_remotec::nfs_res_t_toto_get;
*swig_toto_set = *NFS_remotec::nfs_res_t_toto_set;
sub new {
    my $pkg = shift;
    my $self = NFS_remotec::new_nfs_res_t(@_);
    bless $self, $pkg if defined($self);
}

sub DESTROY {
    return unless $_[0]->isa('HASH');
    my $self = tied(%{$_[0]});
    return unless defined $self;
    delete $ITERATORS{$self};
    if (exists $OWNER{$self}) {
        NFS_remotec::delete_nfs_res_t($self);
        delete $OWNER{$self};
    }
}

sub DISOWN {
    my $self = shift;
    my $ptr = tied(%$self);
    delete $OWNER{$ptr};
}

sub ACQUIRE {
    my $self = shift;
    my $ptr = tied(%$self);
    $OWNER{$ptr} = 1;
}


# ------- VARIABLE STUBS --------

package NFS_remote;

*NFS2_MAXDATA = *NFS_remotec::NFS2_MAXDATA;
*NFS2_MAXPATHLEN = *NFS_remotec::NFS2_MAXPATHLEN;
*NFS2_MAXNAMLEN = *NFS_remotec::NFS2_MAXNAMLEN;
*NFS2_COOKIESIZE = *NFS_remotec::NFS2_COOKIESIZE;
*NFS2_FHSIZE = *NFS_remotec::NFS2_FHSIZE;
*NFS2_MNTPATHLEN = *NFS_remotec::NFS2_MNTPATHLEN;
*NFS2_MNTNAMLEN = *NFS_remotec::NFS2_MNTNAMLEN;
*NFS3_FHSIZE = *NFS_remotec::NFS3_FHSIZE;
*NFS3_COOKIEVERFSIZE = *NFS_remotec::NFS3_COOKIEVERFSIZE;
*NFS3_CREATEVERFSIZE = *NFS_remotec::NFS3_CREATEVERFSIZE;
*NFS3_WRITEVERFSIZE = *NFS_remotec::NFS3_WRITEVERFSIZE;
*NFS2_MAX_FILESIZE = *NFS_remotec::NFS2_MAX_FILESIZE;
*NFS_OK = *NFS_remotec::NFS_OK;
*NFSERR_PERM = *NFS_remotec::NFSERR_PERM;
*NFSERR_NOENT = *NFS_remotec::NFSERR_NOENT;
*NFSERR_IO = *NFS_remotec::NFSERR_IO;
*NFSERR_NXIO = *NFS_remotec::NFSERR_NXIO;
*NFSERR_ACCES = *NFS_remotec::NFSERR_ACCES;
*NFSERR_EXIST = *NFS_remotec::NFSERR_EXIST;
*NFSERR_NODEV = *NFS_remotec::NFSERR_NODEV;
*NFSERR_NOTDIR = *NFS_remotec::NFSERR_NOTDIR;
*NFSERR_ISDIR = *NFS_remotec::NFSERR_ISDIR;
*NFSERR_FBIG = *NFS_remotec::NFSERR_FBIG;
*NFSERR_NOSPC = *NFS_remotec::NFSERR_NOSPC;
*NFSERR_ROFS = *NFS_remotec::NFSERR_ROFS;
*NFSERR_NAMETOOLONG = *NFS_remotec::NFSERR_NAMETOOLONG;
*NFSERR_NOTEMPTY = *NFS_remotec::NFSERR_NOTEMPTY;
*NFSERR_DQUOT = *NFS_remotec::NFSERR_DQUOT;
*NFSERR_STALE = *NFS_remotec::NFSERR_STALE;
*NFSERR_WFLUSH = *NFS_remotec::NFSERR_WFLUSH;
*NFNON = *NFS_remotec::NFNON;
*NFREG = *NFS_remotec::NFREG;
*NFDIR = *NFS_remotec::NFDIR;
*NFBLK = *NFS_remotec::NFBLK;
*NFCHR = *NFS_remotec::NFCHR;
*NFLNK = *NFS_remotec::NFLNK;
*NFSOCK = *NFS_remotec::NFSOCK;
*NFBAD = *NFS_remotec::NFBAD;
*NFFIFO = *NFS_remotec::NFFIFO;
*NFS3_OK = *NFS_remotec::NFS3_OK;
*NFS3ERR_PERM = *NFS_remotec::NFS3ERR_PERM;
*NFS3ERR_NOENT = *NFS_remotec::NFS3ERR_NOENT;
*NFS3ERR_IO = *NFS_remotec::NFS3ERR_IO;
*NFS3ERR_NXIO = *NFS_remotec::NFS3ERR_NXIO;
*NFS3ERR_ACCES = *NFS_remotec::NFS3ERR_ACCES;
*NFS3ERR_EXIST = *NFS_remotec::NFS3ERR_EXIST;
*NFS3ERR_XDEV = *NFS_remotec::NFS3ERR_XDEV;
*NFS3ERR_NODEV = *NFS_remotec::NFS3ERR_NODEV;
*NFS3ERR_NOTDIR = *NFS_remotec::NFS3ERR_NOTDIR;
*NFS3ERR_ISDIR = *NFS_remotec::NFS3ERR_ISDIR;
*NFS3ERR_INVAL = *NFS_remotec::NFS3ERR_INVAL;
*NFS3ERR_FBIG = *NFS_remotec::NFS3ERR_FBIG;
*NFS3ERR_NOSPC = *NFS_remotec::NFS3ERR_NOSPC;
*NFS3ERR_ROFS = *NFS_remotec::NFS3ERR_ROFS;
*NFS3ERR_MLINK = *NFS_remotec::NFS3ERR_MLINK;
*NFS3ERR_NAMETOOLONG = *NFS_remotec::NFS3ERR_NAMETOOLONG;
*NFS3ERR_NOTEMPTY = *NFS_remotec::NFS3ERR_NOTEMPTY;
*NFS3ERR_DQUOT = *NFS_remotec::NFS3ERR_DQUOT;
*NFS3ERR_STALE = *NFS_remotec::NFS3ERR_STALE;
*NFS3ERR_REMOTE = *NFS_remotec::NFS3ERR_REMOTE;
*NFS3ERR_BADHANDLE = *NFS_remotec::NFS3ERR_BADHANDLE;
*NFS3ERR_NOT_SYNC = *NFS_remotec::NFS3ERR_NOT_SYNC;
*NFS3ERR_BAD_COOKIE = *NFS_remotec::NFS3ERR_BAD_COOKIE;
*NFS3ERR_NOTSUPP = *NFS_remotec::NFS3ERR_NOTSUPP;
*NFS3ERR_TOOSMALL = *NFS_remotec::NFS3ERR_TOOSMALL;
*NFS3ERR_SERVERFAULT = *NFS_remotec::NFS3ERR_SERVERFAULT;
*NFS3ERR_BADTYPE = *NFS_remotec::NFS3ERR_BADTYPE;
*NFS3ERR_JUKEBOX = *NFS_remotec::NFS3ERR_JUKEBOX;
*NF3REG = *NFS_remotec::NF3REG;
*NF3DIR = *NFS_remotec::NF3DIR;
*NF3BLK = *NFS_remotec::NF3BLK;
*NF3CHR = *NFS_remotec::NF3CHR;
*NF3LNK = *NFS_remotec::NF3LNK;
*NF3SOCK = *NFS_remotec::NF3SOCK;
*NF3FIFO = *NFS_remotec::NF3FIFO;
*DONT_CHANGE = *NFS_remotec::DONT_CHANGE;
*SET_TO_SERVER_TIME = *NFS_remotec::SET_TO_SERVER_TIME;
*SET_TO_CLIENT_TIME = *NFS_remotec::SET_TO_CLIENT_TIME;
*ACCESS3_READ = *NFS_remotec::ACCESS3_READ;
*ACCESS3_LOOKUP = *NFS_remotec::ACCESS3_LOOKUP;
*ACCESS3_MODIFY = *NFS_remotec::ACCESS3_MODIFY;
*ACCESS3_EXTEND = *NFS_remotec::ACCESS3_EXTEND;
*ACCESS3_DELETE = *NFS_remotec::ACCESS3_DELETE;
*ACCESS3_EXECUTE = *NFS_remotec::ACCESS3_EXECUTE;
*UNSTABLE = *NFS_remotec::UNSTABLE;
*DATA_SYNC = *NFS_remotec::DATA_SYNC;
*FILE_SYNC = *NFS_remotec::FILE_SYNC;
*UNCHECKED = *NFS_remotec::UNCHECKED;
*GUARDED = *NFS_remotec::GUARDED;
*EXCLUSIVE = *NFS_remotec::EXCLUSIVE;
*FSF3_LINK = *NFS_remotec::FSF3_LINK;
*FSF3_SYMLINK = *NFS_remotec::FSF3_SYMLINK;
*FSF3_HOMOGENEOUS = *NFS_remotec::FSF3_HOMOGENEOUS;
*FSF3_CANSETTIME = *NFS_remotec::FSF3_CANSETTIME;
*NFS_PROGRAM = *NFS_remotec::NFS_PROGRAM;
*NFS_V2 = *NFS_remotec::NFS_V2;
*NFSPROC_NULL = *NFS_remotec::NFSPROC_NULL;
*NFSPROC_GETATTR = *NFS_remotec::NFSPROC_GETATTR;
*NFSPROC_SETATTR = *NFS_remotec::NFSPROC_SETATTR;
*NFSPROC_ROOT = *NFS_remotec::NFSPROC_ROOT;
*NFSPROC_LOOKUP = *NFS_remotec::NFSPROC_LOOKUP;
*NFSPROC_READLINK = *NFS_remotec::NFSPROC_READLINK;
*NFSPROC_READ = *NFS_remotec::NFSPROC_READ;
*NFSPROC_WRITECACHE = *NFS_remotec::NFSPROC_WRITECACHE;
*NFSPROC_WRITE = *NFS_remotec::NFSPROC_WRITE;
*NFSPROC_CREATE = *NFS_remotec::NFSPROC_CREATE;
*NFSPROC_REMOVE = *NFS_remotec::NFSPROC_REMOVE;
*NFSPROC_RENAME = *NFS_remotec::NFSPROC_RENAME;
*NFSPROC_LINK = *NFS_remotec::NFSPROC_LINK;
*NFSPROC_SYMLINK = *NFS_remotec::NFSPROC_SYMLINK;
*NFSPROC_MKDIR = *NFS_remotec::NFSPROC_MKDIR;
*NFSPROC_RMDIR = *NFS_remotec::NFSPROC_RMDIR;
*NFSPROC_READDIR = *NFS_remotec::NFSPROC_READDIR;
*NFSPROC_STATFS = *NFS_remotec::NFSPROC_STATFS;
*NFS_V3 = *NFS_remotec::NFS_V3;
*NFSPROC3_NULL = *NFS_remotec::NFSPROC3_NULL;
*NFSPROC3_GETATTR = *NFS_remotec::NFSPROC3_GETATTR;
*NFSPROC3_SETATTR = *NFS_remotec::NFSPROC3_SETATTR;
*NFSPROC3_LOOKUP = *NFS_remotec::NFSPROC3_LOOKUP;
*NFSPROC3_ACCESS = *NFS_remotec::NFSPROC3_ACCESS;
*NFSPROC3_READLINK = *NFS_remotec::NFSPROC3_READLINK;
*NFSPROC3_READ = *NFS_remotec::NFSPROC3_READ;
*NFSPROC3_WRITE = *NFS_remotec::NFSPROC3_WRITE;
*NFSPROC3_CREATE = *NFS_remotec::NFSPROC3_CREATE;
*NFSPROC3_MKDIR = *NFS_remotec::NFSPROC3_MKDIR;
*NFSPROC3_SYMLINK = *NFS_remotec::NFSPROC3_SYMLINK;
*NFSPROC3_MKNOD = *NFS_remotec::NFSPROC3_MKNOD;
*NFSPROC3_REMOVE = *NFS_remotec::NFSPROC3_REMOVE;
*NFSPROC3_RMDIR = *NFS_remotec::NFSPROC3_RMDIR;
*NFSPROC3_RENAME = *NFS_remotec::NFSPROC3_RENAME;
*NFSPROC3_LINK = *NFS_remotec::NFSPROC3_LINK;
*NFSPROC3_READDIR = *NFS_remotec::NFSPROC3_READDIR;
*NFSPROC3_READDIRPLUS = *NFS_remotec::NFSPROC3_READDIRPLUS;
*NFSPROC3_FSSTAT = *NFS_remotec::NFSPROC3_FSSTAT;
*NFSPROC3_FSINFO = *NFS_remotec::NFSPROC3_FSINFO;
*NFSPROC3_PATHCONF = *NFS_remotec::NFSPROC3_PATHCONF;
*NFSPROC3_COMMIT = *NFS_remotec::NFSPROC3_COMMIT;
*MNTPATHLEN = *NFS_remotec::MNTPATHLEN;
*MNTNAMLEN = *NFS_remotec::MNTNAMLEN;
*MNT3_OK = *NFS_remotec::MNT3_OK;
*MNT3ERR_PERM = *NFS_remotec::MNT3ERR_PERM;
*MNT3ERR_NOENT = *NFS_remotec::MNT3ERR_NOENT;
*MNT3ERR_IO = *NFS_remotec::MNT3ERR_IO;
*MNT3ERR_ACCES = *NFS_remotec::MNT3ERR_ACCES;
*MNT3ERR_NOTDIR = *NFS_remotec::MNT3ERR_NOTDIR;
*MNT3ERR_INVAL = *NFS_remotec::MNT3ERR_INVAL;
*MNT3ERR_NAMETOOLONG = *NFS_remotec::MNT3ERR_NAMETOOLONG;
*MNT3ERR_NOTSUPP = *NFS_remotec::MNT3ERR_NOTSUPP;
*MNT3ERR_SERVERFAULT = *NFS_remotec::MNT3ERR_SERVERFAULT;
*MOUNTPROG = *NFS_remotec::MOUNTPROG;
*MOUNT_V1 = *NFS_remotec::MOUNT_V1;
*MOUNTPROC2_NULL = *NFS_remotec::MOUNTPROC2_NULL;
*MOUNTPROC2_MNT = *NFS_remotec::MOUNTPROC2_MNT;
*MOUNTPROC2_DUMP = *NFS_remotec::MOUNTPROC2_DUMP;
*MOUNTPROC2_UMNT = *NFS_remotec::MOUNTPROC2_UMNT;
*MOUNTPROC2_UMNTALL = *NFS_remotec::MOUNTPROC2_UMNTALL;
*MOUNTPROC2_EXPORT = *NFS_remotec::MOUNTPROC2_EXPORT;
*MOUNT_V3 = *NFS_remotec::MOUNT_V3;
*MOUNTPROC3_NULL = *NFS_remotec::MOUNTPROC3_NULL;
*MOUNTPROC3_MNT = *NFS_remotec::MOUNTPROC3_MNT;
*MOUNTPROC3_DUMP = *NFS_remotec::MOUNTPROC3_DUMP;
*MOUNTPROC3_UMNT = *NFS_remotec::MOUNTPROC3_UMNT;
*MOUNTPROC3_UMNTALL = *NFS_remotec::MOUNTPROC3_UMNTALL;
*MOUNTPROC3_EXPORT = *NFS_remotec::MOUNTPROC3_EXPORT;
1;
