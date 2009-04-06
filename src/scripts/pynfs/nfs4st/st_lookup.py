from base_st_classes import *

class LookupSuite(NFSSuite):
    """Test operation 15: LOOKUP

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not directory or symlink(11)
            invalid filehandle(12)
            symlink(13)
    Input Condition: objname
        Valid equivalence classes:
            legal name(20)
        Invalid equivalence classes:
            zero length(21)
            non-utf8(22)
            non-existent object(23)
            non-accessible object(24)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testDir(self):
        """LOOKUP directory

        Covered valid equivalence classes: 10, 20
        """
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.dirfile)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """LOOKUP with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops1 = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookup_op("porting"))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """LOOKUP without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        res = self.do_compound(lookupops)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testSymlinkFh(self):
        """LOOKUP with (cfh) as symlink should return NFS4ERR_SYMLINK

        Covered invalid equivalence classes: 13
        """
        lookupops1 = self.ncl.lookup_path(self.dirsymlinkfile)
        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookup_op("porting"))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_SYMLINK])

    def testNonExistent(self):
        """LOOKUP with non-existent components should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 23
        """
        lookupops = self.ncl.lookup_path(self.vaporfile)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testNonAccessable(self):
        """LOOKUP with non-accessable components should return NFS4ERR_ACCES

        Covered invalid equivalence classes: 24
        """
        # FIXME: This test is currently broken.
        self.info_message("(TEST DISABLED)")
        return
        lookupops = self.ncl.lookup_path(self.notaccessablefile)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ACCES])

    def testNonUTF8(self):
        """LOOKUP with non-UTF8 name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop]
            operations.append(self.ncl.lookup_op(name))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])
            
    #
    # Extra tests.
    #
    def _assert_noent(self, pathcomps):
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testDots(self):
        """LOOKUP on (nonexistent) . and .. should return NFS4ERR_NOENT 

        Extra test

        Comments: Even if the server does not allow creation of files
        called . and .., LOOKUP should return NFS4ERR_NOENT. 
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._assert_noent([testname])
        
        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._assert_noent([testname])

        # Try lookup on ["doc", ".", "README"]
        # First, make sure there is no object named "."
        # in the doc directory
        if not self.make_sure_nonexistent(".", self.dirfile): return
        # Of course it wasn't. Try LOOKUP with this strange path.
        # Note: The file doc/./README actually exists on a UNIX server. 
        self._assert_noent(self.dirfile + [".", "README"])
        
        # Same goes for ".."
        # Note: The file doc/porting/../README actually exists on a
        # UNIX server.
        if not self.make_sure_nonexistent("..", self.docporting):
            return
        self._assert_noent(self.docporting + ["..", "README"])

    def testValidNames(self):
        """LOOKUP should succeed on all legal names

        Extra test

        Comments: This test tries LOOKUP on all names returned from try_file_names()
        """
        self.init_connection()

        # Saved files for LOOKUP
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return

        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try LOOKUP on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.lookup_op(filename))
            res = self.do_compound(operations)
            self.assert_OK(res)

    def testInvalidNames(self):
        """LOOKUP should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries LOOKUP on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case, although
        the server rejects creation of objects with these names
        """
        self.init_connection()

        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try LOOKUP on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.lookup_op(filename))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])
