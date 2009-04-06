from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_utf8strings

def testDir(t, env):
    """LOOKUPP with directory (cfh)

    FLAGS: lookupp all
    DEPEND: MKDIR
    CODE: LOOKP1
    """
    c = env.c1
    res = c.create_obj(c.homedir + [t.code])
    check(res)
    ops = c.use_obj(c.homedir)
    ops += [c.getfh_op(), c.lookup_op(t.code), c.lookupp_op(), c.getfh_op()]
    res = c.compound(ops)
    check(res)
    fh1 = res.resarray[-4].arm.arm.object
    fh2 = res.resarray[-1].arm.arm.object
    if fh1 != fh2:
        t.fail("LOOKUPP FH does not match orig FH")

def testFile(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp file all
    DEPEND: LOOKFILE
    CODE: LOOKP2r
    """
    c = env.c1
    ops = c.use_obj(env.opts.usefile) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")
    
def testFifo(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp fifo all
    DEPEND: LOOKFIFO
    CODE: LOOKP2f
    """
    c = env.c1
    ops = c.use_obj(env.opts.usefifo) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")
    
def testLink(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp symlink all
    DEPEND: LOOKLINK
    CODE: LOOKP2a
    """
    c = env.c1
    ops = c.use_obj(env.opts.uselink) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")
    
def testBlock(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp block all
    DEPEND: LOOKBLK
    CODE: LOOKP2b
    """
    c = env.c1
    ops = c.use_obj(env.opts.useblock) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")
    
def testChar(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp char all
    DEPEND: LOOKCHAR
    CODE: LOOKP2c
    """
    c = env.c1
    ops = c.use_obj(env.opts.usechar) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")
    
def testSock(t, env):
    """LOOKUPP with non-dir (cfh)

    FLAGS: lookupp socket all
    DEPEND: LOOKSOCK
    CODE: LOOKP2s
    """
    c = env.c1
    ops = c.use_obj(env.opts.usesocket) + [c.lookupp_op()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "LOOKUPP with non-dir <cfh>")

def testAtRoot(t, env):
    """LOOKUPP with (cfh) at root should return NFS4ERR_NOENT

    FLAGS: lookupp all
    CODE: LOOKP3
    """
    c = env.c1
    res = c.compound([c.putrootfh_op(), c.lookupp_op()])
    check(res, NFS4ERR_NOENT, "LOOKUPP at root")

def testNoFh(t, env):
    """LOOKUPP with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: lookupp all
    CODE: LOOKP4
    """
    c = env.c1
    res = c.compound([c.lookupp_op()])
    check(res, NFS4ERR_NOFILEHANDLE, "LOOKUPP at root")
