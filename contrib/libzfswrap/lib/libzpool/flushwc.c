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

//It shouldn't be necessary to include stddef.h explicitly, but
//on my machine scsi/sg.h errors out without it.
#include <stddef.h>
#include <scsi/sg.h>
#include <string.h>
#include <linux/hdreg.h>
#include <linux/major.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <sys/zfs_context.h>

#if !defined(_KERNEL) && defined(ioctl)
#undef ioctl
#define ioctl real_ioctl
#endif

/*
 * This function flushes the write cache on a SCSI or SATA drive.
 * It issues a SCSI command using the SG_IO ioctl, and the libata
 * driver used for SATA devices translates some SCSI commands
 * automatically. Command 0x35--SYNCHRONIZE CACHE(10)--is one of
 * those commands.
 *
 * A return value of 0 indicates success.
 * A return value other than 0 indicates failure.
 */
static int flushSCSIwc(int fd) {
  /*
   * Taken together, these three variables make up the data
   * structure used to talk to the SCSI disk driver via the SG_IO ioctl.
   */
  struct sg_io_hdr io_hdr;
  unsigned char sense_b[32];
  unsigned char cmdp[10];

  memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
  memset(sense_b, 0, sizeof(sense_b));
  memset(cmdp, 0, sizeof(cmdp));

  io_hdr.interface_id = 'S';
  io_hdr.dxfer_direction = SG_DXFER_NONE;
  cmdp[0] = 0x35; // This is a SYNCHRONIZE CACHE(10) command.
  cmdp[1] = 0x00; // This command should be synchronous.
  cmdp[2] = 0x00; // 2-5 are the starting LBA.
  cmdp[3] = 0x00;
  cmdp[4] = 0x00;
  cmdp[5] = 0x00;
  cmdp[6] = 0x00; // No group number.
  cmdp[7] = 0x00; // 7-8 Synchronize ALL blocks.
  cmdp[8] = 0x00;
  cmdp[9] = 0x00; // Nothing special in the control field.
  io_hdr.cmdp = cmdp;
  io_hdr.cmd_len = sizeof(cmdp);
  io_hdr.sbp = sense_b;
  io_hdr.mx_sb_len = sizeof(sense_b);
  io_hdr.timeout = 60000; //Give the command 60,000ms to complete.

  if(ioctl(fd, SG_IO, &io_hdr) == -1)
    /* The ioctl failed. The only time this should happen
     * is if the sg driver objects to the io_hdr structure we're
     * sending to it.
     */
    return EIO;

  if(io_hdr.status != 0)
    /* The device somehow objected to having its cache flushed.
     * Support for IMMEDiate flushing was determined when
     * the device was first accessed, so that's almost certainly not
     * the issue. The device may return status not equal to 0 if
     * there was a write error committing data from the volatile
     * (or nonvolatile) cache to permanent storage.
     * If you need more information about the kind of failure, look
     * at the sense data in sense_b. For now, it doesn't really matter
     * why the cache flush failed.
     */
    return EIO;

  // Everything went fine.
  return 0;
}

/* This function flushes the write cache on an ATA drive.
 * It could be used for both old-style (P)ATA drives and newer
 * SATA drives, but it will only be called for the former.
 *
 * A return value of 0 indicates success.
 * A return value other than 0 indicates failure.
 *
 * For now, a return value of -1 means failure. Do not depend on
 * this, as failure values may change in the future to describe
 * the nature of the failure.
 */
static int flushATAwc(int fd) {
  unsigned char ata_command[4];

  ata_command[0] = WIN_FLUSH_CACHE;
  ata_command[1] = 0;
  ata_command[2] = 0;
  ata_command[3] = 0;

  return ioctl(fd, HDIO_DRIVE_CMD, ata_command) == 0 ? 0 : EIO;
}

/*
 * This function flushes the write cache on ATA, SATA, and SCSI
 * hard drives. It is mostly a wrapper, choosing between flushATAwc and
 * flushSCSIwc.
 *
 * A return value of 0 indicates success.
 * A return value other than 0 indicates failure.
 *
 * For now, a return value of 1 means failure. Do not depend on
 * this, as failure values may change in the future to describe
 * the nature of the failure.
 */
int flushwc(vnode_t *vn) {
  int major_number;

  if(!S_ISBLK(vn->v_stat.st_mode))
    // We can only flush the write cache of a block device.
    return ENOTSUP;

  major_number = major(vn->v_stat.st_rdev);

  switch(major_number) {
  case SCSI_DISK0_MAJOR:
  case SCSI_DISK1_MAJOR:
  case SCSI_DISK2_MAJOR:
  case SCSI_DISK3_MAJOR:
  case SCSI_DISK4_MAJOR:
  case SCSI_DISK5_MAJOR:
  case SCSI_DISK6_MAJOR:
  case SCSI_DISK7_MAJOR:
  case SCSI_DISK8_MAJOR:
  case SCSI_DISK9_MAJOR:
  case SCSI_DISK10_MAJOR:
  case SCSI_DISK11_MAJOR:
  case SCSI_DISK12_MAJOR:
  case SCSI_DISK13_MAJOR:
  case SCSI_DISK14_MAJOR:
  case SCSI_DISK15_MAJOR:
    return flushSCSIwc(vn->v_fd);
  case IDE0_MAJOR:
  case IDE1_MAJOR:
  case IDE2_MAJOR:
  case IDE3_MAJOR:
  case IDE4_MAJOR:
  case IDE5_MAJOR:
  case IDE6_MAJOR:
  case IDE7_MAJOR:
  case IDE8_MAJOR:
  case IDE9_MAJOR:
    return flushATAwc(vn->v_fd);
  default:
    //Unknown block device driver. Can't flush the write cache.
    return ENOTSUP;
  }
}
