from base_st_classes import *

class CommitSuite(NFSSuite):
    """Test operation 5: COMMIT

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
        Invalid equivalence classes:
            link(2)
            special object(3)
            dir(7)
            invalid filehandle(8)
    Input Condition: offset
        Valid equivalence classes:
            zero(9)
            nonzero(10)
        Invalid equivalence classes:
            -
    Input Condition: count
        Valid equivalence classes:
            zero(11)
            nonzero(12)
        Invalid equivalence classes:
            -
    Note: We do not examine the writeverifier in any way. It's hard
    since it can change at any time.
    """

    #
    # Testcases covering valid equivalence classes.
    #
    def testOffsets(self):
        """Simple COMMIT on file with offset 0, 1 and 2**64 - 1

        Covered valid equivalence classes: 1, 9, 10, 11

        Comments: This test case tests boundary values for the offset
        parameter in the COMMIT operation. All values are
        legal. Tested values are 0, 1 and 2**64 - 1 (selected by BVA)
        """
        lookupops = self.ncl.lookup_path(self.regfile)

        # Offset = 0
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)

        # offset = 1
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(1, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)
        
        # offset = 2**64 - 1
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0xffffffffffffffffL, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)

    def _testWithCount(self, count):
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, count))
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testCounts(self):
        """COMMIT on file with count 0, 1 and 2**32 - 1

        Covered valid equivalence classes: 1, 9, 11, 12

        This test case tests boundary values for the count parameter
        in the COMMIT operation. All values are legal. Tested values
        are 0, 1 and 2**32 - 1 (selected by BVA)
        """
        # count = 0
        self._testWithCount(0)

        # count = 1
        self._testWithCount(1)
        
        # count = 2**32 - 1
        self._testWithCount(0xffffffffL)

    #
    # Testcases covering invalid equivalence classes
    #
    def _testOnObj(self, obj, expected_response):
        lookupops = self.ncl.lookup_path(obj)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, 0))
        res = self.do_compound(operations)
        self.assert_status(res, [expected_response])
    
    def testOnLink(self):
        """COMMIT should fail with NFS4ERR_SYMLINK on links

        Covered invalid equivalence classes: 2
        """
        self._testOnObj(self.linkfile, NFS4ERR_SYMLINK)

    def testOnSpecials(self):
        """COMMIT on special objects should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 3
        """
        for obj in self.special_objects:
            self._testOnObj(obj, NFS4ERR_INVAL)

    def testOnDir(self):
        """COMMIT should fail with NFS4ERR_ISDIR on directories

        Covered invalid equivalence classes: 7
        """
        self._testOnObj(self.dirfile, NFS4ERR_ISDIR)
        
    def testWithoutFh(self):
        """COMMIT should return NFS4ERR_NOFILEHANDLE if called without filehandle.

        Covered invalid equivalence classes: 8
        """
        commitop = self.ncl.commit_op(0, 0)
        res = self.do_compound([commitop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    #
    # Extra tests
    #
    def testOverflow(self):
        """COMMIT on file with offset+count >= 2**64 should fail

        Extra test

        Comments: If the COMMIT operation is called with an offset
        plus count that is larger than 2**64, the server should return
        NFS4ERR_INVAL
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(-1, -1))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])
