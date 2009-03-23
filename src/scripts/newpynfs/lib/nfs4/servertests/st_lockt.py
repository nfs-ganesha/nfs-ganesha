from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_clientid

def testUnlockedFile(t, env):
    """LOCKT on a regular unlocked file

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_test(fh)
    check(res, msg="LOCKT on unlocked file")

def testDir(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt dir all
    DEPEND: LOOKDIR
    CODE: LKT2d
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.usedir)
    check(res, NFS4ERR_ISDIR, "LOCKT on non-file object")
   
def testFifo(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt fifo all
    DEPEND: LOOKFIFO
    CODE: LKT2f
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.usefifo)
    check(res, NFS4ERR_INVAL, "LOCKT on non-file object")
   
def testLink(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt symlink all
    DEPEND: LOOKLINK
    CODE: LKT2a
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.uselink)
    check(res, NFS4ERR_INVAL, "LOCKT on non-file object")
   
def testBlock(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt block all
    DEPEND: LOOKBLK
    CODE: LKT2b
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.useblock)
    check(res, NFS4ERR_INVAL, "LOCKT on non-file object")
   
def testChar(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt char all
    DEPEND: LOOKCHAR
    CODE: LKT2c
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.usechar)
    check(res, NFS4ERR_INVAL, "LOCKT on non-file object")
   
def testSocket(t, env):
    """LOCKT on non-file objects)

    FLAGS: lockt socket all
    DEPEND: LOOKSOCK
    CODE: LKT2s
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(env.opts.usesocket)
    check(res, NFS4ERR_INVAL, "LOCKT on non-file object")
   
def testPartialLockedFile1(t, env):
    """LOCKT on an unlocked portion of a locked file

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 100, 50)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh, 0, 50)
    check(res, msg="LOCKT on an unlocked portion of a locked file")
    
def testPartialLockedFile2(t,env):
    """LOCKT on a locked portion of a locked file

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, 100, 50)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh, 125, 50)
    check(res, NFS4ERR_DENIED, "LOCKT on a locked portion of a locked file")
    
def test32bitRange(t, env):
    """LOCKT range over 32 bits should work or return NFS4ERR_BAD_RANGE

    FLAGS: lockt all
    DEPEND: MKFILE LKT1
    CODE: LKT5
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_test(fh, 0, 0xffffffffffff)
    checklist(res, [NFS4_OK, NFS4ERR_BAD_RANGE], "LOCKT range over 32 bits")
    if res.status == NFS4ERR_BAD_RANGE:
        t.fail_support("Server does not support 64 bit lock ranges")
              
def testOverlap(t, env):
    """LOCKT against your own lock should return NFS4_OK or NFS4ERR_LOCK_RANGE

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKTOVER
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    lockowner = "lockowner_LKTOVER"
    res = c.lock_file(t.code, fh, stateid, 100, 50, lockowner=lockowner)
    check(res, msg="Locking file %s" % t.code)
    res = c.lock_test(fh, 100, 50, tester=lockowner)
    check(res, msg="LOCKT against own exactly matching lock")
    res = c.lock_test(fh, 75, 50, tester=lockowner)
    checklist(res, [NFS4_OK, NFS4ERR_LOCK_RANGE],
              "LOCKT against own overlapping lock")
    if res.status == NFS4ERR_LOCK_RANGE:
        t.fail_support("Server does not support lock consolidation")
    
def testZeroLen(t, env):
    """LOCKT with len=0 should return NFS4ERR_INVAL

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT6
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_test(fh, 75, 0)
    check(res, NFS4ERR_INVAL, "LOCKT with len=0")

def testLenTooLong(t, env):
    """LOCKT with offset+len overflow should return NFS4ERR_INVAL

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT7
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_test(fh, 100, 0xfffffffffffffffe)
    check(res, NFS4ERR_INVAL, "LOCKT with offset+len overflow")

def testNoFh(t, env):
    """LOCKT with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: lockt emptyfh all
    CODE: LKT8
    """
    c = env.c1
    c.init_connection()
    res = c.lock_test(None)
    check(res, NFS4ERR_NOFILEHANDLE, "LOCKT with no <cfh>")
    
def testStaleClientid(t, env):
    """LOCK with a bad clientid should return NFS4ERR_STALE_CLIENTID

    FLAGS: lockt all
    DEPEND: MKFILE
    CODE: LKT9
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    orig_clientid = c.clientid
    try:
        c.clientid = get_invalid_clientid()
        res = c.lock_test(fh)
        check(res, NFS4ERR_STALE_CLIENTID, "LOCKT with a bad clientid")
    finally:
        c.clientid = orig_clientid
