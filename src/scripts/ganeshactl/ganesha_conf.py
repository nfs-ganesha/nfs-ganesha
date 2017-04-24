#!/usr/bin/env python

import os, sys
from Ganesha.config_editor import BLOCK, ArgError
import logging, pprint

# Modify the file with the given data atomically
def modify_file(filename, data):
    from tempfile import NamedTemporaryFile
    f = NamedTemporaryFile(dir=os.path.dirname(filename), delete=False)
    f.write(data)
    f.flush()
    os.fsync(f.fileno())

    # If filename exists, get its stats and apply them to the temp file
    try:
        stat = os.stat(filename)
        os.chown(f.name, stat.st_uid, stat.st_gid)
        os.chmod(f.name, stat.st_mode)
    except:
        pass

    os.rename(f.name, filename)

# Get block names and key value options as separate lists
def get_blocks(args):
    key_found = False
    for i, arg in enumerate(args):
        if arg.startswith("--"):
            key_found = True
            break

    if key_found:
        return (args[0:i],  args[i:])
    else:
        return (args, [])


usage = """
Usage: %s set <block-descriptor> [--param value]
       %s del <block-descriptor> [--param]

       where <block-descriptor> is a list of blocknames with possible key value pair identifying the block.

       For example,
       %s set log --default_log_level DEBUG
       %s set log components --FSAL FULL_DEBUG
       %s set export export_id 14 --pseudo "/nfsroot/export1"
       %s set export export_id 14 client clients '*' --manage-gids true
""" % (6 * (sys.argv[0],))

if len(sys.argv) < 2:
    sys.exit(usage);

FORMAT = "[%(filename)s:%(lineno)d-%(funcName)s()] %(message)s"
#logging.basicConfig(format=FORMAT, level=logging.DEBUG)

opcode = sys.argv[1]
if opcode == "set":
    (names, pairs) = get_blocks(sys.argv[2:])
    if not names:
        sys.exit("no block names")
    if len(pairs) % 2 != 0:
        sys.exit("odd number for key value pairs")

    for key in pairs[::2]:
        if not key.startswith("--"):
            sys.exit("some keys are not with -- prefix")

    # remove "--" prefix of keys
    keys = [key[2:] for key in pairs[::2]]
    values = [value for value in pairs[1::2]]

    opairs = zip(keys, values)
    block = BLOCK(names)

elif opcode == "del":
    (names, keys) = get_blocks(sys.argv[2:])
    if not names:
        sys.exit("no block names")
    for key in keys:
        if not key.startswith("--"):
            sys.exit("some key not with -- prefix")

    # remove "--" prefix of keys
    keys = [key[2:] for key in keys]
    if not keys:
        keys = []

    block = BLOCK(names)
else:
    sys.exit(usage)

conffile = os.environ.get("CONFFILE", "/etc/ganesha/ganesha.conf")
old = open(conffile).read()

try:
    if (opcode == "set"):
        logging.debug("opairs: %s", pprint.pformat(opairs))
        new = block.set_keys(old, opairs)
    else:
        logging.debug("keys: %s", pprint.pformat(keys))
        new = block.del_keys(old, keys)
except ArgError as e:
        sys.exit(e.error)

modify_file(conffile, new)
