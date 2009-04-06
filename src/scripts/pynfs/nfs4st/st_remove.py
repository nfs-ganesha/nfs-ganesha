from base_st_classes import *

class RemoveSuite(NFSSuite):
    """Test operation 28: REMOVE

    # FIXME: Test (OPEN, REMOVE, WRITE) sequence. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: filename
        Valid equivalence classes:
            valid, existing name(20)
        Invalid equivalence classes:
            zerolength(21)
            non-utf8(22)
            non-existing name(23)
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"
        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Valid REMOVE on existing object

        Covered valid equivalence classes: 10, 20
        """
        if not self.create_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(self.obj_name))
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.remove_op(self.obj_name))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """REMOVE without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        removeop = self.ncl.remove_op(self.obj_name)
        res = self.do_compound([removeop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testZeroLengthTarget(self):
        """REMOVE with zero length target should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 21
        """
        if not self.create_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(""))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """REMOVE with non-UTF8 components should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22

        Comments: There is no need to create the object first; the
        UTF8 check should be done before verifying if the object exists. 
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] + self.lookup_dir_ops
            operations.append(self.ncl.remove_op(name))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    def testNonExistent(self):
        """REMOVE on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 23
        """
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(self.vaporfilename))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    #
    # Extra tests. 
    #
    def _do_remove(self, name):
        # Lookup /doc
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.dirfile)
        operations.append(self.ncl.remove_op(name))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])
    
    def testDots(self):
        """REMOVE on . or .. should return NFS4ERR_NOENT or NFS4ERR_INVAL
        
        Extra test

        No files named . or .. should exist in doc directory
        """
        # name = .
        self._do_remove(".")

        # name = ..
        self._do_remove("..")

    def testValidNames(self):
        """REMOVE should succeed on all legal names

        Extra test

        Comments: This test tries REMOVE on all names returned from try_file_names()
        """
        # This test testes the lookup part of REMOVE
        self.init_connection()

        # Save files for REMOVE
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try REMOVE on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.remove_op(filename))
            res = self.do_compound(operations)
            self.assert_OK(res)

    def testInvalidNames(self):
        """REMOVE should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries REMOVE on rejected file names from
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

        # Ok, lets try REMOVE on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.remove_op(filename))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])
