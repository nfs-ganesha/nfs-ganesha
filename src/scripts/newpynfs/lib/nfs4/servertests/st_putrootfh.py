from nfs4.nfs4_const import *
from environment import check

def testSupported(t, env):
    """Do a simple PUTROOTFH

    FLAGS: putrootfh all
    CODE: ROOT1
    """
    c = env.c1
    ops = [c.putrootfh_op()]
    res = c.compound(ops)
    check(res)
