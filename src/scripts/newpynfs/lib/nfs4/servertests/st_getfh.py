from nfs4.nfs4_const import *
from environment import check

def testFile(t, env):
    """GETFH on testtree file

    FLAGS: getfh file all
    DEPEND: LOOKFILE
    CODE: GF1r
    """
    env.c1.do_getfh(env.opts.usefile)
    
def testDir(t, env):
    """GETFH on testtree dir

    FLAGS: getfh dir all
    DEPEND: LOOKDIR
    CODE: GF1d
    """
    env.c1.do_getfh(env.opts.usedir)
    
def testLink(t, env):
    """GETFH on testtree symlink

    FLAGS: getfh symlink all
    DEPEND: LOOKLINK
    CODE: GF1a
    """
    env.c1.do_getfh(env.opts.uselink)
    
def testSocket(t, env):
    """GETFH on testtree socket

    FLAGS: getfh socket all
    DEPEND: LOOKSOCK
    CODE: GF1s
    """
    env.c1.do_getfh(env.opts.usesocket)
    
def testFifo(t, env):
    """GETFH on testtree fifo

    FLAGS: getfh fifo all
    DEPEND: LOOKFIFO
    CODE: GF1f
    """
    env.c1.do_getfh(env.opts.uselink)
    
def testBlock(t, env):
    """GETFH on testtree block device

    FLAGS: getfh block all
    DEPEND: LOOKBLK
    CODE: GF1b
    """
    env.c1.do_getfh(env.opts.uselink)
    
def testChar(t, env):
    """GETFH on testtree character device

    FLAGS: getfh char all
    DEPEND: LOOKCHAR
    CODE: GF1c
    """
    env.c1.do_getfh(env.opts.uselink)
    
def testNoFh(t, env):
    """GETFH should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: getfh emptyfh all
    DEPEND:
    CODE: GF9
    """
    c = env.c1
    ops = [c.getfh_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOFILEHANDLE, "GETFH with no <cfh>")
              
