from nfs4.nfs4_const import *
from nfs4.nfs4_type import stateid4
from environment import check, checklist, makeStaleId

def testFile(t, env):
    """LOCKU a regular file

    FLAGS: locku all
    DEPEND: MKFILE LOCK1
    CODE: LKU1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1, msg="Locking file %s" % t.code)
    res2 = c.lock_test(fh)
    check(res2, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)
    res3 = c.unlock_file(1, fh, res1.lockid)
    check(res3, msg="Unlocking locked file %s" % t.code)
    res2 = c.lock_test(fh)
    check(res2, msg="Testing file %s was unlocked" % t.code)

def testUnlocked(t, env):
    """LOCKU on an unlocked area should work or return NFS4ERR_LOCK_RANGE

    FLAGS: locku all
    DEPEND: MKFILE
    CODE: LKUNONE
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 100, 100)
    check(res1, msg="Locking file %s" % t.code)
    res2 = c.unlock_file(1, fh, res1.lockid, 0, 50)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_RANGE], "LOCKU on an unlocked area")
    if res2.status == NFS4ERR_LOCK_RANGE:
        t.fail_support("LOCKU on an unlocked area should return OK")

def testSplit(t, env):
    """LOCKU inside a locked range should work or return NFS4ERR_LOCK_RANGE

    FLAGS: locku all
    DEPEND: MKFILE
    CODE: LKUSPLIT
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 100, 100)
    check(res1, msg="Locking file %s" % t.code)
    res2 = c.unlock_file(1, fh, res1.lockid, 125, 50)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_RANGE], "LOCKU inside locked area")
    if res2.status == NFS4ERR_LOCK_RANGE:
        t.fail_support("LOCKU inside a locked area should return OK")

def testOverlap(t, env):
    """LOCKU on an overlapping range should work or return NFS4ERR_LOCK_RANGE

    FLAGS: locku all
    DEPEND: MKFILE LKUNONE LKUSPLIT
    CODE: LKUOVER
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 100, 100)
    check(res1, msg="Locking file %s" % t.code)
    res2 = c.unlock_file(1, fh, res1.lockid, 50, 100)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_RANGE],
              "LOCKU overlapping a locked area")
    if res2.status == NFS4ERR_LOCK_RANGE:
        t.fail("LOCKU overlapping a locked area should return OK, "
               "given server allows other non-matching LOCKUs")

def test32bitRange(t, env):
    """LOCKU ranges over 32 bits should work or return NFS4ERR_BAD_RANGE

    FLAGS: lock locku all
    DEPEND: MKFILE LOCKRNG LKUSPLIT
    CODE: LKU2
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, res1.lockid, 0, 0xffffffffffff)
    checklist(res2, [NFS4_OK, NFS4ERR_BAD_RANGE, NFS4ERR_LOCK_RANGE],
              "LOCKU range over 32 bits")
    if res2.status == NFS4ERR_BAD_RANGE:
        t.fail("Allowed 64 bit LOCK range, but only 32 bit LOCKU range")
    if res2.status == NFS4ERR_LOCK_RANGE:
        t.fail("Allowed 32bit splitting of locks, but not 64bit")

def testZeroLen(t, env):
    """LOCKU with len=0 should return NFS4ERR_INVAL

    FLAGS: locku all
    DEPEND: MKFILE
    CODE: LKU3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, res1.lockid, 1, 0)
    check(res2, NFS4ERR_INVAL, "LOCKU with len=0")
    
def testLenTooLong(t, env):
    """LOCKU with offset+len overflow should return NFS4ERR_INVAL

    FLAGS: locku emptyfh all
    DEPEND: MKFILE
    CODE: LKU4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, res1.lockid, 100, 0xfffffffffffffffe)
    check(res2, NFS4ERR_INVAL, "LOCKU with offset+len overflow")

def testNoFh(t, env):
    """LOCKU with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: locku all
    DEPEND: MKFILE
    CODE: LKU5
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, None, res1.lockid)
    check(res2, NFS4ERR_NOFILEHANDLE, "LOCKU with no <cfh>")

def testBadLockSeqid(t, env):
    """LOCKU with a bad lockseqid should return NFS4ERR_BAD_SEQID

    FLAGS: locku seqid all
    DEPEND: MKFILE
    CODE: LKU6
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(2, fh, res1.lockid)
    check(res2, NFS4ERR_BAD_SEQID, "LOCKU with a bad lockseqid=2")

def testBadLockSeqid2(t, env):
    """LOCKU with a bad lockseqid should return NFS4ERR_BAD_SEQID

    FLAGS: locku seqid all
    DEPEND: MKFILE
    CODE: LKU6b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 0, 50)
    check(res1)
    res2 = c.relock_file(1, fh, res1.lockid, 100, 50)
    check(res2)
    res3 = c.unlock_file(0, fh, res2.lockid)
    check(res3, NFS4ERR_BAD_SEQID, "LOCKU with a bad lockseqid=1")

# See 8.1.5, as well as def of BAD_SEQID in sec 12
# Turns out should expect replay of LOCK command.
# But nfs4lib will raise an error, and not allow checking of response
# FRED - fix this
def xxxtestBadLockSeqid3(t, env):
    """LOCKU with a bad lockseqid should return NFS4ERR_BAD_SEQID

    FLAGS: locku seqid all
    DEPEND: MKFILE
    CODE: LKU6c
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 0, 50)
    check(res1)
    res2 = c.relock_file(1, fh, res1.lockid, 100, 50)
    check(res2)
    res3 = c.unlock_file(1, fh, res2.lockid)
    check(res3, NFS4ERR_BAD_SEQID, "LOCKU with a bad lockseqid=1")

def testOldLockStateid(t, env):
    """LOCKU with old lock stateid should return NFS4ERR_OLD_STATEID

    FLAGS: locku oldid all
    DEPEND: MKFILE
    CODE: LKU7
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, res1.lockid)
    check(res2)
    # Try to unlock again with old stateid
    res3 = c.unlock_file(2, fh, res1.lockid)
    check(res3, NFS4ERR_OLD_STATEID, "LOCKU with old lockstateid",
          [NFS4ERR_BAD_STATEID])

def testBadLockStateid(t, env):
    """LOCKU should return NFS4ERR_BAD_STATEID if use a bad id

    FLAGS: locku badid all
    DEPEND: MKFILE
    CODE: LKU8
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, stateid4(0, ''))
    check(res2, NFS4ERR_BAD_STATEID, "LOCKU with a bad stateid")
    
def testStaleLockStateid(t, env):
    """LOCKU with stale lockstateid should return NFS4ERR_STALE_STATEID

    FLAGS: locku staleid all
    DEPEND: MKFILE
    CODE: LKU9
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    res2 = c.unlock_file(1, fh, makeStaleId(res1.lockid))
    check(res2, NFS4ERR_STALE_STATEID, "LOCKU with stale lockstateid",
          [NFS4ERR_BAD_STATEID, NFS4ERR_OLD_STATEID])

# FRED - what is correct error return?
def testTimedoutUnlock(t, env):
    """LOCKU: Try to unlock file after timed out 

    NFS4ER_EXPIRED return mandated by section 8.6.3 of rfc
    
    FLAGS: locku timed all
    DEPEND: MKFILE
    CODE: LKU10
    """
    c = env.c1
    sleeptime = c.getLeaseTime() * 3 // 2
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, attrs={FATTR4_MODE: 0666})
    res1 = c.lock_file(t.code, fh, stateid)
    check(res1)
    env.sleep(sleeptime)
    # Conflicting open should force server to drop state
    c2 = env.c2
    c2.init_connection()
    c2.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_WRITE)
    res2 = c.unlock_file(1, fh, res1.lockid)
    checklist(res2, [NFS4ERR_EXPIRED, NFS4_OK],
              "Try to unlock file after timed out")
