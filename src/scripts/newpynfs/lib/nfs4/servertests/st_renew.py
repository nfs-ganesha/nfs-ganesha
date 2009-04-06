from nfs4.nfs4_const import *
from environment import check

def testRenew(t, env):
    """RENEW with valid clientid

    FLAGS: renew all
    CODE: RENEW1
    """
    c = env.c1
    c.init_connection()
    res = c.compound([c.renew_op(c.clientid)])
    check(res, msg="RENEW")

def testBadRenew(t, env):
    """RENEW with bad clientid should return NFS4ERR_STALE_CLIENTID

    FLAGS: renew all
    CODE: RENEW2
    """
    c = env.c1
    res = c.compound([c.renew_op(0)])
    check(res, NFS4ERR_STALE_CLIENTID, "RENEW with bad clientid")

def testExpired(t, env):
    """RENEW with expired lease should return NFS4ERR_EXPIRED

    FLAGS: renew timed all
    CODE: RENEW3
    """
    c = env.c1
    c.init_connection()
    c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_BOTH,
                     deny=OPEN4_SHARE_DENY_BOTH)
    sleeptime = c.getLeaseTime() * 3 / 2
    env.sleep(sleeptime)
    # Force server to purge c1 state
    c2 = env.c2
    c2.init_connection()
    c2.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                    deny=OPEN4_SHARE_DENY_NONE)
    res = c.compound([c.renew_op(c.clientid)])
    check(res, NFS4ERR_EXPIRED, "RENEW with expired lease")

