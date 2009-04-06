from nfs4.nfs4_const import *
from nfs4.nfs4_type import stateid4
from environment import check, checklist, get_invalid_clientid, makeStaleId
import time

def testFile(t, env):
    """LOCK and LOCKT a regular file

    FLAGS: lock lockt all
    DEPEND: MKFILE
    CODE: LOCK1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)

def testClose(t, env):
    """LOCK - closing should release locks or return NFS4ERR_LOCKS_HELD

    FLAGS: lock close all
    DEPEND: MKFILE LOCK1
    CODE: LOCKHELD
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)
    res = c.close_file(t.code, fh, stateid)
    checklist(res, [NFS4_OK, NFS4ERR_LOCKS_HELD],
              "Trying to close locked file")
    if res.status == NFS4ERR_LOCKS_HELD:
        t.fail_support("Can not close locked files")
    # Now make sure lock was released
    res = c.lock_test(fh)
    check(res, msg="Testing that close released locks on file %s" % t.code)
    
def testExistingFile(t, env):
    """LOCK a regular file that was opened w/o create

    Note several servers return _ERR_OPENMODE, but this is not a legit
    option. (FRED - why not?)
    
    FLAGS: lock all
    DEPEND: MKFILE LOCK1
    CODE: LOCK3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.close_file(t.code, fh, stateid)
    check(res)
    fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                                 deny=OPEN4_SHARE_DENY_NONE)
    res = c.lock_file(t.code, fh, stateid)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)

def test32bitRange(t, env):
    """LOCK ranges over 32 bits should work or return NFS4ERR_BAD_RANGE

    FLAGS: lock locku all
    DEPEND: MKFILE
    CODE: LOCKRNG
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 0, 0xffffffffffff)
    checklist(res, [NFS4_OK, NFS4ERR_BAD_RANGE], "LOCK range over 32 bits")
    if res.status == NFS4ERR_BAD_RANGE:
        t.fail_support("Server does not support 64 bit lock ranges")

def testOverlap(t, env):
    """LOCK: overlapping locks should work or return NFS4ERR_LOCK_RANGE

    FLAGS: lock all
    DEPEND: MKFILE LOCK1 LKTOVER
    CODE: LOCKMRG
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 25, 75)
    check(res1)
    res2 = c.relock_file(1, fh, res1.lockid, 50, 75)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_RANGE], "Overlapping locks")
    if res2.status == NFS4ERR_LOCK_RANGE:
        t.fail_support("Server does not support lock consolidation")
    # Test the merged lock
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)
    lock = res.resarray[-1].arm.arm
    if (lock.offset, lock.length) != (25, 100):
        t.fail("Merged lock had [offset,length] = [%i,%i], "
               "expected [25,100]" % (lock.offset, lock.length))

def testDowngrade(t, env):
    """LOCK change from write to a read lock

    FLAGS: lock all
    DEPEND: MKFILE LOCK1
    CODE: LOCKCHGD
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 25, 75)
    check(res1)
    # Lock again with read lock
    res2 = c.relock_file(1, fh, res1.lockid, 25, 75, READ_LT)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_NOTSUPP], "Lock downgrade")
    if res2.status == NFS4ERR_LOCK_NOTSUPP:
        t.fail_support("Server does not support atomic lock downgrades")
    # Test the lock has changed
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)
    if res.resarray[-1].arm.arm.locktype != READ_LT:
        t.fail("Attempted lock downgrade to READ_LT, but got %s" %
               nfs_lock_type4[res.resarray[-1].arm.arm.locktype])
    
def testUpgrade(t, env):
    """LOCK change from read to a write lock

    FLAGS: lock all
    DEPEND: MKFILE LOCK1
    CODE: LOCKCHGU
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 25, 75, READ_LT)
    check(res1)
    # Lock again with write lock
    res2 = c.relock_file(1, fh, res1.lockid, 25, 75, WRITE_LT)
    checklist(res2, [NFS4_OK, NFS4ERR_LOCK_NOTSUPP], "Lock upgrade")
    if res2.status == NFS4ERR_LOCK_NOTSUPP:
        t.fail_support("Server does not support atomic lock upgrades")
    # Test the lock has changed
    res = c.lock_test(fh)
    check(res, NFS4ERR_DENIED, "Testing file %s is locked" % t.code)
    if res.resarray[-1].arm.arm.locktype != WRITE_LT:
        t.fail("Attempted lock downgrade to WRITE_LT, but got %s" %
               nfs_lock_type4[res.resarray[-1].arm.arm.locktype])
    
def testMode(t, env):
    """LOCK: try to write-lock a read-mode file

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ)
    res = c.lock_file(t.code, fh, stateid)
    checklist(res, [NFS4_OK, NFS4ERR_OPENMODE],
              "Write-locking a read-mode file")
    if res.status == NFS4_OK:
        t.pass_warn("Allowed write-locking a read-mode file, "
                    "not POSIX-compliant")

def testZeroLen(t, env):
    """LOCK with len=0 should return NFS4ERR_INVAL

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK5
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 25, 0)
    check(res, NFS4ERR_INVAL, "LOCK with len=0")

def testLenTooLong(t, env):
    """LOCK with offset+len overflow should return NFS4ERR_INVAL

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK6
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 100, 0xfffffffffffffffe)
    check(res, NFS4ERR_INVAL, "LOCK with offset+len overflow")

def testNoFh(t, env):
    """LOCK with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: lock emptyfh all
    DEPEND: MKFILE
    CODE: LOCK7
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, None, stateid)
    check(res, NFS4ERR_NOFILEHANDLE, "LOCK with no <cfh>")

def testBadLockSeqid(t, env):
    """LOCK with a bad lockseqid should return NFS4ERR_BAD_SEQID

    FLAGS: lock seqid all
    DEPEND: MKFILE
    CODE: LOCK8a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 0, 25)
    check(res1)
    res2 = c.relock_file(2, fh, res1.lockid, 50, 25)
    check(res2, NFS4ERR_BAD_SEQID, "LOCK with a bad lockseqid=2")

def testBadOpenSeqid(t, env):
    """LOCK with a bad openseqid should return NFS4ERR_BAD_SEQID

    FLAGS: lock seqid all
    DEPEND: MKFILE
    CODE: LOCK8b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, openseqid=50)
    check(res, NFS4ERR_BAD_SEQID, "LOCK with a bad openseqid=50")

def testNonzeroLockSeqid(t, env):
    """LOCK with newlockowner should set lockid to 0

    FLAGS: lock seqid all
    DEPEND: MKFILE
    CODE: LOCK8c
    """
    # FRED - if it must be 0, why is it an option?
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, lockseqid=1)
    check(res, NFS4ERR_BAD_SEQID, "LOCK with newlockowner's lockseqid=1")

def testOldLockStateid(t, env):
    """LOCK with old lock stateid should return NFS4ERR_OLD_STATEID

    FLAGS: lock oldid all
    DEPEND: MKFILE
    CODE: LOCK9a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 0, 25)
    check(res1)
    res2 = c.relock_file(1, fh, res1.lockid, 50, 25)
    check(res2)
    res3 = c.relock_file(2, fh, res1.lockid, 100, 25)
    check(res3, NFS4ERR_OLD_STATEID, "LOCK with old lockstateid",
          [NFS4ERR_BAD_STATEID])

def testOldOpenStateid(t, env):
    """LOCK with old open stateid should return NFS4ERR_OLD_STATEID

    FLAGS: lock oldid all
    DEPEND: MKFILE
    CODE: LOCK9b
    """
    c = env.c1
    c.init_connection()
    fh, oldstateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                      deny=OPEN4_SHARE_DENY_NONE)
    fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.downgrade_file(t.code, fh, stateid,
                           access=OPEN4_SHARE_ACCESS_READ,
                           deny=OPEN4_SHARE_DENY_NONE)
    check(res)
    stateid = res.resarray[-1].arm.arm.open_stateid
    if oldstateid.seqid == stateid.seqid and oldstateid.other == stateid.other:
        t.fail("Stateid did not change")
    res2 = c.lock_file(t.code, fh, oldstateid, type=READ_LT)
    check(res2, NFS4ERR_OLD_STATEID, "LOCK with old openstateid")

# FRED - see section 8.1.3
# what is correct return?  what is a lock operation?
def testOldOpenStateid2(t, env):
    """LOCK with old open stateid should return NFS4ERR_OLD_STATEID

    FLAGS: lock oldid all
    DEPEND: MKFILE
    CODE: LOCK9c
    """
    c = env.c1
    c.init_connection()
    fh, oldstateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                      deny=OPEN4_SHARE_DENY_NONE)
    fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                 deny=OPEN4_SHARE_DENY_WRITE)
    if oldstateid.seqid == stateid.seqid and oldstateid.other == stateid.other:
        t.fail("Stateid did not change")
    res2 = c.lock_file(t.code, fh, oldstateid, type=READ_LT)
    check(res2, NFS4ERR_OLD_STATEID, "LOCK with old openstateid",
          [NFS4ERR_BAD_STATEID])
    
def testStaleClientid(t, env):
    """LOCK with a bad clientid should return NFS4ERR_STALE_CLIENTID

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK10
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    orig_clientid = c.clientid
    try:
        c.clientid = get_invalid_clientid()
        res = c.lock_file(t.code, fh, stateid, 0, 25)
        check(res, NFS4ERR_STALE_CLIENTID, "LOCK with a bad clientid")
    finally:
        c.clientid = orig_clientid

#FRED - do open and lock stateids
def testBadStateid(t, env):
    """LOCK should return NFS4ERR_BAD_STATEID if use a bad id

    FLAGS: lock badid all
    DEPEND: MKFILE
    CODE: LOCK11
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid4(0, ''))
    check(res, NFS4ERR_BAD_STATEID, "LOCK with a bad stateid")

def testStaleLockStateid(t, env):
    """LOCK with stale lockstateid should return NFS4ERR_STALE_STATEID

    FLAGS: lock staleid all
    DEPEND: MKFILE
    CODE: LOCK12a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res1 = c.lock_file(t.code, fh, stateid, 0, 25)
    check(res1)
    res2 = c.relock_file(1, fh, makeStaleId(res1.lockid), 50, 25)
    check(res2, NFS4ERR_STALE_STATEID, "LOCK with stale lockstateid",
          [NFS4ERR_BAD_STATEID, NFS4ERR_OLD_STATEID])

def testStaleOpenStateid(t, env):
    """LOCK with stale openstateid should return NFS4ERR_STALE_STATEID

    FLAGS: lock staleid all
    DEPEND: MKFILE
    CODE: LOCK12b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "LOCK with stale openstateid",
          [NFS4ERR_BAD_STATEID, NFS4ERR_OLD_STATEID])

def testTimedoutGrabLock(t, env):
    """LOCK: server should release locks of timed out client 

    FLAGS: lock timed all
    DEPEND: MKFILE
    CODE: LOCK13
    """
    c1 = env.c1
    c1.init_connection()
    # Client 1: create a file and get its fh
    fh1, stateid1 = c1.create_confirm(t.code)
    c2 = env.c2
    c2.init_connection()
    # Client 2: open the file
    fh2, stateid2 = c2.open_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    # Client 1: lock file
    res1 = c1.lock_file(t.code, fh1, stateid1)
    check(res1)
    # Now wait, let client1 expire while client2 sends RENEWs
    sleeptime = c2.getLeaseTime() // 2
    for i in range(3):
        env.sleep(sleeptime)
        res = c2.compound([c2.renew_op(c2.clientid)])
        checklist(res, [NFS4_OK, NFS4ERR_CB_PATH_DOWN])
    # Client 2: Lock file, should work since Client 1's lock has expired
    res2 = c2.lock_file(t.code, fh2, stateid2, type=READ_LT)
    check(res2, msg="Locking file after another client's lock expires")

def testGrabLock1(t, env):
    """MULTIPLE owners trying to get lock

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK14
    """
    c = env.c1
    c.init_connection()
    file = c.homedir + [t.code]
    # owner1 creates a file
    fh1, stateid1 = c.create_confirm('owner1', file,
                                     access=OPEN4_SHARE_ACCESS_BOTH,
                                     deny=OPEN4_SHARE_DENY_WRITE)
    # owner2 opens the file
    fh2, stateid2 = c.open_confirm('owner2', file,
                                   access=OPEN4_SHARE_ACCESS_READ,
                                   deny=OPEN4_SHARE_DENY_NONE)
    # owner1 locks the file
    res1 = c.lock_file('owner1', fh1, stateid1, type=WRITE_LT)
    check(res1)
    # owner2 tries to lock the file, should fail
    res2 = c.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2, NFS4ERR_DENIED,
          "Getting read lock when another owner has write lock")
    # owner1 unlocks the file
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # owner2 tries to lock the file, should work now
    res2 = c.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2,
          msg="Getting read lock after another owner has released write lock")

def testGrabLock2(t, env):
    """MULTIPLE clients trying to get lock

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK15
    """
    c1 = env.c1
    c1.init_connection()
    c2 = env.c2
    c2.init_connection()
    file = c1.homedir + [t.code]
    # Client1 creates a file
    fh1, stateid1 = c1.create_confirm('owner1', file,
                                      access=OPEN4_SHARE_ACCESS_BOTH,
                                      deny=OPEN4_SHARE_DENY_WRITE)
    # Client2 opens the file
    fh2, stateid2 = c2.open_confirm('owner2', file,
                                    access=OPEN4_SHARE_ACCESS_READ,
                                    deny=OPEN4_SHARE_DENY_NONE)
    # Client1 locks the file
    res1 = c1.lock_file('owner1', fh1, stateid1, type=WRITE_LT)
    check(res1)
    # Client2 tries to lock the file, should fail
    res2 = c2.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2, NFS4ERR_DENIED,
          "Getting read lock when another owner has write lock")
    # Client1 unlocks the file
    res1 = c1.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # Client2 tries to lock the file, should work now
    res2 = c2.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2,
          msg="Getting read lock after another owner has released write lock")

def testReadLocks1(t, env):
    """MULTIPLE owners all trying to get read locks

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK16
    """
    c = env.c1
    c.init_connection()
    file = c.homedir + [t.code]
    # owner1 creates a file
    fh1, stateid1 = c.create_confirm('owner1', file,
                                     access=OPEN4_SHARE_ACCESS_BOTH,
                                     deny=OPEN4_SHARE_DENY_NONE)
    # owner2 opens the file
    fh2, stateid2 = c.open_confirm('owner2', file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    # owner1 read locks the file
    res1 = c.lock_file('owner1', fh1, stateid1, type=READ_LT)
    check(res1)
    # owner2 read locks the file
    res2 = c.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2, msg="Getting read lock when another owner has read lock")
    # owner1 write locks the file, should fail
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    res1 = c.relock_file(2, fh1, res1.lockid, type=WRITE_LT)
    check(res1, NFS4ERR_DENIED,
          "Getting write lock when another owner has read lock")
    
def testReadLocks2(t, env):
    """MULTIPLE clients all trying to get read locks

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK17
    """
    c1 = env.c1
    c1.init_connection()
    c2 = env.c2
    c2.init_connection()
    file = c1.homedir + [t.code]
    # Client1 creates a file
    fh1, stateid1 = c1.create_confirm('owner1', file,
                                      attrs={FATTR4_MODE: 0666},
                                      access=OPEN4_SHARE_ACCESS_BOTH,
                                      deny=OPEN4_SHARE_DENY_NONE)
    # Client2 opens the file
    fh2, stateid2 = c2.open_confirm('owner2', file,
                                    access=OPEN4_SHARE_ACCESS_BOTH,
                                    deny=OPEN4_SHARE_DENY_NONE)
    # Client1 read locks the file
    res1 = c1.lock_file('owner1', fh1, stateid1, type=READ_LT)
    check(res1)
    # Client2 read locks the file
    res2 = c2.lock_file('owner2', fh2, stateid2, type=READ_LT)
    check(res2, msg="Getting read lock when another owner has read lock")
    # Client1 write locks the file, should fail
    res1 = c1.unlock_file(1, fh1, res1.lockid)
    check(res1)
    res1 = c1.relock_file(2, fh1, res1.lockid, type=WRITE_LT)
    check(res1, NFS4ERR_DENIED,
          "Getting write lock when another owner has read lock")

##########################################
def testFairness(t, env):
    """MULTIPLE blocking locks may or may not be fair

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK18
    """
    c = env.c1
    c.init_connection()
    # Standard owner opens and locks a file
    fh1, stateid1 = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    res1 = c.lock_file(t.code, fh1, stateid1, type=WRITE_LT)
    check(res1, msg="Locking file %s" % t.code)
    # Second owner is denied a blocking lock
    file = c.homedir + [t.code]
    fh2, stateid2 = c.open_confirm("owner2", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK18")
    check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
    # Standard owner releases lock
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # Third owner tries to butt in and steal lock second owner is waiting for
    file = c.homedir + [t.code]
    fh3, stateid3 = c.open_confirm("owner3", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res3 = c.lock_file("owner3", fh3, stateid3,
                       type=WRITEW_LT, lockowner="lockowner3_LOCK18")
    if res3.status == NFS4_OK:
        t.pass_warn("Locking is not fair")
    check(res3, NFS4ERR_DENIED, msg="Tried to grab lock on %s while another is waiting" % t.code)
    # Second owner goes back and gets his lock
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK18")
    check(res2)

def testBlockPoll(t, env):
    """Can we handle blocking lock polling

    FLAGS: lock timed all
    DEPEND: MKFILE
    CODE: LOCK19
    """
    c = env.c1
    c.init_connection()
    # Standard owner opens and locks a file
    fh1, stateid1 = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    res1 = c.lock_file(t.code, fh1, stateid1, type=WRITE_LT)
    check(res1, msg="Locking file %s" % t.code)
    # Second owner is denied a blocking lock
    file = c.homedir + [t.code]
    fh2, stateid2 = c.open_confirm("owner2", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK19")
    check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
    sleeptime = c.getLeaseTime() // 2
    # Poll for lock
    for i in range(4):
        # Note this renews for standard owner
        env.sleep(sleeptime, "Waiting for lock release")
        res2 = c.lock_file("owner2", fh2, stateid2,
                           type=WRITEW_LT, lockowner="lockowner2_LOCK19")
        check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
    # Standard owner releases lock
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # Third owner tries to butt in and steal lock second owner is waiting for
    file = c.homedir + [t.code]
    fh3, stateid3 = c.open_confirm("owner3", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res3 = c.lock_file("owner3", fh3, stateid3,
                       type=WRITEW_LT, lockowner="lockowner3_LOCK19")
    if res3.status == NFS4_OK:
        t.pass_warn("Locking is not fair")
    check(res3, NFS4ERR_DENIED, msg="Tried to grab lock on %s while another is waiting" % t.code)
    # Second owner goes back and gets his lock
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK19")
    check(res2)

def testBlockTimeout(t, env):
    """Client is denied blocking lock, and then forgets about it

    FLAGS: lock timed all
    DEPEND: MKFILE
    CODE: LOCK20
    """
    c = env.c1
    c.init_connection()
    # Standard owner opens and locks a file
    fh1, stateid1 = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    res1 = c.lock_file(t.code, fh1, stateid1, type=WRITE_LT)
    check(res1, msg="Locking file %s" % t.code)
    # Second owner is denied a blocking lock
    file = c.homedir + [t.code]
    fh2, stateid2 = c.open_confirm("owner2", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK20")
    check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
    sleeptime = c.getLeaseTime() // 2
    # Wait for queued lock to timeout
    for i in range(3):
        env.sleep(sleeptime, "Waiting for queued blocking lock to timeout")
        res = c.compound([c.renew_op(c.clientid)])
        checklist(res, [NFS4_OK, NFS4ERR_CB_PATH_DOWN])
    # Standard owner releases lock
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # Third owner tries to butt in and steal lock second owner is waiting for
    # Should work, since second owner let his turn expire
    file = c.homedir + [t.code]
    fh3, stateid3 = c.open_confirm("owner3", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res3 = c.lock_file("owner3", fh3, stateid3,
                       type=WRITEW_LT, lockowner="lockowner3_LOCK20")
    check(res3, msg="Grabbing lock after another owner let his 'turn' expire")

def testBlockingQueue(t, env):
    """MULTIPLE blocking locks queued up

    FLAGS: lock all
    DEPEND: MKFILE
    CODE: LOCK21
    """
    def geti(owner_name):
        return int(owner_name[5:])
    def reverse(array):
        copy = array[:]
        copy.reverse()
        return copy
    c = env.c1
    c.init_connection()
    num_clients = 5
    file = c.homedir + [t.code]
    owner = ["owner%i" % i for i in range(num_clients)]
    # Create the file
    fh = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)[0]
    # Have each client open the file
    stateid = [None for i in range(num_clients)]
    res = stateid[:]
    for i in range(num_clients):
        stateid[i] = c.open_confirm(owner[i], file,
                                    access=OPEN4_SHARE_ACCESS_BOTH,
                                    deny=OPEN4_SHARE_DENY_NONE)[1]
    seqid = 0
    while owner:
        # Have each client try to lock the file
        for own in owner:
            i = geti(own)
            res[i] = c.lock_file(own, fh, stateid[i], lockseqid=seqid,
                                 type=WRITEW_LT, lockowner="lock%s_LOCK21"%own)
            if own == owner[0]:
                check(res[i], msg="Locking file %s" % t.code)
            else:
                check(res[i], NFS4ERR_DENIED,
                      "Conflicting lock on %s" % t.code)
        # Release the lock
        #seqid += 1
        own = owner[0]
        i = geti(own)
        res[i] = c.unlock_file(1, fh, res[i].lockid)
        # Have clients that are not next in line try to get lock
        for own in reverse(owner[2:]):
            i = geti(own)
            res[i] = c.lock_file(own, fh, stateid[i], lockseqid=seqid,
                                 type=WRITEW_LT, lockowner="lock%s_LOCK21"%own)
            if res[i].status == NFS4_OK:
                t.pass_warn("Locking is not fair")
            check(res[i], NFS4ERR_DENIED,
                  "Tried to grab lock on %s while another is waiting" % t.code)
        #seqid += 1
        # Remove first owner from the fray
        del owner[0]

def testLongPoll(t, env):
    """Check queue not prematurely reaped

    FLAGS: lock timed all citi
    DEPEND: MKFILE
    CODE: LOCK22
    """
    c = env.c1
    c.init_connection()
    # Standard owner opens and locks a file
    fh1, stateid1 = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    res1 = c.lock_file(t.code, fh1, stateid1, type=WRITE_LT)
    check(res1, msg="Locking file %s" % t.code)
    file = c.homedir + [t.code]
    fh2, stateid2 = c.open_confirm("owner2", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    fh3, stateid3 = c.open_confirm("owner3", file,
                                   access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    # Second owner is denied a blocking lock
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK22")
    check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
    sleeptime = c.getLeaseTime() - 5 # Just in time renewal
    badpoll = 0
    timeleft = 2 * sleeptime
    # Poll for lock
    while timeleft:
        time.sleep(1)
        if badpoll:
            # Third owner tries to butt in and steal lock
            res3 = c.lock_file("owner3", fh3, stateid3,
                               type=WRITEW_LT, lockowner="lockowner3_LOCK22")
            if res3.status == NFS4_OK:
                t.pass_warn("Locking is not fair")
            check(res3, NFS4ERR_DENIED,
                  "Tried to grab lock on %s while another is waiting" % t.code)
        if timeleft == sleeptime:
            res2 = c.lock_file("owner2", fh2, stateid2,
                               type=WRITEW_LT, lockowner="lockowner2_LOCK22")
            check(res2, NFS4ERR_DENIED, msg="Conflicting lock on %s" % t.code)
        timeleft -= 1
        badpoll = not badpoll
    # Standard owner releases lock
    res1 = c.unlock_file(1, fh1, res1.lockid)
    check(res1)
    # Third owner tries to butt in and steal lock second owner is waiting for
    res3 = c.lock_file("owner3", fh3, stateid3,
                       type=WRITEW_LT, lockowner="lockowner3_LOCK22")
    if res3.status == NFS4_OK:
        t.pass_warn("Locking is not fair")
    check(res3, NFS4ERR_DENIED, msg="Tried to grab lock on %s while another is waiting" % t.code)
    # Second owner goes back and gets his lock
    res2 = c.lock_file("owner2", fh2, stateid2,
                       type=WRITEW_LT, lockowner="lockowner2_LOCK22")
    check(res2)

####################################
        
# FRED - why again is this impossible?
def xxtestLockowner2(t, env):
    """LOCK owner should not work if reused with 2nd file

    Lockowner is uniquely identified by clientid and an owner string.
    Each lockowner must have its own seqid.  Thus 2 file
    
    FLAGS: lock all
    DEPEND: MKFILE MKDIR
    CODE: LOCK13
    """


###########################################

    def testLockowner(self):
        """LOCK owner should not work after openowner closes file"""
        self.fh, self.stateid = self.ncl.create_confirm()
        lockid = self.ncl.lock_file(self.fh, self.stateid, 25, 75)

        # Close file
        self.ncl.close_file(self.fh, self.stateid)

        # Attempt to keep using lockowner
        lockid = self.ncl.unlock_file(self.fh, lockid, 1, 25, 75,
                                      error=[NFS4ERR_BAD_STATEID])

    def testLockowner2(self):
        """LOCK owner should not work if reused with 2nd file"""
        self.fh, self.stateid = self.ncl.create_confirm(owner="LOCK2")
        lockid = self.ncl.lock_file(self.fh, self.stateid, 25, 75)
        self.fh2, self.stateid2 = self.ncl.create_confirm(name='foo',
                                                          owner="LOCK2")
        lockid2 = self.ncl.lock_file(self.fh2, self.stateid2, 25, 75,
                                     error=[NFS4ERR_BAD_STATEID])

    def testLockowner3(self):
        """LOCK owner with same name as openowner"""
        self.fh, self.stateid = self.ncl.create_confirm(owner="LOCK3")
        lockid = self.ncl.lock_file(self.fh, self.stateid, 25, 75, owner="LOCK3")

        # Attempt to keep using lockowner
        lockid = self.ncl.unlock_file(self.fh, lockid, 1, 25, 75)

        # Close file
        self.ncl.close_file(self.fh, self.stateid)

    def testLockowner4(self):
        """LOCK owner created twice on same file should fail"""
        self.fh, self.stateid = self.ncl.create_confirm(owner="Lockowner4")
        lockid1 = self.ncl.lock_file(self.fh, self.stateid, 25, 75, owner="LOCK4")
        self.fh, self.stateid = self.ncl.open_confirm(owner="Lockowner4")
        lockid2 = self.ncl.lock_file(self.fh, self.stateid, 150, 75, owner="LOCK4", error=[NFS4ERR_BAD_STATEID])

    def testLockowner5(self):
        """LOCK owner created twice on two opens of same file should fail"""
        self.fh, self.stateid = self.ncl.create_confirm()
        lockid1 = self.ncl.lock_file(self.fh, self.stateid, 25, 75, owner="LOCK5")
        self.fh2, self.stateid2 = self.ncl.open_confirm()
        lockid2 = self.ncl.lock_file(self.fh2, self.stateid2, 150, 75, owner="LOCK5", error=[NFS4ERR_BAD_STATEID])

    def testRepeatedLock(self):
        """LOCK a regular file twice using newowner should fail"""
        self.fh, self.stateid = self.ncl.create_confirm()
        self.ncl.lock_file(self.fh, self.stateid)
        self.ncl.lock_test(self.fh)
        self.ncl.lock_file(self.fh, self.stateid, error=[NFS4ERR_BAD_STATEID])

