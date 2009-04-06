from nfs4.nfs4_const import *
from nfs4.nfs4_type import stateid4
from environment import check, checklist, makeStaleId

def _confirm(t, c, file, stateid):
    ops = c.use_obj(file)
    ops += [c.open_confirm_op(stateid, c.get_seqid(t.code))]
    res = c.compound(ops)
    c.advance_seqid(t.code, res)
    return res

def testConfirmCreate(t, env):
    """OPEN_CONFIRM done twice in a row should return NFS4ERR_BAD_STATEID

    FLAGS: openconfirm all
    DEPEND: MKFILE
    CODE: OPCF1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    # Now confirm again
    res = _confirm(t, c, fh, stateid)
    check(res, NFS4ERR_BAD_STATEID, "OPEN_CONFIRM done twice in a row")

def testNoFh(t, env):
    """OPENCONFIRM  should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: openconfirm emptyfh all
    CODE: OPCF2
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, None, stateid)
    check(res, NFS4ERR_NOFILEHANDLE, "OPEN_CONFIRM with no <cfh>")

def testDir(t, env):
    """OPEN_CONFIRM of a dir should return NFS4ERR_ISDIR

    FLAGS: openconfirm dir all
    DEPEND: LOOKDIR
    CODE: OPCF3d
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.usedir, stateid)
    check(res, NFS4ERR_ISDIR, "OPEN_CONFIRM of a dir")

def testLink(t, env):
    """OPEN_CONFIRM of a nonfile object should return NFS4ERR_INVAL

    FLAGS: openconfirm symlink all
    DEPEND: LOOKLINK
    CODE: OPCF3a
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.uselink, stateid)
    check(res, NFS4ERR_INVAL, "OPEN_CONFIRM of a nonfile object")

def testFifo(t, env):
    """OPEN_CONFIRM of a nonfile object should return NFS4ERR_INVAL

    FLAGS: openconfirm fifo all
    DEPEND: LOOKFIFO
    CODE: OPCF3f
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.usefifo, stateid)
    check(res, NFS4ERR_INVAL, "OPEN_CONFIRM of a nonfile object")

def testBlock(t, env):
    """OPEN_CONFIRM of a nonfile object should return NFS4ERR_INVAL

    FLAGS: openconfirm block all
    DEPEND: LOOKBLK
    CODE: OPCF3b
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.useblock, stateid)
    check(res, NFS4ERR_INVAL, "OPEN_CONFIRM of a nonfile object")

def testChar(t, env):
    """OPEN_CONFIRM of a nonfile object should return NFS4ERR_INVAL

    FLAGS: openconfirm char all
    DEPEND: LOOKCHAR
    CODE: OPCF3c
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.usechar, stateid)
    check(res, NFS4ERR_INVAL, "OPEN_CONFIRM of a nonfile object")

def testSocket(t, env):
    """OPEN_CONFIRM of a nonfile object should return NFS4ERR_INVAL

    FLAGS: openconfirm socket all
    DEPEND: LOOKSOCK
    CODE: OPCF3s
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    res = _confirm(t, c, env.opts.usesocket, stateid)
    check(res, NFS4ERR_INVAL, "OPEN_CONFIRM of a nonfile object")

def testBadSeqid(t, env):
    """OPEN_CONFIRM with a bad seqid should return NFS4ERR_BAD_SEQID

    FLAGS: openconfirm seqid all
    CODE: OPCF4
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    fh = res.resarray[-1].arm.arm.object
    ops = [c.putfh_op(fh), c.open_confirm_op(stateid, 50)]
    res = c.compound(ops)
    check(res, NFS4ERR_BAD_SEQID, "OPEN_CONFIRM with a bad seqid=50")

def testBadStateid(t, env):
    """OPEN_CONFIRM with a bad state should return NFS4ERR_BAD_STATEID

    FLAGS: openconfirm badid all
    CODE: OPCF5
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    fh = res.resarray[-1].arm.arm.object
    res = _confirm(t, c, fh, stateid4(0, ''))
    check(res, NFS4ERR_BAD_STATEID, "OPEN_CONFIRM with a bad state")

def testStaleStateid(t, env):
    """OPEN_CONFIRM with a stale state should return NFS4ERR_STALE_STATEID

    FLAGS: openconfirm staleid all
    CODE: OPCF6
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    stateid = res.resarray[-2].arm.arm.stateid
    fh = res.resarray[-1].arm.arm.object
    res = _confirm(t, c, fh, makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "OPEN_CONFIRM with a stale state")

 # FRED - old id test
 
