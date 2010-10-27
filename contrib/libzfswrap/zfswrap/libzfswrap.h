#ifndef LIBZFSWRAP_H
#define LIBZFSWRAP_H

#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/** Reprensentation of a file system object */
typedef struct
{
        /** Object inode */
        uint64_t inode;
        /** Object generation */
        uint64_t generation;
}inogen_t;

/** Representation of a directory entry */
typedef struct
{
        /** Object name */
        char psz_filename[256];
        /** Object representation */
        inogen_t object;
        /** Object type */
        int type;
        /** Obejct attributes */
        struct stat stats;
}libzfswrap_entry_t;

/** Representation of the user rights */
typedef struct
{
        /** User identifier */
        uid_t uid;
        /** Group identifier */
        gid_t gid;
}creden_t;

/** libzfswrap library handle */
typedef struct libzfs_handle_t libzfswrap_handle_t;
/** Virtual file system handle */
typedef struct vfs_t libzfswrap_vfs_t;
/** Virtual node handle */
typedef struct libzfswrap_vnode_t libzfswrap_vnode_t;


/** Object mode */
#define LZFSW_ATTR_MODE         (1 << 0)
/** Owner user identifier */
#define LZFSW_ATTR_UID          (1 << 1)
/** Group identifier */
#define LZFSW_ATTR_GID          (1 << 2)
/** Access time */
#define LZFSW_ATTR_ATIME        (1 << 3)
/** Modification time */
#define LZFSW_ATTR_MTIME        (1 << 4)


/**
 * Initialize the libzfswrap library
 * @return a handle to the library, NULL in case of error
 */
libzfswrap_handle_t *libzfswrap_init();

/**
 * Uninitialize the library
 * @param p_zhd: the libzfswrap handle
 */
void libzfswrap_exit(libzfswrap_handle_t *p_zhd);

/**
 * Create a zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_name: the name of the zpool
 * @param psz_type: type of the zpool (mirror, raidz, raidz([1,255])
 * @param ppsz_error: the error message (if any)
 * @return 0 on success, the error code overwise
 */
int libzfswrap_zpool_create(libzfswrap_handle_t *p_zhd, const char *psz_name, const char *psz_type,
                            const char **ppsz_dev, size_t i_dev, const char **ppsz_error);

/**
 * Destroy the given zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_name: zpool name
 * @param b_force: force the unmount process or not
 * @param ppsz_error: the error message (if any)
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_destroy(libzfswrap_handle_t *p_zhd, const char *psz_name, int b_force,
                             const char **ppsz_error);

/**
 * Add to the given zpool the following device
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_type: type of the device group to add
 * @param ppsz_dev: the list of devices
 * @param i_dev: the number of devices
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_add(libzfswrap_handle_t *p_zhd, const char *psz_zpool,
                         const char *psz_type, const char **ppsz_dev,
                         size_t i_dev, const char **ppsz_error);

/**
 * Remove the given vdevs from the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param ppsz_vdevs: the vdevs
 * @param i_vdevs: the number of vdevs
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_remove(libzfswrap_handle_t *p_zhd, const char *psz_zpool,
                            const char **ppsz_dev, size_t i_vdevs,
                            const char **ppsz_error);

/**
 * Attach the given device to the given vdev in the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_current_dev: the device to use as an attachment point
 * @param psz_new_dev: the device to attach
 * @param i_replacing: do we have to attach or replace ?
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_attach(libzfswrap_handle_t *p_zhd, const char *psz_zpool,
                            const char *psz_current_dev, const char *psz_new_dev,
                            int i_replacing, const char **ppsz_error);

/**
 * Detach the given vdevs from the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_dev: the device to detach
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error message overwise
 */
int libzfswrap_zpool_detach(libzfswrap_handle_t *p_zhd, const char *psz_zpool, const char *psz_dev, const char **ppsz_error);

/**
 * List the available zpools
 * @param p_zhd: the libzfswrap handle
 * @param psz_props: the properties to retrieve
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_list(libzfswrap_handle_t *p_zhd, const char *psz_props, const char **ppsz_error);

/**
 * Print the status of the available zpools
 * @param p_zhd: the libzfswrap handle
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_status(libzfswrap_handle_t *p_zhd, const char **ppsz_error);


/**
 * Print the list of ZFS file systems and properties
 * @param p_zhd: the libzfswrap handle
 * @param psz_props: the properties to retrieve
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_list(libzfswrap_handle_t *p_zhd, const char *psz_props, const char **ppsz_error);

/**
 * List the available snapshots for the given zfs
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_list_snapshot(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char **ppsz_error);

/**
 * Return the list of snapshots for the given zfs in an array of strings
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param pppsz_snapshots: the array of snapshots names
 * @param ppsz_error: the error message if any
 * @return the number of snapshots in case of success, -1 overwise
 */
int libzfswrap_zfs_get_list_snapshots(libzfswrap_handle_t *p_zhd, const char *psz_zfs, char ***pppsz_snapshots, const char **ppsz_error);

/**
 * Create a snapshot of the given ZFS file system
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param psz_snapshot: name of the snapshot
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_snapshot(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char *psz_snapshot, const char **ppsz_error);

/**
 * Destroy a snapshot of the given ZFS file system
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param psz_snapshot: name of the snapshot
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_snapshot_destroy(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char *psz_snapshot, const char **ppsz_error);

/**
 * Mount the given file system
 * @param psz_zpool: the pool to mount
 * @param psz_dir: the directory to mount
 * @param psz_options: options for the mounting point
 * @return the vitual file system
 */
libzfswrap_vfs_t *libzfswrap_mount(const char *psz_zpool, const char *psz_dir, const char *psz_options);

/**
 * Get the root object of a file system
 * @param p_vfs: the virtual filesystem
 * @param p_root: return the root object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getroot(libzfswrap_vfs_t *p_vfs, inogen_t *p_root);


/**
 * Unmount the given file system
 * @param p_vfs: the virtual file system
 * @param b_force: force the unmount ?
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_umount(libzfswrap_vfs_t *p_vfs, int b_force);

/**
 * Get some more informations about the file system
 * @param p_vfs: the virtual file system
 * @param p_stats: the statistics
 * @return 0 in case of success, -1 overwise
 */
int libzfswrap_statfs(libzfswrap_vfs_t *p_vfs, struct statvfs *p_stats);

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
int libzfswrap_lookup(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, inogen_t *p_object, int *p_type);

/**
 * Test the access right of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param mask: the rights to check
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_access(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int mask);

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
int libzfswrap_create(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, mode_t mode, inogen_t *p_file);

/**
 * Open the given object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object to open
 * @param i_flags: the opening flags
 * @param pp_vnode: the vnode to return
 * @return 0 on success, the error code overwise
 */
int libzfswrap_open(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int i_flags, libzfswrap_vnode_t **pp_vnode);

/**
 * Close the given vnode
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode to close
 * @param i_flags: the flags given when opening
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_close(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, int i_flags);

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
int libzfswrap_read(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset);

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
int libzfswrap_write(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset);

/**
 * Get the stat about a file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_stat: the stat struct to fill in
 * @return 0 on success, the error code overwise
 */
int libzfswrap_stat(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, struct stat *p_stat);

/**
 * Get the attributes of an object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param p_stat: the attributes to fill
 * @param p_type: return the type of the object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int *p_type);

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
int libzfswrap_setattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int flags, struct stat *p_new_stat);

/**
 * List the extended attributes
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param ppsz_buffer: the buffer to fill with the list of attributes
 * @param p_size: will contain the size of the buffer
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_listxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, char **ppsz_buffer, size_t *p_size);

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
int libzfswrap_setxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, const char *psz_value);

/**
 * Get the value for the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @param ppsz_value: the value
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_getxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, char **ppsz_value);

/**
 * Remove the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_removexattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key);

/**
 * Open a directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param directory: the directory to open
 * @param pp_vnode: the vnode to return
 * @return 0 on success, the error code overwise
 */
int libzfswrap_opendir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t directory, libzfswrap_vnode_t **pp_vnode);

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
int libzfswrap_readdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, libzfswrap_entry_t *p_entries, size_t size, off_t *cookie);

/**
 * Close the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @return 0 on success, the error code overwise
 */
int libzfswrap_closedir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode);

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
int libzfswrap_mkdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, mode_t mode, inogen_t *p_directory);

/**
 * Remove the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_rmdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename);

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
int libzfswrap_symlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, const char *psz_link, inogen_t *p_symlink);

/**
 * Read the content of a symbolic link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param symlink: the symlink to read
 * @param psz_content: return the content of the symlink
 * @param content_size: size of the buffer
 * @return 0 on success, the error code overwise
 */
int libzfswrap_readlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t symlink, char *psz_content, size_t content_size);

/**
 * Create a hard link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param target: the target object
 * @param psz_name: name of the link
 * @return 0 on success, the error code overwise
 */
int libzfswrap_link(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, inogen_t target, const char *psz_name);

/**
 * Unlink the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_unlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename);

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
int libzfswrap_rename(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, inogen_t new_parent, const char *psz_new_filename);

/**
 * Set the size of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param file: the file to truncate
 * @param size: the new size
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_truncate(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t file, size_t size);

#endif //LIBZFSWRAP_H
