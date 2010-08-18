#include <umem.h>
#include <libsolkerncompat.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/systm.h>
#include <libzfs.h>
#include <sys/zfs_znode.h>
#include <sys/mode.h>
#include <sys/fcntl.h>

#include "zfs_ioctl.h"

#include "libzfswrap.h"

extern int zfs_vfsinit(int fstype, char *name);

static int getattr_helper(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, uint64_t *p_gen, int *p_type);

static void flags2zfs(int i_flags, int *p_flags, int *p_mode)
{
        if(i_flags & O_WRONLY)
        {
                *p_mode = VWRITE;
                *p_flags = FWRITE;
        }
        else if(i_flags & O_RDWR)
        {
               *p_mode = VREAD | VWRITE;
               *p_flags = FREAD | FWRITE;
        }
        else
        {
                *p_mode = VREAD;
                *p_flags = FREAD;
        }

        if(i_flags & O_CREAT)
                *p_flags |= FCREAT;
        if(i_flags & O_SYNC)
                *p_flags |= FSYNC;
        if(i_flags & O_DSYNC)
                *p_flags |= FDSYNC;
        if(i_flags & O_RSYNC)
                *p_flags |= FRSYNC;
        if(i_flags & O_APPEND)
                *p_flags |= FAPPEND;
        if(i_flags & O_LARGEFILE)
                *p_flags |= FOFFMAX;
        if(i_flags & O_NOFOLLOW)
                *p_flags |= FNOFOLLOW;
        if(i_flags & O_TRUNC)
                *p_flags |= FTRUNC;
        if(i_flags & O_EXCL)
                *p_flags |= FEXCL;
}

/**
 * Initialize the libzfswrap library
 * @return a handle to the library, NULL in case of error
 */
libzfswrap_handle_t *libzfswrap_init()
{
        init_mmap();
        libsolkerncompat_init();
        zfs_vfsinit(zfstype, NULL);
        zfs_ioctl_init();
        libzfs_handle_t *p_zhd = libzfs_init();

        if(!p_zhd)
                libsolkerncompat_exit();

        return (libzfswrap_handle_t*)p_zhd;
}

/**
 * Uninitialize the library
 * @param p_zfsw: the libzfswrap handle
 */
void libzfswrap_exit(libzfswrap_handle_t *p_zhd)
{
        libzfs_fini((libzfs_handle_t*)p_zhd);
        libsolkerncompat_exit();
}

extern vfsops_t *zfs_vfsops;
/**
 * Mount the given file system
 * @param psz_zpool: the pool to mount
 * @param psz_dir: the directory to mount
 * @param psz_options: options for the mounting point
 * @return the vitual file system
 */
libzfswrap_vfs_t *libzfswrap_mount(const char *psz_zpool, const char *psz_dir, const char *psz_options)
{
        vfs_t *p_vfs = calloc(1, sizeof(vfs_t));
        if(!p_vfs)
                return NULL;

        VFS_INIT(p_vfs, zfs_vfsops, 0);
        VFS_HOLD(p_vfs);

        struct mounta uap = {
        .spec = (char*)psz_zpool,
        .dir = (char*)psz_dir,
        .flags = 0 | MS_SYSSPACE,
        .fstype = "zfs-ganesha",
        .dataptr = "",
        .datalen = 0,
        .optptr = (char*)psz_options,
        .optlen = strlen(psz_options)
        };

        cred_t cred = { .cr_uid = 0, .cr_gid = 0 };
        int i_error = VFS_MOUNT(p_vfs, rootdir, &uap, &cred);
        if(i_error)
        {
                free(p_vfs);
                return NULL;
        }
        return (libzfswrap_vfs_t*)p_vfs;
}

/**
 * Get the root object of a file system
 * @param p_vfs: the virtual filesystem
 * @param p_root: return the root object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getroot(libzfswrap_vfs_t *p_vfs, inogen_t *p_root)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, 3, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode != NULL);
        // Get the generation
        p_root->inode = 3;
        p_root->generation = p_znode->z_phys->zp_gen;

        VN_RELE(ZTOV(p_znode));
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Unmount the given file system
 * @param p_vfs: the virtual file system
 * @param b_force: force the unmount ?
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_umount(libzfswrap_vfs_t *p_vfs, int b_force)
{
        int i_error;
        cred_t cred = { .cr_uid = 0, .cr_gid = 0 };

        VFS_SYNC((vfs_t*)p_vfs, 0, &cred);
        if((i_error = VFS_UNMOUNT((vfs_t*)p_vfs, b_force ? MS_FORCE : 0, &cred)))
        {
                return i_error;
        }

        assert(b_force || ((vfs_t*)p_vfs)->vfs_count == 1);
        return 0;
}



/**
 * Get some more informations about the file system
 * @param p_vfs: the virtual file system
 * @param p_stats: the statistics
 * @return 0 in case of success, -1 overwise
 */
int libzfswrap_statfs(libzfswrap_vfs_t *p_vfs, struct statvfs *p_statvfs)
{
        //FIXME: no ZFS_ENTER ??
        struct statvfs64 zfs_stats = { 0 };
        int i_error;
        if((i_error = VFS_STATVFS((vfs_t*)p_vfs, &zfs_stats)))
                return i_error;

        p_statvfs->f_bsize = zfs_stats.f_frsize;
        p_statvfs->f_frsize = zfs_stats.f_frsize;
        p_statvfs->f_blocks = zfs_stats.f_blocks;
        p_statvfs->f_bfree = zfs_stats.f_bfree;
        p_statvfs->f_bavail = zfs_stats.f_bavail;
        p_statvfs->f_files = zfs_stats.f_files;
        p_statvfs->f_ffree = zfs_stats.f_ffree;
        p_statvfs->f_favail = zfs_stats.f_favail;
        p_statvfs->f_fsid = zfs_stats.f_fsid;
        p_statvfs->f_flag = zfs_stats.f_flag;
        p_statvfs->f_namemax = zfs_stats.f_namemax;

        return 0;
}

/**
 * Lookup for a given file in the given directory
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent file object
 * @param psz_name: filename
 * @param p_object: return the object node and generation
 * @param p_type: return the object type
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_lookup(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, inogen_t *p_object, int *p_type)
{
        if(strlen(psz_name) >= MAXNAMELEN)
                return -1;

        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_parent_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);

        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        ASSERT(p_parent_znode != NULL);
        // Check the parent generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vnode_t *p_vnode = NULL;
        if((i_error = VOP_LOOKUP(p_parent_vnode, (char*)psz_name, &p_vnode, NULL, 0, NULL, (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
	}

	p_object->inode = VTOZ(p_vnode)->z_id;
	p_object->generation = VTOZ(p_vnode)->z_phys->zp_gen;
	*p_type = VTTOIF(p_vnode->v_type);

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return 0;
}

/**
 * Test the access right of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param mask: the rights to check
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_access(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int mask)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        int mode = 0;
        if(mask & R_OK)
                mode |= VREAD;
        if(mask & W_OK)
                mode |= VWRITE;
        if(mask & X_OK)
                mode |= VEXEC;

        i_error = VOP_ACCESS(p_vnode, mode, 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Open the given object
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object to open
 * @param i_flags: the opening flags
 * @param pp_vnode: the virtual node
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_open(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int i_flags, libzfswrap_vnode_t **pp_vnode)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int mode = 0, flags = 0, i_error;
        flags2zfs(i_flags, &flags, &mode);

        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        vnode_t *p_old_vnode = p_vnode;

        // Check errors
        if((i_error = VOP_OPEN(&p_vnode, flags, (cred_t*)p_cred, NULL)))
        {
                //FIXME: memleak ?
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_old_vnode == p_vnode);

        ZFS_EXIT(p_zfsvfs);
        *pp_vnode = (libzfswrap_vnode_t*)p_vnode;
        return 0;
}

/**
 * Create the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent object
 * @param psz_filename: the file name
 * @param mode: the file mode
 * @param p_file: return the file
 * @return 0 in case of success the error code overwise
 */
int libzfswrap_create(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, mode_t mode, inogen_t *p_file)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vattr_t vattr = { 0 };
        vattr.va_type = VREG;
        vattr.va_mode = mode;
        vattr.va_mask = AT_TYPE | AT_MODE;

        vnode_t *p_new_vnode;

        if((i_error = VOP_CREATE(p_parent_vnode, (char*)psz_filename, &vattr, NONEXCL, mode, &p_new_vnode, (cred_t*)p_cred, 0, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        p_file->inode = VTOZ(p_new_vnode)->z_id;
        p_file->generation = VTOZ(p_new_vnode)->z_phys->zp_gen;

        VN_RELE(p_new_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Open a directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param directory: the directory to open
 * @param pp_vnode: the vnode to return
 * @return 0 on success, the error code overwise
 */
int libzfswrap_opendir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t directory, libzfswrap_vnode_t **pp_vnode)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, directory.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != directory.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        // Check that we have a directory
        if(p_vnode->v_type != VDIR)
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return ENOTDIR;
        }

        vnode_t *p_old_vnode = p_vnode;
        if((i_error = VOP_OPEN(&p_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_old_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_old_vnode == p_vnode);

        ZFS_EXIT(p_zfsvfs);
        *pp_vnode = (libzfswrap_vnode_t*)p_vnode;
        return 0;
}

/**
 * Read the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_entries: the array of entries to fill
 * @param size: the array size
 * @param cookie: the offset to read in the directory
 * @return 0 on success, the error code overwise
 */
int libzfswrap_readdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, libzfswrap_entry_t *p_entries, size_t size, off_t *cookie)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;

        // Check that the vnode is a directory
        if(((vnode_t*)p_vnode)->v_type != VDIR)
                return ENOTDIR;

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        off_t next_entry = *cookie;
        int i_error, eofp = 0;
        union {
                char buf[DIRENT64_RECLEN(MAXNAMELEN)];
                struct dirent64 dirent;
        } entry;

        ZFS_ENTER(p_zfsvfs);
        size_t index = 0;
        while(index < size)
        {
                iovec.iov_base = entry.buf;
                iovec.iov_len = sizeof(entry.buf);
                uio.uio_resid = iovec.iov_len;
                uio.uio_loffset = next_entry;

                /* TODO: do only one call for more than one entry ? */
                if((i_error = VOP_READDIR((vnode_t*)p_vnode, &uio, (cred_t*)p_cred, &eofp, NULL, 0)))
                        break;

                // End of directory ?
                if(iovec.iov_base == entry.buf)
                        break;

                // Copy the entry name
                strcpy(p_entries[index].psz_filename, entry.dirent.d_name);
                p_entries[index].object.inode = entry.dirent.d_ino;
                getattr_helper(p_vfs, p_cred, p_entries[index].object, &(p_entries[index].stats), &(p_entries[index].object.generation), &(p_entries[index].type));

                // Go to the next entry
                next_entry = entry.dirent.d_off;
                index++;
        }
        ZFS_EXIT(p_zfsvfs);

        // Set the last element to NULL if we end before size elements
        if(index < size)
        {
                p_entries[index].psz_filename[0] = '\0';
                *cookie = 0;
        }
        else
                *cookie = next_entry;

        return 0;
}

/**
 * Close the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @return 0 on success, the error code overwise
 */
int libzfswrap_closedir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode)
{
        return libzfswrap_close(p_vfs, p_cred, p_vnode, O_RDONLY);
}

/**
 * Get the stat about a file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_stat: the stat struct to fill in
 * @return 0 on success, the error code overwise
 */
int libzfswrap_stat(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, struct stat *p_stat)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        vattr_t vattr;
        vattr.va_mask = AT_ALL;
        memset(p_stat, 0, sizeof(*p_stat));

        ZFS_ENTER(p_zfsvfs);
        int i_error = VOP_GETATTR((vnode_t*)p_vnode, &vattr, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);
        if(i_error)
                return i_error;

        p_stat->st_dev = vattr.va_fsid;                      
        p_stat->st_ino = vattr.va_nodeid;
        p_stat->st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
        p_stat->st_nlink = vattr.va_nlink;
        p_stat->st_uid = vattr.va_uid;
        p_stat->st_gid = vattr.va_gid;
        p_stat->st_rdev = vattr.va_rdev;
        p_stat->st_size = vattr.va_size;
        p_stat->st_blksize = vattr.va_blksize;         
        p_stat->st_blocks = vattr.va_nblocks;
        TIMESTRUC_TO_TIME(vattr.va_atime, &p_stat->st_atime);
        TIMESTRUC_TO_TIME(vattr.va_mtime, &p_stat->st_mtime);
        TIMESTRUC_TO_TIME(vattr.va_ctime, &p_stat->st_ctime);

        return 0;
}

static int getattr_helper(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, uint64_t *p_gen, int *p_type)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_FALSE)))
                return i_error;
        ASSERT(p_znode);
        // Check the generation
        if(p_gen)
                *p_gen = p_znode->z_phys->zp_gen;
        else if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        vattr_t vattr;
        vattr.va_mask = AT_ALL;
        memset(p_stat, 0, sizeof(*p_stat));

        if(p_type)
	        *p_type = VTTOIF(p_vnode->v_type);

        if((i_error = VOP_GETATTR(p_vnode, &vattr, 0, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_vnode);
                return i_error;
        }
        VN_RELE(p_vnode);

        p_stat->st_dev = vattr.va_fsid;
        p_stat->st_ino = vattr.va_nodeid;
        p_stat->st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
        p_stat->st_nlink = vattr.va_nlink;
        p_stat->st_uid = vattr.va_uid;
        p_stat->st_gid = vattr.va_gid;
        p_stat->st_rdev = vattr.va_rdev;
        p_stat->st_size = vattr.va_size;
        p_stat->st_blksize = vattr.va_blksize;
        p_stat->st_blocks = vattr.va_nblocks;
        TIMESTRUC_TO_TIME(vattr.va_atime, &p_stat->st_atime);
        TIMESTRUC_TO_TIME(vattr.va_mtime, &p_stat->st_mtime);
        TIMESTRUC_TO_TIME(vattr.va_ctime, &p_stat->st_ctime);

        return 0;
}

/**
 * Get the attributes of an object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param p_stat: the attributes to fill
 * @param p_type: return the type of the object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int *p_type)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        i_error = getattr_helper(p_vfs, p_cred, object, p_stat, NULL, p_type);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Set the attributes of an object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param p_stat: new attributes to set
 * @param flags: bit field of attributes to set
 * @param p_new_stat: new attributes of the object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_setattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int flags, struct stat *p_new_stat)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;
        int update_time = 0;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t* p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        vattr_t vattr = { 0 };
        if(flags & LZFSW_ATTR_MODE)
        {
                vattr.va_mask |= AT_MODE;
                vattr.va_mode = p_stat->st_mode;
        }
        if(flags & LZFSW_ATTR_UID)
        {
                vattr.va_mask |= AT_UID;
                vattr.va_uid = p_stat->st_uid;
        }
        if(flags & LZFSW_ATTR_GID)
        {
                vattr.va_mask |= AT_GID;
                vattr.va_gid = p_stat->st_gid;
        }
        if(flags & LZFSW_ATTR_ATIME)
        {
                vattr.va_mask |= AT_ATIME;
                TIME_TO_TIMESTRUC(p_stat->st_atime, &vattr.va_atime);
                update_time = ATTR_UTIME;
        }
        if(flags & LZFSW_ATTR_MTIME)
        {
                vattr.va_mask |= AT_MTIME;
                TIME_TO_TIMESTRUC(p_stat->st_mtime, &vattr.va_mtime);
                update_time = ATTR_UTIME;
        }

        i_error = VOP_SETATTR(p_vnode, &vattr, update_time, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Helper function for every function that manipulate xattrs
 * @param p_zfsvfs: the virtual file system root object
 * @param p_cred: the user credentials
 * @param object: the object
 * @param
 */
int xattr_helper(zfsvfs_t *p_zfsvfs, creden_t *p_cred, inogen_t object, vnode_t **pp_vnode)
{
        znode_t *p_znode;
        int i_error;

        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
                return i_error;
        ASSERT(p_znode);

        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                return ENOENT;
        }
        vnode_t* p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        // Lookup for the xattr directory
        vnode_t *p_xattr_vnode;
        i_error = VOP_LOOKUP(p_vnode, "", &p_xattr_vnode, NULL,
                             LOOKUP_XATTR | CREATE_XATTR_DIR, NULL,
                             (cred_t*)p_cred, NULL, NULL, NULL);
        VN_RELE(p_vnode);

        if(i_error || !p_xattr_vnode)
        {
                if(p_xattr_vnode)
                        VN_RELE(p_xattr_vnode);
                return i_error ? i_error : ENOSYS;
        }

        *pp_vnode = p_xattr_vnode;
        return 0;
}

/**
 * List the extended attributes
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param ppsz_buffer: the buffer to fill with the list of attributes
 * @param p_size: will contain the size of the buffer
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_listxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, char **ppsz_buffer, size_t *p_size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Open the speudo directory
        if((i_error = VOP_OPEN(&p_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        char *psz_buffer = NULL;
        size_t i_size = 0;
        union {
                char buf[DIRENT64_RECLEN(MAXNAMELEN)];
                struct dirent64 dirent;
        } entry;

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        int eofp = 0;
        off_t next = 0;

        while(1)
        {
                iovec.iov_base = entry.buf;
                iovec.iov_len = sizeof(entry.buf);
                uio.uio_resid = iovec.iov_len;
                uio.uio_loffset = next;

                if((i_error = VOP_READDIR(p_vnode, &uio, (cred_t*)p_cred, &eofp, NULL, 0)))
                {
                        VOP_CLOSE(p_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);
                        VN_RELE(p_vnode);
                        ZFS_EXIT(p_zfsvfs);
                        return i_error;
                }

                if(iovec.iov_base == entry.buf)
                        break;

                next = entry.dirent.d_off;
                // Skip '.' and '..'
                char *s = entry.dirent.d_name;
                if(*s == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0)))
                        continue;

                size_t length = strlen(s);
                psz_buffer = realloc(psz_buffer, i_size + length + 1);
                strcpy(&psz_buffer[i_size], s);
                i_size += length + 1;
        }

        VOP_CLOSE(p_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        // Return the values
        *ppsz_buffer = psz_buffer;
        *p_size = i_size;

        return 0;
}

/**
 * Add the given (key,value) to the extended attributes.
 * This function will change the value if the key already exist.
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param psz_key: the key
 * @param psz_value: the value
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_setxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, const char *psz_value)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Create a new speudo-file
        vattr_t vattr = { 0 };
        vattr.va_type = VREG;
        vattr.va_mode = 0660;
        vattr.va_mask = AT_TYPE | AT_MODE | AT_SIZE;
        vattr.va_size = 0;

        vnode_t *p_pseudo_vnode;
        if((i_error = VOP_CREATE(p_vnode, (char*)psz_key, &vattr, NONEXCL, VWRITE,
                                 &p_pseudo_vnode, (cred_t*)p_cred, 0, NULL, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
        }
        VN_RELE(p_vnode);

        // Open the key-file
        vnode_t *p_key_vnode = p_pseudo_vnode;
        if((i_error = VOP_OPEN(&p_key_vnode, FWRITE, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_pseudo_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = (void *) psz_value;
        iovec.iov_len = strlen(psz_value);
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_WRITE(p_key_vnode, &uio, FWRITE, (cred_t*)p_cred, NULL);
        VOP_CLOSE(p_key_vnode, FWRITE, 1, (offset_t) 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_key_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Get the value for the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @param ppsz_value: the value
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_getxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, char **ppsz_value)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;
        char *psz_value;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Lookup for the right file
        vnode_t *p_key_vnode;
        if((i_error = VOP_LOOKUP(p_vnode, (char*)psz_key, &p_key_vnode, NULL, 0, NULL,
                                 (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        VN_RELE(p_vnode);

        // Get the size of the value
        vattr_t vattr = { 0 };
        vattr.va_mask = AT_STAT | AT_NBLOCKS | AT_BLKSIZE | AT_SIZE;
        if((i_error = VOP_GETATTR(p_key_vnode, &vattr, 0, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_key_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        if((i_error = VOP_OPEN(&p_key_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_key_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Read the value
        psz_value = malloc(vattr.va_size + 1);
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = psz_value;
        iovec.iov_len = vattr.va_size + 1;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_READ(p_key_vnode, &uio, FREAD, (cred_t*)p_cred, NULL);
        VOP_CLOSE(p_key_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);

        VN_RELE(p_key_vnode);
        ZFS_EXIT(p_zfsvfs);

        if(!i_error)
        {
                psz_value[vattr.va_size] = '\0';
                *ppsz_value = psz_value;
        }
        return i_error;
}

/**
 * Remove the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_removexattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;
        char *psz_value;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        i_error = VOP_REMOVE(p_vnode, (char*)psz_key, (cred_t*)p_cred, NULL, 0);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}


/**
 * Read some data from the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_buffer: the buffer to write into
 * @param size: the size of the buffer
 * @param behind: do we have to read behind the file ?
 * @param offset: the offset to read
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_read(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;                      // TODO: Do we have to give the same flags ?
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = p_buffer;
        iovec.iov_len = size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = offset;
        if(behind)
                uio.uio_loffset += VTOZ((vnode_t*)p_vnode)->z_phys->zp_size;

        ZFS_ENTER(p_zfsvfs);
        int error = VOP_READ((vnode_t*)p_vnode, &uio, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);

        if(offset == uio.uio_loffset)
                return 0;
        else
                return size;
        return error;
}


/**
 * Write some data to the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_buffer: the buffer to write
 * @param size: the size of the buffer
 * @param behind: do we have to write behind the end of the file ?
 * @param offset: the offset to write
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_write(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;                      // TODO: Do we have to give the same flags ?
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = p_buffer;
        iovec.iov_len = size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = offset;
        if(behind)
                uio.uio_loffset += VTOZ((vnode_t*)p_vnode)->z_phys->zp_size;

        ZFS_ENTER(p_zfsvfs);
        int error = VOP_WRITE((vnode_t*)p_vnode, &uio, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);

        return error;
}

/**
 * Close the given vnode
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode to close
 * @param i_flags: the flags given when opening
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_close(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, int i_flags)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;

        int mode, flags, i_error;
        flags2zfs(i_flags, &flags, &mode);

        ZFS_ENTER(p_zfsvfs);
        i_error = VOP_CLOSE((vnode_t*)p_vnode, flags, 1, (offset_t)0, (cred_t*)p_cred, NULL);
        VN_RELE((vnode_t*)p_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Create the given directory
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_name: the name of the directory
 * @param mode: the mode for the directory
 * @param p_directory: return the new directory
 * @return 0 on success, the error code overwise
 */
int libzfswrap_mkdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, mode_t mode, inogen_t *p_directory)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode != NULL);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);
        vnode_t *p_vnode = NULL;

        vattr_t vattr = { 0 };
        vattr.va_type = VDIR;
        vattr.va_mode = mode & PERMMASK;
        vattr.va_mask = AT_TYPE | AT_MODE;

        if((i_error = VOP_MKDIR(p_parent_vnode, (char*)psz_name, &vattr, &p_vnode, (cred_t*)p_cred, NULL, 0, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

	p_directory->inode = VTOZ(p_vnode)->z_id;
        p_directory->generation = VTOZ(p_vnode)->z_phys->zp_gen;

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return 0;
}

/**
 * Remove the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_rmdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode);

        i_error = VOP_RMDIR(p_parent_vnode, (char*)psz_filename, NULL, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error == EEXIST ? ENOTEMPTY : i_error;
}

/**
 * Create a symbolic link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_name: the symbolic name
 * @param psz_link: the link content
 * @param p_symlink: the new symlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_symlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, const char *psz_link, inogen_t *p_symlink)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode != NULL);
        // Check generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vattr_t vattr = { 0 };
        vattr.va_type = VLNK;
        vattr.va_mode = 0777;
        vattr.va_mask = AT_TYPE | AT_MODE;

        if((i_error = VOP_SYMLINK(p_parent_vnode, (char*)psz_name, &vattr, (char*) psz_link, (cred_t*)p_cred, NULL, 0)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        vnode_t *p_vnode;
        if((i_error = VOP_LOOKUP(p_parent_vnode, (char*) psz_name, &p_vnode, NULL, 0, NULL, (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        ASSERT(p_vnode != NULL);
        p_symlink->inode = VTOZ(p_vnode)->z_id;
        p_symlink->generation = VTOZ(p_vnode)->z_phys->zp_gen;

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Read the content of a symbolic link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param symlink: the symlink to read
 * @param psz_content: return the content of the symlink
 * @param content_size: size of the buffer
 * @return 0 on success, the error code overwise
 */
int libzfswrap_readlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t symlink, char *psz_content, size_t content_size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);

        if((i_error = zfs_zget(p_zfsvfs, symlink.inode, &p_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode != NULL);
        // Check generation
        if(p_znode->z_phys->zp_gen != symlink.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;
        iovec.iov_base = psz_content;
        iovec.iov_len = content_size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_READLINK(p_vnode, &uio, (cred_t*)p_cred, NULL);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        if(!i_error)
                psz_content[uio.uio_loffset] = '\0';
        else
                psz_content[0] = '\0';

        return i_error;
}

/**
 * Create a hard link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param target: the target object
 * @param psz_name: name of the link
 * @return 0 on success, the error code overwise
 */
int libzfswrap_link(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, inogen_t target, const char *psz_name)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode, *p_target_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        if((i_error = zfs_zget(p_zfsvfs, target.inode, &p_target_znode, B_FALSE)))
        {
                VN_RELE((ZTOV(p_parent_znode)));
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_target_znode);
        // Check the generation
        if(p_target_znode->z_phys->zp_gen != target.generation)
        {
                VN_RELE(ZTOV(p_target_znode));
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        vnode_t *p_target_vnode = ZTOV(p_target_znode);

        i_error = VOP_LINK(p_parent_vnode, p_target_vnode, (char*)psz_name, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_target_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Unlink the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_unlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode);

        i_error = VOP_REMOVE(p_parent_vnode, (char*)psz_filename, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Rename the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the current parent directory
 * @param psz_filename: current name of the file
 * @param new_parent: the new parents directory
 * @param psz_new_filename: new file name
 * @return 0 on success, the error code overwise
 */
int libzfswrap_rename(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, inogen_t new_parent, const char *psz_new_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_parent_znode, *p_new_parent_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        if((i_error = zfs_zget(p_zfsvfs, new_parent.inode, &p_new_parent_znode, B_FALSE)))
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_new_parent_znode);
        // Check the generation
        if(p_new_parent_znode->z_phys->zp_gen != new_parent.generation)
        {
                VN_RELE(ZTOV(p_new_parent_znode));
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }


        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        vnode_t *p_new_parent_vnode = ZTOV(p_new_parent_znode);
        ASSERT(p_parent_vnode);
        ASSERT(p_new_parent_vnode);

        i_error = VOP_RENAME(p_parent_vnode, (char*)psz_filename, p_new_parent_vnode,
                             (char*)psz_new_filename, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_new_parent_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Set the size of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param file: the file to truncate
 * @param size: the new size
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_truncate(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t file, size_t size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, file.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != file.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        flock64_t fl;
        fl.l_whence = 0;        // beginning of the file
        fl.l_start = size;
        fl.l_type = F_WRLCK;
        fl.l_len = (off_t)0;

        i_error = VOP_SPACE(p_vnode, F_FREESP, &fl, FWRITE, 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

