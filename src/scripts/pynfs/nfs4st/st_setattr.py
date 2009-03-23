from base_st_classes import *

class SetattrSuite(NFSSuite):
    """Test operation 34: SETATTR

    FIXME: Test invalid filehandle. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(10)
            dir(11)
            block(12)
            char(13)
            link(14)
            socket(15)
            FIFO(16)
            attribute directory(17)
            named attribute(18)
        Invalid equivalence classes:
            no filehandle(19)
    Input Condition: stateid
        Valid equivalence classes:
            all bits zero(20)
            all bits one(21)
            valid stateid from open(22)
        Invalid equivalence classes:
            invalid stateid(23)
    Input Condition: obj_attributes.attrmask
        Valid equivalence classes:
            writeable attributes without object_size(30)
            writeable attributes with object_size(31)
        Invalid equivalence classes:
            non-writeable attributes(32)
    Input Condition: obj_attributes.attr_vals
        Valid equivalence classes:
            valid attributes(40)
        Invalid equivalence classes:
            invalid attributes(41)
            attr with invalid utf8(42)
    """

    def setUp(self):
        NFSSuite.setUp(self)
        self.new_mode = 0775

    def _setattr_op(self, stateid):
        attrmask = nfs4lib.list2attrmask([FATTR4_MODE])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_uint(self.new_mode)
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        return self.ncl.setattr_op(stateid, obj_attributes)

    def _check_notsupp(self, res):
        if res.status == NFS4ERR_ATTRNOTSUPP:
            self.info_message("SETATTR(FATTR4_MODE) not supported on %s" \
                              % self.regfile)
            return TRUE
        
        return FALSE

    def _getattr_check(self, lookupops):
        # Ok, SETATTR succeeded. Check that GETATTR matches.
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_MODE]))
        res = self.do_compound(operations)
        if res.status != NFS4_OK:
            self.info_message("GETATTR failed, cannot verify if SETATTR was done right")
            return

        obj = res.resarray[-1].arm.arm.obj_attributes
        d =  nfs4lib.fattr2dict(obj)
        mode = d.get("mode") 
        self.failIf(mode != self.new_mode,
                    "GETATTR after SETATTR(FATTR4_MODE) returned different mode")

    def _valid_setattr(self, file, stateval):
        lookupops = self.ncl.lookup_path(file)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, stateval)
        operations.append(self._setattr_op(stateid))

        res = self.do_compound(operations)
        if self._check_notsupp(res): return

        self.assert_status(res, [NFS4_OK, NFS4ERR_ATTRNOTSUPP])
        self._getattr_check(lookupops)

    def _invalid_setattr(self, file, stateval):
        lookupops = self.ncl.lookup_path(file)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, stateval)
        operations.append(self._setattr_op(stateid))

        res = self.do_compound(operations)
        if self._check_notsupp(res): return
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])

    #
    # Testcases covering valid equivalence classes.
    #
    def testStateidOnes(self):
        """SETATTR(FATTR4_MODE) on regular file with stateid=ones
        
        Covered valid equivalence classes: 10, 21, 30, 40
        """
        stateval = nfs4lib.long2opaque(0xffffffffffffffffffffffffL)
        self._valid_setattr(self.regfile, stateval)
        
    def testDir(self):
        """SETATTR(FATTR4_MODE) on directory

        Covered valid equivalence classes: 11, 20, 30, 40
        """
        self._valid_setattr(self.dirfile, "")

    def testBlock(self):
        """SETATTR(FATTR4_MODE) on block device

        Covered valid equivalence classes: 12, 20, 30, 40
        """
        self._valid_setattr(self.blockfile, "")
        

    def testChar(self):
        """SETATTR(FATTR4_MODE) on char device

        Covered valid equivalence classes: 13, 20, 30, 40
        """
        self._valid_setattr(self.charfile, "")

    def testLink(self):
        """SETATTR(FATTR4_MODE) on symbolic link

        Covered valid equivalence classes: 14, 20, 30, 40

        Comments: The response to mode setting on a symbolic link is
        server dependent; about any response is valid. We just test
        and print the result. 
        """
        lookupops = self.ncl.lookup_path(self.linkfile)
        operations = [self.putrootfhop] + lookupops
        stateid = stateid4(self.ncl, 0, "")
        operations.append(self._setattr_op(stateid))
        res = self.do_compound(operations)
        self.info_message("SETATTR(FATTR4_MODE) on symlink returned %s" \
                          % nfsstat4_id[res.status])
        
    def testSocket(self):
        """SETATTR(FATTR4_MODE) on socket

        Covered valid equivalence classes: 15, 20, 30, 40
        """
        self._valid_setattr(self.socketfile, "")
        
    def testFIFO(self):
        """SETATTR(FATTR4_MODE) on FIFO

        Covered valid equivalence classes: 16, 20, 30, 40
        """
        self._valid_setattr(self.fifofile, "")
        
    def testNamedattrdir(self):
        """SETATTR(FATTR4_MODE) on named attribute directory

        Covered valid equivalence classes: 17, 20, 30, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testNamedattr(self):
        """SETATTR(FATTR4_MODE) on named attribute 

        Covered valid equivalence classes: 18, 20, 30, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testChangeSize(self):
        """SETATTR(FATTR4_MODE) with changes to file size and valid stateid

        Covered valid equivalence classes: 10, 22, 31, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """SETATTR without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 19
        """
        stateid = stateid4(self.ncl, 0, "")
        operations = [self._setattr_op(stateid)]

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testInvalidStateid(self):
        """SETATTR with invalid stateid should return NFS4ERR_BAD_STATEID

        Covered invalid equivalence classes: 23
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")
        # FIXME: Move to common method. Check correctness. 
        #stateval = nfs4lib.long2opaque(0x123456)
        #self._valid_setattr(self.regfile, stateval)

    def testNonWriteable(self):
        """SETATTR(FATTR4_LINK_SUPPORT) should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 32

        Comments: FATTR4_LINK_SUPPORT is a read-only attribute and cannot be
        changed via SETATTR. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")

        attrmask = nfs4lib.list2attrmask([FATTR4_LINK_SUPPORT])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_bool(FALSE)
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testInvalidAttr(self):
        """SETATTR with invalid attribute data should return NFS4ERR_BADXDR

        Covered invalid equivalence classes: 41

        Comments: This testcase try to set FATTR4_MODE but does not send any
        mode data. The server should return NFS4ERR_BADXDR. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")
        attrmask = nfs4lib.list2attrmask([FATTR4_MODE])
        attr_vals = ""
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)

        self.assert_status(res, [NFS4ERR_BADXDR])

    def testNonUTF8(self):
        """SETATTR(FATTR4_MIMETYPE) with non-utf8 string should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 42

        """
        for name in self.get_invalid_utf8strings():
            lookupops = self.ncl.lookup_path(self.regfile)
            operations = [self.putrootfhop] + lookupops
            
            stateid = stateid4(self.ncl, 0, "")
            # Create attribute
            attrmask = nfs4lib.list2attrmask([FATTR4_MIMETYPE])
            dummy_ncl = nfs4lib.DummyNcl()
            dummy_ncl.packer.pack_utf8string(name)
            attr_vals = dummy_ncl.packer.get_buffer()
            obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
            # Setattr operation
            operations.append(self.ncl.setattr_op(stateid, obj_attributes))

            res = self.do_compound(operations)
            
            if self._check_notsupp(res): return

            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def _settime(self, dummy_ncl, time):
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")
        
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_MODIFY_SET])
        settime = settime4(dummy_ncl, set_it=SET_TO_CLIENT_TIME4, time=time)
        settime.pack()
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)
        return res
    
    def testInvalidTime(self):
        """SETATTR(FATTR4_TIME_MODIFY_SET) with invalid nseconds

        Extra test

        nseconds larger than 999999999 are considered invalid.
        SETATTR(FATTR4_TIME_MODIFY_SET) should return NFS4ERR_INVAL on
        such values. 
        """
        dummy_ncl = nfs4lib.DummyNcl()

        # First, try to set the date to 900 000 000 = 1998-07-09
        # to check if setting time_modify is possible at all. 
        time = nfstime4(dummy_ncl, seconds=500000000, nseconds=0)
        res = self._settime(dummy_ncl, time)
        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("Attribute time_modify_set is not supported, "
                              "skipping test")
            return

        # If servers supports the attribute but does not accept the 
        # date 1998-07-09, consider it broken. 
        self.assert_OK(res)

        # Ok, lets try nseconds = 1 000 000 000
        dummy_ncl = nfs4lib.DummyNcl()
        time = nfstime4(dummy_ncl, seconds=500000000, nseconds=int(1E9))
        res = self._settime(dummy_ncl, time)
        self.assert_status(res, [NFS4ERR_INVAL])
