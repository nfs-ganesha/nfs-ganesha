from nfs4.nfs4_const import *
from environment import check
import os

def testStale(t, env):
    """SETCLIENTID_CONFIRM with unknown id should return NFS4ERR_STALE_CLIENTID

    FLAGS: setclientidconfirm all
    CODE: CIDCF1
    """
    c = env.c1
    res = c.compound([c.setclientid_confirm_op(0,'')])
    check(res, NFS4ERR_STALE_CLIENTID, "SETCLIENTID_CONFIRM with unknown id=0")

def testBadConfirm(t, env):
    """SETCLIENTID_CONFIRM with case not covered in RFC

    FLAGS: setclientidconfirm all
    CODE: CIDCF2
    """
    c = env.c1
    id = "Clientid_for_%s_pid=%i" % (t.code, os.getpid())
    clientid, idconfirm = c.init_connection(id)

    res = c.compound([c.setclientid(id=id)])
    check(res)
    # Now confirm 1st set again, instead of 2nd
    res = c.compound([c.setclientid_confirm_op(clientid, idconfirm)])
    check(res, msg="SETCLIENTID_CONFIRM with case not covered in RFC, "
          "seems most likely should do nothing and")

def testAllCases(t, env):
    """SETCLIENTID_CONFIRM with each case from RFC

    FLAGS: setclientidconfirm all
    CODE: CIDCF3
    """
    c = env.c1
    id = "Clientid_for_%s_pid=%i" % (t.code, os.getpid())
    # no (**c*s), no (**c*s)
    res = c.compound([c.setclientid_confirm_op(0,'')])
    check(res, NFS4ERR_STALE_CLIENTID, "SETCLIENTID_CONFIRM with unknown id=0")
    # no (**c*s) and no (*xd*t), (*xc*s)
    c.init_connection(id)
    # no (**c*s) and (*xd*t), (*xc*s)
    clientid, idconfirm = c.init_connection(id, verifier='')
    # (vxc*s), no (vxc**)
    res = c.compound([c.setclientid_confirm_op(clientid, idconfirm)])
    check(res)
    # (vxc*t), (vxc*s)
    c.init_connection(id, verifier='')

