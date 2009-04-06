from base_st_classes import *

class ReadSuite(NFSSuite):
    """Test operation 25: READ

##     FIXME: Adapt to protocol changes. 

##     FIXME: Add attribute directory and named attribute testing.
##     FIXME: Try reading a locked file. 

##     Equivalence partitioning:

##     Input Condition: current filehandle
##         Valid equivalence classes:
##             file(1)
##             named attribute(2)
##         Invalid equivalence classes:
##             dir(3)
##             special device files(4)
##             invalid filehandle(10)
##     Input Condition: stateid
##         Valid equivalence classes:
##             all bits zero(17)
##             all bits one(18)
##             valid stateid from open(19)
##         Invalid equivalence classes:
##             invalid stateid(20)
##     Input Condition: offset
##         Valid equivalence classes:2
##             zero(11)
##             less than file size(12)
##             greater than or equal to file size(13)
##         Invalid equivalence classes:
##             -
##     Input Condition: count
##         Valid equivalence classes:
##             zero(14)
##             one(15)
##             greater than one(16)
##         Invalid equivalence classes:
##             -
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testSimpleRead(self):
        """READ from regular file with stateid=zeros

        Covered valid equivalence classes: 1, 11, 14, 17
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")
        operations.append(self.ncl.read(offset=0, count=0, stateid=stateid))
        res = self.do_compound(operations)
        self.assert_OK(res)
    def testReadAttr(self):
        """READ from named attribute

        Covered valid equivalence classes: 2, 12, 16, 18
        """
        # FIXME: Implement rest of testcase.
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testStateidOne(self):
        """READ with offset=2, count=1, stateid=ones

        Covered valid equivalence classes: 1, 12, 15, 18
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, nfs4lib.long2opaque(0xffffffffffffffffffffffffL))
        readop = self.ncl.read(offset=2, count=1, stateid=stateid)
        operations.append(readop)
        
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testWithOpen(self):
        """READ with offset>size, count=5, stateid from OPEN

        Covered valid equivalence classes: 1, 13, 16, 19
        """
        self.init_connection()

        
        lookupops = self.ncl.lookup_path(self.regfile[:-1])
        operations = [self.putrootfhop] + lookupops

        # OPEN
        operations.append(self.ncl.open(file=self.regfile[-1]))
        operations.append(self.ncl.getfh_op())
        res = self.do_compound(operations)
        self.assert_OK(res)
        # [-2] is the OPEN operation
        stateid = res.resarray[-2].arm.arm.stateid
        # [-1] is the GETFH operation
        fh = res.resarray[-1].arm.arm.object

        # README is 36 bytes. Lets use 1000 as offset.
        putfhop = self.ncl.putfh_op(fh)
        readop = self.ncl.read(offset=1000, count=5, stateid=stateid)

        res = self.do_compound([putfhop, readop])
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testDirFh(self):
        """READ with (cfh)=directory should return NFS4ERR_ISDIR

        Covered invalid equivalence classes: 3
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        stateid = stateid4(self.ncl, 0, "")
        readop = self.ncl.read(stateid)
        operations.append(readop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ISDIR])

    def testSpecials(self):
        """READ with (cfh)=device files should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 4
        """
        for pathcomps in [self.blockfile,
                          self.charfile,
                          self.linkfile,
                          self.socketfile,
                          self.fifofile]:
            lookupop = self.ncl.lookup_op(pathcomps)
            readop = self.ncl.read()

            res = self.do_compound([self.putrootfhop, lookupop, readop])

            if res.status != NFS4ERR_INVAL:
                self.info_message("READ on %s dit not return NFS4ERR_INVAL" % name)
            
            self.assert_status(res, [NFS4ERR_INVAL])

    def testNoFh(self):
        """READ without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 10
        """
        readop = self.ncl.read()
        res = self.do_compound([readop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testInvalidStateid(self):
        """READ with a (guessed) invalid stateid should return NFS4ERR_STALE_STATEID

        Covered invalid equivalence classes: 20
        """
        # FIXME
        stateid = stateid4(self.ncl, 0, "")
        readop = self.ncl.read(stateid=0x123456789L)
        res = self.do_compound([readop])
        self.assert_status(res, [NFS4ERR_STALE_STATEID])
