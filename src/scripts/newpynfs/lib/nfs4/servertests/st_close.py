from nfs4.nfs4_const import *
from environment import check, makeStaleId


def testCloseCreate(t, env):
    """CLOSE a normal created file

    FLAGS: close all
    DEPEND: MKFILE
    CODE: CLOSE1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, fh, stateid)
    check(res, msg="CLOSE a created file")

def testCloseOpen(t, env):
    """CLOSE a normal file which was opened without creation

    FLAGS: close all
    DEPEND: INIT LOOKFILE
    CODE: CLOSE2
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.open_confirm(t.code, env.opts.usefile)
    res = c.close_file(t.code, fh, stateid)
    check(res, msg="CLOSE a non-create open")

def testBadSeqid(t, env):
    """CLOSE with a bad sequence should return NFS4ERR_BAD_SEQID

    FLAGS: close seqid all
    DEPEND: MKFILE
    CODE: CLOSE3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, fh, stateid, seqid=50)
    check(res, NFS4ERR_BAD_SEQID, "CLOSE with a bad openseqid=50")

def testBadStateid(t, env):
    """CLOSE should return NFS4ERR_BAD_STATEID if use a bad id

    FLAGS: close badid all
    DEPEND: MKFILE
    CODE: CLOSE4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, fh, env.stateid0)
    check(res, NFS4ERR_BAD_STATEID, "CLOSE with a bad stateid")
    
def testOldStateid(t, env):
    """CLOSE with old stateid should return NFS4ERR_OLD_STATEID

    FLAGS: close oldid all
    DEPEND: MKFILE
    CODE: CLOSE5
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res)
    fh = res.resarray[-1].arm.arm.object
    stateid = res.resarray[-2].arm.arm.stateid
    c.confirm(t.code, res)
    res = c.close_file(t.code, fh, stateid)
    check(res, NFS4ERR_OLD_STATEID, "CLOSE with an old stateid")

def testStaleStateid(t, env):
    """CLOSE with stale stateid should return NFS4ERR_STALE_STATEID

    FLAGS: close staleid all
    DEPEND: MKFILE
    CODE: CLOSE6
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, fh, makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "CLOSE with a stale stateid")
    
def testNoCfh(t, env):
    """CLOSE with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: close emptyfh all
    DEPEND: MKFILE
    CODE: CLOSE7
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, None, stateid)
    check(res, NFS4ERR_NOFILEHANDLE, "CLOSE with no <cfh>")

def testTimedoutClose1(t, env):
    """CLOSE: Try to close file after timed out

    EXPIRED return required by 8.6.3
    
    FLAGS: close timed all
    DEPEND: MKFILE
    CODE: CLOSE8
    """
    c = env.c1
    sleeptime = c.getLeaseTime() * 3 // 2
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_WRITE,
                                   attrs={FATTR4_MODE: 0666})
    env.sleep(sleeptime)
    # Conflicting open should force server to drop state
    c2 = env.c2
    c2.init_connection()
    c2.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_WRITE)
    res = c.close_file(t.code, fh, stateid)
    check(res, NFS4ERR_EXPIRED, "CLOSE after lease timeout")
    
def testTimedoutClose2(t, env):
    """CLOSE: Try to close file after timed out with locks held

    EXPIRED return required by 8.6.3
    
    FLAGS: close timed all
    DEPEND: MKFILE
    CODE: CLOSE9
    """
    c = env.c1
    sleeptime = c.getLeaseTime() * 3 // 2
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_WRITE,
                                   attrs={FATTR4_MODE: 0666})
    res = c.lock_file(t.code, fh, stateid)
    check(res)
    env.sleep(sleeptime)
    # Conflicting open should force server to drop state
    c2 = env.c2
    c2.init_connection()
    c2.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_WRITE)
    res = c.close_file(t.code, fh, stateid)
    check(res, NFS4ERR_EXPIRED, "CLOSE after lease timeout with lock held")
