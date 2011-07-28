from nfs4.nfs4_const import *
from environment import check, checklist
from nfs4.nfs4_type import exist_lock_owner4, locker4

def _replay(c, ops, error=NFS4_OK):
    # Can send in an error list, but replays must return same error as orig
    if type(error) is list:
        check_funct = checklist
    else:
        check_funct = check
    res = c.compound(ops)
    check_funct(res, error, "Call to be replayed")
    error = res.status
    xid = c.xid
    orig_funct = c.get_new_xid
    try:
        c.get_new_xid = lambda : xid
        res = c.compound(ops)
        check(res, error, "Replay the first time")
        res = c.compound(ops)
        check(res, error, "Replay the second time")
    finally:
        c.get_new_xid = orig_funct

def testOpen(t, env):
    """REPLAY: Send three OPEN calls with the same XID, SEQID, check DRC

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY1
    """
    c = env.c1
    c.init_connection()
    ops = c.use_obj(c.homedir)
    ops += [c.open(t.code, type=OPEN4_CREATE), c.getfh_op()]
    _replay(c, ops)
    # Note that seqid is now off on this and other replay tests


def testReplayState1(t, env):
    """REPLAY an erroneous OPEN of a nonexistant file

    FLAGS: replay all
    DEPEND: MKDIR INIT
    CODE: RPLY2
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code])
    ops = c.use_obj(c.homedir + [t.code])
    ops += [c.open(t.code, 'vapor'), c.getfh_op()]
    _replay(c, ops, NFS4ERR_NOENT)
    
def testReplayState2(t, env):
    """REPLAY an erroneous OPEN of a dir

    FLAGS: replay all
    DEPEND: MKDIR INIT
    CODE: RPLY3
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code])
    ops = c.use_obj(c.homedir)
    ops += [c.open(t.code), c.getfh_op()]
    _replay(c, ops, NFS4ERR_ISDIR)

def testReplayNonState(t, env):
    """REPLAY an erroneous LOOKUP

    FLAGS: replay all
    DEPEND: MKDIR
    CODE: RPLY4
    """
    c = env.c1
    c.maketree([t.code])
    ops = c.use_obj(c.homedir + [t.code, 'vapor'])
    _replay(c, ops, NFS4ERR_NOENT)

def testLock(t, env):
    """REPLAY a LOCK command

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY5
    """
    c = env.c1
    c.init_connection()
    # Create a file and partially lock it
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 20, 100)
    check(res, msg="Locking file %s" % t.code)
    # Create and replay LOCK ops
    ops = c.use_obj(fh)
    lock_owner = exist_lock_owner4(res.lockid, 1)
    locker = locker4(FALSE, lock_owner=lock_owner)
    ops += [c.lock_op(WRITE_LT, FALSE, 0, 10, locker)]
    _replay(c, ops)
    
def testLockDenied(t, env):
    """REPLAY a LOCK command that fails

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY6
    """
    c = env.c1
    c.init_connection()
    # Create a file and lock it
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 20, 100)
    check(res1, msg="Locking file %s for first owner" % t.code)
    res2 = c.lock_file(t.code, fh, stateid, 0, 10)
    check(res2, msg="Locking file %s for second owner" % t.code)
    # Create and replay LOCK ops
    ops = c.use_obj(fh)
    lock_owner = exist_lock_owner4(res1.lockid, 1)
    locker = locker4(FALSE, lock_owner=lock_owner)
    ops += [c.lock_op(WRITE_LT, FALSE, 0, 10, locker)]
    _replay(c, ops, NFS4ERR_DENIED)
    
def testUnlock(t, env):
    """REPLAY a LOCKU command

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY7
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 20, 100)
    check(res, msg="Locking file %s" % t.code)
    ops = c.use_obj(fh)
    ops += [c.locku_op(READ_LT, 1, res.lockid, 0, 0xffffffffffffffff)]
    _replay(c, ops)

def testUnlockWait(t, env):
    """REPLAY a LOCKU command after lease has expired

    FLAGS: replay all timed
    DEPEND: MKFILE
    CODE: RPLY8
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 20, 100)
    check(res, msg="Locking file %s" % t.code)
    sleeptime = c.getLeaseTime() * 3 // 2
    env.sleep(sleeptime)
    ops = c.use_obj(fh)
    ops += [c.locku_op(READ_LT, 1, res.lockid, 0, 0xffffffffffffffff)]
    _replay(c, ops, [NFS4_OK, NFS4ERR_EXPIRED])

def testClose(t, env):
    """REPLAY a CLOSE command

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY9
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    ops = c.use_obj(fh)
    ops += [c.close_op(c.get_seqid(t.code), stateid)]
    _replay(c, ops)
    
def testCloseWait(t, env):
    """REPLAY a CLOSE command after lease has expired

    FLAGS: replay all timed
    DEPEND: MKFILE
    CODE: RPLY10
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    sleeptime = c.getLeaseTime() * 3 // 2
    env.sleep(sleeptime)
    ops = c.use_obj(fh)
    ops += [c.close_op(c.get_seqid(t.code), stateid)]
    _replay(c, ops, [NFS4_OK, NFS4ERR_EXPIRED])
    
def testCloseFail(t, env):
    """REPLAY a CLOSE command that fails

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY11
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    ops = c.use_obj(fh)
    ops += [c.close_op(c.get_seqid(t.code)+1, stateid)]
    _replay(c, ops, NFS4ERR_BAD_SEQID)
    
def testOpenConfirm(t, env):
    """REPLAY an OPEN_CONFIRM command

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY12
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    fh = res.resarray[-1].arm.arm.object
    stateid = res.resarray[-2].arm.arm.stateid
    rflags = res.resarray[-2].arm.arm.rflags
    if not rflags & OPEN4_RESULT_CONFIRM:
        t.pass_warn("OPEN did not require CONFIRM")
    ops = c.use_obj(fh)
    ops += [c.open_confirm_op(stateid, c.get_seqid(t.code))]
    _replay(c, ops)
    
def testOpenConfirmFail(t, env):
    """REPLAY an OPEN_CONFIRM command that fails

    FLAGS: replay all
    DEPEND: MKFILE
    CODE: RPLY13
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    fh = res.resarray[-1].arm.arm.object
    stateid = res.resarray[-2].arm.arm.stateid
    rflags = res.resarray[-2].arm.arm.rflags
    if not rflags & OPEN4_RESULT_CONFIRM:
        t.pass_warn("OPEN did not require CONFIRM")
    ops = c.use_obj(fh)
    ops += [c.open_confirm_op(stateid, c.get_seqid(t.code)+1)]
    _replay(c, ops, NFS4ERR_BAD_SEQID)
