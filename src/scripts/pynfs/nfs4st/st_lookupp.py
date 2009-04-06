from base_st_classes import *

class LookuppSuite(NFSSuite):
    """Test operation 16: LOOKUPP

    Equivalence partitioning:
        
    Input Condition: current filehandle
        Valid equivalence classes:
            directory(10)
            named attribute dir(11)
        Invalid equivalence classes:
            not directory(12)
            invalid filehandle(13)

    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testDir(self):
        """LOOKUPP with directory (cfh)

        Covered valid equivalence classes: 10
        """
        lookupops1 = self.ncl.lookup_path(self.docporting)

        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookupp_op())
        operations.append(self.ncl.lookup_op("README"))
        
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testNamedAttrDir(self):
        """LOOKUPP with named attribute directory (cfh)

        Covered valid equivalence classes: 11

        Comments: Not yet implemented. 
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testInvalidFh(self):
        """LOOKUPP with non-dir (cfh)

        Covered invalid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops 
        operations.append(self.ncl.lookupp_op())
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    #
    # Extra tests.
    #
    def testAtRoot(self):
        """LOOKUPP with (cfh) at root should return NFS4ERR_NOENT

        Extra test
        """
        lookuppop = self.ncl.lookupp_op()
        res = self.do_compound([self.putrootfhop, lookuppop])
        self.assert_status(res, [NFS4ERR_NOENT])
