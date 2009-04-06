from nfs4.nfs4_const import *
from environment import check

_text = "Random data to write"

def _commit(t, c, offset=0, count=0):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1a
    """
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.write_file(fh, _text, 0, stateid, how=UNSTABLE4)
    check(res, msg="WRITE with how=UNSTABLE4")
    res = c.commit_file(fh, offset, count)
    check(res, msg="COMMIT with offset=%x, count=%x" % (offset, count))

def testCommitOffset0(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1a
    """
    _commit(t, env.c1, 0)

def testCommitOffset1(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1b
    """
    _commit(t, env.c1, 1)

def testCommitOffsetMax1(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1c
    """
    _commit(t, env.c1, 0xffffffffffffffffL)

def testCommitOffsetMax2(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1d
    """
    _commit(t, env.c1, 0xfffffffffffffffeL)

def testCommitCount1(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1e
    """
    _commit(t, env.c1, 1, 1)

def testCommitCountMax(t, env):
    """COMMIT

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT1f
    """
    _commit(t, env.c1, 0, 0xffffffffL)


def testLink(t, env):
    """COMMIT

    FLAGS: commit symlink all
    DEPEND: LOOKLINK
    CODE: CMT2a
    """
    c = env.c1
    res = c.commit_file(env.opts.uselink)
    check(res, NFS4ERR_INVAL, "COMMIT with non-file object")

def testBlock(t, env):
    """COMMIT

    FLAGS: commit block all
    DEPEND: LOOKBLK
    CODE: CMT2b
    """
    c = env.c1
    res = c.commit_file(env.opts.useblock)
    check(res, NFS4ERR_INVAL, "COMMIT with non-file object")

def testChar(t, env):
    """COMMIT

    FLAGS: commit char all
    DEPEND: LOOKCHAR
    CODE: CMT2c
    """
    c = env.c1
    res = c.commit_file(env.opts.usechar)
    check(res, NFS4ERR_INVAL, "COMMIT with non-file object")

def testDir(t, env):
    """COMMIT

    FLAGS: commit dir all
    DEPEND: LOOKDIR
    CODE: CMT2d
    """
    c = env.c1
    res = c.commit_file(env.opts.usedir)
    check(res, NFS4ERR_ISDIR, "COMMIT with non-file object")

def testFifo(t, env):
    """COMMIT

    FLAGS: commit fifo all
    DEPEND: LOOKFIFO
    CODE: CMT2f
    """
    c = env.c1
    res = c.commit_file(env.opts.usefifo)
    check(res, NFS4ERR_INVAL, "COMMIT with non-file object")

def testSocket(t, env):
    """COMMIT

    FLAGS: commit socket all
    DEPEND: LOOKSOCK
    CODE: CMT2s
    """
    c = env.c1
    res = c.commit_file(env.opts.usesocket)
    check(res, NFS4ERR_INVAL, "COMMIT with non-file object")

def testNoFh(t, env):
    """COMMIT should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: commit emptyfh all
    CODE: CMT3
    """
    c = env.c1
    res = c.commit_file(None)
    check(res, NFS4ERR_NOFILEHANDLE, "COMMIT with no <cfh>")

def testCommitOverflow(t, env):
    """COMMIT on file with offset+count >= 2**64 should return NFS4ERR_INVAL

    FLAGS: commit all
    DEPEND: MKFILE
    CODE: CMT4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.write_file(fh, _text, 0, stateid, how=UNSTABLE4)
    check(res, msg="WRITE with how=UNSTABLE4")
    res = c.commit_file(fh, 0xfffffffffffffff0L, 64)
    check(res, NFS4ERR_INVAL, "COMMIT with offset + count overflow")
