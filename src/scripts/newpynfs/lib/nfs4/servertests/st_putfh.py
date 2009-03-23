from nfs4.nfs4_const import *
from environment import check

def _try_put(t, c, path):
    # Get fh via LOOKUP
    res = c.compound(c.use_obj(path) + [c.getfh_op()])
    check(res)
    oldfh = res.resarray[-1].arm.arm.object
    # Now try PUTFH and GETFH, see if it agrees
    res = c.compound([c.putfh_op(oldfh), c.getfh_op()])
    check(res)
    newfh = res.resarray[-1].arm.arm.object
    if oldfh != newfh:
        t.fail("GETFH did not return input of PUTFH for /%s" % '/'.join(path))

def testFile(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup file all
    DEPEND: LOOKFILE
    CODE: PUTFH1r
    """
    _try_put(t, env.c1, env.opts.usefile)

def testLink(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup symlink all
    DEPEND: LOOKLINK
    CODE: PUTFH1a
    """
    _try_put(t, env.c1, env.opts.uselink)

def testBlock(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup block all
    DEPEND: LOOKBLK
    CODE: PUTFH1b
    """
    _try_put(t, env.c1, env.opts.useblock)

def testChar(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup char all
    DEPEND: LOOKCHAR
    CODE: PUTFH1c
    """
    _try_put(t, env.c1, env.opts.usechar)

def testDir(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup dir all
    DEPEND: LOOKDIR
    CODE: PUTFH1d
    """
    _try_put(t, env.c1, env.opts.usedir)

def testFifo(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup fifo all
    DEPEND: LOOKFIFO
    CODE: PUTFH1f
    """
    _try_put(t, env.c1, env.opts.usefifo)

def testSocket(t, env):
    """PUTFH followed by GETFH should end up with original fh

    FLAGS: putfh getfh lookup socket all
    DEPEND: LOOKSOCK
    CODE: PUTFH1s
    """
    _try_put(t, env.c1, env.opts.usesocket)

def testBadHandle(t, env):
    """PUTFH with bad filehandle should return NFS4ERR_BADHANDLE

    FLAGS: putfh all
    CODE: PUTFH2
    """
    c = env.c1
    res = c.compound([c.putfh_op('abc')])
    check(res, NFS4ERR_BADHANDLE, "PUTFH with bad filehandle='abc'")

def testStaleHandle(t, env):
    """PUTFH which nolonger exists should return NFS4ERR_STALE

    FLAGS: putfh all
    DEPEND: MKFILE
    CODE: PUTFH3
    """
    c = env.c1
    c.init_connection()
    # Create a stale fh
    stale_fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, stale_fh, stateid)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res)
    # Now try to use it
    res = c.compound([c.putfh_op(stale_fh)])
    check(res, NFS4ERR_STALE, "Using a stale fh")
