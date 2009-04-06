from base_st_classes import *

class SecinfoSuite(NFSSuite):
    """Test operation 33: SECINFO

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            invalid filehandle(12)
    Input Condition: name
        Valid equivalence classes:
            valid name(20)
        Invalid equivalence classes:
            non-existent object(21)
            zerolength(22)
            non-utf8(23)

    Comments: It's hard to cover eq. class 12, since it's not possible
    PUTFH an invalid filehandle. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SECINFO on existing file

        Covered valid equivalence classes: 10, 20
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_OK(res)

        # Make sure at least one security mechanisms is returned.
        mechanisms = res.resarray[-1].arm.arm
        self.failIf(len(mechanisms) < 1,
                    "SECINFO returned no security mechanisms")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """SECINFO with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNonExistent(self):
        """SECINFO on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 21
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(self.vaporfilename))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testZeroLengthName(self):
        """SECINFO with zero length name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(""))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """SECINFO with non-UTF8 name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 23

        Comments: It does'nt matter that the files does not exist; the UTF8
        check should return NFS4ERR_INVAL anyway. 
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] 
            operations.append(self.ncl.secinfo_op(name))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def testRPCSEC_GSS(self):
        """SECINFO must return at least RPCSEC_GSS

        Extra test
        """
        # FIXME: Since the Linux server always returns NFS4ERR_NOTSUPP right
        # know, this is untested code.
        # FIXME: Also verify that all Kerberos and LIPKEY security triples
        # listed in section 3.2.1.1 and 3.2.1.2 are supported.
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_OK(res)
        mechanisms = res.resarray[-1].arm.arm
        found_rpcsec_gss = 0

        for mech in mechanisms:
            if mech.flavor == RPCSEC_GSS:
                found_rpcsec_gss = 1

        self.failIf(not found_rpcsec_gss,
                    "SECINFO did not return (mandatory) flavor RPCSEC_GSS")


    def _assert_secinfo_response(self, name):
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(name))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])

    def testDots(self):
        """SECINFO on . and .. should return NOENT/INVAL in /doc

        Extra test
        """
        # . should not exist in doc dir
        if not self.make_sure_nonexistent(".", self.dirfile): return
        self._assert_secinfo_response(".")

        # .. should not exist in doc dir
        if not self.make_sure_nonexistent("..", self.dirfile): return
        self._assert_secinfo_response("..")

    # FIXME: Add file name policy tests: testValidNames/testInvalidNames
    # (like with LOOKUP, REMOVE and RENAME)
