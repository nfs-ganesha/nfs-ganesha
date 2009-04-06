from base_st_classes import *

class AccessSuite(NFSSuite):
    """Test operation 3: ACCESS

    Note: We do not examine if the "access" result actually corresponds to
    the correct rights. This is hard since the rights for a object can
    change at any time.

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:
    
    Input Condition: current filehandle
        Valid equivalence classes:
            link(1)
            block(2)
            char(3)
            socket(4)
            FIFO(5)
            dir(6)
            file(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: accessreq
        Valid equivalence classes:
            valid accessreq(9)
        Invalid equivalence classes:
            invalid accessreq(10)
    """
            
    
    maxval = ACCESS4_DELETE + ACCESS4_EXECUTE + ACCESS4_EXTEND + ACCESS4_LOOKUP \
             + ACCESS4_MODIFY + ACCESS4_READ

    def valid_access_ops(self):
        result = []
        for i in range(AccessSuite.maxval + 1):
            result.append(self.ncl.access_op(i))
        return result

    def invalid_access_ops(self):
        result = []
        for i in [64, 65, 66, 127, 128, 129]:
            result.append(self.ncl.access_op(i))
        return result
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """ACCESS on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9
        
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.access_op(ACCESS4_READ))
            res = self.do_compound(operations)
            self.assert_OK(res)
        
    def testDir(self):
        """All valid combinations of ACCESS arguments on directory

        Covered valid equivalence classes: 6, 9

        Comments: The ACCESS operation takes an uint32_t as an
        argument, which is bitwised-or'd with zero or more of all
        ACCESS4* constants. This test case tests all valid
        combinations of these constants. It also verifies that the
        server does not respond with a right in "access" but not in
        "supported".
        """
        
        for accessop in self.valid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_OK(res)
            
            supported = res.resarray[1].arm.arm.supported
            access = res.resarray[1].arm.arm.access

            # Server should not return an access bit if this bit is not in supported. 
            self.failIf(access > supported, "access is %d, but supported is %d" % (access, supported))

    def testFile(self):
        """All valid combinations of ACCESS arguments on file

        Covered valid equivalence classes: 7, 9

        Comments: See testDir. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        
        for accessop in self.valid_access_ops():
            operations = [self.putrootfhop] + lookupops
            operations.append(accessop)
            res = self.do_compound(operations)
            self.assert_OK(res)

            supported = res.resarray[-1].arm.arm.supported
            access = res.resarray[-1].arm.arm.access

            # Server should not return an access bit if this bit is not in supported. 
            self.failIf(access > supported, "access is %d, but supported is %d" % (access, supported))

    #
    # Testcases covering invalid equivalence classes.
    #
    def testWithoutFh(self):
        """ACCESS should return NFS4ERR_NOFILEHANDLE if called without filehandle.

        Covered invalid equivalence classes: 8
        
        """
        accessop = self.ncl.access_op(ACCESS4_READ)
        res = self.do_compound([accessop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])


    def testInvalids(self):
        """ACCESS should fail on invalid arguments

        Covered invalid equivalence classes: 10

        Comments: ACCESS should return with NFS4ERR_INVAL if called
        with an illegal access request (eg. an integer with bits set
        that does not correspond to any ACCESS4* constant).
        """
        for accessop in self.invalid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_status(res, [NFS4ERR_INVAL])
    #
    # Extra tests
    #
    def testNoExecOnDir(self):
        """ACCESS4_EXECUTE should never be returned for directory

        Extra test

        Comments: ACCESS4_EXECUTE has no meaning for directories and
        should not be returned in "access" or "supported".
        """
        for accessop in self.valid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_OK(res)
            
            supported = res.resarray[1].arm.arm.supported
            access = res.resarray[1].arm.arm.access

            self.failIf(supported & ACCESS4_EXECUTE,
                        "server returned ACCESS4_EXECUTE for root dir (supported=%d)" % supported)

            self.failIf(access & ACCESS4_EXECUTE,
                        "server returned ACCESS4_EXECUTE for root dir (access=%d)" % access)
