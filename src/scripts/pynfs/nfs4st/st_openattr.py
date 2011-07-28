from base_st_classes import *

class OpenattrSuite(NFSSuite):
    """Test operation 19: OPENATTR

    FIXME: Verify that these tests works, as soon as I have access to a server
    that supports named attributes. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            dir(2)
            block(3)
            char(4)
            link(5)
            socket(6)
            FIFO(7)
        Invalid equivalence classes:
            attribute directory(8)
            named attribute(9)
    Input Condition: createdir
        Valid equivalence classes:
            false(20)
            true(21)
        Invalid equivalence classes:
            -
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def _openattr(self, createdir):
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.openattr_op(createdir))
            res = self.do_compound(operations)

            if res.status == NFS4ERR_NOTSUPP:
                path = self.ncl.lookuplist2comps(lookupops)
                self.info_message("OPENATTR not supported on " + str(path))

            self.assert_status(res, [NFS4_OK, NFS4ERR_NOTSUPP])

    def testValidNoCreate(self):
        """OPENATTR on all non-attribute objects, createdir=FALSE

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 20
        """
        self._openattr(FALSE)

    def testValidCreate(self):
        """OPENATTR on all non-attribute objects, createdir=TRUE

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 21
        """
        self._openattr(TRUE)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testOnAttrDir(self):
        """OPENATTR on attribute directory should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 8
        """
        # Open attribute dir for root dir
        openattrop = self.ncl.openattr_op(FALSE)
        res = self.do_compound([self.putrootfhop, openattrop])
        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("OPENATTR not supported on /, cannot try this test")
            return

        openattrop1 = self.ncl.openattr_op(FALSE)
        openattrop2 = self.ncl.openattr_op(FALSE)
        res = self.do_compound([self.putrootfhop, openattrop1, openattrop2])
        self.assert_status(res, [NFS4ERR_INVAL])

    def testOnAttr(self):
        """OPENATTR on attribute should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 9

        Comments: Not yet implemented. 
        """
        # Open attribute dir for doc/README
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.openattr_op(FALSE))
        res = self.do_compound(operations)

        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("OPENATTR not supported on %s, cannot try this test" \
                              % self.regfile)
            return

        # FIXME: Implement rest of testcase.
        self.info_message("(TEST NOT IMPLEMENTED)")
