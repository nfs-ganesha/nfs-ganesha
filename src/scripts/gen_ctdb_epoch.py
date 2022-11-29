#!/usr/bin/python3

# Usually ganesha daemon start up time is used as its epoch. This is
# fine for a single node configurations, but in clustered environment,
# ganesha daemon will have to distinguish itself from instances running
# on other nodes. This is not possible if ganesha daemon is started at
# the same time on multiple cluster nodes.
#
# The epoch will have to be a 32 bit number. We use nodeid in the most
# significant 16 bits and a generation number in the least significant
# 16 bits. Current generation number is written to seq_num_file.

import os, sys, re, fcntl
from pathlib import Path
from subprocess import Popen, PIPE
from ctypes import c_int, c_long, pointer, POINTER, Structure

seq_num_file = "/var/lib/nfs/ganesha/seq_num"
def main():
    genid = get_genid() + 1 # next genid
    genid &= 0xFFFF         # handle 16 bit overflow
    put_genid(genid)        # store the genid
    nodeid = get_nodeid()
    epoch = nodeid << 16 | genid
    print(epoch)

def get_genid():
    try:
        with open(seq_num_file, "r",encoding='UTF-8') as f:
            genid = int(f.read())
    except Exception:
        genid = 0
    return genid

def put_genid(genid):
    with open(seq_num_file, "w+", encoding='UTF-8') as f:
        f.write("%s" % genid)

def get_nodeid():
    child = Popen(['/usr/bin/ctdb', 'pnn'], encoding='UTF-8',
					stdout=PIPE, stderr=PIPE)
    (nodeid, err) = child.communicate()
    child.wait()
    return int(nodeid)

if __name__ == "__main__":
    import traceback
    import syslog
    try:
        main()
    except:
        syslog.syslog(traceback.format_exc())
        sys.exit(1)
