/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Varun Chandramohan <varunc@linux.vnet.ibm.com>
 *              Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *              Chandra Seetharaman <sekharan@us.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "../include/handle.h"

MODULE_LICENSE("GPL");

#define openbyhandle_devname "openhandle_dev"

int openhandle_open(struct inode *inode, struct file *filp);
int openhandle_release(struct inode *inode, struct file *filp);
long openhandle_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

struct file_operations openhandle_fops = {
    .owner = THIS_MODULE,
    .open = openhandle_open,
    .release = openhandle_release,
    .unlocked_ioctl = openhandle_ioctl,
};

long openhandle_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval = -ENOSYS;
    struct open_arg oarg;
    struct link_arg linkarg;
    struct name_handle_arg harg;
    struct readlink_arg readlinkarg;
    struct stat_arg statarg;

    switch (cmd)
        {
        case OPENHANDLE_NAME_TO_HANDLE:
            if(copy_from_user(&harg, (void *)arg, sizeof(struct name_handle_arg)))
                return -EFAULT;
            retval = name_to_handle_at(harg.dfd, harg.name, harg.handle, harg.flag);
            break;
        case OPENHANDLE_OPEN_BY_HANDLE:
            if(copy_from_user(&oarg, (void *)arg, sizeof(struct open_arg)))
                return -EFAULT;
            retval = open_by_handle(oarg.mountdirfd, oarg.handle, oarg.flags);
            break;
        case OPENHANDLE_LINK_BY_FD:
            if(copy_from_user(&linkarg, (void *)arg, sizeof(struct link_arg)))
                return -EFAULT;
            retval = link_by_fd(linkarg.file_fd, linkarg.dir_fd, linkarg.name);
            break;
        case OPENHANDLE_READLINK_BY_FD:
            if(copy_from_user(&readlinkarg, (void *)arg, sizeof(struct readlink_arg)))
                return -EFAULT;
            retval = readlink_by_fd(readlinkarg.fd, readlinkarg.buffer, readlinkarg.size);
            break;
        case OPENHANDLE_STAT_BY_HANDLE:
            if (copy_from_user(&statarg, (void *)arg, sizeof(struct stat_arg)))
                return -EFAULT;
            retval = stat_by_handle(statarg.mountdirfd, statarg.handle, statarg.buf);
        default:
            break;
        }
    return retval;
}

int openhandle_open(struct inode *inode, struct file *filp)
{
    return 0;                     /* success */
}

int openhandle_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct class *openbyhandle_class;
static struct device *openbyhandle_dev;
static int major;

int init_module()
{
    void *ptr_err;

    major = register_chrdev(0, openbyhandle_devname, &openhandle_fops);
    if(major < 0)
        {
            printk("Can't get major number, error %d\n", major);
            return major;
        }

    openbyhandle_class = class_create(THIS_MODULE, openbyhandle_devname);
    ptr_err = openbyhandle_class;
    if(IS_ERR(ptr_err))
        goto erro2;

    openbyhandle_dev = device_create(openbyhandle_class, NULL,
                               MKDEV(major, 0), NULL, openbyhandle_devname);
    ptr_err = openbyhandle_dev;
    if(IS_ERR(ptr_err))
        goto erro1;

    printk("device registered with major number %d\n", major);
    return 0;

erro1:
    class_destroy(openbyhandle_class);
erro2:
    unregister_chrdev(major, openbyhandle_devname);
    return PTR_ERR(ptr_err);
}

void cleanup_module()
{
    device_destroy(openbyhandle_class, MKDEV(major, 0));
    class_destroy(openbyhandle_class);
    unregister_chrdev(major, openbyhandle_devname);
    return;
}
