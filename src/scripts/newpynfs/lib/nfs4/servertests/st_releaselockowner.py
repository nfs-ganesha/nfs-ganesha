from nfs4.nfs4_const import *
from nfs4.nfs4_type import lock_owner4
from environment import check, checklist

def testFile(t, env):
    """RELEASE_LOCKOWNER - basic test

    FLAGS: releaselockowner all
    DEPEND:
    CODE: RLOWN1
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.lock_file(t.code, fh, stateid, lockowner="lockowner_RLOWN1")
    check(res)
    res = c.unlock_file(1, fh, res.lockid)
    check(res)
    

    # Release lockowner
    owner = lock_owner4(c.clientid, "lockowner_RLOWN1")
    res = c.compound([c.release_lockowner_op(owner)])
    check(res)
