from base_st_classes import *

class CompoundSuite(NFSSuite):
    """Test COMPOUND procedure

    Equivalence partitioning:

    Input Condition: tag
        Valid equivalence classes:
            no tag (0)
            tag (1)
        Invalid equivalence classes:
            -
    Input Condition: minorversion
        Valid equivalence classes:
            supported minorversions(2)
        Invalid equivalence classes:
            unsupported minorversions(3)
    Input Condition: argarray
        Valid equivalence classes:
            valid operations array(4)
        Invalid equivalence classes:
            invalid operations array(5)

    """

    #
    # Testcases covering valid equivalence classes.
    #
    def testZeroOps(self):
        """Test COMPOUND without operations

        Covered valid equivalence classes: 0, 2, 4
        """
        res = self.do_compound([])
        self.assert_OK(res)

    def testWithTag(self):
        """Simple COMPOUND with tag

        Covered valid equivalence classes: 1, 2, 4
        """
        res = self.do_compound([self.putrootfhop], tag="nfs4st.py test tag")
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testInvalidMinor(self):
        """Test COMPOUND with invalid minor version

        Covered invalid equivalence classes: 3

        Comments: Also verifies that the result array after
        NFS4ERR_MINOR_VERS_MISMATCH is empty.

        """
        res = self.do_compound([self.putrootfhop], minorversion=0xFFFF)
        self.assert_status(res, [NFS4ERR_MINOR_VERS_MISMATCH])

        self.failIf(res.resarray, "expected empty result array after"\
                    "NFS4ERR_MINOR_VERS_MISMATCH")

    def _verify_notsupp(self, opnum, valid_opnum_fn):
        """Verify COMPOUND result for undefined operations"""

        # nfs4types.nfs_argop4 does not allow packing invalid operations.
        class custom_nfs_argop4:
            def __init__(self, ncl, argop):
                self.ncl = ncl
                self.packer = ncl.packer
                self.unpacker = ncl.unpacker
                self.argop = argop

            def pack(self, dummy=None):
                self.packer.pack_nfs_opnum4(self.argop)

        op = custom_nfs_argop4(self.ncl, argop=opnum)

        try:
            # This *should* raise the BadCompoundRes exception.
            res =  self.ncl.compound([op])
            # Ouch. This should not happen.
            opnum = res.resarray[0].resop
            self.fail("Expected BadCompoundRes exception. INTERNAL ERROR.")
        except BadDiscriminant, e:
            # This should happen.
            self.failIf(not valid_opnum_fn(e.value),
                        "Expected result array with opnum %d, got %d" % (opnum, e.value))
            # We have to do things a little by hand here
            pos = self.ncl.unpacker.get_position()
            data = self.ncl.unpacker.get_buffer()[pos:]
            errcode = nfs4lib.opaque2long(data)
            self.failIf(errcode != NFS4ERR_NOTSUPP,
                        "Expected NFS4ERR_NOTSUPP, got %d" % errcode)
        except rpc.RPCException, e:
            self.fail(e)

    def testUndefinedOps(self):
        """COMPOUND with operations 0, 1, 2 and 200 should return NFS4ERR_NOTSUPP

        Covered invalid equivalence classes: 5

        Comments: The server should return NFS4ERR_NOTSUPP for the
        undefined operations 0, 1 and 2. Although operation 2 may be
        introduced in later minor versions, the server should always
        return NFS4ERR_NOTSUPP if the minorversion is 0.
        """
        self._verify_notsupp(0, lambda x: x == 0)
        self._verify_notsupp(1, lambda x: x == 1)
        self._verify_notsupp(2, lambda x: x == 2)
        # For unknown operations beyound OP_WRITE, the server should return
        # the largest defined operation. It should at least be OP_WRITE!
        self._verify_notsupp(200, lambda x: x > OP_WRITE)
