from nfs4.nfs4_const import *
from environment import check

def testReadlink(t, env):
    """READLINK on link

    FLAGS: readlink symlink all
    DEPEND: LOOKLINK
    CODE: RDLK1
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.uselink) + [c.readlink_op()])
    check(res)
    data = res.resarray[-1].arm.arm.link
    if data != env.linkdata:
        t.fail("link data was '%s', expected '%s'" % (data, env.linkdata))

def testFile(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink file all
    DEPEND: LOOKFILE
    CODE: RDLK2r
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.usefile) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testBlock(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink block all
    DEPEND: LOOKBLK
    CODE: RDLK2b
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.useblock) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testChar(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink char all
    DEPEND: LOOKCHAR
    CODE: RDLK2c
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.usechar) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testDir(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink dir all
    DEPEND: LOOKDIR
    CODE: RDLK2d
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.usedir) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testFifo(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink fifo all
    DEPEND: LOOKFIFO
    CODE: RDLK2f
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.usefifo) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testSocket(t, env):
    """READLINK on non-symlink objects should return NFS4ERR_INVAL

    FLAGS: readlink socket all
    DEPEND: LOOKSOCK
    CODE: RDLK2s
    """
    c = env.c1
    res = c.compound(c.use_obj(env.opts.usesocket) + [c.readlink_op()])
    check(res, NFS4ERR_INVAL, "READLINK on non-symlink objects")

def testNoFh(t, env):
    """READLINK without (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: readlink emptyfh all
    CODE: RDLK3
    """
    c = env.c1
    res = c.compound([c.readlink_op()])
    check(res, NFS4ERR_NOFILEHANDLE, "READLINK with no <cfh>")
#####################################

