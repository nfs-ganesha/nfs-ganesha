from nfs4.nfs4_const import *
from environment import check

# NOTE other tests in restorefh.py
def testNoFh(t, env):
    """SAVEFH should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: savefh emptyfh all
    CODE: SVFH1
    """
    c = env.c1
    res = c.compound([c.savefh_op()])
    check(res, NFS4ERR_NOFILEHANDLE, "SAVEFH with no <cfh>")

