#!/usr/bin/python3

# Usually ganesha daemon start up time is used as its epoch. This is
# fine for a single node configurations, but in clustered environment,
# ganesha daemon will have to distinguish itself from instances running
# on other nodes. This is not possible if ganesha daemon is started at
# the same time on multiple cluster nodes.
#
# The epoch will have to be a 32 bit number. We use nodeid in the most
# significant 16 bits and a generation number in the least significant
# 16 bits. Current generation number is written to epoch_file.

import os, sys, re, fcntl
from subprocess import Popen, PIPE
from ctypes import c_int, c_long, pointer, POINTER, Structure

epoch_file = "/var/lib/nfs/ganesha/gpfs-epoch"
def main():
    genid = get_genid() + 1 # next genid
    genid &= 0xFFFF         # handle 16 bit overflow
    put_genid(genid)        # store the genid
    nodeid = get_nodeid()
    epoch = nodeid << 16 | genid
    print(epoch)

def get_genid():
    try:
        with open(epoch_file, "r") as f:
            genid = int(f.read())
    except Exception:
        genid = 0

    return genid

def put_genid(genid):
    with open(epoch_file, "w+") as f:
        f.write("%s" % genid)

def get_mount():
    output = Popen("mount", shell=True, stdout=PIPE).communicate()[0].decode()
    for line in output.splitlines():
        # the mount output line format is
        #
        # dev on mountpoint type fstype (options)
        #
        # Easier with split, but let us handle mount point names that
        # may include strange characters like space! Take anything from
        # "on" to "type gpfs" as a mount point!
        mo = re.search(r"\bon\b(.+)\btype gpfs\b", line)
        if mo:
            return mo.group(1).strip()

    return None

# From gpfs headers! C code is probably less error prone for this!
#
# struct kxArgs { signed long arg1; signed long arg2 }
# struct grace_period_arg { int mountdirfd, int grace_sec }
# Note that struct grace_period_arg is used for GET_NODEID!

class GracePeriodArg(Structure):
    _fields_ = [("mountdirfd", c_int), ("grace_sec", c_int)]

class KxArgs(Structure):
    _fields_ = [("arg1", c_long), ("arg2", POINTER(GracePeriodArg))]

def get_nodeid():
    # GPFS FSAL constants
    GPFS_DEVNAMEX = "/dev/ss0"
    kGanesha = 140
    OPENHANDLE_GET_NODEID = 125

    gpfs_fd = os.open(GPFS_DEVNAMEX, os.O_RDONLY)
    gpfs_mount = get_mount()
    mountdirfd = os.open(gpfs_mount, os.O_RDONLY|os.O_DIRECTORY)

    gpa = GracePeriodArg(mountdirfd, 0)
    kxarg = KxArgs(c_long(OPENHANDLE_GET_NODEID), pointer(gpa))
    return fcntl.ioctl(gpfs_fd, kGanesha, kxarg)

if __name__ == "__main__":
    import traceback
    import syslog
    try:
        main()
    except:
        syslog.syslog(traceback.format_exc())
        sys.exit(1)
