/*
 * Copyright 2008 Eric Anopolsky
 * CDDL or GPL version 2 or later. Take your pick.
 *
 * Credits:
 * Thank you to kantor and Chris in ##c on irc.freenode.net for
 * help understanding the ioctls involved.
 * Thank you to John Hauser and Greg Martyn for testing with
 * real SCSI hardware.
 */

#ifndef FLUSH_H
#define FLUSH_H

#include <sys/zfs_context.h>

int flushwc(vnode_t *vn);

#endif
