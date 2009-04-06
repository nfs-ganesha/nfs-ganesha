from nfs4.nfs4_const import *
from environment import check
import os
import struct, time

def _checkprinciples(t, env):
    """Make sure c1 and c2 have different principles"""
    # STUB
    return True

def testValid(t, env):
    """SETCLIENTID simple call

    FLAGS: setclientid setclientidconfirm all
    CODE: INIT
    """
    env.c1.init_connection()

def testClientReboot(t, env):
    """SETCLIENTID - create a stale client id and use it

    Note CLOSE does not have NFS4ERR_STALE_CLIENTID

    FLAGS: setclientid setclientidconfirm all
    DEPEND: INIT MKFILE
    CODE: CID1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    # This should clean out client state, invalidating stateid
    c.init_connection(verifier='')
    res = c.close_file(t.code, fh, stateid)
    check(res, NFS4ERR_OLD_STATEID,
          "Trying to use old stateid after SETCLIENTID_CONFIRM purges state",
          [NFS4ERR_BAD_STATEID])
    
def testClientUpdateCallback(t, env):
    """SETCLIENTID - make sure updating callback info does not invalidate state

    FLAGS: setclientid setclientidconfirm all
    DEPEND: INIT MKFILE
    CODE: CID1b
    """
    c = env.c1
    # Need to fix id and verf in case using --newverf option
    id = 'pynfs%i_%s' % (os.getpid(), t.code)
    verf = struct.pack('>d', time.time())
    c.init_connection(id, verf)
    fh, stateid = c.create_confirm(t.code)
    c.init_connection(id, verf)
    res = c.close_file(t.code, fh, stateid)
    check(res, msg="Close after updating callback info")
    
def testInUse(t, env):
    """SETCLIENTID with same nfs_client_id.id should return NFS4ERR_CLID_INUSE

    This requires NCL1 and NCL2 to have different principals (UIDs).
    
    FLAGS: setclientid setclientidconfirm all
    DEPEND: _checkprinciples INIT
    CODE: CID2
    """
    c1 = env.c1
    c2 = env.c2
    c1.init_connection("Badid_for_%s_pid=%i" % (t.code, os.getpid()),
                       verifier=c1.verifier)
    ops = [c2.setclientid(id="Badid_for_%s_pid=%i" % (t.code, os.getpid()),
                          verifier=c1.verifier)]
    res = c2.compound(ops)
    check(res, NFS4ERR_CLID_INUSE, "SETCLIENTID with same nfs_client_id.id")
    
def testLoseAnswer(t, env):
    """SETCLIENTID after a client reboot could cause case not covered in RFC

    FLAGS: setclientid all
    DEPEND: INIT
    CODE: CID3
    """
    c = env.c1
    id = "Clientid_for_%s_pid=%i" % (t.code, os.getpid())
    c.init_connection(id)
    res = c.compound([c.setclientid(id=id)])
    check(res)
    # Now assume client reboot, id should stay same, but verifier changes,
    # and we have lost result from second setclientid.
    # This case is not covered in RFC 3530, but should return OK.
    res = c.compound([c.setclientid(id=id, verifier='')])
    check(res, msg="SETCLIENTID case not covered in RFC")
    
def testAllCases(t, env):
    """SETCLIENTID with each case from RFC

    Note: This just runs through the cases, but there seems to be no
    way to check that server is actually doing the correct thing.

    FLAGS: setclientid all
    DEPEND: INIT
    CODE: CID4
    """
    c = env.c1
    id = "Clientid_for_%s_pid=%i" % (t.code, os.getpid())
    # no (*x***), no (*x****)
    res = c.compound([c.setclientid(id=id)])
    check(res)
    # no (*x***), (*x****)
    c.init_connection(id)
    # (vxc**), no (vxc**)
    res = c.compound([c.setclientid(id=id)])
    check(res)
    # (vxc**), (vxc**)
    c.init_connection(id)
    # (*x***), no (*x***)
    res = c.compound([c.setclientid(id=id, verifier='')])
    check(res)
    # (*xc*s), (*xd*t)
    res = c.compound([c.setclientid(id=id, verifier='')])
    check(res)
    
def testLotsOfClients(t, env):
    """SETCLIENTID called multiple times

    FLAGS: setclientid setclientidconfirm all
    DEPEND: INIT MKFILE
    CODE: CID5
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    idlist = ["Clientid%i_for_%s_pid%i" % (x, t.code, os.getpid()) \
              for x in range(1024)]
    for id in idlist:
        c.init_connection(id)
        c.create_confirm(t.code, basedir + [id])

def testNoConfirm(t, env):
    """SETCLIENTID - create a stale clientid, and use it.

    FLAGS: setclientid all
    DEPEND: INIT
    CODE: CID6
    """
    c = env.c1
    id = "Clientid_for_%s_pid=%i" % (t.code, os.getpid())
    res = c.compound([c.setclientid(id)])
    check(res)
    res = c.compound([c.setclientid(id, '')])
    check(res)
    c.clientid = res.resarray[0].arm.arm.clientid
    ops = c.use_obj(c.homedir)
    ops += [c.open(t.code, t.code, OPEN4_CREATE)]
    res = c.compound(ops)
    check(res, NFS4ERR_STALE_CLIENTID,
          "OPEN using clientid that was never confirmed")
