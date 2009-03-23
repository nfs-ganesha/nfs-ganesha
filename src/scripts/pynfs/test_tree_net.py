#!/usr/bin/env python2.2

# pynfs - Python NFS4 tools
#
# Written by Peter Åstrand <peter@cendio.se>
# Copyright (C) 2001 Cendio Systems AB (http://www.cendio.se)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License. 
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# TODO: Clean up. Support TCP. More error checks. 

import sys, os
import nfs4lib
from nfs4constants import *
from nfs4types import *

def clean_dir(directory):
    fh = ncl.do_getfh(directory)
    entries = ncl.do_readdir(fh)
    names = [entry.name for entry in entries]

    for name in names:
        lookup_dir_ops = ncl.lookup_path(directory)
        operations = [ncl.putrootfh_op()] + lookup_dir_ops
        operations.append(ncl.remove_op(name))
        res = ncl.compound(operations)

        if res.status == NFS4ERR_NOTEMPTY:
            # Recursive deletion
            clean_dir(directory + [name])
            # Remove dir itself
            lookup_dir_ops = ncl.lookup_path(directory)
            operations = [ncl.putrootfh_op()] + lookup_dir_ops
            operations.append(ncl.remove_op(name))
            res = ncl.compound(operations)
            nfs4lib.check_result(res)
        elif not res.status == NFS4_OK:
            raise "Cannot clean directory %s" % directory

    # Verify that all files were removed
    entries = ncl.do_readdir(fh)
    if entries:
        raise "Cannot clean directory %s" % directory

def create_dir(ncl, curdir, create_dir):
    operations = [ncl.putrootfh_op()] + ncl.lookup_path(curdir)
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, create_dir)
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

def create_leading_paths(ncl, prefix):
    for x in range(len(prefix)):
        leading_comps = prefix[0:x+1]
        curdir = leading_comps[:-1]
        lastcomp = leading_comps[-1]
        fh = ncl.do_getfh(curdir)
        entries = ncl.do_readdir(fh)
        names = [entry.name for entry in entries]
        if not lastcomp in names:
            create_dir(ncl, curdir, lastcomp)

def main(ncl, unix_prefix):
    ncl.init_connection()
    prefix = nfs4lib.unixpath2comps(unix_prefix)
    putrootfhop = ncl.putrootfh_op()

    lookup_treeroot = [putrootfhop] + ncl.lookup_path(prefix)
    lookup_dev = lookup_treeroot + ncl.lookup_path(["dev"])
    lookup_doc = lookup_treeroot + ncl.lookup_path(["doc"])
    lookup_src = lookup_treeroot + ncl.lookup_path(["src"])
    lookup_tmp = lookup_treeroot + ncl.lookup_path(["tmp"])
    lookup_private = lookup_treeroot + ncl.lookup_path(["private"])

    print "Creating path", unix_prefix
    create_leading_paths(ncl, prefix)

    print "Clearing", unix_prefix
    clean_dir(prefix)

    print "Creating /dev"
    operations = lookup_treeroot[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "dev")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /dev/floppy"
    operations = lookup_dev[:]
    objtype = createtype4(ncl, type=NF4LNK, linkdata="fd0")
    createop = ncl.create(objtype, "floppy")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /dev/fd0"
    operations = lookup_dev[:]
    devdata = specdata4(ncl, 2, 0)
    objtype = createtype4(ncl, type=NF4BLK, devdata=devdata)
    createop = ncl.create(objtype, "fd0")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /dev/ttyS0"
    operations = lookup_dev[:]
    devdata = specdata4(ncl, 4, 64)
    objtype = createtype4(ncl, type=NF4CHR, devdata=devdata)
    createop = ncl.create(objtype, "ttyS0")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /dev/log"
    operations = lookup_dev[:]
    objtype = createtype4(ncl, type=NF4SOCK)
    createop = ncl.create(objtype, "log")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /dev/initctl"
    operations = lookup_dev[:]
    objtype = createtype4(ncl, type=NF4FIFO)
    createop = ncl.create(objtype, "initctl")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /doc"
    operations = lookup_treeroot[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "doc")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating /doc/README"
    remote = nfs4lib.NFS4OpenFile(ncl)
    remote.open(os.path.join(unix_prefix, "doc/README"), "w")
    data ="""\
    Welcome to this NFS4 server.
    Enjoy.
    """
    remote.write(data)
    remote.close()

    print "Creating directory doc/porting"
    operations = lookup_doc[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "porting")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating doc/porting/TODO"
    remote = nfs4lib.NFS4OpenFile(ncl)
    remote.open(os.path.join(unix_prefix, "doc/porting/TODO"), "w")
    data ="""\
    Need to work on DNIX support...
    Enjoy.
    """
    remote.write(data)
    remote.close()

    print "Creating src"
    operations = lookup_treeroot[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "src")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating src/hello.c"
    remote = nfs4lib.NFS4OpenFile(ncl)
    remote.open(os.path.join(unix_prefix, "src/hello.c"), "w")
    data = """\
    #include <stdio.h>
    #include <stdlib.h>

    int main()
    {
        printf("Hello world!\\n");
        exit(0);
    }
    """
    remote.write(data)
    remote.close()

    print "Creating tmp"
    operations = lookup_treeroot[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "tmp")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating tmp/gazonk"
    operations = lookup_tmp[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "gazonk")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating tmp/gazonk/foo.c"
    remote = nfs4lib.NFS4OpenFile(ncl)
    remote.open(os.path.join(unix_prefix, "tmp/gazonk/foo.c"), "w")
    data = """\
    #include <stdio.h>
    #include <stdlib.h>

    int main()
    {
        printf("Hello world!\\n");
        exit(0);
    }
    """
    remote.write(data)
    remote.close()

    print "Creating directory private"
    operations = lookup_treeroot[:]
    objtype = createtype4(ncl, type=NF4DIR)
    createop = ncl.create(objtype, "private")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)

    print "Creating private/info.txt"
    remote = nfs4lib.NFS4OpenFile(ncl)
    remote.open(os.path.join(unix_prefix, "private/info.txt"), "w")
    remote.write("Personal data.\n")
    remote.close()

    print "Changing UNIX permissions of private dir to 0000"
    operations = lookup_private[:]
    stateid = stateid4(ncl, 0, "")
    attrmask = nfs4lib.list2attrmask([FATTR4_MODE])
    dummy_ncl = nfs4lib.DummyNcl()
    dummy_ncl.packer.pack_uint(0000)
    attr_vals = dummy_ncl.packer.get_buffer()
    obj_attributes = fattr4(ncl, attrmask, attr_vals)
    operations.append(ncl.setattr_op(stateid, obj_attributes))

    res = ncl.compound(operations)
    if res.status == NFS4ERR_ATTRNOTSUPP:
        print "UNIX mode attribute not supported"
    else:
        nfs4lib.check_result(res)

    print "Creating symlink src/doc -> ../doc"
    operations = lookup_src[:]
    objtype = createtype4(ncl, type=NF4LNK, linkdata="../doc")
    createop = ncl.create(objtype, "doc")
    operations.append(createop)
    res = ncl.compound(operations)
    nfs4lib.check_result(res)


def usage():
    print "Usage: %s [nfs://]host[:[port]]<prefix>" % sys.argv[0]
    print "Creates tree contents on server for nfs4st testing"
    print "Directories and files will be created"
    print "under <server><prefix>/nfs4st/"
    print
    print "<prefix> defaults to /, e.g. the directory nfs4st is"
    print "created directly under the server root"
    sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        usage()

    parse_result = nfs4lib.parse_nfs_url(sys.argv[1])
    if not parse_result:
        usage()

    (host, portstring, directory) = parse_result

    if portstring:
        port = int(portstring)
    else:
        port = nfs4lib.NFS_PORT

    if not directory:
        directory = "/"

    directory = os.path.join(directory, "nfs4st_tree")
        
    ncl = nfs4lib.create_client(host, 2049, "udp", uid=0, gid=0)
    main(ncl, directory)

