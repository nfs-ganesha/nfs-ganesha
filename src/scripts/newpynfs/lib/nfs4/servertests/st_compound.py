from nfs4.nfs4_const import *
from nfs4.nfs4_type import nfs_argop4
from nfs4.nfs4_pack import NFS4Packer
from environment import check, checklist, get_invalid_utf8strings
from rpc import RPCError

def testZeroOps(t, env):
    """COMPOUND without operations should return NFS4_OK

    FLAGS: compound all
    CODE: COMP1
    """
    c = env.c1
    res = c.compound([])
    check(res)

def testGoodTag(t, env):
    """COMPOUND with tag

    FLAGS: compound all
    CODE: COMP2
    """
    c = env.c1
    tag = 'tag test'
    res = c.compound([c.putrootfh_op()], tag)
    check(res)
    if res.tag != tag:
        t.fail("Returned tag '%s' does not equal sent tag '%s'" %
               (res.tag, tag))

def testBadTags(t, env):
    """COMPOUND with invalid utf8 tags

    FLAGS: compound utf8 all
    CODE: COMP3
    """
    c = env.c1
    for tag in get_invalid_utf8strings():
        res = c.compound([c.putrootfh_op()], tag)
        check(res, NFS4ERR_INVAL, "Compound with invalid utf8 tag %s" %
              repr(tag))
        if res.tag != tag:
            t.fail("Returned tag %s does not equal sent tag %s" %
                   (repr(res.tag), repr(tag)))

def testInvalidMinor(t, env):
    """COMPOUND with invalid minor version returns NFS4ERR_MINOR_VERS_MISMATCH

    FLAGS: compound all
    CODE: COMP4
    """
    c = env.c1
    res = c.compound([c.putrootfh_op()], minorversion=50)
    check(res, NFS4ERR_MINOR_VERS_MISMATCH,
          "COMPOUND with invalid minor version")
    if res.resarray:
        t.fail("Nonempty result array after NFS4ERR_MINOR_VERS_MISMATCH")

def testUndefined(t, env):
    """COMPOUND with ops 0, 1, 2 and 200 should return NFS4ERR_OP_ILLEGAL

    Comments: The server should return NFS4ERR_OP_ILLEGAL for the
    undefined operations 0, 1 and 2. Although operation 2 may be
    introduced in later minor versions, the server should always
    return NFS4ERR_NOTSUPP if the minorversion is 0.

    FLAGS: compound all
    CODE: COMP5
    """
    # pack_nfs_argop4 does not allow packing invalid operations.
    opnum = OP_ILLEGAL
    class custom_packer(NFS4Packer):
        def pack_nfs_argop4(self, data):
            self.pack_int(data.argop)
    c = env.c1
    origpacker = c.nfs4packer
    try:
        c.nfs4packer = custom_packer()
        for opnum in [OP_ILLEGAL, 0, 1, 2, 200]:
            try:
                res = c.compound([nfs_argop4(argop=opnum)])
                check(res, NFS4ERR_OP_ILLEGAL, "Sent illegal op=%i" % opnum)
            except RPCError, e:
                t.fail("COMPOUND with illegal op=%i got %s, "
                       "expected NFS4ERR_OP_ILLEGAL" % (opnum,e))
    finally:
        c.nfs4packer = origpacker

def testLongCompound(t, env):
    """COMPOUND with very long argarray
    
    FLAGS: compound all
    CODE: COMP6
    """
    c = env.c1
    baseops = [c.putrootfh_op(), c.getfh_op(), c.getattr([FATTR4_SIZE])]
    step = 50
    count = 0
    try:
        while 1:
            count += step
            res = c.compound(baseops * count)
            checklist(res, [NFS4_OK, NFS4ERR_RESOURCE],
                      "COMPOUND with len=%i argarry" % (3*count))
            if res.status == NFS4ERR_RESOURCE:
                return
    except RPCError, e:
        t.fail("COMPOUND with len=%i argarry got %s, "
               "expected NFS4ERR_RESOURCE" % (3*count, e))
