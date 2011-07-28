from nfs4.nfs4_const import *
from environment import check

_maxval = ACCESS4_DELETE | ACCESS4_EXECUTE | ACCESS4_EXTEND | \
          ACCESS4_LOOKUP | ACCESS4_MODIFY | ACCESS4_READ
_valid_access_ops   = [x for x in range(_maxval + 1) if (x & _maxval)==x]
_invalid_access_ops = [64, 65, 66, 127, 128, 129]

def _try_all_combos(t, c, path, forbid=0):
    baseops = c.use_obj(path)
    for i in _valid_access_ops:
        res = c.compound(baseops + [c.access_op(i)])
        check(res)
        supported = res.resarray[-1].arm.arm.supported
        access = res.resarray[-1].arm.arm.access
        # Must have "access" subset of "supported" subset of "i"
        if supported & ~i:
            t.fail("ACCESS4resok.supported = 0x%x "
                   "is not a subset of requested 0x%x" % (supported, i))
        if access & ~supported:
            t.fail("ACCESS4resok.acces = 0x%x is not a subset of "
                   "ACCESS4resok.supported = 0x%x" % (access, supported))
        if supported & forbid:
            t.fail("ACCESS4resok.supported = 0x%x "
                   "contains not meaningful bits 0x%x" %
                   (supported, supported & forbid))
        if access & forbid:
            t.fail("ACCESS4resok.access = 0x%x "
                   "contains not meaningful bits 0x%x" %
                   (access, forbid & access))

def _try_read(c, path):
    ops = c.use_obj(path)
    ops += [c.access_op(ACCESS4_READ)]
    res = c.compound(ops)
    check(res)

def _try_invalid(t, c, path):
    baseops = c.use_obj(path)
    for i in _invalid_access_ops:
        res = c.compound(baseops + [c.access_op(i)])
        if res.status == NFS4_OK:
            supported = res.resarray[-1].arm.arm.supported
            access = res.resarray[-1].arm.arm.access
            if supported & ~0x3f or access & ~0x3f:
                t.fail("ACCESS with invalid argument 0x%x returned "
                       "supported=0x%x, access=0x%x." %
                       (i, supported, access))
        else:
            check(res, NFS4ERR_INVAL, "ACCESS with invalid argument 0x%x" % i)

def testReadFile(t, env):
    """ACCESS with read permission of testtree file

    FLAGS: access file all
    DEPEND: LOOKFILE
    CODE: ACC1r
    """
    _try_read(env.c1, env.opts.usefile)

def testReadDir(t, env):
    """ACCESS with read permission of testtree dir

    FLAGS: access dir all
    DEPEND: LOOKDIR
    CODE: ACC1d
    """
    _try_read(env.c1, env.opts.usedir)

def testReadLink(t, env):
    """ACCESS with read permission of testtree symlink

    FLAGS: access symlink all
    DEPEND: LOOKLINK
    CODE: ACC1a
    """
    _try_read(env.c1, env.opts.uselink)

def testReadFifo(t, env):
    """ACCESS with read permission of testtree fifo

    FLAGS: access fifo all
    DEPEND: LOOKFIFO
    CODE: ACC1f
    """
    _try_read(env.c1, env.opts.usefifo)

def testReadSocket(t, env):
    """ACCESS with read permission of testtree socket

    FLAGS: access socket all
    DEPEND: LOOKSOCK
    CODE: ACC1s
    """
    _try_read(env.c1, env.opts.usesocket)

def testReadChar(t, env):
    """ACCESS with read permission of testtree character device

    FLAGS: access char all
    DEPEND: LOOKCHAR
    CODE: ACC1c
    """
    _try_read(env.c1, env.opts.usechar)

def testReadBlock(t, env):
    """ACCESS with read permission of testtree block device

    FLAGS: access block all
    DEPEND: LOOKBLK
    CODE: ACC1b
    """
    _try_read(env.c1, env.opts.useblock)

def testAllDir(t, env):
    """ACCESS All valid combinations of arguments on directory

    FLAGS: access dir all
    DEPEND: LOOKDIR
    CODE: ACC2d
    """
    _try_all_combos(t, env.c1, env.opts.usedir, ACCESS4_EXECUTE)

def testAllFile(t, env):
    """ACCESS All valid combinations of arguments on file

    FLAGS: access file all
    DEPEND: LOOKFILE
    CODE: ACC2r
    """
    _try_all_combos(t, env.c1, env.opts.usefile,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testAllFifo(t, env):
    """ACCESS All valid combinations of arguments on fifo

    FLAGS: access fifo all
    DEPEND: LOOKFIFO
    CODE: ACC2f
    """
    _try_all_combos(t, env.c1, env.opts.usefifo,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testAllLink(t, env):
    """ACCESS All valid combinations of arguments on link

    FLAGS: access symlink all
    DEPEND: LOOKLINK
    CODE: ACC2a
    """
    _try_all_combos(t, env.c1, env.opts.uselink,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testAllSocket(t, env):
    """ACCESS All valid combinations of arguments on socket

    FLAGS: access socket all
    DEPEND: LOOKSOCK
    CODE: ACC2s
    """
    _try_all_combos(t, env.c1, env.opts.usesocket,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testAllChar(t, env):
    """ACCESS All valid combinations of arguments on character device

    FLAGS: access char all
    DEPEND: LOOKCHAR
    CODE: ACC2c
    """
    _try_all_combos(t, env.c1, env.opts.usechar,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testAllBlock(t, env):
    """ACCESS All valid combinations of arguments on block device

    FLAGS: access block all
    DEPEND: LOOKBLK
    CODE: ACC2b
    """
    _try_all_combos(t, env.c1, env.opts.useblock,
                    ACCESS4_LOOKUP | ACCESS4_DELETE)

def testNoFh(t, env):
    """ACCESS should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: access emptyfh all
    CODE: ACC3
    """
    c = env.c1
    res = c.compound([c.access_op(ACCESS4_READ)])
    check(res, NFS4ERR_NOFILEHANDLE, "ACCESS with no <cfh>")

def testInvalidsFile(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access file all
    DEPEND: LOOKFILE
    CODE: ACC4r
    """
    _try_invalid(t, env.c1, env.opts.usefile)

def testInvalidsDir(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access dir all
    DEPEND: LOOKDIR
    CODE: ACC4d
    """
    _try_invalid(t, env.c1, env.opts.usedir)

def testInvalidsFifo(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access fifo all
    DEPEND: LOOKFIFO
    CODE: ACC4f
    """
    _try_invalid(t, env.c1, env.opts.usefifo)

def testInvalidsLink(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access symlink all
    DEPEND: LOOKLINK
    CODE: ACC4a
    """
    _try_invalid(t, env.c1, env.opts.uselink)

def testInvalidsSocket(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access socket all
    DEPEND: LOOKSOCK
    CODE: ACC4s
    """
    _try_invalid(t, env.c1, env.opts.usesocket)

def testInvalidsChar(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access char all
    DEPEND: LOOKCHAR
    CODE: ACC4c
    """
    _try_invalid(t, env.c1, env.opts.usechar)

def testInvalidsBlock(t, env):
    """ACCESS should fail with NFS4ERR_INVAL on invalid arguments

    FLAGS: access block all
    DEPEND: LOOKBLK
    CODE: ACC4b
    """
    _try_invalid(t, env.c1, env.opts.useblock)
