from base_st_classes import *

class ReaddirSuite(NFSSuite):
    """Test operation 26: READDIR

    FIXME: More testing of dircount/maxcount combinations.
    Note: maxcount represents READDIR4resok. Test this.
    fattr4_rdattr_error vs. global error

    Equivalence partitioning:
        
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: cookie
        Valid equivalence classes:
            zero(20)
            nonzero valid cookie(21)
        Invalid equivalence classes:
            invalid cookie(22)
    Input Condition: cookieverf
        Valid equivalence classes:
            zero(30)
            nonzero valid verifier(31)
        Invalid equivalence classes:
            invalid verifier(32)
    Input Condition: dircount
        Valid equivalence classes:
            zero(40)
            nonzero(41)
        Invalid equivalence classes:
            -
    Input Condition: maxcount
        Valid equivalence classes:
            nonzero(50)
        Invalid equivalence classes:
            zero(51)
    Input Condition: attrbits
        Valid equivalence classes:
            all requests without FATTR4_*_SET (60)
        Invalid equivalence classes:
            requests with FATTR4_*_SET (61)

    Comments: It's not possible to cover eq. class 22, since the cookie is
    opaque to the client. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testFirst(self):
        """READDIR with cookie=0, maxcount=4096

        Covered valid equivalence classes: 10, 20, 30, 40, 50, 60
        """        
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_OK(res)
        
        
    def testSubsequent(self):
        """READDIR with cookie from previus call

        Covered valid equivalence classes: 10, 21, 31, 41, 50, 60
        """
        # FIXME: Implement rest of testcase, as soon as
        # CITI supports dircount/maxcount. 
        self.info_message("(TEST NOT IMPLEMENTED)")

        # Call READDIR with small maxcount, to make sure not all
        # entries are returned. Save cookie. 

        # Call READDIR a second time with saved cookie.

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        operations.append(readdirop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """READDIR without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        res = self.do_compound([readdirop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
        
    def testInvalidCookieverf(self):
        """READDIR with invalid cookieverf should return NFS4ERR_BAD_COOKIE

        Covered invalid equivalence classes: 32
        """
        # FIXME: Implement rest of testcase, as soon as
        # CITI supports dircount/maxcount. 
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testMaxcountZero(self):
        """READDIR with maxcount=0 should return NFS4ERR_READDIR_NOSPC
        
        Covered invalid equivalence classes: 51
        """
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=0,
                                        attr_request=[])
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_status(res, [NFS4ERR_READDIR_NOSPC])

    def testWriteOnlyAttributes(self):
        """READDIR with attrs=FATTR4_*_SET should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 61

        Comments: See GetattrSuite.testWriteOnlyAttributes. 
        """
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_ACCESS_SET])
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=attrmask)
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_status(res, [NFS4ERR_INVAL])
        

    #
    # Extra tests.
    #
    def testUnaccessibleDir(self):
        """READDIR with (cfh) in unaccessible directory

        Extra test
        
        Comments: This test crashes/crashed the Linux server
        """
        # FIXME: This test is currently broken.
        self.info_message("(TEST DISABLED)")
        return
        lookupops = self.ncl.lookup_path(self.notaccessibledir)
        operations = [self.putrootfhop] + lookupops

        attrmask = nfs4lib.list2attrmask([FATTR4_TYPE, FATTR4_SIZE, FATTR4_TIME_MODIFY])
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=2, maxcount=4096,
                                        attr_request=attrmask)
        operations.append(readdirop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ACCES])

    def testDots(self):
        """READDIR should not return . and .. in /doc

        Extra test

        Although some servers may actually support files named "." and
        "..", no files named "." or ".." should exist in /doc.
        """
        # Lookup fh for /doc
        fh = self.do_rpc(self.ncl.do_getfh, self.dirfile)

        # Get entries
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        names = [entry.name for entry in entries]

        self.failIf("." in names,
                    "READDIR in /doc returned .-entry")

        self.failIf(".." in names,
                    "READDIR in /doc returned ..-entry")

    def testStrangeNames(self):
        """READDIR should obey OPEN naming policy

        Extra test

        Comments: Verifying that readdir obeys the same naming policy
        as OPEN.
        """
        self.init_connection()
        
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        fh = self.do_rpc(self.ncl.do_getfh, self.tmp_dir) 
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        readdir_names = [entry.name for entry in entries]

        # Verify that READDIR returned all accepted_names
        missing_names = []
        for name in accepted_names:
            if name not in readdir_names:
                missing_names.append(name)

        self.failIf(missing_names, "Missing names in READDIR results: %s" \
                    % missing_names)

        # ... and nothing more
        extra_names = []
        for name in readdir_names:
            if not name in accepted_names:
                extra_names.append(name)

        self.failIf(extra_names, "Extra names in READDIR results: %s" \
                    % extra_names)
