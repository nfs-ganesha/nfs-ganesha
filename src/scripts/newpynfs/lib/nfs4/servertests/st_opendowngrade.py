from nfs4.nfs4_const import *
from environment import check, makeStaleId

def testRegularOpen(t, env):
    """OPENDOWNGRADE on regular file

    FLAGS: opendowngrade all
    DEPEND: MKFILE
    CODE: OPDG1
    """
    c = env.c1
    c.init_connection()
    c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                     deny=OPEN4_SHARE_DENY_NONE)
    fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                                 deny=OPEN4_SHARE_DENY_NONE)
    res = c.downgrade_file(t.code, fh, stateid, OPEN4_SHARE_ACCESS_READ,
                           deny=OPEN4_SHARE_DENY_NONE)
    check(res, msg="OPENDOWNGRADE on regular file")

def testNewState1(t, env):
    """OPENDOWNGRADE to never opened mode should return NFS4ERR_INVAL

    FLAGS: opendowngrade all
    DEPEND: MKFILE
    CODE: OPDG2
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.downgrade_file(t.code, fh, stateid, OPEN4_SHARE_ACCESS_READ,
                           deny=OPEN4_SHARE_DENY_NONE)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE to never opened mode")

def testNewState2(t, env):
    """OPENDOWNGRADE to never opened mode should return NFS4ERR_INVAL

    FLAGS: opendowngrade all
    DEPEND: MKFILE
    CODE: OPDG3
    """
    c = env.c1
    c.init_connection()
    c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_WRITE,
                     deny=OPEN4_SHARE_DENY_NONE)
    fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                                 deny=OPEN4_SHARE_DENY_NONE)
    res = c.downgrade_file(t.code, fh, stateid, OPEN4_SHARE_ACCESS_READ,
                           deny=OPEN4_SHARE_DENY_NONE)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE to never opened mode")

def testBadSeqid(t, env):
    """OPENDOWNGRADE with bad seqid should return NFS4ERR_BAD_SEQID

    FLAGS: opendowngrade seqid all
    DEPEND: MKFILE
    CODE: OPDG4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, fh, stateid, seqid=50)
    check(res, NFS4ERR_BAD_SEQID, "OPENDOWNGRADE with bad seqid=50")
    
def testBadStateid(t, env):
    """OPENDOWNGRADE with bad stateid should return NFS4ERR_BAD_STATEID

    FLAGS: opendowngrade badid all
    DEPEND: MKFILE
    CODE: OPDG5
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, fh, env.stateid0)
    check(res, NFS4ERR_BAD_STATEID, "OPENDOWNGRADE with bad stateid")

def testStaleStateid(t, env):
    """OPENDOWNGRADE with stale stateid should return NFS4ERR_STALE_STATEID

    FLAGS: opendowngrade staleid all
    DEPEND: MKFILE
    CODE: OPDG6
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, fh, makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "OPENDOWNGRADE with stale stateid")

def testOldStateid(t, env):
    """OPENDOWNGRADE with old stateid should return NFS4ERR_OLD_STATEID

    FLAGS: opendowngrade oldid all
    DEPEND: MKFILE
    CODE: OPDG7
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res, msg="Creating file %s" % t.code)
    oldstateid = res.resarray[-2].arm.arm.stateid
    fh, stateid = c.confirm(t.code, res)
    res = c.downgrade_file(t.code, fh, oldstateid)
    check(res, NFS4ERR_OLD_STATEID, "OPENDOWNGRADE with old stateid")

def testNoFh(t, env):
    """OPENDOWNGRADE with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: opendowngrade emptyfh all
    DEPEND: MKFILE
    CODE: OPDG8
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, None, stateid)
    check(res, NFS4ERR_NOFILEHANDLE, "OPENDOWNGRADE with no <cfh>")

def testDir(t, env):
    """OPENDOWNGRADE using dir

    FLAGS: opendowngrade dir all
    DEPEND: MKFILE LOOKDIR
    CODE: OPDG9d
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.usedir, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
def testLink(t, env):
    """OPENDOWNGRADE using non-file object

    FLAGS: opendowngrade symlink all
    DEPEND: MKFILE LOOKLINK
    CODE: OPDG9a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.uselink, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
def testBlock(t, env):
    """OPENDOWNGRADE using non-file object

    FLAGS: opendowngrade block all
    DEPEND: MKFILE LOOKBLK
    CODE: OPDG9b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.useblock, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
def testChar(t, env):
    """OPENDOWNGRADE using non-file object

    FLAGS: opendowngrade char all
    DEPEND: MKFILE LOOKCHAR
    CODE: OPDG9c
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.usechar, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
def testFifo(t, env):
    """OPENDOWNGRADE using non-file object

    FLAGS: opendowngrade fifo all
    DEPEND: MKFILE LOOKFIFO
    CODE: OPDG9f
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.usefifo, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
def testSocket(t, env):
    """OPENDOWNGRADE using non-file object

    FLAGS: opendowngrade socket all
    DEPEND: MKFILE LOOKSOCK
    CODE: OPDG9s
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.downgrade_file(t.code, env.opts.usesocket, stateid)
    check(res, NFS4ERR_INVAL, "OPENDOWNGRADE with nonfile object",
          [NFS4ERR_BAD_STATEID])
    
