from base_st_classes import *

class VerifySuite(NFSSuite):
    """Test operation 37: VERIFY

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
    Input Condition: fattr.attrmask
        Valid equivalence classes:
            valid attribute(9)
        Invalid equivalence classes:
            invalid attrmask(10) (FATTR4_*_SET)
    Input Condition: fattr.attr_vals
        Valid equivalence classes:
            same attributes(11)
            not same attributes(12)
        Invalid equivalence classes:
            attr with invalid utf8(13)

    """
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testSame(self):
        """VERIFY with same attributes should execute remaining ops

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 11
        """
        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do verify with same filesize
        # get filesize again, and check if it match previous size.
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            # New getattr
            operations.append(self.ncl.getattr([FATTR4_SIZE]))

            res = self.do_compound(operations)

            self.assert_OK(res)

            # Assert the new getattr was executed.
            # File sizes should match. 
            obj = res.resarray[-1].arm.arm.obj_attributes
            d =  nfs4lib.fattr2dict(obj)
            new_size = d["size"]
            self.failIf(objsize != new_size,
                        "GETATTR after VERIFY returned different filesize")

    def testNotSame(self):
        """VERIFY with not same attributes should return NFS4ERR_NOT_SAME

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 12
        """
        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do verify with wrong filesize. 
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize + 17, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            res = self.do_compound(operations)

            self.assert_status(res, [NFS4ERR_NOT_SAME])

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """VERIFY without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 8
        """
        attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        verifyop = self.ncl.verify_op(obj_attributes)
        res = self.do_compound([verifyop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testWriteOnlyAttributes(self):
        """VERIFY with FATTR4_*_SET should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 10

        Comments: See GetattrSuite.testWriteOnlyAttributes. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        # Verify
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_ACCESS_SET])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.verify_op(obj_attributes))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """VERIFY with non-UTF8 FATTR4_OWNER should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_OWNER])
            dummy_ncl = nfs4lib.DummyNcl()
            dummy_ncl.packer.pack_utf8string(name)
            attr_vals = dummy_ncl.packer.get_buffer()
            obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])
