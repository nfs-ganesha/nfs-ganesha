from nfs4.nfs4_const import *
from environment import check

def testReference(t, env):
    """FSLOCATION test of referral node

    This assumes option --usespecial was set to point to correct path

    FLAGS: fslocations
    CODE: FSLOC1
    """
    c = env.c1
    path = env.opts.usespecial
    ops = [c.putrootfh_op(), c.getfh_op()]
    for comp in path:
        ops += [c.lookup_op(comp), c.getfh_op()]
    res = c.compound(ops)
    print res.resarray[-2].opgetfh
    check(res, NFS4ERR_MOVED, "LOOKUP of path indicated by --usespecial")
    fh = res.resarray[-2].opgetfh.resok4.object
    locs = c.do_getattr(FATTR4_FS_LOCATIONS, fh)
    print "After NFS4ERR_MOVED, GETATTR(fs_locations) = %s" % locs
