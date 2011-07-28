from base_st_classes import *

class CreateSuite(NFSSuite):
    """Test operation 6: CREATE

    FIXME: Add attribute directory and named attribute testing.

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: type
        Valid equivalence classes:
            link(20)
            blockdev(21)
            chardev(22)
            socket(23)
            FIFO(24)
            directory(25)
        Invalid equivalence classes:
            regular file(26)
    Input Condition: name
        Valid equivalence classes:
            legal name(30)
        Invalid equivalence classes:
            zero length(31)
    Input Condition: fattr.attrmask
        Valid equivalence classes:
            valid attrmask(40)
        Invalid equivalence classes:
            invalid attrmask(41) (FATTR4_*_SET)
    Input Condition: fattr.attr_vals
        Valid equivalence classes:
            valid attribute value(50)
        Invalid equivalence classes:
            valid attribute value(51)

    """
    __pychecker__ = 'no-classattr'
    
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    #
    # Testcases covering valid equivalence classes.
    #
    def testLink(self):
        """CREATE (symbolic) link

        Covered valid equivalence classes: 10, 20, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4LNK, linkdata="/etc/X11")

        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("links not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testBlock(self):
        """CREATE a block device

        Covered valid equivalence classes: 10, 21, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        
        devdata = specdata4(self.ncl, 1, 2)
        objtype = createtype4(self.ncl, type=NF4BLK, devdata=devdata)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        # FIXME: Maybe try to create block and character devices as root.
        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("blocks devices not supported")
        elif res.status == NFS4ERR_PERM:
            self.info_message("not permitted")
        else:
            self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE, NFS4ERR_PERM])

    def testChar(self):
        """CREATE a character device

        Covered valid equivalence classes: 10, 22, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        devdata = specdata4(self.ncl, 1, 2)
        objtype = createtype4(self.ncl, type=NF4CHR, devdata=devdata)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("character devices not supported")
        elif res.status == NFS4ERR_PERM:
            self.info_message("not permitted")
        else:
            self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE, NFS4ERR_PERM])

    def testSocket(self):
        """CREATE a socket

        Covered valid equivalence classes: 10, 23, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4SOCK)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("sockets not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testFIFO(self):
        """CREATE a FIFO

        Covered valid equivalence classes: 10, 24, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4FIFO)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("FIFOs not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testDir(self):
        """CREATE a directory

        Covered valid equivalence classes: 10, 25, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("directories not supported!")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """CREATE should fail with NFS4ERR_NOTDIR if (cfh) is not dir

        Covered invalid equivalence classes: 11
        """
        if not self.remove_object(): return
        lookupops = self.ncl.lookup_path(self.regfile)
        
        operations = [self.putrootfhop] + lookupops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """CREATE should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 12
        """
        if not self.remove_object(): return
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)

        res = self.do_compound([createop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testZeroLengthName(self):
        """CREATE with zero length name should fail

        Covered invalid equivalence classes: 31
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, "")
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testRegularFile(self):
        """CREATE should fail with NFS4ERR_INVAL for regular files

        Covered invalid equivalence classes: 26
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        # nfs4types.createtype4 does not allow packing invalid types
        class custom_createtype4(createtype4):
            def pack(self, dummy=None):
                assert_not_none(self, self.type)
                self.packer.pack_nfs_ftype4(self.type)
            
        objtype = custom_createtype4(self.ncl, type=NF4REG)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_BADXDR])

    def testInvalidAttrmask(self):
        """CREATE should fail with NFS4ERR_INVAL on invalid attrmask

        Covered invalid equivalence classes: 41

        Comments: We are using a read-only attribute on CREATE, which
        should return NFS4ERR_INVAL. 
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        objtype = createtype4(self.ncl, type=NF4DIR)

        attrmask = nfs4lib.list2attrmask([FATTR4_LINK_SUPPORT])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_bool(TRUE)
        attr_vals = dummy_ncl.packer.get_buffer()
        createattrs = fattr4(self.ncl, attrmask, attr_vals)
        
        createop = self.ncl.create_op(objtype, self.obj_name, createattrs)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testInvalidAttributes(self):
        """CREATE should fail with NFS4ERR_XDR on invalid attr_vals

        Covered invalid equivalence classes: 51

        Comments: BADXDR should take precedence over NOTSUPP; BADXDR
        should be returned even if the server does not support the attribute
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        objtype = createtype4(self.ncl, type=NF4DIR)

        attrmask = nfs4lib.list2attrmask([FATTR4_ARCHIVE])
        # We use a short buffer, to trigger BADXDR. 
        attr_vals = ""
        createattrs = fattr4(self.ncl, attrmask, attr_vals)
        
        createop = self.ncl.create_op(objtype, self.obj_name, createattrs)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_BADXDR])

    #
    # Extra tests.
    #
    def _do_create(self, name):
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])

    def testDots(self):
        """CREATE with . or .. should succeed or return NFS4ERR_INVAL

        Extra test

        Servers supporting . and .. in file names should return NFS4_OK. Others
        should return NFS4ERR_INVAL. NFS4ERR_EXIST should not be returned.
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._do_create(testname)

        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._do_create(testname)

    def testSlash(self):
        """CREATE WITH "/" in filename should succeed or return NFS4ERR_INVAL

        Extra test

        Make sure / in file names are not treated as directory
        separator. Servers supporting "/" in file names should return
        NFS4_OK. Others should return NFS4ERR_INVAL. NFS4ERR_EXIST
        should not be returned.
        """
        # Great idea. Try this:
        # tmp
        # |-- "gazonk/foo.c"
        # `-- gazonk
        #     `--foo.c
        #
        # /tmp/gazonk/foo.c is created by test_tree.py. 
        
        testname = "gazonk/foo.c"
        if not self.make_sure_nonexistent(testname): return
        # Try to create "gazonk/foo.c"
        self._do_create(testname)

    def testNamingPolicy(self):
        """CREATE should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_create) = self.try_file_names(creator=self.create_via_create)
            self.info_message("Rejected file names by CREATE: %s" \
                              % repr(rejected_names_create))
            
            
            self.failIf(rejected_names_open != rejected_names_create,
                        "CREATE does not obey OPEN naming policy")
        except SkipException, e:
            print e
