from nfs4.nfs4_const import *
from environment import checklist

def testSupported(t, env):
    """Do a simple PUTPUBFH

    FLAGS: putpubfh all
    CODE: PUB1
    """

    c = env.c1
    ops = [c.putpubfh_op()]
    res = c.compound(ops)
    checklist(res, [NFS4_OK, NFS4ERR_NOTSUPP])
    if res.status == NFS4ERR_NOTSUPP:
        t.fail_support("PUTPUBFH not supported")
