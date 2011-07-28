from base_st_classes import *

class LinkSuite(NFSSuite):
    """Test operation 11: LINK

    FIXME: Add attribute directory and named attribute testing.
    FIXME: More combinations of invalid filehandle types. 

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
        Invalid equivalence classes:
            dir(7)
            invalid filehandle(8)
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(9)
        Invalid equivalence classes:
            not dir(10)
            invalid filehandle(11)
    Input Condition: newname
        Valid equivalence classes:
            valid name(12)
        Invalid equivalence classes:
            zerolength(13)
            non-utf8(14)

    Comments: It's not possible to cover eq. class 11, since saving a filehandle
    gives a current filehandle as well. 
    """

    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "link1"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    def _prepare_operation(self, file):
        # Put root FH
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(file))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        return operations
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testFile(self):
        """LINK a regular file

        Covered valid equivalence classes: 1, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testLink(self):
        """LINK a symbolic link should succeed or return NFS4ERR_NOTSUPP

        Covered valid equivalence classes: 2, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.linkfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)

        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("LINK a symbolic link is not supported")

        self.assert_status(res, [NFS4_OK, NFS4ERR_NOTSUPP])

    def testBlock(self):
        """LINK a block device

        Covered valid equivalence classes: 3, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.blockfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testChar(self):
        """LINK a character device

        Covered valid equivalence classes: 4, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.charfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testSocket(self):
        """LINK a socket

        Covered valid equivalence classes: 5, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.socketfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    
    def testFIFO(self):
        """LINK a FIFO

        Covered valid equivalence classes: 6, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.fifofile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #

    def testDir(self):
        """LINK a directory should fail with NFS4ERR_ISDIR

        Covered invalid equivalence classes: 7
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.dirfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ISDIR])

    def testNoSfh(self):
        """LINK should fail with NFS4ERR_NOFILEHANDLE if no (sfh)

        Covered invalid equivalence classes: 8

        Comments: LINK should fail with NFS4ERR_NOFILEHANDLE if no
        saved filehandle exists. 
        """
        linkop = self.ncl.link_op(self.obj_name)
        res = self.do_compound([self.putrootfhop, linkop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testCfhNotDir(self):
        """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

        Covered invalid equivalence classes: 10
        """
        if not self.remove_object(): return

        # Put root FH
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory (a file, this time)
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.regfile))

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testZeroLengthName(self):
        """LINK with zero length new name should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        # Link operation
        linkop = self.ncl.link_op("")
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """LINK with non-UTF8 name should return NFS4ERR_INVAL

        Covered valid equivalence classes: 14
        """
        for name in self.get_invalid_utf8strings():
            operations = self._prepare_operation(self.regfile)

            # Link operation
            linkop = self.ncl.link_op(name)
            operations.append(linkop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests.
    #
    def _do_link(self, newname):
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        linkop = self.ncl.link_op(newname)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])
    
    def testDots(self):
        """LINK with newname . or .. should succeed or return NFS4ERR_INVAL

        Extra test

        Comments: Servers supporting . and .. in file names should
        return NFS4_OK. Others should return
        NFS4ERR_INVAL. NFS4ERR_EXIST should not be returned.
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._do_link(testname)

        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._do_link(testname)

    def testNamingPolicy(self):
        """LINK should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_link) = self.try_file_names(creator=self.create_via_link)
            self.info_message("Rejected file names by LINK: %s" \
                              % repr(rejected_names_link))
            
            
            self.failIf(rejected_names_open != rejected_names_link,
                        "LINK does not obey OPEN naming policy")
        except SkipException, e:
            print e
