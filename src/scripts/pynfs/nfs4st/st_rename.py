from base_st_classes import *

class RenameSuite(NFSSuite):
    """Test operation 29: RENAME

    FIXME: Test renaming of a named attribute
    to be a regular file and vice versa.

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            non-dir(11)
            no filehandle(12)
            invalid filehandle(13)
    Input Condition: oldname
        Valid equivalence classes:
            valid name(20)
        Invalid equivalence classes:
            non-existent name(21)
            zerolength(22)
            non-utf8(23)
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(30)
        Invalid equivalence classes:
            non-dir(31)
            no filehandle(32)
            invalid filehandle(33)
    Input Condition: newname
        Valid equivalence classes:
            valid name(40)
        Invalid equivalence classes:
            zerolength(41)
            non-utf8(42)

    Comments: It's not possible to cover eq. class 32, since saving a filehandle
    gives a current filehandle as well. 
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.oldname = "object1"
        self.obj_name = self.oldname # Easier call of create_object()
        self.newname = "object2"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    def _prepare_operation(self):
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.lookup_dir_ops)
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        return operations

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Test valid RENAME operation

        Covered valid equivalence classes: 10, 20, 30, 40
        """
        if not self.create_object(): return

        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testSfhNotDir(self):
        """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoSfh(self):
        """RENAME without (sfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        # Lookup target directory
        operations = [self.putrootfhop]
        operations.extend(self.lookup_dir_ops)
        
        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    # FIXME: Cover eq. class 13.

    def testNonExistent(self):
        """RENAME on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 21
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op("vapor_object", self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testZeroLengthOldname(self):
        """RENAME with zero length oldname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op("", self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8Oldname(self):
        """RENAME with non-UTF8 oldname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 23

        Comments: There is no need to create the object first; the
        UTF8 check should be done before verifying if the object exists. 
        """
        for name in self.get_invalid_utf8strings():
            operations = self._prepare_operation()

            # Rename
            renameop = self.ncl.rename_op(name, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])
        
    def testCfhNotDir(self):
        """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 31
        """
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.lookup_dir_ops)
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.regfile))

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    # FIXME: Cover eq. class 33.
    
    def testZeroLengthNewname(self):
        """RENAME with zero length newname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 41
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op(self.oldname, "")
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])
    
    def testNonUTF8Newname(self):
        """RENAME with non-UTF8 newname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 42
        """
        for name in self.get_invalid_utf8strings():
            # Create the object to rename 
            if not self.create_object(): return
            operations = self._prepare_operation()

            # Rename
            renameop = self.ncl.rename_op(self.oldname, name)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def _do_test_oldname(self, oldname):
        operations = self._prepare_operation()
        renameop = self.ncl.rename_op(oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])

    def _do_test_newname(self, newname):
        operations = self._prepare_operation()
        renameop = self.ncl.rename_op(self.oldname, newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])
    
    def testDotsOldname(self):
        """RENAME with oldname containing . or .. should return NOENT/INVAL

        Extra test
        
        No files named . or .. should exist in /doc directory. RENAME should
        return NFS4ERR_NOENT (if server supports "." and ".." as file names)
        or NFS4ERR_INVAL. 
        """
        self._do_test_oldname(".")
        self._do_test_oldname("..")

    def testDotsNewname(self):
        """RENAME with newname . or .. should succeed or return OK/INVAL

        Extra test
        """
        # Create dummy object
        if not self.create_object(): return
        # Try to rename it to "."
        self._do_test_newname(".")

        # Create dummy object
        if not self.create_object(): return
        # Try to rename it to ".."
        self._do_test_newname("..")

    def testNamingPolicy(self):
        """RENAME should obey OPEN file name creation policy

        Extra test
        """
        # This test testes the create part of RENAME. 
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_rename) = self.try_file_names(creator=self.create_via_rename)
            self.info_message("Rejected file names by RENAME: %s" \
                              % repr(rejected_names_rename))
            
            
            self.failIf(rejected_names_open != rejected_names_rename,
                        "RENAME does not obey OPEN naming policy")
        except SkipException, e:
            print e

    def testValidNames(self):
        """RENAME should succeed on all legal names

        Extra test

        Comments: This test tries RENAME on all names returned from try_file_names()
        """
        # This test testes the lookup part of RENAME. 
        self.init_connection()

        # Saved files for 
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try RENAME on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = self._prepare_operation()
            renameop = self.ncl.rename_op(filename, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_OK(res)
            # Remove file. 
            self.do_rpc(self.ncl.do_remove, self.tmp_dir + [self.newname])

    def testInvalidNames(self):
        """RENAME should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries RENAME on rejected file names from
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

        # Ok, lets try RENAME on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = self._prepare_operation()
            renameop = self.ncl.rename_op(filename, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])
