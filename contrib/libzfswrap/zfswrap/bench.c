#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/zone.h>
#include <grp.h>
#include <pwd.h>
#include <sys/mkdev.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>

#define MAXPATHLEN 32768

#include "format.h"

#include <libzfs.h>
#include <sys/zfs_debug.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>

#include <syslog.h>
#include <time.h>

#include "libzfswrap.h"
#include <sys/zfs_vfsops.h>

extern uint64_t max_arc_size; // defined in arc.c
static const char *cf_pidfile = NULL;
static const char *cf_fuse_mount_options = NULL;
static int cf_disable_block_cache = 0;
static int cf_disable_page_cache = 0;
extern void fuse_unmount_all(); // in fuse_listener.c
static int cf_daemonize = 1;
extern int no_kstat_mount; // kstat.c
extern int zfs_vdev_cache_size; // in lib/libzpool/vdev_cache.c

#define BUFF_SIZE_MAX 16384

size_t stack_size = 0;
char psz_buffer[BUFF_SIZE_MAX];

void read_file(vfs_t *p_vfs, vnode_t *p_vnode, size_t i_size)
{
        size_t offset = 0;
//        size_t read = libzfswrap_read(p_vfs, p_vnode, psz_buffer, i_size, 0, offset);
//        while(read)
        {
                printf("%s", psz_buffer);
                offset += i_size;
//                read = libzfswrap_read(p_vfs, p_vnode, psz_buffer, i_size, 0, offset);
        }
}

int main(int argc, char *argv[])
{
        if(argc != 3)
                return 3;
        const char *psz_filename = argv[1];
        size_t block_size = atoi(argv[2]);
        libzfs_handle_t *zhd = libzfswrap_init();
        if(!zhd)
        {
                printf("Unable to initialize libzfs\n");
                libzfswrap_exit(zhd);
                return 1;
        }

	/* one sane default a day keeps GDB away - Rudd-O */
	zfs_vdev_cache_size = 9ULL << 20;         /* 10MB */

        /** Do some fancy stuffs */
        // Virtually mount the filesystem
        printf("mounting the zpool /tank\n");
        vfs_t *p_vfs = libzfswrap_mount( "tank", "/tank", "");
        if(!p_vfs)
        {
                printf("Unable to mount the zpool\n");
                libzfswrap_exit(zhd);
                return 2;
        }

#if 0
        // Print some statistics about the filesystem
        struct statvfs statvfs;
        if(libzfswrap_statfs(p_vfs, &statvfs))
        {
                libzfswrap_exit(zhd);
                return 2;
        }
        printf("=========\n");
        printf("Block size: %lu\n", statvfs.f_bsize);
        printf("Fragment size: %lu\n", statvfs.f_frsize);
        printf("Number of fragments: %"PRIu64"\n", statvfs.f_blocks);
        printf("Free blocks: %"PRIu64"\n", statvfs.f_bfree);
        printf("Free block (non-root): %"PRIu64"\n", statvfs.f_bavail);
        printf("Inodes: %"PRIu64"\n", statvfs.f_files);
        printf("Free inodes: %"PRIu64"\n", statvfs.f_ffree);
        printf("FS id: %lu\n", statvfs.f_flag);
        printf("Max filename length: %lu\n", statvfs.f_namemax);
        printf("=========\n\n");

        // Lookup for the file
        int i_error, fd, type;
        if((i_error = libzfswrap_lookup(p_vfs, 3, psz_filename, &fd, &type)))
        {
                printf("Unable to lookup for '%s', error %i\n", psz_filename, i_error);
                libzfswrap_exit(zhd);
                return 2;
        }
        printf("Filename(fd) = %s(%i)\n", psz_filename, fd);

        // Open the file
        vnode_t *p_vnode;
        if((i_error = libzfswrap_open(p_vfs, fd, O_RDONLY | O_LARGEFILE, &p_vnode)))
        {
                printf("Unable to open '%s', error %i\n", psz_filename, i_error);
                libzfswrap_exit(zhd);
                return 2;
        }
        printf("\tp_vnode = 0x%jx\n", (uintmax_t)p_vnode);

        // Get file attributes
        struct stat fstat;
        if((i_error = libzfswrap_stat(p_vfs, p_vnode, &fstat)))
        {
                printf("Unable to get attributes from '%s', error: %i\n", psz_filename, i_error);
                libzfswrap_exit(zhd);
                return 2;
        }
        printf("\tDevice: %lu\n", fstat.st_dev);
        printf("\tInode: %lu\n", fstat.st_ino);
        printf("\tMode: %d\n", fstat.st_mode);
        printf("\tLinks: %"PRIu64"\n", fstat.st_nlink);
        printf("\tOwner: %d\n", fstat.st_uid);
        printf("\tGroup: %d\n", fstat.st_gid);
        printf("\tSize: %li\n", fstat.st_size);
        printf("\tBlocks: %"PRIi64"\n", fstat.st_blocks);
        printf("\tBlock size: %"PRIu64"\n", fstat.st_blksize);
        printf("\tatime: %s", ctime(&fstat.st_atime));
        printf("\tmtime: %s", ctime(&fstat.st_mtime));
        printf("\tctime: %s", ctime(&fstat.st_ctime));

        // Read the file
        read_file(p_vfs, p_vnode, block_size);

        // Close the file
        if((i_error = libzfswrap_close(p_vfs, p_vnode, O_RDONLY | O_LARGEFILE)))
        {
                printf("Unable to close the file '%s', error %i\n", psz_filename, i_error);
                libzfswrap_exit(zhd);
                return 2;
        }


        // Get file attributes
        printf("Second attempt (file closed)\nFilename(fd) = %s(%i)\n", psz_filename, fd);

        if((i_error = libzfswrap_statfd(p_vfs, fd, &fstat, NULL)))
        {
                printf("Unable to get attributes from '%s', error: %i\n", psz_filename, i_error);
                libzfswrap_exit(zhd);
                return 2;
        }
        printf("\tDevice: %lu\n", fstat.st_dev);
        printf("\tInode: %lu\n", fstat.st_ino);
        printf("\tMode: %d\n", fstat.st_mode);
        printf("\tLinks: %"PRIu64"\n", fstat.st_nlink);
        printf("\tOwner: %d\n", fstat.st_uid);
        printf("\tGroup: %d\n", fstat.st_gid);
        printf("\tSize: %li\n", fstat.st_size);
        printf("\tBlocks: %"PRIi64"\n", fstat.st_blocks);
        printf("\tBlock size: %"PRIu64"\n", fstat.st_blksize);
        printf("\tatime: %s", ctime(&fstat.st_atime));
        printf("\tmtime: %s", ctime(&fstat.st_mtime));
        printf("\tctime: %s", ctime(&fstat.st_ctime));
////
#endif

        // Open the root directory
        int i_error;
        creden_t cred = { .uid = 0, .gid = 0};
        vnode_t *p_vnode;
        inogen_t root;
        libzfswrap_getroot(p_vfs, &root);

        if((i_error = libzfswrap_opendir(p_vfs, &cred, root, &p_vnode)))
        {
                printf("Unable to open the root directory: %i\n", i_error);
                libzfswrap_exit(zhd);
                return 2;
        }
        libzfswrap_entry_t *entries = calloc(10, sizeof(libzfswrap_entry_t));
        off_t cookie = 0;
        int index = 0;

        if((i_error = libzfswrap_readdir(p_vfs, &cred, p_vnode, entries, 10, &cookie)))
        {
                printf("Unable to read the directory: %i\n", i_error);
                libzfswrap_exit(zhd);
                return 2;
        }

        printf("\n\n<directory>\n");
        while(cookie != 0)
        {
                for(int i=0; i < 10; i++)
                {
                        if(entries[i].psz_filename[0] == '\0')
                                break;
                        printf("\t<entry %i (%"PRIu64")>%s</entry>\n", index++,
                               entries[i].object.inode, entries[i].psz_filename);
                }
                if((i_error = libzfswrap_readdir(p_vfs, &cred, p_vnode, entries, 10, &cookie)))
                {
                        printf("Unable to read the directory: %i\n", i_error);
                        libzfswrap_exit(zhd);
                        return 2;
                }
        }
        for(int i=0; i < 10; i++)
        {
                if(entries[i].psz_filename[0] == '\0')
                        break;
                printf("\t<entry %i (%"PRIu64")>%s</entry>\n", index++,
                       entries[i].object.inode, entries[i].psz_filename);
        }
        printf("</directory>\n");

        zfsvfs_t *p_zfsvfs = p_vfs->vfs_data;
        if(p_zfsvfs->z_ctldir)
                printf(".zfs directory does exist\n");
        else
                printf("the .zfs directory does not exist\n");

//        libzfswrap_snapshot(zhd, "tank");

#if 0
        /* Create a directory 'test' */
        libzfswrap_mkdir(p_vfs, 3, "test", 0123, &fd);
        /* Create an empty file */
        libzfswrap_create(p_vfs, 3, "empty_file", 0234, &fd);

        /* Create a symlink */
        libzfswrap_symlink(p_vfs, 3, "un_sym_link", "eth0:192.168.122.185", &fd);

        /* Read it */
        char psz_content[256];
        libzfswrap_readlink(p_vfs, fd, psz_content, 256);
        printf("'un_sym_link' => '%s'\n", psz_content);


        /* Destroy the directory called 'dossier' */
        libzfswrap_rmdir(p_vfs, 3, "dossier");

        int fd;
        char psz_file[10];
        for(int i = 0; i < atoi(argv[2]); i++)
        {
                sprintf(psz_file, "%u", i);
                libzfswrap_create(p_vfs, 3, psz_file, 0644, &fd);
                libzfswrap_unlink(p_vfs, 3, psz_file);
        }

        inogen_t fd;
        inogen_t root = {.inode=3, .generation=0};
        libzfswrap_create(p_vfs, root, "empty_file", 0234, &fd);
#endif
        libzfswrap_umount(p_vfs, 1);
        libzfswrap_exit(zhd);
	return 0;
}
