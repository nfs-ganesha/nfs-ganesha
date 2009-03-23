from base_st_classes import *

class GetattrSuite(NFSSuite):
    """Test operation 9: GETATTR

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
            dir(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: attrbits
        Valid equivalence classes:
            all requests without FATTR4_*_SET (9)
        Invalid equivalence classes:
            requests with FATTR4_*_SET (10)
    
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """GETATTR(FATTR4_SIZE) on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.getattr([FATTR4_SIZE]))
            res = self.do_compound(operations)
            self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """GETATTR should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 8
        """
        
        getattrop = self.ncl.getattr([FATTR4_SIZE])
        res = self.do_compound([getattrop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testWriteOnlyAttributes(self):
        """GETATTR(FATTR4_*_SET) should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 10

        Comments: Some attributes are write-only (currently
        FATTR4_TIME_ACCESS_SET and FATTR4_TIME_MODIFY_SET). If GETATTR
        is called with any of these, NFS4ERR_INVAL should be returned.
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_TIME_ACCESS_SET]))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def testAllMandatory(self):
        """Assure GETATTR can return all mandatory attributes

        Extra test

        Comments: A server should be able to return all mandatory
        attributes.
        """

        attrbitnum_dict = nfs4lib.get_attrbitnum_dict()
        all_mandatory_names = [
            "supported_attrs", 
            "type",
            "fh_expire_type",
            "change",
            "size",
            "link_support",
            "symlink_support",
            "named_attr",
            "fsid",
            "unique_handles",
            "lease_time",
            "rdattr_error"]
        all_mandatory = []
        
        for attrname in all_mandatory_names:
            all_mandatory.append(attrbitnum_dict[attrname])

        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr(all_mandatory))
        
        res = self.do_compound(operations)
        self.assert_OK(res)
        obj = res.resarray[-1].arm.arm.obj_attributes
        d = nfs4lib.fattr2dict(obj)

        unsupported = []
        keys = d.keys()
        for attrname in all_mandatory_names:
            if not attrname in keys:
                unsupported.append(attrname)

        if unsupported:
            self.fail("mandatory attributes not supported: %s" % str(unsupported))

    def testUnknownAttr(self):
        """GETATTR should not fail on unknown attributes

        Covered valid equivalence classes: 1, 9

        Comments: This test calls GETATTR with request for attribute
        number 1000.  Servers should not fail on unknown attributes.
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([1000]))

        res = self.do_compound(operations)
        self.assert_OK(res)

    def testEmptyCall(self):
        """GETATTR should accept empty request

        Covered valid equivalence classes: 1, 9

        Comments: GETATTR should accept empty request
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([]))

        res = self.do_compound(operations)
        self.assert_OK(res)

    def testSupported(self):
        """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory

        Covered valid equivalence classes: 1, 9
        
        Comments: GETATTR(FATTR4_SUPPORTED_ATTRS) should return at
        least all mandatory attributes
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_SUPPORTED_ATTRS]))

        res = self.do_compound(operations)
        self.assert_OK(res)

        obj = res.resarray[-1].arm.arm.obj_attributes

        ncl = nfs4lib.DummyNcl(obj.attr_vals)
        intlist = ncl.unpacker.unpack_fattr4_supported_attrs()
        i = nfs4lib.intlist2long(intlist)

        all_mandatory_bits = 2**(FATTR4_RDATTR_ERROR+1) - 1

        returned_mandatories = i & all_mandatory_bits

        self.failIf(not returned_mandatories == all_mandatory_bits,
                    "not all mandatory attributes returned: %s" % \
                    nfs4lib.int2binstring(returned_mandatories)[-12:])

        sys.stdout.flush()
