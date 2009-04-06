#!/usr/bin/env python2

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


import os
import sys
import socket
import glob

UID = 4711
GID = 4711

def main(treeroot, force):
    if treeroot == "/":
        print "Refusing to use / as treeroot"
        sys.exit(1)
        
    if treeroot[-1] == os.sep: treeroot = treeroot[:-1]
    treeroot += os.sep + "nfs4st"
    if not os.path.exists(treeroot):
        print "Creating", treeroot
        os.mkdir(treeroot)
    
    print "Changing current directory to", treeroot
    os.chdir(treeroot)
    # Sanity check
    if os.getcwd() != treeroot:
        print "Couldn't change to %s, aborting." % treeroot
        sys.exit(1)

    if not force and glob.glob("*"):
        if not glob.glob("src/hello.c"):
            print "Files exists, but src/hello.c does not."
            print "Cowardly refusing to clear directory."
            print "Are you sure you want to use this directory as treeroot?"
            print "Clear it manually, then, or use --force"
            sys.exit(1)

    print "Clearing tree"
    os.system("chmod -R 777 *")
    os.system("rm -rf *")

    print "Creating /dev"
    os.mkdir("dev")
    os.symlink("fd0", "dev/floppy")
    os.system("mknod dev/fd0 b 2 0")
    os.system("mknod dev/ttyS0 c 4 64")
    s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind("dev/log")
    os.mkfifo("dev/initctl")

    print "Creating doc"
    os.mkdir("doc")

    print "Creating doc/README"
    f = open("doc/README", "w")
    f.write("Welcome to this NFS4 server.\n")
    f.write("Enjoy.\n")
    f.close()

    print "Creating directory doc/porting"
    os.mkdir("doc/porting")

    print "Creating doc/porting/TODO"
    f = open("doc/porting/TODO", "w")
    f.write("Need to work on DNIX support...\n")
    f.write("Enjoy.\n")
    f.close()

    print "Creating src"
    os.mkdir("src")

    print "Creating src/hello.c"
    f = open("src/hello.c", "w")
    s = """\
#include <stdio.h>
#include <stdlib.h>

int main()
{
    printf("Hello world!\n");
    exit(0);
}
"""
    f.write(s)
    f.close()

    print "Creating tmp"
    os.mkdir("tmp")
    os.chmod("tmp", 0777)

    print "Creating tmp/gazonk"
    os.mkdir("tmp/gazonk")
    os.chmod("tmp/gazonk", 0777)

    print "Creating tmp/gazonk/foo.c"
    f = open("tmp/gazonk/foo.c", "w")
    f.write(s)
    f.close()

    print "Creating private directory with info.txt"
    os.mkdir("private")
    f = open("private/info.txt", "w")
    f.write("Personal data.\n")
    f.close()
    os.chmod("private", 0000)

    print "Creating symlink src/doc -> ../doc"
    os.symlink("../doc", "src/doc")

    print "Setting owner.group on entire tree to %s.%s" % (UID, GID)
    set_owners(treeroot)


def set_owners(treeroot):
    os.path.walk(treeroot, set_owner, 0)

def set_owner(unused, dirname, names):
    for name in names:
        abs_name = dirname + os.sep + name
        
        os.chown(abs_name, UID, GID)

if __name__ == "__main__":
    print "This program is obsolete. Use test_tree_net.py instead."
    sys.exit(1)
    
    if len(sys.argv) < 2:
        print "Usage: %s <treeroot> [--force]" % sys.argv[0]
        print "Creates tree contents for nfs4st testing"
        print "Directories and files will be created"
        print "under <treeroot>/nfs4st/"
        sys.exit(1)

    if not hasattr(os, "getuid"):
        print "This script (currently) only works on UNIX. Port me."
        sys.exit(1)

    if os.getuid() != 0:
        print "You must be root to run this script"
        sys.exit(1)

    main(sys.argv[1], (sys.argv[2:3] == ["--force"]))

