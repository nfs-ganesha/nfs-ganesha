#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2018 IBM
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Author: Malahal Naineni <malahal@us.ibm.com>

# Convert given Linux kernel NFS server exports into Ganesha exports.
#
# Typical usage:
# cat /etc/exports | knfs2ganesha-exports > /etc/ganesha/ganesha.conf
# cat /etc/exports | knfs2ganesha-exports --fsal gpfs > /etc/ganesha/ganesha.conf
#
# NOTE: default options with DASH/HYPHEN are not implemented!

from __future__ import print_function
import sys, os
import pyparsing as pp
import subprocess

# List of all export paths processed are kept in gan_paths dictionary
gan_paths = {}
export_id = 0 # increment before use

LPAREN = pp.Literal("(").suppress()
RPAREN = pp.Literal(")").suppress()

# export path can be quoted to include white space!
ppath = pp.QuotedString(quoteChar="'", escChar='\\',
                        unquoteResults=False,
                        convertWhitespaceEscapes=False)
ppath = pp.Or([ppath, pp.QuotedString(quoteChar='"', escChar='\\',
                                      unquoteResults=False,
                                      convertWhitespaceEscapes=False)])
ppath = pp.Or([ppath, pp.Word(pp.alphanums+"_-/.+")]).setResultsName("path")

phost = pp.Word(pp.alphanums+".@+*").setResultsName("host")
phost_opts = (LPAREN + pp.CharsNotIn(')') + RPAREN).setResultsName("host_opts")
phost_block = pp.Group(phost + phost_opts)
pblock = ppath + pp.OneOrMore(phost_block)

def process_exports(fsalname):
    for line in sys.stdin:
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        p = pblock.ignore(pp.pythonStyleComment).parseString(line, parseAll=True)
        path = p["path"]
        host_blocks = p[1:]
        for host_block in host_blocks:
            host = host_block["host"]
            opts = host_block["host_opts"]
            if opts: # Not supposed as we don't support DASH based default options
                opts = opts[0]
            else:
                opts = ""
            #print(path, host, opts)
            pairs = process_opts(opts)
            create_client(path, host, pairs, fsalname)

# Takes a knfs options string and converts to Ganesha
# key,value pairs and returns a key,value list
def process_opts(opts):
    # Set to defaults
    access_value = "ro"
    squash_value = "root_squash"
    sec_value = "sys"

    pairs = []
    opts = opts.split(",")
    opts = [x.strip() for x in opts]
    for opt in opts:
        if opt in ["ro", "rw"]:
            access_value = opt
        elif opt in ["no_root_squash", "all_squash", "root_squash"]:
            squash_value = opt
        elif opt.startswith("sec"):
            auth = opt.split("=")[1]
            auth = auth.split(":")
            sec_value = ",".join(auth)
        elif opt in ["insecure", "sync", "no_subtree_check"]:
            pass
        elif opt == "async":
            sys.exit("async option is not supported in NFS-Ganesha, "
                     "more over, you shouldn't be using it if you "
                     "care about your data!\n"
                     "Please remove this option and re-run the program")
        elif opt == "subtree_check":
            sys.exit("subtree_check option is not supported in NFS-Ganesha.\n"
                     "Please remove this option and re-run the program")
        elif opt.startswith("fsid"):
            pass
        elif opt.startswith("anonuid"):
            uid = opt.split("=")[1].strip()
            pairs.append(("--Anonymous_uid", uid))
        elif opt.startswith("anongid"):
            gid = opt.split("=")[1].strip()
            pairs.append(("--Anonymous_gid", gid))
        else:
            sys.exit("Unknown option: %s, exiting" % opt)

    pairs.append(("--Access_Type", access_value))
    pairs.append(("--Squash", squash_value))
    pairs.append(("--SecType", sec_value))
    return pairs

def create_client(path, host, pairs, fsalname):
    global gan_paths
    global export_id

    # Create EXPORT{} block if needed
    if path not in gan_paths:
        gan_paths[path] = 1
        export_id += 1
        cmd = ["ganesha_conf", "set", "EXPORT", "Path", path]
        cmd += ["--Export_Id", str(export_id), "--Pseudo", path]
        subprocess.check_call(cmd)

    # Create EXPORT{FSAL{}} block
    cmd = ["ganesha_conf", "set", "EXPORT", "Path", path, "FSAL", "--name", fsalname]
    subprocess.check_call(cmd)

    # Create EXPORT{CLIENT{}} block with all key,value pairs
    cmd = ["ganesha_conf", "set", "EXPORT", "Path", path]
    cmd += ["CLIENT", "Clients", host]
    for key, value in pairs:
        cmd += [key, value]
    subprocess.check_call(cmd)

def usage(msg=""):
    msg += "\n%s [--fsal <fsal-name>]" % sys.argv[0]
    sys.exit(msg)

fsals = ["gpfs", "vfs", "lustre"]
def main():
    fsal = "vfs"
    argc = len(sys.argv)
    if argc == 1:
        pass
    elif argc == 2:
        if sys.argv[1].startswith("-h") or sys.argv[1].startswith("--help"):
            usage()
        else:
            msg = "Unknown parameter: %s" % sys.argv[1]
            usage(msg)
    elif argc == 3:
        if not sys.argv[1].startswith("--fsal"):
            msg = "Unknown parameter: %s" % sys.argv[2]
            usage(msg)
        fsal = sys.argv[2]
        if fsal not in fsals:
            msg = "Unknown fsal: %s" % fsal
            usage(msg)
    else:
        usage()

    import tempfile
    f = tempfile.NamedTemporaryFile()
    os.environ["CONFFILE"] = f.name
    process_exports(fsal)
    if export_id == 0:
        sys.exit("processed 0 exports!")
    else:
        # f.name is updated by subprocesses (ganesha_conf) and reading
        # "f" here gets nothing! Re-open does the trick!
        with open(f.name) as f2:
            for line in f2:
                sys.stdout.write(line)

if __name__ == '__main__':
    main()
