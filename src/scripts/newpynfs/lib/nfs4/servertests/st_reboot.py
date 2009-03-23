from nfs4.nfs4_const import *
from environment import check, checklist
import sys
import os

# NOTE - reboot tests are NOT part of the standard test suite

__asked = None

def _ask(t, env):
    global __asked
    if __asked is None:
        print "Reboot tests are not part of the standard test suite."
        if not env.opts.verbose:
            print "Also, it is probably better to use the -v option"
        print "Are your *sure* you want to run them?"
        answer = sys.stdin.readline()
        c = answer.lower()[0]
        __asked = (c=='y')
    return __asked

def _getcount(t, env):
    print "For test %s, how many clientids to use?" % t.code
    answer = sys.stdin.readline()
    try:
         t.__clientcount = int(answer)
         return True
    except:
        return False

def _waitForReboot(c):
    print "Hit ENTER to continue after server is reset"
    oldleasetime = c.getLeaseTime()
    sys.stdin.readline()
    newleasetime = c.getLeaseTime()
    print "Continuing with test"
    return 5 + max(oldleasetime, newleasetime)

#####################################################

def testRebootValid(t, env):
    """REBOOT with valid CLAIM_PREVIOUS

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    sleeptime = _waitForReboot(c)
    try:
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, NFS4ERR_STALE_CLIENTID, "Reclaim using old clientid")
        c.init_connection()
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, msg="Reclaim using newly created clientid")
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testManyClaims(t, env):
    """REBOOT test

    FLAGS: reboot
    DEPEND: _ask _getcount MKDIR MKFILE
    CODE: REBT2
    """
    c = env.c1
    if not hasattr(t, '__clientcount'):
        # default if use --force
        t.__clientcount = 5
    pid = str(os.getpid())
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res, msg="Creating test directory %s" % t.code)
    # Make lots of client ids
    fhdict = {}
    idlist = ['pynfs%s%06i' % (pid, x) for x in range(t.__clientcount)]
    badids = ['badpynfs%s%06i' % (pid, x) for x in range(t.__clientcount)]
    for id in idlist:
        c.init_connection(id)
        fh, stateid = c.create_confirm(t.code, basedir + [id])
        fhdict[id] = fh
    sleeptime = _waitForReboot(c)
    try: 
        # Lots of reclaims
        badfh = fhdict[idlist[-1]]
        for goodid, badid in zip(idlist, badids):
            c.init_connection(goodid)
            res = c.open_file(t.code, fhdict[goodid],
                              claim_type=CLAIM_PREVIOUS,
                              deleg_type=OPEN_DELEGATE_NONE)
            check(res, msg="Reclaim with valid clientid %s" % goodid)
            c.init_connection(badid)
            res = c.open_file(t.code, badfh, claim_type=CLAIM_PREVIOUS,
                              deleg_type=OPEN_DELEGATE_NONE)
            checklist(res, [NFS4ERR_RECLAIM_CONFLICT, NFS4ERR_RECLAIM_BAD],
                      "Reclaim with bad clientid %s" % badid)
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testRebootWait(t, env):
    """REBOOT with late CLAIM_PREVIOUS should return NFS4ERR_NO_GRACE

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    sleeptime = _waitForReboot(c)
    try:
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, NFS4ERR_STALE_CLIENTID, "Reclaim using old clientid")
        c.init_connection()
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")
    res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                      deleg_type=OPEN_DELEGATE_NONE)
    check(res, NFS4ERR_NO_GRACE, "Reclaim after grace period has expired")

def testRebootInvalid(t, env):
    """REBOOT with invalid CLAIM_PREVIOUS

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ)
    sleeptime = _waitForReboot(c)
    try:
        c.init_connection()
        res = c.open_file(t.code, fh, access=OPEN4_SHARE_ACCESS_WRITE,
                          claim_type=CLAIM_PREVIOUS,
                          deleg_type=OPEN_DELEGATE_NONE)
        check(res, NFS4ERR_RECLAIM_CONFLICT,
              "Reclaim with write access, when only had read access",
              [NFS4ERR_RECLAIM_BAD])
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testEdge1(t, env):
    """REBOOT with first edge condition from RFC 3530

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT5
    """
    c1 = env.c1
    c1.init_connection()
    # Client 1: lock file
    fh1, stateid1 = c1.create_confirm(t.code, attrs={FATTR4_MODE:0666})
    res1 = c1.lock_file(t.code, fh1, stateid1)
    check(res1, msg="Client 1 locking file")
    # Let lease expire
    sleeptime = c1.getLeaseTime() * 3 // 2
    env.sleep(sleeptime, "Waiting for lock lease to expire")
    # Client 2: come in and grab lock
    c2 = env.c2
    c2.init_connection()
    fh2, stateid2 = c2.open_confirm(t.code)
    res2 = c2.lock_file(t.code, fh2, stateid2)
    check(res2, msg="Client 2 grabbing lock from expired client 1")
    # Client2: now unlock and release the file
    res2 = c2.unlock_file(1, fh2, res2.lockid)
    check(res2, msg="Client 2 releasing lock")
    res2 = c2.close_file(t.code, fh2, stateid2)
    check(res2, msg="Client 2 closing file")
    # Server reboots
    sleeptime = _waitForReboot(c2)
    try:
        # Client 1: Reclaim lock (should not work, since #2 has interfered)
        res1 = c1.compound([c1.renew_op(c1.clientid)])
        check(res1, NFS4ERR_STALE_CLIENTID, "RENEW after reboot")
        c1.init_connection()
        res1 = c1.open_file(t.code, fh1, claim_type=CLAIM_PREVIOUS,
                            deleg_type=OPEN_DELEGATE_NONE)
        check(res1, NFS4ERR_RECLAIM_CONFLICT,
              "Reclaim lock that has been interfered with",
              [NFS4ERR_RECLAIM_BAD])
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testEdge2(t, env):
    """REBOOT with second edge condition from RFC 3530

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT6
    """
    c1 = env.c1
    c1.init_connection()
    # Client 1: lock file
    fh1, stateid1 = c1.create_confirm(t.code, attrs={FATTR4_MODE:0666})
    res1 = c1.lock_file(t.code, fh1, stateid1)
    check(res1, msg="Client 1 locking file")
    # Server reboots
    sleeptime = _waitForReboot(c1)
    # Let grace period expire
    env.sleep(sleeptime, "Waiting for grace period to end")
    # Client 2: come in and grab lock
    c2 = env.c2
    c2.init_connection()
    fh2, stateid2 = c2.open_confirm(t.code)
    res2 = c2.lock_file(t.code, fh2, stateid2)
    check(res2, msg="Client 2 grabbing lock from expired client 1")
    # Client2: now unlock and release the file
    res2 = c2.unlock_file(1, fh2, res2.lockid)
    check(res2, msg="Client 2 releasing lock")
    res2 = c2.close_file(t.code, fh2, stateid2)
    check(res2, msg="Client 2 closing file")
    # Server reboots
    sleeptime = _waitForReboot(c2)
    try:
        # Client 1: Reclaim lock (should not work, since #2 has interfered)
        res1 = c1.compound([c1.renew_op(c1.clientid)])
        check(res1, NFS4ERR_STALE_CLIENTID, "RENEW after reboot")
        c1.init_connection()
        res1 = c1.open_file(t.code, fh1, claim_type=CLAIM_PREVIOUS,
                            deleg_type=OPEN_DELEGATE_NONE)
        check(res1, NFS4ERR_RECLAIM_CONFLICT,
              "Reclaim lock that has been interfered with",
              [NFS4ERR_RECLAIM_BAD])
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testRootSquash(t, env):
    """REBOOT root squash does not work after grace ends?

    FLAGS: reboot
    DEPEND: _ask MKFILE MKDIR
    CODE: REBT7
    """
    # Note this assumes we can legally use uid 0...either we are using
    # a secure port or an insecure server
    if env.opts.security != 'sys' or env.opts.uid != 0:
        t.fail_support("Test only works run as root with AUTH_SYS")
    c = env.c1
    c.init_connection()
    c.maketree([t.code])
    
    # See if we are using root squashing
    oldowner = c.do_getattr(FATTR4_OWNER, c.homedir + [t.code])
    oldname = oldowner.split('@')[0]
    if oldname == 'root':
        t.fail_support("No root squashing detected")
    print "Detected root squashing: root -> %s" % oldname
    
    # Wait for grace period to have *just* expired
    _waitForReboot(c)
    c.init_connection()
    while 1:
        res = c.create_file(t.code, c.homedir + [t.code, 'file'])
        checklist(res, [NFS4_OK, NFS4ERR_GRACE], "Creating file")
        if res.status == NFS4ERR_GRACE:
            env.sleep(1, "Waiting for grace period to *just* finish")
        else:
            break
    fh, stateid = c.confirm(t.code, res)
    newowner = c.do_getattr(FATTR4_OWNER, fh)
    if newowner != oldowner:
        t.fail("Before reboot, root->%s.  After reboot, root->%s." %
               (oldowner, newowner))

def testValidDeleg(t, env):
    """REBOOT with read delegation and reclaim it

    FLAGS: reboot delegations
    DEPEND: _ask MKFILE
    CODE: REBT8
    """
    from st_delegation import _get_deleg
    c = env.c1
    id = 'pynfs%i_%s' % (os.getpid(), t.code)
    c.init_connection(id, cb_ident=0)
    deleg_info, fh, stateid =_get_deleg(t, c, c.homedir + [t.code],
                                        None, NFS4_OK)
    sleeptime = _waitForReboot(c)
    try:
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                          deleg_type=OPEN_DELEGATE_NONE)
        check(res, NFS4ERR_STALE_CLIENTID, "Reclaim using old clientid")
#        res = c.compound([c.renew_op(c.clientid)])
#        check(res, NFS4ERR_STALE_CLIENTID, "RENEW after reboot")
        c.init_connection(id, cb_ident=0)
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_READ)
        check(res, msg="Reclaim using newly created clientid")
        deleg_info = res.resarray[-2].arm.arm.delegation
        if deleg_info.delegation_type != OPEN_DELEGATE_READ:
            t.fail("Could not reclaim read delegation")
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")
    
def testRebootMultiple(t, env):
    """REBOOT multiple times with valid CLAIM_PREVIOUS

    FLAGS: reboot
    DEPEND: _ask MKFILE
    CODE: REBT10
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    sleeptime = _waitForReboot(c)
    try:
        c.init_connection()
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, msg="Reclaim using newly created clientid")
        sleeptime = _waitForReboot(c)
        c.init_connection()
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, msg="Reclaim using newly created clientid")
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")

def testGraceSeqid(t, env):
    """Make sure NFS4ERR_GRACE bumps seqid

    FLAGS: reboot
    DEPEND:
    CODE: REBT11
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    sleeptime = _waitForReboot(c)
    try:
        c.init_connection()
        res = c.open_file(t.code, fh, claim_type=CLAIM_PREVIOUS,
                       deleg_type=OPEN_DELEGATE_NONE)
        check(res, msg="Reclaim using newly created clientid")
        res = c.open_file(t.code)
        check(res, NFS4ERR_GRACE, "First OPEN during grace period")
        res = c.open_file(t.code)
        check(res, NFS4ERR_GRACE, "Second OPEN during grace period")
    finally:
        env.sleep(sleeptime, "Waiting for grace period to end")
    res = c.open_file(t.code)
    check(res, NFS4_OK, "OPEN after grace period")
     
    
